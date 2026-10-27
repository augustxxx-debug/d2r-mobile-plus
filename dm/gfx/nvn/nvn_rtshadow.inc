// ---------- 光追阴影：用 Metal 硬件光追算太阳方向的逐像素可见度，替掉游戏自己的两级级联阴影图 ----------
// 怎么接（都在翻译层，不改游戏）：
//   1. 阴影通道（无颜色附件、方形 2D 深度）的每次绘制 = 一个遮挡体实例：网格按（顶点流地址、步长、格式、索引缓冲、数量、基顶点、图元 + 数据指纹）做键缓存 BLAS，
//      首次见到时用计算核把属性 0 的位置解码成 float3（SNORM16x4 · exp2(w·13)，与 vs_models_v 的 MSL 一致）再建；三角带先在 CPU 上展成三角列表；cb0 的物体→世界 3x4 进 TLAS 实例；
//      进了 TLAS 的绘制本身不再编码（阴影图那部分保持清空 = 游戏采样全亮）。
//      就绪闸门（五审 Q5-02、第 7.3 节）：网格三态 待建 → 已提交（本帧编进了构建）→ 可用（下一帧起）；只有「本帧是光追帧」且网格可用的绘制进 TLAS、不编码，
//      其余（新网格、待建、不支持的图元 / 格式、配额满、实例满）照原版画进阴影图（_rt 变体取 min(遮罩, 原采样)，所以照原版画的影子不丢）。光追帧 = 上一帧光追完整（有主通道、相机、有阴影通道就得有太阳，实例都追踪并合成了）；失败就退回原版阴影图，按 2、4 … 512 帧退避再试。
//      网格指纹只读已校验的字节（五审 Q5-01）：索引取 min(已校验字节数, 32)、顶点取 min(缓冲剩余, 32)，索引字节数与顶点偏移都查溢出，新网格的整段顶点范围（按属性宽度）要落在声明缓冲与内存池内。
//   2. 深度预渲染（无颜色、主深度）的绘制里取 cb3 +336 的相机视投影；阴影通道的 cb3 +336 是光源视投影，其 z 列（取朝天那一侧）= 太阳方向。
//   3. 主不透明通道（RG11B10F + Depth32Float_Stencil8，宽 ≥ 500、非方形）的设目标处：建 TLAS、按主深度重建世界坐标、每像素向太阳发一条遮挡射线，写 R8 遮罩（3D 尺寸）。
//   4. 读级联阴影图的片元着色器换成 <键>_rt 变体（dm/gfx/tools/rts_variant.py 出，出包时生成），取 min(遮罩, 原级联采样)：原阴影图里只剩镂空遮挡体（树冠等片元会 discard 的绘制，照常光栅化、不进加速结构）；包里没有变体时退回整屏一趟 HDR × (amb + (1 − amb) · 遮罩)。
//   5. 第二阶段·蒙皮单位（rt_is_skin 的 10 个顶点着色器）：阴影通道里把这次绘制换成 <键>_pos 顶点变体、关光栅化照常编码，顶点阶段把蒙皮后的裁剪坐标写进本实例的槽缓冲；
//      主通道前用光源视投影的逆矩阵还原成世界坐标，以绑定姿势 BLAS 为源 refit 到本实例的槽 BLAS（每网格若干槽，按本帧出现次序分配），实例变换为单位阵。没有 _pos 变体时退回绑定姿势。
//   6. 第三阶段·点光阴影：主通道的片元着色器把光源记录读自纹理缓冲 fp_t_tcb_6A（每盏 4 个 texel：位置 / 颜色 / 标志 / 打包参数），本层在换上 _rt 变体的绘制处记下这块缓冲；
//      追踪太阳之后，在 CPU 上按「离画面中心视线近、亮」挑至多 4 盏（DM_RT_POINT_N），半分辨率下每像素各向一盏光发一条射线写 RGBA8 遮罩；_rt 变体在点光立方体阴影采样处按记录下标查遮罩（rts_pl），其余光照原样。
// 门槛：supportsRaytracing 且 GPU 家族 ≥ Apple9 且机型白名单（用户要求三重判定）；任一不满足 = 整个模块不建任何对象、每次调用立即返回。开关：DM_RT_SHADOW=0/1 钉住，否则 dm_rt_shadow_setting()；DM_RT_POINT=0 只关点光。
//   9. 玩家随身光（「光照半径」点光）：按「相对画面中心视线的偏移连续 60 帧不变而相机在动」认出，颜色乘 DM_RT_PLAYER_LIGHT（缺省 0.8，1 = 不动）；写回游戏的光源记录，游戏不重写时不重复乘。
//   8. 无太阳的关卡（室内 / 地下：整帧没有阴影通道）：上一帧没见到阴影绘制时，遮挡体改从深度预渲染（无颜色、主深度）的绘制登记（照常编码，蒙皮按绑定姿势），只追点光、太阳遮罩保持全亮、不压暗画面。
//   7. 第四阶段·软阴影（可选，DM_RT_SOFT=<强度>，缺省 0 = 硬阴影）：太阳射线改取最近命中、把命中距离一起写进 RG8，再做两趟分离的深度感知模糊，半径 ∝ 周围最远的遮挡距离（近硬远软）；结果写回 R8 遮罩，_rt 变体不用改。
// 计时：光追的计算编码器走 pg_compute（DM_NVN_PASSGPU 开着时逐通道计时，并累计到每 600 帧的统计行）。只编 Metal 4 路径；DM_NVN_M3 下模块为空。
// ---------- Ray-traced shadows: per-pixel sun visibility via Metal hardware ray tracing, replacing the game's two cascaded shadow maps ----------
// How it hooks in (translation layer only, the game is untouched):
//   1. Every draw of a shadow pass (no color attachment, square 2D depth) = one occluder instance: meshes are cached as BLAS keyed by (vertex stream address, stride, format, index buffer, count, base vertex, primitive + data fingerprint);
//      on first sight a compute kernel decodes attribute 0 into float3 (SNORM16x4 · exp2(w·13), matching the vs_models_v MSL) before the build; triangle strips are expanded into lists on the CPU; cb0's object→world 3x4 goes into the TLAS instance;
//      draws that entered the TLAS are no longer encoded (that part of the shadow map stays cleared = the game samples fully lit).
//      Readiness gate (fifth review Q5-02, section 7.3): meshes go pending → submitted (build encoded this frame) → usable (from the next frame); only draws in a ray-traced frame whose mesh is usable enter the TLAS and are not encoded,
//      all others (new, pending, unsupported primitive / format, quota full, instances full) draw the original shadow map (the _rt variants take min(mask, original sample), so those shadows are not lost). Ray-traced frame = the previous frame's ray tracing was complete (main pass, camera, a sun whenever there was a shadow pass, all instances traced and composited); on failure fall back to the original shadow map, retry after 2, 4 … 512 frames.
//      The mesh fingerprint reads validated bytes only (fifth review Q5-01): min(validated index bytes, 32) and min(remaining vertex buffer, 32), the index byte count and the vertex offset are overflow-checked, and a new mesh's whole vertex range (per attribute width) must lie inside the declared buffer and a memory pool.
//   2. The camera view-projection is read from cb3 +336 of the depth-prepass draws (no color, main depth); cb3 +336 of the shadow pass is the light view-projection, whose z column (the side pointing up) = sun direction.
//   3. At the set-target of the main opaque pass (RG11B10F + Depth32Float_Stencil8, width ≥ 500, non-square): build the TLAS, reconstruct world positions from the main depth, shoot one occlusion ray per pixel toward the sun, write an R8 mask (3D size).
//   4. Fragment shaders that sample the cascades are swapped for <key>_rt variants (dm/gfx/tools/rts_variant.py, generated at packaging time) taking min(mask, original cascade sample): the original shadow maps keep only the alpha-tested occluders (foliage etc. whose fragment may discard; rasterized as usual, not in the acceleration structure); without variants in the package a fullscreen HDR × (amb + (1 − amb) · mask) pass is the fallback.
//   5. Phase 2 · skinned units (the 10 vertex shaders of rt_is_skin): in the shadow pass the draw is encoded as usual but with the <key>_pos vertex variant and rasterization off, so the vertex stage writes the skinned clip positions into this instance's slot buffer;
//      before the main pass they are unprojected with the inverse light view-projection into world space and refit from the bind-pose BLAS into this instance's slot BLAS (a few slots per mesh, assigned in order of appearance within the frame); the instance transform is identity. Without a _pos variant the bind pose is used.
//   6. Phase 3 · point-light shadows: the main-pass fragment shaders read light records from the texture buffer fp_t_tcb_6A (4 texels per light: position / color / flags / packed parameters); this layer notes that buffer at the draws using the _rt variant;
//      after the sun trace the CPU picks up to 4 lights (DM_RT_POINT_N) by "close to the screen-center view line, bright", and at half resolution one ray per pixel per light writes an RGBA8 mask; the _rt variants look the mask up by record index at the point-light cube-shadow sample (rts_pl), every other light stays as is.
// Gate: supportsRaytracing and GPU family ≥ Apple9 and a model allowlist (triple check requested by the user); if any fails the module creates nothing and every call returns immediately. Switch: DM_RT_SHADOW=0/1 pins it, otherwise dm_rt_shadow_setting(); DM_RT_POINT=0 turns off point lights only.
//   9. The player's own light (the "light radius" point light): recognized by "offset from the screen-center view line constant for 60 frames while the camera moves", its color is multiplied by DM_RT_PLAYER_LIGHT (default 0.8, 1 = untouched); written back into the game's light record, not multiplied again while the game does not rewrite it.
//   8. Levels without a sun (indoor / underground: no shadow pass in the whole frame): when the previous frame saw no shadow draw, occluders are registered from the depth-prepass draws (no color, main depth; encoded as usual, skinned units in bind pose), only point lights are traced, the sun mask stays fully lit and nothing is darkened.
//   7. Phase 4 · soft shadows (optional, DM_RT_SOFT=<strength>, default 0 = hard): the sun ray takes the nearest hit and writes the hit distance into an RG8 texture, then two separable depth-aware blur passes with a radius ∝ the widest blocker distance around (near hard, far soft); the result goes back into the R8 mask, the _rt variants are unchanged.
// Timing: the ray-tracing compute encoder goes through pg_compute (per-pass timing when DM_NVN_PASSGPU is on, also accumulated into the 600-frame statistics line). Metal 4 path only; under DM_NVN_M3 the module is empty.
__attribute__((weak)) int dm_rt_shadow_setting(void) { return 0; }   // 宿主设置页提供强定义；缺省关 | The host settings page provides the strong definition; off by default
__attribute__((weak)) float dm_rt_shadow_ambient(void) { return 0.45f; }   // 第一阶段合成的环境项（阴影里保留的亮度比例） | Ambient term of the phase-1 compositing (brightness kept inside shadow)
static int rt_is_skin(void);
#include <sys/sysctl.h>
#include "nvn_rtshadow_util.h"
#include "nvn_rtshadow_msl.h"
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
enum { RTS_PENDING, RTS_SUBMITTED, RTS_READY };   // 待建（在缓存里，命中就排队）/ 已提交（构建编进了本帧命令缓冲）/ 可用（下一帧起可进 TLAS、可作 refit 源） | pending (cached, queued on a hit) / submitted (build encoded into this frame's command buffer) / usable (may enter the TLAS and serve as a refit source from the next frame)
typedef struct { uint64_t key; id<MTLAccelerationStructure> as; id<MTLBuffer> pos, ib; uint64_t last_frame; uint32_t tris, nverts, isz; int state, queued, skin; NSUInteger as_size; rts_slot slots[RTS_MAX_SLOTS]; int nslots, used; } rts_mesh;
#define RTS_MESH_READY(m) ((m)->state == RTS_READY)
static struct {
    int on, gate, dbg, failed; uint64_t frame;
    nvn_hmap meshes; int nmesh;
    MTLIndirectAccelerationStructureInstanceDescriptor inst[RTS_MAX_INST]; uint64_t inst_tag[RTS_MAX_INST]; rts_mesh *inst_mesh[RTS_MAX_INST]; int ninst, ndup, nskip_fmt, nskip_prim, nskin, nskin_pose, nalpha;
    rts_mesh *pending[RTS_BUILDS_PER_FRAME]; struct { uint64_t gpu; uint32_t n, stride, fmt, base; } pend_src[RTS_BUILDS_PER_FRAME]; int npend;   // 源地址只在当帧有效：帧开始时作废 | source addresses are valid for the current frame only: dropped at frame start
    rts_mesh *submitted[RTS_BUILDS_PER_FRAME]; int nsub;   // 本帧编进构建的网格：下一帧开始时转可用 | meshes whose build was encoded this frame: usable from the next frame start
    int rt_frame, backoff; uint64_t retry_at, n_rt_frames, n_fail_frames, n_orig_draws;   // 就绪闸门：本帧走光追？失败退避；统计 | readiness gate: is this a ray-traced frame? backoff after a failure; counters
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
    float player_scale; rts_player_track ptrack; float ptrack_orig[3], ptrack_set[3]; int ptrack_logged; uint64_t n_player_scaled;   // 玩家随身光调弱 | player-light weakening
    id<MTLComputePipelineState> ps_ptrace; id<MTLTexture> pmask; id<MTLBuffer> plidx[3], plidx_none, pkbuf[3]; int npl, pl_done; float plpos[4][3]; int plrec[4]; uint64_t n_ptraced;
    uint64_t n_frames, n_builds, n_traced, n_skipped_draws, n_refit; double build_tris;
    int last_r;   // 上一次绘制的答案：draw 重入（g_draw_reentry）时原样复用，不重复登记实例 / 槽 | the previous draw's answer: reused as is on draw re-entry (g_draw_reentry), so instances / slots are not registered twice
} g_rts = { .on = -1, .meshes = {.klen = 8} };
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
        const char *pl = getenv("DM_RT_PLAYER_LIGHT"); g_rts.player_scale = pl ? (float)atof(pl) : 0.8f; if (!(g_rts.player_scale >= 0 && g_rts.player_scale <= 1)) g_rts.player_scale = 1; g_rts.ptrack.rec = -1;   // 玩家随身光颜色倍率（1 = 不动） | player-light color factor (1 = untouched)
        const char *pe = getenv("DM_RT_POINT"), *pn = getenv("DM_RT_POINT_N"); g_rts.point_on = pe ? atoi(pe) != 0 : 1; g_rts.point_n = pn ? atoi(pn) : 4; if (g_rts.point_n > 4) g_rts.point_n = 4; if (g_rts.point_n < 1) g_rts.point_on = 0;
        // 前缀与宿主 dm_rt_shadow_setting 的「光追阴影：生效」同一个：钉住时不走宿主那条，分享诊断日志的摘要照样取得到 | same prefix as "光追阴影：生效" in the host dm_rt_shadow_setting: when pinned that path is skipped, and the share-diagnostics summary still finds the line
        if (e) MLOG("光追阴影：生效 %s（测试开关 DM_RT_SHADOW=%s 钉住%s）", atoi(e) && g_rts.gate ? "开" : "关", e, g_rts.gate ? "" : "，机器不可用，仍不生效");
    }
    if (!g_rts.gate || g_rts.failed) return 0;
    return g_rts.on == -2 ? dm_rt_shadow_setting() != 0 : g_rts.on;
}
static const uint8_t *rts_mem(uint64_t addr, uint64_t len) { void *m; uint64_t o; if (!addr || !len || !mtl_addr(addr, len, &m, &o)) return NULL; const uint8_t *b = [(__bridge id<MTLBuffer>)m contents]; return b ? b + o : NULL; }
static uint64_t rts_fnv(uint64_t h, const void *p, size_t n) { const uint8_t *b = p; for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 1099511628211ull; return h; }
static uint64_t rts_attr_bytes(uint32_t fmt) { return fmt == 0x2e ? 16 : fmt == 0x22 ? 12 : 8; }   // 解码核读的属性 0 宽度（FLOAT4 / FLOAT3 / SNORM16x4、HALF4） | Width of attribute 0 as read by the decode kernel
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
// 上一帧的光追完不完整：NULL = 完整（或本帧无需光追），否则是原因。无太阳的关卡（整帧没有阴影绘制）不算缺太阳 | Was the previous frame's ray tracing complete: NULL = complete (or nothing to trace), else the reason. A level without a sun (no shadow draw in the frame) does not count as a missing sun
static const char *rts_frame_fail(void) {
    if (!g_rts.main_seen) return "没有主通道";
    if (!g_rts.have_cam) return "没取到相机";
    if (!g_rts.have_sun && g_rts.shadow_draws) return "没取到太阳方向";
    if (g_rts.rt_frame && g_rts.ninst && g_rts.have_sun && !g_rts.mask_done) return "遮罩没算出来";
    if (g_rts.rt_frame && g_rts.ninst && g_rts.mask_done && !g_rts.overlay_done && (g_rts.dbg || !g_rts.n_rt_loaded)) return "遮罩没合成进画面";
    return NULL;
}
static void rts_frame_begin(uint64_t f) {
    if (g_rts.n_frames && g_rts.n_frames % 600 == 0) {
        MLOG("光追阴影：第 %llu 帧 实例 %d（重复 %d、蒙皮 %d、其中绑定姿势 %d、格式不认 %d、非三角列表 %d、镂空留原图 %d）、网格缓存 %d、累计建 %llu 个 BLAS（%.1f 万三角形）、refit %llu 次、追踪 %llu 帧、跳过阴影图绘制 %llu 次、_rt 变体 %d 个（缺 %d）、_pos 变体 %d 个（缺 %d）、点光 %d 盏（光源记录 %u 条、追踪 %llu 帧）、本帧太阳 %s、预渲染遮挡体 %d、玩家光 %s（调弱 %llu 次）、光追 GPU %.2f ms/帧（%llu 帧计时）；光追帧 %llu、失败退回 %llu 帧、照原版画 %llu 次",
            (unsigned long long)g_rts.n_frames, g_rts.ninst, g_rts.ndup, g_rts.nskin, g_rts.nskin_pose, g_rts.nskip_fmt, g_rts.nskip_prim, g_rts.nalpha, g_rts.nmesh, (unsigned long long)g_rts.n_builds, g_rts.build_tris / 1e4, (unsigned long long)g_rts.n_refit, (unsigned long long)g_rts.n_traced, (unsigned long long)g_rts.n_skipped_draws,
            g_rts.n_rt_loaded, g_rts.n_rt_missing, g_rts.n_pos_loaded, g_rts.n_pos_missing, g_rts.npl, g_rts.lbuf_texels / 4, (unsigned long long)g_rts.n_ptraced, g_rts.have_sun ? "有" : "无", g_rts.nprepass, g_rts.ptrack.sure ? "已认出" : "未认出", (unsigned long long)g_rts.n_player_scaled, g_rts_gpu_n ? g_rts_gpu_sum / g_rts_gpu_n : 0, (unsigned long long)g_rts_gpu_n, (unsigned long long)g_rts.n_rt_frames, (unsigned long long)g_rts.n_fail_frames, (unsigned long long)g_rts.n_orig_draws);
        g_rts_gpu_sum = 0; g_rts_gpu_n = 0; }
    // 1. 就绪闸门：上一帧（必须紧挨着）完整才走光追；光追帧失败 → 原版阴影图，退避 2、4 … 512 帧再试 | 1. Readiness gate: ray trace only if the previous (adjacent) frame was complete; a failed ray-traced frame → original shadow map, retry after 2, 4 … 512 frames
    const char *why = g_rts.n_frames && g_rts.frame + 1 == f ? rts_frame_fail() : "上一帧没经过光追模块";
    if (g_rts.rt_frame && g_rts.frame + 1 == f) {
        if (why) { g_rts.backoff = g_rts.backoff ? MIN(g_rts.backoff * 2, 512) : 2; g_rts.retry_at = f + (uint64_t)g_rts.backoff; g_rts.n_fail_frames++;
            if (g_rts.n_fail_frames <= 8 || g_rts.n_fail_frames % 100 == 0) MLOG("光追阴影：回退原版阴影图（帧 %llu %s，%d 帧后再试，第 %llu 次）", (unsigned long long)g_rts.frame, why, g_rts.backoff, (unsigned long long)g_rts.n_fail_frames); }
        else if (g_rts.backoff) { MLOG("光追阴影：恢复（帧 %llu 光追完整，此前退避 %d 帧）", (unsigned long long)g_rts.frame, g_rts.backoff); g_rts.backoff = 0; }
    }
    g_rts.rt_frame = !why && f >= g_rts.retry_at; g_rts.n_rt_frames += g_rts.rt_frame;
    // 2. 上一帧编进构建的网格转可用（同一队列里排在后面的 TLAS 构建一定在它之后执行） | 2. Meshes whose build was encoded last frame become usable (TLAS builds later on the same queue always run after it)
    for (int i = 0; i < g_rts.nsub; i++) g_rts.submitted[i]->state = RTS_READY;
    g_rts.nsub = 0;
    // 3. 没建的排队作废（源地址只在当帧有效）：网格留在缓存里待建，下次命中重新排队 | 3. Unbuilt queue entries are dropped (source addresses are per frame): meshes stay cached as pending and are queued again on the next hit
    for (int i = 0; i < g_rts.npend; i++) g_rts.pending[i]->queued = 0;
    g_rts.npend = 0;
    g_rts.frame = f; g_rts.n_frames++; g_rts.ninst = g_rts.ndup = g_rts.nskin = g_rts.nskin_pose = g_rts.nskip_fmt = g_rts.nskip_prim = g_rts.nalpha = 0; g_rts.have_cam = g_rts.have_sun = 0; g_rts.main_seen = g_rts.mask_done = g_rts.overlay_done = 0;
    for (int i = 0; i < g_rts.nexp; i++) g_rts.exp_mesh[i]->used = 0;   // 上一帧的蒙皮槽全部放回 | release every skinned slot of the previous frame
    g_rts.nexp = 0; g_rts.cur_pos4 = nil; g_rts.pl_done = 0;
    g_rts.prepass_occ = g_rts.n_frames > 1 && g_rts.shadow_draws == 0; g_rts.shadow_draws = 0; g_rts.nprepass = 0;
    // 网格缓存淘汰：600 帧没用过的释放（待建的也算：从没建成的网格不能一直占着） | Mesh cache eviction: release those unused for 600 frames (pending ones too: a mesh that never got built must not stay forever)
    if (g_rts.n_frames % 120 == 0 && g_rts.nmesh) { uint64_t dead[64]; int nd = 0;
        for (uint32_t i = 0; i < g_rts.meshes.cap && nd < 64; i++) { rts_mesh *m = g_rts.meshes.vals[i]; if (m && !m->queued && f - m->last_frame > 600) dead[nd++] = m->key; }
        for (int i = 0; i < nd; i++) { rts_mesh *m = nvn_hm_del(&g_rts.meshes, &dead[i]); if (!m) continue; rts_mesh_free(m); g_rts.nmesh--; } }
}
// 三角带 → 三角列表索引缓冲（展开在 nvn_rtshadow_util.h） | triangle strip → list index buffer (expansion in nvn_rtshadow_util.h)
static id<MTLBuffer> rts_strip_ib(const uint8_t *ib, int isz, int n, uint32_t *ntris) {
    uint32_t *o = malloc((size_t)(n - 2) * 12); uint32_t t = rts_strip_expand(ib, isz, n, o);
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
    #define RTS_SKIP 0   // 任何没进 TLAS 的绘制都照原版编码（阴影通道的画进原阴影图，_rt 变体取 min；预渲染的本就要画）（五审 Q5-02） | every draw that did not enter the TLAS is encoded as original (a shadow-pass draw goes into the original shadow map, the _rt variants take min; a prepass draw has to be drawn anyway) (fifth review Q5-02)   // 第四阶段·镂空遮挡体（树冠等，片元会 discard）：照常画进原级联阴影图，_rt 变体取 min(光追遮罩, 原采样)；不进加速结构 | phase 4 · alpha-tested occluders (foliage etc., the fragment may discard): still drawn into the original cascades, the _rt variants take min(ray-traced mask, original sample); not in the acceleration structure
    const uint8_t *lvp = has_c3 && c3->addr && c3->size >= 400 ? rts_mem(c3->addr, 400) : NULL; if (lvp) lvp += 336;   // 本次绘制的光源视投影（两级级联各一份） | this draw's light view-projection (one per cascade)
    if (!g_rts.have_sun && lvp) { const float *L = (const float *)lvp; float d[3] = {L[2], L[6], L[10]}; float n = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (n > 0) { float s = (d[1] > 0 ? 1.f : -1.f) / n; g_rts.sun[0] = d[0] * s; g_rts.sun[1] = d[1] * s; g_rts.sun[2] = d[2] * s; g_rts.have_sun = 1; } }
    if (shadow) g_rts.n_orig_draws++;   // 下面任何一处返回 0 都照原版画；进了 TLAS 时再减回去 | every return 0 below draws the original; taken back when the draw enters the TLAS
    if (prim != 4 && prim != 5) { g_rts.nskip_prim++; return RTS_SKIP; }   // 三角列表 / 三角带；别的图元跳过绘制但不进加速结构 | triangle lists / strips; other primitives skip the draw without entering the acceleration structure
    int n = dw ? dw->count : di->count; if (n < 3) return RTS_SKIP;
    if (S.nattr < 1 || S.attr[0].stream >= 16) return RTS_SKIP;
    va_st at = S.attr[0]; vs_st sm = S.stream[at.stream]; NVNbufferRange vb = S.vbuf[at.stream];
    if (!sm.stride || sm.divisor || !vb.address || (at.fmt != 0x2b && at.fmt != 0x2e && at.fmt != 0x29 && at.fmt != 0x22)) { g_rts.nskip_fmt++; return RTS_SKIP; }
    if (di && di->itype != 1 && di->itype != 2) { g_rts.nskip_fmt++; return RTS_SKIP; }
    const p_bind_buf *c0 = &S.ubo[0][0]; const uint8_t *m0 = c0->addr && c0->size >= 64 ? rts_mem(c0->addr, 64) : NULL; if (!m0) return RTS_SKIP;
    // 网格键：布局 + 索引与顶点各自已校验跨度内前至多 32 字节的指纹（池内存会被别的网格复用）。指纹只读校验过的字节：最小三角形的 16 位索引只有 6 字节（五审 Q5-01）
    // Mesh key: layout + fingerprint of at most the first 32 bytes of the validated index and vertex spans (pool memory gets reused by other meshes). The fingerprint reads validated bytes only: a minimal triangle's 16-bit indices are just 6 bytes (fifth review Q5-01)
    uint64_t key = 1469598103934665603ull; uint64_t lay[7] = {vb.address + at.offset, sm.stride, at.fmt, dw ? 0 : di->ibuf, (uint64_t)n, dw ? (uint64_t)dw->first : (uint64_t)(int64_t)di->base_vertex, (uint64_t)prim};
    key = rts_fnv(key, lay, sizeof lay);
    int isz = di ? (di->itype == 1 ? 2 : 4) : 0; uint64_t ibytes = 0; const uint8_t *ib = NULL;
    if (di) { if (__builtin_mul_overflow((uint64_t)n, (uint64_t)isz, &ibytes) || !(ib = rts_mem(di->ibuf, ibytes))) return RTS_SKIP; key = rts_fnv(key, ib, MIN(ibytes, 32)); }
    int64_t vfirst = dw ? dw->first : di->base_vertex; uint64_t voff;   // 首顶点相对顶点缓冲的偏移；负基顶点、溢出、越出缓冲都不取顶点指纹 | Offset of the first vertex in the vertex buffer; no vertex fingerprint for a negative base, an overflow or past the buffer
    if (vfirst >= 0 && !__builtin_mul_overflow((uint64_t)vfirst, (uint64_t)sm.stride, &voff) && !__builtin_add_overflow(voff, (uint64_t)at.offset, &voff) && voff < vb.size && voff <= UINT64_MAX - vb.address) {
        uint64_t vlen = MIN(vb.size - voff, 32); const uint8_t *v0 = rts_mem(vb.address + voff, vlen); if (v0) key = rts_fnv(key, v0, vlen); }
    int skin = rt_is_skin();
    rts_mesh *m = nvn_hm_get(&g_rts.meshes, &key);
    if (!m || (m->state == RTS_PENDING && !m->queued)) {   // 新网格，或待建而本帧还没排队（缺相机 / 深度、构建失败、配额满、关了再开之后都会落到这里）：校验顶点范围后排队 | new mesh, or pending and not queued this frame (after a missing camera / depth, a failed build, a full quota, off then on): validate the vertex range, then queue
        if (g_rts.npend >= RTS_BUILDS_PER_FRAME) return RTS_SKIP;   // 本帧配额用完：下一帧再建 | quota for this frame used up: build next frame
        uint64_t nv = m ? m->nverts : (uint64_t)n; int64_t vbase = vfirst;
        if (ib && !m) { uint32_t mx = 0; for (int k = 0; k < n; k++) { uint32_t v = isz == 2 ? ((const uint16_t *)ib)[k] : ((const uint32_t *)ib)[k]; if (v > mx) mx = v; } nv = (uint64_t)mx + 1; }
        // 解码核读第 vbase … vbase+nv−1 个顶点的属性 0：这段要在顶点缓冲声明的范围内，整段（不只开头 16 字节）也要落在内存池里；乘加都查溢出 | The decode kernel reads attribute 0 of vertices vbase … vbase+nv−1: that range must lie within the declared vertex buffer and entirely (not just the first 16 bytes) inside a memory pool; every multiply / add is overflow-checked
        uint64_t vlast, vend;
        if (vbase < 0 || nv > UINT32_MAX || __builtin_mul_overflow((uint64_t)vbase + nv - 1, (uint64_t)sm.stride, &vlast) || __builtin_add_overflow(vlast, (uint64_t)at.offset + rts_attr_bytes(at.fmt), &vend) || vend > vb.size) return RTS_SKIP;
        void *mb; uint64_t mo, vgpu; if (!mtl_gpu_addr(vb.address + at.offset, vend - at.offset, &mb, &mo, &vgpu)) return RTS_SKIP;
        if (!m) {
            m = calloc(1, sizeof *m); m->key = key; m->nverts = (uint32_t)nv; m->isz = (uint32_t)isz; m->skin = skin;
            if (prim == 5) { m->ib = rts_strip_ib(ib, isz, n, &m->tris); m->isz = 4; if (!m->ib) { free(m); return RTS_SKIP; } }
            else { m->tris = (uint32_t)(n / 3); if (ib) m->ib = [g_dev newBufferWithBytes:ib length:(NSUInteger)ibytes options:MTLResourceStorageModeShared]; }   // 索引拷一份：游戏的索引内存会被复用 | copy the indices: the game's index memory gets reused
            m->pos = [g_dev newBufferWithLength:(NSUInteger)nv * 12 options:MTLResourceStorageModePrivate];
            MTLAccelerationStructureSizes sz = m->pos ? [g_dev accelerationStructureSizesWithDescriptor:rts_blas_desc(m, m->pos)] : (MTLAccelerationStructureSizes){0};
            m->as_size = sz.accelerationStructureSize; m->as = m->as_size ? [g_dev newAccelerationStructureWithSize:m->as_size] : nil;
            if (!m->pos || !m->as || (ib && !m->ib)) { if (m->pos) defer_obj_release(m->pos); if (m->as) defer_obj_release(m->as); if (m->ib) defer_obj_release(m->ib); free(m); return RTS_SKIP; }
            NSUInteger need = MAX(sz.buildScratchBufferSize, sz.refitScratchBufferSize);
            if (need > g_rts.scratch_len) { res_remove(g_rts.scratch); defer_obj_release(g_rts.scratch); g_rts.scratch_len = need * 2; g_rts.scratch = [g_dev newBufferWithLength:g_rts.scratch_len options:MTLResourceStorageModePrivate];
                if (!g_rts.scratch) { g_rts.failed = 1; MLOG("【红】光追阴影：暂存缓冲 %lu KiB 建不出（退回原版阴影图）", (unsigned long)(g_rts.scratch_len >> 10)); } else res_add(g_rts.scratch); }
            res_add(m->pos); res_add(m->as); if (m->ib) res_add(m->ib);
            nvn_hm_put(&g_rts.meshes, &key, m); g_rts.nmesh++;
        }
        g_rts.pending[g_rts.npend] = m; g_rts.pend_src[g_rts.npend] = (typeof(g_rts.pend_src[0])){vgpu, (uint32_t)nv, sm.stride, at.fmt, (uint32_t)vbase}; g_rts.npend++; m->queued = 1;
    }
    m->last_frame = f;
    if (!g_rts.rt_frame || m->state != RTS_READY || g_rts.ninst >= RTS_MAX_INST) return RTS_SKIP;   // 不是光追帧 / 网格还没可用 / 实例满：照原版画 | not a ray-traced frame / mesh not usable yet / instances full: draw the original
    const float *M = (const float *)m0; uint64_t tag = key ^ rts_fnv(1469598103934665603ull, m0, 48);
    for (int i = 0; i < g_rts.ninst; i++) if (g_rts.inst_tag[i] == tag) { g_rts.ndup++; return shadow ? 1 : 0; }   // 两级级联画同一实例：只进一次（第二次也不编码） | both cascades draw the same instance: enter once (the second is not encoded either)
    if (shadow) { g_rts.n_orig_draws--; g_rts.n_skipped_draws++; } else g_rts.nprepass++;
    MTLIndirectAccelerationStructureInstanceDescriptor *d = &g_rts.inst[g_rts.ninst]; memset(d, 0, sizeof *d);
    d->options = MTLAccelerationStructureInstanceOptionOpaque; d->mask = 0xff; d->accelerationStructureID = m->as.gpuResourceID;
    int r = shadow ? 1 : 0;   // 进了 TLAS：阴影通道的不编码；预渲染的照常画 | entered the TLAS: a shadow-pass draw is not encoded, a prepass draw still is
    if (skin) { g_rts.nskin++;
        // 蒙皮：_pos 变体首次按需加载；有变体、绑定姿势 BLAS 已建、本帧配额未满 → 取一个槽，导出 + refit | skinned: the _pos variant is loaded on demand; variant present, bind-pose BLAS built, quota left → take a slot, export + refit
        if (!pr->pos_tried) { pr->pos_tried = 1; NSString *k = [NSString stringWithFormat:@"%.16s_pos", (pr->vs_base ? pr->vs_base : pr->sh[0])->key];
            if ([NSFileManager.defaultManager fileExistsAtPath:[NSString stringWithFormat:@"%@/%@.json", g_msl_dir, k]]) { pr->vs_pos = shader_get(k, 0); if (pr->vs_pos) g_rts.n_pos_loaded++; else g_rts.n_pos_missing++; } else g_rts.n_pos_missing++;
            if (g_rts.n_pos_loaded + g_rts.n_pos_missing == 1) MLOG("光追阴影：蒙皮位置导出变体 %s（%s）", pr->vs_pos ? "已加载" : "缺", k.UTF8String); }
        rts_slot *s = shadow && pr->vs_pos && lvp && g_rts.nexp < RTS_MAX_EXPORT ? rts_slot_take(m) : NULL;   // 网格已可用（上面核过）才到这里：绑定姿势 BLAS 可作 refit 源 | only reached with a usable mesh (checked above): the bind-pose BLAS can be the refit source   // 预渲染里不能关光栅化（要写深度）：按绑定姿势 | the prepass must rasterize (depth): bind pose
        if (s) {
            double a[16], ia[16]; const float *L = (const float *)lvp; for (int i = 0; i < 16; i++) a[i] = L[i]; rts_inv4(a, ia);
            float *u = (float *)((uint8_t *)s->lvp.contents + g_rts.ri * 256); for (int i = 0; i < 16; i++) u[i] = (float)ia[i]; ((uint32_t *)u)[16] = m->nverts;
            g_rts.exp_mesh[g_rts.nexp] = m; g_rts.exp_slot[g_rts.nexp] = s; g_rts.nexp++; g_rts.cur_pos4 = s->pos4;
            d->transformationMatrix.columns[0].x = d->transformationMatrix.columns[1].y = d->transformationMatrix.columns[2].z = 1; d->accelerationStructureID = s->as.gpuResourceID;   // 位置已是世界坐标 | positions are already world space
            r = 2;
        } else g_rts.nskin_pose++;
    }
    if (r != 2) for (int c = 0; c < 4; c++) { d->transformationMatrix.columns[c].x = M[c * 4 + 0]; d->transformationMatrix.columns[c].y = M[c * 4 + 1]; d->transformationMatrix.columns[c].z = M[c * 4 + 2]; }   // 行向量 3x4 的第 c 行 = 列主序的第 c 列 | row c of the row-vector 3x4 = column c in column-major
    g_rts.inst_tag[g_rts.ninst] = tag; g_rts.inst_mesh[g_rts.ninst] = m; g_rts.ninst++;
    return r;
    #undef RTS_SKIP
}
static int rts_draw(NvProg *pr, const p_draw *dw, const p_draw_idx *di, int prim) {
    int r;
    if (g_draw_reentry) r = g_rts.last_r;   // draw 因资源版本变化重来（同一次绘制、状态不变）：同一答案 | draw retried after a resource-version change (same draw, same state): same answer
    else g_rts.last_r = r = rts_draw_inner(pr, dw, di, prim);
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
    double best[4], p0[3]; int bi[4];
    int n = rts_pick_lights_cpu(L, nrec, ia, g_rts.point_n, bi, best, p0);
    for (int i = 0; i < n; i++) { const float *rec = L + bi[i] * 16; g_rts.plpos[i][0] = rec[0]; g_rts.plpos[i][1] = rec[1]; g_rts.plpos[i][2] = rec[2]; g_rts.plrec[i] = bi[i] * 4; }
    g_rts.npl = n;
    // 玩家随身光（用户 2026-10-27 要求默认调弱，不做滑块）：认出后把颜色 texel 乘以倍率；游戏每帧重写记录就每帧乘，没重写（值还是我们写的）就不再乘，不会越乘越暗
    // Player light (the user asked on 2026-10-27 for a weaker default, no slider): once recognized, the color texel is multiplied by the factor; if the game rewrites the record each frame it is scaled each frame, if not (the value is still ours) it is left alone, so it never fades further
    { int rec = n ? bi[0] : -1; const float *rp = rec >= 0 ? L + rec * 16 : NULL;
      if (rts_player_feed(&g_rts.ptrack, rec, rp, p0) && g_rts.player_scale < 1) { float *c = (float *)(L + rec * 16 + 4);
          int ours = memcmp(c, g_rts.ptrack_set, 12) == 0;
          if (!ours) { memcpy(g_rts.ptrack_orig, c, 12); for (int i = 0; i < 3; i++) c[i] *= g_rts.player_scale; memcpy(g_rts.ptrack_set, c, 12); g_rts.n_player_scaled++; }
          if (!g_rts.ptrack_logged) { g_rts.ptrack_logged = 1; MLOG("光追点光：玩家随身光 = 记录 #%d（相对视线起点偏移 (%.1f, %.1f, %.1f)，%d 帧不变、相机走了 %.0f），颜色 (%.2f, %.2f, %.2f) × %.2f（DM_RT_PLAYER_LIGHT）", rec, g_rts.ptrack.off[0], g_rts.ptrack.off[1], g_rts.ptrack.off[2], g_rts.ptrack.frames, g_rts.ptrack.moved, g_rts.ptrack_orig[0], g_rts.ptrack_orig[1], g_rts.ptrack_orig[2], g_rts.player_scale); } } }
    { static int once; if (n && once++ < 3) { const float *rec = L + bi[0] * 16; MLOG("光追点光：记录 %u 条，挑 %d 盏；首盏 #%d 位置 (%.0f, %.0f, %.0f) 颜色 (%.2f, %.2f, %.2f, %.2f) [2]=(%.2f, %.2f, %.2f, %.2f) [3]=(%.3g, %.3g, %.3g, %.3g) 得分 %.3g；视线起点 (%.0f, %.0f, %.0f)",
        nrec, n, bi[0], rec[0], rec[1], rec[2], rec[4], rec[5], rec[6], rec[7], rec[8], rec[9], rec[10], rec[11], rec[12], rec[13], rec[14], rec[15], best[0], p0[0], p0[1], p0[2]); } }
    return n;
}
static id<MTLTexture> rts_pmask_tex(void) { return g_rts.pl_done && g_rts.pmask ? g_rts.pmask : g_rts.white; }
static uint64_t rts_pmask_rid(void) { id<MTLTexture> t = rts_pmask_tex(); return t ? t.gpuResourceID._impl : 0; }
static id<MTLBuffer> rts_plidx_buf(void) { return g_rts.pl_done ? g_rts.plidx[(g_rts.ri + 2) % 3] : g_rts.plidx_none; }   // 本帧追踪用的那一份（ri 在追踪时已轮换） | the set this frame's trace used (ri already rotated at trace time)
static uint64_t rts_plidx_gpu(void) { id<MTLBuffer> b = rts_plidx_buf(); return b ? b.gpuAddress : 0; }
// 主通道设目标处：建待建的 BLAS、蒙皮槽 refit、TLAS，追踪写遮罩 | At the main pass's set-target: build pending BLAS, refit the skinned slots, the TLAS, trace into the mask
static void rts_unqueue(void) { for (int i = 0; i < g_rts.npend; i++) g_rts.pending[i]->queued = 0; g_rts.npend = 0; }
static void rts_build_and_trace(id<MTLTexture> depth) {
    // 待建的 BLAS 不需要相机 / 太阳 / 深度，每帧都建；光追帧且条件齐全时再做蒙皮 refit、TLAS、追踪（五审 Q5-02） | pending BLAS builds need no camera / sun / depth and happen every frame; skinned refits, the TLAS and the trace only in a ray-traced frame with everything present (fifth review Q5-02)
    int trace = g_rts.rt_frame && g_rts.ninst && g_rts.have_cam && depth && (g_rts.have_sun || g_rts.point_on);
    if (!trace && !g_rts.npend) return;
    if (trace && (!g_rts.mask || g_rts.mask.width != depth.width || g_rts.mask.height != depth.height)) {
        MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Unorm width:depth.width height:depth.height mipmapped:NO];
        d.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite; d.storageMode = MTLStorageModePrivate;
        if (g_rts.mask) { res_remove(g_rts.mask); defer_obj_release(g_rts.mask); } g_rts.mask = [g_dev newTextureWithDescriptor:d];
        if (!g_rts.mask) { g_rts.failed = 1; rts_unqueue(); MLOG("【红】光追阴影：遮罩 %lux%lu 建不出（退回原版阴影图）", (unsigned long)depth.width, (unsigned long)depth.height); return; } res_add(g_rts.mask);
        if (g_rts.soft > 0) { MTLTextureDescriptor *d2 = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRG8Unorm width:depth.width height:depth.height mipmapped:NO]; d2.usage = d.usage; d2.storageMode = d.storageMode;
            if (g_rts.mask_raw) { res_remove(g_rts.mask_raw); defer_obj_release(g_rts.mask_raw); } if (g_rts.mask_tmp) { res_remove(g_rts.mask_tmp); defer_obj_release(g_rts.mask_tmp); }
            g_rts.mask_raw = [g_dev newTextureWithDescriptor:d2]; g_rts.mask_tmp = [g_dev newTextureWithDescriptor:d2]; if (!g_rts.mask_raw || !g_rts.mask_tmp) { g_rts.soft = 0; MLOG("【红】光追阴影：软阴影纹理建不出，退回硬阴影"); } else { res_add(g_rts.mask_raw); res_add(g_rts.mask_tmp); } }
        MLOG("光追阴影：遮罩 %lux%lu%s", (unsigned long)depth.width, (unsigned long)depth.height, g_rts.soft > 0 ? "（软阴影：最近命中 + 两趟模糊）" : ""); }
    rp_end(); if (g_blit) { enc_b_end(g_blit); g_blit = nil; } ser_gate();
    CENC ce = (CENC)pg_compute(); pg_tag_rt();   // 走逐通道计时（DM_NVN_PASSGPU 没开时就是普通计算编码器） | through the per-pass timer (a plain compute encoder when DM_NVN_PASSGPU is off)
    if (!ce) { rts_unqueue(); { static int n; if (n++ < 3) MLOG("【红】光追阴影：计算编码器建不出，本帧不建 BLAS、不追踪（网格留待建，下次命中重排）"); } return; }
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
            m->state = RTS_SUBMITTED; m->queued = 0; if (g_rts.nsub < RTS_BUILDS_PER_FRAME) g_rts.submitted[g_rts.nsub++] = m; g_rts.n_builds++; g_rts.build_tris += m->tris; }
        g_rts.npend = 0;
    }
    if (!trace) { pg_compute_end(ce); enc_c_end(ce); args_dirty_all(); g_pso_dirty = g_dss_dirty = 1; return; }
    int ri = g_rts.ri; g_rts.ri = (ri + 1) % 3;
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
    // 3. TLAS（只放可用的 BLAS：实例登记时已核过，这里再核一遍） | 3. TLAS (usable BLAS only: checked when the instance was recorded, checked again here)
    id<MTLBuffer> ibuf = g_rts.instbuf[ri]; MTLIndirectAccelerationStructureInstanceDescriptor *dst = ibuf.contents; int n = 0;
    for (int i = 0; i < g_rts.ninst; i++) if (g_rts.inst_mesh[i]->state == RTS_READY) dst[n++] = g_rts.inst[i];
    if (!n) { pg_compute_end(ce); enc_c_end(ce); args_dirty_all(); g_pso_dirty = g_dss_dirty = 1; return; }
    MTL4InstanceAccelerationStructureDescriptor *td = [MTL4InstanceAccelerationStructureDescriptor new];
    td.instanceDescriptorType = MTLAccelerationStructureInstanceDescriptorTypeIndirect; td.instanceCount = n; td.instanceDescriptorStride = sizeof(MTLIndirectAccelerationStructureInstanceDescriptor);
    td.instanceDescriptorBuffer = (MTL4BufferRange){ibuf.gpuAddress, (uint64_t)n * sizeof(MTLIndirectAccelerationStructureInstanceDescriptor)};
    [ce buildAccelerationStructure:g_rts.tlas descriptor:td scratchBuffer:scratch];
    [ce barrierAfterEncoderStages:MTLStageAccelerationStructure beforeEncoderStages:MTLStageDispatch visibilityOptions:MTL4VisibilityOptionDevice];
    // 4. 追踪：常量 = 逆视投影（列主序 = 行向量矩阵求逆后原样当列）、太阳、尺寸 | 4. Trace: constants = inverse view-projection (column-major = the inverted row-vector matrix used as columns), sun, size
    double a[16], ia[16]; for (int i = 0; i < 16; i++) a[i] = g_rts.cam[i]; rts_inv4(a, ia);
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
