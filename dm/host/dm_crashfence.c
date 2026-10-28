// 崩溃围栏，接口见 dm_crashfence.h。
// 什么算「正常结束」：宿主优雅退出（dm_host_shutdown：iOS 场景断开 / App 终止、Mac 关窗、SIGTERM）和 iOS 进后台。进后台也清，是因为 iOS 在后台杀进程（玩家在多任务界面划掉、系统收内存）
// 是直接 SIGKILL、不走任何回调，若不清会把这类正常结束当成闪退；回前台再写回。前台里被看门狗杀、闪退、断电都留下标记。
// 连续次数：标记里记本局开始时已连续异常退出的次数 k；本局再异常退出，下次启动读到 k 就是第 k + 1 次。正常结束删标记 = 清零。
// 测试局：带测试环境变量启动的局（DM_TEST_*、DM_WATCH*、DM_SAVE_TEST / DM_SAVE_FRESH、DM_KEYS、DM_TOUCH_SCRIPT、DM_BENCH_DIR；devicectl 用 DEVICECTL_CHILD_ 前缀传的到进程里也是这些名字）
// 在标记里记 test=1。测试脚本常用 terminate --kill（SIGKILL）收尾，清不掉标记；下次启动看到测试局的残留只记一行「测试局，不计」，连续次数不加，不会因此关掉玩家的实验项
// Test runs: a run started with test environment variables (DM_TEST_*, DM_WATCH*, DM_SAVE_TEST / DM_SAVE_FRESH, DM_KEYS, DM_TOUCH_SCRIPT, DM_BENCH_DIR; those passed by devicectl with the DEVICECTL_CHILD_ prefix arrive under these names)
// records test=1 in the marker. Test scripts often end with terminate --kill (SIGKILL), which cannot clear the marker; a leftover from a test run only logs "test run, not counted" at the next start, the streak does not grow, and the player's experimental features are never turned off because of it
// 测试开关 DM_TEST_CRASH_MARK=<n>（n ≥ 1）：当作有残留标记、本局是第 n 次连续异常退出之后的启动（不读盘上的标记；照常写标记、照常关实验项写回设置）。只读一次环境变量，不常驻分配
// Crash fence; the interface is in dm_crashfence.h.
// What counts as a "normal end": the host's graceful exit (dm_host_shutdown: iOS scene disconnect / app termination, Mac window close, SIGTERM) and iOS going to the background. Going to the background clears too, because iOS kills background processes
// (the player swiping the app away in the switcher, the system reclaiming memory) with a plain SIGKILL and no callback at all; without clearing, such normal ends would count as crashes. Coming back to the foreground writes the marker again. A watchdog kill, a crash or a power loss in the foreground leaves the marker behind.
// Streak: the marker records k, the consecutive abnormal exits seen when this run started; if this run also exits abnormally, the next start reads k and this is number k + 1. A normal end deletes the marker = back to zero.
// Test switch DM_TEST_CRASH_MARK=<n> (n >= 1): act as if a marker were left over and this start follows the n-th consecutive abnormal exit (the marker on disk is not read; the marker is written and experimental features are turned off and saved as usual). The environment variable is read once, nothing stays allocated
#include "dm_crashfence.h"
#include "dm_custom.h"
#include "dm_i18n.h"
#include "dm_input.h"
#include "dm_widget.h"
#include "dm_notice_geometry.h"
#include "hle.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

extern char **environ;
// 异常退出的原因（用户：「哪里闪退了，应该分场景」）：标记里记本局的原因 cause，和这一串连续异常退出里前几次的原因 hist（最近的在末尾，最多 8 个）。
//   闪退 = 致命信号（crash.c 的信号处理在退出前调 g_crash_note_hook，异步信号安全地把 cause=1:<信号> 写进标记）；
//   卡死 = iOS 进后台那一刻已经 5 秒没出图（画面卡住了，玩家切出去再划掉）：这时不清标记、记 cause=2；回前台又出图就改回 0；
//   内存不足 = 收到过系统内存告警（applicationDidReceiveMemoryWarning）之后在前台没了：记 cause=3（只在还没有别的原因时）；
//   其余（断电、系统不打招呼直接结束等）= 0 原因不明。主菜单提示按最近两次的原因选文案：两次相同就说那个原因，不同就说「异常退出」
// Cause of the abnormal exit (user: "where did it crash, it should be split by scenario"): the marker records this run's cause and, as hist, the causes of the earlier exits in the current streak (most recent last, at most 8).
//   crash = fatal signal (crash.c's signal handler calls g_crash_note_hook before exiting, which writes cause=1:<signal> into the marker async-signal-safely);
//   frozen = at the moment iOS goes to the background there has been no present for 5 s (the picture was stuck; the player switched away and swiped the app off): the marker is kept with cause=2; back in the foreground with presents it returns to 0;
//   out of memory = gone in the foreground after a system memory warning (applicationDidReceiveMemoryWarning): cause=3 (only if there is no other cause yet);
//   anything else (power loss, the system ending the app without notice, ...) = 0 unknown. The main-menu notice picks its text from the last two causes: the same twice names that cause, different ones say "quit unexpectedly"
enum { CF_OTHER, CF_CRASH, CF_HANG, CF_MEM };
static const char *const k_cf_name[4] = { "原因不明", "闪退", "卡死", "内存不足" };
static const char *const k_cf_msg[4] = { "dmSafeModeMsgOther", "dmSafeModeMsgCrash", "dmSafeModeMsgHang", "dmSafeModeMsgMem" };
static int g_started, g_streak, g_marked, g_test, g_cause, g_cause_arg, g_msg = CF_OTHER;
static char g_hist[9];   // 这一串里前几次的原因（'0'–'3'） | causes of the earlier exits in this streak ('0'-'3')
static _Atomic int g_notice;
static char g_sig_path[1100], g_sig_body[64]; static int g_sig_len;   // 信号处理里用：标记路径与不含 cause 的正文，写标记时备好 | for the signal handler: marker path and the body without cause, prepared when the marker is written
extern void (*g_crash_note_hook)(int sig);
uint64_t crash_last_present_ns(void), crash_now_ns(void);

static int marker_path(char *p, size_t n) {
    char d[1024]; if (!dm_custom_dir(d, sizeof d)) return 0;
    return snprintf(p, n, "%s/dm_running.txt", d) < (int)n;
}
// 本局是不是测试局：返回认出的那个变量名（只进日志），不是返回 NULL | Is this a test run: returns the variable name that identified it (log only), NULL if not
static const char *test_env(void) {
    static const char *const exact[] = { "DM_SAVE_TEST", "DM_SAVE_FRESH", "DM_KEYS", "DM_TOUCH_SCRIPT", "DM_BENCH_DIR" };
    static char name[48];
    for (char **e = environ; e && *e; e++) {
        const char *v = *e, *eq = strchr(v, '='); size_t n = eq ? (size_t)(eq - v) : strlen(v);
        int hit = (n > 8 && !strncmp(v, "DM_TEST_", 8)) || (n >= 8 && !strncmp(v, "DM_WATCH", 8));
        for (size_t i = 0; !hit && i < sizeof exact / sizeof *exact; i++) hit = n == strlen(exact[i]) && !strncmp(v, exact[i], n);
        if (hit) { snprintf(name, sizeof name, "%.*s", (int)(n < sizeof name ? n : sizeof name - 1), v); return name; }
    }
    return NULL;
}
const char *dm_crashfence_test_env(void) { return test_env(); }
static int marker_write(void) {   // 先写临时文件再换名：写到一半被杀不会留下半个标记 | write a temporary file first, then rename: being killed halfway leaves no half marker
    char p[1100], t[1120]; if (!marker_path(p, sizeof p)) return 0;
    g_sig_len = snprintf(g_sig_body, sizeof g_sig_body, "streak=%d\n%shist=%s\n", g_streak, g_test ? "test=1\n" : "", g_hist); snprintf(g_sig_path, sizeof g_sig_path, "%s", p);
    snprintf(t, sizeof t, "%s.tmp", p); FILE *f = fopen(t, "w"); if (!f) return 0;
    int ok = fprintf(f, "%scause=%d:%d\n", g_sig_body, g_cause, g_cause_arg) > 0; ok = fclose(f) == 0 && ok;
    return ok && rename(t, p) == 0;
}
// 致命信号退出之前（crash.c 调，信号处理上下文）：只用 open / write / close，标记改成 cause=1:<信号> | before exiting on a fatal signal (called by crash.c in signal context): only open / write / close; the marker becomes cause=1:<signal>
static void cf_on_signal(int sig) {
    if (!g_marked || !g_sig_path[0]) return;
    int fd = open(g_sig_path, O_WRONLY | O_TRUNC); if (fd < 0) return;
    char tail[24] = "cause=1:"; int n = 8; if (sig >= 100) tail[n++] = (char)('0' + sig / 100 % 10); if (sig >= 10) tail[n++] = (char)('0' + sig / 10 % 10); tail[n++] = (char)('0' + sig % 10); tail[n++] = '\n';
    if (write(fd, g_sig_body, (size_t)g_sig_len) < 0 || write(fd, tail, (size_t)n) < 0) {}
    close(fd);
}
// 读残留：-1 = 没有标记；否则标记里的 k（坏内容按 0 算：标记在，至少是一次异常退出）；*test = 残留来自测试局；*cause / *arg = 那一局的原因；hist = 那一串之前的原因
// read the leftover: -1 = no marker; otherwise k from the marker (bad contents count as 0: the marker exists, so at least one abnormal exit); *test = from a test run; *cause / *arg = that run's cause; hist = the earlier causes of that streak
static int marker_read(int *test, int *cause, int *arg, char *hist, size_t hn) {
    char p[1100]; *test = 0; *cause = CF_OTHER; *arg = 0; hist[0] = 0; if (!marker_path(p, sizeof p)) return -1;
    FILE *f = fopen(p, "r"); if (!f) return -1;
    char ln[64]; int k = 0;
    while (fgets(ln, sizeof ln, f)) {
        if (!strncmp(ln, "streak=", 7)) { k = atoi(ln + 7); if (k < 0 || k > 1000000) k = 0; }
        else if (!strncmp(ln, "test=1", 6)) *test = 1;
        else if (!strncmp(ln, "cause=", 6)) { int c = ln[6] - '0'; if (c >= CF_OTHER && c <= CF_MEM) *cause = c; const char *q = strchr(ln, ':'); if (q) *arg = atoi(q + 1); }
        else if (!strncmp(ln, "hist=", 5)) { size_t o = 0; for (const char *q = ln + 5; *q >= '0' && *q <= '3' && o + 1 < hn; q++) hist[o++] = *q; hist[o] = 0; }
    }
    fclose(f); return k;
}
static void hist_push(int c) { size_t n = strlen(g_hist); if (n + 1 >= sizeof g_hist) { memmove(g_hist, g_hist + 1, n - 1); n--; } g_hist[n] = (char)('0' + c); g_hist[n + 1] = 0; }
static void cause_text(char *o, size_t n, int c, int arg) {
    if (c == CF_CRASH) snprintf(o, n, "闪退：信号 %d", arg);
    else if (c == CF_HANG) snprintf(o, n, "卡死：进后台时画面已 %d 秒没更新", arg);
    else if (c == CF_MEM) snprintf(o, n, "内存不足：之前收到过系统内存告警");
    else snprintf(o, n, "原因不明");
}

int dm_crashfence_start(void) {
    if (g_started) return g_streak;
    g_started = 1;
    char p[1100]; if (!marker_path(p, sizeof p)) { LOG("【红】崩溃围栏：没有设置目录，不写运行标记"); return 0; }
    const char *tm = getenv("DM_TEST_CRASH_MARK"); int test = tm && atoi(tm) >= 1, was_test = 0, c = CF_OTHER, arg = 0; char why[96];
    int k = test ? atoi(tm) - 1 : marker_read(&was_test, &c, &arg, g_hist, sizeof g_hist);
    if (test) {   // 测试开关：DM_TEST_CRASH_CAUSE=crash / hang / mem / other 当作这几次都是这个原因（缺省原因不明） | test switch: DM_TEST_CRASH_CAUSE=crash / hang / mem / other treats all of them as this cause (default unknown)
        const char *tc = getenv("DM_TEST_CRASH_CAUSE"); c = !tc ? CF_OTHER : !strcmp(tc, "crash") ? CF_CRASH : !strcmp(tc, "hang") ? CF_HANG : !strcmp(tc, "mem") ? CF_MEM : CF_OTHER; arg = c == CF_CRASH ? 11 : c == CF_HANG ? 5 : 0;
        g_hist[0] = 0; for (int i = 0; i < k && i < 7; i++) hist_push(c); }
    g_streak = k < 0 ? 0 : was_test ? k : k + 1;   // 测试局的残留不加：仍是那一局开始时的连续次数 | a test run's leftover adds nothing: still the streak at that run's start
    if (k >= 0 && !was_test) hist_push(c);
    if (k < 0) g_hist[0] = 0;
    cause_text(why, sizeof why, c, arg);
    if (k >= 0 && was_test) LOG("崩溃围栏：上一局异常退出（测试局，不计；连续 %d 次不变；%s）", g_streak, why);
    else if (g_streak) LOG("崩溃围栏：上一局异常退出（%s）（第 %d 次连续）%s", why, g_streak, test ? "（测试开关 DM_TEST_CRASH_MARK）" : "");
    else LOG("崩溃围栏：上一局正常结束");
    const char *te = test_env(); g_test = te != NULL;
    if (te) LOG("崩溃围栏：本局是测试局（%s），被强杀不计入连续异常退出", te);
    g_cause = CF_OTHER; g_cause_arg = 0; g_marked = marker_write();
    if (!g_marked) LOG("【红】崩溃围栏：运行标记写不进 %s", p);
    g_crash_note_hook = cf_on_signal;
    if (g_streak >= 2) {
        size_t n = strlen(g_hist); int a = n >= 1 ? g_hist[n - 1] - '0' : CF_OTHER, b = n >= 2 ? g_hist[n - 2] - '0' : CF_OTHER;
        g_msg = a == b ? a : CF_OTHER;   // 最近两次原因相同就说那个原因，不同就说「异常退出」 | the last two causes equal: name it; different: "quit unexpectedly"
        char what[256]; int ch = dm_custom_safe_mode(what, sizeof what);
        atomic_store(&g_notice, 1);
        LOG("崩溃围栏：连续 %d 次异常退出（原因依次 %s），关闭全部实验性功能（%s；改了 %d 项%s），主菜单提示一次，文案按「%s」", g_streak, g_hist, what, ch, ch ? "，已写回设置文件" : "", k_cf_name[g_msg]);
    }
    return g_streak;
}
void dm_crashfence_clean(const char *why) {
    if (!g_marked) return;
    char p[1100]; if (!marker_path(p, sizeof p)) return;
    int ok = unlink(p) == 0; g_marked = !ok; if (ok) { g_streak = 0; g_hist[0] = 0; g_cause = CF_OTHER; }
    LOG("崩溃围栏：%s，清除运行标记%s", why ? why : "正常结束", ok ? "" : "【红】失败");
}
// iOS 进后台：画面还在动 = 正常切出去，清标记（之后在后台被划掉 / 被系统收走都不算）；进后台那一刻已经 5 秒没出图 = 画面卡住了，标记留着、记卡死
// iOS going to the background: the picture still moving = a normal switch-away, clear the marker (being swiped off / reclaimed in the background later does not count); no present for 5 s at that moment = the picture was stuck, keep the marker and record frozen
void dm_crashfence_background(void) {
    uint64_t last = crash_last_present_ns(), now = crash_now_ns();
    int stale = last && now > last ? (int)((now - last) / 1000000000ull) : 0;
    if (!g_marked || stale < 5) { dm_crashfence_clean("进后台"); return; }
    g_cause = CF_HANG; g_cause_arg = stale; marker_write();
    LOG("崩溃围栏：进后台时画面已 %d 秒没更新，当卡死记下，运行标记不清（之后被划掉 / 系统结束就算一次卡死）", stale);
}
void dm_crashfence_resume(void) {
    if (!g_started) return;
    if (g_marked) { if (g_cause == CF_HANG) { g_cause = CF_OTHER; g_cause_arg = 0; marker_write(); LOG("崩溃围栏：回前台，撤销卡死记录"); } return; }
    g_marked = marker_write();
    LOG("崩溃围栏：回前台，重写运行标记%s", g_marked ? "" : "【红】失败");
}
// 系统内存告警（iOS applicationDidReceiveMemoryWarning）：之后在前台没了多半是内存不足被系统结束；已有别的原因就不改 | system memory warning: vanishing in the foreground afterwards is most likely being ended for memory; another cause already recorded is kept
void dm_crashfence_note_memory(void) {
    if (!g_marked || g_cause != CF_OTHER) return;
    g_cause = CF_MEM; marker_write(); LOG("崩溃围栏：收到系统内存告警，标记里记下（之后在前台没了就算内存不足）");
}
int dm_crashfence_notice_take(void) { return atomic_exchange(&g_notice, 0); }
// 主菜单上的游戏内提示框（用户：「不要用原生的弹窗，用游戏内弹窗」）：控件是布局覆盖层在主菜单面板下加的边框 15 片 TouchSafeTile（我们通知框同一套美术、自己一套切片 Ztouch_safe）/ Title / Desc0–2 / Ok
// （dm/ios/tools/touch_layout.py _SAFE / _SAFE_PAD），平时摆在屏幕外、字空着。要提示时每帧在游戏主线程写字、摆到屏幕中间；
// 「确定」按钮发 PanelManager:ClosePanel:TouchSafeOk（空操作，宿主当信号收），手柄模式接管 A 键当确定。点了就收回屏幕外，本次启动不再出。
// 布局里没有这些控件（跑原版布局）：等 600 帧还找不到就只记一行日志作罢。「前端：提示已显示」「玩家点了确定」两行保留（分享诊断日志要看）
// In-game notice box on the main menu (user: "do not use a native popup, use an in-game one"): the widgets are the 15 frame tiles TouchSafeTile (same art as our notification box, its own slices Ztouch_safe) / Title / Desc0-2 / Ok added under the main menu panel by the layout overlay
// (_SAFE / _SAFE_PAD in dm/ios/tools/touch_layout.py), normally off screen with empty text. While notifying, every frame on the game main thread writes the text and moves them to the screen center;
// "OK" sends PanelManager:ClosePanel:TouchSafeOk (a no-op the host takes as a signal), and controller mode takes over A as OK. Once confirmed they go back off screen and do not return during this launch.
// Without these widgets (original layout): after 600 frames without finding them only one log line is written. The "shown" / "player tapped OK" lines are kept (share diagnostics read them)
static _Atomic int g_show, g_ack;   // g_show：0 不提示 / 1 要提示 / 2 显示着 | g_show: 0 none / 1 pending / 2 shown
// 同一个提示框还给内存 0 档的一次性提示用（dm_custom.c 内存档位，用户 2026-10-29）：崩溃围栏的优先，内存提示留到下次启动 | the same box also carries the memory-tier-0 one-time notice (dm_custom.c memory tier, user 2026-10-29): the crash fence wins, the memory notice waits for the next launch
enum { NOTICE_CRASH, NOTICE_LOWMEM };
static int g_kind;
static void notice_dismissed(void) { if (g_kind == NOTICE_LOWMEM) dm_custom_mem_notice_done(); }
void dm_crashfence_main_menu(void) {
    if (atomic_load(&g_show)) return;
    if (dm_crashfence_notice_take()) { g_kind = NOTICE_CRASH; atomic_store(&g_show, 1); LOG("崩溃围栏：主菜单提示「已关闭实验性功能」（游戏内提示框）"); return; }
    static int lowmem_once; if (lowmem_once || !dm_custom_mem_notice_due()) return;
    lowmem_once = 1; g_kind = NOTICE_LOWMEM; atomic_store(&g_show, 1); LOG("内存档位：主菜单提示「本机内存偏小，已自动降低画质」（游戏内提示框，只提示一次）");
}
void dm_crashfence_on_uimsg(const char *cat, const char *act, const char *arg) {   // 界面消息回调（diag.c 转来，游戏主线程） | UI message callback (forwarded from diag.c, game main thread)
    if (cat && act && arg && !strcmp(cat, "PanelManager") && !strcmp(act, "ClosePanel") && !strcmp(arg, "TouchSafeOk") && atomic_load(&g_show) == 2) atomic_store(&g_ack, 1);
}
static uint8_t *cf_base(void) { static uint8_t *b; if (!b) for (int i = 0; i < g_nmod; i++) if (!strcmp(g_mod[i].name, "main")) b = (uint8_t *)g_mod[i].base; return b; }
static uint64_t cf_find(uint64_t w, const char *name, int dep) {   // 按名字找控件：名字 +8、子控件数组 +0x58、个数 +0x60（同 dm_update.c） | find a widget by name (same as dm_update.c)
    if (!w || dep > 6) return 0; const char *nm = *(const char **)(w + 8); if (nm && !strcmp(nm, name)) return w;
    uint64_t ch = *(uint64_t *)(w + 0x58), n = *(uint64_t *)(w + 0x60); if (!ch || n > 256) return 0;
    for (uint64_t i = 0; i < n; i++) { uint64_t r = cf_find(*(uint64_t *)(ch + 8 * i), name, dep + 1); if (r) return r; }
    return 0;
}
static void cf_place(uint64_t w, int x, int y) {   // 矩形 +0x70 / +0x74，改完调虚表 +0x10 重算（同 dm_update.c place） | rect at +0x70 / +0x74, then vtable +0x10 to recompute (same as dm_update.c place)
    int32_t *r = (int32_t *)(w + 0x70); if (r[0] == x && r[1] == y) return; r[0] = x; r[1] = y; ((void (*)(uint64_t))*(uint64_t *)(*(uint64_t *)w + 0x10))(w);
}
// 边框片的位置与大小（同 dm_cursor.c ui_notice_tile_rect 的做法与检查）：只改我们自己的单帧切片 Ztouch_safe/tileRC 的绘制尺寸，名字、文件名、帧数都对上才写
// Position and size of a frame tile (same method and checks as ui_notice_tile_rect in dm_cursor.c): only the draw size of our own single-frame slice Ztouch_safe/tileRC is changed, written only when name, file name and frame count all match
static void cf_tile(uint8_t *b, uint64_t w, const int want[4]) {
    const char *name = w ? *(const char **)(w + 8) : NULL, *pre = "TouchSafeTile"; size_t n = strlen(pre);
    if (!name || strncmp(name, pre, n) || name[n] < '0' || name[n] > '4' || name[n + 1] < '0' || name[n + 1] > '2' || name[n + 2]) return;
    if (want[2] < 1 || want[2] > 4000 || want[3] < 1 || want[3] > 800 || !dm_widget_is(b, w, DM_VT_IMAGE)) return;
    char expect[40]; snprintf(expect, sizeof expect, "Ztouch_safe/tile%c%c", name[n], name[n + 1]);
    const char *file = *(const char **)(w + 0x108); uint64_t len = *(uint64_t *)(w + 0x110);
    if (!file || len != strlen(expect) || memcmp(file, expect, len) || *(int32_t *)(w + 0x90) != 0) return;
    uint64_t ref = *(uint64_t *)(w + 0x88), sp = ref ? *(uint64_t *)ref : 0; if (!sp) return;
    uint32_t *atlas = (uint32_t *)(sp + 0xbc), *height = (uint32_t *)(sp + 0xc0), *frames = (uint32_t *)(sp + 0xcc), *width = (uint32_t *)(sp + 0xd0);
    if (*frames != 1 || *atlas != *width || !*width || *width > 4000 || !*height || *height > 800) return;
    *atlas = *width = (uint32_t)want[2]; *height = (uint32_t)want[3];
    int32_t *r = (int32_t *)(w + 0x70); if (memcmp(r, want, 4 * sizeof *r)) { r[0] = want[0]; r[1] = want[1]; r[2] = want[2]; r[3] = want[3]; }
}
static void cf_text(uint8_t *b, uint64_t w, const char *s) {   // 文字框设文本（同 dm_update.c set_text；不是文字框不动） | set a text box's text (same as dm_update.c set_text; not a text box = left alone)
    if (*(uint64_t *)w - (uint64_t)(uintptr_t)b != 0x20ba4c8) return;
    size_t n = strlen(s); if (*(uint64_t *)(w + 0x90) == n && !memcmp(*(const char **)(w + 0x88), s, n)) return;
    ((void *(*)(uint64_t, const char *, uint64_t))(b + 0x136e5c))(w + 0x88, s, n); ((void (*)(uint64_t))(b + 0xadc434))(w);
}
void dm_crashfence_pump(void) {   // 取手柄处每帧调（游戏主线程，hle_hid.c） | called every frame at the controller poll (game main thread, hle_hid.c)
    int st = atomic_load(&g_show); if (!st) return;
    // 控件挂在哪：触屏版在主菜单面板下；手柄版在左侧按钮栏面板 MainMenuButtonRibbon 下（它画在主菜单之上，提示框才能盖住按钮栏、整屏居中：touch_layout._safe_into）
    // Where the widgets hang: touch under the main menu panel; controller under the left button-column panel MainMenuButtonRibbon (it draws above the main menu, so the box can cover the column and center on the screen: touch_layout._safe_into)
    int dm_padmode_current(void); uint64_t dm_padmode_page_keys(uint64_t); int pad = dm_padmode_current();
    static int wait; uint8_t *b = cf_base(); uint64_t root = b ? *(uint64_t *)(b + 0x2f05458) : 0, mm = root ? cf_find(root, pad ? "MainMenuButtonRibbon" : "MainMenuPanel", 0) : 0;
    uint64_t ti = mm ? cf_find(mm, "TouchSafeTitle", 0) : 0, ok = mm ? cf_find(mm, "TouchSafeOk", 0) : 0, de[3], tl[15]; int tiles = 1;
    for (int k = 0; k < 15; k++) { char nm[24]; snprintf(nm, sizeof nm, "TouchSafeTile%d%d", k / 3, k % 3); tl[k] = mm ? cf_find(mm, nm, 0) : 0; tiles &= tl[k] != 0; }
    for (int k = 0; k < 3; k++) { char nm[24]; snprintf(nm, sizeof nm, "TouchSafeDesc%d", k); de[k] = mm ? cf_find(mm, nm, 0) : 0; }   // 正文三行各一个文字框（游戏文字框不认换行） | one text box per body line (the game's text box ignores newlines)
    if (!tiles || !ti || !de[0] || !de[1] || !de[2] || !ok) { if (st == 1 && ++wait == 600) { atomic_store(&g_show, 0); LOG("【红】崩溃围栏：主菜单面板里没有提示框控件（原版布局？），只记日志不显示"); } return; }
    int vis = *(uint8_t *)(mm + 0x50);
    if (pad && vis && st == 2 && (dm_padmode_page_keys(DM_BTN_A) & DM_BTN_A)) atomic_store(&g_ack, 1);   // 手柄：显示着时接管 A 当确定（游戏收不到这一下） | controller: while shown, take over A as OK (the game does not get this press)
    const char *title = dm_str(b, g_kind == NOTICE_LOWMEM ? "dmLowMemTitle" : "dmSafeModeTitle");   // 正文按原因选（k_cf_msg[g_msg]）；内存提示用自己的两条 | the body is picked by cause (k_cf_msg[g_msg])
    if (atomic_exchange(&g_ack, 0)) {
        for (int k = 0; k < 15; k++) { int32_t *r = (int32_t *)(tl[k] + 0x70); r[0] = r[1] = 99999; }   // 边框片挪出屏幕（尺寸不动） | frame tiles off screen (size unchanged)
        cf_place(ti, 99999, 99999); cf_place(ok, 99999, 99999); cf_text(b, ti, "");
        for (int k = 0; k < 3; k++) { cf_place(de[k], 99999, 99999); cf_text(b, de[k], ""); }
        atomic_store(&g_show, 0); LOG("前端：提示「%s」玩家点了确定（%s，游戏内提示框收起）", title, pad ? "手柄 A" : "点按钮"); notice_dismissed(); return;
    }
    // 位置（布局单位，2160 = 屏高）。触屏版跟主菜单面板锚在屏幕中心（x 相对中心），框在 y 640–1260，正好在「进入游戏」上方。
    // 手柄版控件不带锚点，x 按屏宽算：整屏正中（x、y 都居中），盖住左侧按钮栏与右上角色列表（用户：「弹窗没居中，可以遮挡菜单」）
    // Position in layout units (2160 = screen height). Touch widgets are anchored at the screen center (x relative to it), box at y 640-1260, just above "Play".
    // Controller widgets carry no anchor, so x comes from the screen width: dead center of the screen (both x and y), covering the left button column and the character list (user: "the popup is not centered; it may cover the menu")
    int vw = *(int32_t *)(b + 0x211da38), vh = *(int32_t *)(b + 0x211da3c), xr = vh > 0 ? (int)((long)vw * 2160 / vh) : 3840;
    int cx = pad ? xr / 2 : 0, y0 = pad ? (2160 - 620) / 2 : 640;
    cf_text(b, ti, title);
    { const char *m = dm_str(b, g_kind == NOTICE_LOWMEM ? "dmLowMemMsg" : k_cf_msg[g_msg]); for (int k = 0; k < 3; k++) { const char *e = m ? strchr(m, '\n') : NULL; char ln[256];   // 按 \n 拆三行 | split into three lines at \n
        snprintf(ln, sizeof ln, "%.*s", m ? (int)(e ? (size_t)(e - m) : strlen(m)) : 0, m ? m : ""); cf_text(b, de[k], ln); m = e ? e + 1 : NULL; } }
    // 边框：宽 1800、高 620，左上角 (cx − 900, y0)；几何同我们的通知框（dm_notice_geometry：四角与宝石定长、边条拉伸），它以 244 高为基准上下居中，这里换算到 y0 起
    // Frame: 1800 wide, 620 high, top left at (cx - 900, y0); geometry as our notification box (dm_notice_geometry: fixed corners and gems, stretched edge strips), which centers around a 244-high baseline, converted here to start at y0
    { int g[15][4], h = dm_notice_geometry(1800 - 128, 620 - 24, 1800, g), top = (244 - h) / 2;
      // 上面四行各往下多画 2 个单位、压住下一行的上沿：手柄版（不带锚点）Mac 实测行与行之间取整出一条 1 像素暗缝 | the top four rows each draw 2 units taller, over the next row's top edge: on the Mac the controller version (no anchor) showed a 1-pixel dark seam from rounding between rows
      for (int k = 0; h && k < 15; k++) { int r[4] = { cx - 900 + g[k][0], y0 + g[k][1] - top, g[k][2], g[k][3] + (k / 3 < 4 ? 2 : 0) }; cf_tile(b, tl[k], r); } }
    cf_place(ti, cx - 850, y0 + 40); cf_place(ok, cx - 235, y0 + 480);
    for (int k = 0; k < 3; k++) cf_place(de[k], cx - 850, y0 + 160 + 66 * k);
    if (st == 1) { atomic_store(&g_show, 2); LOG("前端：提示已显示：%s（游戏内提示框，%s）", title, pad ? "手柄模式，A 键确定" : "点「确定」按钮"); }
}
