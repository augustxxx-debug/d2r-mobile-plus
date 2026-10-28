// ---------- 光追阴影（第一阶段）：用 Metal 硬件光追算太阳方向的逐像素可见度，替掉游戏自己的两级级联阴影图 ----------
// 怎么接（都在翻译层，不改游戏）：
//   1. 阴影通道（无颜色附件、方形 2D 深度）的每次绘制 = 一个遮挡体实例：网格按（顶点流地址、步长、格式、索引缓冲、数量、基顶点 + 数据指纹）做键缓存 BLAS，
//      首次见到时用计算核把属性 0 的位置解码成 float3（SNORM16x4 · exp2(w·13)，与 vs_models_v 的 MSL 一致）再建；cb0 的物体→世界 3x4 进 TLAS 实例。
//      阴影绘制照常画进阴影图（六审 5.1）；网格三态 待建 → 已提交（本帧编进了构建）→ 可用（下一帧起），只有光追帧里网格可用的绘制进 TLAS。主通道前光追成功、且本帧每次阴影绘制都在 TLAS 里，
//      才把阴影图清回游戏自己的全亮值（rp_begin_ex 登记），否则原版阴影留着——失败的那一帧本身也有影子。光追帧 = 上一帧光追完整；失败后按 2、4 … 512 帧退避再试（五审 Q5-02）。
//   2. 深度预渲染（无颜色、主深度）的绘制里取 cb3 +336 的相机视投影；阴影通道的 cb3 +336 是光源视投影，其 z 列（取朝天那一侧）= 太阳方向。
//   3. 主不透明通道（RG11B10F + Depth32Float_Stencil8，宽 ≥ 500、非方形）的设目标处：建 TLAS、按主深度重建世界坐标、每像素向太阳发一条遮挡射线，写 R8 遮罩（3D 尺寸）。
//   4. 主通道之后第一次再挂 HDR 的设目标处：全屏一趟把 HDR 乘上 amb + (1 − amb) · 遮罩（第一阶段的合成方式；第二阶段改成片元 MSL 的 _rt 变体直接读遮罩，这一趟就撤）。
// 门槛：supportsRaytracing 且 GPU 家族 ≥ Apple9 且机型白名单（用户要求三重判定）；任一不满足 = 整个模块不建任何对象、每次调用立即返回。开关：DM_RT_SHADOW=0/1 钉住，否则 dm_rt_shadow_setting()。
// 蒙皮角色（rt_is_skin 的 10 个顶点着色器）第一阶段按绑定姿势 + 世界矩阵进 TLAS（投影形状不对，第二阶段改成位置导出变体 + refit）。只编 Metal 4 路径；DM_NVN_M3 下模块为空。
// ---------- Ray-traced shadows (phase 1): per-pixel sun visibility via Metal hardware ray tracing, replacing the game's two cascaded shadow maps ----------
// How it hooks in (translation layer only, the game is untouched):
//   1. Every draw of a shadow pass (no color attachment, square 2D depth) = one occluder instance: meshes are cached as BLAS keyed by (vertex stream address, stride, format, index buffer, count, base vertex + data fingerprint);
//      on first sight a compute kernel decodes attribute 0 into float3 (SNORM16x4 · exp2(w·13), matching the vs_models_v MSL) before the build; cb0's object→world 3x4 goes into the TLAS instance.
//      Shadow draws are still encoded into the shadow map (sixth review 5.1); meshes go pending → submitted (build encoded this frame) → usable (from the next frame), and only usable meshes in a ray-traced frame enter the TLAS. Only when the trace before the main pass succeeds and every shadow draw of the frame is in the TLAS
//      are the shadow maps cleared back to the game's own fully-lit value (registered by rp_begin_ex); otherwise the original shadows stay - the failing frame itself has shadows too. Ray-traced frame = the previous frame was complete; after a failure retry with a 2, 4 … 512 frame backoff (fifth review Q5-02).
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
static void rts_note_pass(RPD *pd) { (void)pd; }
static void rts_note_clear(id<MTLTexture> t, NSUInteger level, NSUInteger slice, double cd) { (void)t; (void)level; (void)slice; (void)cd; }
static void rts_select_fs(NvProg *pr) { (void)pr; }
static uint64_t rts_mask_rid(void) { return 0; }
static id<MTLTexture> rts_mask_tex(void) { return nil; }
int mtl_rt_shadow_supported(void) { return 0; }
#else
#define RTS_MAX_INST 4096
#define RTS_BUILDS_PER_FRAME 12   // 每帧最多新建这么多 BLAS（新网格首次出现时），其余下一帧再建，避免一次卡顿 | At most this many new BLAS builds per frame (first appearance of new meshes); the rest wait for the next frame to avoid a hitch
#define RTS_SLOTS_MAX 8           // 每帧常量 / 实例槽的上限（分配器上限也是 8：最多 8 次提交在途） | Cap of per-frame constant / instance slots (the allocator cap is 8 too: at most 8 submissions in flight)
#define RTS_MEMO 512             // 本帧「布局 → 网格键」备忘的槽数（两级级联重复绘制不重算摘要） | slots of this frame's "layout → mesh key" memo (repeated cascade draws skip the digest)
#define RTS_MAX_SMAP 8            // 每帧登记的阴影图附件（贴图 + mip + 层）上限 | Cap of shadow-map attachments (texture + mip + slice) registered per frame
enum { RTS_PENDING, RTS_SUBMITTED, RTS_READY };   // 待建（在缓存里，命中就排队）/ 已提交（构建编进了本帧命令缓冲）/ 可用（下一帧起可进 TLAS） | pending (cached, queued on a hit) / submitted (build encoded into this frame's command buffer) / usable (may enter the TLAS from the next frame)
typedef struct { uint64_t key, bytes; id<MTLAccelerationStructure> as; id<MTLBuffer> pos, ib; uint64_t last_frame; uint32_t tris, nverts, isz; int state, queued; } rts_mesh;
#define RTS_MESH_READY(m) ((m)->state == RTS_READY)
#define RTS_LAST_SLOT_BUF() (g_rts.slot[g_rts.cur].inst)   // 单测用：本帧追踪用的实例槽 | for the unit tests: the instance slot this frame's trace used
#define RTS_MAP_CLEARED() (g_rts.map_cleared)
#define RTS_HAS_NOTE_PASS 1
#define RTS_UNC(r) ((long long)g_rts.n_unc[r])   // 单测用：光追帧里没进 TLAS 的绘制按原因累计 | for the unit tests: draws outside the TLAS in ray-traced frames, by reason
// 光追帧里一次阴影绘制没进 TLAS 的原因（真机 3fc0f41：走到某处后每帧约 8 次，日志分不出是哪种） | Why a shadow draw in a ray-traced frame stayed outside the TLAS (device run of 3fc0f41: about 8 per frame after walking somewhere, and the log could not tell which)
enum { RTS_UNC_PASS, RTS_UNC_NODEPTH, RTS_UNC_SMAPFULL, RTS_UNC_PRIM, RTS_UNC_FMT, RTS_UNC_MAT, RTS_UNC_IDX, RTS_UNC_VRANGE, RTS_UNC_VPOOL, RTS_UNC_QUOTA, RTS_UNC_ALLOC, RTS_UNC_PENDING, RTS_UNC_INSTFULL, RTS_UNC_N };
static const char *const k_rts_unc[RTS_UNC_N] = { "通道没登记", "通道没挂深度", "阴影图登记满", "图元不认（不是三角形列表 / 带）", "顶点属性 / 格式 / 索引类型不认", "缺物体矩阵常量", "索引段不在内存池", "顶点范围越界",
    "顶点不在内存池", "本帧新建配额满", "网格对象建不出", "网格待建 / 刚提交", "实例满" };
#define RTS_WHY() rts_frame_why()   // 单测用：刚结束这一帧的回退原因（"" = 完整） | for the unit tests: the fallback reason of the frame just finished ("" = complete)
static struct {
    int on, gate, dbg, failed, was_on; uint64_t frame;
    nvn_hmap meshes; int nmesh; uint64_t cache_bytes, budget;   // 网格缓存与总字节预算（DM_RT_SHADOW_CACHE_MB，缺省 64） | mesh cache and its total byte budget (DM_RT_SHADOW_CACHE_MB, default 64)
    MTLIndirectAccelerationStructureInstanceDescriptor inst[RTS_MAX_INST]; uint64_t inst_tag[RTS_MAX_INST]; rts_mesh *inst_mesh[RTS_MAX_INST]; int ninst, ndup, nskip_fmt, nskin;
    rts_mesh *pending[RTS_BUILDS_PER_FRAME]; struct { uint64_t gpu; uint32_t n, stride, fmt, base; } pend_src[RTS_BUILDS_PER_FRAME]; int npend;   // 源地址只在当帧有效：帧开始时作废 | source addresses are valid for the current frame only: dropped at frame start
    rts_mesh *submitted[RTS_BUILDS_PER_FRAME]; int nsub;   // 本帧编进构建的网格：下一帧开始时转可用 | meshes whose build was encoded this frame: usable from the next frame start
    int rt_frame, backoff; uint64_t retry_at, n_rt_frames, n_fail_frames, n_orig_draws, n_partial_frames;   // 就绪闸门：本帧走光追？失败退避；统计 | readiness gate: is this a ray-traced frame? backoff after a failure; counters
    // 本帧的阴影图附件（rp_begin_ex 登记：贴图、mip、层、本帧第一次挂它时清没清、清成什么）；nuncov = 本帧没被 TLAS 代表的阴影绘制数
    // This frame's shadow-map attachments (registered by rp_begin_ex: texture, mip, slice, whether the first pass on it this frame cleared it and to what); nuncov = shadow draws this frame not represented by the TLAS
    struct { id<MTLTexture> t; NSUInteger level, slice; double cd; int cleared, used; } smap[RTS_MAX_SMAP]; int nsmap, cur_smap, nuncov, map_cleared;
    int unc_why, pass_why; uint32_t prim_mask; uint64_t n_empty, n_strip; uint64_t n_unc[RTS_UNC_N]; int f_unc[RTS_UNC_N]; uint64_t n_unc_log;   // 没进 TLAS 的原因：本次绘制 / 本通道、累计、本帧、按原因记过几帧 | outside-the-TLAS reason: this draw / this pass, cumulative, this frame, frames logged
    char why[200];   // 本帧光追帧没追踪的具体原因（rts_build_and_trace 写，下一帧开头随回退行记下） | the specific reason this ray-traced frame did not trace (written by rts_build_and_trace, logged with the fallback line at the next frame start)
    struct { uint64_t frame, lay, key; } memo[RTS_MEMO];
    float cam[16], lvp[16]; int have_cam, have_sun; float sun[3];
    int main_seen, mask_done, overlay_done; uint64_t hdr_ser, ds_ser; id<MTLTexture> main_hdr;
    id<MTLComputePipelineState> ps_decode, ps_trace; id<MTLRenderPipelineState> ps_overlay; id<MTL4ArgumentTable> at_decode, at_trace, at_overlay;
    // 每帧常量 / 实例槽：fence = 最后一次用它的提交；GPU 做完那次提交（g_done ≥ fence）才可复用，全忙就加一槽，到上限本帧不追踪（阴影图照原版，见 rts_build_and_trace）
    // Per-frame constant / instance slots: fence = the last submission that used it; reusable only once the GPU finished that submission (g_done >= fence); all busy → add a slot, at the cap skip tracing this frame (the shadow map stays original, see rts_build_and_trace)
    struct { id<MTLBuffer> inst, k, d; uint64_t fence; } slot[RTS_SLOTS_MAX]; int nslot, cur;
    id<MTLAccelerationStructure> tlas; id<MTLBuffer> scratch; NSUInteger scratch_len;
    id<MTLTexture> mask, white; int n_rt_loaded, n_rt_missing;
    uint64_t n_frames, n_builds, n_traced, n_skipped_draws, n_evict_budget, n_slot_busy, key_ns, key_bytes; double build_tris;
} g_rts = { .on = -1, .meshes = {.klen = 8}, .cur_smap = -1 };
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
static void rts_release_all(const char *why);
static int rts_on(void) {
    if (g_rts.on < 0) {
        const char *e = getenv("DM_RT_SHADOW"); g_rts.gate = mtl_rt_shadow_supported(); g_rts.dbg = getenv("DM_RT_SHADOW_DBG") ? atoi(getenv("DM_RT_SHADOW_DBG")) : 0;
        g_rts.on = e ? atoi(e) != 0 : -2;
        const char *b = getenv("DM_RT_SHADOW_CACHE_MB"); g_rts.budget = (uint64_t)(b && atoi(b) > 0 ? atoi(b) : 64) << 20;
        // 前缀与宿主 dm_rt_shadow_setting 的「光追阴影：生效」同一个：钉住时不走宿主那条，分享诊断日志的摘要照样取得到 | same prefix as "光追阴影：生效" in the host dm_rt_shadow_setting: when pinned that path is skipped, and the share-diagnostics summary still finds the line
        if (e) MLOG("光追阴影：生效 %s（测试开关 DM_RT_SHADOW=%s 钉住%s）", atoi(e) && g_rts.gate ? "开" : "关", e, g_rts.gate ? "" : "，机器不可用，仍不生效");
    }
    int v = !g_rts.gate || g_rts.failed ? 0 : g_rts.on == -2 ? dm_rt_shadow_setting() != 0 : g_rts.on;
    if (!v && g_rts.was_on) rts_release_all(g_rts.failed ? "模块失败" : "开关关了");   // 关掉 / 失败：整个缓存等 GPU 在途工作做完后释放（六审 5.1） | off / failed: the whole cache is released once in-flight GPU work is done (sixth review 5.1)
    g_rts.was_on = v;
    return v;
}
static const uint8_t *rts_mem(uint64_t addr, uint64_t len) { void *m; uint64_t o; if (!addr || !len || !mtl_addr(addr, len, &m, &o)) return NULL; const uint8_t *b = [(__bridge id<MTLBuffer>)m contents]; return b ? b + o : NULL; }
static uint64_t rts_fnv(uint64_t h, const void *p, size_t n) { const uint8_t *b = p; for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 1099511628211ull; return h; }
// 整段摘要（六审 5.2：网格键覆盖全部已校验的索引字节与顶点属性 0，后段变了键就变）：一次 8 字节 | Whole-span digest (sixth review 5.2: the mesh key covers every validated index byte and vertex attribute 0, so later changes change the key): 8 bytes at a time
static inline uint64_t rts_mix(uint64_t h, uint64_t w) { h = (h ^ w) * 0x9E3779B97F4A7C15ull; return h ^ (h >> 29); }
static uint64_t rts_digest(uint64_t h, const uint8_t *p, size_t n) {   // 四路并行累加（打断乘法依赖链），尾部逐 8 字节 | four independent lanes (break the multiply dependency chain), tail 8 bytes at a time
    uint64_t l[4] = {h, h ^ 1, h ^ 2, h ^ 3}; size_t i = 0;
    for (; i + 32 <= n; i += 32) for (int j = 0; j < 4; j++) { uint64_t w; memcpy(&w, p + i + j * 8, 8); l[j] = rts_mix(l[j], w); }
    for (; i + 8 <= n; i += 8) { uint64_t w; memcpy(&w, p + i, 8); l[0] = rts_mix(l[0], w); }
    uint64_t t = 0; memcpy(&t, p + i, n - i); l[1] = rts_mix(l[1], t ^ ((uint64_t)n << 56));
    return rts_mix(rts_mix(rts_mix(l[0], l[1]), l[2]), l[3]);
}
// 顶点属性 0 的摘要：第 v0 … v0+nv−1 个顶点各取 asz 字节（步长 stride）；同样四路并行 | Digest of vertex attribute 0: asz bytes of each of vertices v0 … v0+nv−1 (stride apart); four lanes as well
static uint64_t rts_vdigest(uint64_t h, const uint8_t *vp, uint64_t v0, uint64_t nv, uint64_t stride, uint64_t asz) {
    if (stride == asz) return rts_digest(h, vp + v0 * stride, (size_t)(nv * stride));   // 位置单独一条流：整段连续 | position in its own stream: one contiguous span
    uint64_t l[4] = {h, h ^ 1, h ^ 2, h ^ 3};
    for (uint64_t v = 0; v < nv; v++) { const uint8_t *q = vp + (v0 + v) * stride; uint64_t w; memcpy(&w, q, 8); uint64_t *L = &l[v & 3]; *L = rts_mix(*L, w);
        if (asz > 8) { uint64_t w1 = 0; memcpy(&w1, q + 8, asz - 8); *L = rts_mix(*L, w1); } }
    return rts_mix(rts_mix(rts_mix(rts_mix(l[0], l[1]), l[2]), l[3]), nv);
}
static uint64_t rts_attr_bytes(uint32_t fmt) { return fmt == 0x2e ? 16 : fmt == 0x22 ? 12 : 8; }   // 解码核读的属性 0 宽度（FLOAT4 / FLOAT3 / SNORM16x4、HALF4） | Width of attribute 0 as read by the decode kernel
static int rts_slot_new(void) {   // 新加一个常量 / 实例槽；失败返回 -1 | Add one constant / instance slot; -1 on failure
    if (g_rts.nslot >= RTS_SLOTS_MAX) return -1;
    int i = g_rts.nslot; typeof(g_rts.slot[0]) *s = &g_rts.slot[i];
    s->inst = [g_dev newBufferWithLength:RTS_MAX_INST * sizeof(MTLIndirectAccelerationStructureInstanceDescriptor) options:MTLResourceStorageModeShared];
    s->k = [g_dev newBufferWithLength:256 options:MTLResourceStorageModeShared]; s->d = [g_dev newBufferWithLength:64 options:MTLResourceStorageModeShared]; s->fence = 0;
    if (!s->inst || !s->k || !s->d) { s->inst = s->k = s->d = nil; return -1; }
    res_add(s->inst); res_add(s->k); res_add(s->d); g_rts.nslot++;
    return i;
}
// 取一个 GPU 已用完的槽（g_done ≥ 它上次那次提交）；都在用就加一槽；到上限返回 -1（本帧不追踪）。取到的槽记上本次提交的 fence（与 defer_obj_release 同一口径）
// Take a slot the GPU has finished with (g_done >= its last submission); all in use → add a slot; at the cap return -1 (no trace this frame). The slot taken is stamped with this submission's fence (same convention as defer_obj_release)
static int rts_slot_pick(void) {
    uint64_t done = g_done; int i;
    for (i = 0; i < g_rts.nslot; i++) if (g_rts.slot[i].fence <= done) break;
    if (i == g_rts.nslot && (i = rts_slot_new()) < 0) { g_rts.n_slot_busy++;
        if (g_rts.n_slot_busy <= 3 || g_rts.n_slot_busy % 100 == 0) MLOG("光追阴影：回退原版阴影图（%d 个常量 / 实例槽都还在 GPU 上，本帧不追踪，第 %llu 次）", g_rts.nslot, (unsigned long long)g_rts.n_slot_busy);
        return -1; }
    g_rts.slot[i].fence = g_fence + 1; g_rts.cur = i;
    return i;
}
static int rts_setup(void) {
    if (g_rts.ps_trace) return 1;
    if (g_rts.failed) return 0;
    NSError *e = nil; id<MTLLibrary> lib = [g_dev newLibraryWithSource:@(k_rts_msl) options:nil error:&e];
    if (!lib) { g_rts.failed = 1; MLOG("【红】光追阴影：着色器编译失败 %s", e.localizedDescription.UTF8String); return 0; }
    g_rts.ps_decode = [g_dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"rts_decode"] error:&e];
    g_rts.ps_trace = g_rts.ps_decode ? [g_dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"rts_trace"] error:&e] : nil;
    MTL4ArgumentTableDescriptor *ad = [MTL4ArgumentTableDescriptor new]; ad.maxBufferBindCount = 3; ad.maxTextureBindCount = 2;
    g_rts.at_decode = [g_dev newArgumentTableWithDescriptor:ad error:&e]; g_rts.at_trace = [g_dev newArgumentTableWithDescriptor:ad error:&e]; g_rts.at_overlay = [g_dev newArgumentTableWithDescriptor:ad error:&e];
    int s0 = rts_slot_new();
    // TLAS 按最大实例数一次建够；暂存缓冲按 TLAS 的需求起步，BLAS 需要更大时再换 | The TLAS is created once for the maximum instance count; the scratch buffer starts at the TLAS's need and is replaced when a BLAS needs more
    MTL4InstanceAccelerationStructureDescriptor *td = [MTL4InstanceAccelerationStructureDescriptor new];
    td.instanceDescriptorType = MTLAccelerationStructureInstanceDescriptorTypeIndirect; td.instanceCount = RTS_MAX_INST; td.instanceDescriptorStride = sizeof(MTLIndirectAccelerationStructureInstanceDescriptor);
    if (s0 >= 0) td.instanceDescriptorBuffer = (MTL4BufferRange){g_rts.slot[s0].inst.gpuAddress, g_rts.slot[s0].inst.length};
    MTLAccelerationStructureSizes sz = [g_dev accelerationStructureSizesWithDescriptor:td];
    g_rts.tlas = [g_dev newAccelerationStructureWithSize:sz.accelerationStructureSize]; if (g_rts.tlas) res_add(g_rts.tlas);
    g_rts.scratch_len = MAX(sz.buildScratchBufferSize, (NSUInteger)(4 << 20)); g_rts.scratch = [g_dev newBufferWithLength:g_rts.scratch_len options:MTLResourceStorageModePrivate]; if (g_rts.scratch) res_add(g_rts.scratch);
    if (!g_rts.white) { MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Unorm width:1 height:1 mipmapped:NO]; d.usage = MTLTextureUsageShaderRead; d.storageMode = MTLStorageModeShared;
      g_rts.white = [g_dev newTextureWithDescriptor:d]; uint8_t one = 255; if (g_rts.white) { [g_rts.white replaceRegion:MTLRegionMake2D(0, 0, 1, 1) mipmapLevel:0 withBytes:&one bytesPerRow:1]; res_add(g_rts.white); } }
    if (!g_rts.ps_decode || !g_rts.ps_trace || !g_rts.at_decode || !g_rts.at_trace || !g_rts.at_overlay || !g_rts.tlas || !g_rts.scratch || !g_rts.white || s0 < 0) {
        g_rts.failed = 1; MLOG("【红】光追阴影：管线 / 加速结构建不出 %s（退回原版阴影图）", e.localizedDescription.UTF8String); return 0; }
    MLOG("光追阴影：建好（TLAS %lu KiB、暂存 %lu KiB、最多 %d 实例、网格缓存预算 %llu MB）", (unsigned long)(sz.accelerationStructureSize >> 10), (unsigned long)(g_rts.scratch_len >> 10), RTS_MAX_INST, (unsigned long long)(g_rts.budget >> 20));
    return 1;
}
static int rts_overlay_setup(MTLPixelFormat fc) {
    static MTLPixelFormat cur; if (g_rts.ps_overlay && cur == fc) return 1;
    NSError *e = nil; id<MTLLibrary> lib = [g_dev newLibraryWithSource:@(k_rts_msl) options:nil error:&e]; if (!lib) return 0;
    MTLRenderPipelineDescriptor *pd = [MTLRenderPipelineDescriptor new]; pd.vertexFunction = [lib newFunctionWithName:@"rts_vs"]; pd.fragmentFunction = [lib newFunctionWithName:@"rts_fs"];
    pd.colorAttachments[0].pixelFormat = fc; pd.colorAttachments[0].blendingEnabled = YES;
    pd.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorZero; pd.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorSourceColor;
    pd.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorZero; pd.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOne;
    if (g_rts.ps_overlay) defer_obj_release(g_rts.ps_overlay);
    g_rts.ps_overlay = [g_dev newRenderPipelineStateWithDescriptor:pd error:&e]; cur = fc;
    if (!g_rts.ps_overlay) MLOG("【红】光追阴影：合成管线建不出 %s", e.localizedDescription.UTF8String);
    return g_rts.ps_overlay != nil;
}
// 网格出缓存：GPU 对象一律 defer_obj_release（等用过它们的提交做完再放、再移出驻留集；不先 res_remove：在途的命令缓冲可能还在读） | A mesh leaves the cache: every GPU object goes through defer_obj_release (released, and removed from the residency set, after the submissions that used it complete; no res_remove first: an in-flight command buffer may still read it)
static void rts_mesh_drop(rts_mesh *m) {
    if (!nvn_hm_del(&g_rts.meshes, &m->key)) return;
    defer_obj_release(m->as); defer_obj_release(m->pos); defer_obj_release(m->ib);
    m->as = nil; m->pos = m->ib = nil;   // calloc 出的结构体里的强引用 free 不会放：先置空（46e57ec 起淘汰的 BLAS 都漏了这一下） | free() does not release the strong references in a calloc'd struct: clear them first (every BLAS evicted since 46e57ec leaked this way)
    g_rts.cache_bytes -= m->bytes; g_rts.nmesh--; free(m);
}
// 整体释放（开关关了 / 模块失败，六审 5.1）：网格、TLAS、暂存、遮罩、槽、管线都等在途提交做完再放；1x1 白贴图留着（_rt 变体关掉时仍可能绑它）。再开时 rts_setup 重建
// Release everything (switched off / module failed, sixth review 5.1): meshes, TLAS, scratch, mask, slots and pipelines are released after in-flight submissions; the 1x1 white texture stays (_rt variants may still bind it). rts_setup rebuilds on the next switch-on
static void rts_release_all(const char *why) {
    if (!g_rts.nmesh && !g_rts.ps_trace && !g_rts.nslot) return;
    MLOG("光追阴影：释放（%s）：网格 %d 个 %.1f MB、TLAS、暂存 %lu KiB、遮罩、%d 个槽，等 GPU 在途工作做完后放掉", why, g_rts.nmesh, g_rts.cache_bytes / 1048576.0, (unsigned long)(g_rts.scratch_len >> 10), g_rts.nslot);
    while (g_rts.nmesh) { rts_mesh *m = NULL; for (uint32_t i = 0; i < g_rts.meshes.cap && !m; i++) m = g_rts.meshes.vals[i]; if (!m) break; rts_mesh_drop(m); }
    defer_obj_release(g_rts.tlas); defer_obj_release(g_rts.scratch); defer_obj_release(g_rts.mask);
    for (int i = 0; i < g_rts.nslot; i++) { defer_obj_release(g_rts.slot[i].inst); defer_obj_release(g_rts.slot[i].k); defer_obj_release(g_rts.slot[i].d); g_rts.slot[i].inst = g_rts.slot[i].k = g_rts.slot[i].d = nil; }
    defer_obj_release(g_rts.ps_decode); defer_obj_release(g_rts.ps_trace); defer_obj_release(g_rts.ps_overlay);
    defer_obj_release(g_rts.at_decode); defer_obj_release(g_rts.at_trace); defer_obj_release(g_rts.at_overlay);
    g_rts.tlas = nil; g_rts.scratch = nil; g_rts.mask = nil; g_rts.scratch_len = 0; g_rts.nslot = g_rts.cur = 0;
    g_rts.ps_decode = g_rts.ps_trace = nil; g_rts.ps_overlay = nil; g_rts.at_decode = g_rts.at_trace = g_rts.at_overlay = nil;
    g_rts.npend = g_rts.nsub = g_rts.ninst = 0; g_rts.cache_bytes = 0; g_rts.rt_frame = g_rts.backoff = 0; g_rts.retry_at = 0;
    g_rts.mask_done = g_rts.overlay_done = g_rts.map_cleared = 0;
    for (int i = 0; i < g_rts.nsmap; i++) g_rts.smap[i].t = nil;
    g_rts.nsmap = 0; g_rts.cur_smap = -1; g_rts.main_hdr = nil;
}
// 上一帧的光追完不完整：NULL = 完整（或本帧不需要 / 本帧有阴影绘制没进 TLAS、整帧照原版），否则是原因 | Was the previous frame's ray tracing complete: NULL = complete (or not needed / some shadow draw was outside the TLAS and the whole frame stayed original), else the reason
static const char *rts_frame_fail(void) {
    if (!g_rts.main_seen) return "没有主通道";
    if (!g_rts.have_cam) return "没取到相机";
    if (!g_rts.have_sun) return "没取到太阳方向";
    if (!g_rts.rt_frame || !g_rts.ninst || g_rts.nuncov) return NULL;
    if (!g_rts.mask_done) return g_rts.why[0] ? g_rts.why : "遮罩没算出来（主通道开始时还没有阴影实例：阴影绘制在主通道之后）";
    if (!g_rts.map_cleared) return "阴影图没清回全亮";
    if (!g_rts.overlay_done && (g_rts.dbg || !g_rts.n_rt_loaded)) return "遮罩没合成进画面";
    return NULL;
}
static const char *rts_unc_str(char *b, size_t n, const uint64_t *c64, const int *c32) {   // 「原因 × 次数」列表 | "reason × count" list
    size_t k = 0; b[0] = 0; for (int r = 0; r < RTS_UNC_N && k + 1 < n; r++) { uint64_t v = c64 ? c64[r] : (uint64_t)c32[r]; if (v) k += (size_t)snprintf(b + k, n - k, "%s%s ×%llu", k ? "、" : "", k_rts_unc[r], (unsigned long long)v); }
    return k ? b : "无";
}
static const char *rts_frame_why(void) { const char *w = rts_frame_fail(); return w ? w : ""; }
static void rts_frame_begin(uint64_t f) {
    char ubuf[512];
    if (g_rts.n_frames && g_rts.n_frames % 600 == 0) MLOG("光追阴影：第 %llu 帧 实例 %d（重复 %d、蒙皮 %d、格式不认 %d）、网格缓存 %d（%.1f / %llu MB，预算淘汰 %llu）、累计建 %llu 个 BLAS（%.1f 万三角形）、追踪 %llu 帧、进 TLAS 的阴影绘制 %llu 次、_rt 变体 %d 个（缺 %d）；光追帧 %llu、失败退回 %llu 帧、有绘制没进 TLAS 整帧照原版 %llu 帧、光追帧里没进 TLAS 的绘制 %llu 次、槽 %d（全忙 %llu 次）、键摘要 %.0f µs / %.0f KB 每光追帧；没进 TLAS 分原因：%s；空绘制（不足 3 顶点，算已覆盖）%llu 次、三角形带 %llu 次、不认的图元类型掩码 %#x",
        (unsigned long long)g_rts.n_frames, g_rts.ninst, g_rts.ndup, g_rts.nskin, g_rts.nskip_fmt, g_rts.nmesh, g_rts.cache_bytes / 1048576.0, (unsigned long long)(g_rts.budget >> 20), (unsigned long long)g_rts.n_evict_budget, (unsigned long long)g_rts.n_builds, g_rts.build_tris / 1e4, (unsigned long long)g_rts.n_traced, (unsigned long long)g_rts.n_skipped_draws, g_rts.n_rt_loaded, g_rts.n_rt_missing,
        (unsigned long long)g_rts.n_rt_frames, (unsigned long long)g_rts.n_fail_frames, (unsigned long long)g_rts.n_partial_frames, (unsigned long long)g_rts.n_orig_draws, g_rts.nslot, (unsigned long long)g_rts.n_slot_busy, g_rts.key_ns / 1e3 / MAX(g_rts.n_rt_frames, 1), g_rts.key_bytes / 1024.0 / MAX(g_rts.n_rt_frames, 1), rts_unc_str(ubuf, sizeof ubuf, g_rts.n_unc, NULL), (unsigned long long)g_rts.n_empty, (unsigned long long)g_rts.n_strip, g_rts.prim_mask);
    // 1. 就绪闸门：上一帧（必须紧挨着）完整才走光追；光追帧失败 → 后面几帧不追踪，退避 2、4 … 512 帧再试。失败的那一帧本身阴影图没被清，原版阴影还在（六审 5.1）
    // 1. Readiness gate: ray trace only if the previous (adjacent) frame was complete; a failed ray-traced frame → no tracing for a while, retry after 2, 4 … 512 frames. The failing frame itself keeps its uncleared original shadow map (sixth review 5.1)
    const char *why = g_rts.n_frames && g_rts.frame + 1 == f ? rts_frame_fail() : "上一帧没经过光追模块";
    if (g_rts.rt_frame && g_rts.frame + 1 == f) {
        if (g_rts.nuncov && !why) { g_rts.n_partial_frames++;
            if (++g_rts.n_unc_log <= 8 || g_rts.n_unc_log % 100 == 0) MLOG("光追阴影：帧 %llu 有 %d 次阴影绘制没进 TLAS，整帧照原版（%s；第 %llu 帧）", (unsigned long long)g_rts.frame, g_rts.nuncov, rts_unc_str(ubuf, sizeof ubuf, NULL, g_rts.f_unc), (unsigned long long)g_rts.n_unc_log); }
        if (why) { g_rts.backoff = g_rts.backoff ? MIN(g_rts.backoff * 2, 512) : 2; g_rts.retry_at = f + (uint64_t)g_rts.backoff; g_rts.n_fail_frames++;
            if (g_rts.n_fail_frames <= 8 || g_rts.n_fail_frames % 100 == 0) MLOG("光追阴影：回退原版阴影图（帧 %llu %s，当帧阴影图未清、原版阴影保留，%d 帧后再试，第 %llu 次）", (unsigned long long)g_rts.frame, why, g_rts.backoff, (unsigned long long)g_rts.n_fail_frames); }
        else if (g_rts.backoff && g_rts.map_cleared) { MLOG("光追阴影：恢复（帧 %llu 光追完整，此前退避 %d 帧）", (unsigned long long)g_rts.frame, g_rts.backoff); g_rts.backoff = 0; }
    }
    g_rts.rt_frame = !why && f >= g_rts.retry_at; g_rts.n_rt_frames += g_rts.rt_frame;
    // 2. 上一帧编进构建的网格转可用（同一队列里排在后面的 TLAS 构建一定在它之后执行） | 2. Meshes whose build was encoded last frame become usable (TLAS builds later on the same queue always run after it)
    for (int i = 0; i < g_rts.nsub; i++) g_rts.submitted[i]->state = RTS_READY;
    g_rts.nsub = 0;
    // 3. 没建的排队作废（源地址只在当帧有效）：网格留在缓存里待建，下次命中重新排队 | 3. Unbuilt queue entries are dropped (source addresses are per frame): meshes stay cached as pending and are queued again on the next hit
    for (int i = 0; i < g_rts.npend; i++) g_rts.pending[i]->queued = 0;
    g_rts.npend = 0;
    for (int i = 0; i < g_rts.nsmap; i++) g_rts.smap[i].t = nil;
    g_rts.nsmap = 0; g_rts.cur_smap = -1; g_rts.nuncov = 0; g_rts.map_cleared = 0; g_rts.main_hdr = nil; g_rts.why[0] = 0; memset(g_rts.f_unc, 0, sizeof g_rts.f_unc); g_rts.pass_why = RTS_UNC_PASS;
    g_rts.frame = f; g_rts.n_frames++; g_rts.ninst = g_rts.ndup = g_rts.nskin = g_rts.nskip_fmt = 0; g_rts.have_cam = g_rts.have_sun = 0; g_rts.main_seen = g_rts.mask_done = g_rts.overlay_done = 0;
    // 4. 总字节预算（六审 5.1）：超了从最久没用的开始淘汰（这时本帧还没用任何网格，淘汰的只会在早先的提交里在途，defer 等它们做完） | 4. Total byte budget (sixth review 5.1): over it, evict starting from the least recently used (no mesh is used by this frame yet; evicted ones can only be in flight in earlier submissions, and defer waits for those)
    for (int n = 0; g_rts.cache_bytes > g_rts.budget && g_rts.nmesh && n < 256; n++) {
        rts_mesh *old = NULL; for (uint32_t i = 0; i < g_rts.meshes.cap; i++) { rts_mesh *m = g_rts.meshes.vals[i]; if (m && (!old || m->last_frame < old->last_frame)) old = m; }
        if (!old) break; rts_mesh_drop(old); g_rts.n_evict_budget++;
    }
    // 网格缓存淘汰：600 帧没用过的释放（待建的也算：从没建成的网格不能一直占着） | Mesh cache eviction: release those unused for 600 frames (pending ones too: a mesh that never got built must not stay forever)
    if (g_rts.n_frames % 120 == 0 && g_rts.nmesh) { rts_mesh *dead[64]; int nd = 0;
        for (uint32_t i = 0; i < g_rts.meshes.cap && nd < 64; i++) { rts_mesh *m = g_rts.meshes.vals[i]; if (m && f - m->last_frame > 600) dead[nd++] = m; }
        for (int i = 0; i < nd; i++) rts_mesh_drop(dead[i]); }
}
// rp_begin_ex 每开一个渲染通道调一次（六审 5.1）：阴影通道（无颜色、方形 2D 深度）登记它的深度附件，以及本帧第一次挂它时是否清除、清成什么——光追成功后主通道前按这个值把阴影图清回全亮
// Called by rp_begin_ex for every render pass it opens (sixth review 5.1): a shadow pass (no color, square 2D depth) registers its depth attachment, and whether the first pass on it this frame cleared it and to what - after a successful trace the shadow map is cleared back to fully lit with that value before the main pass
static void rts_note_pass(RPD *pd) {
    if (!rts_on()) return;
    uint64_t f = atomic_load(&g_cur_frame); if (f != g_rts.frame) rts_frame_begin(f);
    g_rts.cur_smap = -1; g_rts.pass_why = RTS_UNC_PASS;
    id<MTLTexture> t = pd.depthAttachment.texture;
    if (R.rt.n || !R.rt.depth || R.rt.depth->d.width != R.rt.depth->d.height) return;
    if (!t) { g_rts.pass_why = RTS_UNC_NODEPTH; return; }   // 前瞻证明本通道不用深度（只挂模板 / 都不挂） | the lookahead proved this pass does not use depth (stencil only / nothing)
    if (t.textureType == MTLTextureTypeCube || t.textureType == MTLTextureTypeCubeArray) return;
    int i = 0; for (; i < g_rts.nsmap; i++) if (g_rts.smap[i].t == t && g_rts.smap[i].level == pd.depthAttachment.level && g_rts.smap[i].slice == pd.depthAttachment.slice) break;
    if (i == g_rts.nsmap) { if (i >= RTS_MAX_SMAP) { g_rts.pass_why = RTS_UNC_SMAPFULL; return; } g_rts.nsmap++;
        g_rts.smap[i] = (typeof(g_rts.smap[0])){t, pd.depthAttachment.level, pd.depthAttachment.slice, pd.depthAttachment.clearDepth, pd.depthAttachment.loadAction == MTLLoadActionClear, 0}; }
    else if (pd.depthAttachment.loadAction == MTLLoadActionClear) { g_rts.smap[i].cleared = 1; g_rts.smap[i].cd = pd.depthAttachment.clearDepth; }   // 本帧后来又整张清过：之前画的都没了，按这次的清除值恢复 | cleared again later this frame: earlier draws are gone, restore with this clear value
    g_rts.cur_smap = i;
}
// clear_tex（游戏的 ClearTexture，自己开只清除的通道、不经 rp_begin_ex）清深度时调：方形 2D / 2D 数组深度登记为本帧清过、清成 cd。
// 真机 4978d39：局内阴影图由 ClearTexture 清、阴影通道以读入开，没登记这次清除 → 判成清不回全亮，局内一帧都没追踪
// Called by clear_tex (the game's ClearTexture, which opens its own clear-only pass without rp_begin_ex) for depth clears: a square 2D / 2D-array depth is registered as cleared this frame, to cd.
// Device run of 4978d39: in game the shadow maps are cleared by ClearTexture and the shadow passes open with a load; this clear was not registered → judged not restorable, and not a single in-game frame traced
static void rts_note_clear(id<MTLTexture> t, NSUInteger level, NSUInteger slice, double cd) {
    if (!rts_on() || !t || t.width != t.height || (t.textureType != MTLTextureType2D && t.textureType != MTLTextureType2DArray)) return;
    uint64_t f = atomic_load(&g_cur_frame); if (f != g_rts.frame) rts_frame_begin(f);
    int i = 0; for (; i < g_rts.nsmap; i++) if (g_rts.smap[i].t == t && g_rts.smap[i].level == level && g_rts.smap[i].slice == slice) break;
    if (i == g_rts.nsmap) { if (i >= RTS_MAX_SMAP) return; g_rts.nsmap++; g_rts.smap[i] = (typeof(g_rts.smap[0])){t, level, slice, cd, 1, 0}; }
    else { g_rts.smap[i].cleared = 1; g_rts.smap[i].cd = cd; }
}
// 阴影通道的一次绘制 → 网格（新的或待建的：排队构建）+ 实例。返回 1 = 这次绘制由本帧 TLAS 里的可用 BLAS 代表 | One shadow-pass draw → mesh (new or pending: queued for a build) + instance. Returns 1 = the draw is represented by a usable BLAS in this frame's TLAS
// 算本次绘制的网格键并取缓存项：新网格建对象、待建的排队（新网格的顶点范围在这里校验）；返回 NULL = 这次绘制没有可用的网格 | Compute this draw's mesh key and fetch the cache entry: create objects for a new mesh, queue a pending one (the vertex range is validated here); NULL = no usable mesh for this draw
// 三角形带 → 三角形列表（32 位索引，相对 vbase）：第 k 个三角形偶数为 (k, k+1, k+2)、奇数为 (k+1, k, k+2)；带索引的遇到重启值（16 位 0xFFFF、32 位 0xFFFFFFFF，Metal 带图元恒开重启）从下一个索引起新带。返回三角形数；out 为 NULL 只数
// Triangle strip → triangle list (32-bit indices relative to vbase): triangle k is (k, k+1, k+2) when even, (k+1, k, k+2) when odd; an indexed strip restarts after the restart value (16-bit 0xFFFF, 32-bit 0xFFFFFFFF; Metal strips always restart). Returns the triangle count; out NULL only counts
static uint32_t rts_strip_expand(const uint8_t *ib, int isz, int n, uint32_t *out) {
    uint32_t nt = 0, w[3] = {0}; int run = 0, par = 0; uint32_t rs = isz == 2 ? 0xFFFFu : 0xFFFFFFFFu;
    for (int k = 0; k < n; k++) {
        uint32_t v = !ib ? (uint32_t)k : isz == 2 ? ((const uint16_t *)ib)[k] : ((const uint32_t *)ib)[k];
        if (ib && v == rs) { run = 0; par = 0; continue; }
        w[0] = w[1]; w[1] = w[2]; w[2] = v;
        if (++run >= 3) { if (out) { uint32_t *t = out + nt * 3; if (par) { t[0] = w[1]; t[1] = w[0]; } else { t[0] = w[0]; t[1] = w[1]; } t[2] = w[2]; } nt++; par ^= 1; }
    }
    return nt;
}
static rts_mesh *rts_mesh_get(const p_draw *dw, const p_draw_idx *di, int n, int strip, va_st at, vs_st sm, NVNbufferRange vb, uint64_t *pkey, uint64_t t0) {
    uint64_t key = *pkey;
    int isz = di ? (di->itype == 1 ? 2 : 4) : 0; uint64_t ibytes = 0; const uint8_t *ib = NULL; uint64_t nv = (uint64_t)n; int64_t vbase = dw ? dw->first : di->base_vertex;
    if (di) { if (__builtin_mul_overflow((uint64_t)n, (uint64_t)isz, &ibytes) || !(ib = rts_mem(di->ibuf, ibytes))) { g_rts.unc_why = RTS_UNC_IDX; return NULL; }
        uint32_t mx = 0, rs = strip ? (isz == 2 ? 0xFFFFu : 0xFFFFFFFFu) : 0;   // 带的重启值不算顶点 | a strip's restart value is not a vertex
        if (isz == 2) { for (int k = 0; k < n; k++) { uint32_t v = ((const uint16_t *)ib)[k]; if (v > mx && (!rs || v != rs)) mx = v; } } else { for (int k = 0; k < n; k++) { uint32_t v = ((const uint32_t *)ib)[k]; if (v > mx && (!rs || v != rs)) mx = v; } }
        nv = (uint64_t)mx + 1; key = rts_digest(key, ib, ibytes); }
    // 解码核读第 vbase … vbase+nv−1 个顶点的属性 0：这段要在顶点缓冲声明的范围内，整段也要落在内存池里；乘加都查溢出 | The decode kernel reads attribute 0 of vertices vbase … vbase+nv−1: that range must lie within the declared vertex buffer and entirely inside a memory pool; every multiply / add is overflow-checked
    uint64_t asz = rts_attr_bytes(at.fmt), vlast, vend;
    if (vbase < 0 || nv > UINT32_MAX || __builtin_mul_overflow((uint64_t)vbase + nv - 1, (uint64_t)sm.stride, &vlast) || __builtin_add_overflow(vlast, (uint64_t)at.offset + asz, &vend) || vend > vb.size) { g_rts.unc_why = RTS_UNC_VRANGE; return NULL; }
    void *mb; uint64_t mo, vgpu; if (!mtl_gpu_addr(vb.address + at.offset, vend - at.offset, &mb, &mo, &vgpu)) { g_rts.unc_why = RTS_UNC_VPOOL; return NULL; }
    const uint8_t *vp = rts_mem(vb.address + at.offset, vend - at.offset); if (!vp) { g_rts.unc_why = RTS_UNC_VPOOL; return NULL; }
    key = rts_vdigest(key, vp, (uint64_t)vbase, nv, sm.stride, asz);
    g_rts.key_ns += bp_now() - t0; g_rts.key_bytes += ibytes + nv * asz;
    rts_mesh *m = nvn_hm_get(&g_rts.meshes, &key);
    if (!m || (m->state == RTS_PENDING && !m->queued)) {   // 新网格，或待建而本帧还没排队（缺相机 / 深度、构建失败、配额满、关了再开之后都会落到这里） | new mesh, or pending and not queued this frame (after a missing camera / depth, a failed build, a full quota, off then on)
        if (g_rts.npend >= RTS_BUILDS_PER_FRAME) { g_rts.unc_why = RTS_UNC_QUOTA; return NULL; }   // 本帧配额用完：下一帧再建 | quota for this frame used up: build next frame
        if (!m) {
            uint32_t stri = strip ? rts_strip_expand(ib, isz, n, NULL) : 0;
            if (strip && !stri) { g_rts.unc_why = RTS_UNC_N; return NULL; }   // 带里没有完整三角形（全是重启）：画不出东西，算已覆盖 | no complete triangle in the strip (all restarts): draws nothing, counts as covered
            m = calloc(1, sizeof *m); m->key = key; m->nverts = (uint32_t)nv; m->tris = strip ? stri : (uint32_t)(n / 3); m->isz = strip ? 4 : (uint32_t)isz;
            m->pos = [g_dev newBufferWithLength:(NSUInteger)nv * 12 options:MTLResourceStorageModePrivate];
            if (strip) { m->ib = [g_dev newBufferWithLength:(NSUInteger)stri * 12 options:MTLResourceStorageModeShared]; if (m->ib) rts_strip_expand(ib, isz, n, m->ib.contents); }   // 带展开成列表（32 位） | strip expanded into a list (32-bit)
            else if (ib) { m->ib = [g_dev newBufferWithBytes:ib length:(NSUInteger)ibytes options:MTLResourceStorageModeShared]; }   // 索引拷一份：游戏的索引内存会被复用 | copy the indices: the game's index memory gets reused
            MTL4PrimitiveAccelerationStructureDescriptor *pd = [MTL4PrimitiveAccelerationStructureDescriptor new]; MTL4AccelerationStructureTriangleGeometryDescriptor *gd = [MTL4AccelerationStructureTriangleGeometryDescriptor new];
            gd.vertexBuffer = (MTL4BufferRange){m->pos.gpuAddress, m->pos.length}; gd.vertexFormat = MTLAttributeFormatFloat3; gd.vertexStride = 12; gd.triangleCount = m->tris; gd.opaque = YES;
            if (m->ib) { gd.indexBuffer = (MTL4BufferRange){m->ib.gpuAddress, m->ib.length}; gd.indexType = m->isz == 2 ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32; }
            pd.geometryDescriptors = @[gd]; pd.usage = MTLAccelerationStructureUsageNone;
            MTLAccelerationStructureSizes sz = [g_dev accelerationStructureSizesWithDescriptor:pd];
            m->as = [g_dev newAccelerationStructureWithSize:sz.accelerationStructureSize];
            if (!m->pos || !m->as || ((ib || strip) && !m->ib)) { defer_obj_release(m->pos); defer_obj_release(m->as); defer_obj_release(m->ib); m->as = nil; m->pos = m->ib = nil; free(m); g_rts.unc_why = RTS_UNC_ALLOC; return NULL; }
            if (sz.buildScratchBufferSize > g_rts.scratch_len) { defer_obj_release(g_rts.scratch); g_rts.scratch_len = sz.buildScratchBufferSize * 2; g_rts.scratch = [g_dev newBufferWithLength:g_rts.scratch_len options:MTLResourceStorageModePrivate];
                if (!g_rts.scratch) { g_rts.failed = 1; MLOG("【红】光追阴影：暂存缓冲 %lu KiB 建不出（退回原版阴影图）", (unsigned long)(g_rts.scratch_len >> 10)); } else res_add(g_rts.scratch); }
            res_add(m->pos); res_add(m->as); if (m->ib) res_add(m->ib);
            m->bytes = m->as.allocatedSize + m->pos.length + (m->ib ? m->ib.length : 0);
            nvn_hm_put(&g_rts.meshes, &key, m); g_rts.nmesh++; g_rts.cache_bytes += m->bytes;
            (void)pd;   // 构建描述在 rts_build 里按网格重建（描述对象不保存） | the build descriptor is recreated per mesh in rts_build (descriptor objects are not kept)
        }
        g_rts.pending[g_rts.npend] = m; g_rts.pend_src[g_rts.npend] = (typeof(g_rts.pend_src[0])){vgpu, (uint32_t)nv, sm.stride, at.fmt, (uint32_t)vbase}; g_rts.npend++; m->queued = 1;
    }
    *pkey = key;
    return m;
}
static int rts_instance(const p_draw *dw, const p_draw_idx *di, int prim, uint64_t f) {
    if (!g_rts.rt_frame) return 0;   // 不是光追帧（退避中）：不算键、不查缓存（真机键摘要每帧上毫秒，退避帧白算）；新网格在下一个光追帧排队 | not a ray-traced frame (backing off): no key, no cache lookup (on the device the key digest costs over a millisecond per frame, wasted in backoff frames); new meshes queue in the next ray-traced frame
    int strip = prim == 5;   // 5 三角形带：建网格时展开成列表；7 四边形与生产光栅化一样按三角形列表画 | 5 triangle strip: expanded into a list when the mesh is built; 7 quads are drawn as a triangle list, as production rasterization does
    if (prim != 4 && prim != 7 && !strip) { g_rts.unc_why = RTS_UNC_PRIM; if (prim >= 0 && prim < 32) g_rts.prim_mask |= 1u << prim; return 0; }   // 只认三角形列表（抓帧里阴影通道全是） | triangle lists only (all shadow-pass draws in the capture)
    int n = dw ? dw->count : di->count; if (n < 3) { g_rts.n_empty++; return 1; }   // 不足 3 顶点画不出三角形：不留影子，算已覆盖（真机 d42c8ee 局内每帧 4–8 次「非三角形列表或不足 3 顶点」） | fewer than 3 vertices draw no triangle and leave no shadow: covered (device d42c8ee: 4-8 per frame in game of "not a triangle list or under 3 vertices")
    g_rts.n_strip += strip;
    if (S.nattr < 1 || S.attr[0].stream >= 16) { g_rts.unc_why = RTS_UNC_FMT; return 0; }
    va_st at = S.attr[0]; vs_st sm = S.stream[at.stream]; NVNbufferRange vb = S.vbuf[at.stream];
    if (!sm.stride || sm.divisor || !vb.address || (at.fmt != 0x2b && at.fmt != 0x2e && at.fmt != 0x29 && at.fmt != 0x22)) { g_rts.nskip_fmt++; g_rts.unc_why = RTS_UNC_FMT; return 0; }
    if (di && di->itype != 1 && di->itype != 2) { g_rts.nskip_fmt++; g_rts.unc_why = RTS_UNC_FMT; return 0; }
    const p_bind_buf *c0 = &S.ubo[0][0]; const uint8_t *m0 = c0->addr && c0->size >= 64 ? rts_mem(c0->addr, 64) : NULL; if (!m0) { g_rts.unc_why = RTS_UNC_MAT; return 0; }
    // 网格键：布局 + 全部已校验索引字节的摘要 + 所用顶点范围内属性 0 的摘要（池内存会被别的网格复用；同地址的后段变了，键也要变：六审 5.2）。只读校验过的字节（五审 Q5-01）
    // Mesh key: layout + digest of every validated index byte + digest of attribute 0 over the vertex range used (pool memory gets reused by other meshes; a later change at the same address must change the key: sixth review 5.2). Validated bytes only (fifth review Q5-01)
    uint64_t t0 = bp_now();
    uint64_t key = 1469598103934665603ull; uint64_t lay[7] = {vb.address + at.offset, sm.stride, at.fmt, dw ? 0 : di->ibuf, (uint64_t)n, dw ? (uint64_t)dw->first : (uint64_t)(int64_t)di->base_vertex, (uint64_t)strip};
    key = rts_fnv(key, lay, sizeof lay);
    // 本帧里布局完全相同的重复绘制（两级级联画同一实例）沿用这一帧已算出的键：摘要每帧每份几何只算一次 | A repeated draw with an identical layout in this frame (both cascades draw the same instance) reuses the key computed this frame: each piece of geometry is digested once per frame
    uint64_t lk = key; typeof(g_rts.memo[0]) *mo_ = &g_rts.memo[lk & (RTS_MEMO - 1)]; rts_mesh *m = NULL;
    if (mo_->frame == f && mo_->lay == lk && (m = nvn_hm_get(&g_rts.meshes, &mo_->key)) && (m->state != RTS_PENDING || m->queued)) key = mo_->key;
    else { if (!(m = rts_mesh_get(dw, di, n, strip, at, sm, vb, &key, t0))) { if (g_rts.unc_why == RTS_UNC_N) { g_rts.n_empty++; return 1; } return 0; } *mo_ = (typeof(*mo_)){f, lk, key}; }
    m->last_frame = f;
    if (m->state != RTS_READY || g_rts.ninst >= RTS_MAX_INST) { g_rts.unc_why = m->state != RTS_READY ? RTS_UNC_PENDING : RTS_UNC_INSTFULL; return 0; }   // 网格还没可用 / 实例满 | mesh not usable yet / instances full
    const float *M = (const float *)m0; uint64_t tag = key ^ rts_fnv(1469598103934665603ull, m0, 48);
    for (int i = 0; i < g_rts.ninst; i++) if (g_rts.inst_tag[i] == tag) { g_rts.ndup++; return 1; }   // 两级级联画同一实例：只进一次 | both cascades draw the same instance: enter once
    if (rt_is_skin()) g_rts.nskin++;
    MTLIndirectAccelerationStructureInstanceDescriptor *d = &g_rts.inst[g_rts.ninst]; memset(d, 0, sizeof *d);
    for (int c = 0; c < 4; c++) { d->transformationMatrix.columns[c].x = M[c * 4 + 0]; d->transformationMatrix.columns[c].y = M[c * 4 + 1]; d->transformationMatrix.columns[c].z = M[c * 4 + 2]; }   // 行向量 3x4 的第 c 行 = 列主序的第 c 列 | row c of the row-vector 3x4 = column c in column-major
    d->options = MTLAccelerationStructureInstanceOptionOpaque; d->mask = 0xff; d->accelerationStructureID = m->as.gpuResourceID;
    g_rts.inst_tag[g_rts.ninst] = tag; g_rts.inst_mesh[g_rts.ninst] = m; g_rts.ninst++;
    return 1;
}
// 阴影通道的每次绘制都照原版画进阴影图（返回 0）：光追成功、而且本帧每次阴影绘制都由 TLAS 代表时，主通道之前再把阴影图清回全亮（rts_clear_maps），否则原版阴影留着——失败的那一帧本身也有影子（六审 5.1）。
// 代价：开光追时阴影图照常光栅化（与关光追相同），成功帧多一趟清阴影图。深度预渲染（无颜色、非方形深度）的绘制只记相机
// Every shadow-pass draw is encoded into the shadow map as original (returns 0): when the trace succeeds and every shadow draw of this frame is represented by the TLAS, the shadow map is cleared back to fully lit before the main pass (rts_clear_maps); otherwise the original shadows stay - the failing frame itself has shadows too (sixth review 5.1).
// Cost: with ray tracing on the shadow map is rasterized as usual (same as off), plus one shadow-map clear per successful frame. Depth-prepass draws (no color, non-square depth) only note the camera
static int rts_draw(NvProg *pr, const p_draw *dw, const p_draw_idx *di, int prim) {
    if (!rts_on() || !pr || !pr->sh[0]) return 0;
    uint64_t f = atomic_load(&g_cur_frame); if (f != g_rts.frame) rts_frame_begin(f);
    int shadow = !R.rt.n && R.rt.depth && R.rt.depth->d.width == R.rt.depth->d.height;
    if (shadow) { id<MTLTexture> dt = tex_mtl(R.rt.depth); if (!dt || dt.textureType == MTLTextureTypeCube || dt.textureType == MTLTextureTypeCubeArray) return 0; }   // 点光立方体阴影照旧 | point-light cube shadows unchanged
    const p_bind_buf *c3 = &S.ubo[0][3]; int has_c3 = 0;
    { NvShader *vs = pr->sh[0]; for (int i = 0; i < vs->rf.nb; i++) if (vs->rf.b[i].kind == NVN_RB_CBUF && vs->rf.b[i].reg == 3) has_c3 = 1; }
    if (!shadow) {
        if (!R.rt.n && R.rt.depth && has_c3 && !g_rts.main_seen && c3->addr && c3->size >= 400) { const uint8_t *b = rts_mem(c3->addr, 400); if (b) { memcpy(g_rts.cam, b + 336, 64); g_rts.have_cam = 1; } }
        return 0;
    }
    if (!rts_setup()) return 0;
    if (!g_rts.have_sun && has_c3 && c3->addr && c3->size >= 400) { const uint8_t *b = rts_mem(c3->addr, 400); if (b) { memcpy(g_rts.lvp, b + 336, 64); float d[3] = {g_rts.lvp[2], g_rts.lvp[6], g_rts.lvp[10]}; float n = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (n > 0) { float s = (d[1] > 0 ? 1.f : -1.f) / n; g_rts.sun[0] = d[0] * s; g_rts.sun[1] = d[1] * s; g_rts.sun[2] = d[2] * s; g_rts.have_sun = 1; } } }
    if (g_rts.cur_smap >= 0) g_rts.smap[g_rts.cur_smap].used = 1;
    g_rts.unc_why = -1; int in = rts_instance(dw, di, prim, f);
    if (in && g_rts.cur_smap >= 0) g_rts.n_skipped_draws++;
    else { g_rts.nuncov++; g_rts.n_orig_draws += g_rts.rt_frame;
        int r = in || g_rts.unc_why < 0 ? g_rts.pass_why : g_rts.unc_why;   // 进了 TLAS 但通道没登记，按通道的原因记 | in the TLAS but the pass was not registered: count the pass's reason
        if (g_rts.rt_frame) { g_rts.n_unc[r]++; g_rts.f_unc[r]++; } }   // 没进 TLAS（或这个阴影通道没登记、事后清不回去）：本帧整帧照原版 | not in the TLAS (or this shadow pass was not registered and could not be cleared back): the whole frame stays original
    return 0;
}
// 本帧登记过、画过阴影的阴影图是不是都能清回游戏自己的全亮值（第一次挂它的那个通道是清除通道） | Can every shadow map registered and drawn this frame be cleared back to the game's own fully-lit value (the first pass on it was a clearing pass)
static int rts_maps_restorable(void) {
    int any = 0; for (int i = 0; i < g_rts.nsmap; i++) if (g_rts.smap[i].used) { if (!g_rts.smap[i].cleared) {
        snprintf(g_rts.why, sizeof g_rts.why, "阴影图清不回全亮（第 %d / %d 张 %lux%lu 第 %lu 级第 %lu 层本帧第一次挂上是读入、本帧没清过）", i + 1, g_rts.nsmap,
                 (unsigned long)g_rts.smap[i].t.width, (unsigned long)g_rts.smap[i].t.height, (unsigned long)g_rts.smap[i].level, (unsigned long)g_rts.smap[i].slice); return 0; }
        any = 1; }
    if (!any) snprintf(g_rts.why, sizeof g_rts.why, "本帧没登记到画过的阴影图（登记 %d 张）", g_rts.nsmap);
    return any;
}
static int rts_clear_maps(void) {   // 光追成功：主通道前把本帧画过的阴影图清回全亮（原版阴影由遮罩接替） | Ray tracing succeeded: before the main pass, clear the shadow maps drawn this frame back to fully lit (the mask takes over from the original shadows)
    for (int i = 0; i < g_rts.nsmap; i++) { typeof(g_rts.smap[0]) *e = &g_rts.smap[i]; if (!e->used) continue;
        RPD *rp = [RPD new]; rp.depthAttachment.texture = e->t; rp.depthAttachment.level = e->level; rp.depthAttachment.slice = e->slice;
        rp.depthAttachment.loadAction = MTLLoadActionClear; rp.depthAttachment.clearDepth = e->cd; rp.depthAttachment.storeAction = MTLStoreActionStore;
        if (e->t.pixelFormat == MTLPixelFormatDepth32Float_Stencil8) { rp.stencilAttachment.texture = e->t; rp.stencilAttachment.level = e->level; rp.stencilAttachment.slice = e->slice; rp.stencilAttachment.loadAction = MTLLoadActionLoad; rp.stencilAttachment.storeAction = MTLStoreActionStore; }
        RENC en = [g_cb renderCommandEncoderWithDescriptor:rp];
        if (!en) { MLOG("【红】光追阴影：清阴影图的编码器建不出（第 %d 张），本帧不合成遮罩", i); return 0; }
        [en barrierAfterQueueStages:MTLStageAll beforeStages:MTLStageAll visibilityOptions:MTL4VisibilityOptionDevice]; m4_end(en); }
    return 1;
}
// 主通道设目标处：建待建的 BLAS（不需要相机 / 太阳 / 深度）；光追帧、本帧阴影绘制全在 TLAS 里、条件齐全时再建 TLAS、追踪写遮罩、把阴影图清回全亮。任何一步不成：阴影图不清，原版阴影照常（五审 Q5-02、六审 5.1）
// At the main pass's set-target: build pending BLAS (needs no camera / sun / depth); in a ray-traced frame whose shadow draws are all in the TLAS and with everything present, also build the TLAS, trace into the mask and clear the shadow maps back to fully lit. If any step fails the shadow maps stay uncleared with the original shadows (fifth review Q5-02, sixth review 5.1)
static void rts_unqueue(void) { for (int i = 0; i < g_rts.npend; i++) g_rts.pending[i]->queued = 0; g_rts.npend = 0; }
static void rts_build_and_trace(id<MTLTexture> depth) {
    id<MTLTexture> hdr = g_rts.main_hdr; int need_overlay = g_rts.dbg || !g_rts.n_rt_loaded;
    int trace = g_rts.rt_frame && g_rts.ninst && !g_rts.nuncov && g_rts.have_cam && g_rts.have_sun && depth && rts_maps_restorable();
    if (trace && need_overlay && !(trace = hdr && hdr.width == depth.width && hdr.height == depth.height && rts_overlay_setup(hdr.pixelFormat)))   // 合成做不成就别清阴影图 | if compositing cannot happen, do not clear the shadow maps
        snprintf(g_rts.why, sizeof g_rts.why, "合成做不成（HDR %lux%lu、深度 %lux%lu、合成管线 %d）", (unsigned long)hdr.width, (unsigned long)hdr.height, (unsigned long)depth.width, (unsigned long)depth.height, g_rts.ps_overlay != nil);
    if (!trace && !g_rts.npend) return;
    if (trace && (!g_rts.mask || g_rts.mask.width != depth.width || g_rts.mask.height != depth.height)) {
        MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Unorm width:depth.width height:depth.height mipmapped:NO];
        d.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite; d.storageMode = MTLStorageModePrivate;
        defer_obj_release(g_rts.mask); g_rts.mask = [g_dev newTextureWithDescriptor:d];
        if (!g_rts.mask) { g_rts.failed = 1; rts_unqueue(); MLOG("【红】光追阴影：遮罩 %lux%lu 建不出（退回原版阴影图）", (unsigned long)depth.width, (unsigned long)depth.height); return; } res_add(g_rts.mask);
        MLOG("光追阴影：遮罩 %lux%lu", (unsigned long)depth.width, (unsigned long)depth.height); }
    if (g_rts.rt_frame && g_rts.ninst && !g_rts.nuncov && !depth) snprintf(g_rts.why, sizeof g_rts.why, "主通道没有深度");
    int si = trace ? rts_slot_pick() : -1; if (trace && si < 0) { trace = 0; snprintf(g_rts.why, sizeof g_rts.why, "常量 / 实例槽 %d 个都还在 GPU 上", g_rts.nslot); }
    if (!trace && !g_rts.npend) return;
    rp_end(); if (g_blit) { enc_b_end(g_blit); g_blit = nil; } ser_gate();
    CENC ce = (CENC)enc_c_begin(NULL);
    if (!ce) { rts_unqueue(); if (trace) snprintf(g_rts.why, sizeof g_rts.why, "计算编码器建不出"); { static int n; if (n++ < 3) MLOG("【红】光追阴影：计算编码器建不出，本帧不建 BLAS、不追踪（阴影图照原版，网格留待建，下次命中重排）"); } return; }
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
            m->state = RTS_SUBMITTED; m->queued = 0; if (g_rts.nsub < RTS_BUILDS_PER_FRAME) g_rts.submitted[g_rts.nsub++] = m; g_rts.n_builds++; g_rts.build_tris += m->tris; }
        g_rts.npend = 0;
    }
    if (!trace) { enc_c_end(ce); args_dirty_all(); g_pso_dirty = g_dss_dirty = 1; return; }
    // 2. TLAS（只放可用的 BLAS：实例登记时已核过，这里再核一遍）；实例与常量写进本帧取到的槽（GPU 已用完它） | 2. TLAS (usable BLAS only: checked when the instance was recorded, checked again here); instances and constants go into this frame's slot (the GPU is done with it)
    id<MTLBuffer> ibuf = g_rts.slot[si].inst, kbuf = g_rts.slot[si].k; MTLIndirectAccelerationStructureInstanceDescriptor *dst = ibuf.contents; int n = 0;
    for (int i = 0; i < g_rts.ninst; i++) if (g_rts.inst_mesh[i]->state == RTS_READY) dst[n++] = g_rts.inst[i];
    if (n != g_rts.ninst) { snprintf(g_rts.why, sizeof g_rts.why, "TLAS 实例里有网格不可用（%d / %d）", n, g_rts.ninst); enc_c_end(ce); args_dirty_all(); g_pso_dirty = g_dss_dirty = 1; return; }
    MTL4InstanceAccelerationStructureDescriptor *td = [MTL4InstanceAccelerationStructureDescriptor new];
    td.instanceDescriptorType = MTLAccelerationStructureInstanceDescriptorTypeIndirect; td.instanceCount = n; td.instanceDescriptorStride = sizeof(MTLIndirectAccelerationStructureInstanceDescriptor);
    td.instanceDescriptorBuffer = (MTL4BufferRange){ibuf.gpuAddress, (uint64_t)n * sizeof(MTLIndirectAccelerationStructureInstanceDescriptor)};
    [ce buildAccelerationStructure:g_rts.tlas descriptor:td scratchBuffer:(MTL4BufferRange){g_rts.scratch.gpuAddress, g_rts.scratch.length}];
    [ce barrierAfterEncoderStages:MTLStageAccelerationStructure beforeEncoderStages:MTLStageDispatch visibilityOptions:MTL4VisibilityOptionDevice];
    // 3. 追踪：常量 = 逆视投影（列主序 = 行向量矩阵求逆后原样当列）、太阳、尺寸 | 3. Trace: constants = inverse view-projection (column-major = the inverted row-vector matrix used as columns), sun, size
    double a[16], ia[16]; for (int i = 0; i < 16; i++) a[i] = g_rts.cam[i]; rx_inv4(a, ia);
    float *k = kbuf.contents; for (int i = 0; i < 16; i++) k[i] = (float)ia[i];
    k[16] = g_rts.sun[0]; k[17] = g_rts.sun[1]; k[18] = g_rts.sun[2]; k[19] = 0; ((uint32_t *)k)[20] = (uint32_t)depth.width; ((uint32_t *)k)[21] = (uint32_t)depth.height;
    static float bias = -1; if (bias < 0) { const char *e = getenv("DM_RT_SHADOW_BIAS"); bias = e ? (float)atof(e) : 0.6f; } k[22] = bias; k[23] = 0;
    [ce setArgumentTable:g_rts.at_trace]; enc_c_pso(ce, g_rts.ps_trace);
    [g_rts.at_trace setTexture:depth.gpuResourceID atIndex:0]; [g_rts.at_trace setTexture:g_rts.mask.gpuResourceID atIndex:1];
    [g_rts.at_trace setResource:g_rts.tlas.gpuResourceID atBufferIndex:0]; [g_rts.at_trace setAddress:kbuf.gpuAddress atIndex:1];
    enc_c_dispatch(ce, MTLSizeMake((depth.width + 7) / 8, (depth.height + 7) / 8, 1), MTLSizeMake(8, 8, 1));
    enc_c_end(ce);
    args_dirty_all(); g_pso_dirty = g_dss_dirty = 1;
    g_rts.mask_done = 1; g_rts.n_traced++;
    // 4. 阴影图清回全亮：之后的主通道只看遮罩 | 4. Clear the shadow maps back to fully lit: the main pass that follows sees only the mask
    g_rts.map_cleared = rts_clear_maps();
    { static int n; if (n++ < 3) MLOG("光追阴影：帧 %llu 实例 %d（蒙皮 %d）、遮罩 %lux%lu、太阳 (%.2f, %.2f, %.2f)、阴影图 %d 张清回全亮", (unsigned long long)g_rts.frame, g_rts.ninst, g_rts.nskin, (unsigned long)depth.width, (unsigned long)depth.height, g_rts.sun[0], g_rts.sun[1], g_rts.sun[2], g_rts.map_cleared ? g_rts.nsmap : 0); }
}
static void rts_overlay(id<MTLTexture> hdr) {   // 第一阶段合成：HDR × (amb + (1 − amb) · 遮罩)；只在阴影图已清回全亮时做（否则两层影） | phase-1 compositing: HDR × (amb + (1 − amb) · mask); only once the shadow maps were cleared back to fully lit (else shadows double up)
    if (!g_rts.mask_done || !g_rts.map_cleared || !hdr || hdr.width != g_rts.mask.width || hdr.height != g_rts.mask.height || !rts_overlay_setup(hdr.pixelFormat)) return;
    rp_end(); if (g_blit) { enc_b_end(g_blit); g_blit = nil; }
    id<MTLBuffer> dbuf = g_rts.slot[g_rts.cur].d; float *a = dbuf.contents; a[0] = g_rts.dbg == 2 ? 0.f : dm_rt_shadow_ambient(); a[1] = a[2] = a[3] = 0;   // 与本帧追踪同一槽 | the same slot as this frame's trace
    RPD *rp = [RPD new]; rp.colorAttachments[0].texture = hdr; rp.colorAttachments[0].loadAction = MTLLoadActionLoad; rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    RENC en = [g_cb renderCommandEncoderWithDescriptor:rp]; if (!en) return;
    [en barrierAfterQueueStages:MTLStageAll beforeStages:MTLStageAll visibilityOptions:MTL4VisibilityOptionDevice];
    [en setArgumentTable:g_rts.at_overlay atStages:MTLRenderStageFragment]; [g_rts.at_overlay setTexture:g_rts.mask.gpuResourceID atIndex:0]; [g_rts.at_overlay setAddress:dbuf.gpuAddress atIndex:0];
    [en setViewport:(MTLViewport){0, 0, (double)hdr.width, (double)hdr.height, 0, 1}]; [en setRenderPipelineState:g_rts.ps_overlay];
    [en drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3]; m4_end(en);
    args_dirty_all(); g_pso_dirty = g_dss_dirty = 1; g_rts.overlay_done = 1;
}
// _rt 变体的遮罩绑定：本帧光追生效（遮罩算了、阴影图清回全亮）就给遮罩，否则给 1x1 全亮（变体此时也不选） | Mask binding of the _rt variants: the mask when ray tracing applied this frame (mask traced, shadow maps cleared back), else a 1x1 fully lit texel (variants are not selected then either)
static id<MTLTexture> rts_mask_tex(void) { return g_rts.mask_done && g_rts.map_cleared && g_rts.mask ? g_rts.mask : g_rts.white; }
static uint64_t rts_mask_rid(void) { id<MTLTexture> t = rts_mask_tex(); return t ? t.gpuResourceID._impl : 0; }
// 片元 _rt 变体：开关开且本帧光追生效时用 <键>_rt（首次按需加载，没有该文件则记一次、沿用原版）；否则用原版 | Fragment _rt variant: with the switch on and ray tracing applied this frame use <key>_rt (loaded on demand, a missing file is noted once and the original kept); otherwise the original
static void rts_select_fs(NvProg *pr) {
    if (!pr || !pr->sh[1]) return;
    if (!pr->fs_base) pr->fs_base = pr->sh[1];
    int want = rts_on() && !g_rts.failed && g_rts.mask_done && g_rts.map_cleared && !g_rts.dbg;
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
            g_rts.main_seen = 1; g_rts.hdr_ser = r->color[0]->serial; g_rts.ds_ser = r->depth->serial; g_rts.main_hdr = c0;
            if (!g_rts.npend && !g_rts.ninst) return;
            rts_build_and_trace(dt); }
        return;
    }
    if (g_rts.mask_done && g_rts.map_cleared && !g_rts.overlay_done && (g_rts.dbg || !g_rts.n_rt_loaded) && r->n >= 1 && r->color[0] && r->color[0]->serial == g_rts.hdr_ser) rts_overlay(c0);   // 第一阶段合成：只在调试（DM_RT_SHADOW_DBG）或没有 _rt 变体时；主通道之后第一次再挂 HDR | phase-1 compositing: only for debugging or without _rt variants; first time the HDR is attached again after the main pass
}
#endif
