// 安卓性能提示与温控（perfhint_android.c）：会话线程挑选（阈值、排序、上限、全闲）、AThermal 状态到 0–3 档的换算、
// 系统没有 libandroid 时整段跳过（帧钩子不崩、温控 −1），以及 diag.c 的出图钩子确实调到它。宿主符号打桩。
// cc -std=c11 -Wall -Wextra -Werror -I.. -o <仓外产物> perfhint_android_test.c ../perfhint_android.c
// Android performance hints and thermal status (perfhint_android.c): session thread picking (threshold, order, cap, all idle), AThermal status to 0–3 levels,
// everything skipped without libandroid (the frame hook does not crash, thermal is −1), and the present hook in diag.c really calls it. Host symbols stubbed.
#include "../perfhint_android.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
uint64_t now_ns(void) { static uint64_t t; return t += 1000000; }
int dm_fps_setting(void) { return 60; }
static int checks; static void check(int ok, const char *why) { checks++; if (!ok) { fprintf(stderr, "FAIL %s\n", why); exit(1); } }
int main(int argc, char **argv) {
    pid_t tids[] = {11, 12, 13, 14, 15, 16, 17}, out[DM_PERFHINT_MAX_THREADS];
    uint64_t busy[] = {800, 50, 9000, 900, 2000, 0, 1500};   // 最忙 9000，门槛 900 | busiest 9000, threshold 900
    int k = dm_perfhint_pick(tids, busy, 7, out, DM_PERFHINT_MAX_THREADS);
    check(k == 4 && out[0] == 13 && out[1] == 15 && out[2] == 17 && out[3] == 14, "busiest first, threshold 1/10 inclusive, capped at 4");
    k = dm_perfhint_pick(tids, busy, 7, out, 2); check(k == 2 && out[0] == 13 && out[1] == 15, "cap honoured");
    uint64_t idle[7] = {0}; check(dm_perfhint_pick(tids, idle, 7, out, 4) == 0, "all idle -> no session");
    uint64_t one[] = {5}; check(dm_perfhint_pick(tids, one, 1, out, 4) == 1 && out[0] == 11, "single thread");
    // AThermal：0 NONE 1 LIGHT 2 MODERATE 3 SEVERE 4 CRITICAL 5 EMERGENCY 6 SHUTDOWN；负数 = 错误 | negative = error
    int want[] = {0, 1, 1, 2, 3, 3, 3}; for (int s = 0; s <= 6; s++) check(dm_thermal_android_level(s) == want[s], "thermal mapping");
    check(dm_thermal_android_level(-1) == -1 && dm_thermal_android_level(-22) == -1, "error status -> not read");
    // 本机没有 libandroid：钩子空转、温控读不到 | no libandroid here: the hook idles, thermal unread
    for (int i = 0; i < 1000; i++) dm_perfhint_frame();
    dm_perfhint_frame_at(0); dm_perfhint_frame_at(120);
    check(dm_thermal_now() == -1, "no AThermal -> -1");
    // diag.c 的出图钩子：弱定义 + dm_perf_present 里调用 | the present hook in diag.c: weak definition + called from dm_perf_present
    if (argc > 1) {
        FILE *f = fopen(argv[1], "r"); check(f != NULL, "open diag.c"); static char src[1 << 22]; size_t n = fread(src, 1, sizeof src - 1, f); fclose(f); src[n] = 0;
        check(strstr(src, "__attribute__((weak)) void dm_perfhint_frame(void) {}") != NULL, "weak no-op definition in diag.c");
        char *p = strstr(src, "void dm_perf_present(double iv) {"), *q = p ? strstr(p, "dm_perfhint_frame();") : NULL, *r = p ? strstr(p, "if (!path) return;") : NULL;
        check(p && q && r && q < r, "dm_perf_present calls the hook before the perf-file early return");
    }
    printf("perfhint_android: %d checks passed\n", checks);
    return 0;
}
