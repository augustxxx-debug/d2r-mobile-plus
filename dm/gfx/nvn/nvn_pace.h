// 出图节拍器（纯函数，Mac / iOS 共用；单测 tests/t_pace.c）。
// 语义同垂直同步：节拍点 = 上一节拍点 + 周期。早到 → 等到节拍点；迟到不足一个周期 → 立即出图、保持原相位；
// 落后一个周期以上（卡顿）→ 从当前时刻重新起拍，不连发追帧。周期 0 = 不等（节拍点跟随当前时刻）。
// Present pacer (pure function, shared by Mac / iOS; unit test tests/t_pace.c).
// Semantics as vsync: present slot = previous present slot + period. Early → wait until the present slot; late by less than one period → present immediately and keep the original phase;
// behind by one period or more (stutter) → restart the cadence from the current time, no burst of presents to catch up. Period 0 = no wait (the present slot follows the current time).
#pragma once
#include <stdint.h>

typedef struct { uint64_t target; } nvn_pacer;

// now：单调时钟（ns）；period：本帧周期（ns，0 = 不等）。返回需要等待的 ns（0 = 立即出图），并更新内部目标时刻。
// now: monotonic clock (ns); period: this frame's period (ns, 0 = no wait). Returns the ns to wait (0 = present immediately) and updates the internal target time.
static inline uint64_t nvn_pace(nvn_pacer *p, uint64_t now, uint64_t period) {
    if (!period || !p->target) { p->target = now; return 0; }
    uint64_t next = p->target + period;
    if (now <= next) { p->target = next; return next - now; }
    p->target = now - next < period ? next : now;
    return 0;
}

// 显示节拍驱动出图时（取图先等显示节拍，节拍器只作等不到节拍时的兜底）：游戏跟着节拍走，可以比节拍器的周期跑得快，
// nvn_pace 的目标时刻就每帧多走（周期 − 实际帧间隔）、越漂越远。真机：DM_FPS=120 单开，节拍 120 Hz、节拍器 60 Hz，游戏 80–90 帧，
// 目标每帧超前 5 ms 多；节拍回调迟到一次（39.7 ms），取图兜底睡到「目标 + 周期」即几十秒后，游戏卡在拍 984 / 1447。
// 上一帧的节拍点还没到游戏就来了（只有跑得比节拍器快才会这样）就从当前时刻重新起拍：之后目标最多领先当前时刻一个周期；同速时的回调抖动照常保持相位
// When the display link drives presents (acquire waits for the link first; the pacer is only the fallback when no tick comes): the game follows the link and can run faster than the pacer period,
// so nvn_pace's target advances (period − actual frame interval) extra each frame and drifts ever further. Real device: DM_FPS=120 alone, link 120 Hz, pacer 60 Hz, game 80–90 fps,
// target ahead by 5+ ms more each frame; one late link callback (39.7 ms) and the acquire fallback slept until "target + period", tens of seconds later, with the game stuck at tick 984 / 1447.
// When the game arrives before the previous frame's slot (only possible when running faster than the pacer), restart the cadence from now: afterwards the target leads now by at most one period; callback jitter at equal rates keeps the phase as usual
static inline uint64_t nvn_pace_link(nvn_pacer *p, uint64_t now, uint64_t period) {
    if (period && p->target > now) p->target = now;
    return nvn_pace(p, now, period);
}

// 节拍器的下限帧率：120 帧档（DM_FPS=120 / 设置页 120）= 120，与显示节拍同速（两者不一致时游戏跟着 120 Hz 节拍跑、节拍器按 60 走，见 nvn_pace_link）；
// 其余 60——省电 30 帧由显示节拍决定，节拍器照旧 60 档（60 / 120 Hz 屏与原先逐字相同）
// The pacer's floor frame rate: in 120 fps mode (DM_FPS=120 / settings 120) = 120, the same rate as the display link (when they differ the game runs with the 120 Hz link while the pacer goes by 60, see nvn_pace_link);
// otherwise 60 -- power saving 30 fps is decided by the display link and the pacer keeps the 60 tier (identical to before on 60 / 120 Hz screens)
static inline double nvn_present_floor_hz(int fps_target) { return fps_target == 120 ? 120 : 60; }

// 出图节拍模式：requested = DM_PRESENT_LINK（0 自取 drawable / 1 显示节拍 / 2 120 Hz 节拍隔拍出），省电 30 帧改 3（30 Hz 节拍）。
// 120 帧档在 iOS 上改 0：显示节拍（CAMetalDisplayLink）路径一遇到局内 UIKit 叠层（触控控件 / 自绘 HUD），系统就改走与 UIKit 一起合成、提前量从 2 帧变 3 帧，
// 并只按 60 Hz 合成，隔一张丢一张（iPhone Duo 真机：回调均匀 8.33 ms、GPU 每张早 21 ms 画完，仍每 300 张丢 150；藏掉叠层即 0 丢）。
// 出图线程自取 drawable + 最短显示时长同样叠层下 9000 帧丢 0。Mac 没有 UIKit 叠层，照旧；keep_link（DM_120_LINK=1）强留节拍做对照
// Present link mode: requested = DM_PRESENT_LINK (0 take drawables / 1 display link / 2 120 Hz link presenting every other tick); power saving 30 fps becomes 3 (30 Hz link).
// 120 fps mode on iOS becomes 0: once the in-run UIKit overlay (touch controls / self-drawn HUD) shows, the system composites the display-link (CAMetalDisplayLink) path with UIKit, the lead grows from 2 to 3 frames,
// and composition runs at 60 Hz, dropping every other frame (iPhone Duo: callbacks an even 8.33 ms, each frame done 21 ms early on the GPU, still 150 of 300 dropped; hiding the overlay gives 0 drops).
// The present thread taking drawables with a minimum display duration dropped 0 in 9000 with the same overlay. Mac has no UIKit overlay and is unchanged; keep_link (DM_120_LINK=1) keeps the link for comparison
static inline int nvn_link_mode(int requested, int fps_target, int is_ios, int keep_link) {
    if (!requested) return 0;
    if (fps_target == 30) return 3;
    if (fps_target == 120 && is_ios && !keep_link) return 0;
    return requested;
}

// 出图帧率 = 屏幕刷新率的整数分之一里不低于目标（60，省电模式 30）的最小一档：每帧在屏上停整数个刷新周期，没有交替抖动。
// 60 / 120 Hz 屏 → 60；144 → 72；165 → 82.5；100 → 100；240 → 60；省电 30：120 → 30、144 → 36。刷新率读不到（0）就按目标。
// 0.02 的余量让 119.88 / 59.94 这类 NTSC 档按 2 / 1 倍算
// Present frame rate = the smallest tier among integer fractions of the screen refresh rate that is not below the target (60, power saving mode 30): each frame stays on screen for an integer number of refresh cycles, with no alternating jitter.
// 60 / 120 Hz screen → 60; 144 → 72; 165 → 82.5; 100 → 100; 240 → 60; power saving 30: 120 → 30, 144 → 36. When the refresh rate cannot be read (0), the target is used.
// The 0.02 margin lets NTSC rates like 119.88 / 59.94 count as 2 / 1 times
static inline double nvn_pace_hz(double screen_hz, double floor_hz) {
    if (screen_hz <= 0 || floor_hz <= 0) return floor_hz;
    double k = (double)(long)(screen_hz / floor_hz + 0.02); return k >= 1 ? screen_hz / k : screen_hz;
}
