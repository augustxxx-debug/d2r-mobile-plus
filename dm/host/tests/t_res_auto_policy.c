// 自动输出档闭环的退出策略（审计 P1，2026-10-27）：走生产入口 dm_3d_scale_feed，不复制控制器。
// 策略：闭环只在「渲染分辨率 = 自动、3D 非画质优先、闭环开关未关、动态控制器开着」时生效；任一条件不满足立即清零额外降档，
// 原来不为 0 时推一次 31 让游戏重读输出尺寸；重新满足条件后从第 0 步重新开始，不沿用旧步数。
// Exit policy of the auto output-level loop (audit P1, 2026-10-27): drives the production entry dm_3d_scale_feed, no copied controller.
// Policy: the loop is active only while render resolution = Auto, 3D is not quality-first, the loop switch is not off and the dynamic controller is on; as soon as any condition fails the extra
// downstep is cleared, pushing 31 once (if it was non-zero) so the game re-reads the output size; when the conditions hold again the loop restarts at step 0 instead of resuming the old step.
#define DM_RES_LEVEL_TEST_HOOK 1
#include "../hle_oe.c"
#include <assert.h>
#include <math.h>
float test_fx_hint = 0; int test_fx_auto = 1;
static uint64_t test_clock = 1000000000ull;
uint64_t now_ns(void) { return test_clock; }
module_t g_mod[MAX_MOD]; int g_nmod;
pthread_key_t g_keyres[2];
uint64_t dm_game_res(int docked) { (void)docked; return 0; }
int dm_nvn_ruler_truth(void) { return 0; }
void dm_ui_gen_bump(int why) { (void)why; }
void dm_uiscreen_res_reread(void) {}
uint64_t dm_nvn_frame_count(void) { return 0; }
static int test_ingame = 1;
int dm_ingame_now(void) { return test_ingame; }
int mtl_fps_target(void) { return 60; }
double mtl_display_refresh(void) { return 60; }
double dm_pace_hz(double screen, double desired) { (void)screen; return desired; }
static int test_level;   // 渲染分辨率档位 0 自动 / 1 省电 / 2 均衡 / 3 原生 | render resolution level
int dm_render_res_level(void) { return test_level; }

static int checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr, "FAIL line %d: %s (step %d)\n", __LINE__, #x, dm_res_auto_step_get()); return 1; } } while (0)
static void feed(double ms, double seconds) { for (double t = 0; t < seconds * 1000; t += ms) { test_clock += (uint64_t)(ms * 1e6); dm_3d_scale_feed(ms); } }
static int pending31(void) { int n = 0; pthread_mutex_lock(&g_mq_lock); for (int i = 0; i < g_mqn; i++) n += g_mq[(g_mqh + i) % MQ] == MSG_PERFMODE; g_mqn = 0; pthread_mutex_unlock(&g_mq_lock); return n; }
// 把闭环推到第 2 步：持续 30 fps（33.33 ms），3D 先降到下限，之后输出档再降两步 | Push the loop to step 2: sustained 30 fps, 3D falls to its floor first, then the output level drops twice
static int drive_down(void) { for (int i = 0; i < 400 && dm_res_auto_step_get() < 2; i++) feed(33.333333, 2); return dm_res_auto_step_get(); }

int main(void) {
    unsetenv("DM_3D_SCALE_DYN"); unsetenv("DM_THERMAL_NOFALLBACK"); unsetenv("DM_3D_SCALE_DYN_INJECT"); unsetenv("DM_3D_SCALE_GPU"); unsetenv("DM_RES_AUTO_OFF");
    setenv("DM_3D_SCALE_FLOOR", "0.6", 1);
    g_ntf_perfmode = 1; test_level = 0; dm_3d_quality_mode_set(0); dm_res_auto_base_set(4000000u);
    // ① 自动档下闭环能降到第 2 步 | in Auto the loop reaches step 2
    feed(16.666667, 30); pending31();
    CHECK(drive_down() == 2); pending31();
    // ② 切画质优先：立即清零并通知一次；之后 200 秒健康帧也不再有额外降档 | switch to quality-first: cleared at once with one notification; no extra downstep through 200 s of healthy frames
    dm_3d_quality_mode_set(1); feed(16.666667, 1);
    CHECK(dm_res_auto_step_get() == 0); CHECK(pending31() == 1);
    feed(16.666667, 200); CHECK(dm_res_auto_step_get() == 0); CHECK(pending31() == 0);
    // ③ 画质优先下持续掉帧也不降 | no downstep in quality-first even while frames keep dropping
    feed(33.333333, 120); CHECK(dm_res_auto_step_get() == 0);
    // ④ 回到自动（流畅优先）：从第 0 步重新开始，旧步数不复活 | back to Auto smoothness: restarts at step 0, the old step does not come back
    dm_3d_quality_mode_set(0); feed(16.666667, 1); CHECK(dm_res_auto_step_get() == 0); pending31();
    CHECK(drive_down() == 2); pending31();
    // ⑤ 自动 → 手动档 → 自动：手动档期间清零并通知，回到自动从 0 开始 | Auto → manual level → Auto: cleared with a notification while manual, restarts at 0 back in Auto
    test_level = 2; feed(16.666667, 1); CHECK(dm_res_auto_step_get() == 0); CHECK(pending31() == 1);
    feed(33.333333, 120); CHECK(dm_res_auto_step_get() == 0);
    test_level = 0; feed(16.666667, 1); CHECK(dm_res_auto_step_get() == 0);
    // ⑥ 已在第 0 步时离开闭环不发多余通知 | leaving the loop at step 0 sends no spurious notification
    pending31(); test_level = 3; feed(16.666667, 2); CHECK(pending31() == 0); test_level = 0;
    // ⑦ 前端口径：画质优先时即使读到旧步数也不套用（纵深防御，见 front_ios.m） | front-end rule: never apply a step while quality-first (defence in depth, see front_ios.m)
#ifdef DM_RES_AUTO_HAVE_EFFECTIVE
    CHECK(dm_res_auto_budget(2800000u, dm_res_auto_step_effective(2, 0, 1)) == 2800000u);
    CHECK(dm_res_auto_budget(4000000u, dm_res_auto_step_effective(2, 0, 0)) == 2073600u);
    CHECK(dm_res_auto_step_effective(2, 3, 0) == 0);
#endif
    // ⑧ 复审 F05：基础 280 万（Duo 大屏）只有一档可降，且必须严格降低；基础 207 万不降、不发通知 | re-audit F05: a 2.8 MP base has one lower cap, strictly lower; a 2.07 MP base never steps or notifies
    test_level = 3; feed(16.666667, 1); test_level = 0; pending31();
    dm_res_auto_base_set(2800000u); feed(16.666667, 1); pending31();
    for (int i = 0; i < 400 && dm_res_auto_step_get() < 1; i++) feed(33.333333, 2);
    CHECK(dm_res_auto_step_get() == 1); CHECK(pending31() == 1); feed(33.333333, 200); CHECK(dm_res_auto_step_get() == 1); CHECK(pending31() == 0);
    CHECK(dm_res_auto_budget(2800000u, dm_res_auto_step_get()) == 2073600u);
    test_level = 3; feed(16.666667, 1); test_level = 0; pending31();
    dm_res_auto_base_set(2073600u); feed(33.333333, 300); CHECK(dm_res_auto_step_get() == 0); CHECK(pending31() == 0);
    // ⑨ 复审 F06：29 个干净窗后出局 120 秒再进局，22 秒内不得升档；之后要攒够进局后的新窗 | re-audit F06: 29 clean windows, 120 s out of the run, back in: no raise within 22 s, only fresh windows count
    test_level = 3; feed(16.666667, 1); test_level = 0; pending31(); dm_res_auto_base_set(4000000u); feed(16.666667, 1);
    CHECK(drive_down() == 2); pending31();
    feed(16.666667, 58); CHECK(dm_res_auto_step_get() == 2);   // 约 29 个干净窗，还差一个 | about 29 clean windows, one short
    test_ingame = 0; feed(16.666667, 120); test_ingame = 1;
    feed(16.666667, 22); CHECK(dm_res_auto_step_get() == 2);
    feed(16.666667, 40); CHECK(dm_res_auto_step_get() == 2);   // 进局停判 20 秒 + 重新进入停判 10 秒后，干净窗还不足 30 个 | after the 20 s entry hold + 10 s re-arm hold, still fewer than 30 fresh clean windows
    feed(16.666667, 60); CHECK(dm_res_auto_step_get() == 1);   // 攒够新的连续 60 秒才升 | raises only after 60 fresh continuous seconds
    // ⑩ 回前台同样作废证据 | returning to the foreground voids the evidence too
    for (int i = 0; i < 400 && dm_res_auto_step_get() < 2; i++) feed(33.333333, 2); pending31();
    feed(16.666667, 58); dm_3d_scale_resumed(); feed(16.666667, 4); CHECK(dm_res_auto_step_get() == 2);
    printf("PASS auto output loop exit policy: %d checks (quality-first, manual level, re-entry, no spurious 31)\n", checks);
    return 0;
}
