// 大背包物品栏：纵向图标条（背包 / 护身符两页，同一块网格切换显示）。布局和底图见 dm/ios/tools/bag_layout.py、gen_bag_art.py。
// 切页：格子控件 grid 的页号 +0x630 每帧写（页 0 背包 / 页 5 护身符），原版按它画格子和物品、算命中。
// 手柄（用户 10-31 定稿 A5 手柄 v2，探针见设计稿 §4.1）：
//   图标是布局里的 ButtonWidget，原版光标悬停、按 A 发 onClickMessage（ClosePanel:TouchBagTab0/1），触屏点按走同一条。原版焦点导航不认识它，方向键这里接：
//   网格第 0 列 ← 进图标条；图标条上 ↑↓ 选项、→ 回网格；← 不接管，光标照常往对面面板走。手上拿着物品时，图标上的 A 宿主吞掉（不切页、不放下）。
//   ZL / ZR：对面面板的页签条要用（原版不看光标在哪，见设计稿实测表），所以只有对面没有页签时才归物品栏，键帽也按这个显隐。
// Large-bag inventory: vertical strip (two pages, bag and charms, shown in the same grid area). Layout and art: dm/ios/tools/bag_layout.py, gen_bag_art.py.
// Page switching: the grid widget's page +0x630 is written every frame (page 0 bag / page 5 charms); the original draws cells and items and hit-tests by it.
// Controller (user-approved A5 controller v2, 10-31; probes in design doc §4.1):
//   The icons are ButtonWidgets in the layout: the original cursor hovers them and A sends onClickMessage (ClosePanel:TouchBagTab0/1), touch taps take the same path. The original focus navigation does not know them, so directions are handled here:
//   Left from grid column 0 enters the strip; on the strip up / down pick an entry and right returns to the grid; left is not taken over, the cursor moves on to the opposite panel as usual. With an item in hand, A on an icon is swallowed (no page change, no drop).
//   ZL / ZR: the opposite panel's tab bar needs them (the original ignores where the cursor is, measured table in the design doc), so they belong to the inventory only when the opposite panel has no tabs, and the keycaps follow.
#include "dm_widget.h"
#include "dm_hud.h"
#include "dm_input.h"
#include "dm_cursor.h"
#include "dm_actions.h"
#include "dm_cursor_item.h"
#include "hle.h"
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int dm_padmode_current(void); uint64_t dm_padmode_page_keys(uint64_t mask);
enum { OFF = 99999, COLS = 12, ROWS = 7, GRID_X = 158, GRID_Y = 599, STRIP_X = 78, TAB_W = 76, TAB_H = 117, TAB_STEP = 124, CHARM_PAGE = 5, CAP = COLS * ROWS };
// ZL / ZR 归属：开着这个面板时归它（1）还是归物品栏（0）。方块、交易原版没有页签条，按规则归物品栏；方块已实测，交易未在真实交易中验证（待联机线真机）。
// ZL / ZR ownership: while this panel is open they belong to it (1) or to the inventory (0). The cube and trade have no tab bar in the original, so by rule they go to the inventory; trade has not been verified in a real trade (pending the multiplayer line on devices).
static const struct { const char *panel; int theirs; } k_tab_owner[] = {
    { "VendorPanelLayout", 1 }, { "BankExpansionLayout", 1 }, { "HoradricCubeLayout", 0 }, { "TradeLayout", 0 },
};
static uint8_t *B; static int g_page; static _Atomic int g_tap;   // g_tap = 1 + 点到的图标 | g_tap = 1 + the tapped icon

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

// 网格的屏幕矩形：原版命中函数（虚表 +0x48）从光标点往外量 | Screen rect of the grid: measured outward from the cursor with the original hit test (vtable +0x48)
#define HIT(w, px, py) (((int (*)(uint64_t, uint64_t))*(uint64_t *)(*(uint64_t *)(w) + 0x48))((w), (uint64_t)(uint32_t)(px) | ((uint64_t)(uint32_t)(py) << 32)) & 1)
static void pad_nav(uint64_t root, uint64_t grid, int held) {
    uint64_t fm = *(uint64_t *)(root + 0xd0), hov = fm ? *(uint64_t *)(fm + 0x178) : 0; const char *hn = w_name(hov); if (!hn) return;
    int cx = *(int32_t *)(B + 0x2d128f8), cy = *(int32_t *)(B + 0x2d128fc), vw = *(int32_t *)(B + 0x211da38), vh = *(int32_t *)(B + 0x211da3c);
    static double k = 0.58; static int gx0 = -1, gy0 = -1;   // k = 屏幕像素 / 面板单位；gx0 / gy0 = 网格左上角，最近一次在网格上量的 | k = screen pixels per panel unit; gx0 / gy0 = grid top-left, from the latest measurement on the grid
    if (hov == grid && HIT(grid, cx, cy)) {
        int x0 = cx, x1 = cx, y0 = cy; while (x0 > 1 && HIT(grid, x0 - 2, cy)) x0 -= 2; while (x1 < vw - 2 && HIT(grid, x1 + 2, cy)) x1 += 2; while (y0 > 1 && HIT(grid, cx, y0 - 2)) y0 -= 2;
        k = (x1 - x0) / (double)(COLS * 98); gx0 = x0; gy0 = y0; double cell = 98 * k;
        if (cx < x0 + cell) { uint64_t ev = dm_padmode_page_keys(DM_BTN_LEFT); if (ev & DM_BTN_LEFT) { int tab = cy < y0 + TAB_STEP * k ? 0 : 1; dm_ui_hover_now(x0 - (int)((GRID_X - STRIP_X - TAB_W / 2) * k), y0 + (int)((tab * TAB_STEP + TAB_H / 2) * k)); } }
        return; }
    int tab = !strcmp(hn, "TouchBagTab0") ? 0 : !strcmp(hn, "TouchBagTab1") ? 1 : -1; if (tab < 0) return;
    uint64_t ev = dm_padmode_page_keys(DM_BTN_UP | DM_BTN_DOWN | DM_BTN_RIGHT | (held ? DM_BTN_A : 0));
    int ty = gy0 >= 0 ? gy0 : cy - (int)((tab * TAB_STEP + TAB_H / 2) * k);
    if ((ev & DM_BTN_UP) && tab == 1) dm_ui_hover_now(cx, ty + (int)((TAB_H / 2) * k));
    if ((ev & DM_BTN_DOWN) && tab == 0) dm_ui_hover_now(cx, ty + (int)((TAB_STEP + TAB_H / 2) * k));
    if (ev & DM_BTN_RIGHT) dm_ui_hover_now(cx + (int)((GRID_X + 49 - STRIP_X - TAB_W / 2) * k), cy);
    if ((ev & DM_BTN_A) && held) { static int lg; if (lg++ < 20) LOG("大背包：手上有物品（id %d），图标上的 A 不切页不放下", held); }
    (void)vh;
}

// 游戏主线程每帧（dm_pouch.c ui_pump）；box = 物品栏里我们的容器 TouchPouchBox，它的可见位不代表面板开着，面板开着看面板自己的 +0x50 / +0x51。
// Game main thread every frame (ui_pump in dm_pouch.c); box = our container TouchPouchBox in the inventory, whose visible bit does not mean the panel is open; that is the panel's own +0x50 / +0x51.
void dm_bag_ui(uint8_t *b, uint64_t inv_panel, uint64_t box, uint64_t pl) {
    B = b; uint64_t root = *(uint64_t *)(b + 0x2f05458), grid = find_w(inv_panel, "grid", 1); static int was_up; int open = up(inv_panel);
    if (open && !was_up) { LOG("大背包：物品栏打开，页 %d", g_page); }
    if (!open && was_up) set_page(0, "closed");
    was_up = open;
    dm_hud_info hi; dm_hud_get(&hi); int held = hi.valid ? hi.holding : 0;
    { int t = atomic_exchange(&g_tap, 0); if (t && !held) set_page(t - 1, "tap"); }
    if (grid) *(uint8_t *)(grid + 0x630) = g_page ? CHARM_PAGE : 0;
    // 计数：已用格 / 总格；新进护身符页且不在该页时亮红点 | Counts: cells used / total; the red dot lights when something new arrives on the charm page while it is not shown
    { static int last_c = -1, dot; int used[2] = { 0, 0 }, nch = 0; uint64_t inv = pl ? *(uint64_t *)(pl + 0x90) : 0;
      for (uint64_t it = inv && *(uint32_t *)inv == 0x01020304 ? *(uint64_t *)(inv + 0x10) : 0, n = 0; it && n < 2048; n++) { uint64_t d = *(uint64_t *)(it + 0x10); if (!d) break;
          if (*(uint32_t *)(it + 0xc) == 0) { int pg = *(uint8_t *)(d + 0x55), gw = 1, gh = 1; if (pg == 0 || pg == CHARM_PAGE) { dm_cursor_item_size(b, it, &gw, &gh); used[pg != 0] += gw * gh; nch += pg != 0; } }
          it = *(uint64_t *)(d + 0xb0); }
      if (last_c >= 0 && nch > last_c && !g_page) dot = 1; if (g_page) dot = 0; last_c = nch;
      char t[16]; for (int k = 0; k < 2; k++) { snprintf(t, sizeof t, "%d/%d", used[k], CAP); char nm[20]; snprintf(nm, sizeof nm, "TouchBagCount%d", k); w_text(find_w(box, nm, 1), t); }
      w_move(find_w(box, "TouchBagHi", 1), STRIP_X, GRID_Y + g_page * TAB_STEP);
      w_move(find_w(box, "TouchBagDot", 1), dot ? STRIP_X + TAB_W - 20 : OFF, GRID_Y + TAB_STEP - 6); }
    int pad = dm_padmode_current(), mine = open && pad && !tabs_theirs(root) && !dm_ui_modal_active(root);
    w_move(find_w(box, "TouchBagKeyL", 1), mine ? STRIP_X + TAB_W / 2 : OFF, GRID_Y - 106);
    w_move(find_w(box, "TouchBagKeyR", 1), mine ? STRIP_X + TAB_W / 2 : OFF, GRID_Y + 2 * TAB_STEP - 70);
    if (mine) { uint64_t ev = dm_padmode_page_keys(DM_BTN_ZL | DM_BTN_ZR); if (ev & DM_BTN_ZL) set_page(0, "ZL"); if (ev & DM_BTN_ZR) set_page(1, "ZR"); }
    if (open && pad && grid && !dm_ui_modal_active(root)) pad_nav(root, grid, held);
}

// 服务端每拍：页 0 的护身符搬进页 5（拾取、买入、交易收货、导入、老档都走这一条），一拍一件；页 5 放不下就留在原处。
// 存读（.swcharm）接上之前页 5 的东西存不进 .d2s，所以现在只在测试开关 DM_TEST_BAG_SWEEP 下跑。
// Server every tick: charms on page 0 move to page 5 (pickup, purchase, trade, import and old saves all take this path), one per tick; if page 5 has no room they stay.
// Until saving (.swcharm) lands, page 5 contents do not survive in the .d2s, so for now this only runs under the test switch DM_TEST_BAG_SWEEP.
void dm_bag_srv_tick(uint8_t *b, uint64_t G, uint64_t player) {
    static int on = -1; if (on < 0) on = getenv("DM_TEST_BAG_SWEEP") != NULL; if (!on || !player) return;
    uint64_t inv = *(uint64_t *)(player + 0x90), f = 0; if (!inv || *(uint32_t *)inv != 0x01020304) return;
    int (*is)(uint64_t, int) = (int (*)(uint64_t, int))(b + 0x574bc0); uint8_t used[ROWS][COLS]; memset(used, 0, sizeof used);
    for (uint64_t it = *(uint64_t *)(inv + 0x10), n = 0; it && n < 2048; n++) { uint64_t d = *(uint64_t *)(it + 0x10), pp = *(uint64_t *)(it + 0x38); if (!d) break;
        if (*(uint32_t *)(it + 0xc) == 0 && pp) { int pg = *(uint8_t *)(d + 0x55);
            if (pg == CHARM_PAGE) { int gw = 1, gh = 1, x = *(int32_t *)(pp + 0x10), y = *(int32_t *)(pp + 0x14); dm_cursor_item_size(b, it, &gw, &gh); for (int r = y; r < y + gh && r < ROWS; r++) for (int c = x; c < x + gw && c < COLS; c++) if (r >= 0 && c >= 0) used[r][c] = 1; }
            else if (pg == 0 && !f && is(it, 13)) f = it; }
        it = *(uint64_t *)(d + 0xb0); }
    if (!f) return;
    int gw = 1, gh = 1, fx = -1, fy = -1; dm_cursor_item_size(b, f, &gw, &gh);
    for (int c = 0; c + gw <= COLS && fx < 0; c++) for (int r = 0; r + gh <= ROWS && fx < 0; r++) { int ok = 1; for (int q = r; q < r + gh && ok; q++) for (int w = c; w < c + gw; w++) if (used[q][w]) { ok = 0; break; } if (ok) { fx = c; fy = r; } }
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
