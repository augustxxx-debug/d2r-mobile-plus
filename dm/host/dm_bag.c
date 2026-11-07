// 大背包物品栏：两枚页签（背包 / 护身符两页，同一块网格切换显示），用户 11-01 定稿方案 ④，坐在腰带行左侧。布局和底图见 dm/ios/tools/bag_layout.py、gen_bag_art.py。
// 切页：格子控件 grid 的页号 +0x630 每帧写（页 0 背包 / 页 5 护身符），原版按它画格子和物品、算命中。
// 手柄（探针见设计稿 §4.1；规则表与期望在 dm_bag_nav.h / tests/t_bag_nav.c，Mac 整机判据 ui_regress bag_nav_judge.py）：
//   页签是布局里的 ButtonWidget，原版光标悬停、按 A 发 onClickMessage（ClosePanel:TouchBagTab0/1），触屏点按走同一条。原版焦点导航把页签当普通停靠点，
//   页签正压在网格第 0–2 列上方，从背包按上先跳到页签（B235）。现在物品栏里装备格、武器 I / II 钮、页签、腰带、网格第 0 行的 ↑、底排钮的 ↑ 都由宿主按 dm_bag_nav.h 的表走：
//   页签不是几何候选（从背包按上直接进正上方的装备格 / 腰带）；页签与腰带同一行，腰带第 0 格 ← 进护身符页签、← 背包页签，→ 回腰带，↓ 回第 0 行；腰带两排 ↓ 都走得出去（B239）；
//   往下回网格、底排钮按上回「进来那一格」（原版的选中格，不等光标停稳，B189 ③）；物品栏开 / 关时记忆清空（B189 ①）。表里写「交原版」的方向（对面面板、顶栏、边界、腰带 ↑）不接管。
//   手上拿着物品时页签是死路（A 宿主吞掉，不切页不放下），腰带第 0 格 ← 不动。
//   切页：ZL / ZR（Xbox LT / RT）——对面面板的页签条要用（原版不看光标在哪，见设计稿实测表），所以只有对面没有页签时才归物品栏，键帽也按这个显隐；商店开着时 L / R 点按（B170）。
// Large-bag inventory: two tabs (bag and charms pages, shown in the same grid area), user-approved option 4 (11-01), on the left of the belt row. Layout and art: dm/ios/tools/bag_layout.py, gen_bag_art.py.
// Page switching: the grid widget's page +0x630 is written every frame (page 0 bag / page 5 charms); the original draws cells and items and hit-tests by it.
// Controller (probes in design doc §4.1; the rule table and expectations are dm_bag_nav.h / tests/t_bag_nav.c, the Mac whole-machine judge is ui_regress bag_nav_judge.py):
//   The tabs are ButtonWidgets in the layout: the original cursor hovers them and A sends onClickMessage (ClosePanel:TouchBagTab0/1), touch taps take the same path. The original focus
//   navigation treats the tabs as ordinary stops and they sit right over grid columns 0-2, so Up from the bag jumped to a tab first (B235). Now the equipment slots, weapon I / II buttons,
//   tabs, belt, Up on grid row 0 and Up on the bottom buttons follow the table in dm_bag_nav.h, driven by the host: tabs are no geometric candidates (Up from the bag goes straight to the
//   slot / belt above); tabs and belt share one row, Left on belt cell 0 enters the charm tab, then the bag tab, Right goes back to the belt, Down back to row 0; Down leaves both belt rows
//   (B239); going down back to the grid and Up on a bottom button return to the entry cell (the original's selected cell, not waiting for the cursor to settle, B189 item 3); the memory is
//   cleared when the inventory opens / closes (B189 item 1). Directions the table leaves to the original (opposite panel, top bar, borders, belt Up) are not taken.
//   With an item in hand a tab is a dead end (A is swallowed by the host: no page change, no drop) and Left on belt cell 0 does not move.
//   Pages: ZL / ZR (Xbox LT / RT) -- the opposite panel's tab bar needs them (the original ignores where the cursor is, measured table in the design doc), so they belong to the inventory only
//   when the opposite panel has no tabs, and the keycaps follow; with a shop open, L / R taps (B170).
#include "dm_bag_layout.h"
#include "dm_bag_rule.h"
#include "dm_widget.h"
#include "dm_hud.h"
#include "dm_input.h"
#include "dm_cursor.h"
#include "dm_actions.h"
#include "dm_cursor_item.h"
#include "dm_cursor_rescue.h"
#include "dm_exitwait.h"
#include "hle.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int dm_padmode_current(void); uint64_t dm_padmode_page_keys(uint64_t mask);
enum { OFF = 99999, COLS = DM_BAG_COLS, ROWS = DM_BAG_ROWS, CHARM_PAGE = 5, CAP = COLS * ROWS, VB = 1024, BELT_N = 4 };   // BELT_N：物品栏里腰带显示的格数（bag_layout.py「腰带四格」） | belt cells shown in the inventory
// ZL / ZR 归属：开着这个面板时归它（1）还是归物品栏（0）。方块、交易原版没有页签条，按规则归物品栏；方块已实测，交易未在真实交易中验证（待联机线真机）。
// ZL / ZR ownership: while this panel is open they belong to it (1) or to the inventory (0). The cube and trade have no tab bar in the original, so by rule they go to the inventory; trade has not been verified in a real trade (pending the multiplayer line on devices).
static const struct { const char *panel; int theirs; } k_tab_owner[] = {
    { "VendorPanelLayout", 1 }, { "BankExpansionLayout", 1 }, { "HoradricCubeLayout", 0 }, { "TradeLayout", 0 },
};
static uint8_t *B; static int g_page; static _Atomic int g_last_x = -1, g_last_y = -1;   // 「进来那一格」：最近一次从网格最下一行往下离开时那一格的中心（屏幕坐标，B189 ③；dm_pouch.c 两个底排按钮 ↑ 回格子用），没有 = -1 | the entry cell: centre of the cell last left downward from the grid's last row (screen, B189 item 3; used by the two bottom buttons in dm_pouch.c for Up), none = -1
int dm_bag_pad_last(int *x, int *y) { int a = atomic_load(&g_last_x), b = atomic_load(&g_last_y); if (a < 0) return 0; *x = a; *y = b; return 1; }
 static _Atomic int g_tap, g_restore_fail, g_full, g_only, g_rescue_ground;   // g_rescue_ground：救回落到脚边时提示一次（B205） | g_rescue_ground: one notice when a rescue drops to the ground (B205)
int dm_bag_take_full(void) { return atomic_exchange(&g_full, 0); } int dm_bag_take_restore_fail(void) { return atomic_exchange(&g_restore_fail, 0); }
uint64_t dm_bag_cli_item(uint8_t *b, uint32_t id) { return ((uint64_t (*)(uint64_t, uint32_t, uint32_t, uint32_t))(b + 0x45f5f0))((uint64_t)(uintptr_t)(b + 0x2d603a0 + 4 * 0x400), id & 0x7f, id, 4); }   // g_tap = 1 + 点到的图标 | g_tap = 1 + the tapped icon

// 进护身符页的是所有护身符，未鉴定的也进（D1，用户 11-05 确认，推翻 B146 的「只收已鉴定」）：未鉴定时不生效（原版背包页同样，属性判定另查鉴定位），
//   在页 5 上直接鉴定——免费鉴定（双击 / 按住 A，dm_cursor.c 认页 5）、原版鉴定卷轴 / 书的「对物品使用」、凯恩「鉴定全部」，鉴定后立即生效
// Every charm goes to the charm page, unidentified ones too (D1, confirmed by the user 11-05, overturning B146's identified-only rule): unidentified ones are inactive (as on the original bag page; the stat check looks at the identified bit separately),
//   and are identified right on page 5 -- free identify (double tap / hold A, dm_cursor.c accepts page 5), the original identify scroll / tome "use on item", and Cain's Identify All; they take effect at once
static int charm_paged(uint8_t *b, uint64_t it) { uint64_t d = it ? *(uint64_t *)(it + 0x10) : 0; return d && ((int (*)(uint64_t, int))(b + 0x574bc0))(it, 13); }
// 物品「生效」0x645820(…, 1) 不看鉴定位（实测：未鉴定护身符搬进页 5 当拍生命 +8 / 火抗 +3；原版背包页上同一件鉴定后才加），所以页 5 上只对已鉴定的开；
//   未鉴定的在页 5 上鉴定时由原版鉴定自己开（dm_bag_c2s 0x26 搬回背包页鉴定 / 免费鉴定 0x78acf0）
// Item "effects on" 0x645820(..., 1) ignores the identified bit (measured: an unidentified charm moved to page 5 gave +8 life / +3 fire resistance that tick; on the original bag page the same item only adds them once identified), so on page 5 it is only turned on for identified ones;
//   for unidentified ones identified on page 5 the original identify turns them on (0x26 in dm_bag_c2s identifies on the bag page / free identify 0x78acf0)
static void eff_on(uint8_t *b, uint64_t G, uint64_t it, uint64_t player) { uint64_t d = *(uint64_t *)(it + 0x10); if (d && (*(uint32_t *)(d + 0x18) & 0x10)) ((void (*)(int, uint64_t, uint64_t, int))(b + 0x645820))(*(uint8_t *)(G + 0x106), it, player, 1); }
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
#include "dm_bag_nav.h"
int dm_custom_get(const char *key);
// 导航记忆与网格量值（游戏主线程）：g_nav 见 dm_bag_nav.h；g_nav_prev = 上一帧焦点所在元素；g_gx0 / g_gy0 / g_k = 网格左上角（屏幕）与屏幕像素 / 面板单位
// Navigation memory and grid measurement (game main thread): g_nav see dm_bag_nav.h; g_nav_prev = the element focused last frame; g_gx0 / g_gy0 / g_k = grid top-left (screen) and screen pixels per panel unit
static dm_bag_nav_state g_nav = { 0, 1, 1, -1, -1 }; static int g_nav_prev = BN_ORIG, g_gx0 = -1, g_gy0 = -1; static double g_k; static int g_logic = -1;   // 网格逻辑格（dm_bag_nav_track） | the grid logical cell (dm_bag_nav_track)
static void nav_export(void) {   // 进来那一格 → dm_bag_pad_last（屏幕坐标）；只在焦点正是从那一格一步下来的钮上时给 | the entry cell -> dm_bag_pad_last (screen coordinates); only while the focus is on the button one step down from it
    if (g_nav.entry_bot < 0 || g_gx0 < 0 || dm_bag_nav_geo(&g_nav, dm_bag_nav_cell(g_nav.entry_bot, ROWS - 1), BN_DOWN) != g_nav_prev) { atomic_store(&g_last_x, -1); atomic_store(&g_last_y, -1); return; }
    atomic_store(&g_last_x, g_gx0 + (int)((g_nav.entry_bot * 98 + 49) * g_k)); atomic_store(&g_last_y, g_gy0 + (int)(((ROWS - 1) * 98 + 49) * g_k));
}
// B189 ①：物品栏开 / 关时清空记忆（换角色必经关面板） | B189 item 1: clear the memory when the inventory opens / closes (a character switch always closes it)
static void nav_reset(int open) { dm_bag_nav_panel(&g_nav, open); g_nav_prev = BN_ORIG; g_logic = -1; nav_export(); static int lg; if (lg++ < 50) LOG("手柄物品栏：记忆清空（物品栏%s）", open ? "打开" : "关上"); }
// 焦点所在元素：网格按逻辑格（g_logic，dm_bag_nav_track；没有就用光标所在格），腰带按光标所在排分最下一排 / 第二排，其余按控件名
// The focused element: the grid by the logical cell (g_logic, dm_bag_nav_track; the cursor's cell when there is none),
// the belt by the cursor's row (bottom / second row), the rest by widget name
static int nav_elem(uint64_t grid, uint64_t hov, const char *hn, int cx, int cy, double k) {
    if (hov == grid) {
        if (g_logic >= 0) return g_logic;
        if (g_gx0 < 0) return BN_ORIG;
        int c = (int)((cx - g_gx0) / (98 * k)), r = (int)((cy - g_gy0) / (98 * k));
        if (c < 0) c = 0; if (c > COLS - 1) c = COLS - 1; if (r < 0) r = 0; if (r > ROWS - 1) r = ROWS - 1;
        return dm_bag_nav_cell(c, r);
    }
    if (!strcmp(hn, "Belt")) {
        if (g_gx0 < 0) return BN_ORIG;
        int i = (int)((cx - g_gx0 - (DM_BAG_BELT_X - DM_BAG_GRID_X) * k) / (DM_BAG_BELT_CELL * k)); if (i < 0) i = 0; if (i > BN_BELT_N - 1) i = BN_BELT_N - 1;
        int bottom = cy > g_gy0 + (int)((DM_BAG_BELT_Y - DM_BAG_GRID_Y) * k) - (int)(7 * k);   // 最下一排（和页签同一行）；上面是展开后的几排 | the bottom row (on the tabs' row); above it the expanded rows
        return (bottom ? BN_BELT0 : BN_BELTU0) + i;
    }
    for (int e = BN_CELLS; e < BN_N; e++) if (!dm_bag_nav_is_belt(e) && !dm_bag_nav_is_beltu(e) && !strcmp(dm_bag_nav_rect_of(e).name, hn)) return e;
    return BN_ORIG;
}
// 哪些方向由宿主接管：网格只管第 0 行 ↑（内部、左右边、↓ 都是原版）；底排钮只管 ↑，「存放」与商店里的百宝囊图标归 dm_pouch.c（B170）；腰带第二排只管 ↓；其余元素四向都管（表里交原版的除外）
// Which directions the host takes: the grid only Up on row 0 (inside, side edges and Down are the original's); bottom buttons only Up, with Deposit and the pouch icon in a shop belonging to dm_pouch.c (B170);
// the belt's second row only Down; all four for the other elements (except where the table says original)
static int nav_owned(int e, int d, int shop) {
    if (e < BN_CELLS) return d == BN_UP && e / COLS == 0;
    if (dm_bag_nav_is_bottom(e)) return d == BN_UP && (e == BN_GOLD || (e == BN_POUCH && !shop));
    if (dm_bag_nav_is_beltu(e)) return d == BN_DOWN;
    return 1;
}
static void pad_nav(uint64_t root, uint64_t panel, uint64_t grid, int held, int shop) {
    uint64_t fm = *(uint64_t *)(root + 0xd0), hov = fm ? *(uint64_t *)(fm + 0x178) : 0; const char *hn = w_name(hov); if (!hn) { g_nav_prev = BN_ORIG; return; }
    int cx = *(int32_t *)(B + 0x2d128f8), cy = *(int32_t *)(B + 0x2d128fc);
    double k = *(float *)(panel + 0x80) * *(int32_t *)(B + 0x211da3c) / 2160.0;   // k = 屏幕像素 / 面板单位 | screen pixels per panel unit
    // 网格左上角：光标在网格上时用原版命中函数往左上量（物品栏开着时每次在网格上都重量） | grid top-left: measured up and left with the original hit test whenever the cursor is on the grid
    if (hov == grid && HIT(grid, cx, cy)) { int x0 = cx, y0 = cy; while (x0 > 1 && HIT(grid, x0 - 2, cy)) x0 -= 2; while (y0 > 1 && HIT(grid, cx, y0 - 2)) y0 -= 2; g_gx0 = x0; g_gy0 = y0; g_k = k; }
    g_nav.held = held != 0; g_nav.pouch = dm_custom_get("gem_pick") != 0; g_nav.store = dm_custom_get("store_btn") != 0;
    // 网格逻辑格：方向键按下沿推算，光标停稳 20 帧后以实测为准，刚进网格（宿主放的光标在格心）直接取实测 | grid logical cell: advanced on direction press edges,
    //   resynced to the measured cell once the cursor has rested 20 frames, taken as measured right after entering the grid (the host places the cursor at the cell centre)
    { uint64_t dm_padmode_raw_dirs(void); static uint64_t rprev, last_edge, still; static int px = -1, py = -1;
      uint64_t rb = dm_padmode_raw_dirs(), edge = rb & ~rprev, fr = dm_guest_frame(); rprev = rb; if (edge) last_edge = fr;
      if (cx != px || cy != py) still = fr; px = cx; py = cy;
      if (hov == grid && g_gx0 >= 0 && HIT(grid, cx, cy)) {
          int c = (int)((cx - g_gx0) / (98 * k)), r = (int)((cy - g_gy0) / (98 * k)); if (c > COLS - 1) c = COLS - 1; if (r > ROWS - 1) r = ROWS - 1;
          if (g_logic < 0 || g_nav_prev < 0 || g_nav_prev >= BN_CELLS || (fr - last_edge > 20 && fr - still > 3)) g_logic = dm_bag_nav_cell(c < 0 ? 0 : c, r < 0 ? 0 : r);
          static const uint64_t B4[4] = { DM_BTN_UP, DM_BTN_DOWN, DM_BTN_LEFT, DM_BTN_RIGHT };
          for (int d = 0; d < 4; d++) if (edge & B4[d]) g_logic = dm_bag_nav_track(g_logic, d);
      } else g_logic = -1; }
    int cur = nav_elem(grid, hov, hn, cx, cy, k);
    if (cur != g_nav_prev && g_gx0 >= 0) { int fx = dm_bag_nav_held_fix(&g_nav, g_nav_prev, cur);   // ③ 拿着物品被原版放到页签上：放回原处 | item 3: the original put the focus on a tab with an item in hand: put it back
        if (fx >= 0) { char a[16], b[16], c[16]; dm_bag_nav_rect r = dm_bag_nav_rect_of(fx); int x = g_gx0 + (int)((r.x + r.w / 2.0 - DM_BAG_GRID_X) * k), y = g_gy0 + (int)((r.y + r.h / 2.0 - DM_BAG_GRID_Y) * k);
            if (dm_ui_hover_now(x, y)) { LOG("手柄物品栏：拿着物品从 %s 落到了 %s（页签是死路）→ 放回 %s", dm_bag_nav_name(g_nav_prev, a, sizeof a), dm_bag_nav_name(cur, b, sizeof b), dm_bag_nav_name(fx, c, sizeof c)); cur = fx; } } }
    if (cur != g_nav_prev) { if (g_nav_prev >= 0 && cur >= 0) dm_bag_nav_moved(&g_nav, g_nav_prev, cur); g_nav_prev = cur; nav_export(); }
    // B222：手上拿着进不了腰带的物品（不是药水 / 卷轴）、光标在某个装备槽里、悬停的却是 Belt——腰带控件的命中范围连展开后的上几排也算，盖住鞋格 / 戒指位 / 腰带位
    //   （大背包方案把腰带挪到了页签行）。这时 A 交原版会往腰带里放、被拒；改由宿主直接调原版放下到那个槽（dm_held_equip，同触屏松手兜底 dm_cursor.c）。药水 / 卷轴照原版进腰带
    // B222: holding an item that cannot go into the belt (not a potion / scroll), the cursor inside an equipment slot, but the hovered widget is Belt -- the belt widget's hit range includes the rows above it when expanded and covers the
    //   boots / ring / belt slots (the large-bag layout moved the belt to the tab row). The original A would then try the belt and be refused; instead the host calls the original put-down into that slot (dm_held_equip, as the touch release fallback in dm_cursor.c). Potions / scrolls go into the belt as usual
    if (held && !strcmp(hn, "Belt")) {
        uint64_t it = dm_bag_cli_item(B, (uint32_t)held); int (*is)(uint64_t, int) = (int (*)(uint64_t, int))(B + 0x574bc0);
        if (it && !is(it, 9) && !is(it, 0x16)) { uint64_t ch = *(uint64_t *)(panel + 0x58), n = *(uint64_t *)(panel + 0x60), slot = 0;
            for (uint64_t i = 0; ch && i < n && n <= 256 && !slot; i++) { uint64_t c = *(uint64_t *)(ch + 8 * i); const char *nm = w_name(c); if (c && nm && !strncmp(nm, "slot_", 5) && *(uint8_t *)(c + 0x50) && HIT(c, cx, cy)) slot = c; }
            if (slot) { if (dm_padmode_page_keys(DM_BTN_A) & DM_BTN_A) { void dm_held_equip(int); int loc = *(int32_t *)(slot + 0x5d8); dm_held_equip(loc); LOG("大背包：手上 id %d 在 %s 上（悬停被腰带盖住）按 A → 直接放到栏位 %d（B222）", held, w_name(slot), loc); }
                return; } } }
    if (cur < 0 || g_gx0 < 0) return;   // 不是物品栏里的元素（对面面板 / 顶栏）或还没量过网格：交原版 | not an inventory element (opposite panel / top bar) or grid not measured yet: the original's
    // 手上有物品时页签上的 A 吞掉（不切页、不放下） | with an item in hand, A on a tab is swallowed (no page change, no drop)
    uint64_t a_mask = held && dm_bag_nav_is_tab(cur) ? DM_BTN_A : 0;
    static const uint64_t BTN[4] = { DM_BTN_UP, DM_BTN_DOWN, DM_BTN_LEFT, DM_BTN_RIGHT }; int to[4]; uint64_t want = a_mask;
    for (int d = 0; d < 4; d++) { to[d] = dm_bag_nav_step(&g_nav, cur, d); if (to[d] != BN_ORIG && nav_owned(cur, d, shop)) want |= BTN[d]; }
    if (!want) return;
    uint64_t ev = dm_padmode_page_keys(want);
    if (ev & a_mask) { static int lg; if (lg++ < 20) LOG("大背包：手上有物品（id %d），图标上的 A 不切页不放下", held); }
    for (int d = 0; d < 4; d++) if (ev & BTN[d] & want) {
        char a[16], b[16]; static int lg;
        if (to[d] == BN_STAY) { if (lg++ < 200) LOG("手柄物品栏：%s %c → 不动（手上有物品，页签是死路）", dm_bag_nav_name(cur, a, sizeof a), "UDLR"[d]); break; }
        dm_bag_nav_rect r = dm_bag_nav_rect_of(to[d]);
        int x = g_gx0 + (int)((r.x + r.w / 2.0 - DM_BAG_GRID_X) * k), y = g_gy0 + (int)((r.y + r.h / 2.0 - DM_BAG_GRID_Y) * k);
        int ok = dm_ui_hover_now(x, y);
        if (lg++ < 200) LOG("手柄物品栏：%s %c → %s（%d,%d）%s", dm_bag_nav_name(cur, a, sizeof a), "UDLR"[d], dm_bag_nav_name(to[d], b, sizeof b), x, y, ok ? "" : "【光标没就位】");
        break; }
}

void dm_bag_set_opp(int v); int dm_bag_opp_of(uint64_t root); int dm_bag_take_full(void); int dm_bag_take_restore_fail(void);
const char *dm_str(uint8_t *b, const char *key); void dm_game_notice_ui(uint8_t *main_base, const char *text); uint64_t dm_bag_cli_item(uint8_t *b, uint32_t id);
#ifdef DM_QA_BUILD
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
#else   // 玩家包不带脚本驱动入口（CLAUDE.md §2.9；安卓出包检查 libdm.so 不得有 DM_TEST_UI） | player packages carry no scripted driver entry (the Android packaging check rejects DM_TEST_UI in libdm.so)
static void ui_test(uint64_t panel, uint64_t grid) { (void)panel; (void)grid; }
#endif
// 游戏主线程每帧（dm_pouch.c ui_pump）；box = 物品栏里我们的容器 TouchPouchBox，它的可见位不代表面板开着，面板开着看面板自己的 +0x50 / +0x51。
// Game main thread every frame (ui_pump in dm_pouch.c); box = our container TouchPouchBox in the inventory, whose visible bit does not mean the panel is open; that is the panel's own +0x50 / +0x51.
void dm_bag_ui(uint8_t *b, uint64_t inv_panel, uint64_t box, uint64_t pl) {
    B = b; uint64_t root = *(uint64_t *)(b + 0x2f05458), grid = find_w(inv_panel, "grid", 1); static int was_up; int open = up(inv_panel);
    if (open && !was_up) { LOG("大背包：物品栏打开，页 %d", g_page); nav_reset(1); }
    if (!open && was_up) { set_page(0, "closed"); nav_reset(0); }
    was_up = open;
    dm_hud_info hi; dm_hud_get(&hi); int held = hi.valid ? hi.holding : 0;
    { static int lh; if (held != lh) { LOG("大背包：客户端手上 id %d → %d", lh, held); lh = held; } }
    { int t = atomic_exchange(&g_tap, 0); if (t && !held) set_page(t - 1, "tap"); }
    // 自动切页：手上是护身符 → 护身符页，手上是别的 → 背包页；对面面板里指着护身符也切（用户 15:5x）| Automatic page: a charm in hand goes to the charm page, anything else to the bag page; pointing at a charm in the opposite panel also switches (user rule)
    //   只在手上的东西换了（刚拿起 / 面板刚打开）那一下切，之后手柄 ZL / ZR 手动切页照用户的（D1 C8：拿着护身符 ZL 到背包页按 A 会被拒、留在手上）
    //   Only when what is in hand changes (just picked up / panel just opened); after that a manual ZL / ZR switch stays (D1 C8: ZL to the bag page with a charm in hand, then A, is refused and it stays in hand)
    static int held_sw; if (!open || !held) held_sw = 0;
    if (open) { int o = dm_bag_opp_of(root); dm_bag_set_opp(o);
        if (held && held != held_sw) { uint64_t it = dm_bag_cli_item(b, (uint32_t)held); if (it) { set_page(charm_paged(b, it) ? 1 : 0, "held"); held_sw = held; } }
        else if (o) { dm_cursor_item_info ci; if (dm_cursor_item(&ci) && ci.container != DM_ITEM_AT_INVENTORY && ci.container != DM_ITEM_AT_CHARM && !strncmp(ci.code, "cm", 2)) set_page(1, "hover"); } 
    }
    if (dm_bag_take_full()) dm_game_notice_ui(b, dm_str(b, "dmCharmFull"));
    if (atomic_exchange(&g_only, 0)) { dm_game_notice_ui(b, dm_str(b, "dmCharmOnlyPage")); set_page(1, "C8"); }   // 拒放进背包页后切回护身符页，手上那件只能放这里 | after refusing the bag page, back to the charm page, the only place the item in hand can go
    if (dm_bag_take_restore_fail()) dm_game_notice_ui(b, dm_str(b, "dmCharmRestore"));
    if (atomic_exchange(&g_rescue_ground, 0)) dm_game_notice_ui(b, dm_str(b, "dmRescueGround"));   // B205：救回时两页都满、物品落到脚边 | B205: both pages full during a rescue, the item landed at the feet
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
    // 护身符页的 A 键位条提示去掉了（B148）：页 5 上 A 和背包一样是原版拿起，原版键位条自己就对 | The charm-page A legend override is gone (B148): A on page 5 is the original pick-up like the bag, so the original legend is already right
    int mine = open && pad && !tabs_theirs(root) && !dm_ui_modal_active(root);
    // 手柄焦点框：光标停在页签上时显示（原版 StashTab_Hover）| Controller focus frame: shown while the cursor rests on a tab (the original StashTab_Hover)
    { uint64_t fm = open && pad ? *(uint64_t *)(root + 0xd0) : 0, hov = fm ? *(uint64_t *)(fm + 0x178) : 0; const char *hn = w_name(hov); int ft = hn && !strcmp(hn, "TouchBagTab0") ? 0 : hn && !strcmp(hn, "TouchBagTab1") ? 1 : -1;
      w_move(find_w(box, "TouchBagFocus", 1), ft >= 0 ? DM_BAG_TAB_X + ft * DM_BAG_TAB_DX - DM_BAG_FOCUS_PAD : OFF, DM_BAG_TAB_Y + (ft > 0 ? ft : 0) * DM_BAG_TAB_DY - DM_BAG_FOCUS_PAD); }
    w_move(find_w(box, "TouchBagKeyL", 1), mine ? DM_BAG_KEYL_X : OFF, DM_BAG_KEYL_Y);
    w_move(find_w(box, "TouchBagKeyR", 1), mine ? DM_BAG_KEYR_X : OFF, DM_BAG_KEYR_Y);
    if (mine) { uint64_t ev = dm_padmode_page_keys(DM_BTN_ZL | DM_BTN_ZR); int p;   // 切页键规则见 dm_bag_nav_page_key | page key rules: dm_bag_nav_page_key
        if ((ev & DM_BTN_ZL) && (p = dm_bag_nav_page_key(BN_KEY_ZL, 0, 0, held, 1)) >= 0) set_page(p, "ZL");
        if ((ev & DM_BTN_ZR) && (p = dm_bag_nav_page_key(BN_KEY_ZR, 0, 0, held, 1)) >= 0) set_page(p, "ZR"); }
    int shop = open && up(dm_ui_top(root, "VendorPanelLayout"));
    if (open && pad && grid && !dm_ui_modal_active(root)) pad_nav(root, inv_panel, grid, held, shop);
    // 商店开着时 L / R 点按切背包页（B170）：ZL / ZR 归商店页签条（k_tab_owner），L / R 原版在商店里点按无反应（实测 b170 09_L / 09_R），按住 R 是原版「按住排序」——
    // 所以只旁听不接管：按下到松开不到 0.3 秒算一次点按，L → 背包页、R → 护身符页；按住超过就是排序，不切。手上有物品时不切（同页签上的 A）
    // With a shop open, a tap of L / R switches the bag page (B170): ZL / ZR belong to the shop's tab bar, L / R taps do nothing in the shop (measured), and holding R is the original hold-to-sort --
    // so we only listen and take nothing: press to release under 0.3 s is a tap, L -> bag page, R -> charm page; a longer hold is sorting and switches nothing. Not with an item in hand
    { uint64_t dm_padmode_raw_buttons(void); static uint64_t prev, down_fr[2]; uint64_t bt = shop && pad && !dm_ui_modal_active(root) ? dm_padmode_raw_buttons() : 0, fr = dm_guest_frame();
      const uint64_t K[2] = { DM_BTN_L, DM_BTN_R };
      for (int i = 0; i < 2; i++) { if ((bt & K[i]) && !(prev & K[i])) down_fr[i] = fr;
          if (shop && !(bt & K[i]) && (prev & K[i]) && down_fr[i]) { int p = dm_bag_nav_page_key(i ? BN_KEY_R : BN_KEY_L, 1, 1, held, fr - down_fr[i] < 18); if (p >= 0) set_page(p, i ? "shop R" : "shop L"); }
          if (!(bt & K[i])) down_fr[i] = 0; }
      prev = bt; }
}

// ---- 护身符页的存读：<角色>/<角色>.swcharm（格式见交接文档「护身符页_格式变更」）
// 引擎读档时把 .d2s 里的页 5 丢掉（实测），所以侧文件是唯一权威。写：服务端拍把页 5 每件的位流记进 g_hist（含历史位置）；提交前钩子读本次提交的 .d2s，
// 只把字节确实在里面的写进侧文件——所以侧文件和 .d2s 永远是同一时刻，序列化之后才进页 5 的那件不会两边都有。放不回去的（g_stuck）留在文件里，下次再试。
// Saving the charm page: <character>/<character>.swcharm (format in the handoff doc).
// The engine discards page 5 of the .d2s on load (measured), so the side file is the only authority. Writing: the server tick records every page 5 item's bitstream in g_hist (including old positions); the pre-commit hook reads the .d2s of this commit
// and writes only the ones whose bytes are in it, so the side file and the .d2s are always the same instant and an item that entered page 5 after serialization is not in both. Items that could not be put back (g_stuck) stay in the file for the next try.
typedef struct { uint8_t x, y; uint16_t len; uint8_t *bits; } citem;
enum { HISTN = 1024 };
static citem g_hist[HISTN], g_stuck[CAP]; static int g_nhist, g_nstuck, g_loaded, g_cdirty, g_ncur, g_fresh, g_nread; static char g_char[64];   // g_fresh：读回后还没成功写过护身符页（B210）| g_fresh: no successful charm page write since the read-back (B210)
   // g_cdirty：页 5 现有的件还没全部写进护身符页（B203 退局等提交用）；g_ncur：最近一次快照时页 5 的件数 | g_cdirty: not every item now on page 5 has been written to the charm page yet (for the B203 exit-commit wait); g_ncur: page 5 item count at the latest snapshot
static uint64_t g_snap_last;   // 页 5 上次快照的签名；charm_load 清空 g_hist 时一起清零（B202）| signature of page 5's last snapshot; cleared with g_hist in charm_load (B202)
uint64_t dm_fs_commit_count(void);
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
    eff_on(b, G, nu, player);
    return 1;
}
static void charm_load(uint8_t *b, uint64_t G, uint64_t player) {
    for (int i = 0; i < g_nhist; i++) free(g_hist[i].bits); for (int i = 0; i < g_nstuck; i++) free(g_stuck[i].bits); g_nhist = g_nstuck = 0;
    g_snap_last = 0;   // B202：同一进程重进同一角色时物品 id 与格子都一样，签名不变；不清零就不重建 g_hist，下一次提交把护身符页写成 0 件 | re-entering the same character in one process keeps ids and cells, so the signature repeats; without this g_hist is never rebuilt and the next commit writes an empty charm page
    g_fresh = 0; g_nread = 0;   // 中途返回（老档 / 文件坏）也不留上一个角色的 B210 状态 | no B210 state from the previous character on the early returns (legacy save / bad file) either
    const char *wd = dm_save_workdir(); if (!wd || !g_char[0]) return; char p[1200]; path_of(p, sizeof p, wd, "swcharm"); FILE *f = fopen(p, "rb"); if (!f) { LOG("大背包：%s 还没有护身符页（老档），页 0 的护身符搬进来", g_char); return; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET); uint8_t *d = sz > 0 && sz < 4 * 1024 * 1024 ? malloc((size_t)sz) : NULL; int got = d && fread(d, 1, (size_t)sz, f) == (size_t)sz; fclose(f);
    if (!got || !charm_valid(d, (size_t)sz)) { char q[1200]; path_of(q, sizeof q, wd, "swcharm.bad"); int r = rename(p, q); LOG("【红】大背包：%s 损坏，改名留档 → %d；不当空页", p, r); free(d); atomic_store(&g_restore_fail, 1); return; }
    int cnt = d[8] | d[9] << 8, ok = 0; size_t o = 10;
    for (int i = 0; i < cnt; i++) { int x = d[o], y = d[o + 1], l = d[o + 2] | d[o + 3] << 8; const uint8_t *bits = d + o + 4; o += 4 + (size_t)l;
        if (charm_restore(b, G, player, x, y, bits, l)) ok++; else { uint8_t *c = malloc((size_t)l); if (c) { memcpy(c, bits, (size_t)l); g_stuck[g_nstuck++] = (citem){ (uint8_t)x, (uint8_t)y, (uint16_t)l, c }; } LOG("【红】大背包：页 5 格 %d,%d 放不回去，留在文件里", x, y); atomic_store(&g_restore_fail, 1); } }
    free(d); g_fresh = 1; g_nread = ok; if (ok) charm_notify_stats(b, G, player); LOG("大背包：%s 护身符页读回 %d / %d 件", g_char, ok, cnt);
}
// 签名含物品标志字（d + 0x18，鉴定位 0x10 在里面）：D1 起页 5 上能就地鉴定（免费鉴定 0x78acf0），id 和格子不变、位流变了；
//   不算标志时快照不更新，提交时旧位流在 d2s 里找不到，这件就没写进护身符页（矩阵 saveload 实测少 1 件）
// The signature includes the item flag word (d + 0x18, identified bit 0x10): since D1 an item can be identified in place on page 5 (free identify 0x78acf0), with id and cell unchanged but a new bitstream;
//   without the flags the snapshot did not refresh, the old bitstream was not found in the d2s at commit and the item was left out of the charm page (matrix saveload: one item short)
static void charm_snapshot(uint8_t *b, uint64_t player) {   // 页 5 变了才写位流 | Bitstreams are written only when page 5 changed
    uint64_t inv = *(uint64_t *)(player + 0x90), sig = 0; if (!inv || *(uint32_t *)inv != 0x01020304) return;
    for (uint64_t it = *(uint64_t *)(inv + 0x10), n = 0; it && n < 2048; n++) { uint64_t d = *(uint64_t *)(it + 0x10), pp = *(uint64_t *)(it + 0x38); if (!d) break;
        if (*(uint32_t *)(it + 0xc) == 0 && pp && *(uint8_t *)(d + 0x55) == CHARM_PAGE) sig = (sig ^ (*(uint32_t *)(it + 8) * 0x100000001b3ull + (uint64_t)*(int32_t *)(pp + 0x10) * 257 + (uint64_t)*(int32_t *)(pp + 0x14) + ((uint64_t)*(uint32_t *)(d + 0x18) << 32))) * 0x100000001b3ull + 1;
        it = *(uint64_t *)(d + 0xb0); }
    if (sig == g_snap_last) return; g_snap_last = sig; g_cdirty = 1; g_ncur = 0;
    for (uint64_t it = *(uint64_t *)(inv + 0x10), n = 0; it && n < 2048; n++) { uint64_t d = *(uint64_t *)(it + 0x10), pp = *(uint64_t *)(it + 0x38); if (!d) break;
        if (*(uint32_t *)(it + 0xc) == 0 && pp && *(uint8_t *)(d + 0x55) == CHARM_PAGE) { uint8_t buf[VB]; int len = dm_item_write(b, it, buf); g_ncur++; if (len > 0) hist_add(*(int32_t *)(pp + 0x10), *(int32_t *)(pp + 0x14), buf, len); else LOG("【红】大背包：页 5 物品 id %u 位流写不出", *(uint32_t *)(it + 8)); }
        it = *(uint64_t *)(d + 0xb0); }
}
static void charm_pre_commit(const char *wd) {   // 提交前钩子（写盘线程，持存档锁） | Pre-commit hook (disk-write thread, holding the save lock)
    pthread_mutex_lock(&g_cmu); if (!g_loaded || !g_char[0]) { pthread_mutex_unlock(&g_cmu); return; }
    char p[1200], t[1210]; path_of(p, sizeof p, wd, "d2s"); FILE *f = fopen(p, "rb"); uint8_t *s = NULL; long sn = 0; if (f) { fseek(f, 0, SEEK_END); sn = ftell(f); fseek(f, 0, SEEK_SET); s = sn > 0 ? malloc((size_t)sn) : NULL; if (s && fread(s, 1, (size_t)sn, f) != (size_t)sn) { free(s); s = NULL; } fclose(f); }
    if (!s) { LOG("大背包：本次提交没有 %s，护身符页不动", p); pthread_mutex_unlock(&g_cmu); return; }
    // B210：本次 d2s 里找到的页 5 件少于内存里页 5 现有的件（重进后头一次提交：d2s 还早于刚读回的件），就不改写护身符页、留着上一份文件（它有全部的件），
    //   只在读回后还没成功写过时（g_fresh），且页 5 件数没比读回时少（g_nread：没拿走过，旧文件不会留着已经拿走的件、免得复制）；最多连着跳 8 次（实测重进后连着缺件的提交 2 次），再少就照写并报红（防一件位流永远对不上时护身符页再也不更新）。这时被杀，下次启动读到的仍是全的
    // B210: if this d2s holds fewer page 5 items than page 5 has in memory (the first commit after re-entering: the d2s predates the items just read back), the charm page is not rewritten and the previous file (which has them all) stays;
    //   only while nothing has been written since the read-back (g_fresh) and page 5 holds no fewer items than were read back (g_nread: nothing taken out, so the old file cannot keep an item already removed and duplicate it); at most 8 skips in a row (measured: 2 short commits in a row after re-entering), after that it writes anyway and logs red (so one bitstream that never matches cannot freeze the charm page). Killed meanwhile, the next start still reads the full page
    { static int skips; int found = 0; for (int i = 0; i < g_nhist; i++) if (dm_memfind(s, (size_t)sn, g_hist[i].bits, g_hist[i].len)) found++;
      if (g_fresh && g_ncur >= g_nread && found < g_ncur && skips < 8) { skips++; LOG("大背包：本次存档里只找到页 5 %d / %d 件，护身符页先不改写（第 %d 次）", found, g_ncur, skips); free(s); pthread_mutex_unlock(&g_cmu); return; }
      if (g_fresh && g_ncur >= g_nread && found < g_ncur) LOG("【红】大背包：连续 %d 次存档里页 5 只找到 %d / %d 件，照写", skips, found, g_ncur); skips = 0; }
    path_of(p, sizeof p, wd, "swcharm"); snprintf(t, sizeof t, "%s.tmp", p); f = fopen(t, "wb");
    if (f) { uint8_t h[10] = { 'S', 'W', 'C', 'H', 1 }; int n = g_nstuck; for (int i = 0; i < g_nhist; i++) if (dm_memfind(s, (size_t)sn, g_hist[i].bits, g_hist[i].len)) n++; if (n > CAP) { LOG("【红】大背包：页 5 共 %d 件超过 %d", n, CAP); n = CAP; } h[8] = (uint8_t)n; h[9] = (uint8_t)(n >> 8); int ok = fwrite(h, 1, 10, f) == 10, w = 0;
        for (int pass = 0; pass < 2 && ok; pass++) { int m = pass ? g_nhist : g_nstuck; citem *v = pass ? g_hist : g_stuck; for (int i = 0; i < m && w < n && ok; i++) { if (pass && !dm_memfind(s, (size_t)sn, v[i].bits, v[i].len)) continue; uint8_t e[4] = { v[i].x, v[i].y, (uint8_t)v[i].len, (uint8_t)(v[i].len >> 8) }; ok = fwrite(e, 1, 4, f) == 4 && fwrite(v[i].bits, 1, v[i].len, f) == v[i].len; w++; } }
        ok = fclose(f) == 0 && ok; if (!ok || rename(t, p)) { remove(t); LOG("【红】大背包：写 %s 失败", p); } else { g_fresh = 0; if (n - g_nstuck >= g_ncur) g_cdirty = 0;   // 页 5 现有的件都在这次的 d2s 里才算证明了；退局前那次普通存盘可能早于新放进页 5 的件（实测 B203 延迟局） | proven only when every item now on page 5 is in this d2s; an ordinary save just before leaving may predate items newly put on page 5 (measured in the B203 delay runs)
            static int lastn = -1; if (n != lastn) { lastn = n; LOG("大背包：护身符页写盘 %d 件", n); } } }
    free(s); pthread_mutex_unlock(&g_cmu);
}
__attribute__((constructor)) static void charm_hook_install(void) { extern void (*g_dm_pre_commit_hook[6])(const char *); g_dm_pre_commit_hook[5] = charm_pre_commit; }

// 服务端每拍：进局读回；页 0 的护身符搬进页 5（拾取、买入、交易收货、导入、老档都走这一条），一拍一件；页 5 放不下就留在原处。
// Server every tick: load on entry; charms on page 0 move to page 5 (pickup, purchase, trade, import and old saves all take this path), one per tick; if page 5 has no room they stay.
static int quick_out(uint8_t *b, uint64_t G, uint64_t player, uint32_t id, int np, int nrow, int loud);
static int place_held(uint8_t *b, uint64_t G, uint64_t player, uint32_t id, int x, int y);
// 页 5 上鉴定（dm_bag_c2s 0x26）为了让原版鉴定先搬到背包页的那件：记下页 5 原格，鉴定完（或等满 g_ret_wait 拍）搬回原格（原格被占就照常找空位）。服务端线程读写
// The item moved to the bag page so the original identify accepts it (dm_bag_c2s 0x26): its page 5 cell is kept, and once identified (or after g_ret_wait ticks) it goes back there (a free cell as usual if taken). Server thread only
static uint32_t g_ret_id, g_gp_id; static int g_ret_x, g_ret_y, g_ret_wait, g_gp_ttl;   // g_gp_*：背包页满时改捡到手上的那件地上护身符（dm_bag_c2s 0x16） | g_gp_*: the ground charm taken into the hand because the bag page was full (0x16 in dm_bag_c2s)
static int page_free(uint64_t inv, int page, int gw, int gh, int *fx, int *fy);
// 临时腾格（B190 / B191）：背包页一件不比 cw×ch 小、页 5 放得下的非护身符 Y 挪进页 5 的格表（不通知客户端，用完放回原格，客户端始终看它在原处）
// Temporary room (B190 / B191): a non-charm Y on the bag page, no smaller than cw x ch and fitting on page 5, moves into page 5's grid (the client is not told; it goes back to its cell afterwards, so the client always sees it in place)
typedef struct { uint64_t y; int yx, yy, x5, y5; } troom;
static int room_take(uint8_t *b, uint64_t inv, int cw, int ch, int row, troom *t) {
    t->y = 0; int ya = 1 << 30;
    for (uint64_t it = *(uint64_t *)(inv + 0x10), n = 0; it && n < 2048; n++) { uint64_t d = *(uint64_t *)(it + 0x10), pp = *(uint64_t *)(it + 0x38); if (!d) break;
        if (pp && *(uint8_t *)(d + 0x55) == 0 && *(uint32_t *)(it + 0xc) == 0 && !charm_paged(b, it)) { int w = 1, h = 1, x5, y5; dm_cursor_item_size(b, it, &w, &h);
            if (w >= cw && h >= ch && w * h < ya && page_free(inv, CHARM_PAGE, w, h, &x5, &y5)) { t->y = it; ya = w * h; t->yx = *(int32_t *)(pp + 0x10); t->yy = *(int32_t *)(pp + 0x14); t->x5 = x5; t->y5 = y5; } }
        it = *(uint64_t *)(d + 0xb0); }
    if (!t->y || !((int (*)(uint64_t, uint64_t, int, int, int, int))(b + 0x560740))(inv, t->y, t->x5, t->y5, CHARM_PAGE + 2, row)) { t->y = 0; return 0; }
    *(uint8_t *)(*(uint64_t *)(t->y + 0x10) + 0x55) = CHARM_PAGE; return 1;
}
static void room_give(uint8_t *b, uint64_t G, uint64_t player, uint64_t inv, int row, troom *t) {
    if (!t->y) return; uint64_t yd = *(uint64_t *)(t->y + 0x10);
    if (((int (*)(uint64_t, uint64_t, int, int, int, int))(b + 0x560740))(inv, t->y, t->yx, t->yy, 2, row)) { *(uint8_t *)(yd + 0x55) = 0; t->y = 0; return; }
    void (*nt)(uint64_t, uint64_t, uint64_t, int, int, int, int, int, uint64_t) = (void (*)(uint64_t, uint64_t, uint64_t, int, int, int, int, int, uint64_t))(b + 0x642f70);   // 放不回原格：留在页 5，照实通知客户端 | cannot go back: it stays on page 5 and the client is told
    nt(G, player, t->y, 0x20, 4, 0, 0, t->yy, t->yx); nt(G, player, t->y, 1, 2, 0, CHARM_PAGE, 0, (uint64_t)(uint32_t)t->x5 | (uint64_t)t->y5 << 16);
    LOG("【红】大背包：腾格的物品 id %u 放不回背包页 %d,%d，留在页 5", *(uint32_t *)(t->y + 8), t->yx, t->yy); t->y = 0;
}
// B190 背包页满、页 5 有位时地上捡起护身符：原版只往背包页自动放（满了就拒；把包里标志清零让它上手也不行，实测客户端上手、服务端没上手又回到地上）。
//   做法：同一拍里把背包页一件不比护身符小、页 5 放得下的非护身符 Y 临时挪到页 5 的格表（不通知客户端），腾出格子，再把原包交给原版分派 0x6e5b40；
//   原版捡进背包页后立即把护身符搬进页 5（照常通知），Y 放回原格。离得远时原版先走过去（不捡），dm_bag_srv_tick 每 8 拍重试到捡起或 ttl 到期
// B190 ground pick-up of a charm with the bag page full and room on page 5: the original only auto-places into the bag page (refused when full; clearing the packet flag to take it into the hand does not work either -- measured: the client shows it in hand, the server does not, and it falls back to the ground).
//   So within the same tick a non-charm Y on the bag page, no smaller than the charm and fitting on page 5, moves temporarily into page 5's grid (the client is not told), freeing cells, and the original packet goes to the original dispatcher 0x6e5b40;
//   once the original has put the charm on the bag page it moves straight to page 5 (with the usual notices) and Y goes back to its cell. When far away the original walks there first (no pick-up), and dm_bag_srv_tick retries every 8 ticks until picked up or the ttl runs out
// 服务端物品单位表（同原版 0x6d5f48：G + 0x2e30 按 id 低 7 位分桶，链在 +0x158） | Server item unit table (as in the original 0x6d5f48: G + 0x2e30 bucketed by the low 7 bits of the id, chained at +0x158)
static uint64_t srv_item(uint64_t G, uint32_t id) { for (uint64_t u = *(uint64_t *)(G + 0x2e30 + 8 * (uint64_t)(id & 0x7f)); u; u = *(uint64_t *)(u + 0x158)) if (*(uint32_t *)(u + 8) == id) return u; return 0; }
// QA 测试开关（C9，矩阵执行器调）：开着时拾取不走护身符页过滤、背包页的护身符不自动搬进页 5，用来造「旧档迁移后留在背包里的护身符」；正式包恒为 0
// QA test switch (C9, called by the matrix executor): while on, pick-ups skip the charm page filter and bag page charms are not moved to page 5, to build "charms left in the bag after legacy migration"; always 0 in release builds
static _Atomic int g_legacy;
#ifdef DM_QA_BUILD
void dm_bag_test_legacy(int on) { atomic_store(&g_legacy, on ? 1 : 0); LOG("大背包：QA 旧档模拟 %s", on ? "开" : "关"); }
#endif
static int g_gp_in; static uint8_t g_gp_pk[17];
static uint64_t inv_find(uint64_t inv, uint32_t id) { for (uint64_t it = *(uint64_t *)(inv + 0x10), n = 0; it && n < 2048; n++) { uint64_t d = *(uint64_t *)(it + 0x10); if (!d) break; if (*(uint32_t *)(it + 8) == id) return it; it = *(uint64_t *)(d + 0xb0); } return 0; }
static int gp_pick(uint8_t *b, uint64_t G, uint64_t player, const uint8_t *pk, int in_dispatch) {
    uint32_t iid; memcpy(&iid, pk + 1, 4); uint64_t inv = *(uint64_t *)(player + 0x90), c = srv_item(G, iid); if (!inv || !c || inv_find(inv, iid)) return 0;
    int cw = 1, ch = 1, row = *(int32_t *)(b + 0x1dae684 + *(uint32_t *)(player + 4) * 8 + 4), fx, fy; dm_cursor_item_size(b, c, &cw, &ch);
    troom t; if (!room_take(b, inv, cw, ch, row, &t)) return 0; uint32_t yid = *(uint32_t *)(t.y + 8);
    int room = page_free(inv, CHARM_PAGE, cw, ch, &fx, &fy), got = 0;
    if (room) { static uint8_t q[17]; memcpy(q, pk, 17); g_gp_in = 1; if (!in_dispatch) *(uint32_t *)(G + 0x19050) = 1;
        ((void (*)(uint64_t, uint64_t, uint8_t *, uint32_t))(b + 0x6e5b40))(G, player, q, 17);
        if (!in_dispatch) *(uint32_t *)(G + 0x19050) = 0; g_gp_in = 0; }
    uint64_t f = room ? inv_find(inv, iid) : 0, fd = f ? *(uint64_t *)(f + 0x10) : 0, fp = f ? *(uint64_t *)(f + 0x38) : 0;
    if (fd && fp && *(uint8_t *)(fd + 0x55) == 0 && page_free(inv, CHARM_PAGE, cw, ch, &fx, &fy)) {
        int ox = *(int32_t *)(fp + 0x10), oy = *(int32_t *)(fp + 0x14);
        void (*nt)(uint64_t, uint64_t, uint64_t, int, int, int, int, int, uint64_t) = (void (*)(uint64_t, uint64_t, uint64_t, int, int, int, int, int, uint64_t))(b + 0x642f70);
        ((void (*)(int, uint64_t, uint64_t, int))(b + 0x645820))(*(uint8_t *)(G + 0x106), f, player, 0);
        if (((int (*)(uint64_t, uint64_t, int, int, int, int))(b + 0x560740))(inv, f, fx, fy, CHARM_PAGE + 2, row)) { *(uint8_t *)(fd + 0x55) = CHARM_PAGE; *(uint32_t *)(f + 0xc) = 0; nt(G, player, f, 0x20, 4, 0, 0, oy, ox); nt(G, player, f, 1, 2, 0, CHARM_PAGE, 0, (uint64_t)(uint32_t)fx | (uint64_t)fy << 16); got = 1; }
        eff_on(b, G, f, player); charm_notify_stats(b, G, player); }
    room_give(b, G, player, inv, row, &t);
    LOG("大背包：背包页满，地上护身符 id %u 腾格（id %u）后交原版捡起 → %s", iid, yid, got ? "进页 5" : f ? "进了背包页（页 5 没位）" : "没捡起（离得远，原版先走过去）");
    return got || f;
}
void dm_bag_srv_tick(uint8_t *b, uint64_t G, uint64_t player) {
    static uint64_t lp; static dm_exitwait ew; if (!player) { dm_exitwait_leave(&ew, lp != 0, dm_fs_commit_count()); lp = 0; return; }   // 退局那次提交可能晚于这一拍，清单留着，下一个角色进局才换 | The commit on leaving may come after this tick, so the state stays until the next character loads
    // B203（同 dm_exitwait.h）：游戏不等退局那次提交就回前端，一键重开约 1.2 秒后重进；页 5 有没写盘的改动时，先等那次提交再读护身符页，
    //   否则 charm_load 清掉清单、读回旧文件，晚到的退局提交再按新清单写盘，退局前放进页 5 的那件就丢了（实测：提交延迟 1.5 / 4 秒时少 2 件）。这一拍连快照也不做（新玩家的页 5 还是空的）
    // B203 (as dm_exitwait.h): the game returns to the front end without waiting for the exit commit and the one-tap restart re-enters about 1.2 s later; while page 5 has unwritten changes the charm page load waits for that commit,
    //   otherwise charm_load clears the list and reads the old file, the late exit commit writes from the new list, and the item put on page 5 before leaving is lost (measured: 2 items short with 1.5 / 4 s commit delays). No snapshot on such a tick either (the new player's page 5 is still empty)
    if (player != lp) { int to = 0; pthread_mutex_lock(&g_cmu); int pend = g_loaded && g_cdirty; pthread_mutex_unlock(&g_cmu);
        // 不看提交次数：退局存盘是连着几次提交，头一次还不含刚放进页 5 的件（实测：自然时序下头一次写 2 件、下一次才写 4 件），只等 g_cdirty 被证明清掉
        // The commit count is not used: the exit save is several commits in a row and the first does not yet contain items just put on page 5 (measured: naturally the first writes 2, the next 4), so wait until g_cdirty is proven clear
        if (dm_exitwait_defer(&ew, pend, ew.cc, &to)) return; if (to) LOG("【红】大背包：退局那次提交 %d 拍还没把页 5 写全，照读护身符页（退局前的改动可能丢）", DM_EXITWAIT_MAX); }
    pthread_mutex_lock(&g_cmu);
    if (player != lp) { lp = player; g_ret_id = g_gp_id = 0; uint64_t pd = *(uint64_t *)(player + 0x10); strlcpy(g_char, pd ? (const char *)pd : "", sizeof g_char); charm_load(b, G, player); g_loaded = 1; }
    charm_snapshot(b, player); pthread_mutex_unlock(&g_cmu);
    uint64_t inv = *(uint64_t *)(player + 0x90), f = 0; if (!inv || *(uint32_t *)inv != 0x01020304) return;
    { static uint32_t ls; uint64_t i0 = *(uint64_t *)(player + 0x90), c0 = i0 ? *(uint64_t *)(i0 + 0x40) : 0; uint32_t cid = c0 ? *(uint32_t *)(c0 + 8) : 0; if (cid != ls) { LOG("大背包：服务端手上 id %u → %u", ls, cid); ls = cid; } }
    if (g_gp_id && !*(uint64_t *)(inv + 0x40)) {   // B190 离得远：走到跟前后重试（gp_pick） | B190 far away: retry once walked up (gp_pick)
        if (inv_find(inv, g_gp_id) || !srv_item(G, g_gp_id) || --g_gp_ttl <= 0) g_gp_id = 0; else if (g_gp_ttl % 8 == 0 && gp_pick(b, G, player, g_gp_pk, 0)) g_gp_id = 0; }
    if (atomic_load(&g_legacy)) return;   // QA：模拟旧档迁移后留在背包页的护身符（C9），背包页的护身符不自动搬进页 5 | QA: simulate charms left on the bag page after legacy migration (C9); bag page charms are not moved to page 5
    uint8_t used[ROWS][COLS]; memset(used, 0, sizeof used); int nf = 0; static int last_nf = -1;
    for (uint64_t it = *(uint64_t *)(inv + 0x10), n = 0; it && n < 2048; n++) { uint64_t d = *(uint64_t *)(it + 0x10), pp = *(uint64_t *)(it + 0x38); if (!d) break;
        if (*(uint32_t *)(it + 0xc) == 0 && pp) { int pg = *(uint8_t *)(d + 0x55);
            if (pg == CHARM_PAGE) { int gw = 1, gh = 1, x = *(int32_t *)(pp + 0x10), y = *(int32_t *)(pp + 0x14); dm_cursor_item_size(b, it, &gw, &gh); for (int r = y; r < y + gh && r < ROWS; r++) for (int c = x; c < x + gw && c < COLS; c++) if (r >= 0 && c >= 0) used[r][c] = 1; }
            else if (pg == 0 && charm_paged(b, it)) { nf++; if (!f || *(uint32_t *)(it + 8) == g_ret_id) f = it; } }
        it = *(uint64_t *)(d + 0xb0); }
    if (!f) { last_nf = 0; return; }
    int ret = g_ret_id && *(uint32_t *)(f + 8) == g_ret_id;
    if (ret && !(*(uint32_t *)(*(uint64_t *)(f + 0x10) + 0x18) & 0x10) && g_ret_wait > 0) { g_ret_wait--; return; }   // 还没鉴定：等原版鉴定落定再搬回 | not identified yet: wait for the original identify to settle before moving it back
    int gw = 1, gh = 1, fx = -1, fy = -1; dm_cursor_item_size(b, f, &gw, &gh);
    if (ret) { int ok = g_ret_x >= 0 && g_ret_y >= 0 && g_ret_x + gw <= COLS && g_ret_y + gh <= ROWS; for (int q = g_ret_y; q < g_ret_y + gh && ok; q++) for (int w = g_ret_x; w < g_ret_x + gw; w++) if (used[q][w]) { ok = 0; break; } if (ok) { fx = g_ret_x; fy = g_ret_y; } g_ret_id = 0; }
    for (int c = 0; c + gw <= COLS && fx < 0; c++) for (int r = 0; r + gh <= ROWS && fx < 0; r++) { int ok = 1; for (int q = r; q < r + gh && ok; q++) for (int w = c; w < c + gw; w++) if (used[q][w]) { ok = 0; break; } if (ok) { fx = c; fy = r; } }
    if (dmbag_full_notice(&last_nf, nf, fx < 0)) { atomic_store(&g_full, 1); LOG("大背包：护身符页满了，背包页有 %d 件护身符等着搬", nf); }
    if (fx < 0) return;
    uint64_t d = *(uint64_t *)(f + 0x10), pp = *(uint64_t *)(f + 0x38); int ox = *(int32_t *)(pp + 0x10), oy = *(int32_t *)(pp + 0x14); uint32_t cls = *(uint32_t *)(player + 4); int row = *(int32_t *)(b + 0x1dae684 + cls * 8 + 4);
    void (*eff)(int, uint64_t, uint64_t, int) = (void (*)(int, uint64_t, uint64_t, int))(b + 0x645820); void (*nt)(uint64_t, uint64_t, uint64_t, int, int, int, int, int, uint64_t) = (void (*)(uint64_t, uint64_t, uint64_t, int, int, int, int, int, uint64_t))(b + 0x642f70);
    eff(*(uint8_t *)(G + 0x106), f, player, 0);
    int ok = ((int (*)(uint64_t, uint64_t, int, int, int, int))(b + 0x560740))(inv, f, fx, fy, CHARM_PAGE + 2, row);
    if (ok) { *(uint8_t *)(d + 0x55) = CHARM_PAGE; *(uint32_t *)(f + 0xc) = 0; nt(G, player, f, 0x20, 4, 0, 0, oy, ox); nt(G, player, f, 1, 2, 0, CHARM_PAGE, 0, (uint64_t)(uint32_t)fx | (uint64_t)fy << 16); eff_on(b, G, f, player); }
    else eff_on(b, G, f, player);
    ((void (*)(uint64_t, uint64_t, int))(b + 0x64b220))(G, player, 0); ((void (*)(uint64_t, int))(b + 0x5cadb0))(player, 1);
    LOG("大背包：护身符 id %u 页 0 %d,%d → 页 5 %d,%d：%d", *(uint32_t *)(f + 8), ox, oy, fx, fy, ok);
}

// ---- 护身符页的包过滤（dm_bag_c2s）：客户端发的拿起 0x19 / 放下 0x18 里页号是 5 的、地上捡起 0x16 / 买入 0x32 / 放进背包页 0x18 里是护身符的，在这里接管：
//   拿起 = 经背包页上手（B148），背包页满时临时腾格（B191）；放下 = 手上的护身符（鉴定与否都收，D1）落进页 5（点的格放不下就找空位），手上不是护身符不接；
//   地上捡起 / 买入：页 5 放不下就不收（C5）；放进背包页：护身符不接（C8）
// ---- Charm page packet filter (dm_bag_c2s): pick-up 0x19 / place 0x18 packets with page 5, and ground pick-up 0x16 / purchase 0x32 / place-on-bag-page 0x18 packets for charms, are taken over here:
//   pick up = into the hand via the bag page (B148), with a temporarily freed cell when the bag page is full (B191); place = the charm in hand (identified or not, D1) lands on page 5 (a free cell if the chosen one does not fit), a non-charm is refused;
//   ground pick-up / purchase: not taken if page 5 has no room (C5); placing on the bag page: charms are refused (C8)
enum { OPP_NONE, OPP_STASH, OPP_CUBE, OPP_VENDOR, OPP_TRADE };
static _Atomic int g_opp;
void dm_bag_set_opp(int v) { atomic_store(&g_opp, v); }
int dm_bag_opp_of(uint64_t root) { return up(dm_ui_top(root, "BankExpansionLayout")) ? OPP_STASH : up(dm_ui_top(root, "HoradricCubeLayout")) ? OPP_CUBE : up(dm_ui_top(root, "VendorPanelLayout")) ? OPP_VENDOR : up(dm_ui_top(root, "TradeLayout")) ? OPP_TRADE : OPP_NONE; }
static int page_free(uint64_t inv, int page, int gw, int gh, int *fx, int *fy) {   // 页的格表见 dm_stack.c free_cell | Page grid table: see free_cell in dm_stack.c
    // 空页的格表是放第一件时才建的（0x560740 自己建），没有格表就是全空 | An empty page's grid is only created when the first item goes in (0x560740 builds it), so no grid means all free
    uint64_t pg = *(uint64_t *)(inv + 0x20), e = pg && (uint64_t)page + 2 < *(uint64_t *)(inv + 0x28) ? pg + (uint64_t)(page + 2) * 0x20 : 0, cells = e ? *(uint64_t *)(e + 0x18) : 0; if (!cells) { *fx = 0; *fy = 0; return 1; }
    int w = *(uint8_t *)(e + 0x10), h = *(uint8_t *)(e + 0x11);
    for (int x = 0; x + gw <= w; x++) for (int y = 0; y + gh <= h; y++) { int ok = 1; for (int q = y; q < y + gh && ok; q++) for (int c = x; c < x + gw; c++) if (*(uint64_t *)(cells + 8 * (uint64_t)(q * w + c))) { ok = 0; break; } if (ok) { *fx = x; *fy = y; return 1; } }
    return 0;
}
static int g_qx, g_qy;   // quick_out 最后一次搬到的格 | Cell of quick_out's last move
static int quick_out(uint8_t *b, uint64_t G, uint64_t player, uint32_t id, int np, int nrow, int loud) {
    uint64_t inv = *(uint64_t *)(player + 0x90), f = 0; for (uint64_t it = *(uint64_t *)(inv + 0x10), n = 0; it && n < 2048 && !f; n++) { uint64_t d = *(uint64_t *)(it + 0x10); if (!d) break; if (*(uint32_t *)(it + 8) == id) f = it; else it = *(uint64_t *)(d + 0xb0); }
    if (!f) return 0; uint64_t d = *(uint64_t *)(f + 0x10), pp = *(uint64_t *)(f + 0x38); if (!pp || *(uint8_t *)(d + 0x55) != CHARM_PAGE) return 0;
    int gw = 1, gh = 1, x, y, ox = *(int32_t *)(pp + 0x10), oy = *(int32_t *)(pp + 0x14); dm_cursor_item_size(b, f, &gw, &gh); if (!page_free(inv, np, gw, gh, &x, &y)) { if (loud) LOG("大背包：页 %d 放不下 id %u", np, id); return 0; }
    void (*eff)(int, uint64_t, uint64_t, int) = (void (*)(int, uint64_t, uint64_t, int))(b + 0x645820); void (*nt)(uint64_t, uint64_t, uint64_t, int, int, int, int, int, uint64_t) = (void (*)(uint64_t, uint64_t, uint64_t, int, int, int, int, int, uint64_t))(b + 0x642f70);
    if (np) eff(*(uint8_t *)(G + 0x106), f, player, 0);   // 页 0 的护身符原版也生效，转去卖的那一下不动 | A charm on page 0 is live in the original too, so the hop before selling leaves it on
    if (!((int (*)(uint64_t, uint64_t, int, int, int, int))(b + 0x560740))(inv, f, x, y, np + 2, nrow)) { if (np) eff_on(b, G, f, player); LOG("大背包：放进页 %d %d,%d 失败 id %u", np, x, y, id); return 0; }
    *(uint8_t *)(d + 0x55) = (uint8_t)np; *(uint32_t *)(f + 0xc) = 0; nt(G, player, f, 0x20, 4, 0, CHARM_PAGE, oy, ox); nt(G, player, f, 1, 2, 0, np, 0, (uint64_t)(uint32_t)x | (uint64_t)y << 16); charm_notify_stats(b, G, player);
    g_qx = x; g_qy = y; LOG("大背包：护身符 id %u 页 5 %d,%d → 页 %d %d,%d", id, ox, oy, np, x, y); return 1;
}
static int place_held(uint8_t *b, uint64_t G, uint64_t player, uint32_t id, int x, int y) {
    uint64_t inv = *(uint64_t *)(player + 0x90), it = *(uint64_t *)(inv + 0x40); if (!it || *(uint32_t *)(it + 8) != id || !charm_paged(b, it)) return 0;   // 不是护身符不进页 5，界面已按 charm_paged 切回背包页 | Non-charms do not go to page 5; the UI already switched back to the bag page by charm_paged
    uint64_t d = *(uint64_t *)(it + 0x10); uint32_t cls = *(uint32_t *)(player + 4); int row = *(int32_t *)(b + 0x1dae684 + cls * 8 + 4), gw = 1, gh = 1, fx = x, fy = y; dm_cursor_item_size(b, it, &gw, &gh);
    if (!page_free(inv, CHARM_PAGE, gw, gh, &fx, &fy)) return 0;
    int (*put)(uint64_t, uint64_t, int, int, int, int) = (int (*)(uint64_t, uint64_t, int, int, int, int))(b + 0x560740);
    if (!put(inv, it, x, y, CHARM_PAGE + 2, row)) { if (!put(inv, it, fx, fy, CHARM_PAGE + 2, row)) return 0; x = fx; y = fy; }
    *(uint8_t *)(d + 0x55) = CHARM_PAGE; *(uint32_t *)(it + 0xc) = 0;
    ((void (*)(uint64_t, uint64_t, uint64_t, int, int, int, int, int, uint64_t))(b + 0x642f70))(G, player, it, 1, 2, 0, CHARM_PAGE, 0, (uint64_t)(uint32_t)x | (uint64_t)y << 16);
    eff_on(b, G, it, player); charm_notify_stats(b, G, player);
    LOG("大背包：手上护身符 id %u 放进页 5 %d,%d", id, x, y); return 1;
}

// ---- B163 读档后「看不见的手上物品」：存盘时手上的物品被原版存成「在光标上」，读档后它挂在服务端玩家物品栏的光标上
//   （inv + 0x40），客户端手上在载入收尾（约 7 秒）被清空，之后两端一直不一致，玩家看不见、拿不到（存档里没丢）。
//   两端持续不一致满 50 拍：已鉴定的护身符先放护身符页（页 5，place_held），其余放背包页（页 0，原版「放进容器」0x18 到第一个能放下的格）；
//   都放不下才发原版「丢下光标物品」0x17 让它落到脚边并提示一次。光标物品的模式（+0xc）为 0 是残留在容器里的指针，这一拍不动手（B205）。
//   When the inconsistency lasts 50 ticks: an identified charm goes to the charm page (page 5, place_held) first, anything else to the bag
//   page (page 0, the original "put into container" 0x18 to the first cell that fits); only when neither has room does the original
//   "drop the cursor item" 0x17 land it at the feet with one notice. Mode 0 (item +0xc) is a stale pointer to an item already in a
//   container, so such a tick does nothing (B205).只对传进来的 player 动手——它是本机玩家在本地服务端上的单位
//   （diag.c 的 local_player_id 口径），联机时别人的玩家、远端服务端都不碰。
//   动手后写一行日志，再过 30 拍复查服务端光标是否已空；同一件物品失败后最多再试 3 次，之后只记一次红行。
// ---- B163 "invisible held item" after loading: the original saves an item held at save time as "on the cursor"; after loading it hangs on the
//   server player's inventory cursor (inv + 0x40), while the client hand is cleared at the end of loading (about 7 seconds later), so the two
//   sides stay inconsistent and the player can neither see nor take the item (it is not lost from the save).
//   When the inconsistency lasts 50 ticks: if bag page 0 has room, send the original "put into container" 0x18 to the first cell that fits;
//   if not, send the original "drop the cursor item" 0x17 so it lands at the player's feet. Only the passed player is touched -- it is the
//   local player's unit on the local server (the local_player_id convention in diag.c); other players and remote servers are left alone.
//   One log line after acting, then a recheck 30 ticks later of whether the server cursor is empty; after a failure the same item is retried
//   at most 3 more times, then one red line only.
// 审计 P1（Opus 11-05）：只在角色进局后的前 CR_WINDOW 拍里动手（读档留下的那件在载入收尾约 7 秒后才出现两端不一致），
//   正常游玩中的短暂不一致（魔盒放进仓库后服务端光标还挂着等）不碰，免得背包满时被丢到地上；单位号还要等于客户端本地玩家单位号，联机房主本地服务端上的别人不碰
// Audit P1 (Opus 11-05): act only within the first CR_WINDOW ticks after the character enters the run (the leftover from loading only becomes inconsistent at the end of loading, about 7 s in),
//   so brief inconsistencies in normal play (the server cursor still holding the cube after it went into the stash, etc.) are left alone and nothing gets dropped with a full bag; the unit id must also equal the client's local player unit id, so other players on a host's local server are never touched
#define CR_WINDOW 1500   // 拍（约 60 秒） | ticks (about 60 s)
static uint64_t g_cr_player; static int g_cr_streak, g_cr_recheck_at; static unsigned g_cr_tick, g_cr_enter;
static uint32_t g_cr_pend_id, g_cr_fail_id, g_cr_red_id, g_cr_stale_id; static int g_cr_fails;
void dm_cursor_rescue_tick(uint8_t *b, uint64_t G, uint64_t player) {
    if (!b) return;
    g_cr_tick++;
    if (player != g_cr_player) { g_cr_player = player; g_cr_enter = g_cr_tick; g_cr_streak = 0; g_cr_recheck_at = 0; g_cr_fails = 0; g_cr_fail_id = g_cr_red_id = g_cr_pend_id = g_cr_stale_id = 0; }
    if (!player || !G) return;
    { uint32_t slot = *(uint32_t *)(b + 0x2d20e4c), lid = slot < 8 ? *(uint32_t *)(b + 0x2d60380 + 4 * slot) : 0xffffffffu;   // 同 diag.c local_player_id | same as local_player_id in diag.c
      if (lid == 0xffffffffu || *(uint32_t *)(player + 8) != lid) { g_cr_streak = 0; return; } }
    uint64_t inv = *(uint64_t *)(player + 0x90);
    if (!inv || *(uint32_t *)inv != 0x01020304) { g_cr_streak = 0; return; }
    uint64_t cur = *(uint64_t *)(inv + 0x40);
    // 30 拍后复查：物品还在就是这次没救回 | Recheck 30 ticks later: still there means this attempt did not work
    if (g_cr_recheck_at && (int32_t)(g_cr_tick - (uint32_t)g_cr_recheck_at) >= 0) {
        g_cr_recheck_at = 0;
        if (!cur) { g_cr_fails = 0; g_cr_fail_id = 0; LOG("手上物品救回：复查 30 拍后服务端光标已空（物品 id %u）", g_cr_pend_id); }
        else { uint32_t id = *(uint32_t *)(cur + 8);
            if (id == g_cr_pend_id) { if (id == g_cr_fail_id) g_cr_fails++; else { g_cr_fail_id = id; g_cr_fails = 1; }
                LOG("手上物品救回：物品 id %u 处理 30 拍后仍挂在服务端光标上（第 %d 次失败）", id, g_cr_fails); }
            else { g_cr_fail_id = id; g_cr_fails = 0; LOG("手上物品救回：复查时服务端光标已换成物品 id %u", id); } }
    }
    if (g_cr_tick - g_cr_enter > CR_WINDOW) { g_cr_streak = 0; return; }   // 进局窗口外只做完上面的复查，不再动手 | outside the entry window only the recheck above finishes; no new action
    if (!cur) { g_cr_streak = 0; g_cr_stale_id = 0; return; }
    // B205：模式 0 表示这件物品已经在某个容器里，光标指针只是残留——这一拍不动手，免得背包满时把它丢到脚边；同一件只记一次日志
    // B205: mode 0 means the item is already inside some container and the cursor pointer is only stale -- do not act on this tick, or a full
    //   bag would drop it; log once per item
    if (dm_cursor_rescue_stale(*(uint32_t *)(cur + 0xc))) {
        g_cr_streak = 0; uint32_t sid = *(uint32_t *)(cur + 8);
        if (g_cr_stale_id != sid) { g_cr_stale_id = sid; LOG("手上物品救回：光标残留（物品已在容器里），不动手（物品 id %u）", sid); }
        return;
    }
    g_cr_stale_id = 0;
    if (!dm_cursor_rescue_due(cur != 0, dm_hud_held() != 0, &g_cr_streak)) return;
    uint32_t id = *(uint32_t *)(cur + 8);
    if (id != g_cr_fail_id) { g_cr_fail_id = id; g_cr_fails = 0; g_cr_red_id = 0; }
    if (g_cr_fails > 3) {   // 已经失败 4 次（首次 + 再试 3 次）：只记一次红行 | Four failures (first + 3 retries): one red line only
        if (g_cr_red_id != id) { g_cr_red_id = id; LOG("【红】手上物品救回：物品 id %u 试了 %d 次仍没放回，不再自动重试", id, g_cr_fails); }
        return;
    }
    // B205：护身符先放护身符页（页 5，place_held 自己找空位），放不下或别的物品放背包页（0x18），都放不下才落地（0x17）
    // B205: a charm goes to the charm page (page 5, place_held finds the cell) first, otherwise or when that fails the bag page (0x18),
    //   and only when neither has room does it drop (0x17)
    int w = 1, h = 1, x = 0, y = 0, cx = 0, cy = 0; dm_cursor_item_size(b, cur, &w, &h);
    uint32_t mode = *(uint32_t *)(cur + 0xc);   // 动手前的模式（place_held 会改成 0） | mode before acting (place_held sets it to 0)
    int is_charm = charm_paged(b, cur), charm_room = page_free(inv, CHARM_PAGE, w, h, &cx, &cy), bag_room = page_free(inv, 0, w, h, &x, &y);
    int plan = dm_cursor_rescue_plan(is_charm, charm_room, bag_room);
    LOG("手上物品救回：物品 id %u 模式 %u 护身符 %d 页 5 有空 %d 背包页有空 %d", id, mode, is_charm, charm_room, bag_room);
    if (plan == DM_RESCUE_CHARM && !place_held(b, G, player, id, 0, 0)) plan = bag_room ? DM_RESCUE_BAG : DM_RESCUE_DROP;   // place_held 内部会再查一次空位，真失败才退 | place_held rechecks the room; only a real failure falls back
    g_cr_pend_id = id; g_cr_recheck_at = (int)g_cr_tick + 30;
    if (plan == DM_RESCUE_CHARM) { LOG("手上物品救回：物品 id %u 放进护身符页（页 5）", id); return; }   // place_held 已通知客户端，不发包 | place_held already notified the client; no packet
    uint8_t pk[17]; if (plan == DM_RESCUE_BAG) dm_cursor_rescue_place_pkt(pk, id, x, y);
    else { uint8_t dp[5]; dm_cursor_rescue_drop_pkt(dp, id); memcpy(pk, dp, sizeof dp); if (!g_cr_fails) atomic_store(&g_rescue_ground, 1); }   // 同一件只在第一次落地时提示（审计 P2） | notice only on the first drop of the same item (audit P2)
    *(uint32_t *)(G + 0x19050) = 1;   // 照收包函数 0x632510 / DM_TEST_GIVE_INV 的做法置「处理中」 | set the "processing" flag as 0x632510 / DM_TEST_GIVE_INV do
    ((void (*)(uint64_t, uint64_t, uint8_t *, uint32_t))(b + 0x6e5b40))(G, player, pk, plan == DM_RESCUE_BAG ? 17 : 5);
    *(uint32_t *)(G + 0x19050) = 0;
    LOG("手上物品救回：物品 id %u %s（包 0x%02x），服务端光标%s", id, plan == DM_RESCUE_BAG ? "放回背包页" : "护身符页、背包页都满，丢到脚边", pk[0],
        *(uint64_t *)(inv + 0x40) ? "仍在" : "已空");
    if (plan == DM_RESCUE_BAG) LOG("手上物品救回：物品 id %u 放到背包页 %d,%d", id, x, y);
}
// 命令分派入口（diag.c c2s_obs，先于 dm_stack_c2s）；接管了就换成空操作包 | Command dispatch entry (c2s_obs in diag.c, before dm_stack_c2s); a taken-over packet becomes a no-op
void dm_bag_c2s(uint64_t *regs) {
    uint64_t G = regs[0], player = regs[1]; uint8_t *pk = (uint8_t *)(uintptr_t)regs[2]; uint32_t len = (uint32_t)regs[3]; uint8_t *b = B;
    // 鉴定卷轴 / 书「对物品使用」0x26 的目标在页 5（D1）：原版服务端不认页 5 上的目标（实测发了包、物品没变），
    //   照 B148 拿起的做法先把它搬到背包页，再把包里的目标页 / 格改写成背包页那一格交给原版——扣卷轴 / 书的次数、广播更新、客户端退出等目标都是原版的；
    //   鉴定完下一拍搬回页 5 原格（dm_bag_srv_tick g_ret_*）。背包页放不下：走免费鉴定（同双击，0x78acf0），包照原样交给原版（原版不认、不扣次数）
    //   包体：+1 标志，+2 目标 id，+6 = 4，+7 目标页，+8 / +9 目标格 x / y，+10 卷轴 id（0x2cecf0 拼的，实测）
    // An identify scroll / tome "use on item" 0x26 aimed at page 5 (D1): the original server does not accept a target on page 5 (measured: the packet went out, the item did not change),
    //   so as with the B148 pick-up it is first moved to the bag page and the packet's target page / cell are rewritten to that cell for the original -- using up the scroll / tome charge, the broadcast update and the client leaving targeting are all the original's;
    //   on the next tick after identifying it goes back to its page 5 cell (g_ret_* in dm_bag_srv_tick). No room on the bag page: free identify (as the double tap, 0x78acf0), and the packet goes to the original unchanged (it refuses, no charge used)
    //   Layout: +1 flag, +2 target id, +6 = 4, +7 target page, +8 / +9 target cell x / y, +10 scroll id (built by 0x2cecf0, measured)
    if (b && G && player && pk && len == 36 && pk[0] == 0x26 && pk[6] == 4 && pk[7] == CHARM_PAGE) {
        uint32_t tid; memcpy(&tid, pk + 2, 4); uint64_t inv = *(uint64_t *)(player + 0x90), t = 0;
        for (uint64_t it = inv && *(uint32_t *)inv == 0x01020304 ? *(uint64_t *)(inv + 0x10) : 0, n = 0; it && n < 2048 && !t; n++) { uint64_t d = *(uint64_t *)(it + 0x10); if (!d) break; if (*(uint32_t *)(it + 8) == tid) t = it; else it = *(uint64_t *)(d + 0xb0); }
        uint64_t td = t ? *(uint64_t *)(t + 0x10) : 0, tp = t ? *(uint64_t *)(t + 0x38) : 0;
        if (!td || !tp || *(uint8_t *)(td + 0x55) != CHARM_PAGE || (*(uint32_t *)(td + 0x18) & 0x10)) return;
        int ox = *(int32_t *)(tp + 0x10), oy = *(int32_t *)(tp + 0x14);
        if (quick_out(b, G, player, tid, 0, *(int32_t *)(b + 0x1dae684 + *(uint32_t *)(player + 4) * 8 + 4), 1)) {
            static uint8_t q[36]; memcpy(q, pk, 36); q[7] = 0; q[8] = (uint8_t)g_qx; q[9] = (uint8_t)g_qy; regs[2] = (uint64_t)(uintptr_t)q;
            g_ret_id = tid; g_ret_x = ox; g_ret_y = oy; g_ret_wait = 25; LOG("大背包：页 5 鉴定 id %u：先搬到背包页 %d,%d，改写成原版背包页鉴定", tid, g_qx, g_qy); }
        else { int r = ((int (*)(uint64_t, uint64_t, uint64_t))(b + 0x78acf0))(G, player, t); LOG("大背包：页 5 鉴定 id %u：背包页满，走免费鉴定 → %d", tid, r); }
        return; }
    if (!b || !G || !player || !pk || len != 17) return;
    uint64_t inv = *(uint64_t *)(player + 0x90); if (!inv) return;
    // 护身符只属于护身符页（用户 11-05「照暗黑 4」，语义表 v0.3 C1 / C5 / C8）：
    //   地上捡起 0x16 / 买入 0x32：页 5 放不下就不收（留在原处），提示「护身符页满了」，与背包页有没有空位无关；
    //   背包页满而页 5 有位的地上捡起（B190）：腾一格交原版捡、再搬进页 5（gp_pick）；离得远原版先走过去，到跟前重试（g_gp_*）
    // Charms belong to the charm page only (user 11-05 "like Diablo 4", semantics v0.3 C1 / C5 / C8):
    //   ground pick-up 0x16 / purchase 0x32: if page 5 has no room it is not taken (stays where it is) and "charm page is full" is shown, whatever room the bag page has;
    //   ground pick-up with the bag page full and room on page 5 (B190): a cell is freed for the original pick-up and the charm then moves to page 5 (gp_pick); when far the original walks there first and it is retried on arrival (g_gp_*)
    if ((pk[0] == 0x16 || pk[0] == 0x32) && atomic_load(&g_legacy)) return;   // QA C9：拾取照原版进背包页 | QA C9: pick-up goes to the bag page as in the original
    if (pk[0] == 0x16 || pk[0] == 0x32) {
        uint32_t iid; memcpy(&iid, pk + (pk[0] == 0x16 ? 1 : 5), 4); uint64_t it = srv_item(G, iid);
        if (!it || !*(uint64_t *)(it + 0x10) || !charm_paged(b, it)) return;
        int gw = 1, gh = 1, fx, fy; dm_cursor_item_size(b, it, &gw, &gh);
        if (!page_free(inv, CHARM_PAGE, gw, gh, &fx, &fy)) { atomic_store(&g_full, 1); LOG("大背包：护身符页满了，%s id %u 不收", pk[0] == 0x16 ? "地上捡起" : "买入", iid); }
        else if (pk[0] == 0x32 || page_free(inv, 0, gw, gh, &fx, &fy)) return;   // 背包页有位：原版先进背包页，下一拍搬进页 5 | room on the bag page: the original puts it there and it moves to page 5 next tick
        else if (*(uint64_t *)(inv + 0x40)) { LOG("大背包：背包页满、手上有东西，地上护身符 id %u 照原版（不收）", iid); return; }
        else if (g_gp_in) return;   // gp_pick 自己交给原版的那一包 | the packet gp_pick itself hands to the original
        else if (gp_pick(b, G, player, pk, 1)) g_gp_id = 0;
        else { memcpy(g_gp_pk, pk, 17); g_gp_id = iid; g_gp_ttl = 250; return; } }
    else if (pk[0] == 0x18 || pk[0] == 0x19) {
        uint32_t id, x, y, pg; memcpy(&id, pk + 1, 4); memcpy(&x, pk + 5, 4); memcpy(&y, pk + 9, 4); memcpy(&pg, pk + 13, 4);
        // 手上的护身符放进背包页（C8）：不接，物品留在手上，提示一次（手柄 ZL 切到背包页再按 A 能发出来；触屏拿着护身符时界面一直停在护身符页）
        // A charm in hand placed on the bag page (C8): refused, the item stays in hand, one notice (a controller can send it after ZL to the bag page and A; with touch the UI stays on the charm page while a charm is held)
        if (pk[0] == 0x18 && pg == 0) { uint64_t h = *(uint64_t *)(inv + 0x40); if (!h || *(uint32_t *)(h + 8) != id || !charm_paged(b, h)) return;
            atomic_store(&g_only, 1); LOG("大背包：护身符 id %u 不能放进背包页，留在手上", id); }
        else if (pg != CHARM_PAGE) return;
        // 拿起（B148 / B191）：原版拿起 0x6d5ec0 把页号 ≥ 5 一律当成 0xff（cmp #5 / csel），服务端什么都不做，底层搬上光标的 0x6511d0 对页 5 也拒（实测返回 0），
        //   所以先用已证实的底层搬动把它搬到一张原版认的页上，再把这个包改写成「从那一页那一格拿起」交给原版——手上的图标、跟着拖、放到哪都是原版那一套。
        //   背包页满（B191）：方块 / 仓库页当跳板实测不行（面板没开时原版不从那一页拿起），改成临时腾一格（room_take），经背包页拿起交原版分派，再放回腾格的那件；包换成空操作（直接挂服务端光标的做法实测扔不出、属性两端不一致、存档时崩）
        // Pick up (B148 / B191): the original pick-up 0x6d5ec0 turns any page >= 5 into 0xff (cmp #5 / csel) and the server does nothing, and the low-level move onto the cursor 0x6511d0 refuses page 5 too (measured: returns 0),
        //   so the item first moves to a page the original accepts with the proven low-level move, and the packet is rewritten as "pick up from that page and cell" for the original -- the icon in hand, the drag and every drop target are the original's.
        //   Bag page full (B191): the cube / stash page as a hop does not work (measured: with the panel closed the original does not pick up from that page), so a cell is freed temporarily (room_take), the bag-page pick-up goes to the original dispatcher and the moved item goes back; the packet becomes a no-op (putting it straight on the server cursor was measured to be undroppable, with mismatched stats and a crash on save)
        else if (pk[0] == 0x19) { int r = quick_out(b, G, player, id, 0, *(int32_t *)(b + 0x1dae684 + *(uint32_t *)(player + 4) * 8 + 4), 1);
            LOG("大背包：页 5 拿起 id %u → 先搬到页 0 %d,%d：%d%s", id, g_qx, g_qy, r, r ? "，改写成原版拿起" : "");
            if (r) { static uint8_t pick[17]; memcpy(pick, pk, 17); uint32_t nx = (uint32_t)g_qx, ny = (uint32_t)g_qy, npg = 0; memcpy(pick + 5, &nx, 4); memcpy(pick + 9, &ny, 4); memcpy(pick + 13, &npg, 4);
                regs[2] = (uint64_t)(uintptr_t)pick; return; }
            { uint64_t it = inv_find(inv, id); int cw = 1, ch = 1, row = *(int32_t *)(b + 0x1dae684 + *(uint32_t *)(player + 4) * 8 + 4); troom t; if (it) dm_cursor_item_size(b, it, &cw, &ch);
              // 背包页满（B191）：临时腾一格，照上面的背包页拿起交原版分派（同一拍里上手），再把腾格的那件放回 | bag page full (B191): free a cell temporarily, hand the bag-page pick-up above to the original dispatcher (in hand within the same tick), then put the moved item back
              if (it && !g_gp_in && room_take(b, inv, cw, ch, row, &t)) { uint32_t yid = *(uint32_t *)(t.y + 8); int ok = quick_out(b, G, player, id, 0, row, 1), held = 0;
                  if (ok) { static uint8_t q[17]; memcpy(q, pk, 17); uint32_t nx = (uint32_t)g_qx, ny = (uint32_t)g_qy, npg = 0; memcpy(q + 5, &nx, 4); memcpy(q + 9, &ny, 4); memcpy(q + 13, &npg, 4);
                      g_gp_in = 1; ((void (*)(uint64_t, uint64_t, uint8_t *, uint32_t))(b + 0x6e5b40))(G, player, q, 17); g_gp_in = 0;
                      uint64_t h = *(uint64_t *)(inv + 0x40); held = h && *(uint32_t *)(h + 8) == id;
                      if (!held) { uint64_t f = inv_find(inv, id), fd = f ? *(uint64_t *)(f + 0x10) : 0; if (fd && *(uint8_t *)(fd + 0x55) == 0) { int fx, fy, gw = 1, gh = 1; dm_cursor_item_size(b, f, &gw, &gh);   // 原版没拿起：搬回页 5 | the original did not take it: back to page 5
                          if (page_free(inv, CHARM_PAGE, gw, gh, &fx, &fy) && ((int (*)(uint64_t, uint64_t, int, int, int, int))(b + 0x560740))(inv, f, fx, fy, CHARM_PAGE + 2, row)) { void (*nt)(uint64_t, uint64_t, uint64_t, int, int, int, int, int, uint64_t) = (void (*)(uint64_t, uint64_t, uint64_t, int, int, int, int, int, uint64_t))(b + 0x642f70);
                              *(uint8_t *)(fd + 0x55) = CHARM_PAGE; nt(G, player, f, 0x20, 4, 0, 0, g_qy, g_qx); nt(G, player, f, 1, 2, 0, CHARM_PAGE, 0, (uint64_t)(uint32_t)fx | (uint64_t)fy << 16); eff_on(b, G, f, player); charm_notify_stats(b, G, player); } } } }
                  room_give(b, G, player, inv, row, &t);
                  LOG("%s大背包：页 5 拿起 id %u 背包页满，腾格（id %u）后经背包页拿起 → %s", held ? "" : "【红】", id, yid, held ? "上手" : "没上手，搬回页 5"); }
              else LOG("大背包：页 5 拿起 id %u：背包页满且腾不出格，不拿起", id); } }
        else { int r = place_held(b, G, player, id, (int)x, (int)y); LOG("大背包：页 5 放下 id %u → %d", id, r); } }
    else return;
    static uint8_t noop[0x24]; memset(noop, 0, sizeof noop); noop[0] = 0x26; noop[6] = 0xff; regs[2] = (uint64_t)(uintptr_t)noop; regs[3] = sizeof noop;
}
