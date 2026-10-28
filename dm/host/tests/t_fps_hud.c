// 「显示帧数」采样（dm_fps_hud.h，两端共用）：1 秒滑动窗口、显示值最多 2 Hz、关着清空、倒退重起、不足 500 ms 不出数。
// clang -I.. -o build/t_fps_hud t_fps_hud.c
// "Show frame rate" sampling (dm_fps_hud.h, shared): sliding 1 s window, shown value at most 2 Hz, cleared while off, restart on going backwards, no value under 500 ms.
#include "../dm_fps_hud.h"
#include <stdio.h>
#include <stdlib.h>
#define NEAR(v, want) ((v) >= (want) - 1 && (v) <= (want) + 1)   // 计数取整 + 窗口长 1–1.25 s：允许 ±1 | integer counts + a 1–1.25 s window: allow ±1
static int checks; static void check(int ok, const char *why) { checks++; if (!ok) { fprintf(stderr, "FAIL %s\n", why); exit(1); } }
// 以 fps 的匀速在 [t0, t1) 内每 step 毫秒喂一次（模拟每个 Choreographer 帧问一次），返回最后的显示值 | feed at a steady fps every step ms over [t0, t1), return the last shown value
static int run(dm_fps_hud *s, double fps, uint64_t t0_ms, uint64_t t1_ms, uint64_t step, double *acc, int enabled) {
    int v = 0; for (uint64_t t = t0_ms; t < t1_ms; t += step) { *acc += fps * step / 1000.0; v = dm_fps_hud_sample(s, t * 1000000ull, (uint64_t)*acc, enabled); } return v; }
int main(void) {
    dm_fps_hud s = {0}; double acc = 0; const uint64_t MS = 1000000ull;
    check(run(&s, 60, 0, 400, 8, &acc, 1) == 0, "under 500 ms: no value yet");
    { int v = run(&s, 60, 400, 2000, 8, &acc, 1); check(NEAR(v, 60), "steady 60 presents/s -> 60"); }
    { int v = run(&s, 79, 2000, 4000, 8, &acc, 1); check(NEAR(v, 79), "steady 79 -> 79 (1 s sliding window)"); }
    { int changes = 0, last = s.fps; for (uint64_t t = 4000; t < 6000; t += 8) { acc += 79 * 8 / 1000.0; int v = dm_fps_hud_sample(&s, t * MS, (uint64_t)acc, 1); if (v != last) { changes++; last = v; } } check(changes <= 4, "shown value changes at most every 500 ms (<= 4 times in 2 s)"); }
    { uint64_t lp = s.last_pub; acc += 1; dm_fps_hud_sample(&s, 6001 * MS, (uint64_t)acc, 1); check(s.last_pub == lp, "no republish within 500 ms of the last one"); }
    run(&s, 120, 6000, 6600, 8, &acc, 1); check(s.fps > 79 && s.fps < 120, "half a window after a jump: between the old and new rate (sliding, not reset)");
    { int v = run(&s, 120, 6600, 8000, 8, &acc, 1); check(NEAR(v, 120), "after a full second: 120"); }
    check(dm_fps_hud_sample(&s, 8100 * MS, (uint64_t)acc, 0) == 0 && s.n == 0, "switch off: cleared");
    check(run(&s, 60, 8100, 8500, 8, &acc, 1) == 0, "switch on again: no value for the first 500 ms");
    { int v = run(&s, 60, 8500, 10000, 8, &acc, 1); check(NEAR(v, 60), "then 60"); }
    { int keep = s.fps; check(dm_fps_hud_sample(&s, 100 * MS, 5, 1) == keep && s.n == 1, "clock going backwards: window restarted, shown value kept"); }
    printf("PASS t_fps_hud: %d checks\n", checks); return 0;
}
