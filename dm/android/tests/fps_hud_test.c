// 「显示帧数」采样（fps_hud.h）：500 ms 窗口、四舍五入、关着归零、时间倒退重起窗。clang -I.. -o <仓外产物> fps_hud_test.c
// "Show frame rate" sampling (fps_hud.h): 500 ms window, rounding, zero while off, clock going backwards restarts the window. clang -I.. -o <out> fps_hud_test.c
#include "../fps_hud.h"
#include <stdio.h>
#include <stdlib.h>
static int checks; static void check(int ok, const char *why) { checks++; if (!ok) { fprintf(stderr, "FAIL %s\n", why); exit(1); } }
int main(void) {
    dm_fps_hud s = {0}; const uint64_t MS = 1000000ull;
    check(dm_fps_hud_sample(&s, 1000 * MS, 100, 1) == 0, "first sample arms the window, no value yet");
    check(dm_fps_hud_sample(&s, 1400 * MS, 124, 1) == 0, "under 500 ms: still no value");
    check(dm_fps_hud_sample(&s, 1500 * MS, 130, 1) == 60, "30 presents over 500 ms = 60");
    check(dm_fps_hud_sample(&s, 1700 * MS, 160, 1) == 60, "value held between windows");
    check(dm_fps_hud_sample(&s, 2010 * MS, 191, 1) == 120, "61 presents over 510 ms rounds to 120");
    check(dm_fps_hud_sample(&s, 2300 * MS, 200, 0) == 0 && s.t0 == 0, "switch off: zero and window cleared");
    check(dm_fps_hud_sample(&s, 2300 * MS, 200, 1) == 0, "switch on again re-arms");
    check(dm_fps_hud_sample(&s, 2900 * MS, 215, 1) == 25, "15 presents over 600 ms = 25");
    check(dm_fps_hud_sample(&s, 100 * MS, 300, 1) == 25, "clock going backwards: window restarted, old value kept");
    check(s.t0 == 100 * MS && s.p0 == 300, "restart recorded the new origin");
    check(dm_fps_hud_sample(&s, 700 * MS, 280, 1) == 0, "present count going backwards counts as zero");
    printf("PASS fps_hud_test: %d checks\n", checks); return 0;
}
