// GPU 抓帧（DM_GPU_CAPTURE → dm_gfx_capture_at，图形验收链）：生产 gcap_present + 真 Metal 设备，出图次数手动推进。
// 1. 没有请求：出图照常，什么都不发生（不在抓、队列空，B 级「开关不设 0 副作用」）；2. 一次请求：到期那次出图开始抓、下一次出图停，.gputrace 写出来；
// 3. 目标已存在：不覆盖、不开始；4. 队列上限 4 个，多的丢掉；5. 抓帧前 GCAP_RT_LEAD 次出图起光追阴影关掉（rts_on 为 0、整个释放），抓完再开（Metal 抓帧层碰到光追对象会崩）。本机不支持存 .gputrace 时（没带 METAL_CAPTURE_ENABLED）第 2 节改核「请求作废、没在抓」
// GPU capture (DM_GPU_CAPTURE → dm_gfx_capture_at, graphics acceptance chain): production gcap_present + a real Metal device, with the present count advanced by hand.
// 1. No request: presents go on and nothing happens (not capturing, empty queue; level B "no side effect with the switch unset"); 2. one request: capturing starts at the due present and stops at the next, the .gputrace is written;
// 3. the target exists: not overwritten, not started; 4. the queue holds 4, extra requests are dropped; 5. ray-traced shadows go off (rts_on 0, fully released) GCAP_RT_LEAD presents before capturing and come back afterwards (the Metal capture layer crashes on ray-tracing objects). Where .gputrace is unsupported (no METAL_CAPTURE_ENABLED) section 2 checks "request dropped, not capturing" instead
#include "../nvn/nvn_mtl.m"
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
static int fail, count;
#define EXPECT(name, want, got) do { long long w_=(long long)(want), g_=(long long)(got); count++; printf("%s 期望 %lld → 真结果 %lld %s\n", name, w_, g_, w_==g_?"✓":"✗"); fail += w_ != g_; } while (0)
static void present(void) { g_present_n++; gcap_present(); }   // 出图线程每次出图后的那一步 | the step the present thread takes after every present
static void gpu_work(id<MTLBuffer> b) { mtl_sub_begin(); [blit() fillBuffer:b range:NSMakeRange(0, b.length) value:0x5a]; uint64_t f = mtl_sub_commit(); mtl_wait(f, 5000000000ull); }
int main(void) { @autoreleasepool {
    setvbuf(stdout, NULL, _IONBF, 0); setenv("METAL_CAPTURE_ENABLED", "1", 1); mtl_init();
    MTLCaptureManager *cm = [MTLCaptureManager sharedCaptureManager];
#if !DM_NVN_M3
    unsetenv("DM_RT_SHADOW"); rts_on(); g_rts.on = 1; int rt_gate = g_rts.gate;   // 光追阴影开着（本机可用时） | ray-traced shadows on (where the machine supports them)
#else
    int rt_gate = 0;
#endif
    id<MTLBuffer> b = [g_dev newBufferWithLength:65536 options:MTLResourceStorageModePrivate]; res_add(b);
    // 1. 没有请求 | 1. no request
    for (int i = 0; i < 5; i++) { present(); gpu_work(b); }
    EXPECT("没有请求：没在抓", 0, cm.isCapturing); EXPECT("没有请求：队列空", 0, g_ngcap); EXPECT("没有请求：不关光追", 0, g_gcap_on);
#if !DM_NVN_M3
    if (rt_gate) EXPECT("没有请求：光追照常开", 1, rts_on());
#endif
    char dir[512]; snprintf(dir, sizeof dir, "%s/t_gcap.XXXXXX", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp"); if (!mkdtemp(dir)) { perror("mkdtemp"); return 1; }
    char path[640]; snprintf(path, sizeof path, "%s/sub/frame.gputrace", dir);
    int sup = [cm supportsDestination:MTLCaptureDestinationGPUTraceDocument];
    printf("本机 supportsDestination(.gputrace) = %d\n", sup);
    // 2. 一次请求：+1 帧 | 2. one request: +1 frame
    dm_gfx_capture_at(1, path); EXPECT("请求排进队列", 1, g_ngcap);
    present(); EXPECT("请求后第一次出图：先关光追", 1, g_gcap_on);
#if !DM_NVN_M3
    if (rt_gate) EXPECT("关光追期间：rts_on 为 0", 0, rts_on());
#endif
    int waited = 1; while (sup && !cm.isCapturing && waited < GCAP_RT_LEAD + 5) { present(); gpu_work(b); waited++; }
    if (sup) {
        EXPECT("关光追后等满 GCAP_RT_LEAD 次出图才开始抓", GCAP_RT_LEAD + 1, waited);
        EXPECT("到期那次出图：开始抓", 1, cm.isCapturing); gpu_work(b);
        present(); EXPECT("下一次出图：停了", 0, cm.isCapturing); EXPECT("抓完：队列空", 0, g_ngcap); EXPECT("抓完：光追不再被关", 0, g_gcap_on);
#if !DM_NVN_M3
        if (rt_gate) EXPECT("抓完：光追照常开", 1, rts_on());
#endif
        EXPECT(".gputrace 写出来（父目录自动建）", 1, [NSFileManager.defaultManager fileExistsAtPath:@(path)]);
        // 3. 目标已存在：不覆盖 | 3. the target exists: no overwrite
        dm_gfx_capture_at(0, path); for (int i = 0; i < GCAP_RT_LEAD + 2; i++) present();
        EXPECT("目标已存在：没开始抓", 0, cm.isCapturing); EXPECT("目标已存在：请求作废", 0, g_ngcap); EXPECT("目标已存在：光追不再被关", 0, g_gcap_on);
    } else { for (int i = 0; i < GCAP_RT_LEAD + 2; i++) present(); EXPECT("不支持：请求作废", 0, g_ngcap); EXPECT("不支持：没在抓", 0, cm.isCapturing); EXPECT("不支持：光追不再被关", 0, g_gcap_on); }
    // 4. 队列上限 | 4. queue cap
    for (int i = 0; i < GCAP_MAX + 2; i++) dm_gfx_capture_at(1000, path);
    EXPECT("队列上限 4 个", GCAP_MAX, g_ngcap); g_ngcap = 0; g_gcap_on = 0;
    present(); EXPECT("清空后出图：没在抓", 0, cm.isCapturing);
    [NSFileManager.defaultManager removeItemAtPath:@(dir) error:nil];
    printf("== %d 项，失败 %d ==\n", count, fail);
    return fail != 0;
}}
