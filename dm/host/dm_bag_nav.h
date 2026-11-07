// 手柄物品栏焦点导航的规则表（B235 / B239 / B189；纯函数，不读游戏内存——宿主 dm_bag.c 与 tests/t_bag_nav.c 共用这一份）。
// 元素：背包 12×7 格、十个装备格、武器 I / II 换手钮、两枚背包页签、腰带最下一排四格（及展开后的第二排）、底排三钮（百宝囊 / 金币 / 存放）；矩形是面板单位（1 格 = 98），
//   取自 dm/ios/tools/bag_layout.py 的 RECTS / TAB_L / TAB_R 与 dm_bag_layout.h（底排钮取 pouch_layout.py 与 Mac 实测）；武器 I / II 换手钮也是原版的焦点停靠点（Mac 实测：右手武器 ↑ 进 I/II 钮）。
// 原版规则 = 几何上最近的那个（dm_bag_nav_geo）：候选整个在该方向上（容差 TOL）、中心也在该方向；与起点在横轴（上下走）/ 纵轴（左右走）上有重叠的优先，
//   按「间隔 + 中心偏移 / 2」最小；都不重叠才看不重叠的（间隔 + 2 × 错开距离）。分数相同取表里靠后的（Mac 实测原版：项链 ↓ 进 slot_left_hand）；唯一例外鞋格 ↑ 两个戒指等距，原版进右戒指 slot_right_hand（Mac 实测），表里单写。
// 我们的规则（dm_bag_nav_step，在原版规则上加）：
//   ① 页签永远不是几何候选：从背包第 0 行按上直接进正上方的装备格 / 腰带（B235：页签压在第 0–2 列上方，先跳页签再到武器格）；
//   ② 页签和腰带四格在同一行：腰带第 0 格 ← 进护身符页签、护身符页签 ← 背包页签，背包页签 → 护身符页签 → 腰带第 0 格；背包页签 ← 交原版（对面面板）；
//      页签 ↓ 回背包第 0 行正下方那列，↑ 进正上方的装备格；切页另有 ZL / ZR（商店开着时 L / R 点按），A 切到这一页；
//   ③ 手上拿着物品时页签是死路（A 被吞，切不了页）：腰带第 0 格 ← 不动（宿主吃掉这一下）；网格第 0 行 ↑ 与装备格 / 武器钮之间照原版（原版拿着装备时
//      会挑放得下的栏位，例如拿着靴子从第 10 列按上进鞋格，Mac 实测；几何表不替它做主），只是原版若落到页签（Mac 实测：从网格第 0 行按上、从腰带第二排按左都会），宿主把焦点放回原处（dm_bag_nav_held_fix）；切页仍用 ZL / ZR；
//   ④ 记忆：从第 0 行往上离开时记那一格（entry_top），从最下一行往下离开时记那一格（entry_bot）；站在进来那一格一步就到的元素上，往回按原路回那一格
//      （上去再下来、下去再上来回原格；走开了就按几何）；底排钮按上回 entry_bot 那一格（B189 ③：以前用的是「上次停稳格」，按得快时停稳格还是上一行，按上就交了原版）；
//      物品栏打开 / 关上时清空（B189 ①，换角色必经关面板）；
//   ⑤ 腰带：最下一排 ↑ 交原版（腰带展开时进第二排，否则进正上方的装备格）；第二排 ↓ 回最下一排同一格（B239：腰带格往下走不出去），其余方向交原版；
//      腰带控件矩形在展开时会盖住鞋格 / 腰带位，宿主按光标所在排认最下一排。
// 返回值：元素号；BN_ORIG = 不接管（交原版：对面面板、边界、网格内部以外的原版行为）；BN_STAY = 接管但不动。
// Controller inventory focus navigation rule table (B235 / B239 / B189; pure functions with no game memory -- the host's dm_bag.c and tests/t_bag_nav.c share this copy).
// Elements: the 12x7 bag cells, ten equipment slots, the weapon I / II swap buttons, the two bag tabs, the belt's bottom-row four cells (and the second row when expanded), the three bottom buttons
//   (pouch / gold / deposit); rects in panel units (1 cell = 98) from RECTS in dm/ios/tools/bag_layout.py and dm_bag_layout.h (bottom buttons from pouch_layout.py and Mac
//   measurements); the weapon I / II swap buttons are original focus stops too (measured on the Mac: Up on the right weapon enters them).
// Original rule = the geometrically nearest one (dm_bag_nav_geo): the candidate lies wholly in that direction (tolerance TOL) with its centre in that direction too; candidates
//   overlapping the start on the cross axis win, by the smallest "gap + centre offset / 2"; only when none overlaps the others count (gap + 2 x the miss). Ties go to the later entry (measured original on the Mac: Down on the amulet enters slot_left_hand); the one exception is Up on the boots, equidistant from both rings, where the original enters the right ring slot_right_hand (measured on the Mac), written out in the table.
// Our rules (dm_bag_nav_step, on top of the original rule):
//   1. The tabs are never geometric candidates: Up from bag row 0 goes straight to the equipment slot / belt above (B235: the tabs sit over columns 0-2, the cursor jumped to a tab first);
//   2. The tabs and the four belt cells share one row: Left on belt cell 0 enters the charm tab, Left on the charm tab the bag tab; Right walks bag tab -> charm tab -> belt cell 0; Left
//      on the bag tab stays with the original (opposite panel); Down from a tab returns to the bag row 0 column below, Up goes to the slot above;
//      pages also switch with ZL / ZR (L / R taps with a shop open), A switches to that tab's page;
//   3. With an item in hand a tab is a dead end (A is swallowed, the page cannot change): Left on belt cell 0 does not move (the host eats it); Up from grid row 0 and moves among the
//      equipment slots / weapon buttons stay the original's (holding gear the original picks a slot that takes it, e.g. boots from column 10 go up into the boots slot, measured on the
//      Mac; the geometric table does not overrule it), except that when the original lands on a tab (measured on the Mac: Up from grid row 0, Left from the belt's second row) the host puts the focus back (dm_bag_nav_held_fix); ZL / ZR still page;
//   4. Memory: leaving row 0 upward records that cell (entry_top), leaving the last row downward records that cell (entry_bot); on the element one step from the entry cell, pressing back
//      returns to that cell (up then down, down then up land on the same cell; once moved on, geometry); Up on a bottom button returns to the entry_bot cell (B189 item 3: it used the "last settled cell", which was still the row above after a quick press, so Up
//      fell to the original); cleared when the inventory opens / closes (B189 item 1; a character switch always closes the panel);
//   5. Belt: Up on the bottom row stays with the original (into the second row when the belt is expanded, else the slot above); Down on the second row returns to the same bottom-row cell
//      (B239: the belt could not be left downward), other directions stay with the original; the expanded belt widget covers the boots / belt slots, so the host tells the bottom row by the cursor row.
// Returns: element number; BN_ORIG = not taken over (left to the original: opposite panel, borders, original behaviour outside the grid interior); BN_STAY = taken over but no move.
#pragma once
#include "dm_bag_layout.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if DM_BAG_TAB_DX == 0
#error "dm_bag_nav.h 只写了横排页签（页签与腰带同一行）；纵排要另写一张表 | only the horizontal tab row (tabs on the belt row) is tabulated; a vertical tab bar needs its own table"
#endif

enum { BN_UP, BN_DOWN, BN_LEFT, BN_RIGHT };
enum { BN_COLS = DM_BAG_COLS, BN_ROWS = DM_BAG_ROWS, BN_CELLS = DM_BAG_COLS * DM_BAG_ROWS,
       BN_RARM = BN_CELLS, BN_HEAD, BN_GLOVES, BN_TORSO, BN_BELTSLOT, BN_NECK, BN_RRING, BN_LRING, BN_FEET, BN_LARM, BN_SWAPR, BN_SWAPL,
       BN_TAB0, BN_TAB1, BN_BELT0, BN_BELT1, BN_BELT2, BN_BELT3, BN_BELTU0, BN_BELTU1, BN_BELTU2, BN_BELTU3,
       BN_POUCH, BN_GOLD, BN_STORE, BN_N,
       BN_ORIG = -1, BN_STAY = -2, BN_TOL = 16, BN_BELT_N = 4 };

typedef struct { const char *name; short x, y, w, h; } dm_bag_nav_rect;
// 面板单位矩形；名字是控件名（腰带四格同一个控件 Belt，按格号分） | panel-unit rects; names are widget names (the belt cells are one widget, Belt, split by cell)
static const dm_bag_nav_rect k_bn_rect[BN_N - BN_CELLS] = {
    { "slot_right_arm", 86, 81, 196, 392 }, { "slot_head", 347, 45, 196, 196 }, { "slot_gloves", 346, 265, 196, 196 }, { "slot_torso", 607, 51, 196, 294 },
    { "slot_belt", 606, 371, 196, 98 }, { "slot_neck", 913, 43, 98, 98 }, { "slot_right_hand", 852, 160, 98, 98 }, { "slot_left_hand", 974, 160, 98, 98 },
    { "slot_feet", 864, 273, 196, 196 }, { "slot_left_arm", 1126, 81, 196, 392 },
    { "weaponswap_right_arm", 74, 29, 216, 59 }, { "weaponswap_left_arm", 1114, 29, 216, 59 },   // 武器 I / II 换手钮（bag_layout.py TAB_L / TAB_R，原版焦点停靠） | weapon I / II swap buttons (original focus stops)
    { "TouchBagTab0", DM_BAG_TAB_X, DM_BAG_TAB_Y, DM_BAG_TAB_W, DM_BAG_TAB_H }, { "TouchBagTab1", DM_BAG_TAB_X + DM_BAG_TAB_DX, DM_BAG_TAB_Y + DM_BAG_TAB_DY, DM_BAG_TAB_W, DM_BAG_TAB_H },
    { "Belt", DM_BAG_BELT_X, DM_BAG_BELT_Y, DM_BAG_BELT_CELL, 98 }, { "Belt", DM_BAG_BELT_X + DM_BAG_BELT_CELL, DM_BAG_BELT_Y, DM_BAG_BELT_CELL, 98 },
    { "Belt", DM_BAG_BELT_X + 2 * DM_BAG_BELT_CELL, DM_BAG_BELT_Y, DM_BAG_BELT_CELL, 98 }, { "Belt", DM_BAG_BELT_X + 3 * DM_BAG_BELT_CELL, DM_BAG_BELT_Y, DM_BAG_BELT_CELL, 98 },
    { "Belt", DM_BAG_BELT_X, DM_BAG_BELT_Y - DM_BAG_BELT_CELL, DM_BAG_BELT_CELL, 98 }, { "Belt", DM_BAG_BELT_X + DM_BAG_BELT_CELL, DM_BAG_BELT_Y - DM_BAG_BELT_CELL, DM_BAG_BELT_CELL, 98 },
    { "Belt", DM_BAG_BELT_X + 2 * DM_BAG_BELT_CELL, DM_BAG_BELT_Y - DM_BAG_BELT_CELL, DM_BAG_BELT_CELL, 98 }, { "Belt", DM_BAG_BELT_X + 3 * DM_BAG_BELT_CELL, DM_BAG_BELT_Y - DM_BAG_BELT_CELL, DM_BAG_BELT_CELL, 98 },
    { "TouchPouch", 222, 1294, 280, 128 }, { "gold_button", 500, 1293, 402, 136 }, { "TouchStore", 916, 1323, 271, 67 },
};
static inline int dm_bag_nav_cell(int col, int row) { return row * BN_COLS + col; }
static inline dm_bag_nav_rect dm_bag_nav_rect_of(int e) {
    if (e >= 0 && e < BN_CELLS) return (dm_bag_nav_rect){ "grid", (short)(DM_BAG_GRID_X + e % BN_COLS * 98), (short)(DM_BAG_GRID_Y + e / BN_COLS * 98), 98, 98 };
    return k_bn_rect[e - BN_CELLS];
}
static inline int dm_bag_nav_is_tab(int e) { return e == BN_TAB0 || e == BN_TAB1; }
static inline int dm_bag_nav_is_belt(int e) { return e >= BN_BELT0 && e <= BN_BELT3; }
static inline int dm_bag_nav_is_beltu(int e) { return e >= BN_BELTU0 && e <= BN_BELTU3; }
static inline int dm_bag_nav_is_bottom(int e) { return e == BN_POUCH || e == BN_GOLD || e == BN_STORE; }
static inline int dm_bag_nav_is_slot(int e) { return e >= BN_RARM && e <= BN_LARM; }

typedef struct {
    int held;                  // 手上拿着物品 | an item in hand
    int pouch, store;          // 百宝囊格 / 存放钮在屏上（自定义页开关） | the pouch cell / Deposit button is on screen (custom page switches)
    int entry_top, entry_bot;  // 从第 0 行往上 / 最下一行往下离开时那一列，-1 = 没有 | column left upward from row 0 / downward from the last row, -1 = none
} dm_bag_nav_state;

static inline void dm_bag_nav_reset(dm_bag_nav_state *s) { s->entry_top = s->entry_bot = -1; }
// 物品栏打开 / 关上（B189 ①：记忆复位） | inventory opened / closed (B189 item 1: memory reset)
static inline void dm_bag_nav_panel(dm_bag_nav_state *s, int open) { (void)open; dm_bag_nav_reset(s); }
// 焦点从 from 到了 to（宿主每帧比对；单测与 Mac 判据也用它） | the focus went from from to to (the host compares every frame; the unit test and the Mac judge use it too)
static inline void dm_bag_nav_moved(dm_bag_nav_state *s, int from, int to) {
    if (from < 0 || from >= BN_CELLS || to < 0 || to == from) return;
    if (to >= BN_CELLS && from / BN_COLS == 0 && dm_bag_nav_rect_of(to).y < DM_BAG_GRID_Y) s->entry_top = from % BN_COLS;
    if (to >= BN_CELLS && from / BN_COLS == BN_ROWS - 1 && dm_bag_nav_is_bottom(to)) s->entry_bot = from % BN_COLS;
}

static inline int dm_bag_nav_xov(dm_bag_nav_rect a, dm_bag_nav_rect b) { int l = a.x > b.x ? a.x : b.x, r = a.x + a.w < b.x + b.w ? a.x + a.w : b.x + b.w; return r - l; }
static inline int dm_bag_nav_yov(dm_bag_nav_rect a, dm_bag_nav_rect b) { int t = a.y > b.y ? a.y : b.y, d = a.y + a.h < b.y + b.h ? a.y + a.h : b.y + b.h; return d - t; }

// 原版规则：几何上最近的（见文件头）；页签、腰带第二排、不在屏上的底排钮不是候选 | the original rule: geometrically nearest (see the header); tabs, the belt's second row and hidden bottom buttons are not candidates
static inline int dm_bag_nav_geo(const dm_bag_nav_state *s, int from, int dir) {
    dm_bag_nav_rect f = dm_bag_nav_rect_of(from); int best = BN_ORIG; long bs = 0;
    for (int e = 0; e < BN_N; e++) {
        if (e == from || dm_bag_nav_is_tab(e) || dm_bag_nav_is_beltu(e) || (e == BN_POUCH && !s->pouch) || (e == BN_STORE && !s->store)) continue;
        dm_bag_nav_rect c = dm_bag_nav_rect_of(e); int g, ov, off, beyond;
        switch (dir) {
        case BN_UP:    g = f.y - (c.y + c.h); beyond = 2 * c.y + c.h < 2 * f.y + f.h; break;
        case BN_DOWN:  g = c.y - (f.y + f.h); beyond = 2 * c.y + c.h > 2 * f.y + f.h; break;
        case BN_LEFT:  g = f.x - (c.x + c.w); beyond = 2 * c.x + c.w < 2 * f.x + f.w; break;
        default:       g = c.x - (f.x + f.w); beyond = 2 * c.x + c.w > 2 * f.x + f.w; break;
        }
        if (g < -BN_TOL || !beyond) continue;
        if (g < 0) g = 0;
        if (dir == BN_UP || dir == BN_DOWN) { ov = dm_bag_nav_xov(f, c); off = abs((2 * c.x + c.w) - (2 * f.x + f.w)) / 2; }
        else { ov = dm_bag_nav_yov(f, c); off = abs((2 * c.y + c.h) - (2 * f.y + f.h)) / 2; }
        long sc = ov > 0 ? g + off / 2 : 100000L + g - 2L * ov;
        if (best == BN_ORIG || sc <= bs) { best = e; bs = sc; }
    }
    return best;
}

// 一步：从 from 按 dir 落到哪个元素（原版规则 + 我们的规则，见文件头） | one step: where dir from from lands (original rule + our rules, see the header)
static inline int dm_bag_nav_step(const dm_bag_nav_state *s, int from, int dir) {
    if (from < 0 || from >= BN_N) return BN_ORIG;
    if (s->held && (from < BN_CELLS ? dir == BN_UP && from / BN_COLS == 0 : from <= BN_SWAPL)) return BN_ORIG;   // ③ 拿着物品：原版挑栏位 | item 3, holding: the original picks the slot
    if (from < BN_CELLS) {   // 网格内部是原版逐格走；出边界按几何（第 0 行 ↑ 进装备格 / 腰带，不进页签） | inside the grid the original walks cell by cell; at the edges geometry (row 0 Up: slot / belt, never a tab)
        int c = from % BN_COLS, r = from / BN_COLS;
        if (dir == BN_UP && r) return from - BN_COLS;
        if (dir == BN_DOWN && r < BN_ROWS - 1) return from + BN_COLS;
        if (dir == BN_LEFT && c) return from - 1;
        if (dir == BN_RIGHT && c < BN_COLS - 1) return from + 1;
        return dir == BN_UP || dir == BN_DOWN ? dm_bag_nav_geo(s, from, dir) : BN_ORIG;
    }
    if (dm_bag_nav_is_beltu(from)) return dir == BN_DOWN ? from - BN_BELTU0 + BN_BELT0 : BN_ORIG;
    if (dm_bag_nav_is_belt(from) && dir == BN_UP) return BN_ORIG;
    if (from == BN_TAB0 && dir == BN_LEFT) return BN_ORIG;
    if (from == BN_TAB1 && dir == BN_LEFT) return BN_TAB0;
    if (from == BN_TAB0 && dir == BN_RIGHT) return BN_TAB1;
    if (from == BN_TAB1 && dir == BN_RIGHT) return BN_BELT0;
    if (from == BN_BELT0 && dir == BN_LEFT) return s->held ? BN_STAY : BN_TAB1;
    if (dm_bag_nav_is_belt(from) && from != BN_BELT0 && dir == BN_LEFT) return from - 1;
    if (dm_bag_nav_is_belt(from) && from != BN_BELT3 && dir == BN_RIGHT) return from + 1;
    if (from == BN_FEET && dir == BN_UP) return BN_RRING;   // 鞋格 ↑：两个戒指等距，原版进右戒指（Mac 实测） | boots Up: both rings equidistant, the original enters the right ring (measured on the Mac)
    if (dm_bag_nav_is_bottom(from) && dir != BN_UP) return BN_ORIG;   // 底排钮只管 ↑（← → ↓ 原版 / dm_pouch.c） | bottom buttons: Up only (left / right / down are the original's / dm_pouch.c's)
    // 记忆：从进来那一格一步到这里的，原路回去（上去再下来、下去再上来回原格） | memory: when this element is one step from the entry cell, go back the same way (up then down, down then up return to the same cell)
    if (dm_bag_nav_is_bottom(from) && s->entry_bot >= 0 && dm_bag_nav_geo(s, dm_bag_nav_cell(s->entry_bot, BN_ROWS - 1), BN_DOWN) == from) return dm_bag_nav_cell(s->entry_bot, BN_ROWS - 1);
    if (dir == BN_DOWN && s->entry_top >= 0 && dm_bag_nav_geo(s, dm_bag_nav_cell(s->entry_top, 0), BN_UP) == from) return dm_bag_nav_cell(s->entry_top, 0);
    return dm_bag_nav_geo(s, from, dir);
}

// 网格逻辑格：原版按下方向键当下就换目标格，光标动画几帧后才到（动画途中光标会扫过别的格，B189 ③：从 (7,6) 下到金币钮，光标斜着经过 (6,6)）。
//   宿主按方向键按下沿推算（出网格的那一下不动），光标停稳一阵后以实测为准；记忆（entry_bot）就按这个格记
// The grid's logical cell: the original switches the target cell at the key press and the cursor animates there over a few frames (sweeping over other cells on the way,
//   B189 item 3: from (7,6) down to the gold button the cursor passes over (6,6) diagonally). The host advances it on direction press edges (a press leaving the grid does
//   not move it) and resyncs to the measured cell once the cursor has rested a while; the memory (entry_bot) is recorded from this cell
static inline int dm_bag_nav_track(int cell, int dir) {
    if (cell < 0 || cell >= BN_CELLS) return cell;
    int c = cell % BN_COLS, r = cell / BN_COLS;
    if (dir == BN_UP && r) r--; else if (dir == BN_DOWN && r < BN_ROWS - 1) r++; else if (dir == BN_LEFT && c) c--; else if (dir == BN_RIGHT && c < BN_COLS - 1) c++;
    return dm_bag_nav_cell(c, r);
}

// ③ 拿着物品时原版把焦点从别处放到了页签上：返回要放回的元素（原处）；不用改返回 -1。原处是网格第 0 行时放回那一格（拿着物品从第 0–2 列不能往上，
//   走别的列或先挪开）；原处是腰带第二排时放回最下一排同一格（第二排的格心常被物品图标盖住，回最下排稳）
// item 3: with an item in hand the original moved the focus onto a tab from elsewhere: returns the element to put it back on (where it came from); -1 = nothing to fix.
//   From grid row 0 it goes back to that cell (holding an item, Up from columns 0-2 is blocked; use another column or move first); from the belt's second row back to the same bottom-row cell
static inline int dm_bag_nav_held_fix(const dm_bag_nav_state *s, int prev, int cur) {
    if (!s->held || !dm_bag_nav_is_tab(cur) || prev < 0 || prev >= BN_N || dm_bag_nav_is_tab(prev)) return -1;
    return dm_bag_nav_is_beltu(prev) ? prev - BN_BELTU0 + BN_BELT0 : prev;
}

// 切页键（文件头 ②）：返回切到的页（0 背包 / 1 护身符），-1 = 这个键不归物品栏。ZL / ZR（Xbox LT / RT）：对面面板有页签条（商店、储藏箱）时归对面；
//   L / R（Xbox LB / RB）：不开商店时是原版页组换页（物品栏 → 技能 …），不能拿来切页；商店开着时点按（< 0.3 秒，按住 R 是原版排序）切页，手上有物品不切
// Page keys (header item 2): returns the page switched to (0 bag / 1 charms), -1 = the key is not the inventory's. ZL / ZR (Xbox LT / RT): the opposite panel's when it has a tab bar
//   (shop, stash); L / R (Xbox LB / RB): without a shop they are the original page-group switch (inventory -> skills ...) and cannot page; with a shop open a tap (< 0.3 s; holding R
//   is the original sort) pages, not with an item in hand
enum { BN_KEY_ZL, BN_KEY_ZR, BN_KEY_L, BN_KEY_R };
static inline int dm_bag_nav_page_key(int key, int shop, int opp_tabs, int held, int tap) {
    if (key == BN_KEY_ZL || key == BN_KEY_ZR) return opp_tabs ? -1 : key == BN_KEY_ZR;
    if (!shop || held || !tap) return -1;
    return key == BN_KEY_R;
}

// 元素名（单测、Mac 判据的报告用）：格子写 grid(c,r) | element name for unit-test and Mac-judge reports: cells are grid(c,r)
static inline const char *dm_bag_nav_name(int e, char *buf, int n) {
    static const char *const nm[BN_N - BN_CELLS] = { "RARM", "HEAD", "GLOVES", "TORSO", "BELTSLOT", "NECK", "RRING", "LRING", "FEET", "LARM", "SWAPR", "SWAPL",
        "TAB0", "TAB1", "BELT0", "BELT1", "BELT2", "BELT3", "BELTU0", "BELTU1", "BELTU2", "BELTU3", "POUCH", "GOLD", "STORE" };
    if (e == BN_ORIG) return "ORIG";
    if (e == BN_STAY) return "STAY";
    if (e >= 0 && e < BN_CELLS) { snprintf(buf, (size_t)n, "grid(%d,%d)", e % BN_COLS, e / BN_COLS); return buf; }
    return e >= BN_CELLS && e < BN_N ? nm[e - BN_CELLS] : "?";
}
static inline int dm_bag_nav_parse(const char *s) {
    char b[16]; int c, r;
    if (sscanf(s, "grid(%d,%d)", &c, &r) == 2 && c >= 0 && c < BN_COLS && r >= 0 && r < BN_ROWS) return dm_bag_nav_cell(c, r);
    for (int e = BN_CELLS; e < BN_N; e++) if (!strcmp(dm_bag_nav_name(e, b, sizeof b), s)) return e;
    return !strcmp(s, "STAY") ? BN_STAY : BN_ORIG;
}
