// 手柄佣兵页（B099 / B100）的纯函数：宿主 dm_cursor.c 与单测 tests/t_pad_merc.c 共用，不读游戏内存。
// Pure functions of the controller mercenary page (B099 / B100): shared by the host's dm_cursor.c and the unit test tests/t_pad_merc.c; they read no game memory.
#pragma once
#include <stdint.h>

// 物品数据 +0x18 的标志：0x10 = 已鉴定（原版「佣兵能不能穿」main+0x3bea44 在 0x3beaa8–0x3beab8 先判 (标志 & 0x110) == 0x10，不成立直接返回 0）。
// 返回 1 = 未鉴定（佣兵穿不了，B099 要提示的那种）
// Item data +0x18 flags: 0x10 = identified (the original "can the mercenary wear this" main+0x3bea44 first checks (flags & 0x110) == 0x10 at 0x3beaa8–0x3beab8 and returns 0 otherwise).
// Returns 1 = unidentified (the mercenary cannot wear it, the case B099 explains)
static inline int dm_item_flags_unid(uint32_t flags) { return !(flags & 0x10); }

// 同一面板里从一格跳到另一格时光标的落点（屏幕像素）= 目标格中心：起始格在屏上的左上角 (x0, y0) + (目标中心 − 起始左上) × k（k = 屏幕像素 / 面板布局单位，同 dm_bag.c）。
// 矩形 = 控件 +0x70 的 {x, y, 宽, 高}（布局单位，相对面板）。左上角由调用处拿起始格自己的命中函数从光标往左上量（光标停在格里哪一点都一样）
// Where the cursor lands when jumping from one cell to another in the same panel (screen pixels) = the target cell's centre: the start cell's top-left on screen (x0, y0) + (target centre − start top-left) × k
// (k = screen pixels per panel layout unit, as in dm_bag.c). A rect = the widget's {x, y, width, height} at +0x70 (layout units, relative to the panel). The caller measures the top-left with the start cell's
// own hit test, walking up and left from the cursor (so it does not matter where in the cell the cursor rests)
static inline void dm_pad_slot_jump(int x0, int y0, const int32_t from[4], const int32_t to[4], double k, int *tx, int *ty) {
    *tx = x0 + (int)((to[0] + to[2] / 2 - from[0]) * k);
    *ty = y0 + (int)((to[1] + to[3] / 2 - from[1]) * k);
}

// 原版焦点导航认不认布局里写的「上 = 某格」：目标格整个在起始格上面（目标下沿 ≤ 起始上沿）且左右与起始格有重叠才认，否则弃了这条、按几何往上找，
// 佣兵页上找到的是顶部页签条。实测（Mac 宿主 p4_upedges，佣兵页 NAV 七条「上」逐条走）：副手→头盔（同排）、右戒→躯干（左右不重叠）两条被弃，
// 躯干→头盔、手套→武器、腰带→躯干、左戒→项链、鞋→副手五条照布局走。1 = 原版会弃这条，要宿主接
// Whether the original focus navigation honours a layout "up = cell": only when the target lies wholly above the start cell (target bottom ≤ start top) and overlaps it horizontally;
// otherwise the entry is dropped and the game searches upwards geometrically, reaching the top tab bar on the mercenary page. Measured (Mac host p4_upedges, all seven "up" entries of the
// mercenary NAV walked): off-hand → helm (same row) and right ring → torso (no horizontal overlap) are dropped; torso → helm, gloves → weapon, belt → torso, left ring → amulet and
// boots → off-hand follow the layout. 1 = the game drops it and the host must take it
static inline int dm_pad_up_dropped(const int32_t from[4], const int32_t to[4]) {
    return to[1] + to[3] > from[1] || to[0] >= from[0] + from[2] || from[0] >= to[0] + to[2];
}

// 页签条上的 A 收不收（B100 ②）：手柄模式、手上拿着物品、焦点在顶部页签条上、没有模态框 → 收（不换页）
// Whether A on the tab bar is taken (B100 (2)): controller mode, an item in hand, the focus on the top tab bar and no modal → taken (no page switch)
static inline int dm_pad_tab_a_take(int pad, int held, int on_tabs, int modal) { return pad && held && on_tabs && !modal; }
