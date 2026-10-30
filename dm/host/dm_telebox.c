// 设备与使用统计的告知框 v1（看板 PRD 附录 A，用户 2026-10-30「弹一次就好」）：首次到主菜单弹一次，文案一次覆盖四类事件与入口计数；「同意」两个开关都开、「不同意」都关，照常游戏、零请求；答复写进设置文件（dm_custom_tel_answer），不再弹。
// 只在构建配了服务地址、不是测试局时弹（dm_telemetry_notice_due）；崩溃围栏 / 社区 / 诊断框开着时等它们收起。测试开关 DM_TEST_TELEMETRY_NOTICE=1：测试局也弹。
// 画面（同 dm_diagup.c 的做法）：边框 15 片 TouchTeleTile（自己一套切片 Ztouch_tele）+ 标题 48 + 正文十行 36 + 副文 28 + 按键说明一行 28（手柄模式才有字）+ 两颗原版前端按钮，布局 touch_layout.py _telebox，挂在主菜单面板（触屏 MainMenuPanel / 手柄 MainMenuButtonRibbon）末尾，平时在屏外、字空着。触屏点按钮；手柄 A 同意 / B 不同意。开着时吞掉触屏与手柄（dm_diagup.c 转发，hle_hid.c 闸门）。
// Notice v1 of the device and usage statistics (dashboard PRD appendix A, user 2026-10-30 "show it once"): shown once on reaching the main menu, one text covering the four event kinds and the entry counters; Agree turns both switches on, Decline leaves both off with normal play and zero requests; the answer goes to the settings file and it never shows again.
// Only when the build has a service origin and this is not a test run (dm_telemetry_notice_due); it waits for the crash fence / community / diagnostic boxes. Test switch DM_TEST_TELEMETRY_NOTICE=1: test runs show it too.
// Picture (as dm_diagup.c): 15 frame tiles TouchTeleTile (own slices Ztouch_tele) + title 48 + ten body lines 36 + secondary 28 + a key legend line 28 (text only in controller mode) + two original front-end buttons, layout touch_layout.py _telebox, at the end of the main menu panel (touch MainMenuPanel / controller MainMenuButtonRibbon), normally off screen and empty. Touch taps the buttons; controller A agrees, B declines. While up it swallows touch and controller (forwarded by dm_diagup.c, gate at hle_hid.c).
#include "dm_telemetry.h"
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
#include <time.h>
int dm_ingame_now(void);
int dm_community_modal(void);
int dm_crashfence_busy(void);
int dm_diagup_own_modal(void);

#define BOX_W 2700
#define BOX_H 800
#define TEXT_W 2580
#define N_BODY 10
#define SHOW_DELAY_S 1.0   // 主菜单先站稳再弹 | let the main menu settle first
static _Atomic int g_show;      // 0 不显示 / 1 要显示 / 2 显示着 / 3 收起 | 0 hidden / 1 pending / 2 shown / 3 dismissing
static _Atomic int g_action;    // 1 同意 / 2 不同意（界面消息 / 手柄键攒下来，pump 里做） | 1 agree / 2 decline, queued for the pump
static _Atomic uint64_t g_md_ev;
static _Atomic unsigned g_sw_touch, g_sw_key, g_leak; static _Atomic int g_after; static _Atomic unsigned g_after_n;
int dm_telebox_modal(void) { return atomic_load(&g_show) != 0; }
void dm_telebox_on_uimsg(const char *cat, const char *act, const char *arg) {   // 界面消息（diag.c，游戏主线程） | UI message (diag.c, game main thread)
    if (!cat || !act || !arg || strcmp(cat, "PanelManager") || strcmp(act, "ClosePanel") || atomic_load(&g_show) != 2) return;
    if (!strcmp(arg, "TouchTeleAgree")) atomic_store(&g_action, 1); else if (!strcmp(arg, "TouchTeleDecline")) atomic_store(&g_action, 2);
}
static int stick_off(int32_t v) { return v > -8000 && v < 8000; }
// 手柄闸门（hle_hid.c，同 dm_diagup_input_gate）：开着时键与两根摇杆全清零、按下沿留给框；关掉后这些键全松开、摇杆回到 1/4 以内之前也不交给游戏 | controller gate (hle_hid.c, as dm_diagup_input_gate): while up everything is zeroed and press edges go to the box; after it closes nothing reaches the game until keys are released and sticks centred
void dm_telebox_input_gate(uint64_t *bt, int32_t *lx, int32_t *ly, int32_t *rx, int32_t *ry) {
    static uint64_t prev, held; static int stick;
    if (!atomic_load(&g_show)) {
        if (atomic_load(&g_after) && bt && lx && ly && rx && ry) {
            int centred = stick_off(*lx) && stick_off(*ly) && stick_off(*rx) && stick_off(*ry);
            held &= *bt; if (centred) stick = 0;
            if (held || stick) { atomic_fetch_add(&g_after_n, 1); *bt = 0; *lx = *ly = *rx = *ry = 0; }
            else { atomic_store(&g_after, 0); LOG("统计告知：关框后压住按着的键 / 摇杆 %u 拍，弹窗期间按键 %u 次、点按 %u 次，交给底层 %u", atomic_exchange(&g_after_n, 0), atomic_exchange(&g_sw_key, 0), atomic_exchange(&g_sw_touch, 0), atomic_exchange(&g_leak, 0)); } }
        prev = bt ? *bt : 0; return; }
    if (!bt || !lx || !ly || !rx || !ry) return;
    held = *bt; stick = !(stick_off(*lx) && stick_off(*ly) && stick_off(*rx) && stick_off(*ry)); atomic_store(&g_after, 1); atomic_store(&g_after_n, 0);
    uint64_t down = *bt & ~prev; prev = *bt;
    if (down) atomic_fetch_add(&g_sw_key, 1);
    if (down & (DM_BTN_A | DM_BTN_B)) atomic_fetch_or(&g_md_ev, down & (DM_BTN_A | DM_BTN_B));
    *bt = 0; *lx = *ly = *rx = *ry = 0;
}
void dm_telebox_input_leak(int leaked) { if (leaked && (atomic_load(&g_show) || atomic_load(&g_after))) atomic_fetch_add(&g_leak, 1); }

// ---- 画面（游戏主线程；控件操作同 dm_diagup.c du_*） | picture (game main thread; widget handling as dm_diagup.c du_*) ----
static uint8_t *tb_base(void) { static uint8_t *b; if (!b) for (int i = 0; i < g_nmod; i++) if (!strcmp(g_mod[i].name, "main")) b = (uint8_t *)g_mod[i].base; return b; }
static uint64_t tb_find(uint64_t w, const char *name, int dep) {   // 名字 +8、子控件数组 +0x58、个数 +0x60 | name +8, children +0x58, count +0x60
    if (!w || dep > 6) return 0; const char *nm = *(const char **)(w + 8); if (nm && !strcmp(nm, name)) return w;
    uint64_t ch = *(uint64_t *)(w + 0x58), n = *(uint64_t *)(w + 0x60); if (!ch || n > 256) return 0;
    for (uint64_t i = 0; i < n; i++) { uint64_t r = tb_find(*(uint64_t *)(ch + 8 * i), name, dep + 1); if (r) return r; }
    return 0;
}
static void tb_place(uint64_t w, int x, int y) {   // 只挪自己的控件（名字 TouchTele 开头） | only our own widgets (names starting with TouchTele)
    const char *nm = w ? *(const char **)(w + 8) : NULL; if (!nm || strncmp(nm, "TouchTele", 9)) return;
    int32_t *r = (int32_t *)(w + 0x70); if (r[0] == x && r[1] == y) return; r[0] = x; r[1] = y; ((void (*)(uint64_t))*(uint64_t *)(*(uint64_t *)w + 0x10))(w);
}
static void tb_text(uint8_t *b, uint64_t w, const char *s) {
    if (!w || *(uint64_t *)w - (uint64_t)(uintptr_t)b != DM_VT_TEXT) return;
    size_t n = strlen(s); if (*(uint64_t *)(w + 0x90) == n && !memcmp(*(const char **)(w + 0x88), s, n)) return;
    ((void *(*)(uint64_t, const char *, uint64_t))(b + 0x136e5c))(w + 0x88, s, n); ((void (*)(uint64_t))(b + 0xadc434))(w);
}
static void tb_tile(uint8_t *b, uint64_t w, const int want[4]) {   // 只改自己的单帧切片 Ztouch_tele/tileRC（同 dm_diagup.c du_tile） | only our own slice Ztouch_tele/tileRC
    const char *name = w ? *(const char **)(w + 8) : NULL, *pre = "TouchTeleTile"; size_t n = strlen(pre);
    if (!name || strncmp(name, pre, n) || name[n] < '0' || name[n] > '4' || name[n + 1] < '0' || name[n + 1] > '2' || name[n + 2]) return;
    if (want[2] < 1 || want[2] > 4000 || want[3] < 1 || want[3] > 800 || !dm_widget_is(b, w, DM_VT_IMAGE)) return;
    char expect[40]; snprintf(expect, sizeof expect, "Ztouch_tele/tile%c%c", name[n], name[n + 1]);
    const char *file = *(const char **)(w + 0x108); uint64_t len = *(uint64_t *)(w + 0x110);
    if (!file || len != strlen(expect) || memcmp(file, expect, len) || *(int32_t *)(w + 0x90) != 0) return;
    uint64_t ref = *(uint64_t *)(w + 0x88), sp = ref ? *(uint64_t *)ref : 0; if (!sp) return;
    uint32_t *atlas = (uint32_t *)(sp + 0xbc), *height = (uint32_t *)(sp + 0xc0), *frames = (uint32_t *)(sp + 0xcc), *width = (uint32_t *)(sp + 0xd0);
    if (*frames != 1 || *atlas != *width || !*width || *width > 4000 || !*height || *height > 800) return;
    *atlas = *width = (uint32_t)want[2]; *height = (uint32_t)want[3];
    int32_t *r = (int32_t *)(w + 0x70); if (memcmp(r, want, 4 * sizeof *r)) { r[0] = want[0]; r[1] = want[1]; r[2] = want[2]; r[3] = want[3]; }
}
static uint64_t tb_host(uint64_t root, int pad, const char **name) {   // 同崩溃围栏提示框：手柄版挂按钮栏（画在主菜单之上） | as the crash-fence box: the controller layout hangs under the button column (drawn above the main menu)
    const char *h = pad ? "MainMenuButtonRibbon" : "MainMenuPanel"; if (name) *name = h;
    uint64_t q = root ? dm_ui_top(root, h) : 0; return q && *(uint8_t *)(q + 0x50) && tb_find(q, "TouchTeleTitle", 0) ? q : 0;
}
// 触屏抬起（dm_diagup.c 转发，弹窗开着时）：用控件自己的命中判断，只认两颗按钮，其余吞掉 | touch lift (forwarded by dm_diagup.c while up): the widgets' own hit test, only the two buttons, the rest swallowed
void dm_telebox_ui_tap(uint64_t root, int x, int y) {
    atomic_fetch_add(&g_sw_touch, 1);
    int dm_padmode_current(void); uint64_t p = tb_host(root, dm_padmode_current(), NULL); if (!p || atomic_load(&g_show) != 2) return;
    uint64_t xy = (uint64_t)(uint32_t)x | (uint64_t)(uint32_t)y << 32;
    static const char *const btn[2] = { "TouchTeleAgree", "TouchTeleDecline" };
    for (int k = 0; k < 2; k++) { uint64_t w = tb_find(p, btn[k], 0); if (!w || ((int32_t *)(w + 0x70))[0] == 99999) continue;
        if (((int (*)(uint64_t, uint64_t))*(uint64_t *)(*(uint64_t *)w + 0x48))(w, xy) & 1) { LOG("统计告知：弹窗开着时点按 (%d,%d) → %s", x, y, btn[k]); atomic_store(&g_action, k + 1); return; } }
    LOG("统计告知：弹窗开着时点按 (%d,%d) → 没点在按钮上，吞掉", x, y);
}
static void tb_hide_all(uint8_t *b, uint64_t p) {
    for (int k = 0; k < 15; k++) { char nm[24]; snprintf(nm, sizeof nm, "TouchTeleTile%d%d", k / 3, k % 3); uint64_t t = tb_find(p, nm, 0); if (t) { int32_t *r = (int32_t *)(t + 0x70); r[0] = r[1] = 99999; } }
    tb_place(tb_find(p, "TouchTeleTitle", 0), 99999, 99999); tb_text(b, tb_find(p, "TouchTeleTitle", 0), "");
    for (int k = 0; k < N_BODY + 2; k++) { char nm[24]; snprintf(nm, sizeof nm, "TouchTeleDesc%d", k); uint64_t w = tb_find(p, nm, 0); if (w) { tb_place(w, 99999, 99999); tb_text(b, w, ""); } }
    tb_place(tb_find(p, "TouchTeleAgree", 0), 99999, 99999); tb_place(tb_find(p, "TouchTeleDecline", 0), 99999, 99999);
}
static double now_s(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }
void dm_telebox_pump(void) {   // 取手柄处每帧调（游戏主线程，hle_hid.c） | every frame at the controller poll (game main thread, hle_hid.c)
    int st = atomic_load(&g_show);
    static int done; static double visible_at;
    if (!st && done) return;
    int dm_padmode_current(void); int pad = dm_padmode_current(); uint8_t *b = tb_base(); uint64_t root = b ? *(uint64_t *)(b + 0x2f05458) : 0; const char *hn = ""; uint64_t p = root ? tb_host(root, pad, &hn) : 0;
    if (!st) {
        if (!dm_telemetry_notice_due()) { if (dm_custom_loaded() && dm_custom_tel_notice()) done = 1; return; }
        if (!p || dm_ingame_now() || dm_community_modal() || dm_diagup_own_modal() || dm_crashfence_busy()) { visible_at = 0; return; }
        double now = now_s(); if (!visible_at) visible_at = now; if (now < visible_at + SHOW_DELAY_S) return;
        atomic_store(&g_action, 0); atomic_store(&g_md_ev, 0); atomic_store(&g_show, st = 1); LOG("统计告知：打开告知框 v1");
    }
    if (st == 2 && pad && p) { uint64_t ev = atomic_exchange(&g_md_ev, 0); if (ev & DM_BTN_A) atomic_store(&g_action, 1); else if (ev & DM_BTN_B) atomic_store(&g_action, 2); }
    int a = st == 2 ? atomic_exchange(&g_action, 0) : 0;
    if (a) { LOG("统计告知：玩家选了%s（%s）", a == 1 ? "同意" : "不同意", pad ? "手柄" : "触屏 / 鼠标"); dm_custom_tel_answer(a == 1); done = 1; atomic_store(&g_show, st = 3); }
    if (st == 3) { if (p) tb_hide_all(b, p); atomic_store(&g_show, 0); return; }
    if (!p) return;   // 面板在换（切模式重建 / 进选项）：下一帧按名字重找；没答完不算答过 | the panel is changing (mode switch rebuild / options): looked up again next frame; no answer means not answered
    uint64_t ti = tb_find(p, "TouchTeleTitle", 0), de[N_BODY + 2], ag = tb_find(p, "TouchTeleAgree", 0), dc = tb_find(p, "TouchTeleDecline", 0), tl[15]; int all = ti && ag && dc;
    for (int k = 0; k < N_BODY + 2; k++) { char nm[24]; snprintf(nm, sizeof nm, "TouchTeleDesc%d", k); de[k] = tb_find(p, nm, 0); all &= de[k] != 0; }
    for (int k = 0; k < 15; k++) { char nm[24]; snprintf(nm, sizeof nm, "TouchTeleTile%d%d", k / 3, k % 3); tl[k] = tb_find(p, nm, 0); all &= tl[k] != 0; }
    if (!all) { atomic_store(&g_show, 0); done = 1; LOG("【红】统计告知：%s 里的控件不全，不显示（没答过，下次启动再弹）", hn); return; }
    tb_text(b, ti, dm_str(b, "dmTeleTitle"));
    for (int k = 0; k < N_BODY; k++) { char key[20]; snprintf(key, sizeof key, "dmTele%d", k); tb_text(b, de[k], dm_str(b, key)); }
    tb_text(b, de[N_BODY], dm_str(b, "dmTeleSub")); tb_text(b, de[N_BODY + 1], pad ? dm_str(b, "dmTelePad") : "");
    // 位置同 dm_diagup.c：面板原点 + 缩放，整屏居中 | placement as dm_diagup.c: panel origin + scale, screen centered
    int vw = *(int32_t *)(b + 0x211da38), vh = *(int32_t *)(b + 0x211da3c), xr = vh > 0 ? (int)((long)vw * 2160 / vh) : 3840;
    float ax = *(float *)(p + 0x48), ay = *(float *)(p + 0x4c), s = *(float *)(p + 0x80); int32_t *pr = (int32_t *)(p + 0x70);
    if (!(ax >= 0 && ax <= 1)) ax = 0; if (!(ay >= 0 && ay <= 1)) ay = 0; if (!(s > 0.05f && s < 10)) s = 1;
    int ox = (int)(ax * xr) + pr[0], oy = (int)(ay * 2160) + pr[1];
    int cx = (int)((xr / 2 - ox) / s), y0 = (int)((1080 - oy) / s) - BOX_H / 2;
    { int gm[15][4], h = dm_notice_geometry(BOX_W - 128, BOX_H - 24, BOX_W, gm), top = (244 - h) / 2;
      for (int k = 0; h && k < 15; k++) { int r[4] = { cx - BOX_W / 2 + gm[k][0], y0 + gm[k][1] - top, gm[k][2], gm[k][3] + (k / 3 < 4 ? 2 : 0) }; tb_tile(b, tl[k], r); } }
    tb_place(ti, cx - TEXT_W / 2, y0 + 24);
    for (int k = 0; k < N_BODY; k++) tb_place(de[k], cx - TEXT_W / 2, y0 + 92 + k * 48);
    tb_place(de[N_BODY], cx - TEXT_W / 2, y0 + 576); tb_place(de[N_BODY + 1], cx - TEXT_W / 2, y0 + 614);
    tb_place(ag, cx - 500, y0 + 664); tb_place(dc, cx + 30, y0 + 664);
    if (st == 1) { atomic_store(&g_show, 2); LOG("统计告知：已显示（%s，面板 %s 矩形 %d,%d 缩放 %.2f → 框中心 %d 顶 %d）", pad ? "手柄模式，A 同意 B 不同意" : "触屏 / 鼠标点按钮", hn, pr[0], pr[1], s, cx, y0); }
}
