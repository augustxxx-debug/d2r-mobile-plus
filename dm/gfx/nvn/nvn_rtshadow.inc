// ---------- 光追阴影：用 Metal 硬件光追算太阳与少量点光的逐像素可见度，叠在游戏自己的阴影之上 ----------
// 怎么接（都在翻译层，不改游戏）：
//   1. 阴影通道（无颜色附件、方形 2D 深度）的每次绘制 = 一个遮挡体实例，绘制本身照常画进原阴影图（六审 5.1）。网格键 = 布局 + 全部已校验索引字节的摘要 + 所用顶点范围内属性 0 的摘要
//      （五审 Q5-01、六审 5.2）；BLAS 首次见到时用计算核把属性 0 解码成 float3（SNORM16x4 · exp2(w·13) 等），三角带在 CPU 上展成列表；cb0 的物体→世界 3x4 进 TLAS 实例。
//      网格三态 待建 → 已提交 → 可用（下一帧起）；只有光追帧里可用的网格进 TLAS。光追帧 = 上一帧光追完整；失败按 2 … 512 帧退避（五审 Q5-02）。
//   2. 深度预渲染（无颜色、主深度）的绘制取 cb3 +336 的相机视投影；阴影通道 cb3 +336 是光源视投影，其 z 列（朝天那侧）= 太阳方向。
//   3. 追踪放在主通道第一次绘制之前：建 TLAS、按主深度重建世界坐标、每像素向太阳一条遮挡射线写 R8 遮罩；那时片元阶段绑着的光源记录与分簇表就是本帧主通道要读的那份。
//   4. 读级联阴影图的片元着色器换成 <键>_rt 变体（dm/gfx/tools/rts_variant.py，出包时生成），取 min(遮罩, 原级联采样)，点光立方体阴影处查点光遮罩（rts_pl）；
//      只给主通道里做深度测试、不混合的绘制换（透明 / 水面等接收面与主深度不是同一位置）。光追成功、且本帧每次阴影绘制都在 TLAS 里（镂空、格式不认、没建好、蒙皮没导出都算不在）时，
//      主通道前把原阴影图清回游戏自己的全亮值（rp_begin_ex 登记清除值），否则原阴影留着、变体取 min——失败的那一帧本身也有影子（六审 5.1）。缺变体不再退回整屏压暗（只 DM_RT_SHADOW_DBG 叠加）。
//   5. 蒙皮单位（rt_is_skin 的 10 个顶点着色器）：阴影通道里先照原版画，再用 <键>_pos 顶点变体、关光栅化编一次导出蒙皮后的裁剪坐标；主通道前反投影成世界坐标，以绑定姿势 BLAS 为源 refit 到本实例的槽。
//   6. 点光：经分簇表（簇头 / 槽位）在 GPU 上收集本帧的点光、按亮度与覆盖画面中心的程度挑至多 DM_RT_POINT_N（缺省 2）盏、带滞后，半分辨率每像素各一条到光源的射线；CPU 不读游戏内存。
//      玩家随身光（光照半径）认出后在派生副本里调弱（DM_RT_PLAYER_LIGHT，缺省 0.8），_rt 变体绑副本，游戏原记录不动。
//   7. 无太阳关卡（整帧没有阴影绘制）：遮挡体改从深度预渲染登记，只追点光。软阴影可选（DM_RT_SOFT）：最近命中距离 + 两趟深度感知模糊。
// 资源（六审 5.1、CLAUDE.md「资源使用与释放审查」）：网格缓存有总字节预算（DM_RT_SHADOW_CACHE_MB，缺省 64，含蒙皮槽），超了按最久没用淘汰；600 帧没用的淘汰；
//   每帧常量 / 实例 / 选灯 / 副本缓冲放进带 fence 的槽环（≤ 8），GPU 做完那次提交才复用，全忙本帧不追踪；关掉开关 / 模块失败时全部经 defer_obj_release 释放（等在途提交做完），再开重建。
// 门槛：supportsRaytracing 且 GPU 家族 ≥ Apple9 且机型白名单（Mac 上的 iOS 包按 isiOSAppOnMac 认）；开关：DM_RT_SHADOW=0/1 钉住，否则 dm_rt_shadow_setting()；DM_RT_POINT=0 只关点光。
// 计时：光追计算编码器走 pg_compute（DM_NVN_PASSGPU）；只编 Metal 4 路径，DM_NVN_M3 下模块为空。
// ---------- Ray-traced shadows: per-pixel visibility of the sun and a few point lights via Metal hardware ray tracing, layered over the game's own shadows ----------
// How it hooks in (translation layer only, the game is untouched):
//   1. Every shadow-pass draw (no color attachment, square 2D depth) = one occluder instance, and the draw itself is still encoded into the original shadow map (sixth review 5.1). Mesh key = layout + digest of every validated index byte + digest of
//      attribute 0 over the vertex range used (fifth review Q5-01, sixth review 5.2); on first sight a compute kernel decodes attribute 0 into float3 (SNORM16x4 · exp2(w·13) etc.), strips are expanded into lists on the CPU; cb0's object→world 3x4 goes into the TLAS instance.
//      Meshes go pending → submitted → usable (from the next frame); only usable meshes in a ray-traced frame enter the TLAS. Ray-traced frame = the previous frame was complete; failures back off 2 … 512 frames (fifth review Q5-02).
//   2. The camera view-projection comes from cb3 +336 of the depth prepass (no color, main depth); cb3 +336 of the shadow pass is the light view-projection, whose z column (upward side) = sun direction.
//   3. Tracing happens right before the first draw of the main pass: TLAS build, world positions from the main depth, one occlusion ray per pixel toward the sun into an R8 mask; the light records and cluster tables bound to the fragment stage then are the ones this frame's main pass reads.
//   4. Fragment shaders that sample the cascades are swapped for <key>_rt variants (dm/gfx/tools/rts_variant.py, generated at packaging) taking min(mask, original cascade sample) and looking up the point-light mask at the cube-shadow sample (rts_pl);
//      only depth-tested, unblended draws of the main pass get them (transparent / water receivers are not at the main depth). When the trace succeeded and every shadow draw of the frame is in the TLAS (alpha-tested, unknown format, not built, skinned without export all count as not),
//      the original shadow maps are cleared back to the game's own fully-lit value before the main pass (clear value registered by rp_begin_ex); otherwise they stay and the variants take min - the failing frame keeps its shadows (sixth review 5.1). No fullscreen darkening fallback (only DM_RT_SHADOW_DBG overlays).
//   5. Skinned units (the 10 vertex shaders of rt_is_skin): in the shadow pass the original is drawn first, then the <key>_pos vertex variant with rasterization off exports the skinned clip positions; before the main pass they are unprojected and refit from the bind-pose BLAS into the instance's slot.
//   6. Point lights: the GPU gathers this frame's point lights through the cluster tables (headers / slots), picks up to DM_RT_POINT_N (default 2) by brightness and coverage of the screen-center view line with hysteresis, one ray per pixel to each at half resolution; the CPU never reads game memory.
//      The player's own light (light radius), once recognized, is weakened in a derived copy (DM_RT_PLAYER_LIGHT, default 0.8) that the _rt variants bind; the game's records stay untouched.
//   7. Levels without a sun (no shadow draw in the frame): occluders come from the depth prepass and only point lights are traced. Optional soft shadows (DM_RT_SOFT): nearest-hit distance + two depth-aware blur passes.
// Resources (sixth review 5.1, CLAUDE.md "resource use and release review"): the mesh cache has a total byte budget (DM_RT_SHADOW_CACHE_MB, default 64, skinned slots included) evicting least recently used; meshes unused for 600 frames are evicted;
//   per-frame constant / instance / picking / copy buffers live in a fenced slot ring (≤ 8), reused only after the GPU finished that submission, all busy → no trace this frame; switching off / module failure releases everything through defer_obj_release (after in-flight submissions), rebuilt on the next switch-on.
// Gate: supportsRaytracing and GPU family ≥ Apple9 and a model allowlist (iOS app on a Mac recognized by isiOSAppOnMac); switch: DM_RT_SHADOW=0/1 pins it, otherwise dm_rt_shadow_setting(); DM_RT_POINT=0 turns off point lights only.
// Timing: the ray-tracing compute encoder goes through pg_compute (DM_NVN_PASSGPU); Metal 4 path only, the module is empty under DM_NVN_M3.
__attribute__((weak)) int dm_rt_shadow_setting(void) { return 0; }   // 宿主设置页提供强定义；缺省关 | The host settings page provides the strong definition; off by default
__attribute__((weak)) float dm_rt_shadow_ambient(void) { return 0.45f; }   // 调试叠加的环境项（阴影里保留的亮度比例） | Ambient term of the debug overlay (brightness kept inside shadow)
static int rt_is_skin(void);
#include <sys/sysctl.h>
#include <sys/time.h>
#include "nvn_rtshadow_util.h"
#include "nvn_rtshadow_msl.h"
#if DM_NVN_M3
static int rts_on(void) { return 0; }
static int rts_draw(NvProg *pr, const p_draw *dw, const p_draw_idx *di, int prim) { (void)pr; (void)dw; (void)di; (void)prim; return 0; }
static void rts_set_rt(const p_set_rt *r) { (void)r; }
static void rts_note_pass(RPD *pd) { (void)pd; }
static void rts_select_fs(NvProg *pr) { (void)pr; }
static uint64_t rts_mask_rid(void) { return 0; }
static id<MTLTexture> rts_mask_tex(void) { return nil; }
static uint64_t rts_pos_gpu(void) { return 0; }
static id<MTLBuffer> rts_pos_buf(void) { return nil; }
static uint64_t rts_pmask_rid(void) { return 0; }
static id<MTLTexture> rts_pmask_tex(void) { return nil; }
static uint64_t rts_plidx_gpu(void) { return 0; }
static id<MTLBuffer> rts_plidx_buf(void) { return nil; }
static int rts_keep_raster(void) { return 1; }
static void rts_export_state(NvProg *pr, int on) { (void)pr; (void)on; }
static void rts_export_done(void) {}
static uint64_t rts_lcopy_rid(NvShader *sh, void **tex) { (void)sh; (void)tex; return 0; }
int mtl_rt_shadow_supported(void) { return 0; }
#else
#define RTS_MAX_INST 4096
#define RTS_BUILDS_PER_FRAME 12   // 每帧最多新建这么多 BLAS（新网格首次出现时），其余下一帧再建，避免一次卡顿 | At most this many new BLAS builds per frame (first appearance of new meshes); the rest wait for the next frame to avoid a hitch
#define RTS_MAX_EXPORT 1024       // 每帧最多这么多蒙皮 / 镂空实例走导出 + refit（Mac 地牢实测 256 不够：镂空 245 + 蒙皮） | At most this many skinned / alpha-tested instances per frame go through export + refit (measured in a Mac dungeon: 256 is not enough, 245 alpha-tested + skinned)
#define RTS_MAX_SLOTS 128         // 每个蒙皮网格最多这么多实例槽（同屏同模型的单位数） | At most this many instance slots per skinned mesh (units of the same model on screen)
#define RTS_SLOTS_MAX 8           // 每帧槽环上限（分配器上限也是 8：最多 8 次提交在途） | Cap of the per-frame slot ring (the allocator cap is 8 too: at most 8 submissions in flight)
#define RTS_MEMO 512              // 本帧「布局 → 网格键」备忘的槽数（两级级联重复绘制不重算摘要） | slots of this frame's "layout → mesh key" memo (repeated cascade draws skip the digest)
#define RTS_MAX_SMAP 8            // 每帧登记的阴影图附件（贴图 + mip + 层）上限 | Cap of shadow-map attachments (texture + mip + slice) registered per frame
#define RTS_LIGHT_CAP 256         // 每帧收集的点光上限（GPU 列表） | Cap of point lights gathered per frame (GPU list)
#define RTS_KEEP 1024              // 无太阳关卡保留的静态遮挡体实例上限（屏幕外刚离开的墙 / 柱子继续挡点光） | cap of static occluder instances kept in levels without a sun (walls / pillars that just left the screen keep blocking point lights)
#define RTS_KEEP_FRAMES 90        // 保留多少帧 | for how many frames
#define RTS_MAX_ALPHA 1024        // 每帧最多这么多镂空实例（判定记录放在每帧槽里） | At most this many alpha-tested instances per frame (their judge records live in the frame slot)
#define RTS_AARG 256              // 一份片元参数块快照的字节上限 | byte cap of one fragment argument-block snapshot
// 镂空判定记录（与 nvn_rtshadow_msl.h 的 RtsAI 同布局）：光源视投影（原样，行向量）、视口、深度范围、判定库序号（~0 = 当不透明）、每顶点 float4 数、参数块快照 / 导出缓冲 / 索引的 GPU 地址、索引宽度、插值 locn 位图与槽号
// Alpha judge record (same layout as RtsAI in nvn_rtshadow_msl.h): light view-projection (verbatim, row-vector), viewport, depth range, library index (~0 = treat as opaque), float4s per vertex, GPU addresses of the argument snapshot / export buffer / indices, index width, interpolant locn bitmap and slots
typedef struct { float lvp[16]; float vp[4]; float dr[2]; uint32_t fs, stride; uint64_t args, var, ib; uint32_t isz, locmask; uint8_t slot[16]; } rts_ai;
_Static_assert(sizeof(rts_ai) == 144, "rts_ai layout");
enum { RA_OK, RA_NOLIB, RA_COMPILING, RA_NOFS, RA_NOVAR, RA_LOC, RA_ARGS, RA_NONIDX, RA_SLOT, RA_CAP, RA_QUOTA, RA_FSLOT, RA_MSLOT, RA_N };
static const char *const k_ra_why[RA_N] = {"", "无判定库", "判定库在编", "片元不在库", "顶点无_var", "插值对不上", "参数块过长", "非索引绘制", "网格未就绪", "参数块没抓到", "导出配额满", "每帧槽忙", "网格槽满"};
enum { RM_PRIM, RM_SMALL, RM_ATTR, RM_FMT, RM_ITYPE, RM_INSTANCED, RM_CB0, RM_MESH, RM_NOTRT, RM_NOTREADY, RM_FULL, RM_POSE, RM_ALPHA, RM_N };   // 阴影绘制没进 TLAS 的原因（本帧阴影图因此不清） | why a shadow draw stayed out of the TLAS (so this frame's maps are not cleared)
static const char *const k_rm_why[RM_N] = {"非三角", "顶点不足 3（不画，不挡清图）", "无位置属性", "位置格式不认", "索引宽度不认", "实例化", "无 cb0", "网格取不到", "非光追帧", "网格未建好", "实例满", "蒙皮只有绑定姿势", "镂空"};
static _Atomic int g_rts_alpha_st;   // 判定库：0 未开始 / 1 在编 / 2 可用 / −1 没有或失败 / −2 关了（DM_RT_ALPHA=0） | judge library: 0 not started / 1 compiling / 2 ready / −1 absent or failed / −2 off (DM_RT_ALPHA=0)
static _Atomic uint32_t g_rts_alpha_gen; static pthread_mutex_t g_rts_alpha_mu = PTHREAD_MUTEX_INITIALIZER;   // 整体释放时换代：在编的那次编完就丢 | bumped on release-all: a compile in flight is discarded when it lands
typedef struct { int32_t rec[4]; float pos[4][4]; int32_t n, player, cand_rec, pad; float cand_off[4], p0[4]; } rts_picks;   // 与 nvn_rtshadow_msl.h 的 Picks 同布局 | same layout as Picks in nvn_rtshadow_msl.h
_Static_assert(sizeof(rts_picks) == 128, "rts_picks layout");
typedef struct { id<MTLAccelerationStructure> as; id<MTLBuffer> pos4, pos3, lvp; } rts_islot;   // 蒙皮实例槽；lvp：每个帧槽 256 字节（逆光源视投影 16 floats + 顶点数） | skinned instance slot; lvp: 256 bytes per frame slot (inverse light VP 16 floats + vertex count)
enum { RTS_PENDING, RTS_SUBMITTED, RTS_READY };   // 待建（在缓存里，命中就排队）/ 已提交（构建编进了本帧命令缓冲）/ 可用（下一帧起可进 TLAS、可作 refit 源） | pending (cached, queued on a hit) / submitted (build encoded into this frame's command buffer) / usable (may enter the TLAS and serve as a refit source from the next frame)
typedef struct { uint64_t key, bytes; id<MTLAccelerationStructure> as; id<MTLBuffer> pos, ib; uint64_t last_frame; uint32_t tris, nverts, isz, vstride; int state, queued, skin; NSUInteger as_size, rscratch; rts_islot slots[RTS_MAX_SLOTS]; int nslots, used; } rts_mesh;
#define RTS_MESH_READY(m) ((m)->state == RTS_READY)
#define RTS_LAST_SLOT_BUF() (g_rts.slot[g_rts.cur].inst)   // 单测用：本帧追踪用的实例槽 | for the unit tests: the instance slot this frame's trace used
#define RTS_MAP_CLEARED() (g_rts.map_cleared)
#define RTS_HAS_NOTE_PASS 1
// 每帧槽：fence = 最后一次用它的提交；GPU 做完那次提交（g_done ≥ fence）才复用 | per-frame slot: fence = the last submission that used it; reused only once the GPU finished that submission (g_done >= fence)
typedef struct { id<MTLBuffer> inst, k, d, pk, picks, bb, lcopy, ai, aargs; id<MTLTexture> lctex; uint32_t lcopy_texels; uint64_t fence, frame; int picked; id<MTL4CounterHeap> ts; int ts_state; } rts_fslot;   // ts：光追计算通道起止两个时间戳（自动降档用；ts_state 1 = 写了起点、2 = 起止都写了） | ts: start / end timestamps of the ray-tracing compute pass (for the automatic downgrade; ts_state 1 = start written, 2 = both)
static struct {
    int on, gate, dbg, failed, was_on; uint64_t frame;
    nvn_hmap meshes; int nmesh; uint64_t cache_bytes, budget;   // 网格缓存与总字节预算 | mesh cache and its total byte budget
    MTLIndirectAccelerationStructureInstanceDescriptor inst[RTS_MAX_INST]; uint64_t inst_tag[RTS_MAX_INST]; rts_mesh *inst_mesh[RTS_MAX_INST]; int ninst, ndup, nskip_fmt, nskip_prim, nskin, nskin_pose, nalpha;
    rts_mesh *pending[RTS_BUILDS_PER_FRAME]; struct { uint64_t gpu; uint32_t n, stride, fmt, base; } pend_src[RTS_BUILDS_PER_FRAME]; int npend;   // 源地址只在当帧有效：帧开始时作废 | source addresses are valid for the current frame only: dropped at frame start
    rts_mesh *submitted[RTS_BUILDS_PER_FRAME]; int nsub;   // 本帧编进构建的网格：下一帧开始时转可用 | meshes whose build was encoded this frame: usable from the next frame start
    int rt_frame, backoff; uint64_t retry_at, n_rt_frames, n_fail_frames, n_orig_draws, n_partial_frames;   // 就绪闸门与统计 | readiness gate and counters
    // 本帧的阴影图附件（rp_begin_ex 登记）；nuncov = 本帧没被 TLAS 代表的阴影绘制数；clear_tried / map_cleared = 本帧要不要 / 有没有清回全亮 | this frame's shadow-map attachments (registered by rp_begin_ex); nuncov = shadow draws not represented by the TLAS; clear_tried / map_cleared = whether a clear back to fully lit was attempted / done
    struct { id<MTLTexture> t; NSUInteger level, slice; double cd; int cleared, used, has_l; float l[16]; } smap[RTS_MAX_SMAP]; int nsmap, cur_smap, nuncov, clear_tried, map_cleared;
    struct { uint64_t frame, lay, key; } memo[RTS_MEMO];
    rts_mesh *exp_mesh[RTS_MAX_EXPORT]; rts_islot *exp_slot[RTS_MAX_EXPORT]; int nexp; id<MTLBuffer> cur_pos4; int64_t cur_pos4_off;   // 本帧的蒙皮导出；cur_pos4 = 正在编码的那次导出绘制写的缓冲 | this frame's skinned exports; cur_pos4 = the buffer the export draw being encoded writes
    float cam[16], view[16], cb3raw[128]; int have_cam, have_sun, cb3n; float sun[3];   // view：预渲染 cb3 第 5–8 行的视矩阵（点光记录在视空间） | view: the view matrix in rows 5–8 of the prepass cb3 (point-light records are in view space)   // cb3raw：预渲染 cb3 前 512 字节（诊断用） | cb3raw: first 512 bytes of the prepass cb3 (diagnostic)
    int main_seen, mask_done, overlay_done; uint64_t hdr_ser, ds_ser; id<MTLTexture> main_hdr;
    int shadow_draws, prepass_occ, nprepass;   // 本帧阴影通道绘制数；本帧遮挡体来自深度预渲染（上一帧没有阴影绘制 = 无太阳关卡）；本帧登记的预渲染遮挡体数 | shadow-pass draws this frame; occluders from the depth prepass (no shadow draw last frame = level without a sun); prepass occluders this frame
    id<MTLComputePipelineState> ps_decode, ps_unproj, ps_trace, ps_trace_soft, ps_blur, ps_ptrace, ps_pick, ps_lcopy, ps_lclear, ps_lgather, ps_pup; id<MTLRenderPipelineState> ps_overlay;
    id<MTL4ArgumentTable> at_decode, at_trace, at_overlay, at_blur, at_pick;
    rts_fslot slot[RTS_SLOTS_MAX]; int nslot, cur; uint64_t slot_frame;   // 槽环；cur = 本帧用的槽（slot_frame 那帧取的） | slot ring; cur = this frame's slot (taken in frame slot_frame)
    id<MTLAccelerationStructure> tlas; id<MTLBuffer> scratch; NSUInteger scratch_len;
    id<MTLTexture> mask, white, mask_raw, mask_tmp, pmask, pmaskf, mask_half; int sun_half, point_div; id<MTLBuffer> plidx_none, prev, lseen, llist;   // prev：上一次选中的位置（滞后）；lseen / llist：点光收集的位图与列表 | prev: last picked positions (hysteresis); lseen / llist: bitmap and list of the point-light gather
    int n_rt_loaded, n_rt_missing, n_pos_loaded, n_pos_missing;
    int point_on, point_n; id<MTLBuffer> lbuf; uint64_t lbuf_off; uint32_t lbuf_texels; int lbuf_tid;
    uint64_t s0_gpu, s1_gpu, s0_size, s1_size; const uint32_t *s0_cpu, *s1_cpu;   // 分簇表（簇头、槽位）；CPU 指针只给诊断 | cluster tables (headers, slots); the CPU pointers are for the diagnostic only
    float soft, soft_maxd; float player_scale; rts_player_track ptrack; int ptrack_logged; uint64_t n_player_scaled;
    int pl_done, lc_done; uint64_t n_ptraced; int last_npl;
    int trace_pending; id<MTLTexture> trace_depth; uint64_t n_rt_draws, n_recv_skip, n_late_trace;
    uint64_t n_frames, n_builds, n_traced, n_skipped_draws, n_refit, n_evict_budget, n_slot_busy, key_ns; double build_tris;
    id<MTLComputePipelineState> ps_trace_a, ps_trace_soft_a, ps_ptrace_a; NSDictionary *alpha_idx; int alpha_nfs, alpha_logged; double alpha_ms;   // 判定库版追踪管线（异步编好后置上）；键 → @[序号, locn 位图] | judge-library trace pipelines (set when the async compile lands); key → @[index, locn bitmap]
    uint64_t miss[RM_N];   // 累计 | cumulative
    struct { uint64_t tag, last, seen; rts_mesh *m; MTLIndirectAccelerationStructureInstanceDescriptor d; } keep[RTS_KEEP]; int nkeep, nkeep_used;   // 邻近遮挡体保留（评审阶段一） | neighbourhood occluder retention (review phase 1)
    uint8_t exp_ok[RTS_MAX_EXPORT]; float exp_tr[RTS_MAX_EXPORT][3]; int16_t inst_exp[RTS_MAX_INST]; uint64_t n_exp_lost, n_instanced;   // 导出绘制真编进去了没有（draw 中途返回的不算）；实例 → 导出序号（−1 = 不是导出的） | whether the export draw was actually encoded (a draw that returns early does not count); instance → export index (−1 = not exported)
    int nai, ai_cap, ai_cap_fs, nalpha_rt, n_var_loaded, n_var_missing, n_alpha_logs; NvShader *exp_vs; uint64_t na_why[RA_N], n_alpha_rt_draws;   // 本帧镂空实例数 / 待抓参数块的记录；exp_vs = 本次导出绘制用的顶点变体（_pos / _var） | alpha instances this frame / the record awaiting its argument capture; exp_vs = the vertex variant of the export draw (_pos / _var)
    id<MTLBuffer> badv; uint32_t bad_seen[2];   // 坏顶点计数缓冲与已报过的数 | bad-vertex counter buffer and the counts already reported
    double hang_t; int hang_seen, hang_events;   // GPU 挂起保护：已看过的挂起计数、上次挂起事件时刻、事件数 | GPU hang protection: hang count already seen, time of the last hang event, number of events
    rts_gov gov; rts_lvl_params eff; int auto_on, lvl_pin; double ts_tick, rt_sum, fr_sum; uint64_t rt_n, fr_n, n_lvl_down, n_lvl_up;   // 自动降档：档位状态、本档实际参数、时间戳每拍毫秒、600 帧统计 | automatic downgrade: level state, effective parameters, ms per timestamp tick, 600-frame counters
    int ao_on, ao_rays, ao_done, ao_applied; float ao_radius, ao_strength; id<MTLComputePipelineState> ps_ao, ps_aoblur, ps_ao_a; id<MTLTexture> ao_half, ao_tmp, ao_full; uint64_t n_ao_traced, n_ao_applied, n_ao_missed;   // 光追 AO（实验性）：设置、管线、半分辨率 / 模糊 / 全分辨率贴图、统计 | ray-traced AO (experimental): settings, pipelines, half-res / blur / full-res textures, counters
    int last_r;   // 上一次绘制的答案：draw 重入（g_draw_reentry）时原样复用，不重复登记实例 / 槽 | the previous draw's answer: reused on draw re-entry (g_draw_reentry), so instances / slots are not registered twice
} g_rts = { .on = -1, .meshes = {.klen = 8}, .cur_smap = -1, .cur = -1, .ai_cap = -1 };
int mtl_rt_shadow_supported(void) {   // 宿主设置页用：这台机器能不能显示「光追阴影」 | For the host settings page: whether this machine may show "ray-traced shadows"
    static int v = -1; if (v >= 0) return v;
    v = 0; if (!g_dev) return 0;
    int rt = [g_dev supportsRaytracing], fam = [g_dev supportsFamily:MTLGPUFamilyApple9];
    char hw[64] = "?"; size_t hn = sizeof hw; sysctlbyname("hw.machine", hw, &hn, NULL, 0); int model = 0, major = 0, minor = 0;
#if TARGET_OS_OSX
    int on_mac = 1;   // Mac 原生宿主：只看 GPU 家族 | native Mac host: GPU family alone decides
#else
    int on_mac = [NSProcessInfo.processInfo isiOSAppOnMac];   // Mac 上的 iOS 包：hw.machine 报的是兼容用的 iPad 型号（实测 iPad8,6），不能按它判 | iOS app on a Mac: hw.machine reports a compatibility iPad model (measured iPad8,6), not usable for the allowlist
#endif
    if (on_mac) { model = 1; snprintf(hw, sizeof hw, "Mac"); }
    else if (sscanf(hw, "iPhone%d,%d", &major, &minor) == 2) model = major >= 17 || (major == 16 && (minor == 1 || minor == 2));   // iPhone 15 Pro / Pro Max 起（A17 Pro） | iPhone 15 Pro / Pro Max and later (A17 Pro)
    else if (sscanf(hw, "iPad%d,%d", &major, &minor) == 2) model = major >= 16;   // iPad Pro M4 起 | iPad Pro M4 and later
    else model = 0;   // 其它型号不在白名单 | any other model is not on the allowlist
    v = rt && fam && model;
    MLOG("光追阴影：机器判定 %s（supportsRaytracing %d、GPU 家族 Apple9 %d、机型 %s %s）", v ? "可用" : "不可用", rt, fam, hw, model ? "在白名单" : "不在白名单");
    return v;
}
// 测试翻转 DM_RT_SHADOW_FLIP=<秒>[,<秒>…] 或 f<出图次数>[,…]（造对比素材用；f 与 DM_NVN_SHOT 的帧号同一口径）：到点把开关取反，同一局同一机位拍开 / 关 | test flip DM_RT_SHADOW_FLIP=<s>[,<s>...] or f<present count>[,...] (for comparison material; f uses the same numbering as DM_NVN_SHOT): invert the switch at these points, shooting on / off in one run from one spot
uint64_t mtl_present_count(void);   // nvn_mtl.m：出图次数（与 DM_NVN_SHOT 的帧号同一口径） | nvn_mtl.m: present count (the same numbering as DM_NVN_SHOT)
static double rts_proc_secs(void) {
    static double t0 = -1; struct timeval now; gettimeofday(&now, NULL);
    if (t0 < 0) { struct kinfo_proc kp; size_t len = sizeof kp; int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid()};
        t0 = sysctl(mib, 4, &kp, &len, NULL, 0) == 0 ? kp.kp_proc.p_starttime.tv_sec + kp.kp_proc.p_starttime.tv_usec / 1e6 : now.tv_sec + now.tv_usec / 1e6; }
    return now.tv_sec + now.tv_usec / 1e6 - t0;
}
static int rts_flip(int v) {
    static double at[16]; static int n = -1, last = -1, by_frame;
    if (n < 0) { n = 0; const char *e = getenv("DM_RT_SHADOW_FLIP"); by_frame = e && e[0] == 'f';
        for (const char *q = e; q && *q && n < 16; q = strchr(q, ',') ? strchr(q, ',') + 1 : NULL) at[n++] = atof(q + (*q == 'f')); }
    if (!n) return v;
    double t = by_frame ? (double)mtl_present_count() : rts_proc_secs(); int k = 0; for (int i = 0; i < n; i++) k += t >= at[i];
    int r = k & 1 ? !v : v; if (r != last) { last = r; MLOG("光追阴影：测试翻转 → %s（%s %.1f，DM_RT_SHADOW_FLIP）", r ? "开" : "关", by_frame ? "帧" : "进程第几秒", t); }
    return r;
}
static void rts_release_all(const char *why);
static rts_lvl_params rts_lvl_of(int lvl) { return rts_lvl_apply(lvl, g_rts.point_on, g_rts.point_n, g_rts.point_div, g_rts.sun_half, g_rts.soft > 0, g_rts.ao_on); }
static void rts_lvl_update(void) { g_rts.eff = rts_lvl_of(g_rts.gov.lvl); }
static const char *rts_lvl_desc_p(const rts_lvl_params *e, char *b, size_t n) {   // 某档的实际参数（日志用） | effective parameters of a level (for the log)
    if (e->point_on) snprintf(b, n, "太阳 %s、点光 ≤ %d 盏 1/%d 分辨率", e->soft ? "全分辨率软阴影" : e->sun_div == 1 ? "全分辨率" : e->sun_div == 2 ? "1/2 分辨率" : "1/4 分辨率", e->point_n, e->point_div);
    else snprintf(b, n, "太阳 %s、点光阴影关", e->soft ? "全分辨率软阴影" : e->sun_div == 1 ? "全分辨率" : e->sun_div == 2 ? "1/2 分辨率" : "1/4 分辨率");
    if (g_rts.ao_on) { size_t l = strlen(b); snprintf(b + l, n > l ? n - l : 0, "、光追 AO %s", e->ao ? "开" : "关"); }
    return b;
}
static const char *rts_lvl_desc(char *b, size_t n) { return rts_lvl_desc_p(&g_rts.eff, b, n); }
static int rts_on(void) {
    if (g_rts.on < 0) {
        const char *e = getenv("DM_RT_SHADOW"); g_rts.gate = mtl_rt_shadow_supported(); g_rts.dbg = getenv("DM_RT_SHADOW_DBG") ? atoi(getenv("DM_RT_SHADOW_DBG")) : 0;
        g_rts.on = e ? atoi(e) != 0 : -2;
        const char *b = getenv("DM_RT_SHADOW_CACHE_MB"); g_rts.budget = (uint64_t)(b && atoi(b) > 0 ? atoi(b) : 64) << 20;
        const char *se = getenv("DM_RT_SOFT"), *sd = getenv("DM_RT_SOFT_MAXD"); g_rts.soft = se ? (float)atof(se) : 0.f; g_rts.soft_maxd = sd ? (float)atof(sd) : 200.f; if (g_rts.soft < 0) g_rts.soft = 0;
        const char *pl = getenv("DM_RT_PLAYER_LIGHT"); g_rts.player_scale = pl ? (float)atof(pl) : 0.8f; if (!(g_rts.player_scale >= 0 && g_rts.player_scale <= 1)) g_rts.player_scale = 1; g_rts.ptrack.rec = -1;   // 玩家随身光颜色倍率（1 = 不动） | player-light color factor (1 = untouched)
        { const char *pd = getenv("DM_RT_POINT_DIV"); g_rts.point_div = pd && (atoi(pd) == 2 || atoi(pd) == 4) ? atoi(pd) : 4; }   // 点光遮罩追踪分辨率：全分辨率的 1/4（缺省）或 1/2，之后深度感知上采样 | point-mask trace resolution: 1/4 (default) or 1/2 of full, then depth-aware upsampling
        { const char *sh = getenv("DM_RT_SUN_HALF"); g_rts.sun_half = sh ? atoi(sh) != 0 : 1; }   // 太阳射线半分辨率 + 深度感知上采样（缺省开；0 = 全分辨率） | half-resolution sun rays + depth-aware upsampling (default on; 0 = full resolution)
        const char *pe = getenv("DM_RT_POINT"), *pn = getenv("DM_RT_POINT_N"); g_rts.point_on = pe ? atoi(pe) != 0 : 1; g_rts.point_n = pn ? atoi(pn) : 2; if (g_rts.point_n > 4) g_rts.point_n = 4; if (g_rts.point_n < 1) g_rts.point_on = 0;
        // 光追 AO（实验性，计划第 5 步）：缺省关，DM_RT_AO=1 开；半径（世界单位）、强度（0–1，HDR 乘 1 − 强度 × 遮挡）、每像素射线数 | ray-traced AO (experimental, plan step 5): off by default, DM_RT_AO=1 turns it on; radius (world units), strength (0–1, the HDR is multiplied by 1 − strength × occlusion), rays per pixel
        { const char *ao = getenv("DM_RT_AO"), *ar = getenv("DM_RT_AO_RADIUS"), *as = getenv("DM_RT_AO_STRENGTH"), *an = getenv("DM_RT_AO_RAYS");
          g_rts.ao_on = ao && atoi(ao) != 0; g_rts.ao_radius = ar ? (float)atof(ar) : 6.f; if (!(g_rts.ao_radius > 0 && g_rts.ao_radius <= 100)) g_rts.ao_radius = 6.f;
          g_rts.ao_strength = as ? (float)atof(as) : 0.6f; if (!(g_rts.ao_strength >= 0 && g_rts.ao_strength <= 1)) g_rts.ao_strength = 0.6f; g_rts.ao_rays = an ? atoi(an) : 4; if (g_rts.ao_rays < 1 || g_rts.ao_rays > 16) g_rts.ao_rays = 4; }
        // 自动降档：缺省开（DM_RT_AUTO=0 关）；DM_RT_LEVEL=<0–3> 钉住档位（同时关掉自动） | automatic downgrade: on by default (DM_RT_AUTO=0 turns it off); DM_RT_LEVEL=<0–3> pins a level (and turns automatic off)
        { const char *ae = getenv("DM_RT_AUTO"), *le = getenv("DM_RT_LEVEL"); g_rts.lvl_pin = le ? atoi(le) : -1; if (g_rts.lvl_pin > RTS_LVL_MAX) g_rts.lvl_pin = RTS_LVL_MAX;
          g_rts.auto_on = g_rts.lvl_pin < 0 && (ae ? atoi(ae) != 0 : 1); rts_gov_init(&g_rts.gov, RTS_LVL_MAX); if (g_rts.lvl_pin > 0) g_rts.gov.lvl = g_rts.lvl_pin; rts_lvl_update(); }   // 缺省 2 盏（评审：先 1–2 盏重要灯） | 2 by default (review: 1–2 important lights first)
        // 前缀与宿主 dm_rt_shadow_setting 的「光追阴影：生效」同一个：钉住时不走宿主那条，分享诊断日志的摘要照样取得到 | same prefix as "光追阴影：生效" in the host dm_rt_shadow_setting: when pinned that path is skipped, and the share-diagnostics summary still finds the line
        if (e) MLOG("光追阴影：生效 %s（测试开关 DM_RT_SHADOW=%s 钉住%s）", atoi(e) && g_rts.gate ? "开" : "关", e, g_rts.gate ? "" : "，机器不可用，仍不生效");
    }
    int v = !g_rts.gate || g_rts.failed ? 0 : rts_flip(g_rts.on == -2 ? dm_rt_shadow_setting() != 0 : g_rts.on);
    if (!v) g_rts.hang_seen = atomic_load(&g_gpu_hang_n);   // 光追关着时的挂起不算光追的 | hangs while ray tracing is off do not count against it
    if (!v && g_rts.was_on) rts_release_all(g_rts.failed ? "模块失败" : "开关关了");   // 关掉 / 失败：整个缓存等 GPU 在途工作做完后释放（六审 5.1） | off / failed: the whole cache is released once in-flight GPU work is done (sixth review 5.1)
    g_rts.was_on = v;
    return v;
}
static const uint8_t *rts_mem(uint64_t addr, uint64_t len) { void *m; uint64_t o; if (!addr || !len || !mtl_addr(addr, len, &m, &o)) return NULL; const uint8_t *b = [(__bridge id<MTLBuffer>)m contents]; return b ? b + o : NULL; }
static uint64_t rts_fnv(uint64_t h, const void *p, size_t n) { const uint8_t *b = p; for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 1099511628211ull; return h; }
// 整段摘要（六审 5.2：网格键覆盖全部已校验的索引字节与顶点属性 0，后段变了键就变）：四路并行、一次 8 字节 | Whole-span digest (sixth review 5.2: the key covers every validated index byte and vertex attribute 0): four lanes, 8 bytes at a time
static inline uint64_t rts_mix(uint64_t h, uint64_t w) { h = (h ^ w) * 0x9E3779B97F4A7C15ull; return h ^ (h >> 29); }
static uint64_t rts_digest(uint64_t h, const uint8_t *p, size_t n) {
    uint64_t l[4] = {h, h ^ 1, h ^ 2, h ^ 3}; size_t i = 0;
    for (; i + 32 <= n; i += 32) for (int j = 0; j < 4; j++) { uint64_t w; memcpy(&w, p + i + j * 8, 8); l[j] = rts_mix(l[j], w); }
    for (; i + 8 <= n; i += 8) { uint64_t w; memcpy(&w, p + i, 8); l[0] = rts_mix(l[0], w); }
    uint64_t t = 0; memcpy(&t, p + i, n - i); l[1] = rts_mix(l[1], t ^ ((uint64_t)n << 56));
    return rts_mix(rts_mix(rts_mix(l[0], l[1]), l[2]), l[3]);
}
// 顶点属性 0 的摘要：第 v0 … v0+nv−1 个顶点各取 asz 字节（步长 stride） | Digest of vertex attribute 0: asz bytes of each of vertices v0 … v0+nv−1 (stride apart)
static uint64_t rts_vdigest(uint64_t h, const uint8_t *vp, uint64_t v0, uint64_t nv, uint64_t stride, uint64_t asz) {
    if (stride == asz) return rts_digest(h, vp + v0 * stride, (size_t)(nv * stride));   // 位置单独一条流：整段连续 | position in its own stream: one contiguous span
    uint64_t l[4] = {h, h ^ 1, h ^ 2, h ^ 3};
    for (uint64_t v = 0; v < nv; v++) { const uint8_t *q = vp + (v0 + v) * stride; uint64_t w; memcpy(&w, q, 8); uint64_t *L = &l[v & 3]; *L = rts_mix(*L, w);
        if (asz > 8) { uint64_t w1 = 0; memcpy(&w1, q + 8, asz - 8); *L = rts_mix(*L, w1); } }
    return rts_mix(rts_mix(rts_mix(rts_mix(l[0], l[1]), l[2]), l[3]), nv);
}
static uint64_t rts_attr_bytes(uint32_t fmt) { return fmt == 0x2e ? 16 : fmt == 0x22 ? 12 : 8; }   // 解码核读的属性 0 宽度（FLOAT4 / FLOAT3 / SNORM16x4、HALF4） | Width of attribute 0 as read by the decode kernel
#define RTS_NEW(field, expr) do { (field) = (expr); if (field) res_add(field); } while (0)
static int rts_slot_new(void) {   // 新加一个每帧槽；失败返回 -1 | Add one per-frame slot; -1 on failure
    if (g_rts.nslot >= RTS_SLOTS_MAX) return -1;
    int i = g_rts.nslot; rts_fslot *s = &g_rts.slot[i]; memset(s, 0, sizeof *s);
    RTS_NEW(s->inst, [g_dev newBufferWithLength:RTS_MAX_INST * sizeof(MTLIndirectAccelerationStructureInstanceDescriptor) options:MTLResourceStorageModeShared]);
    RTS_NEW(s->k, [g_dev newBufferWithLength:256 options:MTLResourceStorageModeShared]); RTS_NEW(s->d, [g_dev newBufferWithLength:64 options:MTLResourceStorageModeShared]);
    RTS_NEW(s->pk, [g_dev newBufferWithLength:512 options:MTLResourceStorageModeShared]);   // 0 选灯常量 PK（160 字节，含逆视矩阵）、160 点光追踪常量 P、256 副本常量 LC、384 收集常量 LG、448 点光上采样 PU、464 太阳上采样 PU | 0 pick PK (160 bytes incl. the inverse view), 160 point trace P, 256 copy LC, 384 gather LG, 448 point upsampling PU, 464 sun upsampling PU
    RTS_NEW(s->picks, [g_dev newBufferWithLength:sizeof(rts_picks) options:MTLResourceStorageModeShared]); RTS_NEW(s->bb, [g_dev newBufferWithLength:128 options:MTLResourceStorageModeShared]);   // 0–63 软阴影两趟模糊常量、64 光追 AO 的 PU | 0–63 soft-shadow blur constants (two passes), 64 the PU of the ray-traced AO
    RTS_NEW(s->ai, [g_dev newBufferWithLength:RTS_MAX_ALPHA * sizeof(rts_ai) options:MTLResourceStorageModeShared]); RTS_NEW(s->aargs, [g_dev newBufferWithLength:RTS_MAX_ALPHA * RTS_AARG options:MTLResourceStorageModeShared]);   // 镂空判定记录与参数块快照 | alpha judge records and argument snapshots
    if (!s->inst || !s->k || !s->d || !s->pk || !s->picks || !s->bb || !s->ai || !s->aargs) { id o[8] = {s->inst, s->k, s->d, s->pk, s->picks, s->bb, s->ai, s->aargs}; for (int j = 0; j < 8; j++) if (o[j]) defer_obj_release(o[j]); memset(s, 0, sizeof *s); return -1; }
    { MTL4CounterHeapDescriptor *hd = [MTL4CounterHeapDescriptor new]; hd.type = MTL4CounterHeapTypeTimestamp; hd.count = 2; s->ts = [g_dev newCounterHeapWithDescriptor:hd error:nil];   // 自动关 / 钉住档位时也量（日志照样有光追 GPU 耗时）；建不出就不量这个槽（不影响光追本身） | measured also with automatic off / a pinned level (the log still has the ray-tracing GPU time); if it cannot be created this slot is just not measured (ray tracing unaffected)
        if (!g_rts.ts_tick) { uint64_t fq = [g_dev queryTimestampFrequency]; g_rts.ts_tick = fq ? 1e3 / (double)fq : 1e-6; } }
    rts_picks *o = s->picks.contents; memset(o, 0, sizeof *o); o->player = o->cand_rec = -1;
    g_rts.nslot++; return i;
}
// 本帧的槽：本帧第一次需要时取一个 GPU 已用完的（g_done ≥ 它上次那次提交），都在用就加一槽，到上限返回 -1（本帧不追踪、不导出）；fence 记成本次提交（与 defer_obj_release 同一口径）
// This frame's slot: on first need take one the GPU has finished with (g_done >= its last submission), all in use → add one, at the cap return -1 (no trace, no export this frame); the fence is stamped with this submission (same convention as defer_obj_release)
static double rts_now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (double)t.tv_sec + t.tv_nsec * 1e-9; }
// 槽上次那一帧的光追计算通道 GPU 耗时（起止时间戳，GPU 已做完才读，不等待）→ 统计、喂自动降档；换档写「光追降档：」 | GPU time of the ray-tracing compute pass of the slot's previous frame (start / end timestamps, read only once the GPU is done, never waits) → counters and the automatic downgrade; level changes log 「光追降档：」
static void rts_ts_take(rts_fslot *s) {
    int st = s->ts_state; s->ts_state = 0; if (st != 2 || !s->ts) return;
    NSData *d = [s->ts resolveCounterRange:NSMakeRange(0, 2)]; if (!d || d.length < 2 * sizeof(MTL4TimestampHeapEntry)) return;
    const MTL4TimestampHeapEntry *e = d.bytes; if (!e[0].timestamp || e[1].timestamp < e[0].timestamp) return;
    double rt = (double)(e[1].timestamp - e[0].timestamp) * g_rts.ts_tick, fr = mtl_gpu_ms(s->frame), hz = mtl_pace_hz(), budget = 1000.0 / (hz > 0 ? hz : 60);
    if (!(rt < 1000)) return;   // 读数坏了（GPU 时钟回绕之类）不算 | a broken reading (GPU clock wrap and the like) is ignored
    g_rts.rt_sum += rt; g_rts.rt_n++; if (fr > 0) { g_rts.fr_sum += fr; g_rts.fr_n++; }
    if (!g_rts.auto_on) return;
    const char *why = ""; int old = g_rts.gov.lvl, r = rts_gov_step(&g_rts.gov, rt, fr, budget, rts_now(), &why); if (!r) return;
    if (r > 0) g_rts.n_lvl_down++; else g_rts.n_lvl_up++;
    char b[160]; rts_lvl_params nx = rts_lvl_of(g_rts.gov.lvl);   // 下一帧开头才换（rts_frame_begin）：本帧前后用同一套参数 | applied at the next frame start (rts_frame_begin): the whole of this frame keeps one set of parameters
    MLOG("光追降档：第 %d 档 → 第 %d 档（%s：光追 GPU 约 %.2f ms、整帧 GPU 约 %.2f ms，预算 %.2f ms / %.0f Hz；再升档要先有余量 %.0f 秒）→ %s", old, g_rts.gov.lvl, why, g_rts.gov.rt, g_rts.gov.fr, budget, hz > 0 ? hz : 60, g_rts.gov.hold_up, rts_lvl_desc_p(&nx, b, sizeof b));
}
static int rts_frame_slot(void) {
    if (g_rts.slot_frame == g_rts.frame && g_rts.cur >= 0) return g_rts.cur;
    if (g_rts.slot_frame == g_rts.frame) return -1;   // 本帧已经试过、没取到 | already tried this frame without success
    g_rts.slot_frame = g_rts.frame; g_rts.cur = -1;
    uint64_t done = g_done; int i;
    for (i = 0; i < g_rts.nslot; i++) if (g_rts.slot[i].fence <= done) break;
    if (i < g_rts.nslot) rts_ts_take(&g_rts.slot[i]);   // GPU 做完了它上次那一帧：先把那帧的光追耗时取走 | the GPU finished its previous frame: take that frame's ray-tracing time first
    if (i == g_rts.nslot && (i = rts_slot_new()) < 0) { g_rts.n_slot_busy++;
        if (g_rts.n_slot_busy <= 3 || g_rts.n_slot_busy % 100 == 0) MLOG("光追阴影：本帧不追踪（%d 个每帧槽都还在 GPU 上，第 %llu 次；原版阴影照常）", g_rts.nslot, (unsigned long long)g_rts.n_slot_busy);
        return -1; }
    g_rts.slot[i].fence = g_fence + 1; g_rts.slot[i].frame = g_rts.frame; g_rts.slot[i].picked = 0; g_rts.cur = i;
    return i;
}
// 判定库（第四阶段·镂空遮挡体）：包里的 rts_alpha.metal（rts_variant.py 出，阴影通道的镂空片元着色器各在自己的命名空间）拼在追踪核前面、带 RTS_ALPHA 后台编译，
// 编好前镂空绘制照旧留原阴影图（带它们的帧不清阴影图）。DM_RT_ALPHA=0 关掉（对照用）
// Judge library (phase 4 · alpha-tested occluders): the package's rts_alpha.metal (from rts_variant.py, every alpha-tested shadow-pass fragment shader in its own namespace) is prepended to the trace kernels and compiled in the background with RTS_ALPHA;
// until it lands, alpha-tested draws stay in the original shadow maps as before (frames with them keep their maps). DM_RT_ALPHA=0 turns it off (for comparison)
static void rts_alpha_start(void) {
    if (atomic_load(&g_rts_alpha_st) != 0) return;
    const char *ae = getenv("DM_RT_ALPHA"); if (ae && !atoi(ae)) { atomic_store(&g_rts_alpha_st, -2); MLOG("光追镂空：关（DM_RT_ALPHA=0），镂空遮挡体留原阴影图"); return; }
    NSString *src = [NSString stringWithContentsOfFile:[g_msl_dir stringByAppendingPathComponent:@"rts_alpha.metal"] encoding:NSUTF8StringEncoding error:nil];
    NSData *jd = [NSData dataWithContentsOfFile:[g_msl_dir stringByAppendingPathComponent:@"rts_alpha.json"]];
    NSDictionary *j = jd ? [NSJSONSerialization JSONObjectWithData:jd options:0 error:nil] : nil; NSArray *fs = [j isKindOfClass:NSDictionary.class] ? j[@"fs"] : nil;
    if (!src || ![fs isKindOfClass:NSArray.class] || !fs.count) { atomic_store(&g_rts_alpha_st, -1); MLOG("光追镂空：包里没有判定库 rts_alpha.metal / .json（着色器库 %s），镂空遮挡体留原阴影图", g_msl_dir.lastPathComponent.UTF8String ?: "?"); return; }   // 只记末段（b-<链>）：完整路径带容器 UUID | only the last component (b-<chain>): the full path carries the container UUID
    NSMutableDictionary *idx = [NSMutableDictionary dictionary];
    for (NSUInteger i = 0; i < fs.count; i++) { NSDictionary *e = fs[i]; if (![e isKindOfClass:NSDictionary.class] || ![e[@"key"] isKindOfClass:NSString.class]) continue; uint32_t mask = 0;
        for (NSNumber *l in [e[@"locs"] isKindOfClass:NSArray.class] ? e[@"locs"] : @[]) if ([l isKindOfClass:NSNumber.class] && l.intValue >= 0 && l.intValue < 16) mask |= 1u << l.intValue;
        idx[e[@"key"]] = @[@(i), @(mask)]; }
    g_rts.alpha_idx = idx; g_rts.alpha_nfs = (int)fs.count; atomic_store(&g_rts_alpha_st, 1);
    uint32_t gen = atomic_load(&g_rts_alpha_gen); double t0 = bp_now() / 1e6;
    const char *dbg = getenv("DM_RT_ALPHA_DBG");   // 排查用：1 = 命中即挡（不读记录）、2 = 读插值与像素坐标但不调判定 | for bisecting: 1 = every hit blocks (records unread), 2 = read interpolants and pixel but skip the judge
    const char *only = getenv("DM_RT_ALPHA_ONLY");   // 排查用：只判定这些序号（位图），其余命中即挡 | for bisecting: judge only these indices (bitmap), any other hit blocks
    const char *lod = getenv("DM_RT_ALPHA_LOD"); float lodv = lod ? (float)atof(lod) : 0.f; if (!(lodv >= 0 && lodv <= 12)) lodv = 0.f;   // 判定采样的 mip 级（缺省 0） | mip level of judge samples (0 by default)
    const char *mr = getenv("DM_RT_ALPHA_MAXREJ"); int mrv = mr ? atoi(mr) : 8; if (mrv < 1 || mrv > 1000) mrv = 8;   // 每条射线最多判丢几层就当挡住（缺省 8） | discarded layers per ray before it counts as blocked (8 by default)
    NSString *full = [NSString stringWithFormat:@"#define RTS_ALPHA 1\n#define RTS_ALPHA_DBG %d\n#define RTS_ALPHA_LOD %.1f\n#define RTS_ALPHA_MAXREJ %d\n%s%s%s%s%@\n%s", dbg ? atoi(dbg) : 0, lodv, mrv, only ? "#define RTS_ALPHA_ONLY " : "", only ?: "", only ? "u\n" : "", k_rts_alpha_head, src, k_rts_msl];
    MLOG("光追镂空：判定库 %d 个片元着色器，后台编译中（%lu 字节，判定采样 mip %.1f、每射线最多判丢 %d 层）", (int)fs.count, (unsigned long)full.length, lodv, mrv);
    [g_dev newLibraryWithSource:full options:nil completionHandler:^(id<MTLLibrary> lib, NSError *e) {
        NSError *pe = nil; id<MTLComputePipelineState> a = nil, b = nil, c = nil;
        if (lib) { a = [g_dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"rts_trace"] error:&pe]; b = [g_dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"rts_trace_soft"] error:&pe]; c = [g_dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"rts_ptrace"] error:&pe]; }
        pthread_mutex_lock(&g_rts_alpha_mu);
        if (gen == atomic_load(&g_rts_alpha_gen)) {
            if (a && b && c) { g_rts.ps_trace_a = a; g_rts.ps_trace_soft_a = b; g_rts.ps_ptrace_a = c; g_rts.ps_ao_a = lib ? [g_dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"rts_ao"] error:nil] : nil; g_rts.alpha_ms = bp_now() / 1e6 - t0; atomic_store(&g_rts_alpha_st, 2); }
            else { atomic_store(&g_rts_alpha_st, -1); fprintf(stderr, "[rts] 光追镂空：判定库编不出 %s\n", (e ?: pe).localizedDescription.UTF8String ?: "?"); }
        }
        pthread_mutex_unlock(&g_rts_alpha_mu);
    }];
}
static int rts_setup(void) {
    if (g_rts.ps_trace) return 1;
    if (g_rts.failed) return 0;
    NSError *e = nil; id<MTLLibrary> lib = [g_dev newLibraryWithSource:@(k_rts_msl) options:nil error:&e];
    if (!lib) { g_rts.failed = 1; MLOG("【红】光追阴影：着色器编译失败 %s", e.localizedDescription.UTF8String); return 0; }
    const char *fn[13] = {"rts_decode", "rts_unproj", "rts_trace", "rts_trace_soft", "rts_blur", "rts_ptrace", "rts_pick", "rts_lcopy", "rts_lclear", "rts_lgather", "rts_pup", "rts_ao", "rts_aoblur"};
    id<MTLComputePipelineState> __strong *ps[13] = {&g_rts.ps_decode, &g_rts.ps_unproj, &g_rts.ps_trace, &g_rts.ps_trace_soft, &g_rts.ps_blur, &g_rts.ps_ptrace, &g_rts.ps_pick, &g_rts.ps_lcopy, &g_rts.ps_lclear, &g_rts.ps_lgather, &g_rts.ps_pup, &g_rts.ps_ao, &g_rts.ps_aoblur};
    int ok = 1; for (int i = 0; i < 13 && ok; i++) { *ps[i] = [g_dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@(fn[i])] error:&e]; ok = *ps[i] != nil; }
    MTL4ArgumentTableDescriptor *ad = [MTL4ArgumentTableDescriptor new]; ad.maxBufferBindCount = 4; ad.maxTextureBindCount = 2;   // 第 4 个缓冲 = 坏顶点计数（解码 / 反投影） | buffer 4 = bad-vertex counters (decode / unproject)
    g_rts.at_decode = [g_dev newArgumentTableWithDescriptor:ad error:&e]; g_rts.at_overlay = [g_dev newArgumentTableWithDescriptor:ad error:&e];
    { MTL4ArgumentTableDescriptor *td4 = [MTL4ArgumentTableDescriptor new]; td4.maxBufferBindCount = 4; td4.maxTextureBindCount = 2; g_rts.at_trace = [g_dev newArgumentTableWithDescriptor:td4 error:&e]; }   // 第 4 个缓冲 = 镂空判定记录 | buffer 4 = alpha judge records
    { MTL4ArgumentTableDescriptor *bd = [MTL4ArgumentTableDescriptor new]; bd.maxBufferBindCount = 1; bd.maxTextureBindCount = 3; g_rts.at_blur = [g_dev newArgumentTableWithDescriptor:bd error:&e]; }
    { MTL4ArgumentTableDescriptor *pd = [MTL4ArgumentTableDescriptor new]; pd.maxBufferBindCount = 5; pd.maxTextureBindCount = 1; g_rts.at_pick = [g_dev newArgumentTableWithDescriptor:pd error:&e]; }
    RTS_NEW(g_rts.prev, [g_dev newBufferWithLength:64 options:MTLResourceStorageModeShared]); if (g_rts.prev) memset(g_rts.prev.contents, 0, 64);
    if (!g_rts.badv) { RTS_NEW(g_rts.badv, [g_dev newBufferWithLength:16 options:MTLResourceStorageModeShared]); if (g_rts.badv) memset(g_rts.badv.contents, 0, 16); }   // 坏顶点计数：[0] 解码、[1] 反投影（GPU 原子加，CPU 只读做统计） | bad-vertex counters: [0] decode, [1] unproject (GPU atomic adds, the CPU only reads them for stats)
    RTS_NEW(g_rts.lseen, [g_dev newBufferWithLength:2048 * 4 options:MTLResourceStorageModePrivate]); RTS_NEW(g_rts.llist, [g_dev newBufferWithLength:(1 + RTS_LIGHT_CAP) * 4 options:MTLResourceStorageModePrivate]);
    if (!g_rts.plidx_none) { RTS_NEW(g_rts.plidx_none, [g_dev newBufferWithLength:16 options:MTLResourceStorageModeShared]); if (g_rts.plidx_none) { int32_t none[4] = {-1, -1, -1, -1}; memcpy(g_rts.plidx_none.contents, none, 16); } }
    int s0 = rts_slot_new();
    // TLAS 按最大实例数一次建够；暂存缓冲按 TLAS 的需求起步，BLAS / refit 需要更大时再换 | The TLAS is created once for the maximum instance count; the scratch buffer starts at the TLAS's need and is replaced when a BLAS / refit needs more
    MTL4InstanceAccelerationStructureDescriptor *td = [MTL4InstanceAccelerationStructureDescriptor new];
    td.instanceDescriptorType = MTLAccelerationStructureInstanceDescriptorTypeIndirect; td.instanceCount = RTS_MAX_INST; td.instanceDescriptorStride = sizeof(MTLIndirectAccelerationStructureInstanceDescriptor);
    if (s0 >= 0) td.instanceDescriptorBuffer = (MTL4BufferRange){g_rts.slot[s0].inst.gpuAddress, g_rts.slot[s0].inst.length};
    MTLAccelerationStructureSizes sz = [g_dev accelerationStructureSizesWithDescriptor:td];
    RTS_NEW(g_rts.tlas, [g_dev newAccelerationStructureWithSize:sz.accelerationStructureSize]);
    g_rts.scratch_len = MAX(sz.buildScratchBufferSize, (NSUInteger)(4 << 20)); RTS_NEW(g_rts.scratch, [g_dev newBufferWithLength:g_rts.scratch_len options:MTLResourceStorageModePrivate]);
    if (!g_rts.white) { MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Unorm width:1 height:1 mipmapped:NO]; d.usage = MTLTextureUsageShaderRead; d.storageMode = MTLStorageModeShared;
      g_rts.white = [g_dev newTextureWithDescriptor:d]; uint8_t one = 255; if (g_rts.white) { [g_rts.white replaceRegion:MTLRegionMake2D(0, 0, 1, 1) mipmapLevel:0 withBytes:&one bytesPerRow:1]; res_add(g_rts.white); } }
    if (!ok || !g_rts.at_decode || !g_rts.at_trace || !g_rts.at_overlay || !g_rts.at_blur || !g_rts.at_pick || !g_rts.prev || !g_rts.lseen || !g_rts.llist || !g_rts.plidx_none || !g_rts.tlas || !g_rts.scratch || !g_rts.white || s0 < 0) {
        g_rts.failed = 1; MLOG("【红】光追阴影：管线 / 加速结构建不出 %s（退回原版阴影图）", e.localizedDescription.UTF8String); return 0; }
    // 包里有没有 _rt 变体：一个都没有就不生效（不退回整屏压暗；调试叠加除外） | are there _rt variants in the package: none at all disables the module (no fullscreen darkening fallback; debug overlay excepted)
    { int nrt = 0; for (NSString *f in [NSFileManager.defaultManager contentsOfDirectoryAtPath:g_msl_dir error:nil]) nrt += [f hasSuffix:@"_rt.json"];
      if (!nrt && !g_rts.dbg) { g_rts.failed = 1; MLOG("【红】光追阴影：包里没有 _rt 片元变体（着色器库 %s），不生效，保持原版阴影", g_msl_dir.lastPathComponent.UTF8String ?: "?"); return 0; }
      MLOG("光追阴影：建好（TLAS %lu KiB、暂存 %lu KiB、最多 %d 实例、网格缓存预算 %llu MB、蒙皮导出每帧 ≤ %d、点光 %s ≤ %d 盏（GPU 分簇收集）、软阴影 %s、太阳射线 %s、点光射线 1/%d 分辨率、玩家光 × %.2f、包里 _rt 变体 %d 个）",
          (unsigned long)(sz.accelerationStructureSize >> 10), (unsigned long)(g_rts.scratch_len >> 10), RTS_MAX_INST, (unsigned long long)(g_rts.budget >> 20), RTS_MAX_EXPORT, g_rts.point_on ? "开" : "关", g_rts.point_n, g_rts.soft > 0 ? "开" : "关（DM_RT_SOFT）", g_rts.soft > 0 ? "全分辨率（软阴影）" : g_rts.sun_half ? "半分辨率 + 上采样" : "全分辨率", g_rts.point_div ? g_rts.point_div : 2, g_rts.player_scale, nrt); }
    { char b[160]; MLOG("光追降档：自动 %s，当前第 %d 档（%s；0 = 按设置、1 = 点光 1 盏、2 = 关点光阴影 + 太阳 1/2、3 = 太阳 1/4；按光追通道与整帧 GPU 时间对出图节拍预算判）", g_rts.auto_on ? "开" : g_rts.lvl_pin >= 0 ? "关（DM_RT_LEVEL 钉住）" : "关（DM_RT_AUTO=0）", g_rts.gov.lvl, rts_lvl_desc(b, sizeof b)); }
    if (g_rts.ao_on) MLOG("光追 AO：开（实验性，DM_RT_AO=1）：半分辨率每像素 %d 条半球射线、半径 %.1f、强度 %.2f、5×5 深度感知模糊后上采样，主通道之后乘到画面上；自动降档 1 档起关", g_rts.ao_rays, g_rts.ao_radius, g_rts.ao_strength);
    else MLOG("光追 AO：关（实验性，DM_RT_AO=1 打开）");
    rts_alpha_start();
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
// 网格出缓存：GPU 对象一律 defer_obj_release（等用过它们的提交做完再放、再移出驻留集；不先 res_remove：在途命令缓冲可能还在读）；calloc 出的结构体里的强引用要先置空，free 不会放
// A mesh leaves the cache: every GPU object goes through defer_obj_release (released, and removed from the residency set, after the submissions that used it complete; no res_remove first); strong references in the calloc'd struct are cleared first, free() does not release them
static void rts_mesh_drop(rts_mesh *m) {
    if (!nvn_hm_del(&g_rts.meshes, &m->key)) return;
    for (int i = 0; i < g_rts.nkeep;) if (g_rts.keep[i].m == m) g_rts.keep[i] = g_rts.keep[--g_rts.nkeep]; else i++;   // 保留的实例不能指着已放掉的网格 | kept instances must not point at a released mesh
    defer_obj_release(m->as); defer_obj_release(m->pos); defer_obj_release(m->ib); m->as = nil; m->pos = m->ib = nil;
    for (int i = 0; i < m->nslots; i++) { rts_islot *s = &m->slots[i]; defer_obj_release(s->as); defer_obj_release(s->pos4); defer_obj_release(s->pos3); defer_obj_release(s->lvp); s->as = nil; s->pos4 = s->pos3 = s->lvp = nil; }
    g_rts.cache_bytes -= m->bytes; g_rts.nmesh--; free(m);
}
static void rts_fslot_free(rts_fslot *s) {
    id o[10] = {s->inst, s->k, s->d, s->pk, s->picks, s->bb, s->lcopy, s->lctex, s->ai, s->aargs}; for (int j = 0; j < 10; j++) defer_obj_release(o[j]);
    s->inst = s->k = s->d = s->pk = s->picks = s->bb = s->lcopy = s->ai = s->aargs = nil; s->lctex = nil; s->lcopy_texels = 0;
    defer_obj_release(s->ts); s->ts = nil; s->ts_state = 0;
}
// 整体释放（开关关了 / 模块失败，六审 5.1）：网格、TLAS、暂存、各遮罩、槽、管线、参数表都等在途提交做完再放；1x1 白贴图与全 −1 下标留着（_rt 变体关掉时仍可能绑它们）。再开时 rts_setup 重建
// Release everything (switched off / module failed, sixth review 5.1): meshes, TLAS, scratch, masks, slots, pipelines and argument tables after in-flight submissions; the 1x1 white texture and the all −1 indices stay (_rt variants may still bind them). rts_setup rebuilds on the next switch-on
static void rts_release_all(const char *why) {
    if (!g_rts.nmesh && !g_rts.ps_trace && !g_rts.nslot) return;
    MLOG("光追阴影：释放（%s）：网格 %d 个 %.1f MB、TLAS、暂存 %lu KiB、遮罩、%d 个每帧槽、管线，等 GPU 在途工作做完后放掉", why, g_rts.nmesh, g_rts.cache_bytes / 1048576.0, (unsigned long)(g_rts.scratch_len >> 10), g_rts.nslot);
    while (g_rts.nmesh) { rts_mesh *m = NULL; for (uint32_t i = 0; i < g_rts.meshes.cap && !m; i++) m = g_rts.meshes.vals[i]; if (!m) break; rts_mesh_drop(m); }
    id o[] = {g_rts.badv, g_rts.ao_half, g_rts.ao_tmp, g_rts.ao_full, g_rts.tlas, g_rts.scratch, g_rts.mask, g_rts.mask_raw, g_rts.mask_tmp, g_rts.pmask, g_rts.pmaskf, g_rts.mask_half, g_rts.prev, g_rts.lseen, g_rts.llist, g_rts.ps_overlay, g_rts.at_decode, g_rts.at_trace, g_rts.at_overlay, g_rts.at_blur, g_rts.at_pick};
    for (size_t i = 0; i < sizeof o / sizeof o[0]; i++) if (o[i]) defer_obj_release(o[i]);
    id<MTLComputePipelineState> __strong *ps[13] = {&g_rts.ps_decode, &g_rts.ps_unproj, &g_rts.ps_trace, &g_rts.ps_trace_soft, &g_rts.ps_blur, &g_rts.ps_ptrace, &g_rts.ps_pick, &g_rts.ps_lcopy, &g_rts.ps_lclear, &g_rts.ps_lgather, &g_rts.ps_pup, &g_rts.ps_ao, &g_rts.ps_aoblur};
    for (int i = 0; i < 13; i++) { if (*ps[i]) defer_obj_release(*ps[i]); *ps[i] = nil; }
    pthread_mutex_lock(&g_rts_alpha_mu); atomic_fetch_add(&g_rts_alpha_gen, 1);   // 判定库：换代（在编的编完即丢），已编好的等在途提交做完再放；再开时重编 | judge library: new generation (an in-flight compile is discarded), the ready one after in-flight submissions; recompiled on the next switch-on
    { id<MTLComputePipelineState> __strong *pa[4] = {&g_rts.ps_trace_a, &g_rts.ps_trace_soft_a, &g_rts.ps_ptrace_a, &g_rts.ps_ao_a}; for (int i = 0; i < 4; i++) { if (*pa[i]) defer_obj_release(*pa[i]); *pa[i] = nil; } }
    if (atomic_load(&g_rts_alpha_st) != -2) atomic_store(&g_rts_alpha_st, 0); g_rts.alpha_idx = nil; g_rts.nai = 0; g_rts.ai_cap = -1; g_rts.exp_vs = nil; pthread_mutex_unlock(&g_rts_alpha_mu);
    for (int i = 0; i < g_rts.nslot; i++) rts_fslot_free(&g_rts.slot[i]);
    g_rts.ao_half = g_rts.ao_tmp = g_rts.ao_full = nil; g_rts.ao_done = g_rts.ao_applied = 0; g_rts.badv = nil; g_rts.bad_seen[0] = g_rts.bad_seen[1] = 0;
    g_rts.tlas = nil; g_rts.scratch = nil; g_rts.mask = g_rts.mask_raw = g_rts.mask_tmp = g_rts.pmask = g_rts.pmaskf = g_rts.mask_half = nil; g_rts.prev = g_rts.lseen = g_rts.llist = nil; g_rts.ps_overlay = nil;
    g_rts.at_decode = g_rts.at_trace = g_rts.at_overlay = g_rts.at_blur = g_rts.at_pick = nil; g_rts.scratch_len = 0; g_rts.nslot = 0; g_rts.cur = -1; g_rts.slot_frame = 0;
    g_rts.npend = g_rts.nsub = g_rts.ninst = g_rts.nexp = 0; g_rts.nkeep = 0; g_rts.cur_pos4 = nil; g_rts.cache_bytes = 0; g_rts.rt_frame = g_rts.backoff = 0; g_rts.retry_at = 0;
    g_rts.mask_done = g_rts.overlay_done = g_rts.map_cleared = g_rts.clear_tried = g_rts.pl_done = g_rts.lc_done = 0; g_rts.trace_pending = 0; g_rts.trace_depth = nil;
    for (int i = 0; i < g_rts.nsmap; i++) g_rts.smap[i].t = nil;
    g_rts.nsmap = 0; g_rts.cur_smap = -1; g_rts.main_hdr = nil; g_rts.lbuf = nil; g_rts.lbuf_tid = 0; g_rts.s0_cpu = g_rts.s1_cpu = NULL; g_rts.s0_gpu = g_rts.s1_gpu = 0;
}
// 上一帧的光追完不完整：NULL = 完整（或本帧不需要），否则是原因 | Was the previous frame's ray tracing complete: NULL = complete (or not needed), else the reason
static const char *rts_frame_fail(void) {
    if (!g_rts.main_seen) return "没有主通道";
    if (!g_rts.have_cam) return "没取到相机";
    if (!g_rts.have_sun && g_rts.shadow_draws) return "没取到太阳方向";   // 无太阳关卡（整帧没有阴影绘制）不算 | a level without a sun (no shadow draw at all) does not count
    if (!g_rts.rt_frame || !g_rts.ninst) return NULL;
    if (g_rts.have_sun && !g_rts.mask_done) return "遮罩没算出来";
    if (g_rts.clear_tried && !g_rts.map_cleared) return "阴影图没清回全亮";
    if (g_rts.dbg && g_rts.map_cleared && !g_rts.overlay_done) return "调试叠加没合成进画面";
    return NULL;
}
static void rts_check_dump(void);
// GPU 挂起保护：光追开着时命令缓冲报「Caused GPU Hang」（Mac 上 141 局里见过 5 局，根因未定）。3 秒内的多条算一次事件；第一次 → 直接降到最低档、升档等待 120 秒；
// 第二次 → 本次运行关掉光追（模块失败 → 整体释放，原版阴影图接手）。真机上反复挂起会被系统限制 GPU，宁可保守
// GPU hang protection: command buffers reporting "Caused GPU Hang" while ray tracing is on (seen in 5 of 141 runs on the Mac, root cause open). Errors within 3 s count as one event; the first → straight to the lowest level, 120 s before stepping up;
// the second → ray tracing off for the rest of the run (module failure → full release, the original shadow maps take over). Repeated hangs on a device can get the GPU restricted by the system, so err on the safe side
static void rts_hang_check(uint64_t f) {
    { static int nt = -1; static unsigned long long tf[2]; if (nt < 0) { const char *e = getenv("DM_TEST_RT_HANG"); nt = e ? sscanf(e, "%llu,%llu", &tf[0], &tf[1]) : 0; if (nt < 0) nt = 0; }   // 测试开关：到这些帧号假装报了一次挂起（只读环境变量，不分配） | test switch: pretend a hang was reported at these frames (reads an environment variable only, allocates nothing)
      for (int i = 0; i < nt; i++) if (tf[i] && f == tf[i]) { atomic_fetch_add(&g_gpu_hang_n, 2); MLOG("DM_TEST_RT_HANG：第 %llu 帧假装 GPU 挂起", (unsigned long long)f); } }
    int h = atomic_load(&g_gpu_hang_n); if (h == g_rts.hang_seen) return;
    int nnew = h - g_rts.hang_seen; g_rts.hang_seen = h; double now = rts_now();
    if (g_rts.hang_events && now - g_rts.hang_t < 3.0) { g_rts.hang_t = now; return; }   // 同一次挂起的后续报错 | follow-up errors of the same hang
    g_rts.hang_t = now; g_rts.hang_events++;
    if (g_rts.hang_events == 1) { g_rts.gov.lvl = RTS_LVL_MAX; g_rts.gov.t_change = now; g_rts.gov.hold_up = 120; rts_lvl_update();
        MLOG("【红】光追阴影：GPU 挂起（命令缓冲报 Caused GPU Hang %d 条）→ 直接降到第 %d 档、120 秒内不升；再挂一次本次运行关掉光追", nnew, RTS_LVL_MAX); }
    else { g_rts.failed = 1; MLOG("【红】光追阴影：GPU 又挂起（第 %d 次事件）→ 本次运行关掉光追，退回原版阴影图", g_rts.hang_events); }
}
static void rts_frame_begin(uint64_t f) {
    rts_check_dump(); rts_hang_check(f);
    if (g_rts.badv) { const uint32_t *bv = g_rts.badv.contents; uint32_t b0 = bv[0], b1 = bv[1]; static int nl;   // 坏顶点：数一涨就记（前 8 次），每 600 帧统计行里也有 | bad vertices: logged when the count rises (first 8 times), also in the 600-frame stats
      if ((b0 != g_rts.bad_seen[0] || b1 != g_rts.bad_seen[1]) && nl++ < 8) MLOG("光追阴影：坏顶点（非有限 / 超过 1e7）已收成原点：解码累计 %u 个、反投影累计 %u 个（帧 %llu；反投影的多为该次绘制没用到的顶点——同一顶点缓冲分几次画时各次只写自己引用的，没写的不进三角形，无害）", b0, b1, (unsigned long long)f);
      g_rts.bad_seen[0] = b0; g_rts.bad_seen[1] = b1; }
    if (g_rts.n_frames && g_rts.n_frames % 600 == 0) {
        MLOG("光追阴影：第 %llu 帧 实例 %d（重复 %d、蒙皮 %d、其中绑定姿势 %d、格式不认 %d、非三角列表 %d、镂空 %d）、网格缓存 %d（%.1f / %llu MB，预算淘汰 %llu）、累计建 %llu 个 BLAS（%.1f 万三角形）、refit %llu 次、追踪 %llu 帧、"
             "进 TLAS 的阴影绘制 %llu 次、_rt 变体 %d 个（缺 %d）、_pos 变体 %d 个（缺 %d）、点光 %d 盏（追踪 %llu 帧）、本帧太阳 %s、预渲染遮挡体 %d（保留表 %d、本帧补进 %d）、玩家光 %s（副本 %llu 次）、光追 GPU %.2f ms/帧（%llu 帧计时）；"
             "光追帧 %llu、失败退回 %llu 帧、有绘制没进 TLAS 整帧留原图 %llu 帧、没进 TLAS 的绘制 %llu 次、阴影图清回全亮 %s；换 _rt 变体的绘制 %llu 次、接收面不符没换 %llu 次、主通道无绘制补追 %llu 次；每帧槽 %d（全忙 %llu 次）、键摘要 %.0f µs/帧",
            (unsigned long long)g_rts.n_frames, g_rts.ninst, g_rts.ndup, g_rts.nskin, g_rts.nskin_pose, g_rts.nskip_fmt, g_rts.nskip_prim, g_rts.nalpha, g_rts.nmesh, g_rts.cache_bytes / 1048576.0, (unsigned long long)(g_rts.budget >> 20), (unsigned long long)g_rts.n_evict_budget,
            (unsigned long long)g_rts.n_builds, g_rts.build_tris / 1e4, (unsigned long long)g_rts.n_refit, (unsigned long long)g_rts.n_traced, (unsigned long long)g_rts.n_skipped_draws, g_rts.n_rt_loaded, g_rts.n_rt_missing, g_rts.n_pos_loaded, g_rts.n_pos_missing,
            g_rts.last_npl, (unsigned long long)g_rts.n_ptraced, g_rts.have_sun ? "有" : "无", g_rts.nprepass, g_rts.nkeep, g_rts.nkeep_used, g_rts.ptrack.sure ? "已认出" : "未认出", (unsigned long long)g_rts.n_player_scaled, g_rts_gpu_n ? g_rts_gpu_sum / g_rts_gpu_n : 0, (unsigned long long)g_rts_gpu_n,
            (unsigned long long)g_rts.n_rt_frames, (unsigned long long)g_rts.n_fail_frames, (unsigned long long)g_rts.n_partial_frames, (unsigned long long)g_rts.n_orig_draws, g_rts.map_cleared ? "是" : "否",
            (unsigned long long)g_rts.n_rt_draws, (unsigned long long)g_rts.n_recv_skip, (unsigned long long)g_rts.n_late_trace, g_rts.nslot, (unsigned long long)g_rts.n_slot_busy, g_rts.key_ns / 1e3 / g_rts.n_frames);
        { char mb[400] = ""; size_t mo = 0; for (int i = 0; i < RM_N; i++) if (g_rts.miss[i]) mo += (size_t)snprintf(mb + mo, mo < sizeof mb ? sizeof mb - mo : 0, "%s%s %llu", mo ? "、" : "", k_rm_why[i], (unsigned long long)g_rts.miss[i]);
          MLOG("光追阴影：没进原因 至第 %llu 帧 没进 TLAS 的阴影绘制按原因累计：%s", (unsigned long long)g_rts.n_frames, mo ? mb : "无"); }
        { char wb[400] = ""; size_t o = 0; uint64_t wtot = 0; for (int i = 1; i < RA_N; i++) wtot += g_rts.na_why[i]; for (int i = 1; i < RA_N; i++) if (g_rts.na_why[i]) o += (size_t)snprintf(wb + o, o < sizeof wb ? sizeof wb - o : 0, "%s%s %llu", o ? "、" : "", k_ra_why[i], (unsigned long long)g_rts.na_why[i]);
          MLOG("光追镂空：第 %llu 帧 本帧镂空实例 %d、镂空绘制 %d；累计进光追 %llu 次、留原图 %llu 次（%s）；判定库 %s（%d 个）、_var 变体 %d 个（缺 %d）；导出没编进去的实例 %llu、实例化绘制没进 %llu", (unsigned long long)g_rts.n_frames, g_rts.nalpha_rt, g_rts.nalpha,
              (unsigned long long)g_rts.n_alpha_rt_draws, (unsigned long long)wtot, o ? wb : "无",
              atomic_load(&g_rts_alpha_st) == 2 ? "可用" : atomic_load(&g_rts_alpha_st) == 1 ? "在编" : atomic_load(&g_rts_alpha_st) == -2 ? "关" : "无", g_rts.alpha_nfs, g_rts.n_var_loaded, g_rts.n_var_missing, (unsigned long long)g_rts.n_exp_lost, (unsigned long long)g_rts.n_instanced); }
        { char b[160]; MLOG("光追降档：第 %llu 帧 当前第 %d 档（%s）%s；光追 GPU 均值 %.2f ms（%llu 帧）、整帧 GPU 均值 %.2f ms；累计降 %llu 次、升 %llu 次", (unsigned long long)g_rts.n_frames, g_rts.gov.lvl,
              g_rts.auto_on ? "自动" : g_rts.lvl_pin >= 0 ? "DM_RT_LEVEL 钉住" : "自动关（DM_RT_AUTO=0）", rts_lvl_desc(b, sizeof b), g_rts.rt_n ? g_rts.rt_sum / g_rts.rt_n : 0, (unsigned long long)g_rts.rt_n, g_rts.fr_n ? g_rts.fr_sum / g_rts.fr_n : 0,
              (unsigned long long)g_rts.n_lvl_down, (unsigned long long)g_rts.n_lvl_up); g_rts.rt_sum = g_rts.fr_sum = 0; g_rts.rt_n = g_rts.fr_n = 0; }
        if (g_rts.ao_on) MLOG("光追 AO：第 %llu 帧 当前%s；累计追 %llu 帧、乘到画面 %llu 帧、算了没乘上 %llu 帧（主通道之后没再挂 HDR）", (unsigned long long)g_rts.n_frames, g_rts.eff.ao ? "开" : "关（自动降档）",
            (unsigned long long)g_rts.n_ao_traced, (unsigned long long)g_rts.n_ao_applied, (unsigned long long)g_rts.n_ao_missed);
        g_rts_gpu_sum = 0; g_rts_gpu_n = 0; g_rts.n_rt_draws = g_rts.n_recv_skip = 0; }
    // 1. 就绪闸门：上一帧（必须紧挨着）完整才走光追；失败退避 2、4 … 512 帧。失败那一帧自己的阴影图没被清，原版阴影还在（六审 5.1） | 1. Readiness gate: ray trace only if the previous (adjacent) frame was complete; failures back off 2, 4 … 512 frames. The failing frame's own shadow map was not cleared (sixth review 5.1)
    const char *why = g_rts.n_frames && g_rts.frame + 1 == f ? rts_frame_fail() : "上一帧没经过光追模块";
    if (g_rts.rt_frame && g_rts.frame + 1 == f) {
        if (g_rts.ninst && g_rts.nuncov && !why) g_rts.n_partial_frames++;
        if (why) { g_rts.backoff = g_rts.backoff ? MIN(g_rts.backoff * 2, 512) : 2; g_rts.retry_at = f + (uint64_t)g_rts.backoff; g_rts.n_fail_frames++;
            if (g_rts.n_fail_frames <= 8 || g_rts.n_fail_frames % 100 == 0) MLOG("光追阴影：回退原版阴影图（帧 %llu %s，当帧阴影图未清、原版阴影保留，%d 帧后再试，第 %llu 次）", (unsigned long long)g_rts.frame, why, g_rts.backoff, (unsigned long long)g_rts.n_fail_frames); }
        else if (g_rts.backoff) { MLOG("光追阴影：恢复（帧 %llu 光追完整，此前退避 %d 帧）", (unsigned long long)g_rts.frame, g_rts.backoff); g_rts.backoff = 0; }
    }
    g_rts.rt_frame = !why && f >= g_rts.retry_at; g_rts.n_rt_frames += g_rts.rt_frame;
    rts_lvl_update();   // 自动降档换了档：从这一帧起生效 | the automatic downgrade changed level: effective from this frame
    if (g_rts.ao_done && !g_rts.ao_applied) g_rts.n_ao_missed++;   // 上一帧算了 AO 却没乘上去（主通道之后没再挂 HDR） | last frame traced AO but never applied it (the HDR was not attached again after the main pass)
    g_rts.ao_done = g_rts.ao_applied = 0;
    if (!g_rts.eff.ao && (g_rts.ao_half || g_rts.ao_tmp || g_rts.ao_full)) {   // 降档关了 AO：三张贴图等在途提交做完放掉，升回来再建 | AO switched off by the downgrade: the three textures are released after in-flight submissions and rebuilt on the way back up
        defer_obj_release(g_rts.ao_half); defer_obj_release(g_rts.ao_tmp); defer_obj_release(g_rts.ao_full); g_rts.ao_half = g_rts.ao_tmp = g_rts.ao_full = nil; MLOG("光追 AO：降档关掉，贴图放掉（升回 0 档再建）"); }
    // 2. 上一帧编进构建的网格转可用；3. 没建的排队作废（源地址只在当帧有效） | 2. Meshes built last frame become usable; 3. unbuilt queue entries are dropped (source addresses are per frame)
    for (int i = 0; i < g_rts.nsub; i++) g_rts.submitted[i]->state = RTS_READY;
    g_rts.nsub = 0;
    for (int i = 0; i < g_rts.npend; i++) g_rts.pending[i]->queued = 0;
    g_rts.npend = 0;
    for (int i = 0; i < g_rts.nsmap; i++) g_rts.smap[i].t = nil;
    g_rts.nsmap = 0; g_rts.cur_smap = -1; g_rts.nuncov = 0; g_rts.clear_tried = g_rts.map_cleared = 0; g_rts.main_hdr = nil;
    g_rts.frame = f; g_rts.n_frames++; g_rts.ninst = g_rts.ndup = g_rts.nskin = g_rts.nskin_pose = g_rts.nskip_fmt = g_rts.nskip_prim = g_rts.nalpha = 0; g_rts.have_cam = g_rts.have_sun = 0; g_rts.main_seen = g_rts.mask_done = g_rts.overlay_done = 0;
    for (int i = 0; i < g_rts.nexp; i++) g_rts.exp_mesh[i]->used = 0;   // 上一帧的蒙皮槽放回 | release the previous frame's skinned slots
    g_rts.nexp = 0; g_rts.cur_pos4 = nil; g_rts.pl_done = g_rts.lc_done = 0; g_rts.trace_pending = 0; g_rts.trace_depth = nil;
    g_rts.nai = 0; g_rts.ai_cap = -1; g_rts.nalpha_rt = 0;
    if (!g_rts.alpha_logged && atomic_load(&g_rts_alpha_st) == 2) { g_rts.alpha_logged = 1; MLOG("光追镂空：判定库编好（%d 个片元着色器，%.0f ms，后台编译），镂空遮挡体按游戏原片元着色器在命中处判定、进光追", g_rts.alpha_nfs, g_rts.alpha_ms); }
    if (g_rts.alpha_logged == 0 && atomic_load(&g_rts_alpha_st) == -1) { g_rts.alpha_logged = -1; MLOG("【红】光追镂空：判定库编不出或包里没有，镂空遮挡体留原阴影图（带镂空遮挡体的帧阴影图不清）"); }
    g_rts.prepass_occ = g_rts.n_frames > 1 && g_rts.shadow_draws == 0; g_rts.shadow_draws = 0; g_rts.nprepass = 0;
    // 4. 总字节预算：超了从最久没用的开始淘汰（这时本帧还没用任何网格，淘汰的只会在早先的提交里在途，defer 等它们做完） | 4. Total byte budget: over it, evict least recently used first (nothing is used by this frame yet; defer waits for earlier submissions)
    for (int n = 0; g_rts.cache_bytes > g_rts.budget && g_rts.nmesh && n < 256; n++) {
        rts_mesh *old = NULL; for (uint32_t i = 0; i < g_rts.meshes.cap; i++) { rts_mesh *m = g_rts.meshes.vals[i]; if (m && (!old || m->last_frame < old->last_frame)) old = m; }
        if (!old) break; rts_mesh_drop(old); g_rts.n_evict_budget++;
    }
    // 600 帧没用过的释放（待建的也算） | release meshes unused for 600 frames (pending ones too)
    if (g_rts.n_frames % 120 == 0 && g_rts.nmesh) { rts_mesh *dead[64]; int nd = 0;
        for (uint32_t i = 0; i < g_rts.meshes.cap && nd < 64; i++) { rts_mesh *m = g_rts.meshes.vals[i]; if (m && f - m->last_frame > 600) dead[nd++] = m; }
        for (int i = 0; i < nd; i++) rts_mesh_drop(dead[i]); }
}
// rp_begin_ex 每开一个渲染通道调一次（六审 5.1）：阴影通道登记它的深度附件、本帧第一次挂它时清没清、清成什么——光追成功后主通道前按这个值把阴影图清回全亮
// Called by rp_begin_ex for every render pass it opens (sixth review 5.1): a shadow pass registers its depth attachment and whether / to what the first pass on it this frame cleared it - after a successful trace the map is cleared back to that value before the main pass
static void rts_note_pass(RPD *pd) {
    if (!rts_on()) return;
    uint64_t f = atomic_load(&g_cur_frame); if (f != g_rts.frame) rts_frame_begin(f);
    g_rts.cur_smap = -1;
    id<MTLTexture> t = pd.depthAttachment.texture;
    if (R.rt.n || !R.rt.depth || R.rt.depth->d.width != R.rt.depth->d.height || !t || t.textureType == MTLTextureTypeCube || t.textureType == MTLTextureTypeCubeArray) return;
    int i = 0; for (; i < g_rts.nsmap; i++) if (g_rts.smap[i].t == t && g_rts.smap[i].level == pd.depthAttachment.level && g_rts.smap[i].slice == pd.depthAttachment.slice) break;
    if (i == g_rts.nsmap) { if (i >= RTS_MAX_SMAP) return; g_rts.nsmap++;
        g_rts.smap[i] = (typeof(g_rts.smap[0])){t, pd.depthAttachment.level, pd.depthAttachment.slice, pd.depthAttachment.clearDepth, pd.depthAttachment.loadAction == MTLLoadActionClear, 0, 0, {0}}; }
    g_rts.cur_smap = i;
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
    // 一律可 refit：蒙皮与镂空（非蒙皮的树叶也走导出）都以它为各实例槽 refit 的源；网格由哪种绘制先建出来不定，不可 refit 的源去 refit 是未定义行为（Mac 实测：偶发该步 160–250 ms/帧）
    // always refittable: both skinned and alpha-tested meshes (non-skinned foliage is exported too) use it as the refit source of their instance slots; which kind of draw creates a mesh first is not fixed, and refitting from a non-refittable source is undefined (measured on the Mac: this step occasionally took 160–250 ms per frame)
    pd.geometryDescriptors = @[gd]; pd.usage = MTLAccelerationStructureUsageRefit;
    return pd;
}
static void rts_scratch_need(NSUInteger need) {
    if (need <= g_rts.scratch_len) return;
    defer_obj_release(g_rts.scratch); g_rts.scratch_len = need * 2; g_rts.scratch = [g_dev newBufferWithLength:g_rts.scratch_len options:MTLResourceStorageModePrivate];
    if (!g_rts.scratch) { g_rts.failed = 1; g_rts.scratch_len = 0; MLOG("【红】光追阴影：暂存缓冲 %lu KiB 建不出（退回原版阴影图）", (unsigned long)(need * 2 >> 10)); } else res_add(g_rts.scratch);
}
// 蒙皮网格的实例槽：首次分配（缓冲 + 与源同大的 BLAS），字节计入网格（受总预算管） | Instance slot of a skinned mesh: allocated on first use (buffers + a BLAS the size of the source), bytes counted into the mesh (under the total budget)
// stride：每顶点 float4 数（蒙皮 1、镂空 1 + 插值输出数）；同一网格的槽步长一致，不一致的这次不导出 | stride: float4s per vertex (skinned 1, alpha-tested 1 + interpolant outputs); slots of one mesh share the stride, a mismatching request is not exported
static rts_islot *rts_slot_take(rts_mesh *m, uint32_t stride) {
    if (m->nslots && m->vstride != stride) return NULL;
    if (m->used < m->nslots) return &m->slots[m->used++];
    if (m->nslots >= RTS_MAX_SLOTS) return NULL;
    rts_islot *s = &m->slots[m->nslots]; memset(s, 0, sizeof *s); m->vstride = stride;
    MTLResourceOptions so = getenv("DM_RT_SHADOW_CHECK") ? MTLResourceStorageModeShared : MTLResourceStorageModePrivate;   // 核对时可回读 | readable back during the check
    s->pos4 = [g_dev newBufferWithLength:(NSUInteger)m->nverts * 16 * stride options:so]; s->pos3 = [g_dev newBufferWithLength:(NSUInteger)m->nverts * 12 options:so];
    s->lvp = [g_dev newBufferWithLength:RTS_SLOTS_MAX * 256 options:MTLResourceStorageModeShared]; s->as = [g_dev newAccelerationStructureWithSize:m->as_size];
    if (!s->pos4 || !s->pos3 || !s->lvp || !s->as) { defer_obj_release(s->as); defer_obj_release(s->pos4); defer_obj_release(s->pos3); defer_obj_release(s->lvp); s->as = nil; s->pos4 = s->pos3 = s->lvp = nil; return NULL; }
    res_add(s->pos4); res_add(s->pos3); res_add(s->lvp); res_add(s->as); m->nslots++; m->used = m->nslots;
    uint64_t b = s->as.allocatedSize + s->pos4.length + s->pos3.length + s->lvp.length; m->bytes += b; g_rts.cache_bytes += b;
    return s;
}
// 网格键（布局 + 全部已校验索引字节 + 所用顶点范围内属性 0 的摘要）并取缓存项：新网格建对象、待建的排队；NULL = 这次绘制没有可用的网格
// Mesh key (layout + digest of every validated index byte + attribute 0 over the vertex range used) and the cache entry: create objects for a new mesh, queue a pending one; NULL = no usable mesh for this draw
static rts_mesh *rts_mesh_get(const p_draw *dw, const p_draw_idx *di, int n, int prim, va_st at, vs_st sm, NVNbufferRange vb, uint64_t *pkey, uint64_t t0, int skin) {
    uint64_t key = *pkey;
    int isz = di ? (di->itype == 1 ? 2 : 4) : 0; uint64_t ibytes = 0; const uint8_t *ib = NULL; uint64_t nv = (uint64_t)n; int64_t vbase = dw ? dw->first : di->base_vertex;
    if (di) { if (__builtin_mul_overflow((uint64_t)n, (uint64_t)isz, &ibytes) || !(ib = rts_mem(di->ibuf, ibytes))) return NULL;
        uint32_t mx = 0; if (isz == 2) { for (int k = 0; k < n; k++) { uint32_t v = ((const uint16_t *)ib)[k]; if (v > mx) mx = v; } } else { for (int k = 0; k < n; k++) { uint32_t v = ((const uint32_t *)ib)[k]; if (v > mx) mx = v; } }
        nv = (uint64_t)mx + 1; key = rts_digest(key, ib, ibytes); }
    // 解码核读第 vbase … vbase+nv−1 个顶点的属性 0：这段要在顶点缓冲声明的范围内，整段也要落在内存池里；乘加都查溢出 | The decode kernel reads attribute 0 of vertices vbase … vbase+nv−1: within the declared vertex buffer and entirely inside a memory pool; every multiply / add is overflow-checked
    uint64_t asz = rts_attr_bytes(at.fmt), vlast, vend;
    if (vbase < 0 || nv > UINT32_MAX || __builtin_mul_overflow((uint64_t)vbase + nv - 1, (uint64_t)sm.stride, &vlast) || __builtin_add_overflow(vlast, (uint64_t)at.offset + asz, &vend) || vend > vb.size) return NULL;
    void *mb; uint64_t mo, vgpu; if (!mtl_gpu_addr(vb.address + at.offset, vend - at.offset, &mb, &mo, &vgpu)) return NULL;
    const uint8_t *vp = rts_mem(vb.address + at.offset, vend - at.offset); if (!vp) return NULL;
    key = rts_vdigest(key, vp, (uint64_t)vbase, nv, sm.stride, asz);
    g_rts.key_ns += bp_now() - t0;
    rts_mesh *m = nvn_hm_get(&g_rts.meshes, &key);
    if (!m || (m->state == RTS_PENDING && !m->queued)) {   // 新网格，或待建而本帧还没排队 | new mesh, or pending and not queued this frame
        if (g_rts.npend >= RTS_BUILDS_PER_FRAME) return NULL;   // 本帧配额用完：下一帧再建 | quota for this frame used up: build next frame
        if (!m) {
            m = calloc(1, sizeof *m); m->key = key; m->nverts = (uint32_t)nv; m->isz = (uint32_t)isz; m->skin = skin;
            if (prim == 5) { m->ib = rts_strip_ib(ib, isz, n, &m->tris); m->isz = 4; if (!m->ib) { free(m); return NULL; } }
            else { m->tris = (uint32_t)(n / 3); if (ib) m->ib = [g_dev newBufferWithBytes:ib length:(NSUInteger)ibytes options:MTLResourceStorageModeShared]; }   // 索引拷一份：游戏的索引内存会被复用 | copy the indices: the game's index memory gets reused
            m->pos = [g_dev newBufferWithLength:(NSUInteger)nv * 12 options:MTLResourceStorageModePrivate];
            MTLAccelerationStructureSizes sz = m->pos ? [g_dev accelerationStructureSizesWithDescriptor:rts_blas_desc(m, m->pos)] : (MTLAccelerationStructureSizes){0};
            m->as_size = sz.accelerationStructureSize; m->rscratch = sz.refitScratchBufferSize; m->as = m->as_size ? [g_dev newAccelerationStructureWithSize:m->as_size] : nil;
            if (!m->pos || !m->as || (ib && !m->ib)) { defer_obj_release(m->pos); defer_obj_release(m->as); defer_obj_release(m->ib); m->as = nil; m->pos = m->ib = nil; free(m); return NULL; }
            rts_scratch_need(MAX(sz.buildScratchBufferSize, sz.refitScratchBufferSize)); if (!g_rts.scratch) { defer_obj_release(m->pos); defer_obj_release(m->as); defer_obj_release(m->ib); m->as = nil; m->pos = m->ib = nil; free(m); return NULL; }
            res_add(m->pos); res_add(m->as); if (m->ib) res_add(m->ib);
            m->bytes = m->as.allocatedSize + m->pos.length + (m->ib ? m->ib.length : 0);
            nvn_hm_put(&g_rts.meshes, &key, m); g_rts.nmesh++; g_rts.cache_bytes += m->bytes;
        }
        g_rts.pending[g_rts.npend] = m; g_rts.pend_src[g_rts.npend] = (typeof(g_rts.pend_src[0])){vgpu, (uint32_t)nv, sm.stride, at.fmt, (uint32_t)vbase}; g_rts.npend++; m->queued = 1;
    }
    *pkey = key;
    return m;
}
// 镂空程序能不能进光追（结果按程序缓存）：判定库可用、片元在库里、顶点有 _var 变体且导出了片元要的全部插值、参数块不超长；返回 RA_* 原因
// Can an alpha-tested program enter the ray tracing (cached per program): library ready, fragment shader in it, vertex shader with a _var variant exporting every interpolant the fragment reads, argument block not too long; returns an RA_* reason
static int rts_alpha_prog(NvProg *pr) {
    int st = atomic_load(&g_rts_alpha_st); if (st != 2) return st == 1 ? RA_COMPILING : RA_NOLIB;
    NvShader *fsh = pr->fs_base ? pr->fs_base : pr->sh[1], *vs = pr->vs_base ? pr->vs_base : pr->sh[0];
    if (!pr->alpha_tried) { pr->alpha_tried = 1; pr->alpha_fs = -1;
        NSArray *e = g_rts.alpha_idx[[[NSString alloc] initWithBytes:fsh->key length:16 encoding:NSASCIIStringEncoding] ?: @""];
        if (e) { pr->alpha_fs = [e[0] intValue]; pr->alpha_locs = [e[1] unsignedIntValue]; }
        NSString *k = [NSString stringWithFormat:@"%.16s_var", vs->key]; NSData *jd = [NSData dataWithContentsOfFile:[NSString stringWithFormat:@"%@/%@.json", g_msl_dir, k]];
        NSDictionary *j = jd ? [NSJSONSerialization JSONObjectWithData:jd options:0 error:nil] : nil; NSArray *locs = [j isKindOfClass:NSDictionary.class] ? j[@"rts_var_locs"] : nil;
        if ([locs isKindOfClass:NSArray.class] && locs.count <= 15 && (pr->vs_var = shader_get(k, 0))) {
            pr->var_stride = 1 + (int)locs.count; pr->var_locmask = 0; memset(pr->var_slot, 0, sizeof pr->var_slot);
            for (NSUInteger i = 0; i < locs.count; i++) { int l = [locs[i] isKindOfClass:NSNumber.class] ? [locs[i] intValue] : -1; if (l >= 0 && l < 16) { pr->var_locmask |= 1u << l; pr->var_slot[l] = (uint8_t)(1 + i); } }
            g_rts.n_var_loaded++; } else g_rts.n_var_missing++;
        if (g_rts.n_alpha_logs++ < 24) MLOG("光追镂空：程序 顶点 %.16s（_var %s，插值 %#x）+ 片元 %.16s（判定库 %s，要插值 %#x）", vs->key, pr->vs_var ? "有" : "缺", pr->var_locmask, fsh->key, pr->alpha_fs >= 0 ? "在" : "不在", pr->alpha_locs);
    }
    if (pr->alpha_fs < 0) return RA_NOFS;
    if (!pr->vs_var) return RA_NOVAR;
    if ((pr->alpha_locs & pr->var_locmask) != pr->alpha_locs) return RA_LOC;
    if ((fsh->arg_len ? fsh->arg_len : fsh->ae.encodedLength) > RTS_AARG) return RA_ARGS;
    return RA_OK;
}
// 补画原版之后抓这次绘制的片元参数块（本通道刚编好的那份）进判定记录；抓不到的记录保持「当不透明」，并记 nuncov（本帧阴影图不清） | after the original re-encode, capture this draw's fragment argument block (the one just encoded in this pass) into its judge record; a record that cannot be captured stays "opaque" and bumps nuncov (the shadow map is not cleared this frame)
static void rts_alpha_capture(NvProg *pr) {
    int i = g_rts.ai_cap; g_rts.ai_cap = -1; if (i < 0 || g_rts.cur < 0) return;
    rts_fslot *fs = &g_rts.slot[g_rts.cur]; rts_ai *a = (rts_ai *)fs->ai.contents + i; NvShader *fsh = pr->sh[1];
    NSUInteger al = fsh ? (fsh->arg_len ? fsh->arg_len : fsh->ae.encodedLength) : 0;
    if (fsh && !fsh->ae) { a->fs = (uint32_t)g_rts.ai_cap_fs; return; }   // 片元不读任何资源：不需要参数块 | the fragment shader reads no resource: no argument block needed
    if (fsh && fsh->ab_buf && fsh->ab_pass == g_pass.serial && al && al <= RTS_AARG) { memcpy((uint8_t *)fs->aargs.contents + (size_t)i * RTS_AARG, (const uint8_t *)fsh->ab_buf.contents + fsh->ab_off, al); a->fs = (uint32_t)g_rts.ai_cap_fs; }
    else { g_rts.na_why[RA_CAP]++; g_rts.nuncov++; }
}
// 一次阴影通道 / 预渲染遮挡体绘制 → 实例。返回 0 = 照原版编码；2 = 蒙皮导出（先由 draw 照原版补画，再换 _pos 顶点变体、关光栅化编一次）。阴影绘制没进 TLAS 就记 nuncov（本帧阴影图不清）
// One shadow-pass / prepass occluder draw → instance. Returns 0 = encode as original; 2 = skinned export (draw re-encodes the original first, then once more with the _pos vertex variant and rasterization off). A shadow draw outside the TLAS bumps nuncov (the shadow map is not cleared this frame)
static int rts_draw_inner(NvProg *pr, const p_draw *dw, const p_draw_idx *di, int prim) {
    if (!rts_on() || !pr || !pr->sh[0]) return 0;
    int alpha = 0;
    uint64_t f = atomic_load(&g_cur_frame); if (f != g_rts.frame) rts_frame_begin(f);
    int shadow = !R.rt.n && R.rt.depth && R.rt.depth->d.width == R.rt.depth->d.height;
    if (shadow) { id<MTLTexture> dt = tex_mtl(R.rt.depth); if (!dt || dt.textureType == MTLTextureTypeCube || dt.textureType == MTLTextureTypeCubeArray) return 0; }   // 点光立方体阴影照旧 | point-light cube shadows unchanged
    const p_bind_buf *c3 = &S.ubo[0][3]; int has_c3 = 0;
    { NvShader *vs = pr->vs_base ? pr->vs_base : pr->sh[0]; for (int i = 0; i < vs->rf.nb; i++) if (vs->rf.b[i].kind == NVN_RB_CBUF && vs->rf.b[i].reg == 3) has_c3 = 1; }
    int prepass = !shadow && !R.rt.n && R.rt.depth && !g_rts.main_seen;
    if (!shadow) {   // 深度预渲染：记相机；无太阳关卡还把它的绘制当遮挡体 | depth prepass: note the camera; in a level without a sun its draws also serve as occluders
        if (prepass && has_c3 && c3->addr && c3->size >= 400) { const uint8_t *b = rts_mem(c3->addr, 400); if (b) { memcpy(g_rts.cam, b + 336, 64); memcpy(g_rts.view, b + 80, 64); g_rts.have_cam = 1;
            if (getenv("DM_RT_POINT_DUMP")) { uint64_t nb = MIN(c3->size, (uint64_t)sizeof g_rts.cb3raw); const uint8_t *bb = rts_mem(c3->addr, nb); if (bb) { memcpy(g_rts.cb3raw, bb, nb); g_rts.cb3n = (int)(nb / 4); } } } }
        if (!prepass || !g_rts.prepass_occ || !g_rts.eff.point_on || !rts_setup()) return 0;
        // 无太阳关卡：预渲染里的蒙皮 / 镂空绘制同样走导出（cb3+336 此时是相机视投影，导出的裁剪坐标按它求逆回世界；镂空判定的像素坐标在主画面上），够不上的只是不进 TLAS（没有阴影图要清）
        // level without a sun: skinned / alpha-tested prepass draws go through the export as well (cb3+336 is the camera view-projection here, so the exported clip positions invert through it; the alpha judge's pixel is on the main picture); those that cannot simply stay out of the TLAS (there is no shadow map to clear)
        if (pr->sh[1] && pr->sh[1]->rf.may_discard) { g_rts.nalpha++; alpha = 1; int why = rts_alpha_prog(pr); if (!why && !di) why = RA_NONIDX; if (why) { g_rts.na_why[why]++; return 0; } }
    } else {
        g_rts.shadow_draws++;
        if (!rts_setup()) return 0;
        if (g_rts.cur_smap >= 0) g_rts.smap[g_rts.cur_smap].used = 1;
        // 镂空遮挡体（第四阶段）：程序能进光追就和蒙皮一样走导出（_var 顶点变体写裁剪坐标 + 插值），实例标非不透明、命中处按原片元着色器判定；不能的只在原阴影图里（本帧不清）
        // alpha-tested occluder (phase 4): a program that can enter the ray tracing goes through the export like skinned units (the _var vertex variant writes clip position + interpolants), the instance is non-opaque and judged by the original fragment shader at each hit; one that cannot stays only in the original shadow map (not cleared this frame)
        if (pr->sh[1] && pr->sh[1]->rf.may_discard) { g_rts.nalpha++; alpha = 1;
            int why = rts_alpha_prog(pr); if (!why && !di) why = RA_NONIDX;
            if (why) { g_rts.na_why[why]++; g_rts.nuncov++; g_rts.n_orig_draws++; g_rts.miss[RM_ALPHA]++; return 0; } }
    }
    int awhy = RA_SLOT;   // 镂空没进时记哪条原因（导出那段会改） | which reason an uncovered alpha-tested draw records (the export block changes it)
    #define RTS_MISS(c) do { if (shadow) { g_rts.nuncov++; g_rts.n_orig_draws++; g_rts.miss[(c)]++; if (alpha) g_rts.na_why[awhy]++; } return 0; } while (0)
    const uint8_t *lvp = has_c3 && c3->addr && c3->size >= 400 ? rts_mem(c3->addr, 400) : NULL; if (lvp) lvp += 336;   // 本次绘制的光源视投影（两级级联各一份） | this draw's light view-projection (one per cascade)
    if (shadow && lvp && R.rt.depth->d.width < 1024 && getenv("DM_RT_SHADOW_CHECK")) { static int nl; float sd[3]; if (nl < 4 && rts_sun_from_lvp((const float *)lvp, sd)) { nl++;   // 核对：小阴影图（如加载时烘焙的 256² 静态阴影）的光源方向 | check: light direction of small shadow maps (e.g. the 256² static map baked at load)
        MLOG("光追阴影：核对 小阴影图 %dx%d 帧 %llu 光源方向 (%.3f %.3f %.3f) 第 4 行 (%.1f %.1f %.3f)", (int)R.rt.depth->d.width, (int)R.rt.depth->d.height, (unsigned long long)f, sd[0], sd[1], sd[2], ((const float *)lvp)[12], ((const float *)lvp)[13], ((const float *)lvp)[14]); } }
    if (shadow && !g_rts.have_sun && lvp && rts_sun_from_lvp((const float *)lvp, g_rts.sun)) g_rts.have_sun = 1;   // 光线方向 = 光源 x、y 不变的方向（见 nvn_rtshadow_util.h） | ray direction = where light x and y stay constant (see nvn_rtshadow_util.h)   // 光线方向 = 光源 x、y 不变的方向（见 nvn_rtshadow_util.h） | ray direction = where light x and y stay constant (see nvn_rtshadow_util.h)
    if (shadow && lvp && g_rts.cur_smap >= 0 && !g_rts.smap[g_rts.cur_smap].has_l) { g_rts.smap[g_rts.cur_smap].has_l = 1; memcpy(g_rts.smap[g_rts.cur_smap].l, lvp, 64); }   // 核对用：这张阴影图第一次绘制的光源视投影 | for the check: the light VP of the first draw into this map
    if (prim != 4 && prim != 5) { g_rts.nskip_prim++; RTS_MISS(RM_PRIM); }   // 三角列表 / 三角带 | triangle lists / strips
    int n = dw ? dw->count : di->count; if (n < 3) { g_rts.miss[RM_SMALL]++; return 0; }   // 不足 3 个顶点的三角绘制什么也不画：不算没进（不挡清图） | a triangle draw with fewer than 3 vertices draws nothing: not counted as uncovered (does not block the clear)
    if (S.nattr < 1 || S.attr[0].stream >= 16) RTS_MISS(RM_ATTR);
    va_st at = S.attr[0]; vs_st sm = S.stream[at.stream]; NVNbufferRange vb = S.vbuf[at.stream];
    if (!sm.stride || sm.divisor || !vb.address || (at.fmt != 0x2b && at.fmt != 0x2e && at.fmt != 0x29 && at.fmt != 0x22)) { g_rts.nskip_fmt++; RTS_MISS(RM_FMT); }
    if (di && di->itype != 1 && di->itype != 2) { g_rts.nskip_fmt++; RTS_MISS(RM_ITYPE); }
    if (di && di->instances > 1) { g_rts.n_instanced++; RTS_MISS(RM_INSTANCED); }   // 实例化绘制：每实例的变换不在 cb0，按单实例进 TLAS 会漏阴影——不进（本帧不清） | instanced draw: per-instance transforms are not in cb0, a single-instance entry would miss shadows - kept out (map not cleared this frame)
    const p_bind_buf *c0 = &S.ubo[0][0]; const uint8_t *m0 = c0->addr && c0->size >= 64 ? rts_mem(c0->addr, 64) : NULL; if (!m0) RTS_MISS(RM_CB0);
    int skin = rt_is_skin();
    // 本帧里布局完全相同的重复绘制（两级级联画同一实例）沿用这一帧已算出的键：摘要每帧每份几何只算一次 | A repeated draw with an identical layout this frame (both cascades draw the same instance) reuses this frame's key: each piece of geometry is digested once per frame
    uint64_t t0 = bp_now();
    uint64_t key = 1469598103934665603ull; uint64_t lay[7] = {vb.address + at.offset, sm.stride, at.fmt, dw ? 0 : di->ibuf, (uint64_t)n, dw ? (uint64_t)dw->first : (uint64_t)(int64_t)di->base_vertex, (uint64_t)prim};
    key = rts_fnv(key, lay, sizeof lay);
    uint64_t lk = key; typeof(g_rts.memo[0]) *mo_ = &g_rts.memo[lk & (RTS_MEMO - 1)]; rts_mesh *m = NULL;
    if (mo_->frame == f && mo_->lay == lk && (m = nvn_hm_get(&g_rts.meshes, &mo_->key)) && (m->state != RTS_PENDING || m->queued)) key = mo_->key;
    else { if (!(m = rts_mesh_get(dw, di, n, prim, at, sm, vb, &key, t0, skin))) RTS_MISS(RM_MESH); *mo_ = (typeof(*mo_)){f, lk, key}; }
    m->last_frame = f;
    if (!g_rts.rt_frame) RTS_MISS(RM_NOTRT);
    if (m->state != RTS_READY) RTS_MISS(RM_NOTREADY);
    if (g_rts.ninst >= RTS_MAX_INST) RTS_MISS(RM_FULL);   // 不是光追帧 / 网格还没可用 / 实例满 | not a ray-traced frame / mesh not usable yet / instances full
    const float *M = (const float *)m0; uint64_t tag = key ^ rts_fnv(1469598103934665603ull, m0, 48);
    for (int i = 0; i < g_rts.ninst; i++) if (g_rts.inst_tag[i] == tag) { g_rts.ndup++; if (shadow) g_rts.n_skipped_draws++; return 0; }   // 两级级联画同一实例：只进一次 | both cascades draw the same instance: enter once
    MTLIndirectAccelerationStructureInstanceDescriptor *d = &g_rts.inst[g_rts.ninst]; memset(d, 0, sizeof *d); g_rts.inst_exp[g_rts.ninst] = -1;
    d->options = MTLAccelerationStructureInstanceOptionOpaque; d->mask = 0xff; d->accelerationStructureID = m->as.gpuResourceID;
    int r = 0;
    if (skin || alpha) { if (skin) g_rts.nskin++;
        // 蒙皮：_pos 变体首次按需加载；镂空：_var 变体（rts_alpha_prog 已载）。有变体、本帧有每帧槽、配额未满 → 取一个实例槽，导出 + refit；否则蒙皮只按绑定姿势（nuncov），镂空不进 TLAS（nuncov）
        // skinned: _pos variant loaded on demand; alpha-tested: the _var variant (loaded by rts_alpha_prog). With the variant, a frame slot and quota left → take an instance slot, export + refit; otherwise skinned falls back to the bind pose (nuncov) and alpha-tested stays out of the TLAS (nuncov)
        if (!alpha && !pr->pos_tried) { pr->pos_tried = 1; NSString *k = [NSString stringWithFormat:@"%.16s_pos", (pr->vs_base ? pr->vs_base : pr->sh[0])->key];
            if ([NSFileManager.defaultManager fileExistsAtPath:[NSString stringWithFormat:@"%@/%@.json", g_msl_dir, k]]) { pr->vs_pos = shader_get(k, 0); if (pr->vs_pos) g_rts.n_pos_loaded++; else g_rts.n_pos_missing++; } else g_rts.n_pos_missing++;
            if (g_rts.n_pos_loaded + g_rts.n_pos_missing == 1) MLOG("光追阴影：蒙皮位置导出变体 %s（%s）", pr->vs_pos ? "已加载" : "缺", k.UTF8String); }
        NvShader *xvs = alpha ? pr->vs_var : pr->vs_pos; uint32_t stride = alpha ? (uint32_t)pr->var_stride : 1;
        int fs = (shadow || prepass) && xvs && lvp && g_rts.nexp < RTS_MAX_EXPORT && (!alpha || g_rts.nai < RTS_MAX_ALPHA) ? rts_frame_slot() : -1;
        rts_islot *s = fs >= 0 ? rts_slot_take(m, stride) : NULL;
        if (s) {
            double a[16], ia[16]; const float *L = (const float *)lvp; for (int i = 0; i < 16; i++) a[i] = L[i]; rts_inv4(a, ia);
            float *u = (float *)((uint8_t *)s->lvp.contents + fs * 256); for (int i = 0; i < 16; i++) u[i] = (float)ia[i]; ((uint32_t *)u)[16] = m->nverts; ((uint32_t *)u)[17] = stride;
            g_rts.exp_mesh[g_rts.nexp] = m; g_rts.exp_slot[g_rts.nexp] = s; g_rts.exp_ok[g_rts.nexp] = 0; g_rts.exp_tr[g_rts.nexp][0] = M[12]; g_rts.exp_tr[g_rts.nexp][1] = M[13]; g_rts.exp_tr[g_rts.nexp][2] = M[14]; g_rts.inst_exp[g_rts.ninst] = (int16_t)g_rts.nexp; g_rts.nexp++; g_rts.cur_pos4 = s->pos4;
            // 非索引绘制：Metal 的 vertex_id 含 vertexStart、base_vertex 恒 0，变体按 vid − bv 写会从第 first 个起写、越出槽缓冲（GPU 越界写）；绑定地址往前挪 first 个顶点，写落在 [0, n) | non-indexed draws: Metal's vertex_id includes vertexStart and base_vertex is 0, so the variant writing at vid − bv would start at index first and run past the slot buffer (an out-of-bounds GPU write); shift the binding back by first vertices so the writes land in [0, n)
            g_rts.cur_pos4_off = di ? 0 : -(int64_t)dw->first * (int64_t)stride * 16; g_rts.exp_vs = xvs;
            g_args_dirty[0] = 1;   // 导出缓冲换了：顶点参数块必须重编（同一通道里同一变体、绑定没变时会复用上一份参数块，Mac 实测同一角色的各部件全写进了第一个部件的槽） | the export buffer changed: the vertex argument block must be re-encoded (with the same variant and unchanged bindings in one pass the previous block is reused; measured on the Mac: every part of a character wrote into the first part's slot)
            d->transformationMatrix.columns[0].x = d->transformationMatrix.columns[1].y = d->transformationMatrix.columns[2].z = 1; d->accelerationStructureID = s->as.gpuResourceID;   // 位置已是世界坐标 | positions are already world space
            if (alpha) {   // 判定记录：参数块等补画原版后由 rts_alpha_capture 抓（之前 fs = ~0 当不透明） | judge record: the argument block is captured by rts_alpha_capture after the original re-encode (until then fs = ~0, opaque)
                rts_ai *ar = (rts_ai *)g_rts.slot[fs].ai.contents + g_rts.nai; memset(ar, 0, sizeof *ar); memcpy(ar->lvp, L, 64);
                float tw = R.rt.depth ? (float)R.rt.depth->d.width : 1, th = R.rt.depth ? (float)R.rt.depth->d.height : 1;
                if (S.has_vp) { ar->vp[0] = S.vp[0][0]; ar->vp[1] = S.vp[0][1]; ar->vp[2] = S.vp[0][2]; ar->vp[3] = S.vp[0][3]; ar->dr[0] = S.dr[0]; ar->dr[1] = S.dr[1]; } else { ar->vp[2] = tw; ar->vp[3] = th; ar->dr[1] = 1; }
                ar->fs = ~0u; ar->stride = stride; ar->args = g_rts.slot[fs].aargs.gpuAddress + (uint64_t)g_rts.nai * RTS_AARG; ar->var = s->pos4.gpuAddress;
                ar->ib = m->ib ? m->ib.gpuAddress : 0; ar->isz = m->ib ? m->isz : 0; ar->locmask = pr->var_locmask; memcpy(ar->slot, pr->var_slot, 16);
                g_rts.ai_cap = g_rts.nai; g_rts.ai_cap_fs = pr->alpha_fs;
                d->options = MTLAccelerationStructureInstanceOptionNonOpaque; d->userID = (uint32_t)g_rts.nai; g_rts.nai++; g_rts.nalpha_rt++; g_rts.n_alpha_rt_draws++;
            }
            r = 2;
        } else if (alpha) { awhy = !(g_rts.nexp < RTS_MAX_EXPORT && g_rts.nai < RTS_MAX_ALPHA) ? RA_QUOTA : fs < 0 ? RA_FSLOT : RA_MSLOT; RTS_MISS(RM_ALPHA); }
        else if (prepass) return 0;   // 预渲染：没导出就不进（绑定姿势会挡错地方） | prepass: no export means no entry (the bind pose would block the wrong places)
        else { g_rts.nskin_pose++; if (shadow) { g_rts.nuncov++; g_rts.miss[RM_POSE]++; } }
    }
    if (r != 2) for (int c = 0; c < 4; c++) { d->transformationMatrix.columns[c].x = M[c * 4 + 0]; d->transformationMatrix.columns[c].y = M[c * 4 + 1]; d->transformationMatrix.columns[c].z = M[c * 4 + 2]; }   // 行向量 3x4 的第 c 行 = 列主序的第 c 列 | row c of the row-vector 3x4 = column c in column-major
    g_rts.inst_tag[g_rts.ninst] = tag; g_rts.inst_mesh[g_rts.ninst] = m; g_rts.ninst++;
    if (prepass && r != 2) {   // 无太阳关卡的静态遮挡体：记进保留表（满了顶掉最久没见的） | static occluder in a level without a sun: record in the keep table (when full, replace the one unseen longest)
        int k = 0; for (; k < g_rts.nkeep; k++) if (g_rts.keep[k].tag == tag) break;
        if (k == g_rts.nkeep) { if (g_rts.nkeep < RTS_KEEP) g_rts.nkeep++; else { k = 0; for (int j = 1; j < RTS_KEEP; j++) if (g_rts.keep[j].last < g_rts.keep[k].last) k = j; } }
        g_rts.keep[k].tag = tag; g_rts.keep[k].m = m; g_rts.keep[k].d = *d; g_rts.keep[k].last = g_rts.keep[k].seen = f; }
    if (shadow) g_rts.n_skipped_draws++; else g_rts.nprepass++;
    return r;
    #undef RTS_MISS
}
static int g_rts_plain;   // draw 在为蒙皮导出补画原版阴影：本次按原版处理 | draw is re-encoding the original shadow for a skinned export: handle as original
static int rts_keep_raster(void) { return 1; }   // 阴影图一律照原版画（六审 5.1）；成功时主通道前再清 | shadow maps are always drawn as original (sixth review 5.1); cleared before the main pass on success
static int rts_draw(NvProg *pr, const p_draw *dw, const p_draw_idx *di, int prim) {
    int r;
    if (g_rts_plain) r = 0;
    else if (g_draw_reentry) r = g_rts.last_r;   // draw 因资源版本变化重来（同一次绘制、状态不变）：同一答案 | draw retried after a resource-version change: same answer
    else g_rts.last_r = r = rts_draw_inner(pr, dw, di, prim);
    if (pr && pr->sh[0]) {   // 顶点着色器：导出绘制用 _pos 变体，其余用原版 | vertex shader: the export draw uses the _pos variant, everything else the original
        if (!pr->vs_base) pr->vs_base = pr->sh[0];
        NvShader *want = r == 2 && g_rts.exp_vs ? g_rts.exp_vs : pr->vs_base;
        if (pr->sh[0] != want) { pr->sh[0] = want; g_pso_dirty = 1; } }
    uint8_t nr = r == 2; if (nr != g_pso_noraster) { g_pso_noraster = nr; g_pso_dirty = 1; }
    return r;
}
// 补画原版之后把导出状态（_pos 顶点、关光栅化）恢复给外层这次绘制 | after re-encoding the original, restore the export state (_pos vertex shader, no rasterization) for the outer draw
static void rts_export_state(NvProg *pr, int on) {
    if (!pr || !pr->vs_base) return;
    if (on && g_rts.ai_cap >= 0) rts_alpha_capture(pr);   // 补画原版刚编好它的片元参数块 | the original re-encode has just encoded its fragment argument block
    NvShader *want = on && g_rts.exp_vs ? g_rts.exp_vs : pr->vs_base;
    if (pr->sh[0] != want) { pr->sh[0] = want; g_pso_dirty = 1; } uint8_t nr = on != 0; if (nr != g_pso_noraster) { g_pso_noraster = nr; g_pso_dirty = 1; }
    if (on) g_args_dirty[0] = 1;   // 导出绘制就要编了：补画原版刚把顶点参数块标成干净，这里重新标脏，变体必须按本次的导出缓冲重编 | the export draw is about to be encoded: the original re-encode just marked the vertex arguments clean, so mark them dirty again; the variant must be re-encoded with this draw's export buffer
}
static void rts_export_done(void) { if (g_rts.nexp > 0) g_rts.exp_ok[g_rts.nexp - 1] = 1; }   // draw 真把导出绘制编进去了（关光栅化的那次） | draw actually encoded the export draw (the one with rasterization off)
static uint64_t rts_pos_gpu(void) { return g_rts.cur_pos4 ? g_rts.cur_pos4.gpuAddress + (uint64_t)g_rts.cur_pos4_off : 0; }
static id<MTLBuffer> rts_pos_buf(void) { return g_rts.cur_pos4; }
// 选灯结果回读：取 GPU 已做完、且最新的那份（槽环 fence 保证），只给玩家光识别与日志 | Pick results: read the newest one the GPU has finished (guaranteed by the slot fences), only for player-light recognition and logging
static void rts_picks_readback(void) {
    uint64_t done = g_done; const rts_picks *o = NULL; uint64_t best = 0;
    for (int i = 0; i < g_rts.nslot; i++) { rts_fslot *s = &g_rts.slot[i]; if (i != g_rts.cur && s->picked && s->fence <= done && s->frame > best && s->picks) { best = s->frame; o = s->picks.contents; } }
    if (!o) return; g_rts.last_npl = o->n;
    if (o->cand_rec >= 0 && g_rts.player_scale < 1) { float pos[3] = {o->p0[0] + o->cand_off[0], o->p0[1] + o->cand_off[1], o->p0[2] + o->cand_off[2]}; double p0[3] = {o->p0[0], o->p0[1], o->p0[2]};
        int sure = rts_player_feed(&g_rts.ptrack, o->cand_rec, pos, p0);
        if (sure && !g_rts.ptrack_logged) { g_rts.ptrack_logged = 1; MLOG("光追点光：玩家随身光 = 离视线最近的点光（相对视线起点偏移 (%.1f, %.1f, %.1f)，%d 帧不变、相机走了 %.0f）；派生副本里颜色 × %.2f（DM_RT_PLAYER_LIGHT），游戏原记录不动", g_rts.ptrack.off[0], g_rts.ptrack.off[1], g_rts.ptrack.off[2], g_rts.ptrack.frames, g_rts.ptrack.moved, g_rts.player_scale); } }
    // 诊断 DM_RT_POINT_DUMP=<帧号>（到这一帧打一次；调试用，正式路径不读游戏内存）：按分簇表解点光，核对记录布局 | diagnostic DM_RT_POINT_DUMP=<frame> (once at that frame; debug only, the normal path never reads game memory): decode point lights through the cluster tables to check the record layout
    { static int dumped; const char *de = getenv("DM_RT_POINT_DUMP"); if (!dumped && de && atomic_load(&g_cur_frame) >= (uint64_t)atoll(de) && g_rts.lbuf && g_rts.s0_cpu && g_rts.s1_cpu && o->p0[3] > 0) { dumped = 1;
        const float *L = (const float *)((const uint8_t *)g_rts.lbuf.contents + g_rts.lbuf_off); const uint32_t *h = g_rts.s0_cpu, *x = g_rts.s1_cpu;
        uint32_t ncl = (uint32_t)(g_rts.s0_size / 4), nslot = (uint32_t)(g_rts.s1_size / 4), ntex = g_rts.lbuf_texels; static uint8_t seen[1 << 16]; memset(seen, 0, sizeof seen); int nu = 0, nbad = 0, ncl_used = 0;
        int idx[512]; for (uint32_t c = 0; c < ncl; c++) { uint32_t w = h[c], a = w & 63, b = (w >> 6) & 1023, cc = (w >> 16) & 255, d = w >> 24, st = MAX(a, MAX(b, cc)); if (d) ncl_used++;
            for (uint32_t q = 0; q < d; q++) { uint32_t sl = c * 256 + st + q; if (sl >= nslot) { nbad++; continue; } uint32_t t = x[sl]; if (t + 3 >= ntex || t >= (1u << 16)) { nbad++; continue; } if (!seen[t]) { seen[t] = 1; if (nu < 512) idx[nu] = (int)t; nu++; } } }
        MLOG("光追点光·诊断：簇 %u 个（有点光的 %d 个）、槽位 %u、点光（去重）%d 盏、越界 %d；视线起点 (%.0f, %.0f, %.0f)", ncl, ncl_used, nslot, nu, nbad, o->p0[0], o->p0[1], o->p0[2]);
        for (int q = 0; q + 3 < g_rts.cb3n; q += 4) MLOG("  预渲染 cb3[%d] = (%.3f, %.3f, %.3f, %.3f)", q / 4, g_rts.cb3raw[q], g_rts.cb3raw[q + 1], g_rts.cb3raw[q + 2], g_rts.cb3raw[q + 3]);
        for (int q = 0; q < g_rts.ninst && q < 4; q++) { MTLPackedFloat4x3 *tm = &g_rts.inst[q].transformationMatrix; MLOG("  遮挡体实例 %d 平移 (%.1f, %.1f, %.1f)（与点光位置同一世界坐标才对得上）", q, tm->columns[3].x, tm->columns[3].y, tm->columns[3].z); }
        double v[16], iv[16]; for (int i = 0; i < 16; i++) v[i] = g_rts.view[i]; rts_inv4(v, iv);
        for (int q = 0; q < nu && q < 12; q++) { const float *e = L + idx[q] * 4; double w4[4]; for (int r = 0; r < 4; r++) w4[r] = iv[0 * 4 + r] * e[0] + iv[1 * 4 + r] * e[1] + iv[2 * 4 + r] * e[2] + iv[3 * 4 + r];
            MLOG("  texel %d：视空间 (%.1f, %.1f, %.1f) → 世界 (%.1f, %.1f, %.1f) ｜颜色 (%.3g, %.3g, %.3g) 半径 %.3g ｜标志 %d ｜衰减 %.3g", idx[q],
            e[0], e[1], e[2], w4[0] / w4[3], w4[1] / w4[3], w4[2] / w4[3], e[4], e[5], e[6], e[7], *(const int32_t *)&e[11], e[12]); } } }
    { static int nlog; if (o->n && nlog < 3) { nlog++; MLOG("光追点光：GPU 选中 %d 盏（记录 texel %d、%d），首盏位置 (%.0f, %.0f, %.0f)；视线起点 (%.0f, %.0f, %.0f)；玩家光记录 %d", o->n, o->rec[0], o->rec[1], o->pos[0][0], o->pos[0][1], o->pos[0][2], o->p0[0], o->p0[1], o->p0[2], o->player); } }
}
// 光源记录缓冲与分簇表：主通道第一次绘制时片元阶段绑的那份（贴图槽 8 的纹理缓冲、存储缓冲 4 / 5） | light records and cluster tables: the ones bound to the fragment stage at the first main-pass draw (texture slot 8 texture buffer, storage buffers 4 / 5)
static void rts_resolve_lbuf(void) {
    int tid = (int)(S.tex[1][RTS_LIGHT_TBUF_REG] & 0xfffff); g_rts.lbuf = nil;
    if (tid) { void *tv = NULL; tex_fast_rid(tid, &tv); __unsafe_unretained id<MTLTexture> t = (__bridge id<MTLTexture>)tv;
        if (t && t.textureType == MTLTextureTypeTextureBuffer && t.buffer && t.pixelFormat == MTLPixelFormatRGBA32Float) { g_rts.lbuf = t.buffer; g_rts.lbuf_off = t.bufferOffset; g_rts.lbuf_texels = (uint32_t)t.width; } }
    { const p_bind_buf *b0 = &S.ssbo[1][RTS_LIGHT_HDR_REG], *b1 = &S.ssbo[1][RTS_LIGHT_IDX_REG]; void *m; uint64_t mo;
      g_rts.s0_gpu = b0->addr && b0->size && mtl_gpu_addr(b0->addr, b0->size, &m, &mo, &g_rts.s0_gpu) ? g_rts.s0_gpu : 0; g_rts.s0_size = g_rts.s0_gpu ? b0->size : 0;
      g_rts.s1_gpu = b1->addr && b1->size && mtl_gpu_addr(b1->addr, b1->size, &m, &mo, &g_rts.s1_gpu) ? g_rts.s1_gpu : 0; g_rts.s1_size = g_rts.s1_gpu ? b1->size : 0;
      g_rts.s0_cpu = g_rts.s0_gpu ? (const uint32_t *)rts_mem(b0->addr, b0->size) : NULL; g_rts.s1_cpu = g_rts.s1_gpu ? (const uint32_t *)rts_mem(b1->addr, b1->size) : NULL; }
    if (tid != g_rts.lbuf_tid) { g_rts.lbuf_tid = tid; static int n; if (n++ < 3) MLOG("光追点光：光源记录 贴图号 %d、%u texel、%s；分簇表 簇头 %llu 字节、槽位 %llu 字节", tid, g_rts.lbuf ? g_rts.lbuf_texels : 0, g_rts.lbuf ? "RGBA32F" : "不是 RGBA32F 纹理缓冲，点光不开",
        (unsigned long long)g_rts.s0_size, (unsigned long long)g_rts.s1_size); }
}
// _rt 变体的光源记录绑定：玩家光副本本帧生效时换成副本 | light record binding of the _rt variants: the copy when the player-light copy is in effect this frame
static uint64_t rts_lcopy_rid(NvShader *sh, void **tex) {
    if (!g_rts.lc_done || g_rts.cur < 0 || !sh || !strstr(sh->key, "_rt")) return 0;
    id<MTLTexture> t = g_rts.slot[g_rts.cur].lctex; if (!t) return 0; *tex = (__bridge void *)t; return t.gpuResourceID._impl;
}
static id<MTLTexture> rts_pmask_tex(void) { return g_rts.pl_done && g_rts.pmaskf ? g_rts.pmaskf : g_rts.white; }   // 全分辨率（上采样后）；没算出来给 1x1 全亮——_rt 变体按像素坐标读，越界读 1x1 在 Metal 里返回 0，所以全亮贴图只在 pl_done 为 0 时给，此时变体的 idx 全是 −1 不会读它 | full resolution (upsampled); otherwise the 1x1 fully-lit texture - only bound while pl_done is 0, when the variant's indices are all −1 and never read it
static uint64_t rts_pmask_rid(void) { id<MTLTexture> t = rts_pmask_tex(); return t ? t.gpuResourceID._impl : 0; }
static id<MTLBuffer> rts_plidx_buf(void) { return g_rts.pl_done && g_rts.cur >= 0 ? g_rts.slot[g_rts.cur].picks : g_rts.plidx_none; }   // 本帧选灯结果的 rec（偏移 0 的 int4） | this frame's picks' rec (the int4 at offset 0)
static uint64_t rts_plidx_gpu(void) { id<MTLBuffer> b = rts_plidx_buf(); return b ? b.gpuAddress : 0; }
// 本帧登记过、画过阴影的阴影图是不是都能清回游戏自己的全亮值（第一次挂它的那个通道是清除通道） | Can every shadow map registered and drawn this frame be cleared back to the game's own fully-lit value (the first pass on it was a clearing pass)
static int rts_maps_restorable(void) {
    int any = 0; for (int i = 0; i < g_rts.nsmap; i++) if (g_rts.smap[i].used) { if (!g_rts.smap[i].cleared) return 0; any = 1; }
    return any;
}
static int rts_clear_maps(void) {   // 光追成功：主通道前把本帧画过的阴影图清回全亮（原版阴影由遮罩接替） | Ray tracing succeeded: clear the shadow maps drawn this frame back to fully lit before the main pass (the mask takes over)
    for (int i = 0; i < g_rts.nsmap; i++) { typeof(g_rts.smap[0]) *e = &g_rts.smap[i]; if (!e->used) continue;
        RPD *rp = [RPD new]; rp.depthAttachment.texture = e->t; rp.depthAttachment.level = e->level; rp.depthAttachment.slice = e->slice;
        rp.depthAttachment.loadAction = MTLLoadActionClear; rp.depthAttachment.clearDepth = e->cd; rp.depthAttachment.storeAction = MTLStoreActionStore;
        if (e->t.pixelFormat == MTLPixelFormatDepth32Float_Stencil8) { rp.stencilAttachment.texture = e->t; rp.stencilAttachment.level = e->level; rp.stencilAttachment.slice = e->slice; rp.stencilAttachment.loadAction = MTLLoadActionLoad; rp.stencilAttachment.storeAction = MTLStoreActionStore; }
        RENC en = [g_cb renderCommandEncoderWithDescriptor:rp];
        if (!en) { MLOG("【红】光追阴影：清阴影图的编码器建不出（第 %d 张），本帧原版阴影保留", i); return 0; }
        [en barrierAfterQueueStages:MTLStageAll beforeStages:MTLStageAll visibilityOptions:MTL4VisibilityOptionDevice]; m4_end(en); }
    return 1;
}
static void rts_unqueue(void) { for (int i = 0; i < g_rts.npend; i++) g_rts.pending[i]->queued = 0; g_rts.npend = 0; }
static id<MTLTexture> rts_tex(id<MTLTexture> cur, MTLPixelFormat f, NSUInteger w, NSUInteger h, const char *what) {   // 按尺寸（重）建私有可读写贴图；旧的等在途提交做完再放 | (re)create a private read/write texture of this size; the old one is released after in-flight submissions
    if (cur && cur.width == w && cur.height == h && cur.pixelFormat == f) return cur;
    defer_obj_release(cur); MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:f width:w height:h mipmapped:NO]; d.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite; d.storageMode = MTLStorageModePrivate;
    id<MTLTexture> t = [g_dev newTextureWithDescriptor:d]; if (t) { res_add(t); MLOG("光追阴影：%s %lux%lu", what, (unsigned long)w, (unsigned long)h); } else MLOG("【红】光追阴影：%s %lux%lu 建不出", what, (unsigned long)w, (unsigned long)h);
    return t;
}
// 主通道第一次绘制前：建待建的 BLAS（不需要相机 / 太阳 / 深度）；光追帧、条件齐全、取到每帧槽时再做蒙皮 refit、TLAS、太阳 / 点光追踪；本帧阴影绘制全在 TLAS 里、阴影图都能清回去时才清。任何一步不成：原版阴影照常（五审 Q5-02、六审 5.1）
// Before the first main-pass draw: build pending BLAS (needs no camera / sun / depth); in a ray-traced frame with everything present and a frame slot, also refit skinned slots, build the TLAS and trace sun / point lights; clear the shadow maps only when every shadow draw is in the TLAS and they can be restored. Any failure keeps the original shadows (fifth review Q5-02, sixth review 5.1)
// DM_RT_SHADOW_CHECK=<帧号>[:目录]：到这一帧做一次几何核对（见 nvn_rtshadow_msl.h ck_*），GPU 做完后写 4 张 PGM（光追光源深度 / 游戏阴影图 / 原版应有阴影 / 光追遮罩） | DM_RT_SHADOW_CHECK=<frame>[:dir]: one geometry check at that frame (see ck_* in nvn_rtshadow_msl.h); once the GPU is done, 4 PGMs are written (traced light depth / game shadow map / original's expected shadow / ray-traced mask)
static struct { int nx; id<MTLBuffer> xp3[RTS_MAX_EXPORT], xp4[RTS_MAX_EXPORT]; uint32_t xn[RTS_MAX_EXPORT], xs[RTS_MAX_EXPORT]; uint8_t xok[RTS_MAX_EXPORT]; float xtr[RTS_MAX_EXPORT][3]; } g_rckx;
static struct { int state; uint64_t fence; id<MTLBuffer> lb, cb; uint32_t lw, lh, cw, ch; id<MTLComputePipelineState> pl, pc; char dir[512]; } g_rck;
static void rts_check_dump(void) {
    if (g_rck.state != 1 || g_done < g_rck.fence) return; g_rck.state = 2;
    const char *nm[4] = {"ck_light_rt", "ck_light_map", "ck_cam_raster", "ck_cam_rt"};
    for (int k = 0; k < 4; k++) { int light = k < 2; uint32_t w = light ? g_rck.lw : g_rck.cw, h = light ? g_rck.lh : g_rck.ch; const float *v = (light ? g_rck.lb : g_rck.cb).contents;
        char path[640]; snprintf(path, sizeof path, "%s/%s.pgm", g_rck.dir, nm[k]); FILE *fo = fopen(path, "wb"); if (!fo) continue; fprintf(fo, "P5\n%u %u\n255\n", w, h);
        for (uint64_t i = 0; i < (uint64_t)w * h; i++) { float x = v[i * 2 + (k & 1)]; fputc((int)(fminf(fmaxf(x, 0.f), 1.f) * 255.f + 0.5f), fo); } fclose(fo); }
    { const float *z = (const float *)g_rck.cb.contents + (size_t)g_rck.cw * g_rck.ch * 2; char path[640]; snprintf(path, sizeof path, "%s/ck_camz.bin", g_rck.dir); FILE *fo = fopen(path, "wb"); if (fo) { fwrite(z, 8, (size_t)g_rck.cw * g_rck.ch, fo); fclose(fo); } }
    for (int i = 0; i < g_rckx.nx; i++) { const float *p3 = g_rckx.xp3[i].contents, *p4 = g_rckx.xp4[i].contents; if (!p3 || !p4) continue; float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f}; int bad = 0;
        for (uint32_t v = 0; v < g_rckx.xn[i]; v++) for (int c = 0; c < 3; c++) { float x = p3[v * 3 + c]; if (!isfinite(x)) { bad++; continue; } lo[c] = fminf(lo[c], x); hi[c] = fmaxf(hi[c], x); }
        MLOG("光追阴影：核对 导出 %d 编进 %d 平移 (%.1f %.1f %.1f) 顶点 %u 步长 %u 世界包围盒 (%.1f %.1f %.1f)–(%.1f %.1f %.1f) 非有限 %d 首顶点裁剪 (%.3f %.3f %.3f %.3f)", i, g_rckx.xok[i], g_rckx.xtr[i][0], g_rckx.xtr[i][1], g_rckx.xtr[i][2], g_rckx.xn[i], g_rckx.xs[i], lo[0], lo[1], lo[2], hi[0], hi[1], hi[2], bad, p4[0], p4[1], p4[2], p4[3]); }
    MLOG("光追阴影：几何核对已写出（%s，光源 %ux%u、相机 %ux%u）", strrchr(g_rck.dir, '/') ? strrchr(g_rck.dir, '/') + 1 : g_rck.dir, g_rck.lw, g_rck.lh, g_rck.cw, g_rck.ch);
}
static void rts_check(CENC ce, id<MTLTexture> depth, const double *ivp) {   // ck_cam 另写第 2 组（相机追 TLAS 的深度 / 缓冲深度）到 ck_camz.txt 统计 | ck_cam also writes a second pair (camera-traced TLAS depth / buffer depth), summarized into ck_camz.txt
    const char *e = getenv("DM_RT_SHADOW_CHECK"); if (!e || g_rck.state || atomic_load(&g_cur_frame) < (uint64_t)atoll(e) || !g_rts.mask_done) return;
    int si = -1, want = getenv("DM_RT_SHADOW_CHECK_MAP") ? atoi(getenv("DM_RT_SHADOW_CHECK_MAP")) : 0; for (int i = 0; i < g_rts.nsmap; i++) if (g_rts.smap[i].used && g_rts.smap[i].has_l && (si = i) >= want) break; if (si < 0) return;
    typeof(g_rts.smap[0]) *m = &g_rts.smap[si]; const char *c = strchr(e, ':'); snprintf(g_rck.dir, sizeof g_rck.dir, "%s", c ? c + 1 : "/tmp");
    NSError *er = nil; id<MTLLibrary> lib = [g_dev newLibraryWithSource:@(k_rts_msl) options:nil error:&er];
    g_rck.pl = [g_dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"rts_ck_light"] error:&er]; g_rck.pc = [g_dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"rts_ck_cam"] error:&er];
    id<MTLTexture> sv = [m->t newTextureViewWithPixelFormat:m->t.pixelFormat textureType:MTLTextureType2D levels:NSMakeRange(m->level, 1) slices:NSMakeRange(m->slice, 1)];
    if (!g_rck.pl || !g_rck.pc || !sv) { g_rck.state = 3; MLOG("【红】光追阴影：几何核对建不出 %s", er.localizedDescription.UTF8String ?: "?"); return; }
    g_rck.lw = (uint32_t)sv.width; g_rck.lh = (uint32_t)sv.height; g_rck.cw = (uint32_t)depth.width / 2; g_rck.ch = (uint32_t)depth.height / 2;
    g_rck.lb = [g_dev newBufferWithLength:(NSUInteger)g_rck.lw * g_rck.lh * 8 options:MTLResourceStorageModeShared]; g_rck.cb = [g_dev newBufferWithLength:(NSUInteger)g_rck.cw * g_rck.ch * 16 options:MTLResourceStorageModeShared];
    id<MTLBuffer> kb = [g_dev newBufferWithLength:512 options:MTLResourceStorageModeShared]; res_add(sv); res_add(g_rck.lb); res_add(g_rck.cb); res_add(kb);
    float *k = kb.contents; double a[16], ia[16]; for (int i = 0; i < 16; i++) a[i] = m->l[i]; rts_inv4(a, ia);
    for (int i = 0; i < 16; i++) { k[i] = (float)ia[i]; k[16 + i] = m->l[i]; k[32 + i] = (float)ivp[i]; } uint32_t *ku = (uint32_t *)(k + 48); ku[0] = g_rck.lw; ku[1] = g_rck.lh; ku[2] = g_rck.cw; ku[3] = g_rck.ch; k[52] = m->cd < 0.5 ? 1.f : 0.f; for (int i = 0; i < 16; i++) k[56 + i] = g_rts.cam[i];   // 阴影图清成 0 = 反向深度（近 = 1） | map cleared to 0 = reversed depth (near = 1)
    MTL4ArgumentTableDescriptor *ad = [MTL4ArgumentTableDescriptor new]; ad.maxBufferBindCount = 3; ad.maxTextureBindCount = 3; id<MTL4ArgumentTable> at = [g_dev newArgumentTableWithDescriptor:ad error:&er];
    [ce barrierAfterEncoderStages:MTLStageDispatch | MTLStageAccelerationStructure beforeEncoderStages:MTLStageDispatch visibilityOptions:MTL4VisibilityOptionDevice];
    [ce setArgumentTable:at]; enc_c_pso(ce, g_rck.pl); [at setResource:g_rts.tlas.gpuResourceID atBufferIndex:0]; [at setAddress:kb.gpuAddress atIndex:1]; [at setAddress:g_rck.lb.gpuAddress atIndex:2]; [at setTexture:sv.gpuResourceID atIndex:0];
    enc_c_dispatch(ce, MTLSizeMake((g_rck.lw + 7) / 8, (g_rck.lh + 7) / 8, 1), MTLSizeMake(8, 8, 1));
    enc_c_pso(ce, g_rck.pc); [at setAddress:g_rck.cb.gpuAddress atIndex:2]; [at setTexture:depth.gpuResourceID atIndex:0]; [at setTexture:sv.gpuResourceID atIndex:1]; [at setTexture:g_rts.mask.gpuResourceID atIndex:2]; [at setResource:g_rts.tlas.gpuResourceID atBufferIndex:0];
    enc_c_dispatch(ce, MTLSizeMake((g_rck.cw + 7) / 8, (g_rck.ch + 7) / 8, 1), MTLSizeMake(8, 8, 1));
    defer_obj_release(at); defer_obj_release(kb); defer_obj_release(sv); g_rck.fence = g_fence + 1; g_rck.state = 1;
    g_rckx.nx = g_rts.nexp; for (int i = 0; i < g_rts.nexp; i++) { g_rckx.xp3[i] = g_rts.exp_slot[i]->pos3; g_rckx.xp4[i] = g_rts.exp_slot[i]->pos4; g_rckx.xn[i] = g_rts.exp_mesh[i]->nverts; g_rckx.xs[i] = g_rts.exp_mesh[i]->vstride; g_rckx.xok[i] = g_rts.exp_ok[i]; memcpy(g_rckx.xtr[i], g_rts.exp_tr[i], 12); }
    for (int i = 0; i < g_rts.nsmap; i++) { typeof(g_rts.smap[0]) *q = &g_rts.smap[i]; float n = sqrtf(q->l[2] * q->l[2] + q->l[6] * q->l[6] + q->l[10] * q->l[10]);
        MLOG("光追阴影：核对 阴影图 %d：%p %lux%lu 类型 %lu 层 %lu mip %lu 清除 %.2f 用了 %d 有矩阵 %d 方向 (%.3f %.3f %.3f) 第 4 行 (%.1f %.1f %.4f)；本帧太阳 (%.3f %.3f %.3f)", i, (__bridge void *)q->t, (unsigned long)q->t.width, (unsigned long)q->t.height, (unsigned long)q->t.textureType, (unsigned long)q->slice, (unsigned long)q->level, q->cd, q->used, q->has_l,
            n > 0 ? q->l[2] / n : 0, n > 0 ? q->l[6] / n : 0, n > 0 ? q->l[10] / n : 0, q->l[12], q->l[13], q->l[14], g_rts.sun[0], g_rts.sun[1], g_rts.sun[2]); }
    { const float *q = g_rts.cam; MLOG("光追阴影：核对 相机视投影 [%g %g %g %g | %g %g %g %g | %g %g %g %g | %g %g %g %g]", q[0], q[1], q[2], q[3], q[4], q[5], q[6], q[7], q[8], q[9], q[10], q[11], q[12], q[13], q[14], q[15]); }
    { const float *q = g_rts.view; MLOG("光追阴影：核对 视矩阵 [%g %g %g %g | %g %g %g %g | %g %g %g %g | %g %g %g %g]", q[0], q[1], q[2], q[3], q[4], q[5], q[6], q[7], q[8], q[9], q[10], q[11], q[12], q[13], q[14], q[15]); }
    { const float *q = g_rts.smap[si].l; MLOG("光追阴影：核对 阴影图 %d 光源视投影 [%g %g %g %g | %g %g %g %g | %g %g %g %g | %g %g %g %g]；太阳 (%.3f %.3f %.3f)", si, q[0], q[1], q[2], q[3], q[4], q[5], q[6], q[7], q[8], q[9], q[10], q[11], q[12], q[13], q[14], q[15], g_rts.sun[0], g_rts.sun[1], g_rts.sun[2]); }
    MLOG("光追阴影：几何核对编进第 %llu 帧（阴影图 %d：%lux%lu 层 %lu，光源视投影第 3 列 (%.4f %.4f %.4f %.4f)）", (unsigned long long)atomic_load(&g_cur_frame), si, (unsigned long)sv.width, (unsigned long)sv.height, (unsigned long)m->slice, m->l[2], m->l[6], m->l[10], m->l[14]);
}
static void rts_build_and_trace(id<MTLTexture> depth) {
    int trace = g_rts.rt_frame && g_rts.ninst && g_rts.have_cam && depth && (g_rts.have_sun || (g_rts.eff.point_on && g_rts.lbuf && g_rts.s0_gpu && g_rts.s1_gpu));
    if (!trace && !g_rts.npend) return;
    if (trace && g_rts.have_sun && !(g_rts.mask = rts_tex(g_rts.mask, MTLPixelFormatR8Unorm, depth.width, depth.height, "遮罩"))) { g_rts.failed = 1; rts_unqueue(); return; }
    int si = trace ? rts_frame_slot() : -1; if (si < 0) trace = 0;
    if (!trace && !g_rts.npend) return;
    rp_end(); if (g_blit) { enc_b_end(g_blit); g_blit = nil; } ser_gate();
    if (getenv("DM_RT_SPLIT")) { CENC pe = (CENC)pg_compute(); if (pe) { pg_compute_end(pe); enc_c_end(pe); } }   // 排查：空的计时通道，看等待算在谁头上 | debugging: an empty timed pass, to see who absorbs any wait
    CENC ce = (CENC)pg_compute(); pg_tag_rt();   // 走逐通道计时（DM_NVN_PASSGPU 没开时就是普通计算编码器） | through the per-pass timer (a plain compute encoder when DM_NVN_PASSGPU is off)
    if (!ce) { rts_unqueue(); { static int n; if (n++ < 3) MLOG("【红】光追阴影：计算编码器建不出，本帧不建 BLAS、不追踪（原版阴影照常，网格留待建，下次命中重排）"); } return; }
    if (si >= 0 && g_rts.slot[si].ts) { [ce writeTimestampWithGranularity:MTL4TimestampGranularityRelaxed intoHeap:g_rts.slot[si].ts atIndex:0]; g_rts.slot[si].ts_state = 1; }   // 自动降档：通道起点 | automatic downgrade: pass start
    MTL4BufferRange scratch = {g_rts.scratch.gpuAddress, g_rts.scratch.length};
    // 1. 新网格：解码位置 → 建 BLAS（各步之间加编码器内屏障；暂存缓冲共用，所以逐个建） | 1. New meshes: decode positions → build BLAS (intra-encoder barriers between steps; the scratch buffer is shared, so build one at a time)
    if (g_rts.npend) {
        [ce setArgumentTable:g_rts.at_decode]; enc_c_pso(ce, g_rts.ps_decode); [g_rts.at_decode setAddress:g_rts.badv.gpuAddress atIndex:3];
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
    #define RTS_END() do { if (si >= 0 && g_rts.slot[si].ts_state == 1) { [ce writeTimestampWithGranularity:MTL4TimestampGranularityRelaxed intoHeap:g_rts.slot[si].ts atIndex:1]; g_rts.slot[si].ts_state = 2; } pg_compute_end(ce); enc_c_end(ce); args_dirty_all(); g_pso_dirty = g_dss_dirty = 1; } while (0)
    static int split = -1; if (split < 0) split = getenv("DM_RT_SPLIT") != NULL;   // 排查：各步各自一个计算通道，逐通道计时分得开 | debugging: one compute pass per step so the per-pass timer separates them
    #define RTS_SPLIT() do { if (split) { pg_compute_end(ce); enc_c_end(ce); ce = (CENC)pg_compute(); pg_tag_rt(); if (!ce) return; } } while (0)
    if (!trace) { RTS_END(); return; }
    RTS_SPLIT();
    rts_fslot *fs = &g_rts.slot[si];
    // 2. 蒙皮槽：裁剪坐标 → 世界坐标 → 以绑定姿势 BLAS 为源 refit 到槽 BLAS | 2. Skinned slots: clip → world → refit from the bind-pose BLAS into the slot BLAS
    if (g_rts.nexp) {
        [ce setArgumentTable:g_rts.at_decode]; enc_c_pso(ce, g_rts.ps_unproj); [g_rts.at_decode setAddress:g_rts.badv.gpuAddress atIndex:3];
        for (int i = 0; i < g_rts.nexp; i++) { rts_mesh *m = g_rts.exp_mesh[i]; rts_islot *s = g_rts.exp_slot[i]; if (!g_rts.exp_ok[i]) continue;
            [g_rts.at_decode setAddress:s->pos4.gpuAddress atIndex:0]; [g_rts.at_decode setAddress:s->pos3.gpuAddress atIndex:1]; [g_rts.at_decode setAddress:s->lvp.gpuAddress + (uint64_t)si * 256 atIndex:2];
            enc_c_dispatch(ce, MTLSizeMake((m->nverts + 63) / 64, 1, 1), MTLSizeMake(64, 1, 1)); }
        [ce barrierAfterEncoderStages:MTLStageDispatch beforeEncoderStages:MTLStageAccelerationStructure visibilityOptions:MTL4VisibilityOptionDevice];
        RTS_SPLIT();
        // 各 refit 用暂存缓冲里各自的一段，彼此无依赖：不逐个加屏障，整批之后一道（原先每个之后一道，百来个镂空实例时屏障串行成了大头） | each refit gets its own range of the scratch buffer and they are independent: no barrier after each, one after the batch (one after each serialized ~100 alpha instances and dominated the cost)
        NSUInteger tot = 0; for (int i = 0; i < g_rts.nexp; i++) if (g_rts.exp_ok[i]) tot += (g_rts.exp_mesh[i]->rscratch + 255) & ~(NSUInteger)255;
        if (tot) { rts_scratch_need(tot); if (!g_rts.scratch) { RTS_END(); return; } }
        NSUInteger off = 0;
        for (int i = 0; i < g_rts.nexp; i++) { rts_mesh *m = g_rts.exp_mesh[i]; rts_islot *s = g_rts.exp_slot[i]; if (!g_rts.exp_ok[i]) continue;
            MTL4BufferRange sr = {g_rts.scratch.gpuAddress + off, m->rscratch}; off += (m->rscratch + 255) & ~(NSUInteger)255;
            [ce refitAccelerationStructure:m->as descriptor:rts_blas_desc(m, s->pos3) destination:s->as scratchBuffer:sr options:MTLAccelerationStructureRefitOptionVertexData];
            g_rts.n_refit++; }
        [ce barrierAfterEncoderStages:MTLStageAccelerationStructure beforeEncoderStages:MTLStageAccelerationStructure visibilityOptions:MTL4VisibilityOptionDevice];
        scratch = (MTL4BufferRange){g_rts.scratch.gpuAddress, g_rts.scratch.length};
    }
    RTS_SPLIT();
    // 2b. 无太阳关卡：最近 RTS_KEEP_FRAMES 帧见过、本帧没画的静态遮挡体照样进 TLAS（预渲染只画屏幕内的东西，相机一动屏幕边外的墙就不挡点光了） | 2b. level without a sun: static occluders seen in the last RTS_KEEP_FRAMES frames but not drawn this frame enter the TLAS too (the prepass only draws what is on screen; once the camera moves, walls just off screen would stop blocking point lights)
    g_rts.nkeep_used = 0;
    if (g_rts.prepass_occ) for (int k = 0; k < g_rts.nkeep && g_rts.ninst < RTS_MAX_INST; k++) { typeof(g_rts.keep[0]) *e = &g_rts.keep[k];
        if (e->seen == g_rts.frame || g_rts.frame - e->last > RTS_KEEP_FRAMES || e->m->state != RTS_READY) continue;
        g_rts.inst[g_rts.ninst] = e->d; g_rts.inst_tag[g_rts.ninst] = e->tag; g_rts.inst_mesh[g_rts.ninst] = e->m; g_rts.inst_exp[g_rts.ninst] = -1; g_rts.ninst++; g_rts.nkeep_used++; e->m->last_frame = g_rts.frame; }
    // 3. TLAS（只放可用的 BLAS；有一个不可用就本帧不追踪）；实例与常量写进本帧的槽（GPU 已用完它） | 3. TLAS (usable BLAS only; any unusable one skips tracing this frame); instances and constants go into this frame's slot (the GPU is done with it)
    MTLIndirectAccelerationStructureInstanceDescriptor *dst = fs->inst.contents; int n = 0;
    int nlost = 0;
    for (int i = 0; i < g_rts.ninst; i++) { if (g_rts.inst_exp[i] >= 0 && !g_rts.exp_ok[g_rts.inst_exp[i]]) { nlost++; continue; }   // 导出绘制没编进去：槽里是旧 / 未定义的数据，不进 TLAS，本帧不清 | export draw never encoded: the slot holds stale / undefined data, kept out of the TLAS, map not cleared this frame
        if (g_rts.inst_mesh[i]->state == RTS_READY) dst[n++] = g_rts.inst[i]; }
    if (nlost) { g_rts.nuncov += nlost; g_rts.n_exp_lost += (uint64_t)nlost; }
    if (n + nlost != g_rts.ninst || !n) { RTS_END(); return; }
    MTL4InstanceAccelerationStructureDescriptor *td = [MTL4InstanceAccelerationStructureDescriptor new];
    td.instanceDescriptorType = MTLAccelerationStructureInstanceDescriptorTypeIndirect; td.instanceCount = n; td.instanceDescriptorStride = sizeof(MTLIndirectAccelerationStructureInstanceDescriptor);
    td.instanceDescriptorBuffer = (MTL4BufferRange){fs->inst.gpuAddress, (uint64_t)n * sizeof(MTLIndirectAccelerationStructureInstanceDescriptor)};
    [ce buildAccelerationStructure:g_rts.tlas descriptor:td scratchBuffer:scratch];
    [ce barrierAfterEncoderStages:MTLStageAccelerationStructure beforeEncoderStages:MTLStageDispatch visibilityOptions:MTL4VisibilityOptionDevice];
    // 4. 太阳：常量 = 逆视投影（列主序 = 行向量矩阵求逆后原样当列）、太阳、尺寸；软阴影另两趟模糊 | 4. Sun: constants = inverse view-projection (column-major = the inverted row-vector matrix used as columns), sun, size; soft shadows add two blur passes
    RTS_SPLIT();
    double a[16], ia[16]; for (int i = 0; i < 16; i++) a[i] = g_rts.cam[i]; rts_inv4(a, ia);
    static float bias = -1; if (bias < 0) { const char *e = getenv("DM_RT_SHADOW_BIAS"); bias = e ? (float)atof(e) : 0.6f; }
    MTLSize tg = MTLSizeMake((depth.width + 7) / 8, (depth.height + 7) / 8, 1), tt = MTLSizeMake(8, 8, 1);
    // 有镂空实例就用判定库版追踪核（判定记录绑第 4 个缓冲）；它们只在判定库可用时才会进 TLAS | with alpha-tested instances use the judge-library trace kernels (records bound as buffer 4); they only enter the TLAS once the library is ready
    int ua = g_rts.nai > 0 && g_rts.ps_trace_a && g_rts.ps_trace_soft_a && g_rts.ps_ptrace_a;
    if (g_rts.nai && !ua) { RTS_END(); return; }
    if (ua) [g_rts.at_trace setAddress:fs->ai.gpuAddress atIndex:3];
    if (g_rts.have_sun) {
        float *k = fs->k.contents; for (int i = 0; i < 16; i++) k[i] = (float)ia[i];
        k[16] = g_rts.sun[0]; k[17] = g_rts.sun[1]; k[18] = g_rts.sun[2]; k[19] = 0; ((uint32_t *)k)[20] = (uint32_t)depth.width; ((uint32_t *)k)[21] = (uint32_t)depth.height; k[22] = bias; k[23] = g_rts.soft_maxd;
        int soft = g_rts.eff.soft && g_rts.soft > 0;
        NSUInteger sdv = soft ? 1 : (NSUInteger)g_rts.eff.sun_div, hw2 = (depth.width + sdv - 1) / sdv, hh2 = (depth.height + sdv - 1) / sdv;   // 自动降档：1 / 2 / 4 分之一分辨率 | automatic downgrade: 1 / 2 / 4 resolution divisor
        int half = sdv > 1 && (g_rts.mask_half = rts_tex(g_rts.mask_half, MTLPixelFormatR8Unorm, hw2, hh2, "遮罩（降分辨率）"));
        if (!soft) k[23] = half ? (sdv == 4 ? 4.f : 1.f) : 0.f;   // 硬阴影：第 23 位 = 降分辨率标志（1 = 1/2、4 = 1/4；软阴影的同一位是距离归一） | hard shadows: slot 23 = reduced-resolution flag (1 = half, 4 = quarter; the soft trace uses it for distance normalization)
        if (half) { ((uint32_t *)k)[20] = (uint32_t)hw2; ((uint32_t *)k)[21] = (uint32_t)hh2; }
        if (soft) { g_rts.mask_raw = rts_tex(g_rts.mask_raw, MTLPixelFormatRG8Unorm, depth.width, depth.height, "软阴影 raw"); g_rts.mask_tmp = rts_tex(g_rts.mask_tmp, MTLPixelFormatRG8Unorm, depth.width, depth.height, "软阴影 tmp"); soft = g_rts.mask_raw && g_rts.mask_tmp; }
        [ce setArgumentTable:g_rts.at_trace]; enc_c_pso(ce, soft ? (ua ? g_rts.ps_trace_soft_a : g_rts.ps_trace_soft) : (ua ? g_rts.ps_trace_a : g_rts.ps_trace));
        [g_rts.at_trace setTexture:depth.gpuResourceID atIndex:0]; [g_rts.at_trace setTexture:(soft ? g_rts.mask_raw : half ? g_rts.mask_half : g_rts.mask).gpuResourceID atIndex:1];
        [g_rts.at_trace setResource:g_rts.tlas.gpuResourceID atBufferIndex:0]; [g_rts.at_trace setAddress:fs->k.gpuAddress atIndex:1];
        enc_c_dispatch(ce, half ? MTLSizeMake((hw2 + 7) / 8, (hh2 + 7) / 8, 1) : tg, tt);
        if (half) {   // 深度感知上采样回全分辨率（与点光遮罩同一个核） | depth-aware upsampling back to full resolution (the same kernel as the point mask)
            [ce barrierAfterEncoderStages:MTLStageDispatch beforeEncoderStages:MTLStageDispatch visibilityOptions:MTL4VisibilityOptionDevice];
            uint32_t *pu = (uint32_t *)((uint8_t *)fs->pk.contents + 464); pu[0] = (uint32_t)depth.width; pu[1] = (uint32_t)depth.height; pu[2] = (uint32_t)hw2; pu[3] = (uint32_t)hh2;
            [ce setArgumentTable:g_rts.at_blur]; enc_c_pso(ce, g_rts.ps_pup);
            [g_rts.at_blur setTexture:g_rts.mask_half.gpuResourceID atIndex:0]; [g_rts.at_blur setTexture:g_rts.mask.gpuResourceID atIndex:1]; [g_rts.at_blur setTexture:depth.gpuResourceID atIndex:2]; [g_rts.at_blur setAddress:fs->pk.gpuAddress + 464 atIndex:0];
            enc_c_dispatch(ce, tg, tt); }
        if (soft) for (int pass = 0; pass < 2; pass++) {   // 横 raw → tmp，纵 tmp → R8 遮罩 | horizontal raw → tmp, vertical tmp → the R8 mask
            [ce barrierAfterEncoderStages:MTLStageDispatch beforeEncoderStages:MTLStageDispatch visibilityOptions:MTL4VisibilityOptionDevice];
            uint32_t *bb = (uint32_t *)((uint8_t *)fs->bb.contents + pass * 32); bb[0] = (uint32_t)depth.width; bb[1] = (uint32_t)depth.height; ((int32_t *)bb)[2] = pass == 0; ((int32_t *)bb)[3] = pass == 1; ((float *)bb)[4] = g_rts.soft * 12.f; ((float *)bb)[5] = 0;
            [ce setArgumentTable:g_rts.at_blur]; enc_c_pso(ce, g_rts.ps_blur);
            [g_rts.at_blur setTexture:(pass == 0 ? g_rts.mask_raw : g_rts.mask_tmp).gpuResourceID atIndex:0]; [g_rts.at_blur setTexture:(pass == 0 ? g_rts.mask_tmp : g_rts.mask).gpuResourceID atIndex:1]; [g_rts.at_blur setTexture:depth.gpuResourceID atIndex:2];
            [g_rts.at_blur setAddress:fs->bb.gpuAddress + pass * 32 atIndex:0]; enc_c_dispatch(ce, tg, tt); }
        g_rts.mask_done = 1;
    }
    // 4b. 光追 AO（实验性）：半分辨率半球射线 → 5×5 深度感知模糊 → 上采样到全分辨率；主通道之后 rts_ao_apply 乘到 HDR 上。只读深度与 TLAS，不依赖太阳遮罩
    // 4b. ray-traced AO (experimental): half-resolution hemisphere rays → 5×5 depth-aware blur → upsampled to full resolution; rts_ao_apply multiplies it onto the HDR after the main pass. Reads only depth and the TLAS, independent of the sun mask
    if (g_rts.eff.ao) {
        id<MTLComputePipelineState> pa = ua ? g_rts.ps_ao_a : g_rts.ps_ao; NSUInteger aw = (depth.width + 1) / 2, ah = (depth.height + 1) / 2;
        g_rts.ao_half = rts_tex(g_rts.ao_half, MTLPixelFormatR8Unorm, aw, ah, "AO（半分辨率）"); g_rts.ao_tmp = rts_tex(g_rts.ao_tmp, MTLPixelFormatR8Unorm, aw, ah, "AO（模糊）");
        g_rts.ao_full = rts_tex(g_rts.ao_full, MTLPixelFormatR8Unorm, depth.width, depth.height, "AO（上采样）");
        if (pa && g_rts.ps_aoblur && g_rts.ao_half && g_rts.ao_tmp && g_rts.ao_full) {
            RTS_SPLIT();
            float *ak = (float *)((uint8_t *)fs->k.contents + 128); for (int i = 0; i < 16; i++) ak[i] = (float)ia[i];
            uint32_t *aku = (uint32_t *)(ak + 16); aku[0] = (uint32_t)aw; aku[1] = (uint32_t)ah; ak[18] = g_rts.ao_radius; ak[19] = bias * 0.5f; aku[4] = 0; aku[5] = (uint32_t)g_rts.ao_rays; ak[22] = ak[23] = 0;
            [ce setArgumentTable:g_rts.at_trace]; enc_c_pso(ce, pa);
            [g_rts.at_trace setTexture:depth.gpuResourceID atIndex:0]; [g_rts.at_trace setTexture:g_rts.ao_half.gpuResourceID atIndex:1]; [g_rts.at_trace setResource:g_rts.tlas.gpuResourceID atBufferIndex:0]; [g_rts.at_trace setAddress:fs->k.gpuAddress + 128 atIndex:1];
            enc_c_dispatch(ce, MTLSizeMake((aw + 7) / 8, (ah + 7) / 8, 1), tt);
            [ce barrierAfterEncoderStages:MTLStageDispatch beforeEncoderStages:MTLStageDispatch visibilityOptions:MTL4VisibilityOptionDevice];
            uint32_t *pu = (uint32_t *)((uint8_t *)fs->bb.contents + 64); pu[0] = (uint32_t)depth.width; pu[1] = (uint32_t)depth.height; pu[2] = (uint32_t)aw; pu[3] = (uint32_t)ah;
            [ce setArgumentTable:g_rts.at_blur]; enc_c_pso(ce, g_rts.ps_aoblur);
            [g_rts.at_blur setTexture:g_rts.ao_half.gpuResourceID atIndex:0]; [g_rts.at_blur setTexture:g_rts.ao_tmp.gpuResourceID atIndex:1]; [g_rts.at_blur setTexture:depth.gpuResourceID atIndex:2]; [g_rts.at_blur setAddress:fs->bb.gpuAddress + 64 atIndex:0];
            enc_c_dispatch(ce, MTLSizeMake((aw + 7) / 8, (ah + 7) / 8, 1), tt);
            [ce barrierAfterEncoderStages:MTLStageDispatch beforeEncoderStages:MTLStageDispatch visibilityOptions:MTL4VisibilityOptionDevice];
            enc_c_pso(ce, g_rts.ps_pup);
            [g_rts.at_blur setTexture:g_rts.ao_tmp.gpuResourceID atIndex:0]; [g_rts.at_blur setTexture:g_rts.ao_full.gpuResourceID atIndex:1]; [g_rts.at_blur setTexture:depth.gpuResourceID atIndex:2]; [g_rts.at_blur setAddress:fs->bb.gpuAddress + 64 atIndex:0];
            enc_c_dispatch(ce, tg, tt);
            g_rts.ao_done = 1; g_rts.n_ao_traced++; }
    }
    RTS_SPLIT();
    // 5. 点光：分簇收集（本帧记录）→ 选灯 → 玩家光派生副本 → 半分辨率遮罩 | 5. Point lights: cluster gather (this frame's records) → pick → player-light derived copy → half-resolution mask
    if (g_rts.eff.point_on && g_rts.lbuf && g_rts.s0_gpu && g_rts.s1_gpu && g_rts.lbuf_texels >= 4) {
        rts_picks_readback();   // GPU 已做完的最新一份（不是本帧的槽） | the newest finished one (not this frame's slot)
        NSUInteger pdv = (NSUInteger)(g_rts.eff.point_div ? g_rts.eff.point_div : 2), hw = (depth.width + pdv - 1) / pdv, hh = (depth.height + pdv - 1) / pdv;
        g_rts.pmask = rts_tex(g_rts.pmask, MTLPixelFormatRGBA8Unorm, hw, hh, "点光遮罩");
        g_rts.pmaskf = rts_tex(g_rts.pmaskf, MTLPixelFormatRGBA8Unorm, depth.width, depth.height, "点光遮罩（上采样）");
        uint64_t lgpu = g_rts.lbuf.gpuAddress + g_rts.lbuf_off; uint32_t ntex = g_rts.lbuf_texels, ncl = (uint32_t)(g_rts.s0_size / 4), nslotx = (uint32_t)(g_rts.s1_size / 4);
        int lc = g_rts.ptrack.sure && g_rts.player_scale < 1;
        if (lc && (!fs->lcopy || fs->lcopy_texels < ntex)) {   // 副本按记录长度长大才重建（本槽的；旧的等在途提交做完） | the copy is rebuilt only when the records grow (this slot's; the old one after in-flight submissions)
            defer_obj_release(fs->lctex); defer_obj_release(fs->lcopy); fs->lctex = nil; fs->lcopy = nil; fs->lcopy_texels = 0;
            fs->lcopy = [g_dev newBufferWithLength:(NSUInteger)ntex * 16 options:MTLResourceStorageModePrivate];
            MTLTextureDescriptor *td2 = [MTLTextureDescriptor textureBufferDescriptorWithPixelFormat:MTLPixelFormatRGBA32Float width:ntex resourceOptions:MTLResourceStorageModePrivate usage:MTLTextureUsageShaderRead];
            fs->lctex = fs->lcopy ? [fs->lcopy newTextureWithDescriptor:td2 offset:0 bytesPerRow:(NSUInteger)ntex * 16] : nil;
            if (fs->lcopy) res_add(fs->lcopy); if (fs->lctex) res_add(fs->lctex); if (fs->lcopy && fs->lctex) fs->lcopy_texels = ntex; }
        lc = lc && fs->lcopy && fs->lctex;
        uint8_t *kb = fs->pk.contents;
        float *pk = (float *)kb; for (int i = 0; i < 16; i++) pk[i] = (float)ia[i];
        uint32_t *pku = (uint32_t *)(kb + 64); pku[0] = (uint32_t)g_rts.eff.point_n; pku[1] = (uint32_t)(g_rts.ptrack.sure != 0);
        ((float *)pku)[2] = 0.5f + 0.02f * sqrtf((float)(g_rts.ptrack.off[0] * g_rts.ptrack.off[0] + g_rts.ptrack.off[1] * g_rts.ptrack.off[1] + g_rts.ptrack.off[2] * g_rts.ptrack.off[2])); pku[3] = RTS_LIGHT_CAP;
        float *po = (float *)(kb + 80); po[0] = (float)g_rts.ptrack.off[0]; po[1] = (float)g_rts.ptrack.off[1]; po[2] = (float)g_rts.ptrack.off[2]; po[3] = 0;
        { double v[16], iv[16]; for (int i = 0; i < 16; i++) v[i] = g_rts.view[i]; rts_inv4(v, iv); float *pv = (float *)(kb + 96); for (int i = 0; i < 16; i++) pv[i] = (float)iv[i]; }   // 逆视矩阵：记录位置 视空间 → 世界 | inverse view: record positions view space → world
        float *pt = (float *)(kb + 160); for (int i = 0; i < 16; i++) pt[i] = (float)ia[i]; uint32_t *ptu = (uint32_t *)(kb + 224); ptu[0] = (uint32_t)hw; ptu[1] = (uint32_t)hh; ptu[2] = (uint32_t)depth.width; ptu[3] = (uint32_t)depth.height; ((float *)ptu)[4] = bias;
        uint32_t *lcu = (uint32_t *)(kb + 256); lcu[0] = ntex; ((float *)lcu)[1] = g_rts.player_scale;
        uint32_t *lg = (uint32_t *)(kb + 384); lg[0] = ncl; lg[1] = nslotx; lg[2] = ntex; lg[3] = RTS_LIGHT_CAP;
        [ce barrierAfterEncoderStages:MTLStageDispatch beforeEncoderStages:MTLStageDispatch visibilityOptions:MTL4VisibilityOptionDevice];
        [ce setArgumentTable:g_rts.at_pick];
        enc_c_pso(ce, g_rts.ps_lclear); [g_rts.at_pick setAddress:g_rts.lseen.gpuAddress atIndex:0]; [g_rts.at_pick setAddress:g_rts.llist.gpuAddress atIndex:1];
        enc_c_dispatch(ce, MTLSizeMake(2048 / 64, 1, 1), MTLSizeMake(64, 1, 1));
        [ce barrierAfterEncoderStages:MTLStageDispatch beforeEncoderStages:MTLStageDispatch visibilityOptions:MTL4VisibilityOptionDevice];
        enc_c_pso(ce, g_rts.ps_lgather); [g_rts.at_pick setAddress:g_rts.s0_gpu atIndex:0]; [g_rts.at_pick setAddress:g_rts.s1_gpu atIndex:1]; [g_rts.at_pick setAddress:g_rts.lseen.gpuAddress atIndex:2];
        [g_rts.at_pick setAddress:g_rts.llist.gpuAddress atIndex:3]; [g_rts.at_pick setAddress:fs->pk.gpuAddress + 384 atIndex:4];
        enc_c_dispatch(ce, MTLSizeMake((ncl + 63) / 64, 1, 1), MTLSizeMake(64, 1, 1));
        [ce barrierAfterEncoderStages:MTLStageDispatch beforeEncoderStages:MTLStageDispatch visibilityOptions:MTL4VisibilityOptionDevice];
        enc_c_pso(ce, g_rts.ps_pick); [g_rts.at_pick setAddress:lgpu atIndex:0]; [g_rts.at_pick setAddress:fs->picks.gpuAddress atIndex:1]; [g_rts.at_pick setAddress:fs->pk.gpuAddress atIndex:2];
        [g_rts.at_pick setAddress:g_rts.prev.gpuAddress atIndex:3]; [g_rts.at_pick setAddress:g_rts.llist.gpuAddress atIndex:4];
        enc_c_dispatch(ce, MTLSizeMake(1, 1, 1), MTLSizeMake(1, 1, 1)); fs->picked = 1;
        [ce barrierAfterEncoderStages:MTLStageDispatch beforeEncoderStages:MTLStageDispatch visibilityOptions:MTL4VisibilityOptionDevice];
        if (lc) { enc_c_pso(ce, g_rts.ps_lcopy);
            [g_rts.at_pick setAddress:lgpu atIndex:0]; [g_rts.at_pick setAddress:fs->lcopy.gpuAddress atIndex:1]; [g_rts.at_pick setAddress:fs->picks.gpuAddress atIndex:2]; [g_rts.at_pick setAddress:fs->pk.gpuAddress + 256 atIndex:3];
            enc_c_dispatch(ce, MTLSizeMake((ntex + 63) / 64, 1, 1), MTLSizeMake(64, 1, 1)); g_rts.lc_done = 1; g_rts.n_player_scaled++; }
        if (g_rts.pmask) {
            RTS_SPLIT();
            [ce setArgumentTable:g_rts.at_trace]; enc_c_pso(ce, ua ? g_rts.ps_ptrace_a : g_rts.ps_ptrace);
            [g_rts.at_trace setTexture:depth.gpuResourceID atIndex:0]; [g_rts.at_trace setTexture:g_rts.pmask.gpuResourceID atIndex:1]; [g_rts.at_trace setResource:g_rts.tlas.gpuResourceID atBufferIndex:0];
            [g_rts.at_trace setAddress:fs->pk.gpuAddress + 160 atIndex:1]; [g_rts.at_trace setAddress:fs->picks.gpuAddress atIndex:2];
            enc_c_dispatch(ce, MTLSizeMake((hw + 7) / 8, (hh + 7) / 8, 1), tt);
            if (g_rts.pmaskf) {   // 深度感知上采样到全分辨率（_rt 变体读这张） | depth-aware upsampling to full resolution (the _rt variants read this one)
                RTS_SPLIT();
                [ce barrierAfterEncoderStages:MTLStageDispatch beforeEncoderStages:MTLStageDispatch visibilityOptions:MTL4VisibilityOptionDevice];
                uint32_t *pu = (uint32_t *)(kb + 448); pu[0] = (uint32_t)depth.width; pu[1] = (uint32_t)depth.height; pu[2] = (uint32_t)hw; pu[3] = (uint32_t)hh;
                [ce setArgumentTable:g_rts.at_blur]; enc_c_pso(ce, g_rts.ps_pup);
                [g_rts.at_blur setTexture:g_rts.pmask.gpuResourceID atIndex:0]; [g_rts.at_blur setTexture:g_rts.pmaskf.gpuResourceID atIndex:1]; [g_rts.at_blur setTexture:depth.gpuResourceID atIndex:2]; [g_rts.at_blur setAddress:fs->pk.gpuAddress + 448 atIndex:0];
                enc_c_dispatch(ce, tg, tt); }
            g_rts.pl_done = g_rts.pmaskf ? 1 : 0; g_rts.n_ptraced++; }
    }
    rts_check(ce, depth, ia);
    RTS_END();
    #undef RTS_END
    g_rts.n_traced++;
    // 6. 阴影图清回全亮：太阳遮罩算了、本帧每次阴影绘制都在 TLAS 里、阴影图都能清回原值（调试叠加还要合成做得成） | 6. Clear the shadow maps back to fully lit: sun mask traced, every shadow draw of the frame in the TLAS, every map restorable (and, for the debug overlay, compositing possible)
    if (g_rts.mask_done && !g_rts.nuncov && rts_maps_restorable() && (!g_rts.dbg || (g_rts.main_hdr && g_rts.main_hdr.width == depth.width && g_rts.main_hdr.height == depth.height && rts_overlay_setup(g_rts.main_hdr.pixelFormat)))) {
        g_rts.clear_tried = 1; g_rts.map_cleared = rts_clear_maps(); }
    { static int nl; if (nl++ < 3) MLOG("光追阴影：帧 %llu 实例 %d（蒙皮 %d、其中导出 refit %d）、遮罩 %lux%lu、太阳 %s (%.2f, %.2f, %.2f)、点光遮罩 %s、阴影图 %s", (unsigned long long)g_rts.frame, g_rts.ninst, g_rts.nskin, g_rts.nexp,
        (unsigned long)depth.width, (unsigned long)depth.height, g_rts.have_sun ? "有" : "无", g_rts.sun[0], g_rts.sun[1], g_rts.sun[2], g_rts.pl_done ? "算了" : "没算", g_rts.map_cleared ? "清回全亮（遮罩接替）" : "原版保留（变体取 min）"); }
}
static void rts_overlay(id<MTLTexture> hdr) {   // 调试叠加：HDR × (amb + (1 − amb) · 遮罩)；只在阴影图已清回全亮时做（否则两层影） | debug overlay: HDR × (amb + (1 − amb) · mask); only once the shadow maps were cleared back (else shadows double up)
    if (!g_rts.mask_done || !g_rts.map_cleared || g_rts.cur < 0 || !hdr || hdr.width != g_rts.mask.width || hdr.height != g_rts.mask.height || !rts_overlay_setup(hdr.pixelFormat)) return;
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
// 光追 AO 乘到 HDR 上：HDR × (1 − 强度 + 强度 · AO)，与调试叠加同一条管线（混合：目标 × 源）；主通道之后第一次再挂 HDR 时做（rts_set_rt），先于半透明 / 水面
// ray-traced AO multiplied onto the HDR: HDR × (1 − strength + strength · AO), the same pipeline as the debug overlay (blend: destination × source); done the first time the HDR is attached again after the main pass (rts_set_rt), before transparents / water
static void rts_ao_apply(id<MTLTexture> hdr) {
    if (!g_rts.ao_done || g_rts.ao_applied || g_rts.cur < 0 || !hdr || !g_rts.ao_full || hdr.width != g_rts.ao_full.width || hdr.height != g_rts.ao_full.height || !rts_overlay_setup(hdr.pixelFormat)) return;
    rp_end(); if (g_blit) { enc_b_end(g_blit); g_blit = nil; }
    id<MTLBuffer> dbuf = g_rts.slot[g_rts.cur].d; float *a = (float *)dbuf.contents + 4; a[0] = 1.f - g_rts.ao_strength; a[1] = a[2] = a[3] = 0;   // 本帧槽的 d 缓冲第 16 字节起（0–15 是调试叠加的） | from byte 16 of this frame slot's d buffer (0–15 belong to the debug overlay)
    RPD *rp = [RPD new]; rp.colorAttachments[0].texture = hdr; rp.colorAttachments[0].loadAction = MTLLoadActionLoad; rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    RENC en = [g_cb renderCommandEncoderWithDescriptor:rp]; if (!en) return;
    [en barrierAfterQueueStages:MTLStageAll beforeStages:MTLStageAll visibilityOptions:MTL4VisibilityOptionDevice];
    [en setArgumentTable:g_rts.at_overlay atStages:MTLRenderStageFragment]; [g_rts.at_overlay setTexture:g_rts.ao_full.gpuResourceID atIndex:0]; [g_rts.at_overlay setAddress:dbuf.gpuAddress + 16 atIndex:0];
    [en setViewport:(MTLViewport){0, 0, (double)hdr.width, (double)hdr.height, 0, 1}]; [en setRenderPipelineState:g_rts.ps_overlay];
    [en drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3]; m4_end(en);
    args_dirty_all(); g_pso_dirty = g_dss_dirty = 1; g_rts.ao_applied = 1; g_rts.n_ao_applied++;
}
// _rt 变体的遮罩绑定：本帧太阳遮罩算了就给遮罩（阴影图清没清都对：清了 = min(遮罩, 1)，没清 = min(遮罩, 原采样)），否则 1x1 全亮 | Mask binding of the _rt variants: the mask when the sun mask was traced this frame (correct whether or not the maps were cleared: min(mask, 1) or min(mask, original)), else a 1x1 fully lit texel
static id<MTLTexture> rts_mask_tex(void) { static int lit = -1; if (lit < 0) lit = getenv("DM_RT_MASK_LIT") != NULL; return g_rts.mask_done && g_rts.mask && !lit ? g_rts.mask : g_rts.white; }   // DM_RT_MASK_LIT（对照用）：遮罩恒亮，配合清图得「无阴影」画面 | DM_RT_MASK_LIT (for comparison): the mask is always lit; with the maps cleared this gives the "no shadow" picture
static uint64_t rts_mask_rid(void) { id<MTLTexture> t = rts_mask_tex(); return t ? t.gpuResourceID._impl : 0; }
static int rts_in_main(void) { return R.rt.n >= 1 && R.rt.color[0] && R.rt.color[0]->serial == g_rts.hdr_ser && R.rt.depth && R.rt.depth->serial == g_rts.ds_ser; }
// 片元 _rt 变体：主通道里做深度测试、不混合的绘制，且本帧太阳或点光遮罩算了，用 <键>_rt（首次按需加载）；否则用原版 | Fragment _rt variant: depth-tested, unblended main-pass draws with a sun or point mask this frame use <key>_rt (loaded on demand); otherwise the original
static void rts_select_fs(NvProg *pr) {
    if (!pr || !pr->sh[1]) return;
    if (!pr->fs_base) pr->fs_base = pr->sh[1];
    // 主通道第一次绘制：此时片元阶段绑的光源记录与分簇表就是本帧主通道要读的那份，先建 / 追（编码器还没开或只有清除） | first draw of the main pass: the light records and cluster tables bound now are the ones this frame's main pass reads; build / trace first
    if (g_rts.trace_pending && rts_on() && rts_in_main()) { g_rts.trace_pending = 0; rts_resolve_lbuf(); rts_build_and_trace(g_rts.trace_depth); g_rts.trace_depth = nil; }
    // 接收面（评审 第四节 1）：遮罩按主深度重建，只给主通道里做深度测试、不混合的绘制（主通道不写深度，深度由预渲染写好；混合的是透明 / 水面） | receivers (review 4.1): the mask is reconstructed from the main depth, so only depth-tested, unblended main-pass draws (the main pass does not write depth; blended ones are transparent / water)
    int recv = rts_in_main() && S.ds.dt && !(S.color.blend_en & 1);
    int want = rts_on() && !g_rts.failed && (g_rts.mask_done || g_rts.pl_done) && !g_rts.dbg && recv;
    if (!recv && rts_on() && (g_rts.mask_done || g_rts.pl_done) && pr->fs_rt) g_rts.n_recv_skip++;
    if (want && !pr->rt_tried) { pr->rt_tried = 1;
        NSString *k = [NSString stringWithFormat:@"%.16s_rt", pr->fs_base->key];
        if ([NSFileManager.defaultManager fileExistsAtPath:[NSString stringWithFormat:@"%@/%@.json", g_msl_dir, k]]) { pr->fs_rt = shader_get(k, 1); if (pr->fs_rt) g_rts.n_rt_loaded++; else g_rts.n_rt_missing++; }
        else g_rts.n_rt_missing++; }   // 这个着色器不采样级联（没有变体）：照原版 | this shader does not sample the cascades (no variant): original
    NvShader *sel = want && pr->fs_rt ? pr->fs_rt : pr->fs_base;
    if (pr->sh[1] != sel) { pr->sh[1] = sel; g_pso_dirty = 1; }
    if (sel == pr->fs_rt && pr->fs_rt) g_rts.n_rt_draws++;
}
static void rts_set_rt(const p_set_rt *r) {
    if (!rts_on() || g_rts.failed) return;
    uint64_t f = atomic_load(&g_cur_frame); if (f != g_rts.frame) rts_frame_begin(f);
    id<MTLTexture> c0 = r->n > 0 && r->color[0] ? tex_mtl(r->color[0]) : nil, dt = r->depth ? tex_mtl(r->depth) : nil;
    if (!g_rts.main_seen) {
        if (r->n == 1 && c0 && c0.pixelFormat == MTLPixelFormatRG11B10Float && dt && dt.pixelFormat == MTLPixelFormatDepth32Float_Stencil8 && dt.width >= 500 && dt.width != dt.height) {
            g_rts.main_seen = 1; g_rts.hdr_ser = r->color[0]->serial; g_rts.ds_ser = r->depth->serial; g_rts.main_hdr = c0;
            if (g_rts.npend || g_rts.ninst) { g_rts.trace_pending = 1; g_rts.trace_depth = dt; } }   // 在主通道第一次绘制前做（rts_select_fs） | done before the first draw of the main pass (rts_select_fs)
        return;
    }
    if (g_rts.trace_pending) { g_rts.trace_pending = 0; g_rts.n_late_trace++; rts_resolve_lbuf(); rts_build_and_trace(g_rts.trace_depth); g_rts.trace_depth = nil; }   // 主通道一次绘制都没有：照建照追，遮罩本帧用不上 | the main pass had no draw: build / trace anyway, the mask is unused this frame
    if (g_rts.dbg && g_rts.mask_done && g_rts.map_cleared && !g_rts.overlay_done && r->n >= 1 && r->color[0] && r->color[0]->serial == g_rts.hdr_ser) rts_overlay(c0);
    if (!g_rts.dbg && g_rts.ao_done && !g_rts.ao_applied && r->n >= 1 && r->color[0] && r->color[0]->serial == g_rts.hdr_ser) rts_ao_apply(c0);   // 光追 AO：主通道之后第一次再挂 HDR | ray-traced AO: first time the HDR is attached again after the main pass   // 调试叠加：主通道之后第一次再挂 HDR | debug overlay: first time the HDR is attached again after the main pass
}
#endif
