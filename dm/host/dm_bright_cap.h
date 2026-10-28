// 3D 画面加亮的曝光上限（纯函数，dm_bright.c 用；单测 tests/t_bright_cap.c）。
// DM_BRIGHT_MAX=<k>（1–4）：目标 k 超过它就按它算，仍在对数空间平滑；缺省 4（原规则）。安卓 App 缺省 2：奥丁上洞穴类关卡固定 k = 4 观感过曝（2026-10-28 用户），
// 原版亮度滑杆只动游戏伽马压不住它；iOS 不变。非法值（非数、不在 1–4）按 4。
// Exposure cap for the 3D picture brightening (pure functions, used by dm_bright.c; unit test tests/t_bright_cap.c).
// DM_BRIGHT_MAX=<k> (1–4): a target k above it is replaced by it, still smoothed in log space; default 4 (the original rule). The Android app defaults to 2: on the Thor the fixed
// k = 4 of maze levels reads as overexposed (user, 2026-10-28) and the original brightness slider, which only moves the game's gamma, cannot counter it; iOS is unchanged.
// Invalid values (not a number, outside 1–4) fall back to 4.
#pragma once
#include <math.h>
#include <stdlib.h>
static inline double dm_bright_max_parse(const char *e, int *bad) {
    *bad = 0; if (!e || !*e) return 4.0;
    char *end; double v = strtod(e, &end);
    if (end == e || *end || !(v >= 1.0 && v <= 4.0)) { *bad = 1; return 4.0; }
    return v;
}
static inline double dm_bright_cap_logk(double target_logk, double max_k) { double c = log(max_k); return target_logk > c ? c : target_logk; }
