// 「重启后生效」提示的判定（B151），纯函数，宿主 dm_custom.c 与单测 tests/t_restart_hint.c 共用。
// 设置项：启动时记下的值 boot（boot_ok = 已记），当前已应用值 val，待应用值 pend（-1 = 没有，玩家没点 / 点了「放弃」）。
//   显示 ⇔ 已记 且 （待应用值 或 已应用值）≠ 启动值：改了就显示、改回启动值消失、没改不显示、「放弃」后回到已应用值再比、「应用」后仍 ≠ 启动值则保留到重启。
// 光追（只 Apple，§2.8）：比的是「生效」的开关——启动时那一下降没降分辨率（dm_render_res_rt_drop），与「现在的选择过机器门槛之后开不开」；无硬件光追恒不提示。
// 分辨率行一个控件两种原因：只低内存调高 = 1、只光追跨开关 = 2、两条都成立 = 3（合并成一句，两件事都说到），都不成立 = 0。
// The "applies after restart" decisions (B151), pure functions shared by dm_custom.c and the unit test tests/t_restart_hint.c.
// Setting: launch value boot (boot_ok = recorded), applied value val, pending value pend (-1 = none: the player did not touch it or pressed Cancel).
//   Shown <=> recorded and (pending or applied) != launch value: shown once changed, hidden when changed back, nothing when unchanged; after Cancel the applied value is compared; after Apply it stays until restart if still different.
// Ray tracing (Apple only, section 2.8): compares the effective switch - whether launch dropped the resolution (dm_render_res_rt_drop) versus whether the current choice is on after the machine gate; never on machines without hardware RT.
// The resolution row has one widget for two reasons: low-memory raise only = 1, RT crossing only = 2, both = 3 (one combined sentence naming both), neither = 0.
#ifndef DM_RESTART_HINT_H
#define DM_RESTART_HINT_H
static inline int dm_restart_pending(int boot_ok, int boot, int val, int pend) { return boot_ok && (pend >= 0 ? pend : val) != boot; }
static inline int dm_rt_restart(int boot_ok, int hw_rt, int boot_dropped, int level_now) { return boot_ok && hw_rt && (level_now > 0) != (boot_dropped != 0); }
static inline int dm_res_note_kind(int raise, int rt) { return (raise ? 1 : 0) | (rt ? 2 : 0); }
#endif
