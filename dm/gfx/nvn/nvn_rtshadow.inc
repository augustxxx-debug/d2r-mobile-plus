// ---------- 光追阴影（第一阶段）：用 Metal 硬件光追算太阳方向的逐像素可见度，替掉游戏自己的两级级联阴影图 ----------
// 怎么接（都在翻译层，不改游戏）：
//   1. 阴影通道（无颜色附件、方形 2D 深度）的每次绘制 = 一个遮挡体实例：网格按（顶点流地址、步长、格式、索引缓冲、数量、基顶点 + 数据指纹）做键缓存 BLAS，
//      首次见到时用计算核把属性 0 的位置解码成 float3（SNORM16x4 · exp2(w·13)，与 vs_models_v 的 MSL 一致）再建；cb0 的物体→世界 3x4 进 TLAS 实例；这些绘制本身不再编码（阴影图保持清空 = 游戏采样全亮）。
//   2. 深度预渲染（无颜色、主深度）的绘制里取 cb3 +336 的相机视投影；阴影通道的 cb3 +336 是光源视投影，其 z 列（取朝天那一侧）= 太阳方向。
//   3. 主不透明通道（RG11B10F + Depth32Float_Stencil8，宽 ≥ 500、非方形）的设目标处：建 TLAS、按主深度重建世界坐标、每像素向太阳发一条遮挡射线，写 R8 遮罩（3D 尺寸）。
//   4. 主通道之后第一次再挂 HDR 的设目标处：全屏一趟把 HDR 乘上 amb + (1 − amb) · 遮罩（第一阶段的合成方式；第二阶段改成片元 MSL 的 _rt 变体直接读遮罩，这一趟就撤）。
// 门槛：supportsRaytracing 且 GPU 家族 ≥ Apple9 且机型白名单（用户要求三重判定）；任一不满足 = 整个模块不建任何对象、每次调用立即返回。开关：DM_RT_SHADOW=0/1 钉住，否则 dm_rt_shadow_setting()。
// 蒙皮角色（rt_is_skin 的 10 个顶点着色器）第一阶段按绑定姿势 + 世界矩阵进 TLAS（投影形状不对，第二阶段改成位置导出变体 + refit）。只编 Metal 4 路径；DM_NVN_M3 下模块为空。
// ---------- Ray-traced shadows (phase 1): per-pixel sun visibility via Metal hardware ray tracing, replacing the game's two cascaded shadow maps ----------
// How it hooks in (translation layer only, the game is untouched):
//   1. Every draw of a shadow pass (no color attachment, square 2D depth) = one occluder instance: meshes are cached as BLAS keyed by (vertex stream address, stride, format, index buffer, count, base vertex + data fingerprint);
//      on first sight a compute kernel decodes attribute 0 into float3 (SNORM16x4 · exp2(w·13), matching the vs_models_v MSL) before the build; cb0's object→world 3x4 goes into the TLAS instance; the draws themselves are no longer encoded (the shadow map stays cleared = the game samples fully lit).
//   2. The camera view-projection is read from cb3 +336 of the depth-prepass draws (no color, main depth); cb3 +336 of the shadow pass is the light view-projection, whose z column (the side pointing up) = sun direction.
//   3. At the set-target of the main opaque pass (RG11B10F + Depth32Float_Stencil8, width ≥ 500, non-square): build the TLAS, reconstruct world positions from the main depth, shoot one occlusion ray per pixel toward the sun, write an R8 mask (3D size).
//   4. At the first set-target after the main pass that attaches the HDR again: a fullscreen pass multiplies the HDR by amb + (1 − amb) · mask (phase-1 compositing; phase 2 switches to _rt fragment MSL variants reading the mask directly and this pass goes away).
// Gate: supportsRaytracing and GPU family ≥ Apple9 and a model allowlist (triple check requested by the user); if any fails the module creates nothing and every call returns immediately. Switch: DM_RT_SHADOW=0/1 pins it, otherwise dm_rt_shadow_setting().
// Skinned units (the 10 vertex shaders of rt_is_skin) enter the TLAS in phase 1 as bind pose + world matrix (wrong silhouette; phase 2 uses a position-export variant + refit). Metal 4 path only; under DM_NVN_M3 the module is empty.
__attribute__((weak)) int dm_rt_shadow_setting(void) { return 0; }   // 宿主设置页提供强定义；缺省关 | The host settings page provides the strong definition; off by default
__attribute__((weak)) float dm_rt_shadow_ambient(void) { return 0.45f; }   // 第一阶段合成的环境项（阴影里保留的亮度比例） | Ambient term of the phase-1 compositing (brightness kept inside shadow)
static int rt_is_skin(void);
static void rx_inv4(const double *m, double *o);   // nvn_rtex.inc（在本文件之后包含） | nvn_rtex.inc (included after this file)
#include <sys/sysctl.h>
#if DM_NVN_M3
static int rts_on(void) { return 0; }
static int rts_draw(NvProg *pr, const p_draw *dw, const p_draw_idx *di, int prim) { (void)pr; (void)dw; (void)di; (void)prim; return 0; }
static void rts_set_rt(const p_set_rt *r) { (void)r; }
static void rts_select_fs(NvProg *pr) { (void)pr; }
static uint64_t rts_mask_rid(void) { return 0; }
static id<MTLTexture> rts_mask_tex(void) { return nil; }
int mtl_rt_shadow_supported(void) { return 0; }
#else
#define RTS_MAX_INST 4096
#define RTS_BUILDS_PER_FRAME 12   // 每帧最多新建这么多 BLAS（新网格首次出现时），其余下一帧再建，避免一次卡顿 | At most this many new BLAS builds per frame (first appearance of new meshes); the rest wait for the next frame to avoid a hitch
typedef struct { uint64_t key; id<MTLAccelerationStructure> as; id<MTLBuffer> pos, ib; uint64_t last_frame; uint32_t tris, nverts, isz; int built; } rts_mesh;
static struct {
    int on, gate, dbg, failed; uint64_t frame;
    nvn_hmap meshes; int nmesh;
    MTLIndirectAccelerationStructureInstanceDescriptor inst[RTS_MAX_INST]; uint64_t inst_tag[RTS_MAX_INST]; int ninst, ndup, nskip_fmt, nskin;
    rts_mesh *pending[RTS_BUILDS_PER_FRAME]; struct { uint64_t gpu; uint32_t n, stride, fmt, base; } pend_src[RTS_BUILDS_PER_FRAME]; int npend;
    float cam[16], lvp[16]; int have_cam, have_sun; float sun[3];
    int main_seen, mask_done, overlay_done; uint64_t hdr_ser, ds_ser;
    id<MTLComputePipelineState> ps_decode, ps_trace; id<MTLRenderPipelineState> ps_overlay; id<MTL4ArgumentTable> at_decode, at_trace, at_overlay;
    id<MTLBuffer> instbuf[3], kbuf[3], dbuf[3]; int ri;
    id<MTLAccelerationStructure> tlas; id<MTLBuffer> scratch; NSUInteger scratch_len;
    id<MTLTexture> mask, white; int n_rt_loaded, n_rt_missing;
    uint64_t n_frames, n_builds, n_traced, n_skipped_draws; double build_tris;
} g_rts = { .on = -1, .meshes = {.klen = 8} };
static const char *k_rts_msl =
"#include <metal_stdlib>\n#include <metal_raytracing>\nusing namespace metal; using namespace metal::raytracing;\n"
// 位置解码：fmt 0x2b SNORM16x4、0x2e FLOAT4、0x29 HALF4 都是 xyz · exp2(w · 13)；0x22 FLOAT3 原样 | Position decode
"kernel void rts_decode(device const uchar *src [[buffer(0)]], device packed_float3 *dst [[buffer(1)]], constant uint4 &k [[buffer(2)]], uint i [[thread_position_in_grid]]) {\n"
"  if (i >= k.x) return; device const uchar *p = src + (ulong)(k.w + i) * k.y; float4 f;\n"
"  if (k.z == 0x2b) { device const short *s = (device const short *)p; f = float4(s[0], s[1], s[2], s[3]) / 32767.0; }\n"
"  else if (k.z == 0x2e) { device const float *s = (device const float *)p; f = float4(s[0], s[1], s[2], s[3]); }\n"
"  else if (k.z == 0x29) { device const half *s = (device const half *)p; f = float4(s[0], s[1], s[2], s[3]); }\n"
"  else { device const float *s = (device const float *)p; dst[i] = packed_float3(s[0], s[1], s[2]); return; }\n"
"  dst[i] = packed_float3(f.xyz * exp2(f.w * 13.0)); }\n"
"struct K { float4x4 invVP; float4 sun; uint2 size; float bias; float pad; };\n"
"kernel void rts_trace(depth2d<float, access::read> depth [[texture(0)]], texture2d<float, access::write> mask [[texture(1)]], instance_acceleration_structure as [[buffer(0)]], constant K &k [[buffer(1)]], uint2 g [[thread_position_in_grid]]) {\n"
"  if (g.x >= k.size.x || g.y >= k.size.y) return; float z = depth.read(g);\n"
"  if (z >= 1.0) { mask.write(float4(1), g); return; }\n"   // 远平面 = 没画东西（天空） | far plane = nothing drawn (sky)
"  float2 ndc = float2((float(g.x) + 0.5) / float(k.size.x) * 2.0 - 1.0, 1.0 - (float(g.y) + 0.5) / float(k.size.y) * 2.0);\n"
"  float4 w = k.invVP * float4(ndc, z, 1.0); float3 p = w.xyz / w.w;\n"
"  ray r; r.origin = p + k.sun.xyz * k.bias; r.direction = k.sun.xyz; r.min_distance = 0.0; r.max_distance = 20000.0;\n"
"  intersector<instancing> is; is.accept_any_intersection(true); is.assume_geometry_type(geometry_type::triangle); is.force_opacity(forced_opacity::opaque);\n"
"  intersection_result<instancing> h = is.intersect(r, as, 0xffu);\n"
"  mask.write(float4(h.type == intersection_type::none ? 1.0 : 0.0), g); }\n"
"struct VO { float4 p [[position]]; };\n"
"vertex VO rts_vs(uint id [[vertex_id]]) { float2 uv = float2((id << 1) & 2, id & 2); VO o; o.p = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1); return o; }\n"
"fragment float4 rts_fs(VO in [[stage_in]], texture2d<float> mask [[texture(0)]], constant float4 &a [[buffer(0)]]) {\n"
"  float m = mask.read(uint2(in.p.xy)).r; float f = a.x + (1.0 - a.x) * m; return float4(f, f, f, 1.0); }\n";   // 乘到 HDR 上（混合：目标 × 源） | multiplied onto the HDR (blend: destination × source)
int mtl_rt_shadow_supported(void) {   // 宿主设置页用：这台机器能不能显示「光追阴影」 | For the host settings page: whether this machine may show "ray-traced shadows"
    static int v = -1; if (v >= 0) return v;
    v = 0; if (!g_dev) return 0;
    int rt = [g_dev supportsRaytracing], fam = [g_dev supportsFamily:MTLGPUFamilyApple9];
    char hw[64] = "?"; size_t hn = sizeof hw; sysctlbyname("hw.machine", hw, &hn, NULL, 0); int model = 0, major = 0, minor = 0;
    if (sscanf(hw, "iPhone%d,%d", &major, &minor) == 2) model = major >= 17 || (major == 16 && (minor == 1 || minor == 2));   // iPhone 15 Pro / Pro Max 起（A17 Pro） | iPhone 15 Pro / Pro Max and later (A17 Pro)
    else if (sscanf(hw, "iPad%d,%d", &major, &minor) == 2) model = major >= 16;   // iPad Pro M4 起 | iPad Pro M4 and later
    else model = 1;   // Mac 上的 iOS 包（hw.machine 为 Mac 型号）：只看 GPU 家族 | iOS app on Mac (hw.machine is a Mac model): GPU family alone decides
    v = rt && fam && model;
    MLOG("光追阴影：机器判定 %s（supportsRaytracing %d、GPU 家族 Apple9 %d、机型 %s %s）", v ? "可用" : "不可用", rt, fam, hw, model ? "在白名单" : "不在白名单");
    return v;
}
static int rts_on(void) {
    if (g_rts.on < 0) {
        const char *e = getenv("DM_RT_SHADOW"); g_rts.gate = mtl_rt_shadow_supported(); g_rts.dbg = getenv("DM_RT_SHADOW_DBG") ? atoi(getenv("DM_RT_SHADOW_DBG")) : 0;
        g_rts.on = e ? atoi(e) != 0 : -2;
        // 前缀与宿主 dm_rt_shadow_setting 的「光追阴影：生效」同一个：钉住时不走宿主那条，分享诊断日志的摘要照样取得到 | same prefix as "光追阴影：生效" in the host dm_rt_shadow_setting: when pinned that path is skipped, and the share-diagnostics summary still finds the line
        if (e) MLOG("光追阴影：生效 %s（测试开关 DM_RT_SHADOW=%s 钉住%s）", atoi(e) && g_rts.gate ? "开" : "关", e, g_rts.gate ? "" : "，机器不可用，仍不生效");
    }
    if (!g_rts.gate || g_rts.failed) return 0;
    return g_rts.on == -2 ? dm_rt_shadow_setting() != 0 : g_rts.on;
}
static const uint8_t *rts_mem(uint64_t addr, uint64_t len) { void *m; uint64_t o; if (!addr || !len || !mtl_addr(addr, len, &m, &o)) return NULL; const uint8_t *b = [(__bridge id<MTLBuffer>)m contents]; return b ? b + o : NULL; }
static uint64_t rts_fnv(uint64_t h, const void *p, size_t n) { const uint8_t *b = p; for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 1099511628211ull; return h; }
static int rts_setup(void) {
    if (g_rts.ps_trace) return 1;
    if (g_rts.failed) return 0;
    NSError *e = nil; id<MTLLibrary> lib = [g_dev newLibraryWithSource:@(k_rts_msl) options:nil error:&e];
    if (!lib) { g_rts.failed = 1; MLOG("【红】光追阴影：着色器编译失败 %s", e.localizedDescription.UTF8String); return 0; }
    g_rts.ps_decode = [g_dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"rts_decode"] error:&e];
    g_rts.ps_trace = g_rts.ps_decode ? [g_dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"rts_trace"] error:&e] : nil;
    MTL4ArgumentTableDescriptor *ad = [MTL4ArgumentTableDescriptor new]; ad.maxBufferBindCount = 3; ad.maxTextureBindCount = 2;
    g_rts.at_decode = [g_dev newArgumentTableWithDescriptor:ad error:&e]; g_rts.at_trace = [g_dev newArgumentTableWithDescriptor:ad error:&e]; g_rts.at_overlay = [g_dev newArgumentTableWithDescriptor:ad error:&e];
    for (int i = 0; i < 3; i++) {
        g_rts.instbuf[i] = [g_dev newBufferWithLength:RTS_MAX_INST * sizeof(MTLIndirectAccelerationStructureInstanceDescriptor) options:MTLResourceStorageModeShared];
        g_rts.kbuf[i] = [g_dev newBufferWithLength:256 options:MTLResourceStorageModeShared]; g_rts.dbuf[i] = [g_dev newBufferWithLength:64 options:MTLResourceStorageModeShared];
        if (g_rts.instbuf[i]) res_add(g_rts.instbuf[i]); if (g_rts.kbuf[i]) res_add(g_rts.kbuf[i]); if (g_rts.dbuf[i]) res_add(g_rts.dbuf[i]);
    }
    // TLAS 按最大实例数一次建够；暂存缓冲按 TLAS 的需求起步，BLAS 需要更大时再换 | The TLAS is created once for the maximum instance count; the scratch buffer starts at the TLAS's need and is replaced when a BLAS needs more
    MTL4InstanceAccelerationStructureDescriptor *td = [MTL4InstanceAccelerationStructureDescriptor new];
    td.instanceDescriptorType = MTLAccelerationStructureInstanceDescriptorTypeIndirect; td.instanceCount = RTS_MAX_INST; td.instanceDescriptorStride = sizeof(MTLIndirectAccelerationStructureInstanceDescriptor);
    td.instanceDescriptorBuffer = (MTL4BufferRange){g_rts.instbuf[0].gpuAddress, g_rts.instbuf[0].length};
    MTLAccelerationStructureSizes sz = [g_dev accelerationStructureSizesWithDescriptor:td];
    g_rts.tlas = [g_dev newAccelerationStructureWithSize:sz.accelerationStructureSize]; if (g_rts.tlas) res_add(g_rts.tlas);
    g_rts.scratch_len = MAX(sz.buildScratchBufferSize, (NSUInteger)(4 << 20)); g_rts.scratch = [g_dev newBufferWithLength:g_rts.scratch_len options:MTLResourceStorageModePrivate]; if (g_rts.scratch) res_add(g_rts.scratch);
    { MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Unorm width:1 height:1 mipmapped:NO]; d.usage = MTLTextureUsageShaderRead; d.storageMode = MTLStorageModeShared;
      g_rts.white = [g_dev newTextureWithDescriptor:d]; uint8_t one = 255; if (g_rts.white) { [g_rts.white replaceRegion:MTLRegionMake2D(0, 0, 1, 1) mipmapLevel:0 withBytes:&one bytesPerRow:1]; res_add(g_rts.white); } }
    if (!g_rts.ps_decode || !g_rts.ps_trace || !g_rts.at_decode || !g_rts.at_trace || !g_rts.at_overlay || !g_rts.tlas || !g_rts.scratch || !g_rts.white || !g_rts.instbuf[2] || !g_rts.kbuf[2] || !g_rts.dbuf[2]) {
        g_rts.failed = 1; MLOG("【红】光追阴影：管线 / 加速结构建不出 %s（退回原版阴影图）", e.localizedDescription.UTF8String); return 0; }
    MLOG("光追阴影：建好（TLAS %lu KiB、暂存 %lu KiB、最多 %d 实例）", (unsigned long)(sz.accelerationStructureSize >> 10), (unsigned long)(g_rts.scratch_len >> 10), RTS_MAX_INST);
    return 1;
}
static int rts_overlay_setup(MTLPixelFormat fc) {
    static MTLPixelFormat cur; if (g_rts.ps_overlay && cur == fc) return 1;
    NSError *e = nil; id<MTLLibrary> lib = [g_dev newLibraryWithSource:@(k_rts_msl) options:nil error:&e]; if (!lib) return 0;
    MTLRenderPipelineDescriptor *pd = [MTLRenderPipelineDescriptor new]; pd.vertexFunction = [lib newFunctionWithName:@"rts_vs"]; pd.fragmentFunction = [lib newFunctionWithName:@"rts_fs"];
    pd.colorAttachments[0].pixelFormat = fc; pd.colorAttachments[0].blendingEnabled = YES;
    pd.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorZero; pd.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorSourceColor;
    pd.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorZero; pd.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOne;
    g_rts.ps_overlay = [g_dev newRenderPipelineStateWithDescriptor:pd error:&e]; cur = fc;
    if (!g_rts.ps_overlay) MLOG("【红】光追阴影：合成管线建不出 %s", e.localizedDescription.UTF8String);
    return g_rts.ps_overlay != nil;
}
static void rts_frame_begin(uint64_t f) {
    if (g_rts.n_frames && g_rts.n_frames % 600 == 0) MLOG("光追阴影：第 %llu 帧 实例 %d（重复 %d、蒙皮 %d、格式不认 %d）、网格缓存 %d、累计建 %llu 个 BLAS（%.1f 万三角形）、追踪 %llu 帧、跳过阴影图绘制 %llu 次、_rt 变体 %d 个（缺 %d）",
        (unsigned long long)g_rts.n_frames, g_rts.ninst, g_rts.ndup, g_rts.nskin, g_rts.nskip_fmt, g_rts.nmesh, (unsigned long long)g_rts.n_builds, g_rts.build_tris / 1e4, (unsigned long long)g_rts.n_traced, (unsigned long long)g_rts.n_skipped_draws, g_rts.n_rt_loaded, g_rts.n_rt_missing);
    g_rts.frame = f; g_rts.n_frames++; g_rts.ninst = g_rts.ndup = g_rts.nskin = g_rts.nskip_fmt = 0; g_rts.have_cam = g_rts.have_sun = 0; g_rts.main_seen = g_rts.mask_done = g_rts.overlay_done = 0;
    // 网格缓存淘汰：600 帧没用过的释放 | Mesh cache eviction: release those unused for 600 frames
    if (g_rts.n_frames % 120 == 0 && g_rts.nmesh) { uint64_t dead[64]; int nd = 0;
        for (uint32_t i = 0; i < g_rts.meshes.cap && nd < 64; i++) { rts_mesh *m = g_rts.meshes.vals[i]; if (m && m->built && f - m->last_frame > 600) dead[nd++] = m->key; }
        for (int i = 0; i < nd; i++) { rts_mesh *m = nvn_hm_del(&g_rts.meshes, &dead[i]); if (!m) continue; if (m->as) { res_remove(m->as); defer_obj_release(m->as); } if (m->pos) { res_remove(m->pos); defer_obj_release(m->pos); } if (m->ib) { res_remove(m->ib); defer_obj_release(m->ib); } free(m); g_rts.nmesh--; } }
}
// 阴影通道的一次绘制 → 网格（首次：登记待建）+ 实例。返回 1 = 这次绘制不要编码 | One draw of the shadow pass → mesh (first time: queued for build) + instance. Returns 1 = do not encode this draw
static int rts_draw(NvProg *pr, const p_draw *dw, const p_draw_idx *di, int prim) {
    if (!rts_on() || !pr || !pr->sh[0]) return 0;
    uint64_t f = atomic_load(&g_cur_frame); if (f != g_rts.frame) rts_frame_begin(f);
    int shadow = !R.rt.n && R.rt.depth && R.rt.depth->d.width == R.rt.depth->d.height;
    if (shadow) { id<MTLTexture> dt = tex_mtl(R.rt.depth); if (!dt || dt.textureType == MTLTextureTypeCube || dt.textureType == MTLTextureTypeCubeArray) return 0; }   // 点光立方体阴影照旧 | point-light cube shadows unchanged
    const p_bind_buf *c3 = &S.ubo[0][3]; int has_c3 = 0;
    { NvShader *vs = pr->sh[0]; for (int i = 0; i < vs->rf.nb; i++) if (vs->rf.b[i].kind == NVN_RB_CBUF && vs->rf.b[i].reg == 3) has_c3 = 1; }
    if (!shadow) {   // 深度预渲染（无颜色、非方形深度）：记相机 | depth prepass (no color, non-square depth): note the camera
        if (!R.rt.n && R.rt.depth && has_c3 && !g_rts.main_seen && c3->addr && c3->size >= 400) { const uint8_t *b = rts_mem(c3->addr, 400); if (b) { memcpy(g_rts.cam, b + 336, 64); g_rts.have_cam = 1; } }
        return 0;
    }
    if (!rts_setup()) return 0;
    if (!g_rts.have_sun && has_c3 && c3->addr && c3->size >= 400) { const uint8_t *b = rts_mem(c3->addr, 400); if (b) { memcpy(g_rts.lvp, b + 336, 64); float d[3] = {g_rts.lvp[2], g_rts.lvp[6], g_rts.lvp[10]}; float n = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (n > 0) { float s = (d[1] > 0 ? 1.f : -1.f) / n; g_rts.sun[0] = d[0] * s; g_rts.sun[1] = d[1] * s; g_rts.sun[2] = d[2] * s; g_rts.have_sun = 1; } } }
    g_rts.n_skipped_draws++;
    if (prim != 4) return 1;   // 只认三角形列表（抓帧里阴影通道全是）；别的图元跳过绘制但不进加速结构 | triangle lists only (all shadow-pass draws in the capture); other primitives skip the draw without entering the acceleration structure
    int n = dw ? dw->count : di->count; if (n < 3) return 1;
    if (S.nattr < 1 || S.attr[0].stream >= 16) return 1;
    va_st at = S.attr[0]; vs_st sm = S.stream[at.stream]; NVNbufferRange vb = S.vbuf[at.stream];
    if (!sm.stride || sm.divisor || !vb.address || (at.fmt != 0x2b && at.fmt != 0x2e && at.fmt != 0x29 && at.fmt != 0x22)) { g_rts.nskip_fmt++; return 1; }
    if (di && di->itype != 1 && di->itype != 2) { g_rts.nskip_fmt++; return 1; }
    const p_bind_buf *c0 = &S.ubo[0][0]; const uint8_t *m0 = c0->addr && c0->size >= 64 ? rts_mem(c0->addr, 64) : NULL; if (!m0) return 1;
    // 网格键：布局 + 首 32 字节索引与顶点的指纹（池内存会被别的网格复用） | Mesh key: layout + fingerprint of the first 32 bytes of indices and vertices (pool memory gets reused by other meshes)
    uint64_t key = 1469598103934665603ull; uint64_t lay[6] = {vb.address + at.offset, sm.stride, at.fmt, dw ? 0 : di->ibuf, (uint64_t)n, dw ? (uint64_t)dw->first : (uint64_t)(int64_t)di->base_vertex};
    key = rts_fnv(key, lay, sizeof lay);
    int isz = di ? (di->itype == 1 ? 2 : 4) : 0; const uint8_t *ib = di ? rts_mem(di->ibuf, (uint64_t)n * isz) : NULL; if (di && !ib) return 1;
    if (ib) key = rts_fnv(key, ib, 32);
    int64_t vfirst = dw ? dw->first : di->base_vertex; const uint8_t *v0 = rts_mem(vb.address + (uint64_t)vfirst * sm.stride + at.offset, 32); if (v0) key = rts_fnv(key, v0, 32);
    rts_mesh *m = nvn_hm_get(&g_rts.meshes, &key);
    if (!m) {
        if (g_rts.npend >= RTS_BUILDS_PER_FRAME) return 1;   // 本帧配额用完：下一帧再建 | quota for this frame used up: build next frame
        uint32_t nv = (uint32_t)n; int64_t vbase = vfirst;
        if (ib) { uint32_t mx = 0; for (int k = 0; k < n; k++) { uint32_t v = isz == 2 ? ((const uint16_t *)ib)[k] : ((const uint32_t *)ib)[k]; if (v > mx) mx = v; } nv = mx + 1; }
        if (vbase < 0 || (uint64_t)(vbase + nv) * sm.stride + at.offset + 16 > vb.size + sm.stride) return 1;
        void *mb; uint64_t mo, vgpu; if (!mtl_gpu_addr(vb.address + at.offset, 16, &mb, &mo, &vgpu)) return 1;
        m = calloc(1, sizeof *m); m->key = key; m->nverts = nv; m->tris = (uint32_t)(n / 3); m->isz = (uint32_t)isz;
        m->pos = [g_dev newBufferWithLength:(NSUInteger)nv * 12 options:MTLResourceStorageModePrivate];
        if (ib) { m->ib = [g_dev newBufferWithBytes:ib length:(NSUInteger)n * isz options:MTLResourceStorageModeShared]; }   // 索引拷一份：游戏的索引内存会被复用 | copy the indices: the game's index memory gets reused
        MTL4PrimitiveAccelerationStructureDescriptor *pd = [MTL4PrimitiveAccelerationStructureDescriptor new]; MTL4AccelerationStructureTriangleGeometryDescriptor *gd = [MTL4AccelerationStructureTriangleGeometryDescriptor new];
        gd.vertexBuffer = (MTL4BufferRange){m->pos.gpuAddress, m->pos.length}; gd.vertexFormat = MTLAttributeFormatFloat3; gd.vertexStride = 12; gd.triangleCount = m->tris; gd.opaque = YES;
        if (m->ib) { gd.indexBuffer = (MTL4BufferRange){m->ib.gpuAddress, m->ib.length}; gd.indexType = isz == 2 ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32; }
        pd.geometryDescriptors = @[gd]; pd.usage = MTLAccelerationStructureUsageNone;
        MTLAccelerationStructureSizes sz = [g_dev accelerationStructureSizesWithDescriptor:pd];
        m->as = [g_dev newAccelerationStructureWithSize:sz.accelerationStructureSize];
        if (!m->pos || !m->as || (ib && !m->ib)) { if (m->pos) defer_obj_release(m->pos); if (m->as) defer_obj_release(m->as); if (m->ib) defer_obj_release(m->ib); free(m); return 1; }
        if (sz.buildScratchBufferSize > g_rts.scratch_len) { res_remove(g_rts.scratch); defer_obj_release(g_rts.scratch); g_rts.scratch_len = sz.buildScratchBufferSize * 2; g_rts.scratch = [g_dev newBufferWithLength:g_rts.scratch_len options:MTLResourceStorageModePrivate]; res_add(g_rts.scratch); }
        res_add(m->pos); res_add(m->as); if (m->ib) res_add(m->ib);
        nvn_hm_put(&g_rts.meshes, &key, m); g_rts.nmesh++;
        g_rts.pending[g_rts.npend] = m; g_rts.pend_src[g_rts.npend] = (typeof(g_rts.pend_src[0])){vgpu, nv, sm.stride, at.fmt, (uint32_t)vbase}; g_rts.npend++;
        (void)pd;   // 构建描述在 rts_build 里按网格重建（描述对象不保存） | the build descriptor is recreated per mesh in rts_build (descriptor objects are not kept)
    }
    m->last_frame = f;
    if (g_rts.ninst >= RTS_MAX_INST) return 1;
    const float *M = (const float *)m0; uint64_t tag = key ^ rts_fnv(1469598103934665603ull, m0, 48);
    for (int i = 0; i < g_rts.ninst; i++) if (g_rts.inst_tag[i] == tag) { g_rts.ndup++; return 1; }   // 两级级联画同一实例：只进一次 | both cascades draw the same instance: enter once
    if (rt_is_skin()) g_rts.nskin++;
    MTLIndirectAccelerationStructureInstanceDescriptor *d = &g_rts.inst[g_rts.ninst]; memset(d, 0, sizeof *d);
    for (int c = 0; c < 4; c++) { d->transformationMatrix.columns[c].x = M[c * 4 + 0]; d->transformationMatrix.columns[c].y = M[c * 4 + 1]; d->transformationMatrix.columns[c].z = M[c * 4 + 2]; }   // 行向量 3x4 的第 c 行 = 列主序的第 c 列 | row c of the row-vector 3x4 = column c in column-major
    d->options = MTLAccelerationStructureInstanceOptionOpaque; d->mask = 0xff; d->accelerationStructureID = m->as.gpuResourceID;
    g_rts.inst_tag[g_rts.ninst] = tag; g_rts.ninst++;
    return 1;
}
// 主通道设目标处：建待建的 BLAS、TLAS，追踪写遮罩 | At the main pass's set-target: build pending BLAS, the TLAS, trace into the mask
static void rts_build_and_trace(id<MTLTexture> depth) {
    if (!g_rts.ninst || !g_rts.have_cam || !g_rts.have_sun || !depth) { g_rts.npend = 0; return; }
    if (!g_rts.mask || g_rts.mask.width != depth.width || g_rts.mask.height != depth.height) {
        MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Unorm width:depth.width height:depth.height mipmapped:NO];
        d.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite; d.storageMode = MTLStorageModePrivate;
        if (g_rts.mask) { res_remove(g_rts.mask); defer_obj_release(g_rts.mask); } g_rts.mask = [g_dev newTextureWithDescriptor:d]; if (!g_rts.mask) { g_rts.failed = 1; return; } res_add(g_rts.mask);
        MLOG("光追阴影：遮罩 %lux%lu", (unsigned long)depth.width, (unsigned long)depth.height); }
    rp_end(); if (g_blit) { enc_b_end(g_blit); g_blit = nil; } ser_gate();
    int ri = g_rts.ri; g_rts.ri = (ri + 1) % 3;
    CENC ce = (CENC)enc_c_begin(NULL);
    // 1. 新网格：解码位置 → 建 BLAS（各步之间加编码器内屏障；暂存缓冲共用，所以逐个建） | 1. New meshes: decode positions → build BLAS (intra-encoder barriers between steps; the scratch buffer is shared, so build one at a time)
    if (g_rts.npend) {
        [ce setArgumentTable:g_rts.at_decode]; enc_c_pso(ce, g_rts.ps_decode);
        for (int i = 0; i < g_rts.npend; i++) { rts_mesh *m = g_rts.pending[i]; typeof(g_rts.pend_src[0]) *s = &g_rts.pend_src[i];
            id<MTLBuffer> kb = [g_dev newBufferWithLength:16 options:MTLResourceStorageModeShared]; uint32_t k4[4] = {s->n, s->stride, s->fmt, s->base}; memcpy(kb.contents, k4, 16); res_add(kb); defer_obj_release(kb);
            [g_rts.at_decode setAddress:s->gpu atIndex:0]; [g_rts.at_decode setAddress:m->pos.gpuAddress atIndex:1]; [g_rts.at_decode setAddress:kb.gpuAddress atIndex:2];
            enc_c_dispatch(ce, MTLSizeMake((s->n + 63) / 64, 1, 1), MTLSizeMake(64, 1, 1)); }
        [ce barrierAfterEncoderStages:MTLStageDispatch beforeEncoderStages:MTLStageAccelerationStructure visibilityOptions:MTL4VisibilityOptionDevice];
        for (int i = 0; i < g_rts.npend; i++) { rts_mesh *m = g_rts.pending[i];
            MTL4PrimitiveAccelerationStructureDescriptor *pd = [MTL4PrimitiveAccelerationStructureDescriptor new]; MTL4AccelerationStructureTriangleGeometryDescriptor *gd = [MTL4AccelerationStructureTriangleGeometryDescriptor new];
            gd.vertexBuffer = (MTL4BufferRange){m->pos.gpuAddress, m->pos.length}; gd.vertexFormat = MTLAttributeFormatFloat3; gd.vertexStride = 12; gd.triangleCount = m->tris; gd.opaque = YES;
            if (m->ib) { gd.indexBuffer = (MTL4BufferRange){m->ib.gpuAddress, m->ib.length}; gd.indexType = m->isz == 2 ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32; }
            pd.geometryDescriptors = @[gd];
            [ce buildAccelerationStructure:m->as descriptor:pd scratchBuffer:(MTL4BufferRange){g_rts.scratch.gpuAddress, g_rts.scratch.length}];
            [ce barrierAfterEncoderStages:MTLStageAccelerationStructure beforeEncoderStages:MTLStageAccelerationStructure visibilityOptions:MTL4VisibilityOptionDevice];
            m->built = 1; g_rts.n_builds++; g_rts.build_tris += m->tris; }
        g_rts.npend = 0;
    }
    // 2. TLAS（只放建好的实例） | 2. TLAS (built instances only)
    id<MTLBuffer> ibuf = g_rts.instbuf[ri]; MTLIndirectAccelerationStructureInstanceDescriptor *dst = ibuf.contents; int n = 0;
    for (int i = 0; i < g_rts.ninst; i++) dst[n++] = g_rts.inst[i];
    MTL4InstanceAccelerationStructureDescriptor *td = [MTL4InstanceAccelerationStructureDescriptor new];
    td.instanceDescriptorType = MTLAccelerationStructureInstanceDescriptorTypeIndirect; td.instanceCount = n; td.instanceDescriptorStride = sizeof(MTLIndirectAccelerationStructureInstanceDescriptor);
    td.instanceDescriptorBuffer = (MTL4BufferRange){ibuf.gpuAddress, (uint64_t)n * sizeof(MTLIndirectAccelerationStructureInstanceDescriptor)};
    [ce buildAccelerationStructure:g_rts.tlas descriptor:td scratchBuffer:(MTL4BufferRange){g_rts.scratch.gpuAddress, g_rts.scratch.length}];
    [ce barrierAfterEncoderStages:MTLStageAccelerationStructure beforeEncoderStages:MTLStageDispatch visibilityOptions:MTL4VisibilityOptionDevice];
    // 3. 追踪：常量 = 逆视投影（列主序 = 行向量矩阵求逆后原样当列）、太阳、尺寸 | 3. Trace: constants = inverse view-projection (column-major = the inverted row-vector matrix used as columns), sun, size
    double a[16], ia[16]; for (int i = 0; i < 16; i++) a[i] = g_rts.cam[i]; rx_inv4(a, ia);
    float *k = g_rts.kbuf[ri].contents; for (int i = 0; i < 16; i++) k[i] = (float)ia[i];
    k[16] = g_rts.sun[0]; k[17] = g_rts.sun[1]; k[18] = g_rts.sun[2]; k[19] = 0; ((uint32_t *)k)[20] = (uint32_t)depth.width; ((uint32_t *)k)[21] = (uint32_t)depth.height;
    static float bias = -1; if (bias < 0) { const char *e = getenv("DM_RT_SHADOW_BIAS"); bias = e ? (float)atof(e) : 0.6f; } k[22] = bias; k[23] = 0;
    [ce setArgumentTable:g_rts.at_trace]; enc_c_pso(ce, g_rts.ps_trace);
    [g_rts.at_trace setTexture:depth.gpuResourceID atIndex:0]; [g_rts.at_trace setTexture:g_rts.mask.gpuResourceID atIndex:1];
    [g_rts.at_trace setResource:g_rts.tlas.gpuResourceID atBufferIndex:0]; [g_rts.at_trace setAddress:g_rts.kbuf[ri].gpuAddress atIndex:1];
    enc_c_dispatch(ce, MTLSizeMake((depth.width + 7) / 8, (depth.height + 7) / 8, 1), MTLSizeMake(8, 8, 1));
    enc_c_end(ce);
    args_dirty_all(); g_pso_dirty = g_dss_dirty = 1;
    g_rts.mask_done = 1; g_rts.n_traced++;
    { static int n; if (n++ < 3) MLOG("光追阴影：帧 %llu 实例 %d（蒙皮 %d）、遮罩 %lux%lu、太阳 (%.2f, %.2f, %.2f)", (unsigned long long)g_rts.frame, g_rts.ninst, g_rts.nskin, (unsigned long)depth.width, (unsigned long)depth.height, g_rts.sun[0], g_rts.sun[1], g_rts.sun[2]); }
}
static void rts_overlay(id<MTLTexture> hdr) {   // 第一阶段合成：HDR × (amb + (1 − amb) · 遮罩) | phase-1 compositing: HDR × (amb + (1 − amb) · mask)
    if (!g_rts.mask_done || !hdr || hdr.width != g_rts.mask.width || hdr.height != g_rts.mask.height || !rts_overlay_setup(hdr.pixelFormat)) return;
    rp_end(); if (g_blit) { enc_b_end(g_blit); g_blit = nil; }
    int ri = g_rts.ri; float *a = g_rts.dbuf[ri].contents; a[0] = g_rts.dbg == 2 ? 0.f : dm_rt_shadow_ambient(); a[1] = a[2] = a[3] = 0;
    RPD *rp = [RPD new]; rp.colorAttachments[0].texture = hdr; rp.colorAttachments[0].loadAction = MTLLoadActionLoad; rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    RENC en = [g_cb renderCommandEncoderWithDescriptor:rp]; if (!en) return;
    [en barrierAfterQueueStages:MTLStageAll beforeStages:MTLStageAll visibilityOptions:MTL4VisibilityOptionDevice];
    [en setArgumentTable:g_rts.at_overlay atStages:MTLRenderStageFragment]; [g_rts.at_overlay setTexture:g_rts.mask.gpuResourceID atIndex:0]; [g_rts.at_overlay setAddress:g_rts.dbuf[ri].gpuAddress atIndex:0];
    [en setViewport:(MTLViewport){0, 0, (double)hdr.width, (double)hdr.height, 0, 1}]; [en setRenderPipelineState:g_rts.ps_overlay];
    [en drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3]; m4_end(en);
    args_dirty_all(); g_pso_dirty = g_dss_dirty = 1; g_rts.overlay_done = 1;
}
// _rt 变体的遮罩绑定：本帧算过遮罩就给遮罩，否则给 1x1 全亮（没有光追阴影时和原版阴影图清空后一样 = 全亮） | Mask binding of the _rt variants: the mask when traced this frame, else a 1x1 fully lit texel
static id<MTLTexture> rts_mask_tex(void) { return g_rts.mask_done && g_rts.mask ? g_rts.mask : g_rts.white; }
static uint64_t rts_mask_rid(void) { id<MTLTexture> t = rts_mask_tex(); return t ? t.gpuResourceID._impl : 0; }
// 片元 _rt 变体：开关开且本帧有遮罩时用 <键>_rt（首次按需加载，没有该文件则记一次、沿用原版）；否则用原版 | Fragment _rt variant: with the switch on and a mask this frame use <key>_rt (loaded on demand, a missing file is noted once and the original kept); otherwise the original
static void rts_select_fs(NvProg *pr) {
    if (!pr || !pr->sh[1]) return;
    if (!pr->fs_base) pr->fs_base = pr->sh[1];
    int want = rts_on() && !g_rts.failed && g_rts.mask_done && !g_rts.dbg;
    if (want && !pr->rt_tried) { pr->rt_tried = 1;
        NSString *k = [NSString stringWithFormat:@"%.16s_rt", pr->fs_base->key];
        if ([NSFileManager.defaultManager fileExistsAtPath:[NSString stringWithFormat:@"%@/%@.json", g_msl_dir, k]]) { pr->fs_rt = shader_get(k, 1); if (pr->fs_rt) g_rts.n_rt_loaded++; else g_rts.n_rt_missing++; } }
    pr->sh[1] = want && pr->fs_rt ? pr->fs_rt : pr->fs_base;
}
static void rts_set_rt(const p_set_rt *r) {
    if (!rts_on() || g_rts.failed) return;
    uint64_t f = atomic_load(&g_cur_frame); if (f != g_rts.frame) rts_frame_begin(f);
    id<MTLTexture> c0 = r->n > 0 && r->color[0] ? tex_mtl(r->color[0]) : nil, dt = r->depth ? tex_mtl(r->depth) : nil;
    if (!g_rts.main_seen) {
        if (r->n == 1 && c0 && c0.pixelFormat == MTLPixelFormatRG11B10Float && dt && dt.pixelFormat == MTLPixelFormatDepth32Float_Stencil8 && dt.width >= 500 && dt.width != dt.height) {
            g_rts.main_seen = 1; g_rts.hdr_ser = r->color[0]->serial; g_rts.ds_ser = r->depth->serial;
            if (!g_rts.npend && !g_rts.ninst) return;
            rts_build_and_trace(dt); }
        return;
    }
    if (g_rts.mask_done && !g_rts.overlay_done && (g_rts.dbg || !g_rts.n_rt_loaded) && r->n >= 1 && r->color[0] && r->color[0]->serial == g_rts.hdr_ser) rts_overlay(c0);   // 第一阶段合成：只在调试（DM_RT_SHADOW_DBG）或没有 _rt 变体时 | phase-1 compositing: only for debugging or without _rt variants   // 主通道之后第一次再挂 HDR | first time the HDR is attached again after the main pass
}
#endif
