// 整帧输出分辨率上限（纯函数，iOS 前端 front_game_res 用；单测 tests/t_res_cap.c）。
// 背景：游戏的界面层、最终合成与放大通道都按「输出尺寸」渲染，只有 3D 走另一个比例。iPhone Duo 内屏窗口 2852x2006 ≈ 572 万像素，
// 是 Switch 1080p 的 2.8 倍，3D 比例再低也压不下这部分；这里按像素预算把输出尺寸等比缩小，出图线程现有的整屏放大把它铺回屏幕。
// 档位 render_res（dm_custom.c）：0 自动 = 400 万像素预算（iPhone 17 Pro Max 原生 2868x1320 ≈ 379 万不受影响，Duo 内屏缩到约 0.84 倍边长）；
// 1 省电 = 207 万（1080p 等量）；2 均衡 = 280 万；3 原生 = 不限制。只缩不放，保持宽高比，宽高取偶数。
// Whole-frame output resolution cap (pure functions, used by the iOS front end's front_game_res; unit test tests/t_res_cap.c).
// Background: the game's UI layer, final composite and upscale pass all render at the "output size"; only 3D uses a separate ratio. The iPhone Duo inner-screen
// window 2852x2006 ≈ 5.72 MP is 2.8x the Switch's 1080p, and no 3D ratio can reduce that part; this shrinks the output size proportionally to a pixel budget,
// and the present thread's existing full-screen upscale stretches it back to the screen.
// Levels render_res (dm_custom.c): 0 Auto = 4.0 MP budget (iPhone 17 Pro Max native 2868x1320 ≈ 3.79 MP unaffected, Duo inner screen shrinks to ~0.84x edge length);
// 1 Eco = 2.07 MP (1080p equivalent); 2 Balanced = 2.8 MP; 3 Native = no cap. Never upscales, keeps the aspect ratio, even width and height.
#pragma once
#include <stdint.h>

#define DM_RES_LEVELS 4
static inline uint32_t dm_res_cap_budget(int level) {
    switch (level) { case 1: return 2073600u; case 2: return 2800000u; case 3: return 0; default: return 4000000u; }
}
// 自动档按 GPU 代际给预算（Metal 设备名如 "Apple A17 Pro GPU" / "Apple M4 Pro GPU"）：A16 及以前 207 万（iPhone 14 Pro 2556x1179 ≈ 301 万会缩到约 0.83 倍边长）；
// A17 Pro / A18 280 万；A18 Pro / A19 380 万（iPhone 17 Pro Max 原生 379 万不缩）；A19 Pro / A20 及以后 400 万（Duo 内屏实测 0.33% 掉帧）；M 系列 450 万；认不出的名字按 400 万
// Auto budget by GPU generation (Metal device name such as "Apple A17 Pro GPU" / "Apple M4 Pro GPU"): A16 and earlier 2.07 MP (iPhone 14 Pro 2556x1179 ≈ 3.01 MP shrinks to ~0.83x edge);
// A17 Pro / A18 2.8 MP; A18 Pro / A19 3.8 MP (iPhone 17 Pro Max native 3.79 MP unchanged); A19 Pro / A20 and later 4.0 MP (Duo inner screen measured 0.33% dropped frames); M series 4.5 MP; unrecognized names 4.0 MP
static inline int dm_res_gpu_class(const char *name, int *gen, int *pro, int *m) {
    *gen = 0; *pro = 0; *m = 0; if (!name) return 0;
    for (const char *p = name; *p; p++) {
        if ((p[0] == 'A' || p[0] == 'M') && p[1] >= '0' && p[1] <= '9' && (p == name || p[-1] == ' ')) {
            int v = 0; const char *q = p + 1; while (*q >= '0' && *q <= '9') v = v * 10 + (*q++ - '0');
            if (p[0] == 'M') *m = v; else *gen = v;
            if (q[0] == ' ' && q[1] == 'P' && q[2] == 'r' && q[3] == 'o') *pro = 1;
            return 1; } }
    return 0;
}
static inline uint32_t dm_res_cap_budget_auto(int gen, int pro, int m) {
    if (m > 0) return 4500000u;
    if (gen <= 0) return 4000000u;
    if (gen >= 20 || (gen == 19 && pro)) return 4000000u;
    if (gen == 19 || (gen == 18 && pro)) return 3800000u;
    if (gen == 18 || (gen == 17 && pro)) return 2800000u;
    return 2073600u;
}
static inline uint32_t dm_res_cap_budget_class(int level, int gen, int pro, int m) { return level == 0 ? dm_res_cap_budget_auto(gen, pro, m) : dm_res_cap_budget(level); }
// 自动档再按屏幕像素量收紧：原生超过 450 万像素的屏（目前只有 iPhone Duo 内屏 2852x2006）取均衡预算 280 万。实测（2026-10-27，满屏法术特效）：原生 GPU 全程约 15 ms 饱和，
// 省电档约 10 ms，两档帧率掉帧相同；400 万预算只少 30% 像素，仍在饱和边缘。普通 iPhone / iPad 不受影响
// Auto is tightened further by screen pixel count: screens with more than 4.5 MP native (today only the iPhone Duo inner screen 2852x2006) take the Balanced budget 2.8 MP. Measured 2026-10-27 under full-screen spell effects:
// Native GPU pinned at ~15 ms (saturated), Eco ~10 ms, identical fps and dropped frames; a 4.0 MP budget removes only 30% of the pixels and stays at the edge of saturation. Regular iPhones / iPads are unaffected
#define DM_RES_HUGE_SCREEN_PIXELS 4500000u
static inline uint32_t dm_res_cap_budget_screen(int level, int gen, int pro, int m, uint32_t w, uint32_t h) {
    uint32_t b = dm_res_cap_budget_class(level, gen, pro, m);
    if (level == 0 && (uint64_t)w * h > DM_RES_HUGE_SCREEN_PIXELS && b > 2800000u) b = 2800000u;
    return b;
}
// 返回 {u32 高 << 32 | u32 宽}（与 front_game_res 同一打包）。预算 0 / 本来就在预算内 / 非法尺寸 → 原样返回。
// Returns {u32 height << 32 | u32 width} (same packing as front_game_res). Budget 0 / already within budget / invalid size → returned unchanged.
static inline uint64_t dm_res_cap_apply(uint32_t w, uint32_t h, uint32_t budget) {
    uint64_t orig = (uint64_t)h << 32 | w;
    if (!w || !h || !budget || (uint64_t)w * h <= budget) return orig;
    // 不用浮点开方：从宽的上界二分到最大的、使 cw × ch ≤ 预算的偶数宽；高按原比例取整到偶数
    // No floating-point sqrt: binary search for the largest even width whose cw × ch ≤ budget; height follows the original ratio, rounded to even
    uint32_t lo = 2, hi = w & ~1u, best = 0;
    while (lo <= hi) {
        uint32_t cw = ((lo + hi) / 2) & ~1u; if (cw < 2) cw = 2;
        uint64_t ch = (((uint64_t)cw * h + w / 2) / w) & ~1ull; if (ch < 2) ch = 2;
        if (cw * ch <= budget) { best = cw; lo = cw + 2; } else { if (cw <= 2) break; hi = cw - 2; }
    }
    if (!best) return orig;
    uint32_t ch = (uint32_t)((((uint64_t)best * h + w / 2) / w) & ~1ull); if (ch < 2) ch = 2;
    return (uint64_t)ch << 32 | best;
}

// ---------- 自动档闭环（按实际掉帧，不按机型表）----------
// 输入每个 2 秒窗（与 hle_oe.c 的 3D 比例控制器同窗）：3D 比例已在下限 + 最近 10 秒掉帧 > 5%（≥5 窗）或 60 秒 > 1.5%（≥30 窗）→ 输出档再降一步（最多 2 步：均衡 280 万 → 省电 207 万，且不高于代际预算）；
// 连续 30 个干净窗（60 秒）→ 升一步；刚升 15 秒内又降 → 下次升要 45 窗。每次变档停判 10 秒（换尺寸本身有停顿）。只在自动档、非「画质优先」时生效。
// ---------- Auto-level closed loop (by measured dropped frames, not a device table) ----------
// Fed once per 2 s window (same window as the 3D-scale controller in hle_oe.c): 3D already at its floor + drops > 5% over 10 s (≥5 windows) or > 1.5% over 60 s (≥30 windows) → one more output step down
// (at most 2: Balanced 2.8 MP → Eco 2.07 MP, never above the generation budget); 30 clean windows in a row (60 s) → one step up; dropped again within 15 s of a raise → the next raise needs 45 windows.
// 10 s hold after every change (the resize itself stalls). Only in Auto and not in "quality first".
#define DM_RES_AUTO_STEPS 2
typedef struct { int step; uint64_t hold_until, last_up; int good, need_good; } dm_res_auto_state;
static inline uint32_t dm_res_auto_budget(uint32_t class_budget, int step) {
    uint32_t cap = step <= 0 ? 0 : step == 1 ? 2800000u : 2073600u;
    return cap && cap < class_budget ? cap : class_budget;
}
// 前端实际套用的步数：只有渲染档 = 自动且不是画质优先时才套用闭环步数，否则 0。宿主那边离开闭环时已清零，这里再兜一层（审计 P1）
// Step the front end actually applies: the loop step only when the render level is Auto and not quality-first, otherwise 0. The host already clears it on leaving the loop; this is a second guard (audit P1)
#define DM_RES_AUTO_HAVE_EFFECTIVE 1
static inline int dm_res_auto_step_effective(int step, int level, int quality) { return level == 0 && !quality && step > 0 ? step : 0; }
static inline int dm_res_auto_step(dm_res_auto_state *s, int at_floor, double r10, int enough10, double r60, int enough60, int clean, uint64_t now) {
    const uint64_t sec = 1000000000ull;
    if (now < s->hold_until) return s->step;
    if (at_floor && s->step < DM_RES_AUTO_STEPS && ((enough10 && r10 > 0.05) || (enough60 && r60 > 0.015))) {
        if (s->last_up && now - s->last_up < 15 * sec) s->need_good = 45;
        s->step++; s->good = 0; s->hold_until = now + 10 * sec; return s->step;
    }
    if (clean) { if (++s->good >= (s->need_good ? s->need_good : 30) && s->step > 0) { s->step--; s->good = 0; s->need_good = 0; s->last_up = now; s->hold_until = now + 10 * sec; } }
    else s->good = 0;
    return s->step;
}
