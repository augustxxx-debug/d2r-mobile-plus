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

extern char **environ;
static int g_started, g_streak, g_marked, g_test;
static _Atomic int g_notice;

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
static int marker_write(int streak) {   // 先写临时文件再换名：写到一半被杀不会留下半个标记 | write a temporary file first, then rename: being killed halfway leaves no half marker
    char p[1100], t[1120]; if (!marker_path(p, sizeof p)) return 0;
    snprintf(t, sizeof t, "%s.tmp", p); FILE *f = fopen(t, "w"); if (!f) return 0;
    int ok = fprintf(f, "streak=%d\n%s", streak, g_test ? "test=1\n" : "") > 0; ok = fclose(f) == 0 && ok;
    return ok && rename(t, p) == 0;
}
// 读残留：-1 = 没有标记；否则标记里的 k（坏内容按 0 算：标记在，至少是一次异常退出）；*test = 残留来自测试局 | read the leftover: -1 = no marker; otherwise k from the marker (bad contents count as 0: the marker exists, so at least one abnormal exit); *test = the leftover comes from a test run
static int marker_read(int *test) {
    char p[1100]; *test = 0; if (!marker_path(p, sizeof p)) return -1;
    FILE *f = fopen(p, "r"); if (!f) return -1;
    char ln[64]; int k = 0;
    while (fgets(ln, sizeof ln, f)) { if (!strncmp(ln, "streak=", 7)) { k = atoi(ln + 7); if (k < 0 || k > 1000000) k = 0; } else if (!strncmp(ln, "test=1", 6)) *test = 1; }
    fclose(f); return k;
}

int dm_crashfence_start(void) {
    if (g_started) return g_streak;
    g_started = 1;
    char p[1100]; if (!marker_path(p, sizeof p)) { LOG("【红】崩溃围栏：没有设置目录，不写运行标记"); return 0; }
    const char *tm = getenv("DM_TEST_CRASH_MARK"); int test = tm && atoi(tm) >= 1, was_test = 0;
    int k = test ? atoi(tm) - 1 : marker_read(&was_test);
    g_streak = k < 0 ? 0 : was_test ? k : k + 1;   // 测试局的残留不加：仍是那一局开始时的连续次数 | a test run's leftover adds nothing: still the streak at that run's start
    if (k >= 0 && was_test) LOG("崩溃围栏：上一局异常退出（测试局，不计；连续 %d 次不变）", g_streak);
    else if (g_streak) LOG("崩溃围栏：上一局异常退出（第 %d 次连续）%s", g_streak, test ? "（测试开关 DM_TEST_CRASH_MARK）" : "");
    else LOG("崩溃围栏：上一局正常结束");
    const char *te = test_env(); g_test = te != NULL;
    if (te) LOG("崩溃围栏：本局是测试局（%s），被强杀不计入连续异常退出", te);
    g_marked = marker_write(g_streak);
    if (!g_marked) LOG("【红】崩溃围栏：运行标记写不进 %s", p);
    if (g_streak >= 2) {
        char what[256]; int n = dm_custom_safe_mode(what, sizeof what);
        atomic_store(&g_notice, 1);
        LOG("崩溃围栏：连续 %d 次异常退出，关闭全部实验性功能（%s；改了 %d 项%s），主菜单提示一次", g_streak, what, n, n ? "，已写回设置文件" : "");
    }
    return g_streak;
}
void dm_crashfence_clean(const char *why) {
    if (!g_marked) return;
    char p[1100]; if (!marker_path(p, sizeof p)) return;
    int ok = unlink(p) == 0; g_marked = !ok; if (ok) g_streak = 0;
    LOG("崩溃围栏：%s，清除运行标记%s", why ? why : "正常结束", ok ? "" : "【红】失败");
}
void dm_crashfence_resume(void) {
    if (!g_started || g_marked) return;
    g_marked = marker_write(g_streak);
    LOG("崩溃围栏：回前台，重写运行标记%s", g_marked ? "" : "【红】失败");
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
void dm_crashfence_main_menu(void) {
    if (!dm_crashfence_notice_take()) return;
    atomic_store(&g_show, 1); LOG("崩溃围栏：主菜单提示「已关闭实验性功能」（游戏内提示框）");
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
    static int wait; uint8_t *b = cf_base(); uint64_t root = b ? *(uint64_t *)(b + 0x2f05458) : 0, mm = root ? cf_find(root, "MainMenuPanel", 0) : 0;
    uint64_t ti = mm ? cf_find(mm, "TouchSafeTitle", 0) : 0, ok = mm ? cf_find(mm, "TouchSafeOk", 0) : 0, de[3], tl[15]; int tiles = 1;
    for (int k = 0; k < 15; k++) { char nm[24]; snprintf(nm, sizeof nm, "TouchSafeTile%d%d", k / 3, k % 3); tl[k] = mm ? cf_find(mm, nm, 0) : 0; tiles &= tl[k] != 0; }
    for (int k = 0; k < 3; k++) { char nm[24]; snprintf(nm, sizeof nm, "TouchSafeDesc%d", k); de[k] = mm ? cf_find(mm, nm, 0) : 0; }   // 正文三行各一个文字框（游戏文字框不认换行） | one text box per body line (the game's text box ignores newlines)
    if (!tiles || !ti || !de[0] || !de[1] || !de[2] || !ok) { if (st == 1 && ++wait == 600) { atomic_store(&g_show, 0); LOG("【红】崩溃围栏：主菜单面板里没有提示框控件（原版布局？），只记日志不显示"); } return; }
    int dm_padmode_current(void); uint64_t dm_padmode_page_keys(uint64_t); int pad = dm_padmode_current(), vis = *(uint8_t *)(mm + 0x50);
    if (pad && vis && st == 2 && (dm_padmode_page_keys(DM_BTN_A) & DM_BTN_A)) atomic_store(&g_ack, 1);   // 手柄：显示着时接管 A 当确定（游戏收不到这一下） | controller: while shown, take over A as OK (the game does not get this press)
    const char *title = dm_str(b, "dmSafeModeTitle");
    if (atomic_exchange(&g_ack, 0)) {
        for (int k = 0; k < 15; k++) { int32_t *r = (int32_t *)(tl[k] + 0x70); r[0] = r[1] = 99999; }   // 边框片挪出屏幕（尺寸不动） | frame tiles off screen (size unchanged)
        cf_place(ti, 99999, 99999); cf_place(ok, 99999, 99999); cf_text(b, ti, "");
        for (int k = 0; k < 3; k++) { cf_place(de[k], 99999, 99999); cf_text(b, de[k], ""); }
        atomic_store(&g_show, 0); LOG("前端：提示「%s」玩家点了确定（%s，游戏内提示框收起）", title, pad ? "手柄 A" : "点按钮"); return;
    }
    // 位置（布局单位，2160 = 屏高）。触屏版跟主菜单面板锚在屏幕中心（x 相对中心），框在 y 640–1260，正好在「进入游戏」上方。
    // 手柄版原版主菜单左边是一整条按钮栏（右沿约 800 单位）、右上是角色列表（约 y 860–1140）：Mac 原生宿主实测居中摆会被按钮栏盖住左边框、右边挤着列表，
    // 所以手柄版放在按钮栏右边那块的中间、列表下面（y 1180–1800，底部提示条之上）；控件不带锚点，x 按屏宽算
    // Position in layout units (2160 = screen height). Touch widgets are anchored at the screen center (x relative to it), box at y 640-1260, just above "Play".
    // The original controller main menu has a full-height button column on the left (right edge about 800 units) and the character list at the top right (about y 860-1140): on the Mac native host a centered box had its left frame covered by the column and its right side crowding the list,
    // so the controller version sits centered in the area right of the column, below the list (y 1180-1800, above the bottom hint bar); the widgets carry no anchor, so x comes from the screen width
    int vw = *(int32_t *)(b + 0x211da38), vh = *(int32_t *)(b + 0x211da3c), xr = vh > 0 ? (int)((long)vw * 2160 / vh) : 3840;
    int cx = pad ? (820 + xr) / 2 : 0, y0 = pad ? 1180 : 640;
    cf_text(b, ti, title);
    { const char *m = dm_str(b, "dmSafeModeMsg"); for (int k = 0; k < 3; k++) { const char *e = m ? strchr(m, '\n') : NULL; char ln[256];   // 按 \n 拆三行 | split into three lines at \n
        snprintf(ln, sizeof ln, "%.*s", m ? (int)(e ? (size_t)(e - m) : strlen(m)) : 0, m ? m : ""); cf_text(b, de[k], ln); m = e ? e + 1 : NULL; } }
    // 边框：宽 1800、高 620，左上角 (cx − 900, y0)；几何同我们的通知框（dm_notice_geometry：四角与宝石定长、边条拉伸），它以 244 高为基准上下居中，这里换算到 y0 起
    // Frame: 1800 wide, 620 high, top left at (cx - 900, y0); geometry as our notification box (dm_notice_geometry: fixed corners and gems, stretched edge strips), which centers around a 244-high baseline, converted here to start at y0
    { int g[15][4], h = dm_notice_geometry(1800 - 128, 620 - 24, 1800, g), top = (244 - h) / 2;
      for (int k = 0; h && k < 15; k++) { int r[4] = { cx - 900 + g[k][0], y0 + g[k][1] - top, g[k][2], g[k][3] }; cf_tile(b, tl[k], r); } }
    cf_place(ti, cx - 850, y0 + 40); cf_place(ok, cx - 235, y0 + 480);
    for (int k = 0; k < 3; k++) cf_place(de[k], cx - 850, y0 + 160 + 66 * k);
    if (st == 1) { atomic_store(&g_show, 2); LOG("前端：提示已显示：%s（游戏内提示框，%s）", title, pad ? "手柄模式，A 键确定" : "点「确定」按钮"); }
}
