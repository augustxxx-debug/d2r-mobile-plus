// ---------- 光追阴影：用 Metal 硬件光追算太阳方向的逐像素可见度，替掉游戏自己的两级级联阴影图 ----------
// 怎么接（都在翻译层，不改游戏）：
//   1. 阴影通道（无颜色附件、方形 2D 深度）的每次绘制 = 一个遮挡体实例：网格按（顶点流地址、步长、格式、索引缓冲、数量、基顶点、图元 + 数据指纹）做键缓存 BLAS，
//      首次见到时用计算核把属性 0 的位置解码成 float3（SNORM16x4 · exp2(w·13)，与 vs_models_v 的 MSL 一致）再建；三角带先在 CPU 上展成三角列表；cb0 的物体→世界 3x4 进 TLAS 实例；
//      这些绘制本身不再编码（阴影图保持清空 = 游戏采样全亮）。
//   2. 深度预渲染（无颜色、主深度）的绘制里取 cb3 +336 的相机视投影；阴影通道的 cb3 +336 是光源视投影，其 z 列（取朝天那一侧）= 太阳方向。
//   3. 主不透明通道（RG11B10F + Depth32Float_Stencil8，宽 ≥ 500、非方形）的设目标处：建 TLAS、按主深度重建世界坐标、每像素向太阳发一条遮挡射线，写 R8 遮罩（3D 尺寸）。
//   4. 读级联阴影图的片元着色器换成 <键>_rt 变体（dm/gfx/tools/rts_variant.py 出，出包时生成），取 min(遮罩, 原级联采样)：原阴影图里只剩镂空遮挡体（树冠等片元会 discard 的绘制，照常光栅化、不进加速结构）；包里没有变体时退回整屏一趟 HDR × (amb + (1 − amb) · 遮罩)。
//   5. 第二阶段·蒙皮单位（rt_is_skin 的 10 个顶点着色器）：阴影通道里把这次绘制换成 <键>_pos 顶点变体、关光栅化照常编码，顶点阶段把蒙皮后的裁剪坐标写进本实例的槽缓冲；
//      主通道前用光源视投影的逆矩阵还原成世界坐标，以绑定姿势 BLAS 为源 refit 到本实例的槽 BLAS（每网格若干槽，按本帧出现次序分配），实例变换为单位阵。没有 _pos 变体时退回绑定姿势。
//   6. 第三阶段·点光阴影：主通道的片元着色器把光源记录读自纹理缓冲 fp_t_tcb_6A（每盏 4 个 texel：位置 / 颜色 / 标志 / 打包参数），本层在换上 _rt 变体的绘制处记下这块缓冲；
//      追踪太阳之后，在 CPU 上按「离画面中心视线近、亮」挑至多 4 盏（DM_RT_POINT_N），半分辨率下每像素各向一盏光发一条射线写 RGBA8 遮罩；_rt 变体在点光立方体阴影采样处按记录下标查遮罩（rts_pl），其余光照原样。
// 门槛：supportsRaytracing 且 GPU 家族 ≥ Apple9 且机型白名单（用户要求三重判定）；任一不满足 = 整个模块不建任何对象、每次调用立即返回。开关：DM_RT_SHADOW=0/1 钉住，否则 dm_rt_shadow_setting()；DM_RT_POINT=0 只关点光。
//   8. 无太阳的关卡（室内 / 地下：整帧没有阴影通道）：上一帧没见到阴影绘制时，遮挡体改从深度预渲染（无颜色、主深度）的绘制登记（照常编码，蒙皮按绑定姿势），只追点光、太阳遮罩保持全亮、不压暗画面。
//   7. 第四阶段·软阴影（可选，DM_RT_SOFT=<强度>，缺省 0 = 硬阴影）：太阳射线改取最近命中、把命中距离一起写进 RG8，再做两趟分离的深度感知模糊，半径 ∝ 周围最远的遮挡距离（近硬远软）；结果写回 R8 遮罩，_rt 变体不用改。
// 计时：光追的计算编码器走 pg_compute（DM_NVN_PASSGPU 开着时逐通道计时，并累计到每 600 帧的统计行）。只编 Metal 4 路径；DM_NVN_M3 下模块为空。
// ---------- Ray-traced shadows: per-pixel sun visibility via Metal hardware ray tracing, replacing the game's two cascaded shadow maps ----------
// How it hooks in (translation layer only, the game is untouched):
//   1. Every draw of a shadow pass (no color attachment, square 2D depth) = one occluder instance: meshes are cached as BLAS keyed by (vertex stream address, stride, format, index buffer, count, base vertex, primitive + data fingerprint);
//      on first sight a compute kernel decodes attribute 0 into float3 (SNORM16x4 · exp2(w·13), matching the vs_models_v MSL) before the build; triangle strips are expanded into lists on the CPU; cb0's object→world 3x4 goes into the TLAS instance;
//      the draws themselves are no longer encoded (the shadow map stays cleared = the game samples fully lit).
//   2. The camera view-projection is read from cb3 +336 of the depth-prepass draws (no color, main depth); cb3 +336 of the shadow pass is the light view-projection, whose z column (the side pointing up) = sun direction.
//   3. At the set-target of the main opaque pass (RG11B10F + Depth32Float_Stencil8, width ≥ 500, non-square): build the TLAS, reconstruct world positions from the main depth, shoot one occlusion ray per pixel toward the sun, write an R8 mask (3D size).
//   4. Fragment shaders that sample the cascades are swapped for <key>_rt variants (dm/gfx/tools/rts_variant.py, generated at packaging time) taking min(mask, original cascade sample): the original shadow maps keep only the alpha-tested occluders (foliage etc. whose fragment may discard; rasterized as usual, not in the acceleration structure); without variants in the package a fullscreen HDR × (amb + (1 − amb) · mask) pass is the fallback.
//   5. Phase 2 · skinned units (the 10 vertex shaders of rt_is_skin): in the shadow pass the draw is encoded as usual but with the <key>_pos vertex variant and rasterization off, so the vertex stage writes the skinned clip positions into this instance's slot buffer;
//      before the main pass they are unprojected with the inverse light view-projection into world space and refit from the bind-pose BLAS into this instance's slot BLAS (a few slots per mesh, assigned in order of appearance within the frame); the instance transform is identity. Without a _pos variant the bind pose is used.
//   6. Phase 3 · point-light shadows: the main-pass fragment shaders read light records from the texture buffer fp_t_tcb_6A (4 texels per light: position / color / flags / packed parameters); this layer notes that buffer at the draws using the _rt variant;
//      after the sun trace the CPU picks up to 4 lights (DM_RT_POINT_N) by "close to the screen-center view line, bright", and at half resolution one ray per pixel per light writes an RGBA8 mask; the _rt variants look the mask up by record index at the point-light cube-shadow sample (rts_pl), every other light stays as is.
// Gate: supportsRaytracing and GPU family ≥ Apple9 and a model allowlist (triple check requested by the user); if any fails the module creates nothing and every call returns immediately. Switch: DM_RT_SHADOW=0/1 pins it, otherwise dm_rt_shadow_setting(); DM_RT_POINT=0 turns off point lights only.
//   8. Levels without a sun (indoor / underground: no shadow pass in the whole frame): when the previous frame saw no shadow draw, occluders are registered from the depth-prepass draws (no color, main depth; encoded as usual, skinned units in bind pose), only point lights are traced, the sun mask stays fully lit and nothing is darkened.
//   7. Phase 4 · soft shadows (optional, DM_RT_SOFT=<strength>, default 0 = hard): the sun ray takes the nearest hit and writes the hit distance into an RG8 texture, then two separable depth-aware blur passes with a radius ∝ the widest blocker distance around (near hard, far soft); the result goes back into the R8 mask, the _rt variants are unchanged.
// Timing: the ray-tracing compute encoder goes through pg_compute (per-pass timing when DM_NVN_PASSGPU is on, also accumulated into the 600-frame statistics line). Metal 4 path only; under DM_NVN_M3 the module is empty.
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
static uint64_t rts_pos_gpu(void) { return 0; }
static id<MTLBuffer> rts_pos_buf(void) { return nil; }
static uint64_t rts_pmask_rid(void) { return 0; }
static id<MTLTexture> rts_pmask_tex(void) { return nil; }
static uint64_t rts_plidx_gpu(void) { return 0; }
static id<MTLBuffer> rts_plidx_buf(void) { return nil; }
int mtl_rt_shadow_supported(void) { return 0; }
#else
#define RTS_MAX_INST 4096
#define RTS_BUILDS_PER_FRAME 12   // 每帧最多新建这么多 BLAS（新网格首次出现时），其余下一帧再建，避免一次卡顿 | At most this many new BLAS builds per frame (first appearance of new meshes); the rest wait for the next frame to avoid a hitch
#define RTS_MAX_EXPORT 256   // 每帧最多这么多蒙皮实例走位置导出 + refit | At most this many skinned instances per frame go through position export + refit
#define RTS_MAX_SLOTS 64
#define RTS_LIGHT_TBUF_REG 8   // 主通道片元着色器的光源记录纹理缓冲 fp_t_tcb_6A 所在贴图槽（230 个 _rt 变体一致） | texture slot of the main-pass fragment shaders' light record texture buffer fp_t_tcb_6A (the same in all 230 _rt variants)   // 每个蒙皮网格最多这么多实例槽（同屏同模型的单位数） | At most this many instance slots per skinned mesh (units of the same model on screen)
typedef struct { id<MTLAccelerationStructure> as; id<MTLBuffer> pos4, pos3, lvp; } rts_slot;   // lvp：3 × 256 字节（逆光源视投影 16 floats + 顶点数），按 ri 轮换 | lvp: 3 × 256 bytes (inverse light VP 16 floats + vertex count), rotated by ri
typedef struct { uint64_t key; id<MTLAccelerationStructure> as; id<MTLBuffer> pos, ib; uint64_t last_frame; uint32_t tris, nverts, isz; int built, skin; NSUInteger as_size; rts_slot slots[RTS_MAX_SLOTS]; int nslots, used; } rts_mesh;
static struct {
    int on, gate, dbg, failed; uint64_t frame;
    nvn_hmap meshes; int nmesh;
    MTLIndirectAccelerationStructureInstanceDescriptor inst[RTS_MAX_INST]; uint64_t inst_tag[RTS_MAX_INST]; int ninst, ndup, nskip_fmt, nskip_prim, nskin, nskin_pose, nalpha;
    rts_mesh *pending[RTS_BUILDS_PER_FRAME]; struct { uint64_t gpu; uint32_t n, stride, fmt, base; } pend_src[RTS_BUILDS_PER_FRAME]; int npend;
    rts_mesh *exp_mesh[RTS_MAX_EXPORT]; rts_slot *exp_slot[RTS_MAX_EXPORT]; int nexp; id<MTLBuffer> cur_pos4;   // 本帧的蒙皮导出；cur_pos4 = 正在编码的那次导出绘制写的缓冲 | this frame's skinned exports; cur_pos4 = the buffer the export draw being encoded writes
    float cam[16]; int have_cam, have_sun; float sun[3];
    int main_seen, mask_done, overlay_done; uint64_t hdr_ser, ds_ser;
    int shadow_draws, prepass_occ, nprepass;   // 本帧阴影通道绘制数；本帧遮挡体来自深度预渲染（上一帧没有阴影绘制 = 无太阳关卡）；本帧登记的预渲染遮挡体数 | shadow-pass draws this frame; occluders from the depth prepass this frame (no shadow draw last frame = level without a sun); prepass occluders registered this frame
    id<MTLComputePipelineState> ps_decode, ps_unproj, ps_trace; id<MTLRenderPipelineState> ps_overlay; id<MTL4ArgumentTable> at_decode, at_trace, at_overlay;
    id<MTLBuffer> instbuf[3], kbuf[3], dbuf[3]; int ri;
    id<MTLAccelerationStructure> tlas; id<MTLBuffer> scratch; NSUInteger scratch_len;
    id<MTLTexture> mask, white; int n_rt_loaded, n_rt_missing, n_pos_loaded, n_pos_missing;
    // 点光：光源记录缓冲（主通道片元的纹理缓冲 fp_t_tcb_6A）、本帧挑中的光、半分辨率遮罩、记录下标 int4（三份轮换 + 一份全 −1） | point lights: light record buffer (the main-pass fragment's texture buffer fp_t_tcb_6A), this frame's picks, the half-resolution mask, the int4 of record indices (3 rotating + one all −1)
    int point_on, point_n; id<MTLBuffer> lbuf; uint64_t lbuf_off; uint32_t lbuf_texels; int lbuf_tid; MTLPixelFormat lbuf_fmt;
    float soft, soft_maxd; id<MTLComputePipelineState> ps_trace_soft, ps_blur; id<MTL4ArgumentTable> at_blur; id<MTLTexture> mask_raw, mask_tmp; id<MTLBuffer> bbuf[3][2];   // 软阴影 | soft shadows
    id<MTLComputePipelineState> ps_ptrace; id<MTLTexture> pmask; id<MTLBuffer> plidx[3], plidx_none, pkbuf[3]; int npl, pl_done; float plpos[4][3]; int plrec[4]; uint64_t n_ptraced;
    uint64_t n_frames, n_builds, n_traced, n_skipped_draws, n_refit; double build_tris;
    const p_draw *last_dw; const p_draw_idx *last_di; uint64_t last_f, last_prog; int last_r;   // 同一次绘制再问（draw 重入）给同一个答案 | the same draw asked again (draw re-entry) gets the same answer
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
// 蒙皮导出的裁剪坐标 → 世界坐标（光源投影是正交的，逆矩阵精确） | skinned clip positions → world (the light projection is orthographic, the inverse is exact)
"struct U { float4x4 inv; uint n; };\n"
"kernel void rts_unproj(device const float4 *src [[buffer(0)]], device packed_float3 *dst [[buffer(1)]], constant U &u [[buffer(2)]], uint i [[thread_position_in_grid]]) {\n"
"  if (i >= u.n) return; float4 w = u.inv * src[i]; dst[i] = packed_float3(w.xyz / w.w); }\n"
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
// 软阴影：最近命中 + 命中距离（按 maxd 归一）→ RG8；模糊：半径 = 周围 ±2 像素内最远遮挡距离 × 强度（≤ 12 像素），深度差大的邻居权重小 | soft shadows: nearest hit + hit distance (normalized by maxd) → RG8; blur: radius = widest blocker distance within ±2 px × strength (≤ 12 px), neighbours with a large depth difference weigh less
"struct KS { float4x4 invVP; float4 sun; uint2 size; float bias; float maxd; };\n"
"kernel void rts_trace_soft(depth2d<float, access::read> depth [[texture(0)]], texture2d<float, access::write> mask [[texture(1)]], instance_acceleration_structure as [[buffer(0)]], constant KS &k [[buffer(1)]], uint2 g [[thread_position_in_grid]]) {\n"
"  if (g.x >= k.size.x || g.y >= k.size.y) return; float z = depth.read(g);\n"
"  if (z >= 1.0) { mask.write(float4(1, 0, 0, 0), g); return; }\n"
"  float2 ndc = float2((float(g.x) + 0.5) / float(k.size.x) * 2.0 - 1.0, 1.0 - (float(g.y) + 0.5) / float(k.size.y) * 2.0);\n"
"  float4 w = k.invVP * float4(ndc, z, 1.0); float3 p = w.xyz / w.w;\n"
"  ray r; r.origin = p + k.sun.xyz * k.bias; r.direction = k.sun.xyz; r.min_distance = 0.0; r.max_distance = 20000.0;\n"
"  intersector<instancing> is; is.assume_geometry_type(geometry_type::triangle); is.force_opacity(forced_opacity::opaque);\n"
"  intersection_result<instancing> h = is.intersect(r, as, 0xffu); bool hit = h.type != intersection_type::none;\n"
"  mask.write(float4(hit ? 0.0 : 1.0, hit ? min(h.distance / k.maxd, 1.0) : 0.0, 0, 0), g); }\n"
"struct B { uint2 size; int2 dir; float soft; float pad; };\n"
"kernel void rts_blur(texture2d<float, access::read> src [[texture(0)]], texture2d<float, access::write> dst [[texture(1)]], depth2d<float, access::read> depth [[texture(2)]], constant B &b [[buffer(0)]], uint2 g [[thread_position_in_grid]]) {\n"
"  if (g.x >= b.size.x || g.y >= b.size.y) return; float2 c = src.read(g).xy; float z0 = depth.read(g); float dm = c.y;\n"
"  for (int k = -2; k <= 2; k++) { int2 q = clamp(int2(g) + b.dir * k, int2(0), int2(b.size) - 1); dm = max(dm, src.read(uint2(q)).y); }\n"
"  int r = int(clamp(dm * b.soft, 0.0, 12.0)); if (r == 0) { dst.write(float4(c.x, dm, 0, 0), g); return; }\n"
"  float sum = 0.0, wsum = 0.0; for (int k = -r; k <= r; k++) { int2 q = clamp(int2(g) + b.dir * k, int2(0), int2(b.size) - 1); float2 s = src.read(uint2(q)).xy; float z = depth.read(uint2(q)); float wgt = 1.0 / (1.0 + 4000.0 * abs(z - z0)); sum += s.x * wgt; wsum += wgt; }\n"
"  dst.write(float4(sum / wsum, dm, 0, 0), g); }\n"
// 点光：半分辨率，每像素向挑中的每盏光一条遮挡射线（到光源为止），RGBA 各一盏 | point lights: half resolution, one occlusion ray per pixel toward each chosen light (up to the light), one per RGBA channel
"struct P { float4x4 invVP; float4 lp[4]; uint2 size; uint2 full; uint n; float bias; float2 pad; };\n"
"kernel void rts_ptrace(depth2d<float, access::read> depth [[texture(0)]], texture2d<float, access::write> pm [[texture(1)]], instance_acceleration_structure as [[buffer(0)]], constant P &k [[buffer(1)]], uint2 g [[thread_position_in_grid]]) {\n"
"  if (g.x >= k.size.x || g.y >= k.size.y) return; uint2 fg = min(g * 2, k.full - 1); float z = depth.read(fg); float4 r = float4(1);\n"
"  if (z < 1.0) {\n"
"    float2 ndc = float2((float(fg.x) + 0.5) / float(k.full.x) * 2.0 - 1.0, 1.0 - (float(fg.y) + 0.5) / float(k.full.y) * 2.0);\n"
"    float4 w = k.invVP * float4(ndc, z, 1.0); float3 p = w.xyz / w.w;\n"
"    intersector<instancing> is; is.accept_any_intersection(true); is.assume_geometry_type(geometry_type::triangle); is.force_opacity(forced_opacity::opaque);\n"
"    for (uint i = 0; i < k.n; i++) { float3 d = k.lp[i].xyz - p; float len = length(d); if (len <= k.bias * 2.0) continue; d /= len;\n"
"      ray ry; ry.origin = p + d * k.bias; ry.direction = d; ry.min_distance = 0.0; ry.max_distance = len - k.bias * 2.0;\n"
"      intersection_result<instancing> h = is.intersect(ry, as, 0xffu); if (h.type != intersection_type::none) r[i] = 0.0; } }\n"
"  pm.write(r, g); }\n"
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
        const char *se = getenv("DM_RT_SOFT"), *sd = getenv("DM_RT_SOFT_MAXD"); g_rts.soft = se ? (float)atof(se) : 0.f; g_rts.soft_maxd = sd ? (float)atof(sd) : 200.f; if (g_rts.soft < 0) g_rts.soft = 0;
        const char *pe = getenv("DM_RT_POINT"), *pn = getenv("DM_RT_POINT_N"); g_rts.point_on = pe ? atoi(pe) != 0 : 1; g_rts.point_n = pn ? atoi(pn) : 4; if (g_rts.point_n > 4) g_rts.point_n = 4; if (g_rts.point_n < 1) g_rts.point_on = 0;
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
    g_rts.ps_unproj = g_rts.ps_decode ? [g_dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"rts_unproj"] error:&e] : nil;
    g_rts.ps_trace = g_rts.ps_unproj ? [g_dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"rts_trace"] error:&e] : nil;
    g_rts.ps_ptrace = g_rts.ps_trace ? [g_dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"rts_ptrace"] error:&e] : nil;
    g_rts.ps_trace_soft = g_rts.ps_ptrace ? [g_dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"rts_trace_soft"] error:&e] : nil;
    g_rts.ps_blur = g_rts.ps_trace_soft ? [g_dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"rts_blur"] error:&e] : nil;
    { MTL4ArgumentTableDescriptor *bd = [MTL4ArgumentTableDescriptor new]; bd.maxBufferBindCount = 1; bd.maxTextureBindCount = 3; g_rts.at_blur = [g_dev newArgumentTableWithDescriptor:bd error:&e]; }
    for (int i = 0; i < 3; i++) for (int j = 0; j < 2; j++) { g_rts.bbuf[i][j] = [g_dev newBufferWithLength:32 options:MTLResourceStorageModeShared]; if (g_rts.bbuf[i][j]) res_add(g_rts.bbuf[i][j]); }
    MTL4ArgumentTableDescriptor *ad = [MTL4ArgumentTableDescriptor new]; ad.maxBufferBindCount = 3; ad.maxTextureBindCount = 2;
    g_rts.at_decode = [g_dev newArgumentTableWithDescriptor:ad error:&e]; g_rts.at_trace = [g_dev newArgumentTableWithDescriptor:ad error:&e]; g_rts.at_overlay = [g_dev newArgumentTableWithDescriptor:ad error:&e];
    for (int i = 0; i < 3; i++) {
        g_rts.instbuf[i] = [g_dev newBufferWithLength:RTS_MAX_INST * sizeof(MTLIndirectAccelerationStructureInstanceDescriptor) options:MTLResourceStorageModeShared];
        g_rts.kbuf[i] = [g_dev newBufferWithLength:256 options:MTLResourceStorageModeShared]; g_rts.dbuf[i] = [g_dev newBufferWithLength:64 options:MTLResourceStorageModeShared];
        g_rts.pkbuf[i] = [g_dev newBufferWithLength:256 options:MTLResourceStorageModeShared]; g_rts.plidx[i] = [g_dev newBufferWithLength:16 options:MTLResourceStorageModeShared];
        if (g_rts.instbuf[i]) res_add(g_rts.instbuf[i]); if (g_rts.kbuf[i]) res_add(g_rts.kbuf[i]); if (g_rts.dbuf[i]) res_add(g_rts.dbuf[i]); if (g_rts.pkbuf[i]) res_add(g_rts.pkbuf[i]); if (g_rts.plidx[i]) res_add(g_rts.plidx[i]);
    }
    g_rts.plidx_none = [g_dev newBufferWithLength:16 options:MTLResourceStorageModeShared]; if (g_rts.plidx_none) { int32_t none[4] = {-1, -1, -1, -1}; memcpy(g_rts.plidx_none.contents, none, 16); res_add(g_rts.plidx_none); }
    // TLAS 按最大实例数一次建够；暂存缓冲按 TLAS 的需求起步，BLAS 需要更大时再换 | The TLAS is created once for the maximum instance count; the scratch buffer starts at the TLAS's need and is replaced when a BLAS needs more
    MTL4InstanceAccelerationStructureDescriptor *td = [MTL4InstanceAccelerationStructureDescriptor new];
    td.instanceDescriptorType = MTLAccelerationStructureInstanceDescriptorTypeIndirect; td.instanceCount = RTS_MAX_INST; td.instanceDescriptorStride = sizeof(MTLIndirectAccelerationStructureInstanceDescriptor);
    td.instanceDescriptorBuffer = (MTL4BufferRange){g_rts.instbuf[0].gpuAddress, g_rts.instbuf[0].length};
    MTLAccelerationStructureSizes sz = [g_dev accelerationStructureSizesWithDescriptor:td];
    g_rts.tlas = [g_dev newAccelerationStructureWithSize:sz.accelerationStructureSize]; if (g_rts.tlas) res_add(g_rts.tlas);
    g_rts.scratch_len = MAX(sz.buildScratchBufferSize, (NSUInteger)(4 << 20)); g_rts.scratch = [g_dev newBufferWithLength:g_rts.scratch_len options:MTLResourceStorageModePrivate]; if (g_rts.scratch) res_add(g_rts.scratch);
    { MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Unorm width:1 height:1 mipmapped:NO]; d.usage = MTLTextureUsageShaderRead; d.storageMode = MTLStorageModeShared;
      g_rts.white = [g_dev newTextureWithDescriptor:d]; uint8_t one = 255; if (g_rts.white) { [g_rts.white replaceRegion:MTLRegionMake2D(0, 0, 1, 1) mipmapLevel:0 withBytes:&one bytesPerRow:1]; res_add(g_rts.white); } }
    if (!g_rts.ps_decode || !g_rts.ps_unproj || !g_rts.ps_trace || !g_rts.ps_ptrace || !g_rts.ps_trace_soft || !g_rts.ps_blur || !g_rts.at_blur || !g_rts.bbuf[2][1] || !g_rts.plidx_none || !g_rts.plidx[2] || !g_rts.pkbuf[2] || !g_rts.at_decode || !g_rts.at_trace || !g_rts.at_overlay || !g_rts.tlas || !g_rts.scratch || !g_rts.white || !g_rts.instbuf[2] || !g_rts.kbuf[2] || !g_rts.dbuf[2]) {
        g_rts.failed = 1; MLOG("【红】光追阴影：管线 / 加速结构建不出 %s（退回原版阴影图）", e.localizedDescription.UTF8String); return 0; }
    MLOG("光追阴影：建好（TLAS %lu KiB、暂存 %lu KiB、最多 %d 实例、蒙皮导出每帧 ≤ %d、点光 %s ≤ %d 盏、软阴影 %s）", (unsigned long)(sz.accelerationStructureSize >> 10), (unsigned long)(g_rts.scratch_len >> 10), RTS_MAX_INST, RTS_MAX_EXPORT, g_rts.point_on ? "开" : "关", g_rts.point_n, g_rts.soft > 0 ? "开" : "关（DM_RT_SOFT）");
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
static void rts_mesh_free(rts_mesh *m) {
    if (m->as) { res_remove(m->as); defer_obj_release(m->as); } if (m->pos) { res_remove(m->pos); defer_obj_release(m->pos); } if (m->ib) { res_remove(m->ib); defer_obj_release(m->ib); }
    for (int i = 0; i < m->nslots; i++) { rts_slot *s = &m->slots[i]; id objs[4] = {s->as, s->pos4, s->pos3, s->lvp}; for (int k = 0; k < 4; k++) if (objs[k]) { res_remove(objs[k]); defer_obj_release(objs[k]); } }
    free(m);
}
static void rts_frame_begin(uint64_t f) {
    if (g_rts.n_frames && g_rts.n_frames % 600 == 0) {
        MLOG("光追阴影：第 %llu 帧 实例 %d（重复 %d、蒙皮 %d、其中绑定姿势 %d、格式不认 %d、非三角列表 %d、镂空留原图 %d）、网格缓存 %d、累计建 %llu 个 BLAS（%.1f 万三角形）、refit %llu 次、追踪 %llu 帧、跳过阴影图绘制 %llu 次、_rt 变体 %d 个（缺 %d）、_pos 变体 %d 个（缺 %d）、点光 %d 盏（光源记录 %u 条、追踪 %llu 帧）、本帧太阳 %s、预渲染遮挡体 %d、光追 GPU %.2f ms/帧（%llu 帧计时）",
            (unsigned long long)g_rts.n_frames, g_rts.ninst, g_rts.ndup, g_rts.nskin, g_rts.nskin_pose, g_rts.nskip_fmt, g_rts.nskip_prim, g_rts.nalpha, g_rts.nmesh, (unsigned long long)g_rts.n_builds, g_rts.build_tris / 1e4, (unsigned long long)g_rts.n_refit, (unsigned long long)g_rts.n_traced, (unsigned long long)g_rts.n_skipped_draws,
            g_rts.n_rt_loaded, g_rts.n_rt_missing, g_rts.n_pos_loaded, g_rts.n_pos_missing, g_rts.npl, g_rts.lbuf_texels / 4, (unsigned long long)g_rts.n_ptraced, g_rts.have_sun ? "有" : "无", g_rts.nprepass, g_rts_gpu_n ? g_rts_gpu_sum / g_rts_gpu_n : 0, (unsigned long long)g_rts_gpu_n);
        g_rts_gpu_sum = 0; g_rts_gpu_n = 0; }
    g_rts.frame = f; g_rts.n_frames++; g_rts.ninst = g_rts.ndup = g_rts.nskin = g_rts.nskin_pose = g_rts.nskip_fmt = g_rts.nskip_prim = g_rts.nalpha = 0; g_rts.have_cam = g_rts.have_sun = 0; g_rts.main_seen = g_rts.mask_done = g_rts.overlay_done = 0;
    for (int i = 0; i < g_rts.nexp; i++) g_rts.exp_mesh[i]->used = 0;   // 上一帧的蒙皮槽全部放回 | release every skinned slot of the previous frame
    g_rts.nexp = 0; g_rts.cur_pos4 = nil; g_rts.pl_done = 0;
    g_rts.prepass_occ = g_rts.n_frames > 1 && g_rts.shadow_draws == 0; g_rts.shadow_draws = 0; g_rts.nprepass = 0;
    // 网格缓存淘汰：600 帧没用过的释放 | Mesh cache eviction: release those unused for 600 frames
    if (g_rts.n_frames % 120 == 0 && g_rts.nmesh) { uint64_t dead[64]; int nd = 0;
        for (uint32_t i = 0; i < g_rts.meshes.cap && nd < 64; i++) { rts_mesh *m = g_rts.meshes.vals[i]; if (m && m->built && f - m->last_frame > 600) dead[nd++] = m->key; }
        for (int i = 0; i < nd; i++) { rts_mesh *m = nvn_hm_del(&g_rts.meshes, &dead[i]); if (!m) continue; rts_mesh_free(m); g_rts.nmesh--; } }
}
// 三角带 → 三角列表索引（退化三角形丢掉）；ib 为空 = 非索引（下标 0..n−1） | triangle strip → list indices (degenerate triangles dropped); NULL ib = non-indexed (0..n−1)
static id<MTLBuffer> rts_strip_ib(const uint8_t *ib, int isz, int n, uint32_t *ntris) {
    uint32_t *o = malloc((size_t)(n - 2) * 12), t = 0;
    #define IDX(k) (ib ? (isz == 2 ? ((const uint16_t *)ib)[k] : ((const uint32_t *)ib)[k]) : (uint32_t)(k))
    for (int k = 2; k < n; k++) { uint32_t a = IDX(k - 2), b = IDX(k - 1), c = IDX(k); if (a == b || b == c || a == c) continue; o[t * 3] = a; o[t * 3 + 1] = b; o[t * 3 + 2] = c; t++; }
    #undef IDX
    id<MTLBuffer> r = t ? [g_dev newBufferWithBytes:o length:(NSUInteger)t * 12 options:MTLResourceStorageModeShared] : nil; free(o); *ntris = t; return r;
}
static MTL4PrimitiveAccelerationStructureDescriptor *rts_blas_desc(rts_mesh *m, id<MTLBuffer> pos) {
    MTL4PrimitiveAccelerationStructureDescriptor *pd = [MTL4PrimitiveAccelerationStructureDescriptor new]; MTL4AccelerationStructureTriangleGeometryDescriptor *gd = [MTL4AccelerationStructureTriangleGeometryDescriptor new];
    gd.vertexBuffer = (MTL4BufferRange){pos.gpuAddress, pos.length}; gd.vertexFormat = MTLAttributeFormatFloat3; gd.vertexStride = 12; gd.triangleCount = m->tris; gd.opaque = YES;
    if (m->ib) { gd.indexBuffer = (MTL4BufferRange){m->ib.gpuAddress, m->ib.length}; gd.indexType = m->isz == 2 ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32; }
    pd.geometryDescriptors = @[gd]; pd.usage = m->skin ? MTLAccelerationStructureUsageRefit : MTLAccelerationStructureUsageNone;   // 蒙皮网格的绑定姿势 BLAS 是各实例槽 refit 的源 | the bind-pose BLAS of a skinned mesh is the refit source of its instance slots
    return pd;
}
// 蒙皮网格的实例槽：首次分配（缓冲 + 与源同大的 BLAS） | Instance slot of a skinned mesh: allocated on first use (buffers + a BLAS the size of the source)
static rts_slot *rts_slot_take(rts_mesh *m) {
    if (m->used < m->nslots) return &m->slots[m->used++];
    if (m->nslots >= RTS_MAX_SLOTS) return NULL;
    rts_slot *s = &m->slots[m->nslots]; memset(s, 0, sizeof *s);
    s->pos4 = [g_dev newBufferWithLength:(NSUInteger)m->nverts * 16 options:MTLResourceStorageModePrivate]; s->pos3 = [g_dev newBufferWithLength:(NSUInteger)m->nverts * 12 options:MTLResourceStorageModePrivate];
    s->lvp = [g_dev newBufferWithLength:768 options:MTLResourceStorageModeShared]; s->as = [g_dev newAccelerationStructureWithSize:m->as_size];
    if (!s->pos4 || !s->pos3 || !s->lvp || !s->as) { id objs[4] = {s->as, s->pos4, s->pos3, s->lvp}; for (int k = 0; k < 4; k++) if (objs[k]) defer_obj_release(objs[k]); memset(s, 0, sizeof *s); return NULL; }
    res_add(s->pos4); res_add(s->pos3); res_add(s->lvp); res_add(s->as); m->nslots++; m->used = m->nslots;
    return s;
}
// 阴影通道的一次绘制 → 网格（首次：登记待建）+ 实例。返回 1 = 这次绘制不要编码；2 = 蒙皮位置导出（换 _pos 顶点变体、关光栅化照常编码） | One draw of the shadow pass → mesh (first time: queued for build) + instance. Returns 1 = do not encode this draw; 2 = skinned position export (_pos vertex variant, rasterization off, encoded as usual)
static int rts_draw_inner(NvProg *pr, const p_draw *dw, const p_draw_idx *di, int prim) {
    if (!rts_on() || !pr || !pr->sh[0]) return 0;
    uint64_t f = atomic_load(&g_cur_frame); if (f != g_rts.frame) rts_frame_begin(f);
    int shadow = !R.rt.n && R.rt.depth && R.rt.depth->d.width == R.rt.depth->d.height;
    if (shadow) { id<MTLTexture> dt = tex_mtl(R.rt.depth); if (!dt || dt.textureType == MTLTextureTypeCube || dt.textureType == MTLTextureTypeCubeArray) return 0; }   // 点光立方体阴影照旧 | point-light cube shadows unchanged
    const p_bind_buf *c3 = &S.ubo[0][3]; int has_c3 = 0;
    { NvShader *vs = pr->vs_base ? pr->vs_base : pr->sh[0]; for (int i = 0; i < vs->rf.nb; i++) if (vs->rf.b[i].kind == NVN_RB_CBUF && vs->rf.b[i].reg == 3) has_c3 = 1; }
    int prepass = !shadow && !R.rt.n && R.rt.depth && !g_rts.main_seen;
    if (!shadow) {   // 深度预渲染（无颜色、非方形深度）：记相机；无太阳关卡还把它的绘制当遮挡体（照常编码） | depth prepass (no color, non-square depth): note the camera; in a level without a sun its draws also serve as occluders (encoded as usual)
        if (prepass && has_c3 && c3->addr && c3->size >= 400) { const uint8_t *b = rts_mem(c3->addr, 400); if (b) { memcpy(g_rts.cam, b + 336, 64); g_rts.have_cam = 1; } }
        if (!prepass || !g_rts.prepass_occ || !g_rts.point_on || !rts_setup()) return 0;
        if (pr->sh[1] && pr->sh[1]->rf.may_discard) return 0;
    } else {
        g_rts.shadow_draws++;
        if (!rts_setup()) return 0;
        if (pr->sh[1] && pr->sh[1]->rf.may_discard) { g_rts.nalpha++; return 0; }
    }
    #define RTS_SKIP (shadow ? 1 : 0)   // 阴影通道的绘制登记后不编码；预渲染的照常编码 | a shadow-pass draw is not encoded once registered; a prepass draw is encoded as usual   // 第四阶段·镂空遮挡体（树冠等，片元会 discard）：照常画进原级联阴影图，_rt 变体取 min(光追遮罩, 原采样)；不进加速结构 | phase 4 · alpha-tested occluders (foliage etc., the fragment may discard): still drawn into the original cascades, the _rt variants take min(ray-traced mask, original sample); not in the acceleration structure
    const uint8_t *lvp = has_c3 && c3->addr && c3->size >= 400 ? rts_mem(c3->addr, 400) : NULL; if (lvp) lvp += 336;   // 本次绘制的光源视投影（两级级联各一份） | this draw's light view-projection (one per cascade)
    if (!g_rts.have_sun && lvp) { const float *L = (const float *)lvp; float d[3] = {L[2], L[6], L[10]}; float n = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (n > 0) { float s = (d[1] > 0 ? 1.f : -1.f) / n; g_rts.sun[0] = d[0] * s; g_rts.sun[1] = d[1] * s; g_rts.sun[2] = d[2] * s; g_rts.have_sun = 1; } }
    if (shadow) g_rts.n_skipped_draws++;
    if (prim != 4 && prim != 5) { g_rts.nskip_prim++; return RTS_SKIP; }   // 三角列表 / 三角带；别的图元跳过绘制但不进加速结构 | triangle lists / strips; other primitives skip the draw without entering the acceleration structure
    int n = dw ? dw->count : di->count; if (n < 3) return RTS_SKIP;
    if (S.nattr < 1 || S.attr[0].stream >= 16) return RTS_SKIP;
    va_st at = S.attr[0]; vs_st sm = S.stream[at.stream]; NVNbufferRange vb = S.vbuf[at.stream];
    if (!sm.stride || sm.divisor || !vb.address || (at.fmt != 0x2b && at.fmt != 0x2e && at.fmt != 0x29 && at.fmt != 0x22)) { g_rts.nskip_fmt++; return RTS_SKIP; }
    if (di && di->itype != 1 && di->itype != 2) { g_rts.nskip_fmt++; return RTS_SKIP; }
    const p_bind_buf *c0 = &S.ubo[0][0]; const uint8_t *m0 = c0->addr && c0->size >= 64 ? rts_mem(c0->addr, 64) : NULL; if (!m0) return RTS_SKIP;
    // 网格键：布局 + 首 32 字节索引与顶点的指纹（池内存会被别的网格复用） | Mesh key: layout + fingerprint of the first 32 bytes of indices and vertices (pool memory gets reused by other meshes)
    uint64_t key = 1469598103934665603ull; uint64_t lay[7] = {vb.address + at.offset, sm.stride, at.fmt, dw ? 0 : di->ibuf, (uint64_t)n, dw ? (uint64_t)dw->first : (uint64_t)(int64_t)di->base_vertex, (uint64_t)prim};
    key = rts_fnv(key, lay, sizeof lay);
    int isz = di ? (di->itype == 1 ? 2 : 4) : 0; const uint8_t *ib = di ? rts_mem(di->ibuf, (uint64_t)n * isz) : NULL; if (di && !ib) return RTS_SKIP;
    if (ib) key = rts_fnv(key, ib, 32);
    int64_t vfirst = dw ? dw->first : di->base_vertex; const uint8_t *v0 = rts_mem(vb.address + (uint64_t)vfirst * sm.stride + at.offset, 32); if (v0) key = rts_fnv(key, v0, 32);
    int skin = rt_is_skin();
    rts_mesh *m = nvn_hm_get(&g_rts.meshes, &key);
    if (!m) {
        if (g_rts.npend >= RTS_BUILDS_PER_FRAME) return RTS_SKIP;   // 本帧配额用完：下一帧再建 | quota for this frame used up: build next frame
        uint32_t nv = (uint32_t)n; int64_t vbase = vfirst;
        if (ib) { uint32_t mx = 0; for (int k = 0; k < n; k++) { uint32_t v = isz == 2 ? ((const uint16_t *)ib)[k] : ((const uint32_t *)ib)[k]; if (v > mx) mx = v; } nv = mx + 1; }
        if (vbase < 0 || (uint64_t)(vbase + nv) * sm.stride + at.offset + 16 > vb.size + sm.stride) return RTS_SKIP;
        void *mb; uint64_t mo, vgpu; if (!mtl_gpu_addr(vb.address + at.offset, 16, &mb, &mo, &vgpu)) return RTS_SKIP;
        m = calloc(1, sizeof *m); m->key = key; m->nverts = nv; m->isz = (uint32_t)isz; m->skin = skin;
        if (prim == 5) { m->ib = rts_strip_ib(ib, isz, n, &m->tris); m->isz = 4; if (!m->ib) { free(m); return RTS_SKIP; } }
        else { m->tris = (uint32_t)(n / 3); if (ib) m->ib = [g_dev newBufferWithBytes:ib length:(NSUInteger)n * isz options:MTLResourceStorageModeShared]; }   // 索引拷一份：游戏的索引内存会被复用 | copy the indices: the game's index memory gets reused
        m->pos = [g_dev newBufferWithLength:(NSUInteger)nv * 12 options:MTLResourceStorageModePrivate];
        MTLAccelerationStructureSizes sz = m->pos ? [g_dev accelerationStructureSizesWithDescriptor:rts_blas_desc(m, m->pos)] : (MTLAccelerationStructureSizes){0};
        m->as_size = sz.accelerationStructureSize; m->as = m->as_size ? [g_dev newAccelerationStructureWithSize:m->as_size] : nil;
        if (!m->pos || !m->as || (ib && !m->ib)) { if (m->pos) defer_obj_release(m->pos); if (m->as) defer_obj_release(m->as); if (m->ib) defer_obj_release(m->ib); free(m); return RTS_SKIP; }
        NSUInteger need = MAX(sz.buildScratchBufferSize, sz.refitScratchBufferSize);
        if (need > g_rts.scratch_len) { res_remove(g_rts.scratch); defer_obj_release(g_rts.scratch); g_rts.scratch_len = need * 2; g_rts.scratch = [g_dev newBufferWithLength:g_rts.scratch_len options:MTLResourceStorageModePrivate]; res_add(g_rts.scratch); }
        res_add(m->pos); res_add(m->as); if (m->ib) res_add(m->ib);
        nvn_hm_put(&g_rts.meshes, &key, m); g_rts.nmesh++;
        g_rts.pending[g_rts.npend] = m; g_rts.pend_src[g_rts.npend] = (typeof(g_rts.pend_src[0])){vgpu, nv, sm.stride, at.fmt, (uint32_t)vbase}; g_rts.npend++;
    }
    m->last_frame = f;
    if (g_rts.ninst >= RTS_MAX_INST) return RTS_SKIP;
    const float *M = (const float *)m0; uint64_t tag = key ^ rts_fnv(1469598103934665603ull, m0, 48);
    for (int i = 0; i < g_rts.ninst; i++) if (g_rts.inst_tag[i] == tag) { g_rts.ndup++; return RTS_SKIP; }   // 两级级联画同一实例：只进一次 | both cascades draw the same instance: enter once
    if (!shadow) g_rts.nprepass++;
    MTLIndirectAccelerationStructureInstanceDescriptor *d = &g_rts.inst[g_rts.ninst]; memset(d, 0, sizeof *d);
    d->options = MTLAccelerationStructureInstanceOptionOpaque; d->mask = 0xff; d->accelerationStructureID = m->as.gpuResourceID;
    int r = RTS_SKIP;
    if (skin) { g_rts.nskin++;
        // 蒙皮：_pos 变体首次按需加载；有变体、绑定姿势 BLAS 已建、本帧配额未满 → 取一个槽，导出 + refit | skinned: the _pos variant is loaded on demand; variant present, bind-pose BLAS built, quota left → take a slot, export + refit
        if (!pr->pos_tried) { pr->pos_tried = 1; NSString *k = [NSString stringWithFormat:@"%.16s_pos", (pr->vs_base ? pr->vs_base : pr->sh[0])->key];
            if ([NSFileManager.defaultManager fileExistsAtPath:[NSString stringWithFormat:@"%@/%@.json", g_msl_dir, k]]) { pr->vs_pos = shader_get(k, 0); if (pr->vs_pos) g_rts.n_pos_loaded++; else g_rts.n_pos_missing++; } else g_rts.n_pos_missing++;
            if (g_rts.n_pos_loaded + g_rts.n_pos_missing == 1) MLOG("光追阴影：蒙皮位置导出变体 %s（%s）", pr->vs_pos ? "已加载" : "缺", k.UTF8String); }
        rts_slot *s = shadow && pr->vs_pos && m->built && lvp && g_rts.nexp < RTS_MAX_EXPORT ? rts_slot_take(m) : NULL;   // 预渲染里不能关光栅化（要写深度）：按绑定姿势 | the prepass must rasterize (depth): bind pose
        if (s) {
            double a[16], ia[16]; const float *L = (const float *)lvp; for (int i = 0; i < 16; i++) a[i] = L[i]; rx_inv4(a, ia);
            float *u = (float *)((uint8_t *)s->lvp.contents + g_rts.ri * 256); for (int i = 0; i < 16; i++) u[i] = (float)ia[i]; ((uint32_t *)u)[16] = m->nverts;
            g_rts.exp_mesh[g_rts.nexp] = m; g_rts.exp_slot[g_rts.nexp] = s; g_rts.nexp++; g_rts.cur_pos4 = s->pos4;
            d->transformationMatrix.columns[0].x = d->transformationMatrix.columns[1].y = d->transformationMatrix.columns[2].z = 1; d->accelerationStructureID = s->as.gpuResourceID;   // 位置已是世界坐标 | positions are already world space
            r = 2;
        } else g_rts.nskin_pose++;
    }
    if (r != 2) for (int c = 0; c < 4; c++) { d->transformationMatrix.columns[c].x = M[c * 4 + 0]; d->transformationMatrix.columns[c].y = M[c * 4 + 1]; d->transformationMatrix.columns[c].z = M[c * 4 + 2]; }   // 行向量 3x4 的第 c 行 = 列主序的第 c 列 | row c of the row-vector 3x4 = column c in column-major
    g_rts.inst_tag[g_rts.ninst] = tag; g_rts.ninst++;
    return r;
    #undef RTS_SKIP
}
static int rts_draw(NvProg *pr, const p_draw *dw, const p_draw_idx *di, int prim) {
    uint64_t f = atomic_load(&g_cur_frame); int r;
    if (f == g_rts.last_f && dw == g_rts.last_dw && di == g_rts.last_di && S.prog == g_rts.last_prog) r = g_rts.last_r;   // draw 重入（资源版本变了重来）：同一答案 | draw re-entered (resource version changed): same answer
    else { r = rts_draw_inner(pr, dw, di, prim); g_rts.last_f = f; g_rts.last_dw = dw; g_rts.last_di = di; g_rts.last_prog = S.prog; g_rts.last_r = r; }
    if (pr && pr->sh[0]) {   // 顶点着色器：导出绘制用 _pos 变体，其余用原版 | vertex shader: the export draw uses the _pos variant, everything else the original
        if (!pr->vs_base) pr->vs_base = pr->sh[0];
        NvShader *want = r == 2 && pr->vs_pos ? pr->vs_pos : pr->vs_base;
        if (pr->sh[0] != want) { pr->sh[0] = want; g_pso_dirty = 1; } }
    uint8_t nr = r == 2; if (nr != g_pso_noraster) { g_pso_noraster = nr; g_pso_dirty = 1; }
    return r;
}
static uint64_t rts_pos_gpu(void) { return g_rts.cur_pos4 ? g_rts.cur_pos4.gpuAddress : 0; }
static id<MTLBuffer> rts_pos_buf(void) { return g_rts.cur_pos4; }
// 点光：按「离画面中心视线近、亮」挑至多 point_n 盏。记录 = 4 个 float4 texel：[0] 位置、[1] 颜色、[2].w 阴影标志、[3] 打包参数（_rt 变体里按此读）
// Point lights: pick up to point_n by "close to the screen-center view line, bright". Record = 4 float4 texels: [0] position, [1] color, [2].w shadow flag, [3] packed parameters (as read by the _rt variants)
static int rts_pick_lights(const double *ia) {
    g_rts.npl = 0; if (!g_rts.lbuf || g_rts.lbuf_fmt != MTLPixelFormatRGBA32Float) return 0;
    const uint8_t *base = [g_rts.lbuf contents]; if (!base) return 0; const float *L = (const float *)(base + g_rts.lbuf_off); uint32_t nrec = g_rts.lbuf_texels / 4; if (nrec > 4096) nrec = 4096;
    double p0[3], p1[3]; { double c0[4] = {0, 0, 0, 1}, c1[4] = {0, 0, 1, 1}, o0[4], o1[4];   // 画面中心的视线（正交：近远两点） | the screen-center view line (orthographic: near and far point)
        for (int r = 0; r < 4; r++) { o0[r] = o1[r] = 0; for (int c = 0; c < 4; c++) { o0[r] += ia[c * 4 + r] * c0[c]; o1[r] += ia[c * 4 + r] * c1[c]; } }
        for (int i = 0; i < 3; i++) { p0[i] = o0[i] / o0[3]; p1[i] = o1[i] / o1[3]; } }
    double dir[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]}; double dl = sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]); if (dl <= 0) return 0; for (int i = 0; i < 3; i++) dir[i] /= dl;
    double best[4] = {0, 0, 0, 0}; int bi[4] = {-1, -1, -1, -1}; int n = 0;
    for (uint32_t r = 0; r < nrec; r++) { const float *rec = L + r * 16; float lum = fmaxf(rec[4], fmaxf(rec[5], rec[6]));
        if (!(lum > 0) || !isfinite(rec[0]) || !isfinite(rec[1]) || !isfinite(rec[2])) continue;
        double v[3] = {rec[0] - p0[0], rec[1] - p0[1], rec[2] - p0[2]}; double t = v[0] * dir[0] + v[1] * dir[1] + v[2] * dir[2]; double d2 = 0; for (int i = 0; i < 3; i++) { double q = v[i] - t * dir[i]; d2 += q * q; }
        double score = lum / (d2 + 1e4);
        int k = g_rts.point_n; for (int i = 0; i < g_rts.point_n; i++) if (bi[i] < 0 || score > best[i]) { k = i; break; }   // 插入排序（≤ 4） | insertion (≤ 4)
        if (k >= g_rts.point_n) continue;
        for (int i = g_rts.point_n - 1; i > k; i--) { best[i] = best[i - 1]; bi[i] = bi[i - 1]; } best[k] = score; bi[k] = (int)r; }
    for (int i = 0; i < g_rts.point_n && bi[i] >= 0; i++) { const float *rec = L + bi[i] * 16; g_rts.plpos[n][0] = rec[0]; g_rts.plpos[n][1] = rec[1]; g_rts.plpos[n][2] = rec[2]; g_rts.plrec[n] = bi[i] * 4; n++; }
    g_rts.npl = n;
    { static int once; if (n && once++ < 3) { const float *rec = L + bi[0] * 16; MLOG("光追点光：记录 %u 条，挑 %d 盏；首盏 #%d 位置 (%.0f, %.0f, %.0f) 颜色 (%.2f, %.2f, %.2f, %.2f) [2]=(%.2f, %.2f, %.2f, %.2f) [3]=(%.3g, %.3g, %.3g, %.3g) 距视线 %.0f；视线起点 (%.0f, %.0f, %.0f)",
        nrec, n, bi[0], rec[0], rec[1], rec[2], rec[4], rec[5], rec[6], rec[7], rec[8], rec[9], rec[10], rec[11], rec[12], rec[13], rec[14], rec[15], sqrt(1.0 / best[0] * fmaxf(rec[4], fmaxf(rec[5], rec[6])) - 1e4 > 0 ? 1.0 / best[0] * fmaxf(rec[4], fmaxf(rec[5], rec[6])) - 1e4 : 0), p0[0], p0[1], p0[2]); } }
    return n;
}
static id<MTLTexture> rts_pmask_tex(void) { return g_rts.pl_done && g_rts.pmask ? g_rts.pmask : g_rts.white; }
static uint64_t rts_pmask_rid(void) { id<MTLTexture> t = rts_pmask_tex(); return t ? t.gpuResourceID._impl : 0; }
static id<MTLBuffer> rts_plidx_buf(void) { return g_rts.pl_done ? g_rts.plidx[(g_rts.ri + 2) % 3] : g_rts.plidx_none; }   // 本帧追踪用的那一份（ri 在追踪时已轮换） | the set this frame's trace used (ri already rotated at trace time)
static uint64_t rts_plidx_gpu(void) { id<MTLBuffer> b = rts_plidx_buf(); return b ? b.gpuAddress : 0; }
// 主通道设目标处：建待建的 BLAS、蒙皮槽 refit、TLAS，追踪写遮罩 | At the main pass's set-target: build pending BLAS, refit the skinned slots, the TLAS, trace into the mask
static void rts_build_and_trace(id<MTLTexture> depth) {
    if (!g_rts.ninst || !g_rts.have_cam || !depth || (!g_rts.have_sun && !g_rts.point_on)) { g_rts.npend = 0; return; }
    if (!g_rts.mask || g_rts.mask.width != depth.width || g_rts.mask.height != depth.height) {
        MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Unorm width:depth.width height:depth.height mipmapped:NO];
        d.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite; d.storageMode = MTLStorageModePrivate;
        if (g_rts.mask) { res_remove(g_rts.mask); defer_obj_release(g_rts.mask); } g_rts.mask = [g_dev newTextureWithDescriptor:d]; if (!g_rts.mask) { g_rts.failed = 1; return; } res_add(g_rts.mask);
        if (g_rts.soft > 0) { MTLTextureDescriptor *d2 = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRG8Unorm width:depth.width height:depth.height mipmapped:NO]; d2.usage = d.usage; d2.storageMode = d.storageMode;
            if (g_rts.mask_raw) { res_remove(g_rts.mask_raw); defer_obj_release(g_rts.mask_raw); } if (g_rts.mask_tmp) { res_remove(g_rts.mask_tmp); defer_obj_release(g_rts.mask_tmp); }
            g_rts.mask_raw = [g_dev newTextureWithDescriptor:d2]; g_rts.mask_tmp = [g_dev newTextureWithDescriptor:d2]; if (!g_rts.mask_raw || !g_rts.mask_tmp) { g_rts.soft = 0; MLOG("【红】光追阴影：软阴影纹理建不出，退回硬阴影"); } else { res_add(g_rts.mask_raw); res_add(g_rts.mask_tmp); } }
        MLOG("光追阴影：遮罩 %lux%lu%s", (unsigned long)depth.width, (unsigned long)depth.height, g_rts.soft > 0 ? "（软阴影：最近命中 + 两趟模糊）" : ""); }
    rp_end(); if (g_blit) { enc_b_end(g_blit); g_blit = nil; } ser_gate();
    int ri = g_rts.ri; g_rts.ri = (ri + 1) % 3;
    CENC ce = (CENC)pg_compute(); pg_tag_rt();   // 走逐通道计时（DM_NVN_PASSGPU 没开时就是普通计算编码器） | through the per-pass timer (a plain compute encoder when DM_NVN_PASSGPU is off)
    MTL4BufferRange scratch = {g_rts.scratch.gpuAddress, g_rts.scratch.length};
    // 1. 新网格：解码位置 → 建 BLAS（各步之间加编码器内屏障；暂存缓冲共用，所以逐个建） | 1. New meshes: decode positions → build BLAS (intra-encoder barriers between steps; the scratch buffer is shared, so build one at a time)
    if (g_rts.npend) {
        [ce setArgumentTable:g_rts.at_decode]; enc_c_pso(ce, g_rts.ps_decode);
        for (int i = 0; i < g_rts.npend; i++) { rts_mesh *m = g_rts.pending[i]; typeof(g_rts.pend_src[0]) *s = &g_rts.pend_src[i];
            id<MTLBuffer> kb = [g_dev newBufferWithLength:16 options:MTLResourceStorageModeShared]; uint32_t k4[4] = {s->n, s->stride, s->fmt, s->base}; memcpy(kb.contents, k4, 16); res_add(kb); defer_obj_release(kb);
            [g_rts.at_decode setAddress:s->gpu atIndex:0]; [g_rts.at_decode setAddress:m->pos.gpuAddress atIndex:1]; [g_rts.at_decode setAddress:kb.gpuAddress atIndex:2];
            enc_c_dispatch(ce, MTLSizeMake((s->n + 63) / 64, 1, 1), MTLSizeMake(64, 1, 1)); }
        [ce barrierAfterEncoderStages:MTLStageDispatch beforeEncoderStages:MTLStageAccelerationStructure visibilityOptions:MTL4VisibilityOptionDevice];
        for (int i = 0; i < g_rts.npend; i++) { rts_mesh *m = g_rts.pending[i];
            [ce buildAccelerationStructure:m->as descriptor:rts_blas_desc(m, m->pos) scratchBuffer:scratch];
            [ce barrierAfterEncoderStages:MTLStageAccelerationStructure beforeEncoderStages:MTLStageAccelerationStructure visibilityOptions:MTL4VisibilityOptionDevice];
            m->built = 1; g_rts.n_builds++; g_rts.build_tris += m->tris; }
        g_rts.npend = 0;
    }
    // 2. 蒙皮槽：裁剪坐标 → 世界坐标 → 以绑定姿势 BLAS 为源 refit 到槽 BLAS | 2. Skinned slots: clip → world → refit from the bind-pose BLAS into the slot BLAS
    if (g_rts.nexp) {
        [ce setArgumentTable:g_rts.at_decode]; enc_c_pso(ce, g_rts.ps_unproj);
        for (int i = 0; i < g_rts.nexp; i++) { rts_mesh *m = g_rts.exp_mesh[i]; rts_slot *s = g_rts.exp_slot[i];
            [g_rts.at_decode setAddress:s->pos4.gpuAddress atIndex:0]; [g_rts.at_decode setAddress:s->pos3.gpuAddress atIndex:1]; [g_rts.at_decode setAddress:s->lvp.gpuAddress + (uint64_t)ri * 256 atIndex:2];
            enc_c_dispatch(ce, MTLSizeMake((m->nverts + 63) / 64, 1, 1), MTLSizeMake(64, 1, 1)); }
        [ce barrierAfterEncoderStages:MTLStageDispatch beforeEncoderStages:MTLStageAccelerationStructure visibilityOptions:MTL4VisibilityOptionDevice];
        for (int i = 0; i < g_rts.nexp; i++) { rts_mesh *m = g_rts.exp_mesh[i]; rts_slot *s = g_rts.exp_slot[i];
            [ce refitAccelerationStructure:m->as descriptor:rts_blas_desc(m, s->pos3) destination:s->as scratchBuffer:scratch options:MTLAccelerationStructureRefitOptionVertexData];
            [ce barrierAfterEncoderStages:MTLStageAccelerationStructure beforeEncoderStages:MTLStageAccelerationStructure visibilityOptions:MTL4VisibilityOptionDevice];
            g_rts.n_refit++; }
    }
    // 3. TLAS（只放建好的实例） | 3. TLAS (built instances only)
    id<MTLBuffer> ibuf = g_rts.instbuf[ri]; MTLIndirectAccelerationStructureInstanceDescriptor *dst = ibuf.contents; int n = 0;
    for (int i = 0; i < g_rts.ninst; i++) dst[n++] = g_rts.inst[i];
    MTL4InstanceAccelerationStructureDescriptor *td = [MTL4InstanceAccelerationStructureDescriptor new];
    td.instanceDescriptorType = MTLAccelerationStructureInstanceDescriptorTypeIndirect; td.instanceCount = n; td.instanceDescriptorStride = sizeof(MTLIndirectAccelerationStructureInstanceDescriptor);
    td.instanceDescriptorBuffer = (MTL4BufferRange){ibuf.gpuAddress, (uint64_t)n * sizeof(MTLIndirectAccelerationStructureInstanceDescriptor)};
    [ce buildAccelerationStructure:g_rts.tlas descriptor:td scratchBuffer:scratch];
    [ce barrierAfterEncoderStages:MTLStageAccelerationStructure beforeEncoderStages:MTLStageDispatch visibilityOptions:MTL4VisibilityOptionDevice];
    // 4. 追踪：常量 = 逆视投影（列主序 = 行向量矩阵求逆后原样当列）、太阳、尺寸 | 4. Trace: constants = inverse view-projection (column-major = the inverted row-vector matrix used as columns), sun, size
    double a[16], ia[16]; for (int i = 0; i < 16; i++) a[i] = g_rts.cam[i]; rx_inv4(a, ia);
    float *k = g_rts.kbuf[ri].contents; for (int i = 0; i < 16; i++) k[i] = (float)ia[i];
    k[16] = g_rts.sun[0]; k[17] = g_rts.sun[1]; k[18] = g_rts.sun[2]; k[19] = 0; ((uint32_t *)k)[20] = (uint32_t)depth.width; ((uint32_t *)k)[21] = (uint32_t)depth.height;
    static float bias = -1; if (bias < 0) { const char *e = getenv("DM_RT_SHADOW_BIAS"); bias = e ? (float)atof(e) : 0.6f; } k[22] = bias; k[23] = g_rts.soft_maxd;
    int soft = g_rts.soft > 0 && g_rts.mask_raw && g_rts.mask_tmp;
    [ce setArgumentTable:g_rts.at_trace]; enc_c_pso(ce, soft ? g_rts.ps_trace_soft : g_rts.ps_trace);
    [g_rts.at_trace setTexture:depth.gpuResourceID atIndex:0]; [g_rts.at_trace setTexture:(soft ? g_rts.mask_raw : g_rts.mask).gpuResourceID atIndex:1];
    [g_rts.at_trace setResource:g_rts.tlas.gpuResourceID atBufferIndex:0]; [g_rts.at_trace setAddress:g_rts.kbuf[ri].gpuAddress atIndex:1];
    MTLSize tg = MTLSizeMake((depth.width + 7) / 8, (depth.height + 7) / 8, 1), tt = MTLSizeMake(8, 8, 1);
    if (g_rts.have_sun) enc_c_dispatch(ce, tg, tt);   // 无太阳的关卡：不追太阳，遮罩不算（绑定处给全亮） | level without a sun: no sun trace, the mask is not computed (the binding gets fully lit)
    if (soft && g_rts.have_sun) {   // 两趟模糊：横 raw → tmp，纵 tmp → R8 遮罩 | two blur passes: horizontal raw → tmp, vertical tmp → the R8 mask
        for (int pass = 0; pass < 2; pass++) {
            [ce barrierAfterEncoderStages:MTLStageDispatch beforeEncoderStages:MTLStageDispatch visibilityOptions:MTL4VisibilityOptionDevice];
            uint32_t *bb = g_rts.bbuf[ri][pass].contents; bb[0] = (uint32_t)depth.width; bb[1] = (uint32_t)depth.height; ((int32_t *)bb)[2] = pass == 0; ((int32_t *)bb)[3] = pass == 1; ((float *)bb)[4] = g_rts.soft * 12.f; ((float *)bb)[5] = 0;
            [ce setArgumentTable:g_rts.at_blur]; enc_c_pso(ce, g_rts.ps_blur);
            [g_rts.at_blur setTexture:(pass == 0 ? g_rts.mask_raw : g_rts.mask_tmp).gpuResourceID atIndex:0]; [g_rts.at_blur setTexture:(pass == 0 ? g_rts.mask_tmp : g_rts.mask).gpuResourceID atIndex:1]; [g_rts.at_blur setTexture:depth.gpuResourceID atIndex:2];
            [g_rts.at_blur setAddress:g_rts.bbuf[ri][pass].gpuAddress atIndex:0];
            enc_c_dispatch(ce, tg, tt); }
        [ce setArgumentTable:g_rts.at_trace]; }
    // 5. 点光：挑光 → 半分辨率遮罩（同一编码器，TLAS 已建） | 5. Point lights: pick → half-resolution mask (same encoder, TLAS already built)
    if (g_rts.point_on && rts_pick_lights(ia)) {
        NSUInteger hw = (depth.width + 1) / 2, hh = (depth.height + 1) / 2;
        if (!g_rts.pmask || g_rts.pmask.width != hw || g_rts.pmask.height != hh) {
            MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:hw height:hh mipmapped:NO]; d.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite; d.storageMode = MTLStorageModePrivate;
            if (g_rts.pmask) { res_remove(g_rts.pmask); defer_obj_release(g_rts.pmask); } g_rts.pmask = [g_dev newTextureWithDescriptor:d]; if (g_rts.pmask) res_add(g_rts.pmask); }
        if (g_rts.pmask) {
            float *q = g_rts.pkbuf[ri].contents; for (int i = 0; i < 16; i++) q[i] = (float)ia[i];
            for (int i = 0; i < 4; i++) { q[16 + i * 4] = g_rts.plpos[i][0]; q[17 + i * 4] = g_rts.plpos[i][1]; q[18 + i * 4] = g_rts.plpos[i][2]; q[19 + i * 4] = 0; }
            uint32_t *qu = (uint32_t *)q; qu[32] = (uint32_t)hw; qu[33] = (uint32_t)hh; qu[34] = (uint32_t)depth.width; qu[35] = (uint32_t)depth.height; qu[36] = (uint32_t)g_rts.npl; q[37] = bias;
            int32_t *pi = g_rts.plidx[ri].contents; for (int i = 0; i < 4; i++) pi[i] = i < g_rts.npl ? g_rts.plrec[i] : -1;
            enc_c_pso(ce, g_rts.ps_ptrace); [g_rts.at_trace setTexture:g_rts.pmask.gpuResourceID atIndex:1]; [g_rts.at_trace setAddress:g_rts.pkbuf[ri].gpuAddress atIndex:1];
            enc_c_dispatch(ce, MTLSizeMake((hw + 7) / 8, (hh + 7) / 8, 1), MTLSizeMake(8, 8, 1));
            g_rts.pl_done = 1; g_rts.n_ptraced++; }
    }
    pg_compute_end(ce); enc_c_end(ce);
    args_dirty_all(); g_pso_dirty = g_dss_dirty = 1;
    g_rts.mask_done = g_rts.have_sun; g_rts.n_traced++;
    { static int n; if (n++ < 3) MLOG("光追阴影：帧 %llu 实例 %d（蒙皮 %d、其中导出 refit %d）、遮罩 %lux%lu、太阳 (%.2f, %.2f, %.2f)", (unsigned long long)g_rts.frame, g_rts.ninst, g_rts.nskin, g_rts.nexp, (unsigned long)depth.width, (unsigned long)depth.height, g_rts.sun[0], g_rts.sun[1], g_rts.sun[2]); }
}
static void rts_overlay(id<MTLTexture> hdr) {   // 第一阶段合成：HDR × (amb + (1 − amb) · 遮罩) | phase-1 compositing: HDR × (amb + (1 − amb) · mask)
    if (!g_rts.mask_done || !hdr || hdr.width != g_rts.mask.width || hdr.height != g_rts.mask.height || !rts_overlay_setup(hdr.pixelFormat)) return;
    rp_end(); if (g_blit) { enc_b_end(g_blit); g_blit = nil; }
    int ri = (g_rts.ri + 2) % 3; float *a = g_rts.dbuf[ri].contents; a[0] = g_rts.dbg == 2 ? 0.f : dm_rt_shadow_ambient(); a[1] = a[2] = a[3] = 0;   // 本帧追踪用的那一份 | the set this frame's trace used
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
    int want = rts_on() && !g_rts.failed && (g_rts.mask_done || g_rts.pl_done) && !g_rts.dbg;   // 太阳遮罩或点光遮罩任一算了就换变体（没算的那个绑定全亮 / 全 −1） | swap when either the sun or the point mask was computed (the other binding is fully lit / all −1)
    if (want && !pr->rt_tried) { pr->rt_tried = 1;
        NSString *k = [NSString stringWithFormat:@"%.16s_rt", pr->fs_base->key];
        if ([NSFileManager.defaultManager fileExistsAtPath:[NSString stringWithFormat:@"%@/%@.json", g_msl_dir, k]]) { pr->fs_rt = shader_get(k, 1); if (pr->fs_rt) g_rts.n_rt_loaded++; else g_rts.n_rt_missing++; }
        else { g_rts.n_rt_missing++; if (g_rts.n_rt_missing == 1) MLOG("光追阴影：包里没有 _rt 片元变体（%s），遮罩改为整屏叠加", k.UTF8String); } }   // 缺 = 包里没带（出包时 rts_variant.py 没跑） | missing = not in the package (rts_variant.py did not run at packaging)
    NvShader *sel = want && pr->fs_rt ? pr->fs_rt : pr->fs_base;
    if (pr->sh[1] != sel) { pr->sh[1] = sel; g_pso_dirty = 1; }
    if (want && pr->fs_rt && g_rts.point_on) {   // 点光：记下光源记录缓冲（片元阶段的纹理缓冲 fp_t_tcb_6A，贴图号变了才重取） | point lights: note the light record buffer (the fragment stage's texture buffer fp_t_tcb_6A, resolved again only when the texture id changes)
        const nvn_refl *rf = &pr->fs_base->rf; for (int i = 0; i < rf->nb; i++) if (rf->b[i].kind == NVN_RB_TBUF && rf->b[i].reg == RTS_LIGHT_TBUF_REG) {
            int tid = (int)(S.tex[1][rf->b[i].reg] & 0xfffff); if (tid == g_rts.lbuf_tid) break; g_rts.lbuf_tid = tid;
            void *tv = NULL; if (tid) tex_fast_rid(tid, &tv); __unsafe_unretained id<MTLTexture> t = (__bridge id<MTLTexture>)tv;
            if (t && t.textureType == MTLTextureTypeTextureBuffer && t.buffer) { g_rts.lbuf = t.buffer; g_rts.lbuf_off = t.bufferOffset; g_rts.lbuf_texels = (uint32_t)t.width; g_rts.lbuf_fmt = t.pixelFormat;
                static int once; if (once++ < 2) MLOG("光追点光：光源记录缓冲 贴图号 %d、%u texel、格式 %lu%s", tid, g_rts.lbuf_texels, (unsigned long)t.pixelFormat, t.pixelFormat == MTLPixelFormatRGBA32Float ? "" : "（不是 RGBA32F，点光不开）"); }
            else g_rts.lbuf = nil;
            break; } }
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
