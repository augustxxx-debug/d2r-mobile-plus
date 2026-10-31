// 3D 画面加亮的曝光上限（纯函数，dm_bright.c 用；单测 tests/t_bright_cap.c）。
// DM_BRIGHT_MAX=<k>（1–4）：目标 k 超过它就按它算，仍在对数空间平滑。缺省 2，三端一致（B098，用户 2026-10-31：洞穴类关卡原规则固定 k = 4，
// 亮的冰洞里过亮发花；安卓 App 自 2026-10-28 起就封顶 2）。设 4 = 原规则（将来亮度档位「高」与测试用），1 = 从不加亮。原版亮度滑杆只动游戏伽马压不住它。
// 非法值（非数、不在 1–4）按缺省 2。
// Exposure cap for the 3D picture brightening (pure functions, used by dm_bright.c; unit test tests/t_bright_cap.c).
// DM_BRIGHT_MAX=<k> (1–4): a target k above it is replaced by it, still smoothed in log space. The default is 2 on every platform (B098, user 2026-10-31: the original
// rule pins maze levels at k = 4, which is too bright and blotchy in the bright ice caves; the Android app has capped at 2 since 2026-10-28). 4 = the original rule
// (the future brightness setting "High", and tests), 1 = never brighten. The original brightness slider, which only moves the game's gamma, cannot counter it.
// Invalid values (not a number, outside 1–4) fall back to the default 2.
#pragma once
#include <math.h>
#include <stdlib.h>
#define DM_BRIGHT_MAX_DEFAULT 2.0
static inline double dm_bright_max_parse(const char *e, int *bad) {
    *bad = 0; if (!e || !*e) return DM_BRIGHT_MAX_DEFAULT;
    char *end; double v = strtod(e, &end);
    if (end == e || *end || !(v >= 1.0 && v <= 4.0)) { *bad = 1; return DM_BRIGHT_MAX_DEFAULT; }
    return v;
}
static inline double dm_bright_cap_logk(double target_logk, double max_k) { double c = log(max_k); return target_logk > c ? c : target_logk; }
