// 「显示帧数」的采样（纯函数，安卓触控层 TouchOverlay 经 nativeFpsHud 每帧问；iOS 对应 front_ios.m attach_fps_hud 的 0.5 秒定时器）：
// 每满 500 ms 用「出图计数差 / 时间差」算一次整数帧率；开关关着时归零并清窗口；安卓没有「系统确认上屏」的计数，用 HLE 的出图计数（dm_nvn_frame_count）。
// 单测 tests/fps_hud_test.c。
// "Show frame rate" sampling (pure function; the Android touch overlay asks through nativeFpsHud every frame; the iOS counterpart is the 0.5 s timer in front_ios.m attach_fps_hud):
// every full 500 ms the integer frame rate = present-count delta / time delta; while the switch is off the value is zeroed and the window reset; Android has no "confirmed on screen"
// count, so the HLE present count (dm_nvn_frame_count) is used. Unit test tests/fps_hud_test.c.
#pragma once
#include <stdint.h>
typedef struct { uint64_t t0, p0; int fps; } dm_fps_hud;
static inline int dm_fps_hud_sample(dm_fps_hud *s, uint64_t now_ns, uint64_t presents, int enabled) {
    if (!enabled) { s->t0 = 0; s->p0 = 0; s->fps = 0; return 0; }
    if (!s->t0 || now_ns < s->t0) { s->t0 = now_ns; s->p0 = presents; return s->fps; }
    if (now_ns - s->t0 < 500000000ull) return s->fps;
    uint64_t dp = presents >= s->p0 ? presents - s->p0 : 0, dt = now_ns - s->t0;
    s->fps = (int)((dp * 1000000000ull + dt / 2) / dt);
    s->t0 = now_ns; s->p0 = presents;
    return s->fps;
}
