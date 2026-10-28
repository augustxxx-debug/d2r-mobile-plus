// 「显示帧数」的采样（纯函数，两端共用：iOS front_ios.m attach_fps_hud、Android jni_android.c nativeFpsHud；单测 tests/t_fps_hud.c）。
// 口径：**交换链实际上屏的帧**（iOS mtl_presented_count：系统确认 presentedTime > 0 的；Android dm_present_count：vkQueuePresentKHR 成功的），
// 与系统帧率监控同口径；不数游戏逻辑帧 / 渲染提交（2026-10-28 用户：奥丁 HUD 61、系统监控 79 对不上）。
// 窗口：最近 1 秒的滑动窗口（每 250 ms 记一个采样点，环里存 1.25 秒）；显示值每 500 ms 更新一次（最多 2 Hz，覆盖层不会因为数字跳动多出合成帧）。
// 开关关着时清空；时间或计数倒退时重起。
// "Show frame rate" sampling (pure functions shared by both platforms: iOS front_ios.m attach_fps_hud, Android jni_android.c nativeFpsHud; unit test tests/t_fps_hud.c).
// Measure: **frames actually presented by the swapchain** (iOS mtl_presented_count: system-confirmed presentedTime > 0; Android dm_present_count: successful vkQueuePresentKHR),
// the same measure as system frame-rate monitors; not game logic frames / render submissions (user 2026-10-28: the Thor HUD said 61 while the system monitor said 79).
// Window: a sliding 1 s window (one sample point every 250 ms, a ring holding 1.25 s); the shown value updates every 500 ms (at most 2 Hz, so the overlay creates no extra
// compositions from a jittering number). Cleared while the switch is off; restarted when the clock or the count goes backwards.
#pragma once
#include <stdint.h>
#define DM_FPS_HUD_RING 8
typedef struct { uint64_t t[DM_FPS_HUD_RING], p[DM_FPS_HUD_RING]; int n, head; uint64_t last_pub; int fps; } dm_fps_hud;
static inline void dm_fps_hud_clear(dm_fps_hud *s) { s->n = 0; s->head = 0; s->last_pub = 0; s->fps = 0; }
static inline int dm_fps_hud_sample(dm_fps_hud *s, uint64_t now_ns, uint64_t presents, int enabled) {
    const uint64_t MS = 1000000ull;
    if (!enabled) { dm_fps_hud_clear(s); return 0; }
    if (s->n) { int last = (s->head + DM_FPS_HUD_RING - 1) % DM_FPS_HUD_RING;
        if (now_ns < s->t[last] || presents < s->p[last]) { int keep = s->fps; dm_fps_hud_clear(s); s->fps = keep; } }   // 倒退：重起窗口，显示值保留 | went backwards: restart, keep the shown value
    int last = (s->head + DM_FPS_HUD_RING - 1) % DM_FPS_HUD_RING;
    if (!s->n || now_ns - s->t[last] >= 250 * MS) {   // 每 250 ms 记一点 | one point every 250 ms
        s->t[s->head] = now_ns; s->p[s->head] = presents; s->head = (s->head + 1) % DM_FPS_HUD_RING; if (s->n < DM_FPS_HUD_RING) s->n++; }
    if (s->last_pub && now_ns - s->last_pub < 500 * MS) return s->fps;   // 显示值最多 2 Hz | shown value at most 2 Hz
    // 找 ≥ 1 秒前最近的那一点（没有就用最早的点，至少 500 ms） | the newest point at least 1 s old (else the oldest, at least 500 ms)
    int pick = -1;
    for (int k = 0; k < s->n; k++) { int i = (s->head + DM_FPS_HUD_RING - 1 - k) % DM_FPS_HUD_RING; if (now_ns - s->t[i] >= 1000 * MS) { pick = i; break; } }
    if (pick < 0 && s->n) { int oldest = (s->head + DM_FPS_HUD_RING - s->n) % DM_FPS_HUD_RING; if (now_ns - s->t[oldest] >= 500 * MS) pick = oldest; }
    if (pick < 0) { if (!s->last_pub) s->last_pub = 0; return s->fps; }
    uint64_t dt = now_ns - s->t[pick], dp = presents - s->p[pick];
    s->fps = (int)((dp * 1000000000ull + dt / 2) / dt); s->last_pub = now_ns;
    return s->fps;
}
