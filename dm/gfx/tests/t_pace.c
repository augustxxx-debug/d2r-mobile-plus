// 节拍器单测：给定时钟序列，期望等待时长手算。周期 16.666 ms 记为 P = 16666667 ns。
// Pacer unit tests: given a clock sequence, the expected wait durations are hand-calculated. The period 16.666 ms is written as P = 16666667 ns.
#include "../nvn/nvn_pace.h"
#include <stdio.h>

int main(void) {
    const uint64_t P = 16666667, MS = 1000000;
    struct { uint64_t now, period, want; const char *why; } c[] = {
        {1000 * MS, P, 0, "首帧：起拍，不等"},
        {1005 * MS, P, 1000 * MS + P - 1005 * MS, "早到 5 ms：等到 1000+P"},
        {1000 * MS + 2 * P - 2 * MS, P, 2 * MS, "第三帧早 2 ms：等 2 ms"},
        {1000 * MS + 3 * P + 3 * MS, P, 0, "迟到 3 ms（不足一周期）：立即出图，节拍点仍为 1000+3P"},
        {1000 * MS + 4 * P - 1 * MS, P, 1 * MS, "下一帧早 1 ms：按原相位等到 1000+4P"},
        {1000 * MS + 7 * P, P, 0, "落后两个周期（卡顿）：重新起拍于当时，不连发追帧"},
        {1000 * MS + 7 * P + 4 * MS, P, P - 4 * MS, "重新起拍后的下一帧：等到新目标"},
        {1000 * MS + 9 * P, 0, 0, "周期 0（DM_PRESENT_HZ=0）：不等"},
        {1000 * MS + 9 * P + 1 * MS, 2 * P, 2 * P - 1 * MS, "切回间隔 2：节拍点 = 上帧时刻 9P + 2P"},
        {1000 * MS + 11 * P + 3 * MS, 2 * P, 2 * P - 3 * MS, "间隔 2 下一帧：节拍点 13P"},
    };
    nvn_pacer p = {0}; int fail = 0, n = sizeof c / sizeof *c;
    for (int i = 0; i < n; i++) {
        uint64_t got = nvn_pace(&p, c[i].now, c[i].period);
        int ok = got == c[i].want;
        printf("%-52s 期望等 %8.3f ms → 真结果 %8.3f ms %s\n", c[i].why, c[i].want / 1e6, got / 1e6, ok ? "✓" : "✗");
        fail += !ok;
    }
    // 出图帧率：屏幕刷新率 ÷ k，取不低于目标的最小一档（期望值按 刷新率 ÷ floor(刷新率 ÷ 目标) 手算）
    // Present frame rate: screen refresh rate ÷ k, taking the smallest tier not below the target (expected values computed by hand as refresh rate ÷ floor(refresh rate ÷ target))
    struct { double scr, floor, want; const char *why; } h[] = {
        {60, 60, 60, "60 Hz 屏：60（与原先相同）"}, {120, 60, 60, "120 Hz 屏：60（与原先相同）"}, {144, 60, 72, "144 Hz 屏：72（144 ÷ 2）"},
        {165, 60, 82.5, "165 Hz 屏：82.5（165 ÷ 2）"}, {100, 60, 100, "100 Hz 屏：100（÷ 1）"}, {240, 60, 60, "240 Hz 屏：60（÷ 4）"},
        {119.88, 60, 59.94, "119.88 Hz：59.94（÷ 2，NTSC 档）"}, {59.94, 60, 59.94, "59.94 Hz：59.94（÷ 1）"}, {50, 60, 50, "50 Hz 屏：50（低于目标也只能 ÷ 1）"},
        {0, 60, 60, "读不到刷新率：按目标 60"}, {120, 30, 30, "省电 30，120 Hz 屏：30"}, {144, 30, 36, "省电 30，144 Hz 屏：36（÷ 4）"}, {60, 30, 30, "省电 30，60 Hz 屏：30"},
    };
    for (int i = 0; i < (int)(sizeof h / sizeof *h); i++) {
        double got = nvn_pace_hz(h[i].scr, h[i].floor); int ok = got > h[i].want - 1e-9 && got < h[i].want + 1e-9;
        printf("%-52s 期望 %7.3f Hz → 真结果 %7.3f Hz %s\n", h[i].why, h[i].want, got, ok ? "✓" : "✗"); fail += !ok; n++;
    }
    // 显示节拍驱动（nvn_pace_link）：游戏跟着节拍走、比节拍器周期快（真机：DM_FPS=120 单开，节拍 120 Hz、节拍器 60 Hz，游戏 80–90 帧）。
    // 旧做法 nvn_pace 的目标每帧多走（周期 − 帧间隔），2000 帧后领先当前时刻 11 秒，节拍回调一迟到，取图兜底就睡到那时（拍 984 卡死）。
    // 期望：目标领先当前时刻不超过一个周期，兜底可睡到的时刻（目标 + 周期）不超过两个周期
    // Display-link driven (nvn_pace_link): the game follows the display link and runs faster than the pacer period (real device: DM_FPS=120 alone, link 120 Hz, pacer 60 Hz, game 80–90 fps).
    // The old nvn_pace target advanced (period − frame interval) extra per frame, leading now by 11 s after 2000 frames; one late link callback and the acquire fallback slept until then (stuck at tick 984).
    // Expected: the target leads now by at most one period, and the fallback sleep deadline (target + period) by at most two periods
    {
        nvn_pacer q = {0}; uint64_t t = 1000 * MS, lead_max = 0, step = 11 * MS; int ok = 1;
        for (int i = 0; i < 2000; i++, t += step) { nvn_pace_link(&q, t, P); uint64_t lead = q.target > t ? q.target - t : 0; if (lead > lead_max) lead_max = lead; }
        ok = lead_max <= P && q.target + P <= t - step + 2 * P;
        printf("%-52s 期望领先 ≤ %7.3f ms → 真结果 %9.3f ms %s\n", "节拍 11 ms、节拍器 16.7 ms（2000 帧）：目标不漂", P / 1e6, lead_max / 1e6, ok ? "✓" : "✗"); fail += !ok; n++;
        // 与节拍同速（8.33 ms 节拍、8.33 ms 周期，回调时刻 ±1 ms 抖动）：照常保持相位，不被误判成漂移而清零
        // Same rate as the link (8.33 ms ticks, 8.33 ms period, callback jitter ±1 ms): phase is kept as usual, not mistaken for drift and reset
        const uint64_t Q = 8333333; nvn_pacer r = {0}; uint64_t t2 = 1000 * MS, base = 0; int reset = 0;
        for (int i = 0; i < 600; i++) { uint64_t now = t2 + (uint64_t)i * Q + (i % 2 ? MS : 0); uint64_t before = r.target; nvn_pace_link(&r, now, Q); if (i == 0) base = r.target; else if (r.target != before + Q && r.target != now) reset++; }
        ok = reset == 0 && r.target == base + 599 * Q;
        printf("%-52s 期望相位保持 → 真结果 %s %s\n", "节拍与节拍器同速（±1 ms 抖动）：相位不变", reset ? "被重置" : "保持", ok ? "✓" : "✗"); fail += !ok; n++;
        // 比节拍器慢（节拍器 8.33 ms，游戏 11 ms 一帧）：同 nvn_pace，每帧迟到、不等
        // Slower than the pacer (pacer 8.33 ms, game 11 ms per frame): same as nvn_pace, late every frame, no wait
        nvn_pacer s = {0}; uint64_t t3 = 1000 * MS, waited = 0;
        for (int i = 0; i < 300; i++, t3 += step) waited += nvn_pace_link(&s, t3, Q);
        ok = waited == 0 && s.target <= t3;
        printf("%-52s 期望等 0 → 真结果 %8.3f ms %s\n", "游戏比节拍器慢（11 ms / 8.33 ms）：不等", waited / 1e6, ok ? "✓" : "✗"); fail += !ok; n++;
    }
    // 出图节拍器的下限跟目标帧率走（nvn_present_floor_hz）：120 帧档在 120 Hz 屏上节拍器也是 120，与显示节拍一致；60 Hz 屏只能 60；省电 30 由显示节拍定，节拍器照旧 60 档
    // The pacer floor follows the target frame rate (nvn_present_floor_hz): in 120 fps mode on a 120 Hz screen the pacer is 120 too, matching the display link; a 60 Hz screen can only do 60; power saving 30 is set by the display link, the pacer keeps the 60 tier
    struct { double scr; int target; double want; const char *why; } f[] = {
        {120, 120, 120, "目标 120、120 Hz 屏：节拍器 120（与显示节拍一致）"}, {60, 120, 60, "目标 120、60 Hz 屏：60"}, {144, 120, 144, "目标 120、144 Hz 屏：144"},
        {120, 60, 60, "目标 60、120 Hz 屏：60（与原先相同）"}, {120, 30, 60, "省电 30、120 Hz 屏：节拍器 60 档（与原先相同）"},
    };
    for (int i = 0; i < (int)(sizeof f / sizeof *f); i++) {
        double got = nvn_pace_hz(f[i].scr, nvn_present_floor_hz(f[i].target)); int ok = got > f[i].want - 1e-9 && got < f[i].want + 1e-9;
        printf("%-52s 期望 %7.3f Hz → 真结果 %7.3f Hz %s\n", f[i].why, f[i].want, got, ok ? "✓" : "✗"); fail += !ok; n++;
    }
    // 出图节拍模式（nvn_link_mode）：120 帧档在 iOS 上不用 CAMetalDisplayLink，改出图线程自取 drawable（0）——真机：局内 UIKit 叠层一出现，
    // 显示节拍路径被系统按 60 Hz 合成、隔张丢（每 300 张丢 150）；自取 drawable 同样叠层 9000 帧丢 0。省电 30 照旧 3；60 档与 Mac 不变；DM_120_LINK=1 强留节拍做对照
    // Present link mode (nvn_link_mode): in 120 fps mode on iOS, no CAMetalDisplayLink, the present thread takes drawables itself (0) -- on device, as soon as the in-run UIKit overlay shows,
    // the display-link path is composited at 60 Hz and every other frame is dropped (150 of 300); taking drawables directly, with the same overlay, dropped 0 in 9000. Power saving 30 stays 3; 60 fps and Mac unchanged; DM_120_LINK=1 keeps the link for comparison
    struct { int req, fps, ios, keep, want; const char *why; } lm[] = {
        {1, 120, 1, 0, 0, "iOS 120 帧档：自取 drawable（0）"}, {1, 120, 1, 1, 1, "iOS 120 帧档 + DM_120_LINK=1：留节拍（1）"}, {1, 120, 0, 0, 1, "Mac 120：留节拍（1）"},
        {1, 60, 1, 0, 1, "iOS 60：节拍（1，与原先相同）"}, {1, 30, 1, 0, 3, "iOS 省电 30：30 Hz 节拍（3，与原先相同）"}, {0, 120, 1, 0, 0, "DM_PRESENT_LINK=0：0"},
        {2, 120, 1, 0, 0, "iOS 120 帧档、模式 2：也自取（0）"}, {2, 60, 1, 0, 2, "iOS 60、模式 2：2（与原先相同）"}, {0, 30, 1, 0, 0, "DM_PRESENT_LINK=0 + 省电：0（与原先相同）"},
    };
    for (int i = 0; i < (int)(sizeof lm / sizeof *lm); i++) {
        int got = nvn_link_mode(lm[i].req, lm[i].fps, lm[i].ios, lm[i].keep), ok = got == lm[i].want;
        printf("%-52s 期望 %d → 真结果 %d %s\n", lm[i].why, lm[i].want, got, ok ? "✓" : "✗"); fail += !ok; n++;
    }
    printf("== %d 项，失败 %d ==\n", n, fail);
    return fail != 0;
}
