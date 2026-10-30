// 上传诊断报告的状态机（dm_diagup.c）：没配上传 = 原导出链路；确认 → 一次尝试；结果只认当前 token（旧回调丢弃）；各结果的界面字与按钮；
// 重试沿用同一任务、新任务换任务号；Retry-After 未到不发；连点只开一个任务；关掉提示框上传继续、结果下次打开时显示；冲突不换编号掩盖；看门狗；手柄模式的按键说明。
// 不碰网络与游戏：假外壳记录调用，时钟由测试给（dm_diagup_tick / dm_diagup_view 直接调）。
// Diagnostic upload state machine (dm_diagup.c): no upload configured = original export; confirm → one attempt; results only for the current token (stale callbacks dropped); text and buttons per result;
// retries keep the job, a new report gets a new job; nothing sent before Retry-After; repeated taps start one job; closing the box keeps the upload going and the result shows on the next open; conflicts are not masked; watchdog; controller key legends.
// No network and no game: a fake shell records calls and the test drives the clock (dm_diagup_tick / dm_diagup_view called directly).
#include "../dm_diagup.c"
#include <assert.h>
module_t g_mod[MAX_MOD]; int g_nmod;
int dm_padmode_current(void) { return 0; }
uint64_t dm_padmode_page_keys(uint64_t m) { (void)m; return 0; }
const char *dm_str(uint8_t *b, const char *k) { (void)b; return k; }
static int cr_cfg = 1; int dm_crashreport_configured(void) { return cr_cfg; }
static int checks, fails;
#define CHECK(x) do { checks++; if (!(x)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)
static int cfg = 1, starts, exports, cancels, copies; static unsigned last_token, last_job; static char copied[64];
static int f_configured(void) { return cfg; }
static void f_start(unsigned token, unsigned job) { starts++; last_token = token; last_job = job; }
static void f_cancel(void) { cancels++; }
static void f_export(void) { exports++; }
static int f_copy(const char *s) { copies++; snprintf(copied, sizeof copied, "%s", s); return 1; }
static const dm_diagup_shell_t shell = { f_configured, f_start, f_cancel, f_export, f_copy };
static void shown(void) { if (atomic_load(&g_show) == 1) atomic_store(&g_show, 2); }   // 泵在游戏里把 1 → 2；这里替它 | the pump turns 1 into 2 in the game
static void press(const char *name) { shown(); dm_diagup_on_uimsg("PanelManager", "ClosePanel", name); }
static void hide(void) { if (atomic_load(&g_show) == 3) atomic_store(&g_show, 0); }
#define ID "0123456789abcdef0123456789abcdef"
static int answers, answer; static void consent(int yes) { answers++; answer = yes; }
int main(void) {
    dm_diagup_view_t v; unsigned job, tok; int out; char det[80];
    // 没登记外壳 / 没配地址 / 测试开关：交回原导出链路 | no shell / no origin / test switch: back to the original export
    CHECK(dm_diagup_open(1) == 0);
    dm_diagup_init(&shell); cfg = 0; CHECK(dm_diagup_open(1) == 0); cfg = 1;
    setenv("DM_TEST_SHARELOG_CLOSE", "3", 1); CHECK(dm_diagup_open(1) == 0); unsetenv("DM_TEST_SHARELOG_CLOSE");
    // 打开 = 确认页：上传 / 导出 / 取消；按钮不在显示前收（消息被忽略） | open = consent page
    dm_diagup_on_uimsg("PanelManager", "ClosePanel", "TouchDiagUpload"); dm_diagup_tick(0); CHECK(starts == 0);
    CHECK(dm_diagup_open(1) == 1); CHECK(dm_diagup_open(1) == 1);   // 连点：只开一个 | repeated taps: one box
    dm_diagup_view(0, 0, &v); CHECK(!strcmp(v.title, "dmDiagTitle") && v.upload && v.exp && v.cancel && !v.close && !v.retry && !strcmp(v.sub, "dmDiagConsentHint"));
    dm_diagup_view(0, 1, &v); CHECK(!strcmp(v.sub, "dmDiagPadConsent"));
    // 取消：什么都不发，下次重开回到确认页 | cancel: nothing sent
    press("TouchDiagCancel"); dm_diagup_tick(0); hide(); CHECK(starts == 0 && dm_diagup_state(0, 0, 0, 0, 0) == DM_DIAGUP_ST_IDLE);
    // 确认上传 → 任务 1 第 1 次尝试；上传中再点上传不叠第二个 | confirm → job 1 attempt 1; another Upload while running stacks nothing
    dm_diagup_open(1); press("TouchDiagUpload"); dm_diagup_tick(1); CHECK(starts == 1 && last_job == 1);
    press("TouchDiagUpload"); dm_diagup_tick(1.1); CHECK(starts == 1);
    dm_diagup_view(1, 0, &v); CHECK(!strcmp(v.title, "dmDiagSendingTitle") && v.close && !v.upload && !v.exp);
    // 旧 token 的回调丢弃；当前 token 的「结果未确认」收下 | a stale token is dropped; the current one is taken
    dm_diagup_result(last_token + 7, DM_DIAGUP_OK, ID, 1, 0, 0, 7); dm_diagup_tick(2); CHECK(dm_diagup_state(0, 0, 0, 0, 0) == DM_DIAGUP_ST_RUNNING);
    dm_diagup_result(last_token, DM_DIAGUP_UNCONFIRMED, "response_lost", 0, DM_DIAGUP_F_MAYBE, 0, 0); dm_diagup_tick(3);
    CHECK(dm_diagup_state(&job, &tok, &out, det, sizeof det) == DM_DIAGUP_ST_RESULT && out == DM_DIAGUP_UNCONFIRMED && !strcmp(det, "response_lost"));
    dm_diagup_view(3, 0, &v); CHECK(!strcmp(v.title, "dmDiagUnconfTitle") && v.retry && v.exp && v.close && !strcmp(v.line1, "dmDiagWhy") && !strcmp(v.arg1, "@dmDiagWhyLost") && !strcmp(v.sub, "dmDiagUnconfHint"));
    dm_diagup_view(3, 1, &v); CHECK(!strcmp(v.sub, "dmDiagPadRetry"));
    // 同一结果再到一次（重复回调）：不改状态 | the same result arriving twice changes nothing
    dm_diagup_result(tok, DM_DIAGUP_OK, ID, 1, 0, 0, 7); dm_diagup_tick(3.5); dm_diagup_state(0, 0, &out, 0, 0); CHECK(out == DM_DIAGUP_UNCONFIRMED);
    // 重试：同一任务、新 token；成功（duplicate）→ 报告编号来自服务端回执 | retry: same job, new token; success with duplicate
    press("TouchDiagRetry"); dm_diagup_tick(4); CHECK(starts == 2 && last_job == job && last_token == tok + 1);
    dm_diagup_result(last_token, DM_DIAGUP_OK, ID, 1893456000, DM_DIAGUP_F_DUPLICATE, 0, 7); dm_diagup_tick(5);
    dm_diagup_view(5, 0, &v); CHECK(!strcmp(v.title, "dmDiagDoneTitle") && !strcmp(v.arg0, ID) && !strcmp(v.line1, "dmDiagDoneDup") && v.copy && v.close && !v.retry && !v.exp);
    press("TouchDiagCopy"); dm_diagup_tick(5); CHECK(copies == 1 && !strcmp(copied, ID)); dm_diagup_view(5, 0, &v); CHECK(!strcmp(v.sub, "dmDiagCopied"));
    press("TouchDiagRetry"); dm_diagup_tick(5); CHECK(starts == 2);   // 成功后不许重试 | no retry after success
    press("TouchDiagClose"); dm_diagup_tick(6); hide();
    // 成功看过之后再开 = 新报告（新任务号） | after a seen success, opening again = a new report
    dm_diagup_open(1); dm_diagup_view(6, 0, &v); CHECK(!strcmp(v.title, "dmDiagTitle"));
    press("TouchDiagUpload"); dm_diagup_tick(7); CHECK(starts == 3 && last_job == job + 1);
    // 限流：Retry-After 未到不发、显示倒计时；到了才发 | rate limited: nothing sent before Retry-After
    dm_diagup_result(last_token, DM_DIAGUP_RATE_LIMITED, "rate_limited", 0, 0, 60, 0); dm_diagup_tick(8);
    dm_diagup_view(8, 0, &v); CHECK(!strcmp(v.title, "dmDiagRateTitle") && !v.retry && v.exp && !strcmp(v.line1, "dmDiagWait") && !strcmp(v.arg1, "60"));
    press("TouchDiagRetry"); dm_diagup_tick(30); CHECK(starts == 3);
    dm_diagup_view(68.5, 0, &v); CHECK(v.retry && !strcmp(v.line1, "dmDiagRetryNow"));
    press("TouchDiagRetry"); dm_diagup_tick(69); CHECK(starts == 4);
    // 维护：同样等 Retry-After；「可能已送达」时正文换说法 | maintenance: same wait; maybe-delivered changes the wording
    dm_diagup_result(last_token, DM_DIAGUP_MAINTENANCE, "diagnostic_maintenance", 0, DM_DIAGUP_F_MAYBE, 30, 0); dm_diagup_tick(70);
    dm_diagup_view(70, 0, &v); CHECK(!strcmp(v.title, "dmDiagMaintTitle") && !strcmp(v.line0, "dmDiagMaybe") && !v.retry && !strcmp(v.sub, "dmDiagLaterHint"));
    // 关掉提示框：上传中的结果照收，下次打开先看到它 | closing the box: a running upload's result is kept and shown on the next open
    press("TouchDiagClose"); dm_diagup_tick(71); hide();
    CHECK(dm_diagup_open(1) == 1); press("TouchDiagRetry"); dm_diagup_tick(101); CHECK(starts == 5);
    press("TouchDiagClose"); dm_diagup_tick(102); hide(); CHECK(dm_diagup_state(0, 0, 0, 0, 0) == DM_DIAGUP_ST_RUNNING);
    dm_diagup_result(last_token, DM_DIAGUP_UNAVAILABLE, "offline", 0, 0, 0, 0); dm_diagup_tick(103);
    dm_diagup_open(1); dm_diagup_view(103, 0, &v); CHECK(!strcmp(v.title, "dmDiagUnavailTitle") && !strcmp(v.arg1, "@dmDiagWhyOffline") && v.retry);
    // 导出到本地：收起提示框、走原导出 | export locally: dismiss and run the original export
    press("TouchDiagExport"); dm_diagup_tick(104); CHECK(exports == 1 && atomic_load(&g_show) == 3); hide();
    // 其他 4xx：显示错误码、不能重试；看过后再开 = 新报告 | other 4xx: code shown, no retry
    dm_diagup_open(1); press("TouchDiagRetry"); dm_diagup_tick(105); CHECK(starts == 6);
    dm_diagup_result(last_token, DM_DIAGUP_REJECTED, "invalid_metadata", 0, 0, 0, 0); dm_diagup_tick(106);
    dm_diagup_view(106, 0, &v); CHECK(!strcmp(v.title, "dmDiagRejectTitle") && !strcmp(v.line1, "dmDiagCode") && !strcmp(v.arg1, "invalid_metadata") && !v.retry && v.exp && !strcmp(v.sub, "dmDiagExportHint"));
    dm_diagup_view(106, 1, &v); CHECK(!strcmp(v.sub, "dmDiagPadExport"));
    press("TouchDiagClose"); dm_diagup_tick(107); hide(); dm_diagup_open(1); dm_diagup_view(107, 0, &v); CHECK(!strcmp(v.title, "dmDiagTitle"));
    // 冲突：一直显示那份结果，不换编号重发 | conflict: the result sticks, never re-sent under a new id
    press("TouchDiagUpload"); dm_diagup_tick(108); dm_diagup_result(last_token, DM_DIAGUP_CONFLICT, "upload_conflict", 0, 0, 0, 0); dm_diagup_tick(109);
    press("TouchDiagClose"); dm_diagup_tick(110); hide(); dm_diagup_open(1); dm_diagup_view(110, 0, &v); CHECK(!strcmp(v.arg1, "upload_conflict") && !v.upload && !v.retry);
    press("TouchDiagClose"); dm_diagup_tick(111); hide();
    // 看门狗：一直没回话 → 结果未确认，作废 token；迟到的回调丢弃 | watchdog
    S.st = DM_DIAGUP_ST_CONFIRM; dm_diagup_open(1); press("TouchDiagUpload"); dm_diagup_tick(200); unsigned late = last_token;
    dm_diagup_tick(200 + WATCHDOG_S + 1); dm_diagup_state(0, &tok, &out, det, sizeof det); CHECK(out == DM_DIAGUP_UNCONFIRMED && !strcmp(det, "watchdog") && cancels == 1 && tok == late + 1);
    dm_diagup_result(late, DM_DIAGUP_OK, ID, 1, 0, 0, 7); dm_diagup_tick(400); dm_diagup_state(0, 0, &out, 0, 0); CHECK(out == DM_DIAGUP_UNCONFIRMED);
    // 过大：不能重试、只能导出 | too large: no retry, export only
    press("TouchDiagRetry"); dm_diagup_tick(401); dm_diagup_result(last_token, DM_DIAGUP_TOO_LARGE, "local_limit", 0, 0, 0, 0); dm_diagup_tick(402);
    dm_diagup_view(402, 0, &v); CHECK(!strcmp(v.title, "dmDiagLargeTitle") && !v.retry && v.exp);
    // 自动上报确认页：没配地址只说明、只有「关闭」、答不开启；配了有「开启」/「不开启」；答复只来一次；开着时吞手柄键 | consent page: no origin explains with Close only and answers no; with an origin Enable / Not now; one answer only; the gate swallows the controller
    press("TouchDiagClose"); dm_diagup_tick(500); hide(); S.st = DM_DIAGUP_ST_IDLE;
    cr_cfg = 0; dm_diagup_consent_open(consent); dm_diagup_view(500, 0, &v); CHECK(!strcmp(v.title, "dmAutoCrashNoSrvTitle") && v.close && !v.enable && !v.cancel);
    press("TouchDiagClose"); dm_diagup_tick(501); CHECK(answers == 1 && answer == 0); hide(); cr_cfg = 1;
    dm_diagup_consent_open(consent); dm_diagup_view(502, 0, &v); CHECK(!strcmp(v.title, "dmAutoCrashTitle") && v.enable && v.cancel && !v.upload && !v.exp && !strcmp(v.sub, "dmAutoCrashHint"));
    dm_diagup_view(502, 1, &v); CHECK(!strcmp(v.sub, "dmAutoCrashPad"));
    press("TouchDiagEnable"); dm_diagup_tick(503); CHECK(answers == 2 && answer == 1 && atomic_load(&g_show) == 3); press("TouchDiagEnable"); dm_diagup_tick(504); CHECK(answers == 2); hide();
    dm_diagup_consent_open(consent); press("TouchDiagCancel"); dm_diagup_tick(505); CHECK(answers == 3 && answer == 0); hide();
    CHECK(dm_diagup_state(0, 0, 0, 0, 0) == DM_DIAGUP_ST_IDLE);   // 确认页不碰手动上传的任务 | the consent page does not touch the manual upload job
    // 闸门：开着时清零、按下沿留给框；关掉后按着的键松开前也不交；交给底层计数 0 | gate: zeroes while up, edges kept for the box; nothing passes until released after closing; leak count 0
    { uint64_t bt = 0; int32_t lx = 0, ly = 0, rx = 0, ry = 0; dm_diagup_input_gate(&bt, &lx, &ly, &rx, &ry);
      dm_diagup_consent_open(consent); shown(); bt = DM_BTN_A; lx = 20000; dm_diagup_input_gate(&bt, &lx, &ly, &rx, &ry); CHECK(bt == 0 && lx == 0 && (atomic_load(&g_md_ev) & DM_BTN_A)); dm_diagup_input_leak(bt || lx);
      atomic_store(&g_show, 0); g_consent_cb = NULL; bt = DM_BTN_A; lx = 0; dm_diagup_input_gate(&bt, &lx, &ly, &rx, &ry); CHECK(bt == 0);
      bt = 0; dm_diagup_input_gate(&bt, &lx, &ly, &rx, &ry); CHECK(!atomic_load(&g_after)); bt = DM_BTN_A; dm_diagup_input_gate(&bt, &lx, &ly, &rx, &ry); CHECK(bt == DM_BTN_A); CHECK(dm_diagup_hit_answer() == -1 && !dm_diagup_modal()); atomic_store(&g_md_ev, 0); }
    printf("t_diagup: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
