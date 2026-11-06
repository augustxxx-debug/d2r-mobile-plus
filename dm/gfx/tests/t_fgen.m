// 帧生成（实验性）：开关关着时完全无副作用（B 级图形改动的证据），以及资源审查里的释放、槽位占用与退役。
// 生产 nvn_fgen.inc（经 nvn_mtl.m）+ 真 Metal 设备；后台建插值器是真建（本机不支持 MetalFX 帧插值时跳过那几节）。
// Frame generation (experimental): no side effects at all while the switch is off (evidence for a B-level graphics change), plus the release, slot occupancy and retirement from the resource review.
// Production nvn_fgen.inc (via nvn_mtl.m) + a real Metal device; the background interpolator build is real (those sections are skipped when this machine lacks MetalFX frame interpolation).
#define FGEN_TEST 1
#include "../nvn/nvn_mtl.m"
static CAMetalLayer *g_test_layer_obj;   // 第 10 节出图调用点测试用的图层 | layer used by the present call-site tests in section 10
void *front_metal_layer(void) { return (__bridge void *)g_test_layer_obj; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
static int g_on;
int dm_fg_setting(void) { return g_on; }   // 代替宿主 dm_custom.c 的设置判断 | stands in for the host's dm_custom.c setting check
static int fail __attribute__((unused)), count __attribute__((unused));   // 旧接口版这两个不用（tests_m3 -Werror，B211）| unused in the older-API pass
#define EXPECT(name, want, got) do { long long w_ = (long long)(want), g_ = (long long)(got); count++; printf("%s 期望 %lld → 真结果 %lld %s\n", name, w_, g_, w_ == g_ ? "✓" : "✗"); fail += w_ != g_; } while (0)
#if DM_NVN_M3
int main(void) { printf("SKIP：旧接口版没有帧生成\n"); return 77; }
#else
static uint64_t g_f = 5000;
static id<MTLTexture> mk(MTLPixelFormat f, NSUInteger w, NSUInteger h, MTLTextureUsage u) {
    MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:f width:w height:h mipmapped:NO]; d.usage = u; d.storageMode = MTLStorageModePrivate;
    id<MTLTexture> t = [g_dev newTextureWithDescriptor:d]; res_add(t); return t;
}
static int nothing_held(void) {   // 帧生成持有的每帧 / 每尺寸对象全空 | every per-frame / per-size object held by frame generation is empty
    int n = 0; for (int i = 0; i < FG_RING; i++) n += !!g_fg.h[i] + !!g_fg.c3[i] + !!g_fg.mk[i];
    n += !!g_fg.cv[0] + !!g_fg.cv[1] + !!g_fg.dep + !!g_fg.mv + !!g_fg.out + !!g_fg.comp + !!g_fg.fxin + !!g_fg.fx + !!g_fg.fi;
    return n == 0;
}
static int wait_built(void) {   // 后台建插值器：等到 4（建好待配贴图）/ 3（建不出）/ 0（被关掉丢弃） | background build: wait for 4 / 3 / 0
    for (int i = 0; i < 400; i++) { int s = atomic_load(&g_fg.fi_state); if (s != 1) return s; usleep(50000); }
    return atomic_load(&g_fg.fi_state);
}
static NvProg *upsample_prog(void) {   // 键与生产的原版放大绘制相同：开关漏判时 fgen_after_upsample 会往下走 | same key as the production upsample draw: if the switch check leaked, fgen_after_upsample would go on
    NvProg *p = [NvProg new]; p->sh[1] = [NvShader new]; strcpy(p->sh[1]->key, "52aac1f2306b8fc5"); return p;
}
static NSMutableArray *g_test_keep;   // 让提交的分配器 / 命令缓冲活到完成回调之后 | keep submitted allocators / command buffers alive past the feedback
static uint64_t pq_done_read(void) { pthread_mutex_lock(&g_pq_mu); uint64_t d = g_pq_done; pthread_mutex_unlock(&g_pq_mu); return d; }
static int wait_pq_done(uint64_t t, int ms) { for (int i = 0; i <= ms; i += 2) { if (pq_done_read() >= t) return 1; usleep(2000); } return pq_done_read() >= t; }
static void test_hold_fb(int on) {   // 故障注入：攥住 / 放行生成帧的完成回调 | fault injection: hold / release generated-frame feedbacks
    atomic_store(&g_fgen_test_hold_fb, on);
    pthread_mutex_lock(&g_fgen_test_fb_mu); pthread_cond_broadcast(&g_fgen_test_fb_cv); pthread_mutex_unlock(&g_fgen_test_fb_mu);
}
static int gen_commit_hold(uint64_t t) {   // 真的提交一张生成帧命令缓冲（走生产同一段登记与完成回调）；回调被攥住 = 仍在 GPU 上 | really commit a generated-frame command buffer (same production registration and feedback path); held feedback = still on the GPU
    if (!g_test_keep) g_test_keep = [NSMutableArray array];
    id<MTL4CommandAllocator> a = [g_dev newCommandAllocator]; CBUF cb = [g_dev newCommandBuffer];
    if (!a || !cb) return 0;
    [cb beginCommandBufferWithAllocator:a]; [cb endCommandBuffer];
    [g_test_keep addObject:a]; [g_test_keep addObject:cb];
    fgen_mid_submit(cb, t, nil, 0, 0, 0, CACurrentMediaTime());
    return 1;
}
static tex_t g_pres_t; static id<MTLTexture> g_pres_tex;   // 上屏的交换链贴图（与 g_fg 的出图尺寸 / 格式一致） | the presented swapchain texture (same output size / format as g_fg)
static void present_begin(uint64_t frame) {   // 摆好出图线程这一帧的条目与历史环槽（同第 3 节） | set up the present thread's entry and history slots for this frame (as in section 3)
    mtl_set_frame(frame);
    if (!g_pres_tex) {
        g_pres_t = (tex_t){.serial = 30001}; g_pres_t.d.width = 320; g_pres_t.d.height = 160; g_pres_t.d.depth = g_pres_t.d.samples = g_pres_t.d.levels = 1;
        g_pres_t.d.target = NVN_TEXTURE_TARGET_2D; g_pres_t.d.format = NVN_FORMAT_RGBA8;
        MTLTextureDescriptor *pd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:320 height:160 mipmapped:NO];
        pd.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead; pd.storageMode = MTLStorageModeShared;
        g_pres_tex = [g_dev newTextureWithDescriptor:pd]; res_add(g_pres_tex);
        uint8_t *px = calloc(320 * 160 * 4, 1);   // 全黑：没写过的显存在 Apple GPU 上读出来是品红，会误触紫闪探针 | all black: never-written memory reads back as magenta on Apple GPUs and would trip the magenta probe
        [g_pres_tex replaceRegion:MTLRegionMake2D(0, 0, 320, 160) mipmapLevel:0 withBytes:px bytesPerRow:320 * 4]; free(px);
        serial_put(g_pres_t.serial, g_pres_tex); res_commit();
    }
    int ei = (int)(frame % FG_RING), sl = 1, ps = 0;
    pthread_mutex_lock(&g_fg.mu);
    g_fg.e[ei] = (typeof(g_fg.e[0])){ .frame = frame, .ser = g_pres_t.serial, .slot = sl, .prev = ps, .ok = 1 };
    g_fg.h_frame[sl] = frame; g_fg.h_frame[ps] = frame - 1;
    pthread_mutex_unlock(&g_fg.mu);
}
int main(void) { @autoreleasepool {
    setvbuf(stdout, NULL, _IONBF, 0); unsetenv("DM_FG_SHOT"); unsetenv("DM_FG_DUMP"); mtl_init();
    NvProg *pr = upsample_prog(); int ei = -1;

    // 1. 关着：显示帧率 = 渲染帧率；每帧走一遍生产挂钩，什么都不分配、不计数 | 1. off: display rate = render rate; run the production hooks every frame, nothing allocated or counted
    g_on = 0;
    EXPECT("关着：显示帧率等于渲染帧率", mtl_fps_target(), mtl_display_fps());
    NSUInteger mem0 = g_dev.currentAllocatedSize; int mids = 0;
    for (int i = 0; i < 120; i++) { mtl_set_frame(++g_f); fgen_draw_begin(); fgen_after_upsample(pr, 0); fgen_after_upsample(pr, 1); mids += fgen_mid_entry(g_f, 1, nil, NULL, g_f, &ei); }
    EXPECT("关着：120 帧里接手插值帧的次数", 0, mids);
    EXPECT("关着：没有持有任何贴图 / 插值器 / 超分", 1, nothing_held());
    EXPECT("关着：常量缓冲、参数表、管线都没建", 0, !!g_fg.kbuf + !!g_fg.args + !!g_fg.pso + !!g_fg.pso2 + !!g_fg.cppso + !!g_fg.cvpso + !!g_fg.fence);
    EXPECT("关着：插值器状态", 0, atomic_load(&g_fg.fi_state));
    EXPECT("关着：过了开关检查往下做事的挂钩次数", 0, (long long)g_fg.n_work);
    EXPECT("关着：插值帧 / 跳过 / 槽位占用 / 镜头平移计数", 0, (long long)(g_fg.n_mid + g_fg.n_skip[0] + g_fg.n_skip[1] + g_fg.n_skip[2] + g_fg.n_skip[3] + g_fg.n_skip[4] + g_fg.n_busy + g_fg.n_jump + g_fg.n_move + g_fg.mv_max_c));
    EXPECT("关着：设备显存分配没变", (long long)mem0, (long long)g_dev.currentAllocatedSize);
    EXPECT("关着：退役单子是空的", 0, fgen_reap());

    // 内存退避（纯函数）：跌破 500 MB 或系统告警就停；退避期过了且余量够「线 + 自身占用」才重开；读不到余量（0）不因余量退避 | memory back-off (pure): stop below 500 MB or on a warning; restart after the period with margin for line + own use; unreadable margin (0) does not back off on margin
    { uint64_t u = 0;
      EXPECT("退避：余量够不退避", 0, fgen_mem_step(0, 900, 0, 100, &u)); EXPECT("退避：读不到余量不退避", 0, fgen_mem_step(0, 0, 0, 100, &u));
      EXPECT("退避：跌破线就停", 1, fgen_mem_step(0, 499, 0, 100, &u)); EXPECT("退避：60 秒", 160, (long long)u);
      EXPECT("退避：期内余量回来也不重开", 1, fgen_mem_step(1, 900, 0, 159, &u)); EXPECT("退避：期满余量不够不重开", 1, fgen_mem_step(1, 629, 0, 160, &u)); EXPECT("退避：期满余量够就重开", 0, fgen_mem_step(1, 630, 0, 160, &u));
      EXPECT("退避：告警就停", 1, fgen_mem_step(0, 2000, 1, 200, &u)); EXPECT("退避：告警 300 秒", 500, (long long)u); EXPECT("退避：期内再告警顺延", 1, fgen_mem_step(1, 2000, 1, 400, &u)); EXPECT("退避：顺延到", 700, (long long)u); }

    // 帧率退避（纯函数）：插值没真跑不算；连续两窗低于线才退 | frame-rate back-off (pure): only counts while interpolation really runs; two slow windows in a row
    EXPECT("帧率退避：够快清零", 0, fgen_slow_step(1, 50, 300, 0)); EXPECT("帧率退避：低帧加一", 2, fgen_slow_step(1, 44, 300, 0)); EXPECT("帧率退避：菜单 / 载入不算", 0, fgen_slow_step(1, 20, 100, 0)); EXPECT("帧率退避：有卡顿的窗口不算", 0, fgen_slow_step(2, 20, 300, 1));

    if (![MTLFXFrameInterpolatorDescriptor supportsMetal4FX:g_dev]) { printf("SKIP（2–6 节）：本机不支持 MetalFX 帧插值（Metal 4）\n"); printf("== %d 项，失败 %d ==\n", count, fail); return fail ? 1 : 0; }

    // 原型 DM_FG_SHIFT：不建插值器也能就绪，关掉放干净（只在手动设这个环境变量时跑）| prototype DM_FG_SHIFT: ready without an interpolator, released on off (only when the variable is set by hand)
    if (fgen_shift_on()) { g_on = 1; mtl_set_frame(++g_f); fgen_on();
        id<MTLTexture> dt = mk(MTLPixelFormatRGBA8Unorm, 640, 320, MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead), st = mk(MTLPixelFormatRG11B10Float, 512, 256, MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead), mt = mk(MTLPixelFormatR8Unorm, 512, 256, MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead);
        g_fg.iw = 512; g_fg.ih = 256; EXPECT("平移：建历史环", 1, fgen_alloc(dt, st, mt)); EXPECT("平移：后台建好", 4, wait_built()); fgen_ready(); EXPECT("平移：就绪", 2, atomic_load(&g_fg.fi_state)); EXPECT("平移：没建插值器", 0, !!g_fg.fi); EXPECT("平移：管线在", 1, !!g_fg.shiftpso);
        g_on = 0; mtl_set_frame(++g_f); fgen_on(); EXPECT("平移：关掉后放干净", 1, nothing_held());
        pthread_mutex_lock(&g_pq_mu); g_pq_done = g_pq_in; pthread_mutex_unlock(&g_pq_mu); fgen_reap(); printf("== %d 项，失败 %d ==\n", count, fail); return fail ? 1 : 0; }
    // 2. 打开：显示帧率翻倍；按 3D / 出图尺寸建历史环、后台建插值器与插值帧的空间超分 | 2. on: display rate doubled; rings by 3D / output size, background interpolator and spatial upscaler
    g_on = 1; mtl_set_frame(++g_f); EXPECT("打开：本帧锁存为开", 1, fgen_on());
    EXPECT("打开：显示帧率 = 渲染帧率 × 2", mtl_fps_target() * 2, mtl_display_fps());
    id<MTLTexture> dst = mk(MTLPixelFormatRGBA8Unorm, 320, 160, MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead);
    id<MTLTexture> src = mk(MTLPixelFormatRG11B10Float, 256, 128, MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead);
    id<MTLTexture> msk = mk(MTLPixelFormatR8Unorm, 256, 128, MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead);
    g_fg.iw = 256; g_fg.ih = 128;
    EXPECT("打开：建历史环", 1, fgen_alloc(dst, src, msk));
    EXPECT("打开：后台建好插值器（4 = 待配贴图）", 4, wait_built()); fgen_ready();
    EXPECT("打开：就绪", 2, atomic_load(&g_fg.fi_state));
    EXPECT("打开：插值帧的空间超分建好（3D 比出图小）", 1, g_fg.fx != nil);
    EXPECT("打开：历史环与输出都在", 1, g_fg.h[FG_RING - 1] && g_fg.c3[FG_RING - 1] && g_fg.mk[FG_RING - 1] && g_fg.out && g_fg.comp && g_fg.fxin && g_fg.kbuf);
    EXPECT("打开：设备显存分配增加", 1, g_dev.currentAllocatedSize > mem0);
    EXPECT("打开：运动矢量贴图宽 = 3D 宽 256 / 4（常量场，插值器内部缓冲跟着缩）", 64, (long long)g_fg.mv.width);
    EXPECT("打开：常量深度贴图高 = 3D 高 128 / 4", 32, (long long)g_fg.dep.height);
    EXPECT("打开：插值帧超分的放大结果是出图尺寸", 320, (long long)g_fg.fx.scaledTexture.width);

    // 3. 出图接手一帧：两个槽被这张出图命令缓冲占用；做完之前翻译线程不许改写 | 3. the present takes a frame: both slots are held by its command buffer; the translation thread may not overwrite them before it completes
    int sl = 1, ps = 0; uint64_t F = ++g_f; mtl_set_frame(F);
    pthread_mutex_lock(&g_fg.mu); ei = (int)(F % FG_RING);
    g_fg.e[ei] = (typeof(g_fg.e[0])){ .frame = F, .ser = 7, .slot = sl, .prev = ps, .ok = 1 }; g_fg.h_frame[sl] = F; g_fg.h_frame[ps] = F - 1;
    pthread_mutex_unlock(&g_fg.mu);
    pthread_mutex_lock(&g_pq_mu); uint64_t done0 = g_pq_done; pthread_mutex_unlock(&g_pq_mu);
    uint64_t T = done0 + 3; int got = -1;
    // B179：折叠屏的交换链按最大屏预留（比历史环大），画面在左上角按裁切上屏：裁切从 (0,0) 起且正好是历史环尺寸才接；别的偏移 / 尺寸 / 格式照旧拒 | B179: a foldable's swapchain is reserved for the largest screen (bigger than the ring), the picture is presented from the top-left by crop: accepted only when the crop starts at (0,0) at exactly the ring size; other offsets / sizes / formats are still refused
    { id<MTLTexture> big = mk(MTLPixelFormatRGBA8Unorm, 352, 176, MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead), bigf = mk(MTLPixelFormatBGRA8Unorm, 352, 176, MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead);
      const int c0[4] = { 0, 0, 320, 160 }, c1[4] = { 8, 0, 320, 160 }, c2[4] = { 0, 0, 300, 160 };
      EXPECT("B179 同尺寸不看裁切", 1, fgen_src_fits(dst, NULL, g_fg.w, g_fg.hgt, g_fg.fmt));
      EXPECT("B179 大贴图 + 裁切 0,0 历史环尺寸：接", 1, fgen_src_fits(big, c0, g_fg.w, g_fg.hgt, g_fg.fmt));
      EXPECT("B179 大贴图没给裁切：拒", 0, fgen_src_fits(big, NULL, g_fg.w, g_fg.hgt, g_fg.fmt));
      EXPECT("B179 裁切有偏移：拒", 0, fgen_src_fits(big, c1, g_fg.w, g_fg.hgt, g_fg.fmt));
      EXPECT("B179 裁切尺寸不是历史环：拒", 0, fgen_src_fits(big, c2, g_fg.w, g_fg.hgt, g_fg.fmt));
      EXPECT("B179 格式不同：拒", 0, fgen_src_fits(bigf, c0, g_fg.w, g_fg.hgt, g_fg.fmt));
      EXPECT("B179 比历史环小：拒", 0, fgen_src_fits(src, c0, g_fg.w, g_fg.hgt, g_fg.fmt)); }
    EXPECT("出图接手：条目齐全时接手", 1, fgen_mid_entry(F, 7, dst, NULL, T, &got));
    EXPECT("出图接手：本帧槽记上票号", (long long)T, (long long)g_fg.busy[sl]);
    EXPECT("出图接手：上一帧槽记上票号", (long long)T, (long long)g_fg.busy[ps]);
    EXPECT("票号没做完：本帧槽算占用", 1, fgen_slot_busy(sl, NULL, NULL));
    EXPECT("票号没做完：上一帧槽算占用", 1, fgen_slot_busy(ps, NULL, NULL));
    EXPECT("没被接手过的槽不算占用", 0, fgen_slot_busy(2, NULL, NULL));
    pthread_mutex_lock(&g_pq_mu); g_pq_done = T; pthread_mutex_unlock(&g_pq_mu);
    EXPECT("票号做完：本帧槽放开", 0, fgen_slot_busy(sl, NULL, NULL));

    // 4. 退役：出图还没做完前一直挂着，做完才交给 defer_* | 4. retirement: parked until the present completes, only then handed to defer_*
    id<MTLBuffer> probe = [g_dev newBufferWithLength:64 options:MTLResourceStorageModeShared]; res_add(probe);
    pthread_mutex_lock(&g_pq_mu); g_pq_in = g_pq_done + 2; pthread_mutex_unlock(&g_pq_mu);
    fgen_retire(probe, 0); probe = nil;
    EXPECT("退役：出图没做完，还挂着", 1, fgen_reap());
    pthread_mutex_lock(&g_pq_mu); g_pq_done = g_pq_in; pthread_mutex_unlock(&g_pq_mu);
    EXPECT("退役：出图做完，交出去", 0, fgen_reap());

    // 5. 关掉：历史环、输出、插值器、超分全部退役；之后的帧照「关着」处理 | 5. off: rings, outputs, interpolator and scaler all retired; later frames behave as "off"
    pthread_mutex_lock(&g_pq_mu); g_pq_in = g_pq_done + 2; pthread_mutex_unlock(&g_pq_mu);   // 关掉那一刻还有一张出图在途 | one present still in flight when switching off
    g_on = 0; mtl_set_frame(++g_f); fgen_draw_begin();
    EXPECT("关掉：本帧锁存为关", 0, g_fg.on);
    EXPECT("关掉：没有持有任何贴图 / 插值器 / 超分", 1, nothing_held());
    EXPECT("关掉：插值器状态回到 0", 0, atomic_load(&g_fg.fi_state));
    EXPECT("关掉：槽位占用清零", 0, (long long)(g_fg.busy[0] + g_fg.busy[1] + g_fg.busy[2] + g_fg.busy[3]));
    EXPECT("关掉：退役对象挂在单子上（等出图做完）", 1, fgen_reap() > 0);
    pthread_mutex_lock(&g_pq_mu); g_pq_done = g_pq_in; pthread_mutex_unlock(&g_pq_mu);
    EXPECT("关掉：出图做完后单子清空", 0, fgen_reap());
    EXPECT("关掉：显示帧率回到渲染帧率", mtl_fps_target(), mtl_display_fps());
    uint64_t w5 = g_fg.n_work;
    for (int i = 0; i < 30; i++) { mtl_set_frame(++g_f); fgen_draw_begin(); fgen_after_upsample(pr, 0); fgen_mid_entry(g_f, 7, dst, NULL, g_f, &got); }
    EXPECT("关掉后 30 帧：挂钩没往下做事", 0, (long long)(g_fg.n_work - w5));
    EXPECT("关掉后：接手插值帧", 0, fgen_mid_entry(g_f, 7, dst, NULL, g_f, &got));

    // 6. 后台还在建时就关掉：建好的插值器 / 超分被丢弃，不复活 | 6. switched off while the background build is running: what it built is discarded, nothing comes back
    // 第 6 节的建造故意拖 300 ms，让第 7 节马上派的建造一定与它重叠：建造队列串行时峰值只能是 1（两次并发建插值器会让 AGX 驱动 dispatch_group_leave 失衡 SIGTRAP）
    // section 6's build is held 300 ms so section 7's immediate build surely overlaps it: with a serial build queue the peak must stay 1 (two concurrent interpolator builds unbalance the AGX driver's dispatch_group_leave → SIGTRAP)
    atomic_store(&g_fgen_test_build_peak, atomic_load(&g_fgen_test_build_live)); atomic_store(&g_fgen_test_build_hold_ms, 300);
    g_on = 1; mtl_set_frame(++g_f); fgen_on();
    EXPECT("再开：建历史环", 1, fgen_alloc(dst, src, msk));
    g_on = 0; mtl_set_frame(++g_f); fgen_on();
    int s6 = wait_built();
    EXPECT("建造中关掉：状态停在 0（建好的被丢弃）", 0, s6);
    EXPECT("建造中关掉：没有持有任何贴图 / 插值器 / 超分", 1, nothing_held());
    pthread_mutex_lock(&g_pq_mu); g_pq_done = g_pq_in; pthread_mutex_unlock(&g_pq_mu);
    EXPECT("建造中关掉：单子清空", 0, fgen_reap());

    // 7. 再开一次能重建（第二次建插值器只要几 ms） | 7. switching on again rebuilds (the second interpolator build takes a few ms)
    g_on = 1; mtl_set_frame(++g_f); fgen_on();
    EXPECT("第三次打开：建历史环", 1, fgen_alloc(dst, src, msk));
    EXPECT("第三次打开：后台建好", 4, wait_built()); fgen_ready();
    atomic_store(&g_fgen_test_build_hold_ms, 0);
    EXPECT("第三次打开：就绪", 2, atomic_load(&g_fg.fi_state));
    EXPECT("建造中关→开：后台建造从不并发（串行队列）", 1, atomic_load(&g_fgen_test_build_peak));
    // 8. 切后台（mtl_drain 调）：开关还开着也放掉全部对象；回前台第一次放大绘制重建，开关状态不变 | 8. going to the background (called by mtl_drain): everything is released although the switch stays on; the first upsample draw back in the foreground rebuilds
    pthread_mutex_lock(&g_pq_mu); g_pq_done = g_pq_in; pthread_mutex_unlock(&g_pq_mu);
    fgen_suspend_free();
    EXPECT("切后台：没有持有任何贴图 / 插值器 / 超分", 1, nothing_held());
    EXPECT("切后台：开关状态不变", 1, g_fg.on);
    EXPECT("切后台：显示帧率仍按开算", mtl_fps_target() * 2, mtl_display_fps());
    fgen_suspend_free();   // 第二次是空操作 | a second call does nothing
    EXPECT("回前台：建历史环", 1, fgen_alloc(dst, src, msk));
    EXPECT("回前台：后台建好", 4, wait_built()); fgen_ready();
    EXPECT("回前台：就绪", 2, atomic_load(&g_fg.fi_state));
    // 9. 生成帧在途：同票真帧取不到 drawable / 编码失败、关闭、暂停时，水位不得越过这一票（P1 回归）
    //    生产里两处「present_now 返回 0」的调用方都改走 pq_real_end_locked，只有本票生成帧的完成回调全部到齐才 pq_finish；
    //    这里真的提交生成帧命令缓冲（完成回调与生产同一段，用测试钩子攥住 = 模拟仍在 GPU 上），再按插值 / 外推的提交顺序核对
    // 9. generated frame in flight: on real-frame drawable failure / encode failure, switch-off or suspend the watermark must not pass the ticket (P1 regression)
    //    Both "present_now returned 0" callers in production now go through pq_real_end_locked and pq_finish only after every generated-frame feedback of the ticket has arrived;
    //    here we really commit generated-frame command buffers (same production feedback path, held by the test hook = still on the GPU) and check the interpolation / extrapolation order
    uint64_t base = pq_done_read();
    test_hold_fb(1);

    // (a) 插值：生成帧先提交，同票真帧 nextDrawable = nil
    uint64_t Ta = base + 1;
    pq_gen_begin(Ta);
    EXPECT("(a) 生成帧已提交", 1, gen_commit_hold(Ta));
    pthread_mutex_lock(&g_pq_mu); g_fg.busy[0] = Ta; g_pq_in = Ta; pthread_mutex_unlock(&g_pq_mu);
    pq_gen_close(Ta);
    pq_present_failed(Ta);   // 真帧 nextDrawable = nil
    EXPECT("(a) 生成帧未完成时水位不越过该票", 0, (long long)(pq_done_read() >= Ta));
    EXPECT("(a) 生成帧未完成时历史环槽仍算占用", 1, fgen_slot_busy(0, NULL, NULL));
    { id<MTLBuffer> probe = [g_dev newBufferWithLength:64 options:MTLResourceStorageModeShared]; res_add(probe); fgen_retire(probe, 0); probe = nil; }
    EXPECT("(a) 生成帧未完成时退役资源不 reap", 1, fgen_reap() > 0);
    test_hold_fb(0);
    EXPECT("(a) 生成帧完成后水位前进（不死锁）", 1, wait_pq_done(Ta, 3000));
    EXPECT("(a) 生成帧完成后历史环槽放开", 0, fgen_slot_busy(0, NULL, NULL));
    EXPECT("(a) 生成帧完成后退役资源 reap", 0, fgen_reap());

    // (b) 插值（一票两张生成帧）：同票真帧编码失败
    uint64_t Tb = base + 2;
    test_hold_fb(1);
    pq_gen_begin(Tb);
    EXPECT("(b) 生成帧 1 已提交", 1, gen_commit_hold(Tb));
    EXPECT("(b) 生成帧 2 已提交", 1, gen_commit_hold(Tb));
    pq_gen_close(Tb);
    pq_present_failed(Tb);   // 真帧编码失败
    EXPECT("(b) 生成帧未完成时水位不越过该票", 0, (long long)(pq_done_read() >= Tb));
    test_hold_fb(0);
    EXPECT("(b) 全部生成帧完成后水位前进（不死锁）", 1, wait_pq_done(Tb, 3000));

    // (c) 外推（gen == 3）：真帧先了结、生成帧排在真帧之后；生成帧未完成时关闭
    uint64_t Tc = base + 3;
    test_hold_fb(1);
    pq_gen_begin(Tc);
    pthread_mutex_lock(&g_pq_mu); pq_real_end_locked(Tc); pthread_cond_broadcast(&g_pq_cv); g_fg.busy[1] = Tc; g_pq_in = Tc; pthread_mutex_unlock(&g_pq_mu);   // 真帧完成回调先到 | the real frame's feedback arrives first
    EXPECT("(c) 外推：真帧已了结但生成帧未提交时不越过该票", 0, (long long)(pq_done_read() >= Tc));
    EXPECT("(c) 外推：生成帧提交", 1, gen_commit_hold(Tc));
    pq_gen_close(Tc);
    EXPECT("(c) 外推：生成帧未完成时水位仍不越过该票", 0, (long long)(pq_done_read() >= Tc));
    g_on = 0; mtl_set_frame(++g_f); fgen_on();   // 生成帧未完成时关闭帧生成 | switch frame generation off while the generated frame is unfinished
    EXPECT("(c) 关闭：没有持有任何贴图 / 插值器 / 超分", 1, nothing_held());
    EXPECT("(c) 关闭：退役资源挂在单子上等生成帧", 1, fgen_reap() > 0);
    EXPECT("(c) 关闭：生成帧未完成时水位不越过该票", 0, (long long)(pq_done_read() >= Tc));
    test_hold_fb(0);
    EXPECT("(c) 关闭后生成帧完成：水位前进", 1, wait_pq_done(Tc, 3000));
    EXPECT("(c) 关闭后生成帧完成：退役资源 reap", 0, fgen_reap());
    g_on = 1; mtl_set_frame(++g_f); fgen_on();   // 再开：重建 | switch on again: rebuild
    EXPECT("(c) 再开：建历史环", 1, fgen_alloc(dst, src, msk));
    EXPECT("(c) 再开：后台建好", 4, wait_built()); fgen_ready();
    // (c) 暂停（切后台）：生成帧未完成时暂停再恢复 | suspend (background): pause and resume while a generated frame is unfinished
    uint64_t Td = base + 4;
    test_hold_fb(1);
    pq_gen_begin(Td);
    EXPECT("(c) 暂停：生成帧提交", 1, gen_commit_hold(Td));
    pq_gen_close(Td);
    pq_present_failed(Td);   // 同票真帧没交出去 | the ticket's real frame was never handed over
    pthread_mutex_lock(&g_pq_mu); g_pq_in = Td; pthread_mutex_unlock(&g_pq_mu);
    fgen_suspend_free();
    EXPECT("(c) 暂停：没有持有任何贴图 / 插值器 / 超分", 1, nothing_held());
    EXPECT("(c) 暂停：退役资源挂在单子上等生成帧", 1, fgen_reap() > 0);
    EXPECT("(c) 暂停：生成帧未完成时水位不越过该票", 0, (long long)(pq_done_read() >= Td));
    test_hold_fb(0);
    EXPECT("(c) 暂停后生成帧完成：水位前进", 1, wait_pq_done(Td, 3000));
    EXPECT("(c) 暂停后生成帧完成：退役资源 reap", 0, fgen_reap());
    g_on = 1; mtl_set_frame(++g_f); fgen_on();
    EXPECT("(c) 暂停后恢复：建历史环", 1, fgen_alloc(dst, src, msk));
    EXPECT("(c) 暂停后恢复：后台建好", 4, wait_built()); fgen_ready();
    test_hold_fb(0);
    // 10. 调用点接线：真的走 present_now 与出图线程，真帧失败用 FGEN_TEST 注入（缺省不触发，产品构建没有）
    //     每个调用点的「生成阶段关闭 / 真帧了结」都靠同票生成帧攥在 GPU 上时水位不得前进、放行后必须前进这两条来锁
    // 10. Call-site wiring: really go through present_now and the present thread, injecting real-frame failures under FGEN_TEST (off by default; absent in product builds)
    //     Every call site is locked by "the watermark must not pass the ticket while its generated frame is held on the GPU, and must advance once released"
    { CAMetalLayer *lay = [CAMetalLayer layer]; lay.device = g_dev; lay.pixelFormat = MTLPixelFormatBGRA8Unorm; lay.drawableSize = CGSizeMake(320, 160);
      g_test_layer_obj = lay; }   // 驻留集由 present_now 照生产路径自己挂 | the residency set is attached by present_now itself, as in production
    atomic_store(&g_fgen_test_skip_mid, 1);   // 生成帧的提交由预登记代替（见 10c 说明） | generated-frame submission is replaced by pre-registration (see 10c)

    // (10c) 插值 + 真帧取不到 drawable：由真实出图线程（present_main）走 !submitted 分支。先在票号上登记一张生成帧并攥住其完成回调，
    //       所以「真帧了结」只能让水位前进到这一票之前；分支里写的是 pq_real_end_locked 才对，pq_finish 会让水位提前越过（原 P1）
    // (10c) interpolation + no drawable for the real frame: the real present thread (present_main) runs the !submitted branch. A generated frame is registered on the ticket with its feedback held first,
    //       so "the real frame has ended" must still stop the watermark before the ticket; only pq_real_end_locked in the branch is right, pq_finish would pass it early (the original P1)
    atomic_store(&g_fgen_test_gen, 0); atomic_store(&g_fgen_test_no_drawable, 1); atomic_store(&g_fgen_test_fail_real_encode, 0);
    present_begin(++g_f);
    pthread_mutex_lock(&g_pq_mu); g_pq_read = g_pq_in; uint64_t t_m3 = g_pq_in + 1; pthread_mutex_unlock(&g_pq_mu);
    test_hold_fb(1);
    EXPECT("(10c) 预登记本票生成帧（完成回调攥住 = 仍在 GPU 上）", 1, gen_commit_hold(t_m3));
    pthread_mutex_lock(&g_pq_mu);
    g_pq[g_pq_in % PQ] = (pq_item){ g_pres_t, 1.0 / 60, g_f, {0}, CACurrentMediaTime() };
    uint64_t t_c = ++g_pq_in; pthread_cond_broadcast(&g_pq_cv); pthread_mutex_unlock(&g_pq_mu);
    EXPECT("(10c) 票号连续", (long long)t_m3, (long long)t_c);
    { static int started; if (!started) { started = 1; pthread_t th; if (pthread_create(&th, NULL, present_main, NULL)) MDIE("出图线程起不来"); pthread_detach(th); } }
    int took = 0;
    for (int i = 0; i < 3000; i++) { pthread_mutex_lock(&g_pq_mu); took = g_pq_read >= t_c; int busy = g_classic_busy; pthread_mutex_unlock(&g_pq_mu); if (took && !busy) break; usleep(1000); }
    usleep(100000);   // 等 !submitted 分支登记完 | wait for the !submitted branch to record
    EXPECT("(10c) 出图线程取走该票", 1, took);
    EXPECT("(10c) 生成帧仍在 GPU 上时水位不越过该票（M3 改回 pq_finish 会失败）", 0, (long long)(pq_done_read() >= t_c));
    test_hold_fb(0);
    EXPECT("(10c) 生成帧完成后水位前进", 1, wait_pq_done(t_c, 3000));

    uint64_t cbase = pq_done_read();
    // (10a) 外推（gen == 3）+ 真帧 nextDrawable = nil：present_now 提前返回前必须关生成阶段；调用方按生产写法登记真帧没交出去后水位要能前进
    // (10a) extrapolation (gen == 3) + nextDrawable = nil for the real frame: present_now must close the generated phase before returning early; after the caller records the real frame as never handed over (as production does) the watermark must advance
    present_begin(++g_f);
    atomic_store(&g_fgen_test_gen, 3); atomic_store(&g_fgen_test_no_drawable, 1);
    uint64_t t_a = cbase + 1;
    EXPECT("(10a) 外推真帧取不到 drawable：present_now 返回 0", 0, present_now(&g_pres_t, 1.0 / 60, g_f, t_a, NULL, nil, 0));
    pq_present_failed(t_a);
    EXPECT("(10a) 生成阶段已关：水位前进（M1 去掉 !dr 处的 close 会卡死）", 1, wait_pq_done(t_a, 3000));
    atomic_store(&g_fgen_test_no_drawable, 0);

    // (10b) 外推 + 真帧编码失败：编码失败分支同样必须关生成阶段
    // (10b) extrapolation + real-frame encode failure: the encode-failure branch must close the generated phase too
    present_begin(++g_f);
    atomic_store(&g_fgen_test_fail_real_encode, 1);
    uint64_t t_b = cbase + 2;
    EXPECT("(10b) 外推真帧编码失败：present_now 返回 0", 0, present_now(&g_pres_t, 1.0 / 60, g_f, t_b, NULL, nil, 0));
    pq_present_failed(t_b);
    EXPECT("(10b) 生成阶段已关：水位前进（去掉编码失败处的 close 会卡死）", 1, wait_pq_done(t_b, 3000));
    atomic_store(&g_fgen_test_fail_real_encode, 0);

    // (10d) 外推 + 真帧成功：走到 present_now 末尾，最后一张外推帧提交后必须关生成阶段，否则这一票永远不 finish（死锁）
    // (10d) extrapolation + real frame succeeds: present_now reaches its end, and the generated phase must be closed after the last extrapolated frame is committed, or the ticket never finishes (deadlock)
    present_begin(++g_f);
    uint64_t t_d = cbase + 3;
    EXPECT("(10d) 外推真帧成功：present_now 返回 1", 1, present_now(&g_pres_t, 1.0 / 60, g_f, t_d, NULL, nil, 0));
    EXPECT("(10d) 外推末尾关生成阶段：水位前进（M4 去掉末尾 close 会卡死）", 1, wait_pq_done(t_d, 3000));
    atomic_store(&g_fgen_test_gen, 0); atomic_store(&g_fgen_test_skip_mid, 0);

    g_on = 0; mtl_set_frame(++g_f); fgen_on();
    pthread_mutex_lock(&g_pq_mu); g_pq_done = g_pq_in; pthread_mutex_unlock(&g_pq_mu);
    EXPECT("收尾：关掉后单子清空", 0, fgen_reap());
    printf("== %d 项，失败 %d ==\n", count, fail);
    return fail ? 1 : 0;
} }
#endif
