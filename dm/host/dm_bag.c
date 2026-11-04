// 大背包物品栏：两枚页签（背包 / 护身符两页，同一块网格切换显示），用户 11-01 定稿方案 ④，坐在腰带行左侧。布局和底图见 dm/ios/tools/bag_layout.py、gen_bag_art.py。
// 切页：格子控件 grid 的页号 +0x630 每帧写（页 0 背包 / 页 5 护身符），原版按它画格子和物品、算命中。
// 手柄（探针见设计稿 §4.1）：
//   页签是布局里的 ButtonWidget，原版光标悬停、按 A 发 onClickMessage（ClosePanel:TouchBagTab0/1），触屏点按走同一条。原版焦点导航不认识它，方向键这里接：
//   网格第 0 行前三列 ↑ 进页签；页签上 ←→ 选、↓ 回网格；护身符页签 → 进腰带第 0 格，腰带第 0 格 ← 回护身符页签。手上拿着物品时，页签上的 A 宿主吞掉（不切页、不放下）。
//   ZL / ZR：对面面板的页签条要用（原版不看光标在哪，见设计稿实测表），所以只有对面没有页签时才归物品栏，键帽也按这个显隐。
// Large-bag inventory: two tabs (bag and charms pages, shown in the same grid area), user-approved option 4 (11-01), on the left of the belt row. Layout and art: dm/ios/tools/bag_layout.py, gen_bag_art.py.
// Page switching: the grid widget's page +0x630 is written every frame (page 0 bag / page 5 charms); the original draws cells and items and hit-tests by it.
// Controller (probes in design doc §4.1):
//   The tabs are ButtonWidgets in the layout: the original cursor hovers them and A sends onClickMessage (ClosePanel:TouchBagTab0/1), touch taps take the same path. The original focus navigation does not know them, so directions are handled here:
//   Up from the first three columns of grid row 0 enters the tabs; on the tabs left / right pick and down returns to the grid; right from the charm tab enters belt cell 0, left from belt cell 0 returns to the charm tab. With an item in hand, A on a tab is swallowed (no page change, no drop).
//   ZL / ZR: the opposite panel's tab bar needs them (the original ignores where the cursor is, measured table in the design doc), so they belong to the inventory only when the opposite panel has no tabs, and the keycaps follow.
#include "dm_bag_layout.h"
#include "dm_bag_rule.h"
#include "dm_widget.h"
#include "dm_hud.h"
#include "dm_input.h"
#include "dm_cursor.h"
#include "dm_actions.h"
#include "dm_cursor_item.h"
#include "hle.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int dm_padmode_current(void); uint64_t dm_padmode_page_keys(uint64_t mask);
enum { OFF = 99999, COLS = DM_BAG_COLS, ROWS = DM_BAG_ROWS, CHARM_PAGE = 5, CAP = COLS * ROWS, VB = 1024 };
// ZL / ZR 归属：开着这个面板时归它（1）还是归物品栏（0）。方块、交易原版没有页签条，按规则归物品栏；方块已实测，交易未在真实交易中验证（待联机线真机）。
// ZL / ZR ownership: while this panel is open they belong to it (1) or to the inventory (0). The cube and trade have no tab bar in the original, so by rule they go to the inventory; trade has not been verified in a real trade (pending the multiplayer line on devices).
static const struct { const char *panel; int theirs; } k_tab_owner[] = {
    { "VendorPanelLayout", 1 }, { "BankExpansionLayout", 1 }, { "HoradricCubeLayout", 0 }, { "TradeLayout", 0 },
};
static uint8_t *B; static int g_page, g_selling_ticks; static _Atomic int g_tap, g_restore_fail, g_full, g_sell_full; static _Atomic uint32_t g_selling;
int dm_bag_take_full(void) { return atomic_exchange(&g_full, 0); } int dm_bag_take_restore_fail(void) { return atomic_exchange(&g_restore_fail, 0); }
uint64_t dm_bag_cli_item(uint8_t *b, uint32_t id) { return ((uint64_t (*)(uint64_t, uint32_t, uint32_t, uint32_t))(b + 0x45f5f0))((uint64_t)(uintptr_t)(b + 0x2d603a0 + 4 * 0x400), id & 0x7f, id, 4); }   // g_tap = 1 + 点到的图标 | g_tap = 1 + the tapped icon

int dm_bag_page(void) { return g_page; }
static const char *w_name(uint64_t w) { return w ? *(const char **)(uintptr_t)(w + 8) : NULL; }
static uint64_t find_w(uint64_t w, const char *name, int dep) {
    if (!w || dep > 4) return 0; const char *nm = w_name(w); if (nm && !strcmp(nm, name)) return w;
    uint64_t ch = *(uint64_t *)(w + 0x58), n = *(uint64_t *)(w + 0x60); if (!ch || n > 256) return 0;
    for (uint64_t i = 0; i < n; i++) { uint64_t r = find_w(*(uint64_t *)(ch + 8 * i), name, dep + 1); if (r) return r; }
    return 0;
}
static int ours(uint64_t w) { const char *nm = w_name(w); return nm && !strncmp(nm, "TouchBag", 8); }
static void w_move(uint64_t w, int x, int y) { if (!ours(w)) return; int32_t *r = (int32_t *)(w + 0x70); if (r[0] == x && r[1] == y) return; r[0] = x; r[1] = y; ((void (*)(uint64_t))*(uint64_t *)(*(uint64_t *)w + 0x10))(w); }
static void w_text(uint64_t w, const char *t) {   // 同 dm_pouch.c w_text | Same as w_text in dm_pouch.c
    if (!ours(w) || *(uint64_t *)w - (uint64_t)(uintptr_t)B != DM_VT_TEXT) return; size_t n = strlen(t);
    if (*(uint64_t *)(w + 0x90) == n && !memcmp(*(const char **)(w + 0x88), t, n)) return;
    ((void (*)(uint64_t, const char *, uint64_t))(B + 0x136e5c))(w + 0x88, t, n); ((void (*)(uint64_t))(B + 0xadc434))(w); }
static int up(uint64_t p) { return p && *(uint8_t *)(p + 0x50) && *(uint8_t *)(p + 0x51); }

void dm_bag_on_uimsg(const char *arg) {   // dm_pouch_on_uimsg：ClosePanel:TouchBagTabK（空操作，当信号收） | From dm_pouch_on_uimsg: ClosePanel:TouchBagTabK (a no-op, received as a signal)
    if (!strncmp(arg, "TouchBagTab", 11) && (arg[11] == '0' || arg[11] == '1') && !arg[12]) atomic_store(&g_tap, 1 + (arg[11] - '0'));
}
static void set_page(int p, const char *why) { if (p != g_page) { g_page = p; LOG("大背包：切到%s页（%s）", p ? "护身符" : "背包", why); } }
static int tabs_theirs(uint64_t root) { for (unsigned i = 0; i < sizeof k_tab_owner / sizeof k_tab_owner[0]; i++) if (k_tab_owner[i].theirs && up(dm_ui_top(root, k_tab_owner[i].panel))) return 1; return 0; }

// 网格左上角：原版命中函数（虚表 +0x48）从光标点往左上量 | Grid top-left: measured up and left from the cursor with the original hit test (vtable +0x48)
#define HIT(w, px, py) (((int (*)(uint64_t, uint64_t))*(uint64_t *)(*(uint64_t *)(w) + 0x48))((w), (uint64_t)(uint32_t)(px) | ((uint64_t)(uint32_t)(py) << 32)) & 1)
static void pad_nav(uint64_t root, uint64_t panel, uint64_t grid, int held) {
    uint64_t fm = *(uint64_t *)(root + 0xd0), hov = fm ? *(uint64_t *)(fm + 0x178) : 0; const char *hn = w_name(hov); if (!hn) return;
    int cx = *(int32_t *)(B + 0x2d128f8), cy = *(int32_t *)(B + 0x2d128fc);
    double k = *(float *)(panel + 0x80) * *(int32_t *)(B + 0x211da3c) / 2160.0; static int gx0 = -1, gy0 = -1;   // k = 屏幕像素 / 面板单位；gx0 / gy0 = 网格左上角，最近一次在网格上量的 | k = screen pixels per panel unit; gx0 / gy0 = grid top-left, from the latest measurement on the grid
    const int vert = DM_BAG_TAB_DX == 0; const uint64_t ENTER = vert ? DM_BTN_LEFT : DM_BTN_UP, EXIT = vert ? DM_BTN_RIGHT : DM_BTN_DOWN, PREV = vert ? DM_BTN_UP : DM_BTN_LEFT, NEXT = vert ? DM_BTN_DOWN : DM_BTN_RIGHT;
#define TAB_SX(t) (gx0 + (int)((DM_BAG_TAB_X + (t) * DM_BAG_TAB_DX + DM_BAG_TAB_W / 2 - DM_BAG_GRID_X) * k))
#define TAB_SY(t) (gy0 + (int)((DM_BAG_TAB_Y + (t) * DM_BAG_TAB_DY + DM_BAG_TAB_H / 2 - DM_BAG_GRID_Y) * k))
    if (hov == grid && HIT(grid, cx, cy)) {
        int x0 = cx, y0 = cy; while (x0 > 1 && HIT(grid, x0 - 2, cy)) x0 -= 2; while (y0 > 1 && HIT(grid, cx, y0 - 2)) y0 -= 2;
        gx0 = x0; gy0 = y0; double cell = 98 * k;
        if (vert ? cx < x0 + cell : cy < y0 + cell && cx < x0 + 3 * cell) { uint64_t ev = dm_padmode_page_keys(ENTER); if (ev & ENTER) dm_ui_hover_now(TAB_SX(g_page), TAB_SY(g_page)); }
        return; }
#define BELT_SX(u) (gx0 + (int)(((u) - DM_BAG_GRID_X) * k))
#define BELT_SY (gy0 + (int)((DM_BAG_BELT_Y + 49 - DM_BAG_GRID_Y) * k))
    if (!vert && gx0 >= 0 && !strcmp(hn, "Belt") && cx < BELT_SX(DM_BAG_BELT_X + DM_BAG_BELT_CELL)) { if (dm_padmode_page_keys(PREV) & PREV) dm_ui_hover_now(TAB_SX(1), TAB_SY(1)); return; }
    int tab = !strcmp(hn, "TouchBagTab0") ? 0 : !strcmp(hn, "TouchBagTab1") ? 1 : -1; if (tab < 0) return;
    // 背包页签 ←、护身符页签之外的方向不接管，光标照常往对面面板、装备带走 | Left on the bag tab and the directions not listed stay with the original, so the cursor still reaches the opposite panel and the equipment band
    uint64_t ev = dm_padmode_page_keys((tab == 1 || vert ? PREV : 0) | NEXT | EXIT | (held ? DM_BTN_A : 0));
    if (gx0 >= 0) { if ((ev & PREV) && tab == 1) dm_ui_hover_now(TAB_SX(0), TAB_SY(0)); if ((ev & NEXT) && tab == 0) dm_ui_hover_now(TAB_SX(1), TAB_SY(1)); if ((ev & NEXT) && tab == 1 && !vert) dm_ui_hover_now(BELT_SX(DM_BAG_BELT_X + DM_BAG_BELT_CELL / 2), BELT_SY); }
    if ((ev & EXIT) && gx0 >= 0) { if (vert) dm_ui_hover_now(gx0 + (int)(49 * k), cy); else dm_ui_hover_now(cx, gy0 + (int)(49 * k)); }
    if ((ev & DM_BTN_A) && held) { static int lg; if (lg++ < 20) LOG("大背包：手上有物品（id %d），图标上的 A 不切页不放下", held); }
}

void dm_bag_set_opp(int v); int dm_bag_opp_of(uint64_t root); uint32_t dm_bag_take_sell(void); int dm_bag_take_trade_note(void); int dm_bag_take_full(void); int dm_bag_take_restore_fail(void);
const char *dm_str(uint8_t *b, const char *key); void dm_game_notice_ui(uint8_t *main_base, const char *text); uint64_t dm_bag_cli_item(uint8_t *b, uint32_t id);
// QA 入口（CLAUDE.md §2.9）：DM_TEST_UI=<帧>:<identifier>:hover;…，帧从物品栏打开起算；identifier = CharmTabs@0/1 或 grid@x,y。找不到控件日志报红，脚本判失败。玩家不设这个环境变量。
// QA entry (CLAUDE.md §2.9): DM_TEST_UI=<frame>:<identifier>:hover;..., frames counted from the inventory opening; identifier = CharmTabs@0/1 or grid@x,y. A missing widget logs red and fails the script. Players never set this variable.
static void ui_test(uint64_t panel, uint64_t grid) {
    static int n = -1, at[16], f, kind[16], a0[16], a1[16]; if (n < 0) { n = 0; const char *e = getenv("DM_TEST_UI"); while (e && *e && n < 16) { int c = 0; if (sscanf(e, "%d:CharmTabs@%d:hover%n", &at[n], &a0[n], &c) == 2 && c) kind[n] = 0; else if (sscanf(e, "%d:grid@%d,%d:hover%n", &at[n], &a0[n], &a1[n], &c) == 3 && c) kind[n] = 1; else { LOG("【红】DM_TEST_UI 项写错：%s", e); break; } n++; e += c; if (*e == ';') e++; } }
    if (!n || !up(panel) || !grid) return; f++; int vw = *(int32_t *)(B + 0x211da38), vh = *(int32_t *)(B + 0x211da3c);
    for (int i = 0; i < n; i++) if (at[i] && f >= at[i]) {
        // 面板矩形是相对锚点的，不能直接换算；网格的屏幕矩形用原版命中函数量，步长比一格小 | The panel rect is relative to its anchor, so it cannot be converted directly; the grid's screen rect comes from the original hit test, with a step smaller than a cell
        int x0 = -1, y0 = -1, st = vh / 64 + 1; for (int y = 0; y < vh && x0 < 0; y += st) for (int x = 0; x < vw; x += st) if (HIT(grid, x, y)) { x0 = x; y0 = y; break; }
        if (x0 < 0) { LOG("【红】DM_TEST_UI：grid 不在屏幕上"); at[i] = 0; continue; }
        int yy = y0; while (x0 > 0 && HIT(grid, x0 - 1, yy)) x0--; while (y0 > 0 && HIT(grid, x0, y0 - 1)) y0--; double k = *(float *)(panel + 0x80) * vh / 2160.0;
        int u = kind[i] ? a0[i] * 98 + 49 : DM_BAG_TAB_X + a0[i] * DM_BAG_TAB_DX + DM_BAG_TAB_W / 2 - DM_BAG_GRID_X, v = kind[i] ? a1[i] * 98 + 49 : DM_BAG_TAB_Y + a0[i] * DM_BAG_TAB_DY + DM_BAG_TAB_H / 2 - DM_BAG_GRID_Y;
        { static int once; if (!once++) LOG("DM_TEST_UI：网格左上 %d,%d，比例 %.3f；命中 右上 %d 左下 %d 右下 %d", x0, y0, k, HIT(grid, x0 + (int)(1127 * k), y0 + (int)(49 * k)), HIT(grid, x0 + (int)(49 * k), y0 + (int)(637 * k)), HIT(grid, x0 + (int)(1127 * k), y0 + (int)(637 * k))); }
        int x = x0 + (int)(u * k), y = y0 + (int)(v * k); if (dm_ui_hover_now(x, y)) { LOG("DM_TEST_UI：帧 %d %s → %d,%d", f, kind[i] ? "grid" : "CharmTabs", x, y); at[i] = 0; } }
}
// 游戏主线程每帧（dm_pouch.c ui_pump）；box = 物品栏里我们的容器 TouchPouchBox，它的可见位不代表面板开着，面板开着看面板自己的 +0x50 / +0x51。
// Game main thread every frame (ui_pump in dm_pouch.c); box = our container TouchPouchBox in the inventory, whose visible bit does not mean the panel is open; that is the panel's own +0x50 / +0x51.
void dm_bag_ui(uint8_t *b, uint64_t inv_panel, uint64_t box, uint64_t pl) {
    B = b; uint64_t root = *(uint64_t *)(b + 0x2f05458), grid = find_w(inv_panel, "grid", 1); static int was_up; int open = up(inv_panel);
    if (open && !was_up) { LOG("大背包：物品栏打开，页 %d", g_page); }
    if (!open && was_up) set_page(0, "closed");
    was_up = open;
    dm_hud_info hi; dm_hud_get(&hi); int held = hi.valid ? hi.holding : 0;
    { static int lh; if (held != lh) { LOG("大背包：客户端手上 id %d → %d", lh, held); lh = held; } }
    { int t = atomic_exchange(&g_tap, 0); if (t && !held) set_page(t - 1, "tap"); }
    // 自动切页：手上是护身符 → 护身符页，手上是别的 → 背包页；对面面板里指着护身符也切（用户 15:5x）| Automatic page: a charm in hand goes to the charm page, anything else to the bag page; pointing at a charm in the opposite panel also switches (user rule)
    if (open) { int o = dm_bag_opp_of(root); dm_bag_set_opp(o);
        if (held) { uint64_t it = dm_bag_cli_item(b, (uint32_t)held); if (it) set_page(((int (*)(uint64_t, int))(b + 0x574bc0))(it, 13) ? 1 : 0, "held"); }
        else if (o) { dm_cursor_item_info ci; if (dm_cursor_item(&ci) && ci.container != DM_ITEM_AT_INVENTORY && ci.container != DM_ITEM_AT_CHARM && !strncmp(ci.code, "cm", 2)) set_page(1, "hover"); } 
        // 等客户端看到它回了页 0 再卖（搬动的通知包要走一拍）；0x433e50 距上次交易不足 500 ms 返回 0，下一帧再试 | Sell once the client sees it back on page 0 (the move notice takes a tick); 0x433e50 returns 0 within 500 ms of the last deal, so retry next frame
        { static uint32_t sid; static int wait; uint32_t nid = dm_bag_take_sell(); if (nid) { sid = nid; wait = 120; }
          if (sid && (o != 3 || --wait <= 0)) sid = 0;
          uint64_t ci = sid ? dm_bag_cli_item(b, sid) : 0, cd = ci ? *(uint64_t *)(ci + 0x10) : 0; if (cd && pl && wait < 80 && *(uint8_t *)(cd + 0x55) == 0 && ((int (*)(uint64_t, uint64_t, int, int, int, int))(b + 0x433e50))(pl, ci, 1, 0, 1, 0)) { LOG("大背包：卖护身符 id %u（等了 %d 帧）", sid, 120 - wait); sid = 0; } }
        if (dm_bag_take_trade_note()) dm_game_notice_ui(b, dm_str(b, "dmCharmTradeHint")); }
    if (dm_bag_take_full()) dm_game_notice_ui(b, dm_str(b, "dmCharmFull"));
    if (atomic_exchange(&g_sell_full, 0)) dm_game_notice_ui(b, dm_str(b, "dmCharmSellFull"));
    if (dm_bag_take_restore_fail()) dm_game_notice_ui(b, dm_str(b, "dmCharmRestore"));
    if (grid) *(uint8_t *)(grid + 0x630) = g_page ? CHARM_PAGE : 0;
    // 计数：已用格 / 总格；新进护身符页且不在该页时亮红点 | Counts: cells used / total; the red dot lights when something new arrives on the charm page while it is not shown
    { static int last_c = -1, dot; int used[2] = { 0, 0 }, nch = 0; uint64_t inv = pl ? *(uint64_t *)(pl + 0x90) : 0;
      for (uint64_t it = inv && *(uint32_t *)inv == 0x01020304 ? *(uint64_t *)(inv + 0x10) : 0, n = 0; it && n < 2048; n++) { uint64_t d = *(uint64_t *)(it + 0x10); if (!d) break;
          if (*(uint32_t *)(it + 0xc) == 0) { int pg = *(uint8_t *)(d + 0x55), gw = 1, gh = 1; if (pg == 0 || pg == CHARM_PAGE) { dm_cursor_item_size(b, it, &gw, &gh); used[pg != 0] += gw * gh; nch += pg != 0; } }
          it = *(uint64_t *)(d + 0xb0); }
      if (last_c >= 0 && nch > last_c && !g_page) dot = 1; if (g_page) dot = 0; last_c = nch;
      char t[96]; snprintf(t, sizeof t, "%s %d / %d", dm_str(b, g_page ? "dmBagPageCharm" : "dmBagPageBag"), used[g_page != 0], CAP); w_text(find_w(box, "TouchBagCount0", 1), t);
      w_move(find_w(box, "TouchBagHi0", 1), g_page ? OFF : DM_BAG_TAB_X, DM_BAG_TAB_Y);
      w_move(find_w(box, "TouchBagHi1", 1), g_page ? DM_BAG_TAB_X + DM_BAG_TAB_DX : OFF, DM_BAG_TAB_Y + DM_BAG_TAB_DY);
      w_move(find_w(box, "TouchBagDot", 1), dot ? DM_BAG_TAB_X + DM_BAG_TAB_DX + DM_BAG_DOT_DX : OFF, DM_BAG_TAB_Y + DM_BAG_TAB_DY + DM_BAG_DOT_DY); }
    ui_test(inv_panel, grid);
    int pad = dm_padmode_current();
    if (g_page && open && pad) { int o = dm_bag_opp_of(root); dm_cursor_item_info ci; const char *h = o == 1 ? "dmCharmHintStash" : o == 2 ? "dmCharmHintCube" : o == 3 ? "dmCharmHintSell" : NULL; { int got = h && dm_cursor_item(&ci), ok = got && ci.container == DM_ITEM_AT_CHARM; static int lk = -1; int k = ok ? o : got ? 100 + ci.container : -1; if (k != lk) { lk = k; LOG("大背包：键位条 A 提示 %s（对面 %d，光标下容器 %d）", ok ? h : "不声明", o, got ? ci.container : -1); } if (ok) { void dm_padmode_legend(int, const char *); dm_padmode_legend(0, dm_str(b, h)); } } }
    int mine = open && pad && !tabs_theirs(root) && !dm_ui_modal_active(root);
    // 手柄焦点框：光标停在页签上时显示（原版 StashTab_Hover）| Controller focus frame: shown while the cursor rests on a tab (the original StashTab_Hover)
    { uint64_t fm = open && pad ? *(uint64_t *)(root + 0xd0) : 0, hov = fm ? *(uint64_t *)(fm + 0x178) : 0; const char *hn = w_name(hov); int ft = hn && !strcmp(hn, "TouchBagTab0") ? 0 : hn && !strcmp(hn, "TouchBagTab1") ? 1 : -1;
      w_move(find_w(box, "TouchBagFocus", 1), ft >= 0 ? DM_BAG_TAB_X + ft * DM_BAG_TAB_DX - DM_BAG_FOCUS_PAD : OFF, DM_BAG_TAB_Y + (ft > 0 ? ft : 0) * DM_BAG_TAB_DY - DM_BAG_FOCUS_PAD); }
    w_move(find_w(box, "TouchBagKeyL", 1), mine ? DM_BAG_KEYL_X : OFF, DM_BAG_KEYL_Y);
    w_move(find_w(box, "TouchBagKeyR", 1), mine ? DM_BAG_KEYR_X : OFF, DM_BAG_KEYR_Y);
    if (mine) { uint64_t ev = dm_padmode_page_keys(DM_BTN_ZL | DM_BTN_ZR); if (ev & DM_BTN_ZL) set_page(0, "ZL"); if (ev & DM_BTN_ZR) set_page(1, "ZR"); }
    if (open && pad && grid && !dm_ui_modal_active(root)) pad_nav(root, inv_panel, grid, held);
}

// ---- 护身符页的存读：<角色>/<角色>.swcharm（格式见交接文档「护身符页_格式变更」）
// 引擎读档时把 .d2s 里的页 5 丢掉（实测），所以侧文件是唯一权威。写：服务端拍把页 5 每件的位流记进 g_hist（含历史位置）；提交前钩子读本次提交的 .d2s，
// 只把字节确实在里面的写进侧文件——所以侧文件和 .d2s 永远是同一时刻，序列化之后才进页 5 的那件不会两边都有。放不回去的（g_stuck）留在文件里，下次再试。
// Saving the charm page: <character>/<character>.swcharm (format in the handoff doc).
// The engine discards page 5 of the .d2s on load (measured), so the side file is the only authority. Writing: the server tick records every page 5 item's bitstream in g_hist (including old positions); the pre-commit hook reads the .d2s of this commit
// and writes only the ones whose bytes are in it, so the side file and the .d2s are always the same instant and an item that entered page 5 after serialization is not in both. Items that could not be put back (g_stuck) stay in the file for the next try.
typedef struct { uint8_t x, y; uint16_t len; uint8_t *bits; } citem;
enum { HISTN = 1024 };
static citem g_hist[HISTN], g_stuck[CAP]; static int g_nhist, g_nstuck, g_loaded; static char g_char[64];
static pthread_mutex_t g_cmu = PTHREAD_MUTEX_INITIALIZER;
void *dm_memfind(const uint8_t *h, size_t hn, const uint8_t *n, size_t nn); const char *dm_save_workdir(void); int dm_item_write(uint8_t *b, uint64_t it, uint8_t *buf);
uint64_t dm_item_make(uint8_t *b, uint64_t G, uint64_t room, const uint8_t *buf, int len, int *socks_out);
static void path_of(char *p, size_t n, const char *wd, const char *ext) { snprintf(p, n, "%s/%s/%s.%s", wd, g_char, g_char, ext); }
static void hist_add(int x, int y, const uint8_t *bits, int len) {
    for (int i = 0; i < g_nhist; i++) if (g_hist[i].len == len && !memcmp(g_hist[i].bits, bits, (size_t)len)) return;
    if (g_nhist == HISTN) { free(g_hist[0].bits); memmove(g_hist, g_hist + 1, sizeof g_hist[0] * (HISTN - 1)); g_nhist--; }   // 最老的位置早就不在任何存档里了 | The oldest position is long gone from any save
    uint8_t *c = malloc((size_t)len); if (!c) return; memcpy(c, bits, (size_t)len); g_hist[g_nhist++] = (citem){ (uint8_t)x, (uint8_t)y, (uint16_t)len, c };
}
// 校验同 dm_savepkg_swcharm_ok；1 = 文件可用 | Same checks as dm_savepkg_swcharm_ok; 1 = usable
static int charm_valid(const uint8_t *d, size_t n) {
    if (n < 10 || memcmp(d, "SWCH", 4) || d[4] != 1) return 0; int cnt = d[8] | d[9] << 8; if (cnt > CAP) return 0; size_t o = 10;
    for (int i = 0; i < cnt; i++) { if (o + 4 > n) return 0; int x = d[o], y = d[o + 1], l = d[o + 2] | d[o + 3] << 8; if (x >= COLS || y >= ROWS || l < 1 || l > VB || o + 4 + (size_t)l > n) return 0; o += 4 + (size_t)l; }
    return o == n;
}
static void charm_notify_stats(uint8_t *b, uint64_t G, uint64_t player) { ((void (*)(uint64_t, uint64_t, int))(b + 0x64b220))(G, player, 0); ((void (*)(uint64_t, int))(b + 0x5cadb0))(player, 1); }
// 新建的物品放进页 5：格子被占或建不出就返回 0 | Put a freshly built item on page 5: returns 0 if the cell is taken or it cannot be built
static int charm_restore(uint8_t *b, uint64_t G, uint64_t player, int x, int y, const uint8_t *bits, int len) {
    uint64_t pp = *(uint64_t *)(player + 0x38), room = pp ? *(uint64_t *)(pp + 0x20) : 0, inv = *(uint64_t *)(player + 0x90); if (!room || !inv) return 0;
    uint64_t nu = dm_item_make(b, G, room, bits, len, NULL); if (!nu) return 0;
    uint64_t d = *(uint64_t *)(nu + 0x10); uint32_t cls = *(uint32_t *)(player + 4); int row = *(int32_t *)(b + 0x1dae684 + cls * 8 + 4);
    if (!d || !((int (*)(uint64_t, uint64_t, int, int, int, int))(b + 0x560740))(inv, nu, x, y, CHARM_PAGE + 2, row)) { ((void (*)(uint64_t, uint64_t))(b + 0x658be0))(G, nu); return 0; }
    *(uint8_t *)(d + 0x55) = CHARM_PAGE; *(uint32_t *)(nu + 0xc) = 0;
    ((void (*)(uint64_t, uint64_t, uint64_t, int, int, int, int, int, uint64_t))(b + 0x642f70))(G, player, nu, 1, 2, 0, CHARM_PAGE, 0, (uint64_t)(uint32_t)x | (uint64_t)y << 16);
    ((void (*)(int, uint64_t, uint64_t, int))(b + 0x645820))(*(uint8_t *)(G + 0x106), nu, player, 1);
    return 1;
}
static void charm_load(uint8_t *b, uint64_t G, uint64_t player) {
    for (int i = 0; i < g_nhist; i++) free(g_hist[i].bits); for (int i = 0; i < g_nstuck; i++) free(g_stuck[i].bits); g_nhist = g_nstuck = 0;
    const char *wd = dm_save_workdir(); if (!wd || !g_char[0]) return; char p[1200]; path_of(p, sizeof p, wd, "swcharm"); FILE *f = fopen(p, "rb"); if (!f) { LOG("大背包：%s 还没有护身符页（老档），页 0 的护身符搬进来", g_char); return; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET); uint8_t *d = sz > 0 && sz < 4 * 1024 * 1024 ? malloc((size_t)sz) : NULL; int got = d && fread(d, 1, (size_t)sz, f) == (size_t)sz; fclose(f);
    if (!got || !charm_valid(d, (size_t)sz)) { char q[1200]; path_of(q, sizeof q, wd, "swcharm.bad"); int r = rename(p, q); LOG("【红】大背包：%s 损坏，改名留档 → %d；不当空页", p, r); free(d); atomic_store(&g_restore_fail, 1); return; }
    int cnt = d[8] | d[9] << 8, ok = 0; size_t o = 10;
    for (int i = 0; i < cnt; i++) { int x = d[o], y = d[o + 1], l = d[o + 2] | d[o + 3] << 8; const uint8_t *bits = d + o + 4; o += 4 + (size_t)l;
        if (charm_restore(b, G, player, x, y, bits, l)) ok++; else { uint8_t *c = malloc((size_t)l); if (c) { memcpy(c, bits, (size_t)l); g_stuck[g_nstuck++] = (citem){ (uint8_t)x, (uint8_t)y, (uint16_t)l, c }; } LOG("【红】大背包：页 5 格 %d,%d 放不回去，留在文件里", x, y); atomic_store(&g_restore_fail, 1); } }
    free(d); if (ok) charm_notify_stats(b, G, player); LOG("大背包：%s 护身符页读回 %d / %d 件", g_char, ok, cnt);
}
static void charm_snapshot(uint8_t *b, uint64_t player) {   // 页 5 变了才写位流 | Bitstreams are written only when page 5 changed
    uint64_t inv = *(uint64_t *)(player + 0x90), sig = 0; if (!inv || *(uint32_t *)inv != 0x01020304) return; static uint64_t last;
    for (uint64_t it = *(uint64_t *)(inv + 0x10), n = 0; it && n < 2048; n++) { uint64_t d = *(uint64_t *)(it + 0x10), pp = *(uint64_t *)(it + 0x38); if (!d) break;
        if (*(uint32_t *)(it + 0xc) == 0 && pp && *(uint8_t *)(d + 0x55) == CHARM_PAGE) sig = (sig ^ (*(uint32_t *)(it + 8) * 0x100000001b3ull + (uint64_t)*(int32_t *)(pp + 0x10) * 257 + (uint64_t)*(int32_t *)(pp + 0x14))) * 0x100000001b3ull + 1;
        it = *(uint64_t *)(d + 0xb0); }
    if (sig == last) return; last = sig;
    for (uint64_t it = *(uint64_t *)(inv + 0x10), n = 0; it && n < 2048; n++) { uint64_t d = *(uint64_t *)(it + 0x10), pp = *(uint64_t *)(it + 0x38); if (!d) break;
        if (*(uint32_t *)(it + 0xc) == 0 && pp && *(uint8_t *)(d + 0x55) == CHARM_PAGE) { uint8_t buf[VB]; int len = dm_item_write(b, it, buf); if (len > 0) hist_add(*(int32_t *)(pp + 0x10), *(int32_t *)(pp + 0x14), buf, len); else LOG("【红】大背包：页 5 物品 id %u 位流写不出", *(uint32_t *)(it + 8)); }
        it = *(uint64_t *)(d + 0xb0); }
}
static void charm_pre_commit(const char *wd) {   // 提交前钩子（写盘线程，持存档锁） | Pre-commit hook (disk-write thread, holding the save lock)
    pthread_mutex_lock(&g_cmu); if (!g_loaded || !g_char[0]) { pthread_mutex_unlock(&g_cmu); return; }
    char p[1200], t[1210]; path_of(p, sizeof p, wd, "d2s"); FILE *f = fopen(p, "rb"); uint8_t *s = NULL; long sn = 0; if (f) { fseek(f, 0, SEEK_END); sn = ftell(f); fseek(f, 0, SEEK_SET); s = sn > 0 ? malloc((size_t)sn) : NULL; if (s && fread(s, 1, (size_t)sn, f) != (size_t)sn) { free(s); s = NULL; } fclose(f); }
    if (!s) { LOG("大背包：本次提交没有 %s，护身符页不动", p); pthread_mutex_unlock(&g_cmu); return; }
    path_of(p, sizeof p, wd, "swcharm"); snprintf(t, sizeof t, "%s.tmp", p); f = fopen(t, "wb");
    if (f) { uint8_t h[10] = { 'S', 'W', 'C', 'H', 1 }; int n = g_nstuck; for (int i = 0; i < g_nhist; i++) if (dm_memfind(s, (size_t)sn, g_hist[i].bits, g_hist[i].len)) n++; if (n > CAP) { LOG("【红】大背包：页 5 共 %d 件超过 %d", n, CAP); n = CAP; } h[8] = (uint8_t)n; h[9] = (uint8_t)(n >> 8); int ok = fwrite(h, 1, 10, f) == 10, w = 0;
        for (int pass = 0; pass < 2 && ok; pass++) { int m = pass ? g_nhist : g_nstuck; citem *v = pass ? g_hist : g_stuck; for (int i = 0; i < m && w < n && ok; i++) { if (pass && !dm_memfind(s, (size_t)sn, v[i].bits, v[i].len)) continue; uint8_t e[4] = { v[i].x, v[i].y, (uint8_t)v[i].len, (uint8_t)(v[i].len >> 8) }; ok = fwrite(e, 1, 4, f) == 4 && fwrite(v[i].bits, 1, v[i].len, f) == v[i].len; w++; } }
        ok = fclose(f) == 0 && ok; if (!ok || rename(t, p)) { remove(t); LOG("【红】大背包：写 %s 失败", p); } else { static int lastn = -1; if (n != lastn) { lastn = n; LOG("大背包：护身符页写盘 %d 件", n); } } }
    free(s); pthread_mutex_unlock(&g_cmu);
}
__attribute__((constructor)) static void charm_hook_install(void) { extern void (*g_dm_pre_commit_hook[6])(const char *); g_dm_pre_commit_hook[5] = charm_pre_commit; }

// 服务端每拍：进局读回；页 0 的护身符搬进页 5（拾取、买入、交易收货、导入、老档都走这一条），一拍一件；页 5 放不下就留在原处。
// Server every tick: load on entry; charms on page 0 move to page 5 (pickup, purchase, trade, import and old saves all take this path), one per tick; if page 5 has no room they stay.
void dm_bag_srv_tick(uint8_t *b, uint64_t G, uint64_t player) {
    static uint64_t lp; if (!player) { lp = 0; return; }   // 退局那次提交可能晚于这一拍，清单留着，下一个角色进局才换 | The commit on leaving may come after this tick, so the state stays until the next character loads
    pthread_mutex_lock(&g_cmu);
    if (player != lp) { lp = player; uint64_t pd = *(uint64_t *)(player + 0x10); strlcpy(g_char, pd ? (const char *)pd : "", sizeof g_char); charm_load(b, G, player); g_loaded = 1; }
    charm_snapshot(b, player); pthread_mutex_unlock(&g_cmu);
    uint64_t inv = *(uint64_t *)(player + 0x90), f = 0; if (!inv || *(uint32_t *)inv != 0x01020304) return;
    { static uint32_t ls; uint64_t i0 = *(uint64_t *)(player + 0x90), c0 = i0 ? *(uint64_t *)(i0 + 0x40) : 0; uint32_t cid = c0 ? *(uint32_t *)(c0 + 8) : 0; if (cid != ls) { LOG("大背包：服务端手上 id %u → %u", ls, cid); ls = cid; } }
    uint32_t selling = atomic_load(&g_selling); if (selling && --g_selling_ticks <= 0) { atomic_store(&g_selling, 0); selling = 0; }
    int (*is)(uint64_t, int) = (int (*)(uint64_t, int))(b + 0x574bc0); uint8_t used[ROWS][COLS]; memset(used, 0, sizeof used); int nf = 0; static int last_nf = -1;
    for (uint64_t it = *(uint64_t *)(inv + 0x10), n = 0; it && n < 2048; n++) { uint64_t d = *(uint64_t *)(it + 0x10), pp = *(uint64_t *)(it + 0x38); if (!d) break;
        if (*(uint32_t *)(it + 0xc) == 0 && pp) { int pg = *(uint8_t *)(d + 0x55);
            if (pg == CHARM_PAGE) { int gw = 1, gh = 1, x = *(int32_t *)(pp + 0x10), y = *(int32_t *)(pp + 0x14); dm_cursor_item_size(b, it, &gw, &gh); for (int r = y; r < y + gh && r < ROWS; r++) for (int c = x; c < x + gw && c < COLS; c++) if (r >= 0 && c >= 0) used[r][c] = 1; }
            else if (pg == 0 && is(it, 13) && *(uint32_t *)(it + 8) != selling) { nf++; if (!f) f = it; } }
        it = *(uint64_t *)(d + 0xb0); }
    if (!f) { last_nf = 0; return; }
    int gw = 1, gh = 1, fx = -1, fy = -1; dm_cursor_item_size(b, f, &gw, &gh);
    for (int c = 0; c + gw <= COLS && fx < 0; c++) for (int r = 0; r + gh <= ROWS && fx < 0; r++) { int ok = 1; for (int q = r; q < r + gh && ok; q++) for (int w = c; w < c + gw; w++) if (used[q][w]) { ok = 0; break; } if (ok) { fx = c; fy = r; } }
    if (dmbag_full_notice(&last_nf, nf, fx < 0)) { atomic_store(&g_full, 1); LOG("大背包：护身符页满了，背包页有 %d 件护身符等着搬", nf); }
    if (fx < 0) return;
    uint64_t d = *(uint64_t *)(f + 0x10), pp = *(uint64_t *)(f + 0x38); int ox = *(int32_t *)(pp + 0x10), oy = *(int32_t *)(pp + 0x14); uint32_t cls = *(uint32_t *)(player + 4); int row = *(int32_t *)(b + 0x1dae684 + cls * 8 + 4);
    void (*eff)(int, uint64_t, uint64_t, int) = (void (*)(int, uint64_t, uint64_t, int))(b + 0x645820); void (*nt)(uint64_t, uint64_t, uint64_t, int, int, int, int, int, uint64_t) = (void (*)(uint64_t, uint64_t, uint64_t, int, int, int, int, int, uint64_t))(b + 0x642f70);
    eff(*(uint8_t *)(G + 0x106), f, player, 0);
    int ok = ((int (*)(uint64_t, uint64_t, int, int, int, int))(b + 0x560740))(inv, f, fx, fy, CHARM_PAGE + 2, row);
    if (ok) { *(uint8_t *)(d + 0x55) = CHARM_PAGE; *(uint32_t *)(f + 0xc) = 0; nt(G, player, f, 0x20, 4, 0, 0, oy, ox); nt(G, player, f, 1, 2, 0, CHARM_PAGE, 0, (uint64_t)(uint32_t)fx | (uint64_t)fy << 16); eff(*(uint8_t *)(G + 0x106), f, player, 1); }
    else eff(*(uint8_t *)(G + 0x106), f, player, 1);
    ((void (*)(uint64_t, uint64_t, int))(b + 0x64b220))(G, player, 0); ((void (*)(uint64_t, int))(b + 0x5cadb0))(player, 1);
    LOG("大背包：护身符 id %u 页 0 %d,%d → 页 5 %d,%d：%d", *(uint32_t *)(f + 8), ox, oy, fx, fy, ok);
}

// ---- 放置过滤：护身符页不走光标（用户 15:5x）。客户端发的拿起 0x19 / 放下 0x18 里页号是 5 的这里接管：
//   拿起 = 快速移动：对面开着仓库 / 方块就搬过去，商店就卖（客户端拍里走原版卖出），都没开什么也不做；
//   放下 = 手上的护身符落进页 5（点的格放不下就找空位）；手上不是护身符不接，界面切回背包页（dm_bag_ui）。
// Placement filter: the charm page does not use the cursor (user rule). Pick-up 0x19 / place 0x18 packets from the client with page 5 are taken over here:
//   pick up = quick move: to the stash / cube if open, sold if a vendor is open (through the original sale on the client tick), otherwise nothing;
//   place = the charm in hand lands on page 5 (a free cell if the chosen one does not fit); a non-charm is refused and the UI goes back to the bag page (dm_bag_ui).
enum { OPP_NONE, OPP_STASH, OPP_CUBE, OPP_VENDOR, OPP_TRADE };
static _Atomic int g_opp; static _Atomic uint32_t g_sell_id; static _Atomic int g_note_trade;
void dm_bag_set_opp(int v) { atomic_store(&g_opp, v); }
int dm_bag_opp_of(uint64_t root) { return up(dm_ui_top(root, "BankExpansionLayout")) ? OPP_STASH : up(dm_ui_top(root, "HoradricCubeLayout")) ? OPP_CUBE : up(dm_ui_top(root, "VendorPanelLayout")) ? OPP_VENDOR : up(dm_ui_top(root, "TradeLayout")) ? OPP_TRADE : OPP_NONE; }
uint32_t dm_bag_take_sell(void) { return atomic_exchange(&g_sell_id, 0); }
int dm_bag_take_trade_note(void) { return atomic_exchange(&g_note_trade, 0); }
static int page_free(uint64_t inv, int page, int gw, int gh, int *fx, int *fy) {   // 页的格表见 dm_stack.c free_cell | Page grid table: see free_cell in dm_stack.c
    // 空页的格表是放第一件时才建的（0x560740 自己建），没有格表就是全空 | An empty page's grid is only created when the first item goes in (0x560740 builds it), so no grid means all free
    uint64_t pg = *(uint64_t *)(inv + 0x20), e = pg && (uint64_t)page + 2 < *(uint64_t *)(inv + 0x28) ? pg + (uint64_t)(page + 2) * 0x20 : 0, cells = e ? *(uint64_t *)(e + 0x18) : 0; if (!cells) { *fx = 0; *fy = 0; return 1; }
    int w = *(uint8_t *)(e + 0x10), h = *(uint8_t *)(e + 0x11);
    for (int x = 0; x + gw <= w; x++) for (int y = 0; y + gh <= h; y++) { int ok = 1; for (int q = y; q < y + gh && ok; q++) for (int c = x; c < x + gw; c++) if (*(uint64_t *)(cells + 8 * (uint64_t)(q * w + c))) { ok = 0; break; } if (ok) { *fx = x; *fy = y; return 1; } }
    return 0;
}
static int quick_out(uint8_t *b, uint64_t G, uint64_t player, uint32_t id, int np, int nrow) {
    uint64_t inv = *(uint64_t *)(player + 0x90), f = 0; for (uint64_t it = *(uint64_t *)(inv + 0x10), n = 0; it && n < 2048 && !f; n++) { uint64_t d = *(uint64_t *)(it + 0x10); if (!d) break; if (*(uint32_t *)(it + 8) == id) f = it; else it = *(uint64_t *)(d + 0xb0); }
    if (!f) return 0; uint64_t d = *(uint64_t *)(f + 0x10), pp = *(uint64_t *)(f + 0x38); if (!pp || *(uint8_t *)(d + 0x55) != CHARM_PAGE) return 0;
    int gw = 1, gh = 1, x, y, ox = *(int32_t *)(pp + 0x10), oy = *(int32_t *)(pp + 0x14); dm_cursor_item_size(b, f, &gw, &gh); if (!page_free(inv, np, gw, gh, &x, &y)) { LOG("大背包：页 %d 放不下 id %u", np, id); return 0; }
    void (*eff)(int, uint64_t, uint64_t, int) = (void (*)(int, uint64_t, uint64_t, int))(b + 0x645820); void (*nt)(uint64_t, uint64_t, uint64_t, int, int, int, int, int, uint64_t) = (void (*)(uint64_t, uint64_t, uint64_t, int, int, int, int, int, uint64_t))(b + 0x642f70);
    if (np) eff(*(uint8_t *)(G + 0x106), f, player, 0);   // 页 0 的护身符原版也生效，转去卖的那一下不动 | A charm on page 0 is live in the original too, so the hop before selling leaves it on
    if (!((int (*)(uint64_t, uint64_t, int, int, int, int))(b + 0x560740))(inv, f, x, y, np + 2, nrow)) { if (np) eff(*(uint8_t *)(G + 0x106), f, player, 1); LOG("大背包：放进页 %d %d,%d 失败 id %u", np, x, y, id); return 0; }
    *(uint8_t *)(d + 0x55) = (uint8_t)np; *(uint32_t *)(f + 0xc) = 0; nt(G, player, f, 0x20, 4, 0, CHARM_PAGE, oy, ox); nt(G, player, f, 1, 2, 0, np, 0, (uint64_t)(uint32_t)x | (uint64_t)y << 16); charm_notify_stats(b, G, player);
    LOG("大背包：护身符 id %u 页 5 %d,%d → 页 %d %d,%d", id, ox, oy, np, x, y); return 1;
}
static int place_held(uint8_t *b, uint64_t G, uint64_t player, uint32_t id, int x, int y) {
    uint64_t inv = *(uint64_t *)(player + 0x90), it = *(uint64_t *)(inv + 0x40); if (!it || *(uint32_t *)(it + 8) != id || !((int (*)(uint64_t, int))(b + 0x574bc0))(it, 13)) return 0;
    uint64_t d = *(uint64_t *)(it + 0x10); uint32_t cls = *(uint32_t *)(player + 4); int row = *(int32_t *)(b + 0x1dae684 + cls * 8 + 4), gw = 1, gh = 1, fx = x, fy = y; dm_cursor_item_size(b, it, &gw, &gh);
    if (!page_free(inv, CHARM_PAGE, gw, gh, &fx, &fy)) return 0;
    int (*put)(uint64_t, uint64_t, int, int, int, int) = (int (*)(uint64_t, uint64_t, int, int, int, int))(b + 0x560740);
    if (!put(inv, it, x, y, CHARM_PAGE + 2, row)) { if (!put(inv, it, fx, fy, CHARM_PAGE + 2, row)) return 0; x = fx; y = fy; }
    *(uint8_t *)(d + 0x55) = CHARM_PAGE; *(uint32_t *)(it + 0xc) = 0;
    ((void (*)(uint64_t, uint64_t, uint64_t, int, int, int, int, int, uint64_t))(b + 0x642f70))(G, player, it, 1, 2, 0, CHARM_PAGE, 0, (uint64_t)(uint32_t)x | (uint64_t)y << 16);
    ((void (*)(int, uint64_t, uint64_t, int))(b + 0x645820))(*(uint8_t *)(G + 0x106), it, player, 1); charm_notify_stats(b, G, player);
    LOG("大背包：手上护身符 id %u 放进页 5 %d,%d", id, x, y); return 1;
}
// 命令分派入口（diag.c c2s_obs，先于 dm_stack_c2s）；接管了就换成空操作包 | Command dispatch entry (c2s_obs in diag.c, before dm_stack_c2s); a taken-over packet becomes a no-op
void dm_bag_c2s(uint64_t *regs) {
    uint64_t G = regs[0], player = regs[1]; uint8_t *pk = (uint8_t *)(uintptr_t)regs[2]; uint32_t len = (uint32_t)regs[3]; uint8_t *b = B;
    if (!b || !G || !player || !pk || len != 17 || (pk[0] != 0x18 && pk[0] != 0x19)) return;
    uint32_t id, x, y, pg; memcpy(&id, pk + 1, 4); memcpy(&x, pk + 5, 4); memcpy(&y, pk + 9, 4); memcpy(&pg, pk + 13, 4); if (pg != CHARM_PAGE) return;
    if (pk[0] == 0x19) { int o = atomic_load(&g_opp), r = 0;
        if (o == OPP_STASH) r = quick_out(b, G, player, id, 4, 10); else if (o == OPP_CUBE) r = quick_out(b, G, player, id, 3, 9);
        else if (o == OPP_VENDOR) {   // 服务端不收页 5 的卖出（实测「我无法完成该请求」），先搬回页 0 再走原版卖；没卖成的话 g_selling 到期，下一拍搬回页 5 | The server refuses to buy from page 5 (measured), so hop to page 0 and sell the normal way; if the sale does not happen, g_selling expires and the charm goes back to page 5
            r = quick_out(b, G, player, id, 0, *(int32_t *)(b + 0x1dae684 + *(uint32_t *)(player + 4) * 8 + 4)); if (r) { atomic_store(&g_selling, id); g_selling_ticks = 50; atomic_store(&g_sell_id, id); } else atomic_store(&g_sell_full, 1); }
        else if (o == OPP_TRADE) atomic_store(&g_note_trade, 1);
        LOG("大背包：页 5 拿起 id %u → 对面 %d，搬动 %d", id, o, r); }
    else { int r = place_held(b, G, player, id, (int)x, (int)y); LOG("大背包：页 5 放下 id %u → %d", id, r); }
    static uint8_t noop[0x24]; memset(noop, 0, sizeof noop); noop[0] = 0x26; noop[6] = 0xff; regs[2] = (uint64_t)(uintptr_t)noop; regs[3] = sizeof noop;
}
