// 「上传诊断报告」，接口与做法见 dm_diagup.h。
// 画面（同社区入口 dm_community.c 的做法）：边框 15 片 TouchDiagTile（我们通知框同一套美术、自己一套切片 Ztouch_diag）+ 标题 + 三行字（正文两行 36 号、副文一行 28 号，§2.6）
//   + 六颗原版前端按钮（上传 / 重试 / 导出到本地 / 复制编号 / 取消 / 关闭，宿主按状态露出其中至多三颗），布局 touch_layout.py _diagbox，挂在暂停菜单面板（两份）末尾，平时在屏外、字空着。
//   触屏点按钮；手柄模式接管 A（主操作：上传 / 重试 / 复制编号）、X（导出到本地）、B（取消 / 关闭），副文改写成按键说明。
// 日志固定前缀「诊断上传：」（分享诊断日志摘要取最后一行）。不写上传地址、不写签名材料；报告编号是服务端给的公开编号，可以写。
// "Upload diagnostic report"; interface and approach in dm_diagup.h.
// Picture (same approach as the community entry dm_community.c): 15 frame tiles TouchDiagTile (same art as our notification box, its own slices Ztouch_diag) + title + three lines (two body lines at 36, one secondary line at 28, §2.6)
//   + six original front-end buttons (Upload / Retry / Export locally / Copy number / Cancel / Close; the host shows at most three by state), layout touch_layout.py _diagbox, appended to the pause menu panel (both), normally off screen with empty text.
//   Touch taps the buttons; controller mode takes over A (main action: upload / retry / copy number), X (export locally) and B (cancel / close), and the secondary line becomes the key legend.
// Log prefix "诊断上传：" (the share summary takes the last line). The upload origin and signing material are never logged; the report number is the server's public id and may be.
#include "dm_diagup.h"
#include "dm_crashreport.h"
#include "dm_i18n.h"
#include "dm_input.h"
#include "dm_widget.h"
#include "dm_notice_geometry.h"
#include "hle.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define WATCHDOG_S 150   // 外壳每次尝试有自己的超时（单个请求 15 秒、整个 30 秒，至多十来个请求）；过了这么久还没回话就按「结果未确认」收场 | the shell has its own timeouts; past this the attempt ends as unconfirmed

static const dm_diagup_shell_t *g_shell;
static struct {   // 只在游戏主线程读写 | game main thread only
    int st; unsigned job, token;
    int outcome, flags, retry_after, retention; long long expires; char detail[80];
    double t_start, t_retry; int unseen, from;
} S;
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;   // 护 g_pend（结果从外壳的后台线程来） | guards g_pend (results come from the shell's background thread)
static struct { int valid; unsigned token; int outcome, flags, retry_after, retention; long long expires; char detail[80]; } g_pend;
static _Atomic int g_show;    // 0 不显示 / 1 要显示 / 2 显示着 | 0 hidden / 1 pending / 2 shown
static _Atomic int g_action;  // 按钮动作（界面消息 / 手柄键攒下来，tick 里做）：1 上传 2 重试 3 导出 4 复制 5 关闭 6 同意开启 | button actions queued for tick
static void (*g_consent_cb)(int);   // 非空 = 提示框正显示「自动上报崩溃」确认页（只在游戏主线程读写） | non-NULL = the box shows the crash report consent page (game main thread only)
static _Atomic uint64_t g_md_ev;    // 闸门攒给框的按下沿（A / X / B） | press edges collected for the box
static _Atomic unsigned g_sw_touch, g_sw_key, g_leak; static _Atomic int g_after; static _Atomic unsigned g_after_n;
int dm_diagup_modal(void) { return atomic_load(&g_show) != 0; }
int dm_diagup_hit_answer(void) { return atomic_load(&g_show) ? 0 : -1; }
static int stick_off(int32_t v) { return v > -8000 && v < 8000; }
// 手柄闸门（hle_hid.c 取手柄处最后调，做法同 dm_community_input_gate）：开着时键与两根摇杆全清零、按下沿留给框；关掉后这些键全松开、摇杆回到 1/4 以内之前也不交给游戏 | controller gate (last at the controller poll, as dm_community_input_gate): while up everything is zeroed and press edges go to the box; after it closes nothing reaches the game until keys are released and sticks are back inside a quarter
void dm_diagup_input_gate(uint64_t *bt, int32_t *lx, int32_t *ly, int32_t *rx, int32_t *ry) {
    static uint64_t prev, held; static int stick;
    if (!atomic_load(&g_show)) {
        if (atomic_load(&g_after) && bt && lx && ly && rx && ry) {
            int centred = stick_off(*lx) && stick_off(*ly) && stick_off(*rx) && stick_off(*ry);
            held &= *bt; if (centred) stick = 0;
            if (held || stick) { atomic_fetch_add(&g_after_n, 1); *bt = 0; *lx = *ly = *rx = *ry = 0; }
            else { atomic_store(&g_after, 0); LOG("诊断上传：关框后压住按着的键 / 摇杆 %u 拍，弹窗期间按键 %u 次、点按 %u 次，交给底层 %u", atomic_exchange(&g_after_n, 0), atomic_exchange(&g_sw_key, 0), atomic_exchange(&g_sw_touch, 0), atomic_exchange(&g_leak, 0)); } }
        prev = bt ? *bt : 0; return; }
    if (!bt || !lx || !ly || !rx || !ry) return;
    held = *bt; stick = !(stick_off(*lx) && stick_off(*ly) && stick_off(*rx) && stick_off(*ry)); atomic_store(&g_after, 1); atomic_store(&g_after_n, 0);
    uint64_t down = *bt & ~prev; prev = *bt;
    if (down) atomic_fetch_add(&g_sw_key, 1);
    if (down & (DM_BTN_A | DM_BTN_B | DM_BTN_X)) atomic_fetch_or(&g_md_ev, down & (DM_BTN_A | DM_BTN_B | DM_BTN_X));
    *bt = 0; *lx = *ly = *rx = *ry = 0;
}
void dm_diagup_input_leak(int leaked) { if (leaked && (atomic_load(&g_show) || atomic_load(&g_after))) atomic_fetch_add(&g_leak, 1); }
static int g_copied;          // 0 / 1 已复制 / -1 复制失败 | 0 / 1 copied / -1 copy failed

void dm_diagup_init(const dm_diagup_shell_t *shell) { g_shell = shell; }
int dm_diagup_shown(void) { return atomic_load(&g_show) == 2; }
static double now_s(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }
static int retryable(int o) { return o == DM_DIAGUP_UNCONFIRMED || o == DM_DIAGUP_UNAVAILABLE || o == DM_DIAGUP_MAINTENANCE || o == DM_DIAGUP_RATE_LIMITED; }
static const char *outcome_name(int o) {
    static const char *const n[] = { "已取消", "已上传", "结果未确认", "未发出", "服务维护中", "次数受限", "报告过大", "服务端拒收", "上传冲突（客户端缺陷）", "本机准备失败", "未配置" };
    return o >= 0 && o <= DM_DIAGUP_CONFIG ? n[o] : "未知";
}
int dm_diagup_state(unsigned *job, unsigned *token, int *outcome, char *detail, size_t n) {
    if (job) *job = S.job; if (token) *token = S.token; if (outcome) *outcome = S.outcome; if (detail && n) snprintf(detail, n, "%s", S.detail);
    return S.st;
}

int dm_diagup_open(int from) {
    if (!g_shell || !g_shell->configured || !g_shell->configured() || getenv("DM_TEST_SHARELOG_CLOSE")) return 0;   // 原链路：直接导出 | original path: export directly
    if (atomic_load(&g_show)) return 1;   // 已开着：这一下不再叠 | already open: this tap stacks nothing
    S.from = from;
    // 上一份还在传 → 显示进度；上一份结果玩家还没看过、或还能重试、或是冲突（不换编号掩盖）→ 显示那份结果；否则从确认页开始一份新报告
    // still uploading → progress; a result not yet seen, still retryable, or a conflict (never masked with a new id) → that result; otherwise start from the consent page
    if (S.st == DM_DIAGUP_ST_RESULT && !S.unseen && !retryable(S.outcome) && S.outcome != DM_DIAGUP_CONFLICT) S.st = DM_DIAGUP_ST_CONFIRM;
    if (S.st == DM_DIAGUP_ST_IDLE) S.st = DM_DIAGUP_ST_CONFIRM;
    g_copied = 0; atomic_store(&g_action, 0); atomic_store(&g_show, 1);
    LOG("诊断上传：打开提示框（%s，%s）", from == 1 ? "暂停菜单" : "其他入口", S.st == DM_DIAGUP_ST_CONFIRM ? "确认页" : S.st == DM_DIAGUP_ST_RUNNING ? "上传中" : outcome_name(S.outcome));
    return 1;
}

void dm_diagup_result(unsigned token, int outcome, const char *detail, long long expires_at, int flags, int retry_after, int retention_days) {
    pthread_mutex_lock(&g_mu);
    g_pend.valid = 1; g_pend.token = token; g_pend.outcome = outcome; g_pend.flags = flags; g_pend.retry_after = retry_after; g_pend.retention = retention_days; g_pend.expires = expires_at;
    snprintf(g_pend.detail, sizeof g_pend.detail, "%s", detail ? detail : "");
    pthread_mutex_unlock(&g_mu);
}

void dm_diagup_on_uimsg(const char *cat, const char *act, const char *arg) {   // 界面消息回调（diag.c 转来，游戏主线程） | UI message callback (from diag.c, game main thread)
    if (!cat || !act || !arg || strcmp(cat, "PanelManager") || strcmp(act, "ClosePanel") || strncmp(arg, "TouchDiag", 9) || atomic_load(&g_show) != 2) return;
    static const char *const names[] = { "TouchDiagUpload", "TouchDiagRetry", "TouchDiagExport", "TouchDiagCopy", "TouchDiagCancel", "TouchDiagClose" };
    static const int acts[] = { 1, 2, 3, 4, 5, 5 };
    for (int i = 0; i < 6; i++) if (!strcmp(arg, names[i])) atomic_store(&g_action, acts[i]);
    if (!strcmp(arg, "TouchDiagEnable")) atomic_store(&g_action, 6);
}

static void start_attempt(double now, int retry) {
    if (!retry) { S.job++; S.flags = 0; }
    S.token++; S.st = DM_DIAGUP_ST_RUNNING; S.t_start = now; S.outcome = 0; S.detail[0] = 0; g_copied = 0;
    LOG("诊断上传：%s（任务 %u 第 %u 次尝试）", retry ? "玩家点了重试，沿用同一份报告、换新挑战" : "玩家确认上传，开始收集与脱敏", S.job, S.token);
    g_shell->start(S.token, S.job);
}

void dm_diagup_tick(double now) {
    // ① 外壳交回的结果：只认当前这次尝试，且还在「上传中」 | ① a result from the shell: only for the current attempt, still running
    pthread_mutex_lock(&g_mu); typeof(g_pend) p = g_pend; g_pend.valid = 0; pthread_mutex_unlock(&g_mu);
    if (p.valid) {
        if (p.token != S.token || S.st != DM_DIAGUP_ST_RUNNING || p.outcome == DM_DIAGUP_CANCELLED) LOG("诊断上传：丢弃过期回调（第 %u 次尝试的%s，当前第 %u 次）", p.token, outcome_name(p.outcome), S.token);
        else {
            S.st = DM_DIAGUP_ST_RESULT; S.outcome = p.outcome; S.flags = p.flags; S.retention = p.retention; S.expires = p.expires;
            S.retry_after = p.retry_after; S.t_retry = (p.outcome == DM_DIAGUP_MAINTENANCE || p.outcome == DM_DIAGUP_RATE_LIMITED) ? now + (p.retry_after > 0 ? p.retry_after : 60) : 0;
            snprintf(S.detail, sizeof S.detail, "%s", p.detail); S.unseen = atomic_load(&g_show) != 2;
            if (p.outcome == DM_DIAGUP_OK) LOG("诊断上传：已上传，报告编号 %s%s（任务 %u，用时 %.1f 秒）", S.detail, p.flags & DM_DIAGUP_F_DUPLICATE ? "（重试命中已存的那份，duplicate）" : "", S.job, now - S.t_start);
            else LOG("%s诊断上传：%s（%s%s%s，任务 %u，用时 %.1f 秒）", p.outcome == DM_DIAGUP_CONFLICT ? "【红】" : "", outcome_name(p.outcome), S.detail,
                     p.flags & DM_DIAGUP_F_MAYBE ? "，此前的请求可能已送达" : "", S.t_retry ? "，按服务端要求稍后才能重试" : "", S.job, now - S.t_start);
        }
    }
    // ② 看门狗：外壳一直没回话 → 结果未确认，作废这次尝试的 token | ② watchdog: no answer from the shell → unconfirmed, the attempt's token is retired
    if (S.st == DM_DIAGUP_ST_RUNNING && now - S.t_start > WATCHDOG_S) {
        if (g_shell && g_shell->cancel) g_shell->cancel();
        S.token++; S.st = DM_DIAGUP_ST_RESULT; S.outcome = DM_DIAGUP_UNCONFIRMED; S.flags |= DM_DIAGUP_F_MAYBE; snprintf(S.detail, sizeof S.detail, "watchdog"); S.unseen = atomic_load(&g_show) != 2;
        LOG("【红】诊断上传：%d 秒没等到结果，按结果未确认处理（任务 %u）", WATCHDOG_S, S.job);
    }
    // ③ 按钮动作（只在提示框显示着时攒得下） | ③ button actions (only queued while the box shows)
    int a = atomic_exchange(&g_action, 0); if (!a) return;
    if (g_consent_cb) {   // 确认页只有「开启」与「不开启 / 关闭」 | the consent page only has Enable and Not now / Close
        if (a == 6 || a == 5) { void (*cb)(int) = g_consent_cb; int yes = a == 6 && dm_crashreport_configured(); g_consent_cb = NULL; atomic_store(&g_show, 3); LOG("诊断上传：自动上报确认框收起（%s）", yes ? "同意" : "不开启"); cb(yes); }
        return; }
    if (!g_shell) return;
    if (a == 1 && S.st == DM_DIAGUP_ST_CONFIRM) start_attempt(now, 0);
    else if (a == 2 && S.st == DM_DIAGUP_ST_RESULT && retryable(S.outcome)) {
        if (now < S.t_retry) LOG("诊断上传：还要等 %.0f 秒才能重试（服务端 Retry-After），这次不发", S.t_retry - now);
        else start_attempt(now, 1);
    }
    else if (a == 3 && S.st != DM_DIAGUP_ST_RUNNING) { atomic_store(&g_show, 3); LOG("诊断上传：玩家选了导出到本地（原分享诊断日志）"); if (g_shell->export_local) g_shell->export_local(); }
    else if (a == 4 && S.st == DM_DIAGUP_ST_RESULT && S.outcome == DM_DIAGUP_OK) {
        int ok = g_shell->copy ? g_shell->copy(S.detail) : 0; g_copied = ok ? 1 : -1;
        LOG("%s诊断上传：复制报告编号%s", ok ? "" : "【红】", ok ? "" : "失败");
    }
    else if (a == 5) { atomic_store(&g_show, 3); if (S.st == DM_DIAGUP_ST_CONFIRM) S.st = DM_DIAGUP_ST_IDLE; LOG("诊断上传：提示框收起（%s）", S.st == DM_DIAGUP_ST_RUNNING ? "上传在后台继续，结果下次打开时显示" : "玩家关闭"); }
}

// 各状态的字（表键 / 参数）与按钮。参数以 @ 开头 = 表键（原因、按键说明），否则原样填进 %s | text (keys / args) and buttons per state; an arg starting with @ is a table key, otherwise literal
static const char *why_key(const char *d) {
    static const char *const map[][2] = { { "offline", "@dmDiagWhyOffline" }, { "data_restricted", "@dmDiagWhyRestricted" }, { "dns", "@dmDiagWhyDns" }, { "tls", "@dmDiagWhyTls" },
        { "connect", "@dmDiagWhyConnect" }, { "network", "@dmDiagWhyConnect" }, { "timeout", "@dmDiagWhyTimeout" }, { "response_lost", "@dmDiagWhyLost" }, { "watchdog", "@dmDiagWhyTimeout" } };
    for (size_t i = 0; i < sizeof map / sizeof *map; i++) if (!strcmp(d, map[i][0])) return map[i][1];
    return NULL;
}
void dm_diagup_consent_open(void (*cb)(int)) {
    if (atomic_load(&g_show) || !cb) { if (cb) cb(0); return; }
    g_consent_cb = cb; atomic_store(&g_action, 0); atomic_store(&g_show, 1);
    LOG("诊断上传：打开自动上报确认框（%s）", dm_crashreport_configured() ? "可上报" : "这个构建没配地址，只说明");
}
void dm_diagup_view(double now, int pad, dm_diagup_view_t *v) {
    memset(v, 0, sizeof *v);
    if (g_consent_cb) {
        if (!dm_crashreport_configured()) { v->title = "dmAutoCrashNoSrvTitle"; v->line0 = "dmAutoCrashNoSrv0"; v->line1 = "dmAutoCrashNoSrv1"; v->sub = pad ? "dmDiagPadClose" : NULL; v->close = 1; return; }
        v->title = "dmAutoCrashTitle"; v->line0 = "dmAutoCrash0"; v->line1 = "dmAutoCrash1"; v->sub = pad ? "dmAutoCrashPad" : "dmAutoCrashHint"; v->enable = v->cancel = 1; return;
    }
    v->close = 1;
    if (S.st == DM_DIAGUP_ST_CONFIRM || S.st == DM_DIAGUP_ST_IDLE) {
        v->title = "dmDiagTitle"; v->line0 = "dmDiagConsent0"; v->line1 = "dmDiagConsent1"; v->sub = pad ? "dmDiagPadConsent" : "dmDiagConsentHint";
        v->upload = v->exp = v->cancel = 1; v->close = 0; return;
    }
    if (S.st == DM_DIAGUP_ST_RUNNING) { v->title = "dmDiagSendingTitle"; v->line0 = "dmDiagSending0"; v->line1 = "dmDiagSending1"; v->sub = pad ? "dmDiagPadClose" : "dmDiagSendingHint"; return; }
    int o = S.outcome, wait = S.t_retry > now ? (int)(S.t_retry - now + 0.999) : 0;
    const char *why = why_key(S.detail);
    if (o == DM_DIAGUP_OK) {
        v->title = "dmDiagDoneTitle"; v->line0 = "dmDiagReportId"; snprintf(v->arg0, sizeof v->arg0, "%s", S.detail);
        if (S.flags & DM_DIAGUP_F_DUPLICATE) v->line1 = "dmDiagDoneDup";
        else { v->line1 = "dmDiagKeepDays"; snprintf(v->arg1, sizeof v->arg1, "%d", S.retention > 0 ? S.retention : 0); if (S.retention <= 0) v->line1 = "dmDiagDoneOnce"; }
        v->sub = g_copied > 0 ? "dmDiagCopied" : g_copied < 0 ? "dmDiagCopyFail" : pad ? "dmDiagPadDone" : "dmDiagDoneHint";
        v->copy = 1; return;
    }
    v->exp = 1;
    switch (o) {
    case DM_DIAGUP_UNCONFIRMED: v->title = "dmDiagUnconfTitle"; v->line0 = "dmDiagUnconf0"; break;
    case DM_DIAGUP_UNAVAILABLE: v->title = "dmDiagUnavailTitle"; v->line0 = "dmDiagNotSent"; break;
    case DM_DIAGUP_MAINTENANCE: v->title = "dmDiagMaintTitle"; v->line0 = S.flags & DM_DIAGUP_F_MAYBE ? "dmDiagMaybe" : "dmDiagMaint0"; break;
    case DM_DIAGUP_RATE_LIMITED: v->title = "dmDiagRateTitle"; v->line0 = S.flags & DM_DIAGUP_F_MAYBE ? "dmDiagMaybe" : "dmDiagRate0"; break;
    case DM_DIAGUP_TOO_LARGE: v->title = "dmDiagLargeTitle"; v->line0 = "dmDiagLarge0"; break;
    case DM_DIAGUP_PREPARE: case DM_DIAGUP_CONFIG: v->title = "dmDiagPrepTitle"; v->line0 = "dmDiagPrep0"; break;
    default: v->title = "dmDiagRejectTitle"; v->line0 = "dmDiagNotStored"; break;   // 其他 4xx、upload_conflict | other 4xx, upload_conflict
    }
    // 第二行：等待倒计时 > 网络原因 > 错误码 | second line: countdown > network reason > error code
    if (wait) { v->line1 = "dmDiagWait"; snprintf(v->arg1, sizeof v->arg1, "%d", wait); }
    else if (why) { v->line1 = "dmDiagWhy"; snprintf(v->arg1, sizeof v->arg1, "%s", why); }
    else if (o == DM_DIAGUP_MAINTENANCE || o == DM_DIAGUP_RATE_LIMITED) v->line1 = "dmDiagRetryNow";
    else if (S.detail[0]) { v->line1 = "dmDiagCode"; snprintf(v->arg1, sizeof v->arg1, "%s", S.detail); }
    v->retry = retryable(o) && !wait;
    if (pad) v->sub = v->retry ? "dmDiagPadRetry" : "dmDiagPadExport";
    else v->sub = o == DM_DIAGUP_UNCONFIRMED ? "dmDiagUnconfHint" : o == DM_DIAGUP_MAINTENANCE || o == DM_DIAGUP_RATE_LIMITED ? "dmDiagLaterHint" : retryable(o) ? "dmDiagRetryHint" : "dmDiagExportHint";
}

// ---- 画面（游戏主线程；控件操作同 dm_community.c co_*） | picture (game main thread; widget handling as dm_community.c co_*) ----
static uint8_t *du_base(void) { static uint8_t *b; if (!b) for (int i = 0; i < g_nmod; i++) if (!strcmp(g_mod[i].name, "main")) b = (uint8_t *)g_mod[i].base; return b; }
static uint64_t du_find(uint64_t w, const char *name, int dep) {   // 名字 +8、子控件数组 +0x58、个数 +0x60 | name +8, children +0x58, count +0x60
    if (!w || dep > 6) return 0; const char *nm = *(const char **)(w + 8); if (nm && !strcmp(nm, name)) return w;
    uint64_t ch = *(uint64_t *)(w + 0x58), n = *(uint64_t *)(w + 0x60); if (!ch || n > 256) return 0;
    for (uint64_t i = 0; i < n; i++) { uint64_t r = du_find(*(uint64_t *)(ch + 8 * i), name, dep + 1); if (r) return r; }
    return 0;
}
static void du_place(uint64_t w, int x, int y) {   // 只挪自己的控件（名字 TouchDiag 开头） | only our own widgets (names starting with TouchDiag)
    const char *nm = w ? *(const char **)(w + 8) : NULL; if (!nm || strncmp(nm, "TouchDiag", 9)) return;
    int32_t *r = (int32_t *)(w + 0x70); if (r[0] == x && r[1] == y) return; r[0] = x; r[1] = y; ((void (*)(uint64_t))*(uint64_t *)(*(uint64_t *)w + 0x10))(w);
}
static void du_text(uint8_t *b, uint64_t w, const char *s) {
    if (!w || *(uint64_t *)w - (uint64_t)(uintptr_t)b != DM_VT_TEXT) return;
    size_t n = strlen(s); if (*(uint64_t *)(w + 0x90) == n && !memcmp(*(const char **)(w + 0x88), s, n)) return;
    ((void *(*)(uint64_t, const char *, uint64_t))(b + 0x136e5c))(w + 0x88, s, n); ((void (*)(uint64_t))(b + 0xadc434))(w);
}
static void du_tile(uint8_t *b, uint64_t w, const int want[4]) {   // 只改自己的单帧切片 Ztouch_diag/tileRC（同 dm_community.c co_tile） | only our own slice Ztouch_diag/tileRC
    const char *name = w ? *(const char **)(w + 8) : NULL, *pre = "TouchDiagTile"; size_t n = strlen(pre);
    if (!name || strncmp(name, pre, n) || name[n] < '0' || name[n] > '4' || name[n + 1] < '0' || name[n + 1] > '2' || name[n + 2]) return;
    if (want[2] < 1 || want[2] > 4000 || want[3] < 1 || want[3] > 800 || !dm_widget_is(b, w, DM_VT_IMAGE)) return;
    char expect[40]; snprintf(expect, sizeof expect, "Ztouch_diag/tile%c%c", name[n], name[n + 1]);
    const char *file = *(const char **)(w + 0x108); uint64_t len = *(uint64_t *)(w + 0x110);
    if (!file || len != strlen(expect) || memcmp(file, expect, len) || *(int32_t *)(w + 0x90) != 0) return;
    uint64_t ref = *(uint64_t *)(w + 0x88), sp = ref ? *(uint64_t *)ref : 0; if (!sp) return;
    uint32_t *atlas = (uint32_t *)(sp + 0xbc), *height = (uint32_t *)(sp + 0xc0), *frames = (uint32_t *)(sp + 0xcc), *width = (uint32_t *)(sp + 0xd0);
    if (*frames != 1 || *atlas != *width || !*width || *width > 4000 || !*height || *height > 800) return;
    *atlas = *width = (uint32_t)want[2]; *height = (uint32_t)want[3];
    int32_t *r = (int32_t *)(w + 0x70); if (memcmp(r, want, 4 * sizeof *r)) { r[0] = want[0]; r[1] = want[1]; r[2] = want[2]; r[3] = want[3]; }
}
static const char *const k_hosts[] = { "PauseLayout", "PauseLayoutGarden", "VideoOptionsPanel" };   // 自定义页（借来的视频页）只挂确认页 | the Custom page (the borrowed video page) only hosts the consent page
#define N_BTN 7
static const char *const k_btn[N_BTN] = { "TouchDiagUpload", "TouchDiagRetry", "TouchDiagExport", "TouchDiagCopy", "TouchDiagCancel", "TouchDiagClose", "TouchDiagEnable" };
static uint64_t du_host(uint64_t root, int *which) {   // 确认页在自定义页，上传框在暂停菜单 | the consent page lives on the Custom page, the upload box on the pause menu
    int from = g_consent_cb ? 2 : 0, to = g_consent_cb ? 3 : 2;
    for (int i = from; root && i < to; i++) { uint64_t q = dm_ui_top(root, k_hosts[i]); if (q && *(uint8_t *)(q + 0x50) && du_find(q, "TouchDiagTitle", 0)) { if (which) *which = i; return q; } }
    return 0;
}
// 触屏抬起（dm_cursor.c 鼠标泵交来，弹窗开着时）：用控件自己的命中判断，只认露出来的按钮，其余吞掉（同 dm_community_ui_tap） | touch lift while the box is up: the widgets' own hit test, only shown buttons, the rest swallowed
void dm_diagup_ui_tap(uint64_t root, int x, int y) {
    atomic_fetch_add(&g_sw_touch, 1);
    uint64_t p = du_host(root, NULL); if (!p || atomic_load(&g_show) != 2) return;
    uint64_t xy = (uint64_t)(uint32_t)x | (uint64_t)(uint32_t)y << 32;
    static const int acts[N_BTN] = { 1, 2, 3, 4, 5, 5, 6 };
    for (int k = 0; k < N_BTN; k++) { uint64_t w = du_find(p, k_btn[k], 0); if (!w || ((int32_t *)(w + 0x70))[0] == 99999) continue;
        if (((int (*)(uint64_t, uint64_t))*(uint64_t *)(*(uint64_t *)w + 0x48))(w, xy) & 1) { LOG("诊断上传：弹窗开着时点按 (%d,%d) → %s", x, y, k_btn[k]); atomic_store(&g_action, acts[k]); return; } }
    LOG("诊断上传：弹窗开着时点按 (%d,%d) → 没点在按钮上，吞掉", x, y);
}
static void du_hide_all(uint8_t *b, uint64_t p) {
    for (int k = 0; k < 15; k++) { char nm[24]; snprintf(nm, sizeof nm, "TouchDiagTile%d%d", k / 3, k % 3); uint64_t t = du_find(p, nm, 0); if (t) { int32_t *r = (int32_t *)(t + 0x70); r[0] = r[1] = 99999; } }
    static const char *const tx[] = { "TouchDiagTitle", "TouchDiagDesc0", "TouchDiagDesc1", "TouchDiagDesc2" };
    for (int k = 0; k < 4; k++) { uint64_t w = du_find(p, tx[k], 0); if (w) { du_place(w, 99999, 99999); du_text(b, w, ""); } }
    for (int k = 0; k < N_BTN; k++) du_place(du_find(p, k_btn[k], 0), 99999, 99999);
}
static void du_line(uint8_t *b, char *out, size_t n, const char *key, const char *arg) {   // 表串 + 一个参数（参数 @ 开头 = 再查一次表） | table string + one arg (an arg starting with @ is looked up too)
    if (!key) { out[0] = 0; return; }
    const char *fmt = dm_str(b, key), *a = arg && arg[0] == '@' ? dm_str(b, arg + 1) : arg;
    if (fmt && strstr(fmt, "%s")) snprintf(out, n, fmt, a ? a : ""); else snprintf(out, n, "%s", fmt ? fmt : "");
}
void dm_diagup_pump(void) {   // 取手柄处每帧调（游戏主线程，hle_hid.c） | every frame at the controller poll (game main thread, hle_hid.c)
    double now = now_s(); int st = atomic_load(&g_show);
    int dm_padmode_current(void); uint64_t dm_padmode_page_keys(uint64_t); int pad = dm_padmode_current();
    uint8_t *b = du_base(); uint64_t root = b ? *(uint64_t *)(b + 0x2f05458) : 0, p = 0; int which = 0;
    p = du_host(root, &which);
    { uint64_t ev = atomic_exchange(&g_md_ev, 0); if (pad && st == 2 && p) {   // 手柄：显示着时 A / X / B 归框（闸门 dm_diagup_input_gate 已截下，游戏收不到） | controller: while shown, A / X / B belong to the box (held back by the gate)
        dm_diagup_view_t v; dm_diagup_view(now, 1, &v);
        if (ev & DM_BTN_A) atomic_store(&g_action, v.enable ? 6 : v.upload ? 1 : v.retry ? 2 : v.copy ? 4 : 0);
        else if ((ev & DM_BTN_X) && v.exp) atomic_store(&g_action, 3);
        else if (ev & DM_BTN_B) atomic_store(&g_action, 5);
    } }
    dm_diagup_tick(now);
    st = atomic_load(&g_show);
    static int wait;
    if (st == 3) { if (p) du_hide_all(b, p); atomic_store(&g_show, 0); return; }   // 收起：挪回屏外 | dismissed: back off screen
    if (!st) return;
    if (!p) { if (st == 1 && ++wait == 600) { wait = 0; atomic_store(&g_show, 0); LOG("【红】诊断上传：开着的面板里没有提示框控件（原版布局？），只记日志不显示"); }
              else if (st == 2 && g_consent_cb) { void (*cb)(int) = g_consent_cb; g_consent_cb = NULL; atomic_store(&g_show, 0); LOG("诊断上传：自定义页已关，确认框按不开启收起"); cb(0); }
              else if (st == 2) { atomic_store(&g_show, 0); if (S.st == DM_DIAGUP_ST_CONFIRM) S.st = DM_DIAGUP_ST_IDLE; LOG("诊断上传：暂停菜单已关，提示框收起%s", S.st == DM_DIAGUP_ST_RUNNING ? "（上传在后台继续）" : ""); }
              return; }
    wait = 0;
    uint64_t ti = du_find(p, "TouchDiagTitle", 0), de[3], bt[N_BTN], tl[15]; int all = ti != 0;
    for (int k = 0; k < 3; k++) { char nm[24]; snprintf(nm, sizeof nm, "TouchDiagDesc%d", k); de[k] = du_find(p, nm, 0); all &= de[k] != 0; }
    for (int k = 0; k < N_BTN; k++) { bt[k] = du_find(p, k_btn[k], 0); all &= bt[k] != 0; }
    for (int k = 0; k < 15; k++) { char nm[24]; snprintf(nm, sizeof nm, "TouchDiagTile%d%d", k / 3, k % 3); tl[k] = du_find(p, nm, 0); all &= tl[k] != 0; }
    if (!all) { atomic_store(&g_show, 0); LOG("【红】诊断上传：%s 里的提示框控件不全，不显示", k_hosts[which]); return; }
    dm_diagup_view_t v; dm_diagup_view(now, pad, &v);
    char l0[256], l1[256];
    du_line(b, l0, sizeof l0, v.line0, v.arg0); du_line(b, l1, sizeof l1, v.line1, v.arg1);
    du_text(b, ti, dm_str(b, v.title)); du_text(b, de[0], l0); du_text(b, de[1], l1); du_text(b, de[2], v.sub ? dm_str(b, v.sub) : "");
    // 位置：与社区入口同口径（面板原点 + 缩放，整屏居中）；框 1800×560：标题 / 正文两行 / 副文 / 按钮行 | placement as the community box (panel origin + scale, screen centered); box 1800×560
    int vw = *(int32_t *)(b + 0x211da38), vh = *(int32_t *)(b + 0x211da3c), xr = vh > 0 ? (int)((long)vw * 2160 / vh) : 3840;
    float ax = *(float *)(p + 0x48), ay = *(float *)(p + 0x4c), s = *(float *)(p + 0x80); int32_t *pr = (int32_t *)(p + 0x70);
    if (!(ax >= 0 && ax <= 1)) ax = 0; if (!(ay >= 0 && ay <= 1)) ay = 0; if (!(s > 0.05f && s < 10)) s = 1;
    int ox = (int)(ax * xr) + pr[0], oy = (int)(ay * 2160) + pr[1], H = 560;
    int cx = (int)((xr / 2 - ox) / s), y0 = (int)((1080 - oy) / s) - H / 2;
    { int gm[15][4], h = dm_notice_geometry(1800 - 128, H - 24, 1800, gm), top = (244 - h) / 2;
      for (int k = 0; h && k < 15; k++) { int r[4] = { cx - 900 + gm[k][0], y0 + gm[k][1] - top, gm[k][2], gm[k][3] + (k / 3 < 4 ? 2 : 0) }; du_tile(b, tl[k], r); } }
    du_place(ti, cx - 850, y0 + 40);
    static const int dy[3] = { 140, 206, 284 };   // 36 / 36 / 28 号（§2.6） | sizes 36 / 36 / 28
    for (int k = 0; k < 3; k++) du_place(de[k], cx - 850, y0 + dy[k]);
    // 按钮行：露出来的按钮（至多三颗、各 470 宽、间隔 30）居中排开，顺序 主操作 / 导出 / 取消或关闭 | button row: shown buttons (at most three, 470 wide, 30 apart) centered
    int show[N_BTN] = { v.upload, v.retry, v.exp, v.copy, v.cancel, v.close, v.enable }, order[N_BTN] = { 6, 0, 1, 3, 2, 4, 5 }, n = 0;
    for (int k = 0; k < N_BTN; k++) n += show[k] != 0;
    int x = cx - (n * 470 + (n - 1) * 30) / 2;
    for (int i = 0; i < N_BTN; i++) { int k = order[i]; if (show[k]) { du_place(bt[k], x, y0 + 400); x += 500; } else du_place(bt[k], 99999, 99999); }
    if (st == 1) { atomic_store(&g_show, 2); S.unseen = 0;
        LOG("诊断上传：已显示（%s，%s；面板 %s 矩形 %d,%d 缩放 %.2f → 框中心 %d 顶 %d）", v.title, pad ? "手柄模式，A 主操作 X 导出 B 关闭" : "触屏 / 鼠标点按钮", k_hosts[which], pr[0], pr[1], s, cx, y0); }
}
