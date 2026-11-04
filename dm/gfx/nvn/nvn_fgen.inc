// ---------- AI 帧生成（实验性；图形页开关 fgen，缺省关；日志前缀「帧生成：」；生效与否由宿主 dm_custom.c dm_fg_setting 决定，这里再加内存退避 fgen_effective）----------
// 帧率档只管真渲染帧率，帧生成只管显示翻倍：游戏节拍器仍按 60 跑，每个真帧之前多上一张插值帧，显示 120（ProMotion 表态与出图线程自取 drawable 跟 mtl_display_fps）。
// 翻译线程：
//   主通道（只挂 RG11B10 不透明 HDR + D32S8 主深度、非方形）本帧第一次绘制时记 cb3 +336（本帧视投影，nvn_rtex.inc 同口径）与 3D 尺寸；
//   原版放大绘制（ps_upsample_p，片元键 52aac1f2306b8fc5）编完后，拷三样进历史环：放大的输入 reg 10（3D 画面，3D 尺寸，插值用）、暗角遮罩 reg 11、
//   放大目标（界面合成之前的画面，出图尺寸，只用来认界面；取第 0 张附件，放大绘制只写它）。按 3D 尺寸插值：Duo 上约 4.5–5 ms，按出图尺寸约 10 ms（评估报告第 7 节）。
//   按相邻两帧视投影算镜头平移（正交相机只平移：整屏一个运动矢量，单位 3D 像素），连同槽号发布给出图线程。
//   镜头跳变（朝向变 / 平移超 64 像素）、尺寸变、中间隔了没有 3D 的帧：这一帧不插（下一帧起重新攒历史）。
// 出图线程（present_now）：本帧条目齐了就在同一个上屏命令缓冲里先编插值（MTL4FXFrameInterpolator：上一帧 / 本帧 3D 画面、常量深度、常量运动矢量，输出 3D 尺寸），
//   再合成一趟：「最终画面与放大目标逐像素不同 = 界面」处取最终画面，其余按原版 ps_upsample_p 同一公式放大插值结果（五点锐化 × 暗角遮罩，常量取本帧放大绘制的常量缓冲；开着空间超分时插值帧不走超分），
//   界面不进插值：不被拖影；插值帧单独提交先上屏，隔约 48% 个出图周期再取真帧的 drawable（nvn_mtl.m present_mid）。
// 前提：上屏的画面 = 放大目标 + 叠上去的界面（同尺寸同格式，逐像素比较认界面）。上屏的可以是另一张贴图（末尾合成趟拷过去再画），记一行照插。
// Frame generation (experimental; Graphics page switch fgen, off by default; whether it is in effect is decided by the host's dm_custom.c dm_fg_setting).
// The frame-rate mode sets only the true render rate, frame generation only doubles what is shown: the game pacer still runs at 60, one interpolated frame is presented before every real frame, showing 120.
// Translation thread: on the main pass's first draw of the frame record cb3 +336 (this frame's view-projection, same as nvn_rtex.inc) and the 3D size; after the original upsample draw
//   is encoded, copy three things into the history ring: the upsample input reg 10 (3D picture, 3D size, interpolated), the vignette mask reg 11 and the upsample target
//   (picture before UI compositing, output size, used only to find the UI; attachment 0, the only one the upsample draw writes). Interpolating at 3D size costs about 4.5-5 ms on Duo versus about 10 ms at output size. Compute the camera translation from two consecutive view-projections
//   (orthographic camera, translation only: one motion vector for the whole screen, in 3D pixels) and publish it with the slot numbers to the present thread.
//   Camera jumps (orientation change / more than 64 pixels), size changes and gaps without 3D: no interpolation for this frame.
// Present thread (present_now): when this frame's entry is complete, encode the interpolation (output at 3D size) into the same present command buffer, then one composite pass:
//   where the final picture differs from the upsample target it is UI (take the final picture), elsewhere upscale the interpolation with the original ps_upsample_p formula
//   (five-tap sharpen x vignette mask, constants from this frame's upsample constant buffer; no spatial scaling on interpolated frames), so the UI is never dragged; the interpolated frame is committed on its own and presented first, the real frame takes its drawable about 48% of a present period later (nvn_mtl.m present_mid).
// Precondition: the presented picture = the upsample target + UI drawn over it (same size and format, UI found by per-pixel comparison). It may be another texture (copied by the final composite pass and drawn on), logged once.
#define FG_RING 4
// 深度 / 运动矢量按 3D 尺寸的 1/FG_MV_DIV 交给插值器：两者都是整屏常量，尺寸不影响结果；插值器内部缓冲按这个尺寸分配，省显存
// （Duo 省电档：插值器内部 32.4 → 12.9 MB，两张贴图 9.3 → 0.6 MB）。插值器按运动贴图自己的像素解读运动矢量，所以矢量同比例缩小（离线核对：平移 8 像素的一对，二分之一 / 四分之一尺寸与全尺寸逐像素一致）
// Depth / motion go to the interpolator at 1/FG_MV_DIV of the 3D size: both are whole-screen constants, so the size does not change the result; the interpolator's internal buffers follow this size and memory drops
// (Duo eco: interpolator internals 32.4 -> 12.9 MB, the two textures 9.3 -> 0.6 MB). The interpolator reads motion vectors in the motion texture's own pixels, so vectors shrink by the same ratio (checked offline: an 8-pixel shifted pair matches full size pixel for pixel at 1/2 and 1/4)
#define FG_MV_DIV 4
// 原型开关 DM_FG_SHIFT=1：不跑插值器，按镜头平移自己合成中间帧（正交相机只平移，世界部分整屏一个位移）。在出图尺寸的放大目标上做，半程位移取整到像素不做重采样；前后两帧平移后对得上 = 世界，取本帧平移的结果；对不上 = 独立运动（单位 / 特效），保留本帧原样
// Prototype switch DM_FG_SHIFT=1: no interpolator, synthesize the middle frame from the camera translation (orthographic camera, translation only: one shift for the whole world). It works on the output-size upsample targets and the half shift is rounded to whole pixels with no resampling; where the two shifted frames agree it is world, take the shifted current frame; where they disagree it moves on its own (units / effects), keep the current frame as is
// 多倍帧生成量测（DM_FG_SCHED，宿主 dm_custom.c 的强定义；报告 多倍帧生成可行性）：生成器 0 MetalFX / 1 平移 / 2 零成本 / 3 外推，倍数 N = 每个真帧配 N−1 张生成帧
// Multi-frame generation measurement (DM_FG_SCHED, strong definitions in the host's dm_custom.c): generator 0 MetalFX / 1 shift / 2 null / 3 extrapolation, multiple N = N-1 generated frames per real frame
__attribute__((weak)) int dm_fg_gen(void) { static int v = -1; if (v < 0) v = getenv("DM_FG_SHIFT") != NULL; return v; }
__attribute__((weak)) int dm_fg_mult(void) { return 2; }
__attribute__((weak)) int dm_fg_need_fi(void) { return !getenv("DM_FG_SHIFT"); }
static int fgen_shift_on(void) { return dm_fg_gen() != 0; }
static void fgen_mvsize(int iw, int ih, int *mw, int *mh) { *mw = iw / FG_MV_DIV > 16 ? iw / FG_MV_DIV : 16; *mh = ih / FG_MV_DIV > 16 ? ih / FG_MV_DIV : 16; }
#ifndef FGEN_TEST
__attribute__((weak)) int dm_fg_setting(void) { return 0; }   // 宿主 dm_custom.c 提供强定义 | the strong definition is in the host's dm_custom.c
#else
int dm_fg_setting(void);   // 单测 t_fgen.m 自己定义（同一编译单元里不能再有弱定义） | defined by the unit test t_fgen.m (no weak definition allowed in the same translation unit)
#endif
#if !DM_NVN_M3   // 旧接口包不支持（mtl_fg_supported 为 0） | not supported by the older-API build (mtl_fg_supported is 0)
static struct {
    int on;                                   // 本帧（翻译线程按帧锁存）是否生效 | in effect this frame (latched per frame on the translation thread)
    uint64_t on_frame, vp_frame, last_frame;
    double vp[16], vpp[16]; int last_vp_ok, have_last, last_slot, iw, ih, dep_iw, dep_ih;   // dep_iw / dep_ih：深度 / 运动贴图按哪个 3D 尺寸建的 | 3D size the depth / motion textures were built for
    id<MTLTexture> h[FG_RING], c3[FG_RING], mk[FG_RING], cv[2], dep, mv, out, comp;  // 放大目标环（出图尺寸）、3D 画面环、遮罩环、转格式、常量深度、运动矢量、插值输出（3D 尺寸）、合成结果
                                                     // upsample-target ring (output size), 3D ring, mask ring, format conversion, constant depth, motion vectors, interpolated output (3D size), composited result
    NSUInteger w, hgt, w3, h3, mw, mh; MTLPixelFormat fmt, fmt3, mfmt, ifmt;   // ifmt：插值器的颜色格式（3D 格式不收时退到 RGBA16Float） | interpolator color format (RGBA16Float if the 3D format is refused)
    uint64_t h_frame[FG_RING];
    uint64_t busy[FG_RING]; uint64_t n_busy;
    uint64_t n_work;   // 过了开关检查、真往下做事的挂钩次数（开关关着必须一直是 0，t_fgen 核对） | hook calls that passed the switch check and did work (must stay 0 while off, checked by t_fgen)   // 槽位占用：读这一槽的出图命令缓冲票号（g_pq_done 到了才算读完，翻译线程才能改写） | slot in use: ticket of the present command buffer reading it (only when g_pq_done reaches it may the translation thread overwrite it)
    struct { uint64_t frame, ser; int slot, prev, ok, mfx; float mvx, mvy; } e[FG_RING];   // mfx：这一帧的真帧是 MetalFX 空间超分放大的 | mfx: this frame's real picture was upscaled by MetalFX spatial
    NvMetalFX *fx; id<MTLTexture> fxin; int fx_res;   // 插值帧的空间超分（真帧走超分时插值帧也走，锐度才对得上）及其输入；输出就是它自己的中间贴图（只放大，遮罩在合成趟乘） | spatial upscaler for interpolated frames and its input; its output is its own intermediate texture (scale only, mask applied in the composite pass)
    float k[FG_RING][8]; id<MTLBuffer> kbuf;   // 每槽放大常量：步长 xy、锐化、0、取样框 x0 y0 x1 y1；kbuf 按历史环槽号分格，受同一套占用保护 | per-slot upsample constants; kbuf is split by ring slot, guarded by the same occupancy
    pthread_mutex_t mu;
    id<MTL4FXFrameInterpolator> fi; id<MTLFence> fence; _Atomic int fi_state;   // 0 无 / 1 建造中 / 2 就绪 / 3 建不出 / 4 建好待配贴图 | 0 none / 1 building / 2 ready / 3 failed / 4 built, textures pending
    id<MTLRenderPipelineState> pso, cvpso, pso2, cppso, shiftpso; id<MTL4ArgumentTable> args; MTLPixelFormat pso_fmt;
    uint64_t last_mid;                                                            // 出图线程：上一次插值的帧 | present thread: frame of the last interpolation
    uint64_t n_mid, n_skip[5], n_jump; double gpu_ms[120]; int ngpu;
    uint64_t n_move; uint32_t mv_max_c;   // 翻译线程写、出图线程读（原子）：真帧里镜头有平移的帧数、最大平移（百分之一 3D 像素） | written by the translation thread, read by the present thread (atomic): real frames whose camera moved, largest shift (hundredths of a 3D pixel)
} g_fg = { .mu = PTHREAD_MUTEX_INITIALIZER, .last_slot = -1 };
enum { FGS_NOENTRY, FGS_NOTOK, FGS_STALE, FGS_SIZE, FGS_NOTREADY };
// 退役：帧生成的对象可能正被出图线程的命令缓冲读着（defer_obj_release 只等翻译线程的提交，管不到出图线程）。
// 先挂在这里，记下当时已入队的出图票号；等 g_pq_done 到了这一票（读过它的出图命令缓冲都做完），再交给 defer_obj_release / defer_obj_unref
// Retirement: frame-generation objects may still be read by a present-thread command buffer (defer_obj_release only waits for translation submissions, not the present thread).
// Park them here with the present ticket enqueued at that moment; once g_pq_done reaches it (every present command buffer that read them is done), hand them to defer_obj_release / defer_obj_unref
static uint64_t g_pq_in;   // 出图入队票号（定义在出图线程那一段） | present enqueue ticket (defined with the present thread)
static NSMutableArray *g_fg_ret; static pthread_mutex_t g_fg_ret_mu = PTHREAD_MUTEX_INITIALIZER; static uint64_t g_fg_ret_n;
static void fgen_retire(id obj, int unref) {
    if (!obj) return;
    uint64_t t = __atomic_load_n(&g_pq_in, __ATOMIC_ACQUIRE);
    pthread_mutex_lock(&g_fg_ret_mu); if (!g_fg_ret) g_fg_ret = [NSMutableArray array]; [g_fg_ret addObject:@[obj, @(t), @(unref)]]; g_fg_ret_n++; pthread_mutex_unlock(&g_fg_ret_mu);
}
static int fgen_reap(void) {   // 翻译线程每帧调；返回还挂着的个数 | called by the translation thread every frame; returns how many are still parked
    pthread_mutex_lock(&g_fg_ret_mu); NSUInteger n = g_fg_ret.count; pthread_mutex_unlock(&g_fg_ret_mu);
    if (!n) return 0;
    pthread_mutex_lock(&g_pq_mu); uint64_t done = g_pq_done; pthread_mutex_unlock(&g_pq_mu);
    NSMutableArray *ready = [NSMutableArray array];
    pthread_mutex_lock(&g_fg_ret_mu);
    for (NSInteger i = (NSInteger)g_fg_ret.count - 1; i >= 0; i--) { NSArray *e = g_fg_ret[(NSUInteger)i]; if ([e[1] unsignedLongLongValue] <= done) { [ready addObject:e]; [g_fg_ret removeObjectAtIndex:(NSUInteger)i]; } }
    int left = (int)g_fg_ret.count; pthread_mutex_unlock(&g_fg_ret_mu);
    for (NSArray *e in ready) { if ([e[2] intValue]) defer_obj_unref(e[0]); else defer_obj_release(e[0]); }
    return left;
}
// 关掉时还显存：历史环、插值输出、插值器全放掉（在途命令缓冲读着的由 defer_* 等它们做完再放）；再开时重建（插值器第二次建约几 ms）
// Give the memory back when switched off: release the rings, outputs and the interpolator (defer_* waits for in-flight command buffers); rebuilt on the next enable (a second build takes a few ms)
static void fgen_free(void) {
    pthread_mutex_lock(&g_fg.mu);
    for (int i = 0; i < FG_RING; i++) {
        if (g_fg.h[i]) fgen_retire(g_fg.h[i], 0);
        if (g_fg.c3[i]) fgen_retire(g_fg.c3[i], 0);
        if (g_fg.mk[i]) fgen_retire(g_fg.mk[i], 0);
        g_fg.h[i] = g_fg.c3[i] = g_fg.mk[i] = nil; }
    id<MTLTexture> one[] = { g_fg.cv[0], g_fg.cv[1], g_fg.dep, g_fg.mv, g_fg.out, g_fg.comp, g_fg.fxin }; for (int i = 0; i < 7; i++) if (one[i]) fgen_retire(one[i], 0);
    g_fg.cv[0] = g_fg.cv[1] = g_fg.dep = g_fg.mv = g_fg.out = g_fg.comp = g_fg.fxin = nil;
    if (g_fg.fx) { if (g_fg.fx_res) for (id<MTLResource> r in g_fg.fx.resources) fgen_retire(r, 0); fgen_retire(g_fg.fx, 1); g_fg.fx = nil; g_fg.fx_res = 0; }
    if (g_fg.fi) fgen_retire(g_fg.fi, 1);
    g_fg.fi = nil; atomic_store(&g_fg.fi_state, 0); g_fg.w = g_fg.hgt = g_fg.w3 = g_fg.h3 = 0; g_fg.last_slot = -1;
    for (int i = 0; i < FG_RING; i++) { g_fg.e[i].frame = 0; g_fg.h_frame[i] = 0; g_fg.busy[i] = 0; }
    pthread_mutex_unlock(&g_fg.mu);
}
// 内存退避（CLAUDE.md §3 判据③：离上限余量 ≥ 500 MB；只有 iOS 读得到余量，读不到 = 0 不退避）：余量跌破线或收到系统内存告警就停插帧、放掉帧生成的对象（Duo 省电档约 116 MB），
// 退避期过了且余量够「线 + 帧生成自己的占用」才重开，不来回切。翻译线程每 60 帧查一次；出图线程和主线程只读 g_fg_memblock
// Memory back-off (CLAUDE.md §3 criterion 3: margin to the limit >= 500 MB; only iOS can read the margin, 0 = unreadable = no back-off): below the line or on a system memory warning, stop interpolating and release frame generation's objects (about 116 MB on Duo eco);
// it restarts only after the back-off period and with margin for "the line + frame generation's own use", so it does not flap. The translation thread checks every 60 frames; the present and main threads only read g_fg_memblock
#define FG_MEM_LINE_MB 500
#define FG_MEM_COST_MB 130
static _Atomic int g_fg_memblock; static uint32_t g_fg_memw_seen; static uint64_t g_fg_mem_until, g_fg_mem_checked;
// 纯函数给单测：blocked 当前是否退避，avail_mb 余量（0 = 读不到），warned 这次有没有新告警，now_s / until_s 退避到什么时候；返回新的 blocked | Pure, for the unit test: returns the new blocked state
static int fgen_mem_step(int blocked, uint64_t avail_mb, int warned, uint64_t now_s, uint64_t *until_s) {
    int low = avail_mb && avail_mb < FG_MEM_LINE_MB;
    if (!blocked) { if (low || warned) { *until_s = now_s + (warned ? 300 : 60); return 1; } return 0; }
    if (warned) { *until_s = now_s + 300; return 1; }
    return now_s < *until_s || (avail_mb && avail_mb < FG_MEM_LINE_MB + FG_MEM_COST_MB);
}
static void fgen_mem_check(uint64_t f) {
    if (f < g_fg_mem_checked + 60 && f >= g_fg_mem_checked) return;
    g_fg_mem_checked = f; uint64_t avail = 0;
#if TARGET_OS_IPHONE
    avail = os_proc_available_memory() >> 20;
#endif
    uint32_t w = atomic_load(&g_mem_warn_n); int warned = w != g_fg_memw_seen, was = atomic_load(&g_fg_memblock); g_fg_memw_seen = w;
    int b = fgen_mem_step(was, avail, warned, now_ns() / 1000000000ull, &g_fg_mem_until);
    if (b != was) { atomic_store(&g_fg_memblock, b); MLOG("帧生成：内存退避 %s（余量 %llu MB，线 %d MB%s）", b ? "开始，停止插帧" : "结束，重开插帧", (unsigned long long)avail, FG_MEM_LINE_MB, warned ? "，系统内存告警" : ""); }
}
// 帧率退避：插值确实在跑（300 个真帧里至少 270 张插值帧，菜单 / 载入 / 跳变不算）且窗口内没有卡顿（单帧超过 100 ms = 载入 / 换关，真机上开局帧 901 就误触发过）却连续三个窗口渲染低于 FG_SLOW_FPS，说明 GPU 撑不住，停插帧回到真 60；退避期 120 秒，每再来一次翻倍，封顶 600 秒
// Frame-rate back-off: when interpolation really runs (at least 270 interpolated frames in 300 real ones; menus / loading / jumps do not count) with no hitch in the window (a frame over 100 ms = loading / level change; it fired falsely at frame 901 during startup on devices) yet three windows in a row render below FG_SLOW_FPS, the GPU cannot keep up: stop and go back to real 60. The period is 120 s, doubling on each repeat, capped at 600 s
#define FG_SLOW_FPS 45
static _Atomic int g_fg_slowblock; static uint64_t g_fg_slow_until, g_fg_slow_f0, g_fg_slow_t0, g_fg_slow_m0, g_fg_slow_last; static int g_fg_slow_hitch; static int g_fg_slow_strikes, g_fg_slow_period = 120;
// 纯函数给单测：返回新的连续低帧窗口数；到 3 就该退避 | Pure, for the unit test: returns the new count of consecutive slow windows; 3 means back off
static int fgen_slow_step(int strikes, double fps, uint64_t mids, int hitch) { if (mids < 270 || hitch) return 0; return fps < FG_SLOW_FPS ? strikes + 1 : 0; }
static void fgen_slow_check(uint64_t f) {
    uint64_t now = now_ns();
    static int off = -1; if (off < 0) off = getenv("DM_FG_NO_BACKOFF") != NULL;   // 量测开关：真机对比时不退避 | measurement switch: no back-off during device comparisons
    if (off) return;
    if (g_fg_slow_last && now - g_fg_slow_last > 100000000ull) g_fg_slow_hitch = 1;
    g_fg_slow_last = now;
    if (atomic_load(&g_fg_slowblock)) { if (now / 1000000000ull >= g_fg_slow_until) { atomic_store(&g_fg_slowblock, 0); g_fg_slow_f0 = 0; MLOG("帧生成：帧率退避 结束，重开插帧"); } return; }
    uint64_t mids = __atomic_load_n(&g_fg.n_mid, __ATOMIC_RELAXED);
    if (!g_fg_slow_f0 || f < g_fg_slow_f0) { g_fg_slow_f0 = f; g_fg_slow_t0 = now; g_fg_slow_m0 = mids; g_fg_slow_strikes = 0; g_fg_slow_hitch = 0; return; }
    if (f < g_fg_slow_f0 + 300) return;
    double fps = (f - g_fg_slow_f0) * 1e9 / (double)(now - g_fg_slow_t0); g_fg_slow_strikes = fgen_slow_step(g_fg_slow_strikes, fps, mids - g_fg_slow_m0, g_fg_slow_hitch);
    g_fg_slow_f0 = f; g_fg_slow_t0 = now; g_fg_slow_m0 = mids; g_fg_slow_hitch = 0;
    if (g_fg_slow_strikes >= 3) { g_fg_slow_until = now / 1000000000ull + (uint64_t)g_fg_slow_period; atomic_store(&g_fg_slowblock, 1);
        MLOG("帧生成：帧率退避 开始，渲染 %.1f 帧低于 %d 帧，停止插帧 %d 秒", fps, FG_SLOW_FPS, g_fg_slow_period); g_fg_slow_period = g_fg_slow_period >= 300 ? 600 : g_fg_slow_period * 2; g_fg_slow_strikes = 0; }
}
static int fgen_effective(void) { return dm_fg_setting() && !atomic_load(&g_fg_memblock) && !atomic_load(&g_fg_slowblock); }
int mtl_fg_backoff(void) { return atomic_load(&g_fg_memblock) || atomic_load(&g_fg_slowblock); }   // 图形页副文用（宿主 dm_custom.c 有弱定义） | for the Graphics page subtext (weak definition in the host's dm_custom.c)
static int fgen_on(void) {   // 翻译线程，每帧锁存一次 | translation thread, latched once per frame
    uint64_t f = atomic_load(&g_cur_frame);
    if (f != g_fg.on_frame) { g_fg.on_frame = f; fgen_reap(); if (dm_fg_setting()) { fgen_mem_check(f); fgen_slow_check(f); } else { atomic_store(&g_fg_memblock, 0); atomic_store(&g_fg_slowblock, 0); g_fg_slow_f0 = 0; g_fg_slow_period = 120; g_fg_memw_seen = atomic_load(&g_mem_warn_n); } int on = fgen_effective();
        if (on != g_fg.on) { g_fg.on = on; g_fg.have_last = 0; if (!on && g_fg.h[0]) fgen_free();
            MLOG("帧生成：%s（帧 %llu）", on ? "开始插帧" : "停止插帧，历史环与插值器已释放", (unsigned long long)f); } }
    return g_fg.on;
}
// 绘制开头：主通道第一次绘制记视投影：cb3 +336 本帧、+464 游戏自己给的上一帧（同光追外推 nvn_mtl.m 的取法）。
// 实测（2026-10-28 Mac，走路 400 帧）：+464 与上一帧的 +336 逐帧相同；用它不必跨帧存矩阵，跳过一帧后的第一帧也有平移。插值位置与截图实测对得上（正好在两帧中间）
// Start of a draw: the main pass's first draw records the view-projection: cb3 +336 this frame, +464 the previous frame as the game supplies it (as the RT extrapolation in nvn_mtl.m).
// Measured (Mac 2026-10-28, 400 walking frames): +464 equals the previous frame's +336 every frame; using it needs no matrix kept across frames and the first frame after a skip still has its shift. The interpolated position matches screenshots (exactly halfway)
static void fgen_draw_begin(void) {
    if (!fgen_on() || g_defer_replaying) return;
    g_fg.n_work++;
    uint64_t f = atomic_load(&g_cur_frame); if (g_fg.vp_frame == f) return;
    if (!R.has_rt || R.rt.n != 1 || !R.rt.color[0] || !R.rt.depth) return;
    id<MTLTexture> c0 = tex_mtl(R.rt.color[0]), dt = tex_mtl(R.rt.depth);
    if (c0.pixelFormat != MTLPixelFormatRG11B10Float || dt.pixelFormat != MTLPixelFormatDepth32Float_Stencil8 || dt.width < 500 || dt.width == dt.height) return;
    const p_bind_buf *u = &S.ubo[0][3]; void *mb; uint64_t mo; const uint8_t *base;
    if (!u->addr || u->size < 528 || !mtl_addr(u->addr, 528, &mb, &mo) || !(base = [(__bridge id<MTLBuffer>)mb contents])) return;
    const float *v = (const float *)(base + mo + 336), *vq = (const float *)(base + mo + 464); for (int i = 0; i < 16; i++) { g_fg.vp[i] = v[i]; g_fg.vpp[i] = vq[i]; }
    g_fg.vp_frame = f; g_fg.iw = (int)dt.width; g_fg.ih = (int)dt.height;
}
static id<MTLTexture> fgen_tex(MTLPixelFormat pf, NSUInteger w, NSUInteger h, MTLTextureUsage u, id<MTLTexture> old) {
    if (old) fgen_retire(old, 0);   // 在途的出图 / 翻译命令缓冲可能还读着（MTL4 命令缓冲不保留资源） | an in-flight present / translation command buffer may still read it
    MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:pf width:w height:h mipmapped:NO];
    d.usage = u; d.storageMode = MTLStorageModePrivate; id<MTLTexture> t = [g_dev newTextureWithDescriptor:d]; if (t) res_add(t); return t;
}
static const char *k_fg_msl =
"#include <metal_stdlib>\nusing namespace metal;\n"
"struct V { float4 p [[position]]; };\n"
"vertex V fg_vs(uint i [[vertex_id]]) { float2 uv = float2((i << 1) & 2, i & 2); V v; v.p = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1); return v; }\n"
// 最终画面与放大目标逐像素比：不同 = 界面盖过（取最终画面），相同 = 没有界面（取插值放大 × 遮罩，同 nvn_metalfx.m）
// final vs. upsample target per pixel: different = UI on top (take the final picture), equal = no UI (take the upscaled interpolation x mask, as nvn_metalfx.m)
// 无界面处：同原版 ps_upsample_p（片元键 52aac1f2306b8fc5）—— uv 夹进取样框，中心 ×(1+4s) 减上下左右各 ×s，再乘遮罩（遮罩按原 uv 取）
// No UI: same as the original ps_upsample_p (fragment key 52aac1f2306b8fc5) - clamp uv into the box, centre x (1+4s) minus the four neighbours x s, times the mask (sampled at the raw uv)
"fragment float4 fg_fs(V v [[stage_in]], texture2d<float> f [[texture(0)]], texture2d<float> h [[texture(1)]], texture2d<float> o [[texture(2)]], texture2d<float> m [[texture(3)]], constant float4 *k [[buffer(0)]]) {\n"
"  constexpr sampler s(filter::linear, address::clamp_to_edge); uint2 p = uint2(v.p.xy); float4 a = f.read(p); if (any(a != h.read(p))) return a;\n"
"  float2 uv = v.p.xy / float2(f.get_width(), f.get_height()), c = clamp(uv, k[1].xy, k[1].zw), d = k[0].xy; float w = k[0].z;\n"
"  float3 r = o.sample(s, c, level(0)).rgb * (1 + 4 * w) - w * (o.sample(s, c - float2(d.x, 0), level(0)).rgb + o.sample(s, c + float2(d.x, 0), level(0)).rgb\n"
"    + o.sample(s, c - float2(0, d.y), level(0)).rgb + o.sample(s, c + float2(0, d.y), level(0)).rgb);\n"
"  return float4(r * m.sample(s, uv).r, a.a); }\n"
// 插值器不收 3D 画面格式时转成 RGBA16Float | convert to RGBA16Float when the interpolator refuses the 3D format
// 出图尺寸上直接合成（已放大好的放大目标，不重采样、不再跑超分）：与放大目标不同 = 界面，取本帧最终画面；其余取放大目标平移的结果，前后两帧对不上 = 独立运动，保留本帧
// 对不上的判定按 3×3 稀疏采样过半（间隔默认 3 像素，DM_FG_SHIFT 第四个参数，0 = 逐像素）：逐像素判定会让树叶、岩石边缘来回跳成噪点
// Composed directly at output size (from the already upscaled targets, no resampling and no second upscale): differs from the target = UI, take the final picture; otherwise the shifted target, and where the two frames disagree it moves on its own, keep the current frame
// "Disagree" is a majority of a sparse 3x3 (spacing 3 pixels by default, fourth DM_FG_SHIFT parameter, 0 = per pixel): per-pixel decisions flip back and forth into speckle on leaves and rock edges
"fragment float4 fg_shift(V v [[stage_in]], texture2d<float> f [[texture(0)]], texture2d<float> c [[texture(1)]], texture2d<float> o [[texture(2)]], constant float4 *k [[buffer(0)]]) {\n"
"  int2 q = int2(v.p.xy), wh = int2(c.get_width(), c.get_height()), a = q + int2(k[0].xy), b = q - int2(k[0].zw); float4 F = f.read(uint2(q)), C = c.read(uint2(q)); if (any(F != C)) return F;\n"
"  bool ia = all(a >= 0) && all(a < wh), ib = all(b >= 0) && all(b < wh);\n"
"  if (!ia && !ib) return F; if (!ia) return float4(o.read(uint2(b)).rgb, F.a); float3 A = c.read(uint2(a)).rgb; if (!ib) return float4(A, F.a);\n"
"  int st = int(k[1].z), bad = 0, tot = 0; int2 mx = wh - 1;\n"
"  for (int y = -1; y <= 1; y++) for (int x = -1; x <= 1; x++) { if (st == 0 && (x != 0 || y != 0)) continue; int2 e = int2(x, y) * st;\n"
"    float3 P = c.read(uint2(clamp(a + e, int2(0), mx))).rgb, Q = o.read(uint2(clamp(b + e, int2(0), mx))).rgb, d = abs(P - Q); float m = max(d.r, max(d.g, d.b)), r = max(max(P.r, max(P.g, P.b)), max(Q.r, max(Q.g, Q.b)));\n"
"    tot++; if (m > k[1].x + k[1].y * r) bad++; }\n"
"  return float4(bad * 2 > tot ? F.rgb : A, F.a); }\n"
"fragment float4 fg_cv(V v [[stage_in]], texture2d<float> c [[texture(0)]]) { return float4(c.read(uint2(v.p.xy)).rgb, 1); }\n"
// 真帧走 MetalFX 空间超分时：插值结果已由同一种超分放大到出图尺寸（u），只叠界面 | when the real frame used MetalFX spatial: the interpolation was upscaled by the same scaler (u), only overlay the UI
// u 是超分只放大的结果（出图尺寸），遮罩在这里乘（同 nvn_metalfx.m 的遮罩趟：按 uv 双线性取遮罩） | u is the scale-only result (output size); the mask is applied here (as nvn_metalfx.m's mask pass: bilinear mask at uv)
"fragment float4 fg_fs2(V v [[stage_in]], texture2d<float> f [[texture(0)]], texture2d<float> h [[texture(1)]], texture2d<float> u [[texture(2)]], texture2d<float> m [[texture(3)]]) {\n"
"  constexpr sampler s(filter::linear, address::clamp_to_edge); uint2 p = uint2(v.p.xy); float4 a = f.read(p); if (any(a != h.read(p))) return a;\n"
"  float2 uv = v.p.xy / float2(f.get_width(), f.get_height()); return float4(u.read(p).rgb * m.sample(s, uv).r, a.a); }\n";
// 后台建插值器：先按 3D 画面格式建，不收就退到 RGBA16Float（出图线程先转格式）；首次约 0.4 s，建好前不插
// Build the interpolator in the background: first with the 3D picture's format, else RGBA16Float (the present thread converts first); about 0.4 s the first time, no interpolation until ready
// 建造队列必须串行：开关快速关→开、或建造中尺寸变化时 fgen_alloc 会再派一次建造；两次 newFrameInterpolatorWithDevice: 并发时
// AGX 驱动的着色器编译回调会多调一次 dispatch_group_leave（libdispatch 直接 SIGTRAP，t_fgen 第 6→7 节偶发、真机同路径也会崩）。
// The build queue must be serial: a quick off→on, or a size change mid-build, makes fgen_alloc dispatch another build; two concurrent
// newFrameInterpolatorWithDevice: calls make the AGX driver's shader-compile callback leave a dispatch group once too often (libdispatch SIGTRAPs;
// intermittent in t_fgen sections 6→7, the same path on a device would crash too).
static dispatch_queue_t fgen_build_q(void) {
    static dispatch_queue_t q; static dispatch_once_t once;
    dispatch_once(&once, ^{ q = dispatch_queue_create("dm.fgen.build", dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL, QOS_CLASS_USER_INITIATED, 0)); });
    return q;
}
#ifdef FGEN_TEST
static _Atomic int g_fgen_test_build_live, g_fgen_test_build_peak, g_fgen_test_build_hold_ms;   // 测试：同时在跑的建造数 / 峰值 / 建造开头拖延（缺省 0） | test: concurrently running builds / peak / delay at build start (0 by default)
#define FGEN_TEST_BUILD_IN() do { int l_ = atomic_fetch_add(&g_fgen_test_build_live, 1) + 1, p_ = atomic_load(&g_fgen_test_build_peak); \
    while (l_ > p_ && !atomic_compare_exchange_weak(&g_fgen_test_build_peak, &p_, l_)) {} int h_ = atomic_load(&g_fgen_test_build_hold_ms); if (h_) usleep(h_ * 1000); } while (0)
#define FGEN_TEST_BUILD_OUT() atomic_fetch_sub(&g_fgen_test_build_live, 1)
#else
#define FGEN_TEST_BUILD_IN() ((void)0)
#define FGEN_TEST_BUILD_OUT() ((void)0)
#endif
static void fgen_build(void) {
    int iw = g_fg.iw, ih = g_fg.ih, mw, mh; fgen_mvsize(iw, ih, &mw, &mh); NSUInteger ow = g_fg.w3, oh = g_fg.h3, w = g_fg.w, hgt = g_fg.hgt; MTLPixelFormat pf = g_fg.fmt3, df = g_fg.fmt;
    id<MTLTexture> fxin = g_fg.fxin, comp = g_fg.comp;   // comp 只用来告诉超分出图尺寸与格式 | comp only tells the scaler the output size and format
    dispatch_block_t body = ^{
        uint64_t t0 = now_ns(); NSError *err = nil;
        // 叠界面 / 转格式管线：运行时从源码编（不进游戏着色器缓存，切开关不用重出缓存）；放后台，免得第一次打开时翻译线程卡一下
        // Overlay / conversion pipelines: compiled from source at run time (not part of the game shader cache, toggling needs no cache rebuild); in the background so the first enable does not stall the translation thread
        pthread_mutex_lock(&g_fg.mu); int have = g_fg.pso && g_fg.cvpso && g_fg.pso2 && g_fg.cppso && g_fg.pso_fmt == df && g_fg.cppso.label.length && [g_fg.cppso.label isEqualToString:[NSString stringWithFormat:@"%lu", (unsigned long)pf]]; pthread_mutex_unlock(&g_fg.mu);
        if (!have) {
            id<MTLLibrary> lib = [g_dev newLibraryWithSource:@(k_fg_msl) options:nil error:&err];
            MTLRenderPipelineDescriptor *pd = [MTLRenderPipelineDescriptor new]; pd.vertexFunction = [lib newFunctionWithName:@"fg_vs"]; pd.fragmentFunction = [lib newFunctionWithName:@"fg_fs"];
            pd.colorAttachments[0].pixelFormat = df; id<MTLRenderPipelineState> pso = lib ? [g_dev newRenderPipelineStateWithDescriptor:pd error:&err] : nil;
            pd.fragmentFunction = [lib newFunctionWithName:@"fg_cv"]; pd.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA16Float; id<MTLRenderPipelineState> cvpso = lib ? [g_dev newRenderPipelineStateWithDescriptor:pd error:&err] : nil;
            pd.fragmentFunction = [lib newFunctionWithName:@"fg_fs2"]; pd.colorAttachments[0].pixelFormat = df; id<MTLRenderPipelineState> pso2 = lib ? [g_dev newRenderPipelineStateWithDescriptor:pd error:&err] : nil;
            pd.fragmentFunction = [lib newFunctionWithName:@"fg_cv"]; pd.colorAttachments[0].pixelFormat = pf; pd.label = [NSString stringWithFormat:@"%lu", (unsigned long)pf];
            id<MTLRenderPipelineState> cppso = lib ? [g_dev newRenderPipelineStateWithDescriptor:pd error:&err] : nil;
            pd.fragmentFunction = [lib newFunctionWithName:@"fg_shift"]; pd.colorAttachments[0].pixelFormat = df; id<MTLRenderPipelineState> shiftpso = lib ? [g_dev newRenderPipelineStateWithDescriptor:pd error:&err] : nil;
            if (!pso || !cvpso || !pso2 || !cppso || !shiftpso) { atomic_store(&g_fg.fi_state, 3); MLOG("【红】帧生成：叠界面管线建不出 %s，本局不插帧", err.localizedDescription.UTF8String ?: ""); return; }
            pthread_mutex_lock(&g_fg.mu); g_fg.pso = pso; g_fg.cvpso = cvpso; g_fg.pso2 = pso2; g_fg.cppso = cppso; g_fg.shiftpso = shiftpso; g_fg.pso_fmt = df; pthread_mutex_unlock(&g_fg.mu);
            MLOG("帧生成：叠界面管线建好（格式 %lu），用时 %.0f ms", (unsigned long)df, (now_ns() - t0) / 1e6);
        }
        // 插值帧的空间超分（3D 尺寸比出图小才要）：建不出不影响插帧，插值帧退回原版放大公式 | spatial upscaler for interpolated frames (only when 3D is smaller than the output); if it fails, interpolated frames fall back to the original upsample formula
        NvMetalFX *fx = nil;
        if (fxin && comp && (ow < w || oh < hgt) && [NvMetalFX supported:g_dev]) { uint64_t t1 = now_ns(); fx = [[NvMetalFX alloc] initWithDevice:g_dev input:fxin output:comp];
            if (fx) MLOG("帧生成：插值帧的 MetalFX 空间超分建好 %lux%lu → %lux%lu，用时 %.0f ms", (unsigned long)ow, (unsigned long)oh, (unsigned long)w, (unsigned long)hgt, (now_ns() - t1) / 1e6);
            else MLOG("帧生成：插值帧的 MetalFX 空间超分建不出，真帧走超分时插值帧用原版放大公式（会比真帧软）"); }
        id<MTL4Compiler> c = [g_dev newCompilerWithDescriptor:[MTL4CompilerDescriptor new] error:&err];
        id<MTL4FXFrameInterpolator> fi = nil; MTLPixelFormat used = pf;
        for (int k = 0; k < 2 && c && !fi && dm_fg_need_fi(); k++) {
            used = k ? MTLPixelFormatRGBA16Float : pf; if (k && pf == MTLPixelFormatRGBA16Float) break;
            MTLFXFrameInterpolatorDescriptor *d = [MTLFXFrameInterpolatorDescriptor new];
            d.colorTextureFormat = d.outputTextureFormat = used; d.depthTextureFormat = MTLPixelFormatDepth32Float; d.motionTextureFormat = MTLPixelFormatRG16Float;
            d.inputWidth = mw; d.inputHeight = mh; d.outputWidth = ow; d.outputHeight = oh;
            @try { fi = [d newFrameInterpolatorWithDevice:g_dev compiler:c]; } @catch (NSException *x) { fi = nil; MLOG("帧生成：插值器不收格式 %lu（%s）", (unsigned long)used, x.reason.UTF8String ?: ""); }
        }
        pthread_mutex_lock(&g_fg.mu);
        int same = g_fg.w3 == ow && g_fg.h3 == oh && g_fg.fmt3 == pf && g_fg.iw == iw && g_fg.ih == ih && g_fg.w == w && g_fg.hgt == hgt;
        same = same && g_fg.fxin == fxin && g_fg.comp == comp;
        if (fi && same) { fi.fence = g_fg.fence; g_fg.fi = fi; g_fg.ifmt = used; g_fg.fx = fx; g_fg.fx_res = 0; atomic_store(&g_fg.fi_state, 4); }
        else if (!dm_fg_need_fi() && same) { g_fg.ifmt = used; g_fg.fx = fx; g_fg.fx_res = 0; atomic_store(&g_fg.fi_state, 4); }   // 翻译线程配好输出贴图后转就绪 | ready once the translation thread has matched the output texture
        else if (!fi && dm_fg_need_fi()) atomic_store(&g_fg.fi_state, 3);
        pthread_mutex_unlock(&g_fg.mu);
        if (!dm_fg_need_fi()) MLOG("帧生成：镜头平移原型，不建插值器");
        else if (fi) MLOG("帧生成：插值器建好 深度 / 运动 %dx%d，颜色 %lux%lu 格式 %lu%s，用时 %.0f ms", mw, mh, (unsigned long)ow, (unsigned long)oh, (unsigned long)used, used != pf ? "（3D 画面要先转格式）" : "", (now_ns() - t0) / 1e6);
        else MLOG("【红】帧生成：插值器建不出（深度 / 运动 %dx%d，颜色 %lux%lu 格式 %lu）%s，本局不插帧", mw, mh, (unsigned long)ow, (unsigned long)oh, (unsigned long)pf, err.localizedDescription.UTF8String ?: "");
    };
    dispatch_async(fgen_build_q(), ^{ FGEN_TEST_BUILD_IN(); body(); FGEN_TEST_BUILD_OUT(); });   // 串行：见 fgen_build_q | serial: see fgen_build_q
}
// 翻译线程：插值器建好后按它收的格式配输出 / 转格式贴图，再转就绪（出图线程只在就绪后读这几张） | Translation thread: match the output / conversion textures to the interpolator's format, then mark ready
static void fgen_ready(void) {
    if (atomic_load(&g_fg.fi_state) != 4) return;
    pthread_mutex_lock(&g_fg.mu);
    if (g_fg.ifmt != g_fg.out.pixelFormat) {
        const MTLTextureUsage rw = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite | MTLTextureUsageRenderTarget;
        g_fg.out = fgen_tex(g_fg.ifmt, g_fg.w3, g_fg.h3, rw, g_fg.out);
        for (int i = 0; i < 2; i++) g_fg.cv[i] = fgen_tex(g_fg.ifmt, g_fg.w3, g_fg.h3, MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget, g_fg.cv[i]);
    }
    if (g_fg.fx && !g_fg.fx_res) { for (id<MTLResource> r in g_fg.fx.resources) res_add(r); g_fg.fx_res = 1; }   // 超分自带的中间贴图进驻留集 | the scaler's own intermediate texture joins the residency set
    int ok = g_fg.out && (g_fg.ifmt == g_fg.fmt3 || (g_fg.cv[0] && g_fg.cv[1]));
    atomic_store(&g_fg.fi_state, ok ? 2 : 3);
    pthread_mutex_unlock(&g_fg.mu);
    if (!ok) MLOG("【红】帧生成：转格式贴图建不出，本局不插帧");
}
// 尺寸 / 格式一变：重建历史环与出图线程要用的贴图，后台建插值器 | On a size / format change: rebuild the rings and the present-side textures, build the interpolator in the background
static int fgen_alloc(id<MTLTexture> dst, id<MTLTexture> src, id<MTLTexture> mask) {
    if (g_fg.h[0] && g_fg.w == dst.width && g_fg.hgt == dst.height && g_fg.fmt == dst.pixelFormat && g_fg.w3 == src.width && g_fg.h3 == src.height && g_fg.fmt3 == src.pixelFormat
        && g_fg.mw == mask.width && g_fg.mh == mask.height && g_fg.mfmt == mask.pixelFormat && g_fg.dep && g_fg.dep_iw == g_fg.iw && g_fg.dep_ih == g_fg.ih)
        return atomic_load(&g_fg.fi_state) != 3;
    if (!g_fg.iw || !g_fg.ih) return 0;
    const MTLTextureUsage rw = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite | MTLTextureUsageRenderTarget;
    pthread_mutex_lock(&g_fg.mu);
    for (int i = 0; i < FG_RING; i++) {
        g_fg.h[i] = fgen_tex(dst.pixelFormat, dst.width, dst.height, MTLTextureUsageShaderRead, g_fg.h[i]);
        g_fg.c3[i] = fgen_tex(src.pixelFormat, src.width, src.height, MTLTextureUsageShaderRead, g_fg.c3[i]);
        g_fg.mk[i] = fgen_tex(mask.pixelFormat, mask.width, mask.height, MTLTextureUsageShaderRead, g_fg.mk[i]);
        g_fg.h_frame[i] = 0; g_fg.e[i].frame = 0; g_fg.busy[i] = 0; }   // 新贴图：旧占用随旧贴图（fgen_retire 等出图做完） | new textures: old occupancy goes with the old ones (fgen_retire waits for the present)
    for (int i = 0; i < 2; i++) { if (g_fg.cv[i]) fgen_retire(g_fg.cv[i], 0); g_fg.cv[i] = nil; }   // 用到才建（插值器不收 3D 格式时） | built on demand (when the interpolator refuses the 3D format)
    // 插值输出：3D 尺寸；格式先按 3D 画面，插值器退到 RGBA16Float 时出图线程重建 | interpolated output: 3D size; 3D format first, rebuilt by the present thread if the interpolator falls back to RGBA16Float
    g_fg.out = fgen_tex(src.pixelFormat, src.width, src.height, rw, g_fg.out);
    g_fg.comp = fgen_tex(dst.pixelFormat, dst.width, dst.height, MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget | MTLTextureUsagePixelFormatView, g_fg.comp);   // 上屏对 sRGB 格式开原始格式视图（present_src） | the present opens a raw-format view for sRGB formats
    g_fg.fxin = fgen_tex(src.pixelFormat, src.width, src.height, MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget, g_fg.fxin);
    if (g_fg.fx) { if (g_fg.fx_res) for (id<MTLResource> r in g_fg.fx.resources) fgen_retire(r, 0); fgen_retire(g_fg.fx, 1); g_fg.fx = nil; g_fg.fx_res = 0; }
    int mw, mh; fgen_mvsize(g_fg.iw, g_fg.ih, &mw, &mh); g_fg.dep_iw = g_fg.iw; g_fg.dep_ih = g_fg.ih;
    g_fg.dep = fgen_tex(MTLPixelFormatDepth32Float, mw, mh, MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget, g_fg.dep);
    g_fg.mv = fgen_tex(MTLPixelFormatRG16Float, mw, mh, MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget, g_fg.mv);
    g_fg.w = dst.width; g_fg.hgt = dst.height; g_fg.fmt = dst.pixelFormat; g_fg.w3 = src.width; g_fg.h3 = src.height; g_fg.fmt3 = g_fg.ifmt = src.pixelFormat;
    g_fg.mw = mask.width; g_fg.mh = mask.height; g_fg.mfmt = mask.pixelFormat; g_fg.have_last = 0; g_fg.last_slot = -1;
    if (g_fg.fi) fgen_retire(g_fg.fi, 1);
    g_fg.fi = nil; atomic_store(&g_fg.fi_state, 1);
    pthread_mutex_unlock(&g_fg.mu);
    int ok = g_fg.h[FG_RING - 1] && g_fg.c3[FG_RING - 1] && g_fg.mk[FG_RING - 1] && g_fg.out && g_fg.comp && g_fg.dep && g_fg.mv;
    if (ok) {   // 叠界面 / 转格式管线在后台和插值器一起建（fgen_build），这里只建不花时间的 | the overlay / conversion pipelines are built in the background with the interpolator (fgen_build); only cheap objects here
        NSError *e = nil;
        if (!g_fg.args) { MTL4ArgumentTableDescriptor *ad = [MTL4ArgumentTableDescriptor new]; ad.maxTextureBindCount = 4; ad.maxBufferBindCount = 1; g_fg.args = [g_dev newArgumentTableWithDescriptor:ad error:&e]; }
        if (!g_fg.kbuf) { g_fg.kbuf = [g_dev newBufferWithLength:FG_RING * 160 options:MTLResourceStorageModeShared]; if (g_fg.kbuf) res_add(g_fg.kbuf); }
        if (!g_fg.fence) g_fg.fence = [g_dev newFence];
        if (!g_fg.args || !g_fg.fence || !g_fg.kbuf) { MLOG("【红】帧生成：参数表 / 栅栏 / 常量缓冲建不出 %s", e.localizedDescription.UTF8String ?: ""); ok = 0; }
    }
    if (!ok) { atomic_store(&g_fg.fi_state, 3); MLOG("【红】帧生成：贴图 / 管线建不出，本局不插帧"); return 0; }
    fgen_build();
    MLOG("帧生成：历史环 %d 组：3D 画面 %lux%lu 格式 %lu，遮罩 %lux%lu 格式 %lu，放大目标 %lux%lu 格式 %lu；深度 / 运动 %dx%d；后台建插值器（按 3D 尺寸插值）", FG_RING,
        (unsigned long)src.width, (unsigned long)src.height, (unsigned long)src.pixelFormat, (unsigned long)mask.width, (unsigned long)mask.height, (unsigned long)mask.pixelFormat,
        (unsigned long)dst.width, (unsigned long)dst.height, (unsigned long)dst.pixelFormat, mw, mh);
    return 1;
}
// 相邻两帧视投影 → 镜头平移（目标 = 源 + d，3D 像素；行向量 clip = (ndc, z, 1) · M，同 nvn_rtex.inc rt_shift_px 但不取整）；朝向变返回 0
// Two consecutive view-projections → camera translation (dest = src + d, 3D pixels; same as nvn_rtex.inc rt_shift_px without rounding); returns 0 if the orientation changed
static int fgen_shift(const double *prev, const double *cur, double W, double H, double *dx, double *dy) {
    for (int r = 0; r < 3; r++) for (int c = 0; c < 4; c++) if (fabs(cur[r * 4 + c] - prev[r * 4 + c]) > 1e-4) return 0;
    double ia[16], m[16]; rx_inv4(prev, ia);
    for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) { double s = 0; for (int k = 0; k < 4; k++) s += ia[r * 4 + k] * cur[k * 4 + c]; m[r * 4 + c] = s; }
    if (m[15] == 0) return 0;
    *dx = m[12] / m[15] / 2 * W; *dy = -m[13] / m[15] / 2 * H; return 1;
}
// 诊断转存 DM_FG_DUMP=<插值帧数>:<目录>（只跑一次）：插值帧数到了以后的第一次放大绘制，把原版放大读的 reg 10 贴图、刚拷进历史环的那份、遮罩、放大目标
// 原样按字节转存成 .raw（不经任何着色器），另记这几张贴图的视图信息和 reg 10 的采样器参数，离线解码对比
// Diagnostic dump DM_FG_DUMP=<interpolated frames>:<dir> (once): at the first upsample draw after that many interpolated frames, dump the reg 10 texture the original upsample reads,
// the copy just made into the ring, the mask and the upsample target byte for byte as .raw (no shader involved), plus view info and reg 10's sampler parameters, for offline decoding
static void fgen_dump(BENC b, id<MTLTexture> src, id<MTLTexture> mask, id<MTLTexture> dst, int slot, uint64_t treg10) {
    static int state = -1; static long at; static char dir[700];
    if (state < 0) { state = 0; const char *e = getenv("DM_FG_DUMP"), *c = e ? strchr(e, ':') : NULL; if (c) { at = atol(e); strlcpy(dir, c + 1, sizeof dir); mkdir(dir, 0755); state = 1; } }
    if (state != 1 || (long)g_fg.n_mid < at) return;
    state = 2;
    [b barrierAfterEncoderStages:MTLStageBlit beforeEncoderStages:MTLStageBlit visibilityOptions:MTL4VisibilityOptionDevice];   // 等上面拷进历史环的那份写完再读 | wait for the ring copy above before reading it
    id<MTLTexture> tx[4] = { src, g_fg.c3[slot], mask, dst }; static const char *const nm[4] = { "reg10", "ring3d", "mask", "target" };
    for (int i = 0; i < 4; i++) { id<MTLTexture> t = tx[i]; if (!t) continue;
        size_t bpp = t.pixelFormat == MTLPixelFormatR8Unorm ? 1 : t.pixelFormat == MTLPixelFormatRGBA16Float ? 8 : 4, row = t.width * bpp;
        id<MTLBuffer> buf = [g_dev newBufferWithLength:row * t.height options:MTLResourceStorageModeShared]; if (!buf) continue; res_add(buf);
        enc_b_copy_texture_buffer(b, t, 0, 0, MTLOriginMake(0, 0, 0), MTLSizeMake(t.width, t.height, 1), buf, 0, row, row * t.height);
        id<MTLTexture> par = t.parentTexture;
        MLOG("帧生成转存：%s %lux%lu 格式 %lu 类型 %lu 层级 %lu 用途 %lu；父贴图 %s %lux%lu 格式 %lu 层级数 %lu，视图起始层级 %lu", nm[i], (unsigned long)t.width, (unsigned long)t.height,
            (unsigned long)t.pixelFormat, (unsigned long)t.textureType, (unsigned long)t.mipmapLevelCount, (unsigned long)t.usage, par ? "有" : "无", (unsigned long)par.width, (unsigned long)par.height,
            (unsigned long)par.pixelFormat, (unsigned long)par.mipmapLevelCount, (unsigned long)t.parentRelativeLevel);
        char path[800]; snprintf(path, sizeof path, "%s/%s_%lux%lu_f%lu.raw", dir, nm[i], (unsigned long)t.width, (unsigned long)t.height, (unsigned long)t.pixelFormat);
        NSString *ps = @(path); size_t n = row * t.height;
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 3 * NSEC_PER_SEC), dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{   // 3 秒后命令缓冲早已做完 | the command buffer is long done after 3 s
            [[NSData dataWithBytes:buf.contents length:n] writeToFile:ps atomically:YES]; MLOG("帧生成转存：写好 %s", ps.lastPathComponent.UTF8String); });
    }
    int sid = (int)(treg10 >> 20);
    if (sid > 0 && sid < g_nsreg && g_sreg_ok[sid]) MLOG("帧生成转存：reg 10 采样器 %d 放大过滤 %d 缩小过滤 %d（0 最近点 / 1 双线性；2–5 带 mip）", sid, (int)g_sreg[sid].magf, (int)g_sreg[sid].minf);
    else MLOG("帧生成转存：reg 10 采样器号 %d 认不出", sid);
}
// 槽位还被出图读着：读它的那张出图命令缓冲（票号 busy[slot]）还没做完 | slot still being read by the present: its present command buffer (ticket busy[slot]) has not completed
static int fgen_slot_busy(int slot, uint64_t *bt_out, uint64_t *done_out) {
    pthread_mutex_lock(&g_fg.mu); uint64_t bt = g_fg.busy[slot]; pthread_mutex_unlock(&g_fg.mu);
    uint64_t done = 0; if (bt) { pthread_mutex_lock(&g_pq_mu); done = g_pq_done; pthread_mutex_unlock(&g_pq_mu); }
    if (bt_out) *bt_out = bt; if (done_out) *done_out = done;
    return bt && done < bt;
}
// 原版放大绘制编完：拷 3D 画面进历史环、发布本帧条目 | After the original upsample draw is encoded: copy the 3D picture into the ring and publish this frame's entry
static void fgen_after_upsample(NvProg *pr, int mfx) {   // mfx：这一帧走了 MetalFX 空间超分（metalfx_draw 接手） | mfx: this frame went through MetalFX spatial (metalfx_draw took over)
    if (!g_fg.on || g_defer_replaying || !pr || !pr->sh[1] || strncmp(pr->sh[1]->key, "52aac1f2306b8fc5", 16)) return;
    g_fg.n_work++;
    if (!R.has_rt || R.rt.n < 1 || !R.rt.color[0]) return;
    // 放大绘制是单输出（着色器反射只有 color0）：挂着两张附件（场景 + 窗口）时它也只写第 0 张，窗口贴图这时还是空的。原先取窗口贴图做对照，拷到全黑、逐像素比较把整屏都当界面，插值帧就成了真帧（17 Pro 2026-10-30：fg_mid 与 fg_real 逐字节相同）
    // The upsample draw has one output (reflection shows only color0): with two attachments (scene + window) it still writes only the first, the window texture is still empty at this point. It used to take the window texture as the reference: the copy was all black, the per-pixel comparison took the whole screen as UI and the interpolated frame became the real one (17 Pro 2026-10-30: fg_mid and fg_real byte-identical)
    int wk = 0;
    uint64_t serial; int level, slice; rt_sub(&R.rt, wk, &serial, &level, &slice);
    uint64_t f = atomic_load(&g_cur_frame);
    if (level || slice || !serial || g_fg.last_frame == f) return;
    // 放大的输入：reg 10 = 3D 画面，reg 11 = 暗角遮罩，常量缓冲前两项 = 3D 画面用到的 UV 比例（同 nvn_metalfx.inc metalfx_draw）
    // Upsample inputs: reg 10 = 3D picture, reg 11 = vignette mask, first two constants = UV scale of the 3D picture (as nvn_metalfx.inc metalfx_draw)
    id<MTLTexture> dst = tex_mtl(R.rt.color[wk]), src = nil, mask = nil; NvShader *fs = pr->sh[1]; float uv[2] = {0}, kv[12] = {0}; int uok = 0;
    for (int i = 0; i < fs->rf.nb; i++) { nvn_rbind *rb = &fs->rf.b[i];
        if (rb->kind == NVN_RB_TEX && rb->reg < 128) { void *view = NULL; tex_fast_rid((int)(S.tex[1][rb->reg] & 0xfffff), &view); if (rb->reg == 10) src = (__bridge id<MTLTexture>)view; else if (rb->reg == 11) mask = (__bridge id<MTLTexture>)view; }
        if (rb->kind == NVN_RB_CBUF) { uok = 1; for (int j = 0; j < 12; j++) uok &= ubo_float(1, (uint16_t)((rb->reg << 8) | j), &kv[j]); uv[0] = kv[0]; uv[1] = kv[1]; } }
    if (!dst || dst.textureType != MTLTextureType2D || dst.sampleCount != 1) return;
    if (!src || !mask || src == dst || mask == dst || src.textureType != MTLTextureType2D || mask.textureType != MTLTextureType2D || src.sampleCount != 1 || mask.sampleCount != 1
        || !uok || fabsf(uv[0] - 1) > .0001f || fabsf(uv[1] - 1) > .0001f) {
        static int once; if (!once++) MLOG("【红】帧生成：放大绘制的输入认不出（3D 画面 %s，遮罩 %s，UV 比例 %s %.4f,%.4f），不插帧", src ? "有" : "无", mask ? "有" : "无", uok ? "读到" : "读不到", uv[0], uv[1]);
        return; }
    if (!fgen_alloc(dst, src, mask)) return;
    fgen_ready();
    int slot = (g_fg.last_slot + 1) % FG_RING;
    // 槽位复用的完成证明：上一次读这一槽的出图命令缓冲没做完（同一 GPU 队列上本帧的拷贝可能先于它的读），这一帧不拷不插
    // Proof before reusing a slot: if the present command buffer that last read it has not completed (on the same GPU queue this copy could run before its read), skip this frame
    uint64_t bt, done; if (fgen_slot_busy(slot, &bt, &done)) { g_fg.have_last = 0; g_fg.n_busy++;
        if (g_fg.n_busy <= 10) MLOG("帧生成：历史环槽 %d 还被出图票 %llu 读着（已完成 %llu），这一帧不插（帧 %llu）", slot, (unsigned long long)bt, (unsigned long long)done, (unsigned long long)f); return; }
    BENC b = blit(); enc_b_copy_texture(b, dst, 0, 0, MTLOriginMake(0, 0, 0), MTLSizeMake(dst.width, dst.height, 1), g_fg.h[slot], 0, 0, MTLOriginMake(0, 0, 0));
    enc_b_copy_texture(b, src, 0, 0, MTLOriginMake(0, 0, 0), MTLSizeMake(src.width, src.height, 1), g_fg.c3[slot], 0, 0, MTLOriginMake(0, 0, 0));
    enc_b_copy_texture(b, mask, 0, 0, MTLOriginMake(0, 0, 0), MTLSizeMake(mask.width, mask.height, 1), g_fg.mk[slot], 0, 0, MTLOriginMake(0, 0, 0));
    fgen_dump(b, src, mask, dst, slot, S.tex[1][10]);   // 诊断转存（DM_FG_DUMP，缺省不开） | diagnostic dump (DM_FG_DUMP, off by default)
    args_dirty_all(); g_pso_dirty = g_dss_dirty = 1;   // 本通道在下一次绘制（界面）时按「读入」重开 | the pass reopens with Load at the next (UI) draw
    int vp_now = g_fg.vp_frame == f, ok = 0; double dx = 0, dy = 0;
    if (g_fg.have_last && g_fg.last_frame + 1 == f && vp_now && g_fg.last_vp_ok) {
        ok = fgen_shift(g_fg.vpp, g_fg.vp, g_fg.iw, g_fg.ih, &dx, &dy) && fabs(dx) <= 64 && fabs(dy) <= 64;
        if (!ok) { g_fg.n_jump++; if (g_fg.n_jump <= 20) MLOG("帧生成：镜头跳变（帧 %llu，平移 %.1f,%.1f），这一帧不插", (unsigned long long)f, dx, dy); }
    }
    pthread_mutex_lock(&g_fg.mu);
    int ei = (int)(f % FG_RING);
    g_fg.e[ei].frame = f; g_fg.e[ei].ser = serial; g_fg.e[ei].slot = slot; g_fg.e[ei].prev = g_fg.last_slot; g_fg.e[ei].ok = ok && g_fg.last_slot >= 0; g_fg.e[ei].mfx = mfx;
    g_fg.e[ei].mvx = (float)-dx; g_fg.e[ei].mvy = (float)-dy;   // 运动矢量指向上一帧位置 | motion vectors point to the position in the previous frame
    if (ok && (fabs(dx) >= .05 || fabs(dy) >= .05)) { __atomic_add_fetch(&g_fg.n_move, 1, __ATOMIC_RELAXED);
        uint32_t c = (uint32_t)(hypot(dx, dy) * 100 + .5); if (c > __atomic_load_n(&g_fg.mv_max_c, __ATOMIC_RELAXED)) __atomic_store_n(&g_fg.mv_max_c, c, __ATOMIC_RELAXED); }
    g_fg.h_frame[slot] = f;
    const float kk[8] = { kv[4], kv[5], kv[6], 0, kv[8], kv[9], kv[10], kv[11] }; memcpy(g_fg.k[slot], kk, sizeof kk);   // 常量缓冲第 1、2 个向量 | vectors 1 and 2 of the constant buffer
    pthread_mutex_unlock(&g_fg.mu);
    g_fg.last_slot = slot; g_fg.last_frame = f; g_fg.have_last = 1; g_fg.last_vp_ok = vp_now;
    { static int once; if (!once++) MLOG("帧生成：首次拷 3D 画面（3D %lux%lu 格式 %lu，放大目标 附件 %d/%d ser=%llu %lux%lu 格式 %lu，帧 %llu；放大常量 步长 %.6f,%.6f 锐化 %.3f 取样框 %.5f,%.5f–%.5f,%.5f）", (unsigned long)src.width, (unsigned long)src.height, (unsigned long)src.pixelFormat,
        wk, R.rt.n, (unsigned long long)serial, (unsigned long)dst.width, (unsigned long)dst.height, (unsigned long)dst.pixelFormat, (unsigned long long)f, kv[4], kv[5], kv[6], kv[8], kv[9], kv[10], kv[11]); }
}
// 出图线程：这一帧能不能插（在取 drawable 之前问） | Present thread: can this frame be interpolated (asked before taking drawables)
static int fgen_mid_entry(uint64_t frame, uint64_t present_ser, id<MTLTexture> src, uint64_t ticket, int *ei_out) {
    if (!fgen_effective()) return 0;
    __atomic_add_fetch(&g_fg.n_work, 1, __ATOMIC_RELAXED);   // 出图线程 | present thread
    int why = -1, ei = (int)(frame % FG_RING);
    pthread_mutex_lock(&g_fg.mu);
    if (g_fg.e[ei].frame != frame) why = FGS_NOENTRY;
    else if (!g_fg.e[ei].ok) why = FGS_NOTOK;
    else if (g_fg.h_frame[g_fg.e[ei].slot] != frame || g_fg.h_frame[g_fg.e[ei].prev] != frame - 1 || atomic_load(&g_cur_frame) > frame + FG_RING - 2) why = FGS_STALE;
    else if (src.width != g_fg.w || src.height != g_fg.hgt || src.pixelFormat != g_fg.fmt) why = FGS_SIZE;
    else if (atomic_load(&g_fg.fi_state) != 2) why = FGS_NOTREADY;
    uint64_t up_ser = g_fg.e[ei].ser;
    if (why < 0) g_fg.busy[g_fg.e[ei].slot] = g_fg.busy[g_fg.e[ei].prev] = ticket;   // 这两槽归这次出图，直到它的命令缓冲做完 | both slots belong to this present until its command buffer completes
    pthread_mutex_unlock(&g_fg.mu);
    if (why >= 0) { g_fg.n_skip[why]++; return 0; }
    // 上屏的可以是另一张同尺寸同格式的贴图（Mac / iPhone 比例下：放大通道只写场景贴图，末尾合成趟再写窗口贴图）：叠回界面按逐像素比较，照样成立；记一次
    // What is presented may be another texture of the same size and format (Mac / iPhone aspect: the upsample pass writes only the scene texture, the final composite pass writes the window texture): the per-pixel UI overlay still holds; log once
    if (up_ser != present_ser) { static int once; if (!once++) MLOG("帧生成：上屏的是另一张贴图（上屏 ser=%llu，放大目标 ser=%llu，%lux%lu 格式 %lu），按逐像素比较认界面", (unsigned long long)present_ser, (unsigned long long)up_ser, (unsigned long)src.width, (unsigned long)src.height, (unsigned long)src.pixelFormat); }
    *ei_out = ei; return 1;
}
// 出图线程：在插值帧的命令缓冲里编插值（3D 尺寸）+ 放大叠界面，结果是 g_fg.comp（与 src 同尺寸同格式）
// Present thread: encode the interpolation (3D size) + upscale and UI overlay into the present command buffer; the result is g_fg.comp (same size and format as src)
static void fgen_pass(CBUF cb, id<MTLTexture> target, id<MTLRenderPipelineState> pso, id<MTLTexture> const *tx, int n, int wait_fence) {
    RPD *rp = [RPD new]; rp.colorAttachments[0].texture = target; rp.colorAttachments[0].loadAction = MTLLoadActionDontCare; rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    RENC en = [cb renderCommandEncoderWithDescriptor:rp]; if (!en) return;
    if (wait_fence) [en waitForFence:g_fg.fence beforeEncoderStages:MTLStageFragment];
    [en barrierAfterQueueStages:MTLStageAll beforeStages:MTLStageAll visibilityOptions:MTL4VisibilityOptionDevice];
    [en setArgumentTable:g_fg.args atStages:MTLRenderStageFragment];
    for (int i = 0; i < n; i++) [g_fg.args setTexture:tx[i].gpuResourceID atIndex:i];
    [en setViewport:(MTLViewport){0, 0, target.width, target.height, 0, 1}]; [en setRenderPipelineState:pso]; [en drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    [en barrierAfterStages:MTLStageAll beforeQueueStages:MTLStageAll visibilityOptions:MTL4VisibilityOptionDevice]; m4_end(en);
}
// 上屏分解（出图线程写 pre、完成回调写 GPU）：取两张 drawable、编码、提交到 GPU 开始、GPU 用时，每 600 张插值帧记一行 p50 / p95（真机对比用，分享摘要带最后一行）
// Present breakdown (pre from the present thread, GPU from the completion handler): taking both drawables, encoding, commit to GPU start, GPU span; one p50 / p95 line every 600 interpolated frames
static double fg_pct(double *v, int n, int k) { for (int i = 1; i < n; i++) { double x = v[i]; int j = i - 1; while (j >= 0 && v[j] > x) { v[j + 1] = v[j]; j--; } v[j + 1] = x; } return v[n * k / 100]; }
static void fgen_note_gpu(double dr_ms, double enc_ms, double wait_ms, double gpu_ms) {
    static double a[4][600]; static int n; static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER; double b[4][600]; int full = 0;
    pthread_mutex_lock(&mu); a[0][n] = dr_ms; a[1][n] = enc_ms; a[2][n] = wait_ms; a[3][n] = gpu_ms; if (++n == 600) { memcpy(b, a, sizeof b); n = 0; full = 1; } pthread_mutex_unlock(&mu);
    if (!full) return;
    double p[4][2]; for (int i = 0; i < 4; i++) { p[i][1] = fg_pct(b[i], 600, 95); p[i][0] = b[i][300]; }
    MLOG("帧生成：耗时 取 drawable p50 %.2f / p95 %.2f ms，编码 %.2f / %.2f ms，提交到开始 %.2f / %.2f ms，上屏命令缓冲 GPU %.2f / %.2f ms（600 张插值帧）", p[0][0], p[0][1], p[1][0], p[1][1], p[2][0], p[2][1], p[3][0], p[3][1]);
}
// 真实呈现节奏（两张 drawable 的 presentedTime，系统回调线程写）：相邻两次呈现的间隔分桶，每 1200 张记一行；节奏对时几乎都落在 8.3 ms 桶（WWDC25：帧间隔直方图最多两个桶）
// Actual presentation cadence (presentedTime of both drawables, written by the system's callback thread): intervals between consecutive presents are bucketed, one line every 1200; with correct pacing nearly all fall in the 8.3 ms bucket
static void fgen_note_present(int is_mid, double tt) {
    static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER; static double last; static unsigned b[5], n, dropped[2], shown[2];
    pthread_mutex_lock(&mu);
    if (tt > 0) { shown[is_mid]++; if (last > 0 && tt > last) { double ms = (tt - last) * 1000; b[ms < 6 ? 0 : ms < 11 ? 1 : ms < 14 ? 2 : ms < 20 ? 3 : 4]++; } if (tt > last) last = tt; } else dropped[is_mid]++;
    if (++n == 1200) { unsigned c[5], sh[2], dr[2]; memcpy(c, b, sizeof c); memcpy(sh, shown, sizeof sh); memcpy(dr, dropped, sizeof dr); memset(b, 0, sizeof b); memset(shown, 0, sizeof shown); memset(dropped, 0, sizeof dropped); n = 0; pthread_mutex_unlock(&mu);
        MLOG("帧生成：节奏 呈现间隔 <6 ms %u / 6–11 ms %u / 11–14 ms %u / 14–20 ms %u / ≥20 ms %u；上屏 真帧 %u 插值帧 %u；系统丢弃 真帧 %u 插值帧 %u", c[0], c[1], c[2], c[3], c[4], sh[0], sh[1], dr[0], dr[1]);
        return; }
    pthread_mutex_unlock(&mu);
}
static int fgen_encode_mid(CBUF cb, id<MTLTexture> src, int ei, uint64_t frame, double period, int gk, int gn) {
    id<MTL4FXFrameInterpolator> fi; id<MTLTexture> cur, prev, pre, mk, out, cv0, cv1; float mvx, mvy; int conv;
    pthread_mutex_lock(&g_fg.mu); fi = g_fg.fi; int sl = g_fg.e[ei].slot, ps = g_fg.e[ei].prev;
    cur = g_fg.c3[sl]; prev = g_fg.c3[ps]; pre = g_fg.h[sl]; mk = g_fg.mk[sl];
    // 常量写进本帧槽号那一格：上一次读这一格的出图命令缓冲已完成（槽位占用保护，见 fgen_after_upsample） | constants go into this frame's slot: the last present command buffer reading it has completed (slot occupancy, see fgen_after_upsample)
    uint64_t kaddr = 0; if (g_fg.kbuf) { memcpy((char *)g_fg.kbuf.contents + sl * 32, g_fg.k[sl], 32); kaddr = g_fg.kbuf.gpuAddress + (uint64_t)sl * 32; }
    out = g_fg.out; cv0 = g_fg.cv[0]; cv1 = g_fg.cv[1]; conv = g_fg.ifmt != g_fg.fmt3;
    NvMetalFX *fx = g_fg.e[ei].mfx ? g_fg.fx : nil; id<MTLTexture> fxin = g_fg.fxin;
    id<MTLTexture> mvt = g_fg.mv, dpt = g_fg.dep; int diw = g_fg.dep_iw, dih = g_fg.dep_ih;
    mvx = g_fg.e[ei].mvx; mvy = g_fg.e[ei].mvy; pthread_mutex_unlock(&g_fg.mu);
    if ((!fi && !fgen_shift_on()) || !cur || !prev || !pre || !mk || !out || !kaddr || !mvt || !dpt || !diw || !dih || (conv && (!cv0 || !cv1))) return 0;
    // 运动矢量（整屏常量，清成该值）与常量深度（首次清一次）：一个渲染通道 | motion vectors (one constant, cleared to it) and constant depth (cleared once): one render pass
    RPD *rp = [RPD new];
    rp.colorAttachments[0].texture = mvt; rp.colorAttachments[0].loadAction = MTLLoadActionClear; rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    double msx = (double)mvt.width / diw, msy = (double)mvt.height / dih;   // 3D 像素 → 运动贴图像素 | 3D pixels -> motion-texture pixels
    rp.colorAttachments[0].clearColor = MTLClearColorMake(mvx * msx, mvy * msy, 0, 0);
    static __weak id<MTLTexture> dep_cleared;   // 出图线程独用：清过常量深度的是哪一张（换新贴图后再清一次） | present thread only: which depth texture has been cleared (a new texture gets cleared again)
    if (dep_cleared != dpt) { rp.depthAttachment.texture = dpt; rp.depthAttachment.loadAction = MTLLoadActionClear; rp.depthAttachment.clearDepth = 0.5; rp.depthAttachment.storeAction = MTLStoreActionStore; dep_cleared = dpt; }
    RENC en = [cb renderCommandEncoderWithDescriptor:rp]; if (!en) return 0;
    [en barrierAfterQueueStages:MTLStageAll beforeStages:MTLStageAll visibilityOptions:MTL4VisibilityOptionDevice];   // 等本帧及之前提交的 3D 画面拷贝 | wait for the 3D picture copies submitted earlier
    [en barrierAfterStages:MTLStageAll beforeQueueStages:MTLStageAll visibilityOptions:MTL4VisibilityOptionDevice]; m4_end(en);
    if (conv) { fgen_pass(cb, cv0, g_fg.cvpso, &cur, 1, 0); fgen_pass(cb, cv1, g_fg.cvpso, &prev, 1, 0); cur = cv0; prev = cv1; }   // 插值器不收 3D 格式：先转 RGBA16Float | the interpolator refuses the 3D format: convert to RGBA16Float first
    static int noint = -1; if (noint < 0) noint = getenv("DM_FG_NOINTERP") != NULL;   // 测量开关：不跑插值器、把本帧 3D 画面当结果，量插值器之外的开销 | measurement switch: skip the interpolator and use this frame's 3D picture, to measure everything but the interpolator
    int shifted = 0;
    if (fgen_shift_on()) {   // 半程位移（出图像素）：d = -mv（内容从上一帧到本帧移了 d），中间帧 = 本帧 (p + d/2) = 上一帧 (p - d/2)，两段之和 = 取整后的 d | half shift (output pixels): d = -mv (content moved by d from previous to current), middle = current(p + d/2) = previous(p - d/2), the two parts sum to the rounded d
        float sx = (float)g_fg.w / g_fg.w3, sy = (float)g_fg.hgt / g_fg.h3, dx = -mvx * sx, dy = -mvy * sy, gt = (float)gk / gn, hx = roundf(dx * (1 - gt)), hy = roundf(dy * (1 - gt)); static float thr = -1, rel, step; if (thr < 0) { const char *e = getenv("DM_FG_SHIFT"); thr = .02f; rel = .08f; step = 3; if (e) sscanf(e, "%*d,%f,%f,%f", &thr, &rel, &step); }
        float zx = roundf(dx) - hx, zy = roundf(dy) - hy; if (dm_fg_gen() == 3) { hx = -roundf(dx * gt); hy = -roundf(dy * gt); zx = roundf(dx) - hx; zy = roundf(dy) - hy; }   /* 外推：本帧 (p − t·d)，对照上一帧 (p − t·d − d) | extrapolation: this frame at p - t*d, checked against the previous frame at p - t*d - d */
        const float kk[8] = { hx, hy, zx, zy, thr, rel, step, 0 }; size_t ko = (size_t)(FG_RING * 32 + (sl * 4 + gk - 1) * 32); uint64_t ka = g_fg.kbuf.gpuAddress + ko; memcpy((char *)g_fg.kbuf.contents + ko, kk, sizeof kk);
        pthread_mutex_lock(&g_fg.mu); id<MTLTexture> preo = g_fg.h[ps]; pthread_mutex_unlock(&g_fg.mu);
        [g_fg.args setAddress:ka atIndex:0]; id<MTLTexture> tt[3] = { src, pre, preo }; fgen_pass(cb, g_fg.comp, g_fg.shiftpso, tt, 3, 0); shifted = 1;
    } else if (noint) fgen_pass(cb, out, g_fg.cppso, &cur, 1, 0); else {
    fi.colorTexture = cur; fi.prevColorTexture = prev; fi.depthTexture = dpt; fi.motionTexture = mvt; fi.outputTexture = out;
    fi.motionVectorScaleX = fi.motionVectorScaleY = 1; fi.deltaTime = (float)(period > 0 ? period : 1 / 60.0);
    fi.nearPlane = 0.1f; fi.farPlane = 1000; fi.fieldOfView = 60; fi.aspectRatio = (float)g_fg.w3 / (float)g_fg.h3;   // 正交相机：镜头只平移，这几项不参与 | orthographic camera, translation only: these do not matter
    fi.jitterOffsetX = fi.jitterOffsetY = 0; fi.depthReversed = NO; fi.shouldResetHistory = g_fg.last_mid + 1 != frame;
    [fi encodeToCommandBuffer:cb]; }
    // 合成：本帧最终画面（src）与本帧放大目标（pre）逐像素不同处取最终画面，其余取插值放大 × 遮罩 | composite: final picture where it differs from the upsample target, else the upscaled interpolation x mask
    id<MTLTexture> tx[4] = { src, pre, out, mk };
    if (shifted) fx = nil;
    else if (fx && fxin) {   // 真帧走了 MetalFX 空间超分：插值帧也走，否则一锐一软交替 | the real frame used MetalFX spatial: so does the interpolated one, otherwise sharp / soft alternate
        fgen_pass(cb, fxin, g_fg.cppso, &out, 1, 1);   // 等插值器的栅栏、拷进超分输入（超分只认队列屏障，不认插值器的栅栏） | wait for the interpolator's fence and copy into the scaler input (the scaler only honours queue barriers)
        if ([fx encodeScaleOnly:cb input:fxin contentWidth:g_fg.w3 contentHeight:g_fg.h3]) {
            id<MTLTexture> t2[4] = { src, pre, fx.scaledTexture, mk }; fgen_pass(cb, g_fg.comp, g_fg.pso2, t2, 4, 0);
            { static int once; if (!once++) MLOG("帧生成：插值帧放大走 MetalFX 空间超分（与真帧一致）"); }
        } else { static int once; if (!once++) MLOG("【红】帧生成：插值帧的空间超分编码失败，改用原版放大公式"); fx = nil; } }
    if (!shifted && !fx) {
        if (g_fg.e[ei].mfx) { static int once; if (!once++) MLOG("帧生成：真帧走 MetalFX 空间超分、插值帧没有可用的超分，用原版放大公式（会比真帧软）"); }
        [g_fg.args setAddress:kaddr atIndex:0];
        fgen_pass(cb, g_fg.comp, g_fg.pso, tx, 4, 1); }
    g_fg.last_mid = frame; g_fg.n_mid++;
    if (g_fg.n_mid % 600 == 1) MLOG("帧生成：插值 至帧 %llu：插值帧 %llu，只出真帧 无条目 %llu / 镜头跳变或历史断 %llu / 历史已覆盖 %llu / 尺寸不符 %llu / 插值器未就绪 %llu / 历史环槽还被占 %llu；真帧里镜头平移 %llu 帧，最大 %.2f 3D 像素",
        (unsigned long long)frame, (unsigned long long)g_fg.n_mid, (unsigned long long)g_fg.n_skip[FGS_NOENTRY], (unsigned long long)g_fg.n_skip[FGS_NOTOK], (unsigned long long)g_fg.n_skip[FGS_STALE],
        (unsigned long long)g_fg.n_skip[FGS_SIZE], (unsigned long long)g_fg.n_skip[FGS_NOTREADY], (unsigned long long)g_fg.n_busy,
        (unsigned long long)__atomic_load_n(&g_fg.n_move, __ATOMIC_RELAXED), __atomic_load_n(&g_fg.mv_max_c, __ATOMIC_RELAXED) / 100.0);
    return 1;
}
// 诊断（只在 DM_FG_SHOT 存图那一帧跑一次）：查插值帧发软出在哪一步。同一个上屏命令缓冲里另出五张（8 位，可存 PNG）：
//   0 插值器原始输出（3D 尺寸）  1 本帧 3D 画面  2 上一帧 3D 画面  3 合成趟绕过插值器（直接放大本帧 3D 画面）  4 另建一个 RGBA16Float 插值器的输出（3D 尺寸）
// Diagnostics (run once, on the DM_FG_SHOT frame only): find which step makes interpolated frames soft. Five extra 8-bit pictures in the same present command buffer:
//   0 raw interpolator output (3D size)  1 this frame's 3D picture  2 previous 3D picture  3 composite pass bypassing the interpolator  4 output of a second RGBA16Float interpolator
static int fgen_diag(CBUF cb, int ei, id<MTLTexture> src, __strong id<MTLTexture> d[5]) {
    id<MTLTexture> cur, prev, pre, mk, out; float kk[8], mvx, mvy;
    pthread_mutex_lock(&g_fg.mu); int sl = g_fg.e[ei].slot, ps = g_fg.e[ei].prev;
    cur = g_fg.c3[sl]; prev = g_fg.c3[ps]; pre = g_fg.h[sl]; mk = g_fg.mk[sl]; out = g_fg.out; memcpy(kk, g_fg.k[sl], sizeof kk); mvx = g_fg.e[ei].mvx; mvy = g_fg.e[ei].mvy;
    pthread_mutex_unlock(&g_fg.mu);
    if (!cur || !prev || !pre || !mk || !out) return 0;
    uint64_t t0 = now_ns(); NSError *err = nil;
    id<MTLLibrary> lib = [g_dev newLibraryWithSource:@(k_fg_msl) options:nil error:&err];
    MTLRenderPipelineDescriptor *pd = [MTLRenderPipelineDescriptor new]; pd.vertexFunction = [lib newFunctionWithName:@"fg_vs"]; pd.fragmentFunction = [lib newFunctionWithName:@"fg_cv"];
    pd.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm; id<MTLRenderPipelineState> pso8 = lib ? [g_dev newRenderPipelineStateWithDescriptor:pd error:&err] : nil;
    id<MTLBuffer> kb = [g_dev newBufferWithLength:32 options:MTLResourceStorageModeShared];
    if (!pso8 || !kb || !g_fg.cvpso) { MLOG("【红】帧生成诊断：管线建不出 %s", err.localizedDescription.UTF8String ?: ""); return 0; }
    memcpy(kb.contents, kk, 32); res_add(kb); fgen_retire(kb, 0);
    const MTLTextureUsage rt = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget;
    for (int i = 0; i < 5; i++) d[i] = i == 3 ? fgen_tex(src.pixelFormat, src.width, src.height, rt, nil) : fgen_tex(MTLPixelFormatRGBA8Unorm, g_fg.w3, g_fg.h3, rt, nil);
    id<MTLTexture> a16 = fgen_tex(MTLPixelFormatRGBA16Float, g_fg.w3, g_fg.h3, rt, nil), b16 = fgen_tex(MTLPixelFormatRGBA16Float, g_fg.w3, g_fg.h3, rt, nil);
    id<MTLTexture> o16 = fgen_tex(MTLPixelFormatRGBA16Float, g_fg.w3, g_fg.h3, MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite | MTLTextureUsageRenderTarget, nil);
    if (!d[0] || !d[1] || !d[2] || !d[3] || !d[4] || !a16 || !b16 || !o16) return 0;
    res_commit();
    fgen_pass(cb, d[0], pso8, &out, 1, 1); fgen_pass(cb, d[1], pso8, &cur, 1, 0); fgen_pass(cb, d[2], pso8, &prev, 1, 0);
    id<MTLTexture> tx[4] = { src, pre, cur, mk }; [g_fg.args setAddress:kb.gpuAddress atIndex:0]; fgen_pass(cb, d[3], g_fg.pso, tx, 4, 0);
    // 半精度插值器：同一对输入转成 RGBA16Float，先重置再正式插一次 | half-float interpolator: same inputs converted to RGBA16Float, reset once then the real call
    MTLFXFrameInterpolatorDescriptor *fd = [MTLFXFrameInterpolatorDescriptor new];
    fd.colorTextureFormat = fd.outputTextureFormat = MTLPixelFormatRGBA16Float; fd.depthTextureFormat = MTLPixelFormatDepth32Float; fd.motionTextureFormat = MTLPixelFormatRG16Float;
    fd.inputWidth = g_fg.mv.width; fd.inputHeight = g_fg.mv.height; fd.outputWidth = g_fg.w3; fd.outputHeight = g_fg.h3;
    id<MTL4Compiler> c = [g_dev newCompilerWithDescriptor:[MTL4CompilerDescriptor new] error:&err];
    id<MTL4FXFrameInterpolator> f16 = c ? [fd newFrameInterpolatorWithDevice:g_dev compiler:c] : nil;
    if (f16) {
        fgen_pass(cb, a16, g_fg.cvpso, &cur, 1, 0); fgen_pass(cb, b16, g_fg.cvpso, &prev, 1, 0);
        f16.fence = g_fg.fence;
        for (int k = 0; k < 2; k++) {
            f16.colorTexture = a16; f16.prevColorTexture = b16; f16.depthTexture = g_fg.dep; f16.motionTexture = g_fg.mv; f16.outputTexture = o16;
            f16.motionVectorScaleX = f16.motionVectorScaleY = 1; f16.deltaTime = 1 / 60.0f; f16.nearPlane = 0.1f; f16.farPlane = 1000; f16.fieldOfView = 60;
            f16.aspectRatio = (float)g_fg.w3 / (float)g_fg.h3; f16.jitterOffsetX = f16.jitterOffsetY = 0; f16.depthReversed = NO; f16.shouldResetHistory = k == 0;
            [f16 encodeToCommandBuffer:cb]; }
        fgen_pass(cb, d[4], pso8, &o16, 1, 1);
        fgen_retire(f16, 1);
    } else MLOG("【红】帧生成诊断：RGBA16Float 插值器建不出");
    fgen_retire(a16, 0); fgen_retire(b16, 0); fgen_retire(o16, 0);
    MLOG("帧生成诊断：已编进本帧（运动矢量 %.2f,%.2f 3D 像素，3D %lux%lu 格式 %lu，深度 / 运动 %dx%d，锐化 %.3f），准备用时 %.0f ms", mvx, mvy,
        (unsigned long)g_fg.w3, (unsigned long)g_fg.h3, (unsigned long)g_fg.fmt3, g_fg.iw, g_fg.ih, kk[2], (now_ns() - t0) / 1e6);
    return 1;
}
// 切后台（dm_gfx_suspend → mtl_drain，游戏线程已被挡住、翻译队列与出图都已清空，不会有人同时碰 g_fg）：放掉帧生成的对象，后台不占这部分内存；回前台第一次放大绘制时 fgen_alloc 重建（插值器第二次建约几 ms）
// Going to the background (dm_gfx_suspend -> mtl_drain: game threads are blocked, the translation queue and the present are drained, nobody else touches g_fg): release frame generation's objects so the background does not hold this memory; the first upsample draw back in the foreground rebuilds them in fgen_alloc
static void fgen_suspend_free(void) {
    if (!g_fg.h[0]) return;
    fgen_free(); g_fg.have_last = 0; int left = fgen_reap(); defer_drain();
    MLOG("帧生成：切后台，历史环与插值器已释放（还挂着 %d 个）", left);
}
#else
static void fgen_draw_begin(void) {}
static void fgen_after_upsample(NvProg *pr, int mfx) { (void)pr; (void)mfx; }
static void fgen_suspend_free(void) {}
static int fgen_effective(void) { return 0; }
int mtl_fg_backoff(void) { return 0; }
#endif
// 显示帧率（ProMotion 表态、出图节拍模式用）= 渲染帧率 × 2（帧生成生效时） | Display rate (ProMotion declaration, present link mode) = render rate x 2 while frame generation is in effect
int mtl_display_fps(void) { int f = mtl_fps_target(); return fgen_effective() ? f * dm_fg_mult() : f; }
// 设备支持（宿主 dm_custom.c 弱定义的强定义）：Metal 4 包 + iOS（含 Mac 上跑的 iOS 包）+ 插值器支持 | Device support (strong definition of the host's weak one): Metal 4 build + iOS (incl. the iOS app on a Mac) + interpolator support
int mtl_fg_supported(void) {
#if DM_NVN_M3 || !TARGET_OS_IPHONE
    return 0;
#else
    static _Atomic int s = -1; int v = atomic_load(&s);
    if (v < 0 && g_dev) { v = [MTLFXFrameInterpolatorDescriptor supportsMetal4FX:g_dev] ? 1 : 0; atomic_store(&s, v); MLOG("帧生成：设备%s支持 MetalFX 帧插值（Metal 4）", v ? "" : "不"); }
    return v > 0;
#endif
}
