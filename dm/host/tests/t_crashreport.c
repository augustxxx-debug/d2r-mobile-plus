// 自动上报崩溃的策略（dm_crashreport.c）：开关关 / 没配地址 / 测试局一律不开尝试；异常退出才报；一次启动与跨启动的重试上限；Retry-After；4xx / 冲突不重试；结果提示写 .note、看过才删；关开关删待传报告。
// 不碰网络与游戏：假外壳记录调用，围栏与设置由测试给，时钟由测试给（dm_crashreport_boot / tick 直接调）。
// Automatic crash report policy: switch off / no origin / test run never start an attempt; only an abnormal exit is reported; retry limits per launch and across launches; Retry-After; 4xx / conflict not retried; the result goes to .note and is deleted once seen; turning off deletes the pending report.
// No network and no game: a fake shell records calls, the fence and settings come from the test, and so does the clock.
#include "../dm_crashreport.c"
#include <assert.h>
#include <sys/stat.h>
module_t g_mod[MAX_MOD]; int g_nmod;
static char g_dir[256];
static int f_on = 1, f_prev = 1, f_cfg = 1, f_cancels, f_starts, f_redraws; static const char *f_test; static unsigned f_tok; static char f_fence[160], f_dirarg[256];
int dm_custom_auto_crash(void) { return f_on; }
static int f_loaded = 1; int dm_custom_loaded(void) { return f_loaded; }
static int f_ingame; int dm_ingame_now(void) { return f_ingame; }
int dm_custom_dir(char *p, size_t n) { snprintf(p, n, "%s", g_dir); return 1; }
const char *dm_crashfence_test_env(void) { return f_test; }
int dm_crashfence_prev(int *cause, int *arg, int *streak, char *hist, size_t hn) { if (cause) *cause = 1; if (arg) *arg = 11; if (streak) *streak = 2; if (hist && hn) snprintf(hist, hn, "11"); return f_prev; }
void dm_update_redraw(void) { f_redraws++; }
static int s_cfg(void) { return f_cfg; }
static void s_start(unsigned t, const char *fence, const char *dir) { f_starts++; f_tok = t; snprintf(f_fence, sizeof f_fence, "%s", fence); snprintf(f_dirarg, sizeof f_dirarg, "%s", dir); }
static void s_cancel(void) { f_cancels++; }
static int f_prepares; static void s_prepare(const char *fence, const char *dir) { (void)fence; (void)dir; f_prepares++; }
static const dm_crashreport_shell_t shell = { s_cfg, s_start, s_cancel, s_prepare, 0 };
static const dm_crashreport_shell_t shell_self = { s_cfg, s_start, s_cancel, NULL, 1 };
static int checks, fails;
#define CHECK(x) do { checks++; if (!(x)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)
static int exists(const char *ext) { char p[1100]; snprintf(p, sizeof p, "%s/dm_crash_report.%s", g_dir, ext); return access(p, F_OK) == 0; }
static void touch(const char *ext) { char p[1100]; snprintf(p, sizeof p, "%s/dm_crash_report.%s", g_dir, ext); FILE *f = fopen(p, "w"); fputs("x", f); fclose(f); }
static void launch(double now, long long wall) { f_ingame = 0; atomic_store(&g_sw_on, 1); atomic_store(&g_sw_until, 0); atomic_store(&g_sw_seen, 1); memset(&S, 0, sizeof S); memset(&g_pend, 0, sizeof g_pend); g_note_key[0] = 0; g_note_until = 0; f_starts = 0; dm_crashreport_boot(now, wall); S.menu_since = -100; }   // 缺省场景：已在主菜单停够 | default scene: on the menu long enough
static void wipe(void) { static const char *const e[] = { "payload", "id", "state", "note" }; for (int i = 0; i < 4; i++) { char p[1100]; snprintf(p, sizeof p, "%s/dm_crash_report.%s", g_dir, e[i]); unlink(p); } char q[1100]; snprintf(q, sizeof q, "%s/dm_telemetry.txt", g_dir); unlink(q); }
int main(void) {
    snprintf(g_dir, sizeof g_dir, "/tmp/t_crashreport.%d", getpid()); mkdir(g_dir, 0700); unsetenv("DM_TEST_CRASHREPORT");
    dm_crashreport_init(&shell); int at = 0, la = 0;
    // 开关关：不开尝试、不写文件；遗留的待传报告删掉 | switch off: no attempt, no files; a leftover report is deleted
    f_on = 0; launch(0, 1000); dm_crashreport_tick(1000, 2000); CHECK(f_starts == 0 && dm_crashreport_state(0, 0) == DM_CR_OFF && !exists("state"));
    touch("state"); touch("payload"); touch("id"); launch(0, 1000); CHECK(!exists("state") && !exists("payload") && !exists("id")); f_on = 1;
    // 测试局没带 DM_TEST_CRASHREPORT：不动；带了才动 | test run without DM_TEST_CRASHREPORT: nothing
    f_test = "DM_KEYS"; launch(0, 1000); dm_crashreport_tick(1000, 2000); CHECK(f_starts == 0 && !exists("state"));
    setenv("DM_TEST_CRASHREPORT", "1", 1); launch(0, 1000); CHECK(dm_crashreport_state(0, 0) == DM_CR_WAIT); wipe(); unsetenv("DM_TEST_CRASHREPORT"); f_test = NULL;
    // 没配地址：不动 | no origin: nothing
    f_cfg = 0; launch(0, 1000); dm_crashreport_tick(1000, 2000); CHECK(f_starts == 0 && !exists("state")); f_cfg = 1;
    // 上一局正常：不动 | previous run fine: nothing
    f_prev = 0; launch(0, 1000); dm_crashreport_tick(1000, 2000); CHECK(f_starts == 0 && !exists("state")); f_prev = 1;
    // 异常退出：等 20 秒才开始，不在启动时；围栏行不带用户内容 | abnormal exit: starts 20 s later, not at launch; the fence line carries no user content
    launch(0, 1000); dm_crashreport_tick(19, 1019); CHECK(f_starts == 0 && exists("state"));
    dm_crashreport_tick(20, 1020); CHECK(f_starts == 1 && dm_crashreport_state(&at, &la) == DM_CR_RUNNING && at == 1 && la == 1);
    CHECK(!strcmp(f_fence, "fence cause=crash arg=11 streak=2 hist=11")); CHECK(!strcmp(f_dirarg, g_dir));
    // 成功：删报告、写 note、选角页那一行 20 秒，看过才删 note | success: report deleted, note written, 20 s on the select page, note deleted once seen
    touch("payload"); touch("id"); dm_crashreport_result(f_tok, DM_DIAGUP_OK, "0123456789abcdef0123456789abcdef", 0, 0); dm_crashreport_tick(21, 1021);
    CHECK(dm_crashreport_state(0, 0) == DM_CR_OFF && !exists("state") && !exists("payload") && !exists("id") && exists("note"));
    CHECK(!strcmp(dm_crashreport_note(), "dmCrashReportSent") && !strcmp(dm_crashreport_note_arg(), "01234567") && !exists("note"));
    // note 没被看到（进程先没了）：下次启动还在 | an unseen note survives to the next launch
    wipe(); f_prev = 0; touch("x"); { char p[1100]; snprintf(p, sizeof p, "%s/dm_crash_report.note", g_dir); FILE *f = fopen(p, "w"); fputs("dmCrashReportFailed \n", f); fclose(f); } g_note_key[0] = 0; launch(0, 5000); note_load(); CHECK(!strcmp(dm_crashreport_note(), "dmCrashReportFailed")); f_prev = 1; wipe();
    // 过期回调丢弃 | stale callback dropped
    launch(0, 1000); dm_crashreport_tick(20, 1020); dm_crashreport_result(f_tok + 7, DM_DIAGUP_OK, "x", 0, 0); dm_crashreport_tick(21, 1021); CHECK(dm_crashreport_state(0, 0) == DM_CR_RUNNING);
    // 一次启动最多 3 次：隔 60、300 秒静默重试（期间没有提示）；之后这次启动不再试、报告留着 | at most 3 per launch: 60 s and 300 s apart, silently; then the report waits for the next launch
    dm_crashreport_result(f_tok, DM_DIAGUP_UNAVAILABLE, "offline", 0, 0); dm_crashreport_tick(22, 1022); CHECK(dm_crashreport_state(0, 0) == DM_CR_WAIT && !g_note_key[0]);
    dm_crashreport_tick(81, 1081); CHECK(f_starts == 1); dm_crashreport_tick(83, 1083); CHECK(f_starts == 2);
    dm_crashreport_result(f_tok, DM_DIAGUP_UNCONFIRMED, "timeout", DM_DIAGUP_F_MAYBE, 0); dm_crashreport_tick(84, 1084); dm_crashreport_tick(380, 1380); CHECK(f_starts == 2); dm_crashreport_tick(385, 1385); CHECK(f_starts == 3);
    dm_crashreport_result(f_tok, DM_DIAGUP_UNAVAILABLE, "dns", 0, 0); dm_crashreport_tick(386, 1386); CHECK(dm_crashreport_state(&at, &la) == DM_CR_IDLE && at == 3 && exists("state") && !g_note_key[0]);
    dm_crashreport_tick(99999, 99999); CHECK(f_starts == 3);
    // 下次启动接着传（不看上一局有没有再异常）；总 6 次到顶就放弃并提示一次 | the next launch continues; 6 attempts overall, then give up with one notice
    f_prev = 0; launch(0, 2000); CHECK(dm_crashreport_state(&at, &la) == DM_CR_WAIT && at == 3 && la == 1);
    for (int i = 0; i < 3; i++) { dm_crashreport_tick(1000.0 * (i + 1), 2000 + 1000 * (i + 1)); dm_crashreport_result(f_tok, DM_DIAGUP_UNCONFIRMED, "timeout", 0, 0); dm_crashreport_tick(1000.0 * (i + 1) + 1, 2000 + 1000 * (i + 1) + 1); }
    CHECK(f_starts == 3 && dm_crashreport_state(&at, 0) == DM_CR_OFF && at == 6 && !exists("state") && !strcmp(dm_crashreport_note(), "dmCrashReportFailed")); wipe();
    // Retry-After：60 秒以上按服务端；超过 10 分钟这次启动不再试，下次启动也要等到点 | Retry-After honoured; beyond 10 minutes not this launch, and the next launch still waits for the time
    f_prev = 1; launch(0, 1000); dm_crashreport_tick(20, 1020); dm_crashreport_result(f_tok, DM_DIAGUP_RATE_LIMITED, "rate_limited", 0, 120); dm_crashreport_tick(21, 1021); dm_crashreport_tick(90, 1090); CHECK(f_starts == 1); dm_crashreport_tick(142, 1142); CHECK(f_starts == 2);
    dm_crashreport_result(f_tok, DM_DIAGUP_RATE_LIMITED, "rate_limited", 0, 3600); dm_crashreport_tick(143, 1143); CHECK(dm_crashreport_state(0, 0) == DM_CR_IDLE);
    f_prev = 0; launch(0, 1200); dm_crashreport_tick(30, 1230); CHECK(f_starts == 0); dm_crashreport_tick(5000, 4744); CHECK(f_starts == 1); f_prev = 1; wipe();
    // 明确拒收 / 冲突 / 过大 / 本机失败：不重试，删报告 | explicit refusal / conflict / too large / local failure: no retry
    int nr[] = { DM_DIAGUP_REJECTED, DM_DIAGUP_CONFLICT, DM_DIAGUP_TOO_LARGE, DM_DIAGUP_PREPARE, DM_DIAGUP_CONFIG };
    for (unsigned i = 0; i < sizeof nr / sizeof *nr; i++) { launch(0, 1000); dm_crashreport_tick(20, 1020); dm_crashreport_result(f_tok, nr[i], "invalid_metadata", 0, 0); dm_crashreport_tick(21, 1021); dm_crashreport_tick(9999, 9999); CHECK(f_starts == 1 && dm_crashreport_state(0, 0) == DM_CR_OFF && !exists("state")); wipe(); }
    // 看门狗：外壳不回话算结果未确认、取消、静默重试 | watchdog: unconfirmed, cancelled, silent retry
    f_cancels = 0; launch(0, 1000); dm_crashreport_tick(20, 1020); unsigned late = f_tok; dm_crashreport_tick(171, 1171); CHECK(f_cancels == 1 && dm_crashreport_state(0, 0) == DM_CR_WAIT);
    dm_crashreport_result(late, DM_DIAGUP_OK, "x", 0, 0); dm_crashreport_tick(172, 1172); CHECK(!g_note_key[0]); wipe();
    // 跨启动上限：第 4 次启动直接放弃（不再开尝试）；报告太旧也放弃 | across launches: the 4th launch gives up without an attempt; too old too
    f_prev = 0; launch(0, 1000); S.st = DM_CR_OFF; S.created = 1000; S.attempts = 3; S.launches = 3; snprintf(S.fence, sizeof S.fence, "fence x"); state_write(); launch(0, 2000); CHECK(dm_crashreport_state(0, 0) == DM_CR_OFF && !exists("state") && !strcmp(dm_crashreport_note(), "dmCrashReportFailed")); wipe();
    S.created = 1000; S.attempts = 1; S.launches = 1; snprintf(S.fence, sizeof S.fence, "fence x"); state_write(); launch(0, 1000 + 8 * 86400); CHECK(dm_crashreport_state(0, 0) == DM_CR_OFF && !exists("state")); wipe();
    // 玩家关开关：取消进行中的尝试、删报告；迟到的回调丢弃 | the player turns it off: running attempt cancelled, report deleted; a late callback is dropped
    f_prev = 1; f_cancels = 0; launch(0, 1000); dm_crashreport_tick(20, 1020); late = f_tok; touch("payload"); touch("id"); dm_crashreport_setting_changed(0);
    CHECK(f_cancels == 1 && dm_crashreport_state(0, 0) == DM_CR_OFF && !exists("state") && !exists("payload") && !exists("id"));
    dm_crashreport_result(late, DM_DIAGUP_OK, "x", 0, 0); dm_crashreport_tick(21, 1021); CHECK(!g_note_key[0]);
    dm_crashreport_setting_changed(1); CHECK(dm_crashreport_state(0, 0) == DM_CR_OFF);   // 开：下次启动才起作用 | on: from the next launch
    // 总开关：false = 零尝试、不占次数、报告只打包一次；缓存跨启动沿用；恢复 true 接着发 | server switch: false = no attempt, no budget, packed once; the cache carries across launches; true resumes
    launch(0, 1000); dm_crashreport_switch(1, 0, 0, "maint_01"); f_prepares = 0; dm_crashreport_tick(20, 1020); dm_crashreport_tick(5000, 6000); CHECK(f_starts == 0 && f_prepares == 1 && dm_crashreport_state(&at, &la) == DM_CR_WAIT && at == 0 && la == 0 && !g_note_key[0]);
    { char p[1100]; snprintf(p, sizeof p, "%s/dm_telemetry.txt", g_dir); CHECK(access(p, F_OK) == 0); }
    touch("payload"); touch("id"); f_prev = 0; memset(&S, 0, sizeof S); atomic_store(&g_sw_on, 1); atomic_store(&g_sw_seen, 1); f_starts = 0; f_prepares = 0; dm_crashreport_boot(0, 7000); S.menu_since = -100;   // 下次启动：清单拉不到，读盘上缓存，仍暂停；报告已在盘上不再打包 | next launch: no manifest, the disk cache still pauses; the report is on disk already
    dm_crashreport_tick(100, 7100); CHECK(f_starts == 0 && f_prepares == 0 && !atomic_load(&g_sw_on) && dm_crashreport_state(&at, &la) == DM_CR_WAIT && la == 0);
    dm_crashreport_switch(0, 0, 0, NULL); dm_crashreport_tick(101, 7101); CHECK(f_starts == 1 && dm_crashreport_state(&at, &la) == DM_CR_RUNNING && at == 1 && la == 1);   // 新清单没有这个字段 = 开，补发 | a manifest without the field = on, catch up
    dm_crashreport_result(f_tok, DM_DIAGUP_OK, "0123456789abcdef0123456789abcdef", 0, 0); dm_crashreport_tick(102, 7102); CHECK(dm_crashreport_state(0, 0) == DM_CR_OFF); wipe(); g_note_key[0] = 0; f_prev = 1;
    // pause_until 到点自动恢复；到点前零尝试 | pause_until passes: resumes by itself
    launch(0, 1000); dm_crashreport_switch(1, 0, 2000, NULL); CHECK(dm_crashreport_switch_paused(1999) && !dm_crashreport_switch_paused(2000)); dm_crashreport_tick(20, 1020); CHECK(f_starts == 0); dm_crashreport_tick(1100, 2000); CHECK(f_starts == 1); wipe();
    // 第一次尝试前先等清单，等不到 60 秒后按缓存走；拉取失败立刻按缓存 | the first attempt waits for the manifest, at most 60 s; a failed fetch proceeds on the cache at once
    launch(0, 1000); atomic_store(&g_sw_seen, 0); dm_crashreport_tick(20, 1020); CHECK(f_starts == 0); dm_crashreport_tick(61, 1061); CHECK(f_starts == 1); wipe();
    launch(0, 1000); atomic_store(&g_sw_seen, 0); dm_crashreport_tick(20, 1020); dm_crashreport_switch_no_fetch(); dm_crashreport_tick(21, 1021); CHECK(f_starts == 1); wipe();
    // 503 + Retry-After 当暂停：不占次数；至少隔 300 秒；一次启动探 3 次；Retry-After 很长就留到下次 | 503 + Retry-After is a pause
    launch(0, 1000); for (int i = 0; i < 3; i++) { dm_crashreport_tick(20.0 + 400 * i, 1020 + 400 * i); CHECK(f_starts == i + 1); dm_crashreport_result(f_tok, DM_DIAGUP_MAINTENANCE, "diagnostic_maintenance", 0, 5); dm_crashreport_tick(21.0 + 400 * i, 1021 + 400 * i); CHECK(dm_crashreport_state(&at, &la) == (i < 2 ? DM_CR_WAIT : DM_CR_IDLE) && at == 0 && la == 0); if (i == 0) { dm_crashreport_tick(300, 1300); CHECK(f_starts == 1); } } CHECK(!g_note_key[0] && exists("state")); wipe();
    launch(0, 1000); dm_crashreport_tick(20, 1020); dm_crashreport_result(f_tok, DM_DIAGUP_UNAVAILABLE, "storage_capacity", 0, 3600); dm_crashreport_tick(21, 1021); CHECK(dm_crashreport_state(&at, 0) == DM_CR_IDLE && at == 0); wipe();
    // 外壳自己查总开关（安卓）：这里不拦、不打包 | a shell that checks the switch itself (Android): not held here
    dm_crashreport_init(&shell_self); launch(0, 1000); dm_crashreport_switch(1, 0, 0, NULL); dm_crashreport_tick(20, 1020); CHECK(f_starts == 1); wipe(); dm_crashreport_init(&shell);
    // 局内零请求：到点在局内只打包；回主菜单停够 5 秒才发 | zero requests in a game: packs only when due in a game; sends after 5 s back on the menu
    launch(0, 1000); f_ingame = 1; f_prepares = 0; dm_crashreport_tick(20, 1020); dm_crashreport_tick(500, 1500); CHECK(f_starts == 0 && f_prepares == 1); f_ingame = 0; dm_crashreport_tick(501, 1501); CHECK(f_starts == 0); dm_crashreport_tick(505, 1505); CHECK(f_starts == 0); dm_crashreport_tick(506, 1506); CHECK(f_starts == 1); wipe();
    // 蜂窝：摘要送达提示一次、不占次数，完整版等 Wi-Fi；Wi-Fi 上完整版送达不再提示；报告一直留到那时 | metered: the summary notifies once with no budget used, the full report waits for Wi-Fi and is silent
    launch(0, 1000); dm_crashreport_tick(20, 1020); dm_crashreport_result(f_tok, DM_DIAGUP_SUMMARY_SENT, "0123456789abcdef0123456789abcdef", 0, 0); dm_crashreport_tick(21, 1021); CHECK(dm_crashreport_state(&at, &la) == DM_CR_WAIT && at == 0 && la == 0 && S.summary == 1 && !strcmp(g_note_key, "dmCrashReportSent") && exists("state"));
    dm_crashreport_note(); g_note_key[0] = 0; dm_crashreport_tick(321, 1321); dm_crashreport_result(f_tok, DM_DIAGUP_WAIT_WIFI, "wifi", 0, 0); dm_crashreport_tick(322, 1322); CHECK(dm_crashreport_state(&at, 0) == DM_CR_WAIT && at == 0); dm_crashreport_tick(622, 1622); dm_crashreport_result(f_tok, DM_DIAGUP_WAIT_WIFI, "wifi", 0, 0); dm_crashreport_tick(623, 1623); CHECK(dm_crashreport_state(0, 0) == DM_CR_IDLE && exists("state"));
    f_prev = 0; launch(0, 5000); CHECK(S.summary == 1); dm_crashreport_tick(20, 5020); dm_crashreport_result(f_tok, DM_DIAGUP_OK, "fedcba9876543210fedcba9876543210", 0, 0); dm_crashreport_tick(21, 5021); CHECK(dm_crashreport_state(0, 0) == DM_CR_OFF && !exists("state") && !g_note_key[0]); f_prev = 1; wipe();
    // 摘要之后完整版一直没等到 Wi-Fi：7 天后删除，不报失败 | never got Wi-Fi after the summary: deleted after 7 days, no failure notice
    f_prev = 0; memset(&S, 0, sizeof S); S.created = 1000; S.summary = 1; S.launches = 0; snprintf(S.fence, sizeof S.fence, "fence x"); state_write(); launch(0, 1000 + 8 * 86400); CHECK(dm_crashreport_state(0, 0) == DM_CR_OFF && !exists("state") && !g_note_key[0]); f_prev = 1; wipe();
    // 设置文件还没读到：泵不判定、不删待传报告；读到才判定 | settings not read yet: the pump decides nothing and deletes nothing; it decides once they are read
    f_on = 0; f_loaded = 0; memset(&S, 0, sizeof S); touch("state"); dm_crashreport_pump(); CHECK(!S.booted && exists("state")); f_loaded = 1; dm_crashreport_pump(); CHECK(S.booted && !exists("state")); f_on = 1; wipe();
    printf("t_crashreport: %d checks, %d failed\n", checks, fails); rmdir(g_dir);
    return fails != 0;
}
