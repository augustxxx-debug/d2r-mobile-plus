// 药水堆叠的数目规则：上限、喝 / 喂佣兵 / 进腰带各剩几瓶、拆分与卖出的三档、并叠封顶、数量框控件名、叠数角标在哪个容器哪一页。
// 全是纯函数，不碰游戏内存、不发包；dm_stack.c 只调这里，单测 dm/host/tests/t_stack_rule.c 测的也是这里，所以改规则只改这一份。
// 上限 99 与数据表工具 dm/ios/tools/stack_potions.py 的 MAX_STACK 必须一致（表改了这里也要改，单测会盯住 99）。
// Potion stack arithmetic: the cap, how many bottles are left after a sip / a mercenary feed / moving a stack into the belt,
// the three split and sell modes, the merge ceiling, the quantity-box widget names, and which container page shows count badges.
// All pure functions: no game memory, no packets. dm_stack.c calls only these, and dm/host/tests/t_stack_rule.c tests the same
// functions, so a rule is changed in one place. The 99 cap must match MAX_STACK in the table tool dm/ios/tools/stack_potions.py.
#pragma once
#include <stdint.h>

// 一叠最多几瓶 | how many bottles a stack holds
#ifndef DMSTK_CAP
#define DMSTK_CAP 99
#endif
static inline int dmstk_cap(void) { return DMSTK_CAP; }

// 原版用掉一瓶（喝、喂佣兵、腰带键）后，原位那一叠剩几瓶：数量 ≥2 才有这一套，剩 n−1；单瓶返回 0 = 不放回
// After the original game uses one bottle (a sip, a mercenary feed, a belt key) how many bottles are left in place: only for 2+
// (n−1); a single bottle returns 0 = nothing to put back
static inline int dmstk_rest_after_one(int n) { return n >= 2 ? n - 1 : 0; }

// 整叠进腰带：腰带那一格留 1 瓶（一瓶一瓶补），其余回背包
// A whole stack into the belt: the belt cell keeps one bottle (the belt fills one at a time) and the rest goes back
static inline int dmstk_belt_keep(void) { return 1; }
static inline int dmstk_belt_back(int n) { return n >= 2 ? n - 1 : 0; }

// 拆分框三档（1 拆 1、2 拆半、3 全部）：返回这一次要拆出几瓶。全部由调用方反复拆 1，这里返回 0；拆半向下取整、至少 1
// Split box, three modes (1 = one, 2 = half, 3 = all): how many bottles this step splits off. All is split one at a time by the
// caller, so 0 here; half rounds down and is at least 1
static inline int dmstk_split_amount(int n, int mode) {
    if (n < 2) return 0;
    if (mode == 1) return 1;
    if (mode == 2) return n / 2 > 1 ? n / 2 : 1;
    return 0;
}

// 卖出三档（1 卖 1、2 卖半、3 卖全部）：返回先拆出几瓶再卖那件。卖全部 = 整叠卖 n 瓶、不拆（拆了会多出一件空叠）
// Sell, three modes (1 = one, 2 = half, 3 = all): how many bottles to split off and sell as their own item. All = sell the whole
// stack of n with no split (splitting would leave a remainder item behind)
static inline int dmstk_sell_amount(int n, int mode) {
    if (n < 1) return 0;
    if (mode == 1) return n >= 2 ? 1 : 0;
    if (mode == 2) return n >= 2 ? (n / 2 > 1 ? n / 2 : 1) : 0;
    if (mode == 3) return n;
    return 0;
}

// 并叠：这一叠还能吃几瓶、这一拍实际并进去几瓶（封顶 99，多的留原叠）
// Merging: how many bottles this stack still takes, and how many actually go in this pass (capped at 99, the rest stays put)
static inline int dmstk_merge_room(int held) { return held >= DMSTK_CAP ? 0 : DMSTK_CAP - held; }
static inline int dmstk_merge_take(int held, int add) { int room = dmstk_merge_room(held); return add < room ? add : room; }
static inline int dmstk_merge_fits(int a, int b) { return a + b <= DMSTK_CAP; }

// 方块里只有一叠：3 瓶小紫留给「3 小紫 → 大紫」的配方不拆，其余 ≥2 瓶拆半。返回 1 = 拆
// A lone stack in the cube: 3 small rejuvenation potions are left for the 3 rvs -> rvl recipe, anything else of 2+ splits in
// half. Returns 1 = split
static inline int dmstk_cube_lone_split(int qty, int small_rejuv) { return qty >= 2 && !(qty == 3 && small_rejuv); }

// 数量框的六个按钮：返回档位（1 一、2 半、3 全部），不是按钮返回 0；dmstk_box_name 反过来给出控件名
// （布局 pouch_layout.py 里就是这六个名字；界面按名字找控件，写错一个字就点不到）
// The six quantity-box buttons: the mode (1 one, 2 half, 3 all), 0 when it is not a button; dmstk_box_name gives the widget name
// back (these six names are in the layout pouch_layout.py; the UI finds widgets by name, so one wrong letter and the tap misses)
// 三组：0 拆分、1 卖出、2 买入（买入组 11-04 加，用户要求进 RC2）
// Three sets: 0 split, 1 sell, 2 buy (the buy set was added 11-04 at the user's request for RC2)
enum { DMSTK_SET_SPLIT, DMSTK_SET_SELL, DMSTK_SET_BUY };
static inline int dmstk_box_choice(const char *name) {
    if (!name) return 0;
    if (!__builtin_strcmp(name, "TouchQtyOne") || !__builtin_strcmp(name, "TouchQtySellOne") || !__builtin_strcmp(name, "TouchQtyBuyOne")) return 1;
    if (!__builtin_strcmp(name, "TouchQtyHalf") || !__builtin_strcmp(name, "TouchQtySellHalf") || !__builtin_strcmp(name, "TouchQtyBuyTen")) return 2;
    if (!__builtin_strcmp(name, "TouchQtyAll") || !__builtin_strcmp(name, "TouchQtySellAll") || !__builtin_strcmp(name, "TouchQtyBuyFull")) return 3;
    return 0;
}
static inline const char *dmstk_box_name(int set, int mode) {
    if (set == DMSTK_SET_BUY) return mode == 1 ? "TouchQtyBuyOne" : mode == 2 ? "TouchQtyBuyTen" : mode == 3 ? "TouchQtyBuyFull" : 0;
    if (set) return mode == 1 ? "TouchQtySellOne" : mode == 2 ? "TouchQtySellHalf" : mode == 3 ? "TouchQtySellAll" : 0;
    return mode == 1 ? "TouchQtyOne" : mode == 2 ? "TouchQtyHalf" : mode == 3 ? "TouchQtyAll" : 0;
}
// 三档的手柄键帽控件（布局 pouch_layout.py QTY_KEYS：原版提示控件按当前手柄出图），第 1 / 2 / 3 档 = A / Y / X 键位
// The controller keycap widgets of the three modes (layout QTY_KEYS: the original legend widget draws the glyph for the current controller), modes 1 / 2 / 3 = A / Y / X
// 键帽样式与位置：暂定，等「按钮 + 手柄统一设计规范」（会话 ui-pad-spec，参照原版）定稿后统一改这里与 pouch_layout.py 的 QTY_KEY_* 两处。
// 原版做法：键帽在文字左边、不加括号。位置单位 = 布局单位，相对数量框左上；行距跟按钮行（80）
// Keycap style and position: provisional until the unified button + controller spec (session ui-pad-spec, modelled on the original) is final; change here and
// QTY_KEY_* in pouch_layout.py. As in the original: keycap left of the text, no brackets. Layout units relative to the box top-left; row pitch follows the buttons (80)
enum { DMSTK_KEY_DX = 60, DMSTK_KEY_DY = 4 };
static inline const char *dmstk_key_name(int mode) { return mode == 1 ? "TouchQtyKeyA" : mode == 2 ? "TouchQtyKeyY" : mode == 3 ? "TouchQtyKeyX" : 0; }
// 买入三档：1 瓶 / 10 瓶 / 买满一叠 99 瓶。原版先照常买 1 瓶（原版自己查钱、查空位、查商人），单价 = 这一瓶前后金币差；
// 其余的按身上剩下的金币能买几瓶封顶，总数不超过一叠上限。返回总瓶数（含原版那 1 瓶）；单价认不出时只算原版那 1 瓶
// Buy, three modes: 1 / 10 / a full stack of 99. The original buys 1 bottle first as usual (it checks gold, room and the vendor itself) and the unit
// price is the gold difference of that bottle; the rest is capped by how many the remaining carried gold pays for, the total never above the stack cap.
// Returns the total bottles (including the original one); with an unknown unit price only the original bottle counts
// 买入结果的提示（11-04 用户问超过 99 怎么办）：0 不提示；1 背包没空位买不进；2 金币不够一瓶；3 金币只够买一部分；4 背包放不下其余、只买了一部分
// Buy result notice: 0 none; 1 no room, nothing bought; 2 not enough gold for one; 3 gold paid for only part; 4 no room for the rest, only part bought
enum { DMSTK_BUYNOTE_NONE, DMSTK_BUYNOTE_NOROOM, DMSTK_BUYNOTE_NOGOLD, DMSTK_BUYNOTE_GOLD_PART, DMSTK_BUYNOTE_ROOM_PART };
static inline int dmstk_buy_note(int bought_first, int room_before, int want, int total, int put, int extra) {
    if (!bought_first) return room_before ? DMSTK_BUYNOTE_NOGOLD : DMSTK_BUYNOTE_NOROOM;
    if (put < extra) return DMSTK_BUYNOTE_ROOM_PART;
    if (total < want) return DMSTK_BUYNOTE_GOLD_PART;
    return DMSTK_BUYNOTE_NONE;
}
static inline int dmstk_buy_want(int mode) { return mode == 1 ? 1 : mode == 2 ? 10 : mode == 3 ? DMSTK_CAP : 0; }
static inline int dmstk_buy_total(int want, int gold_after_first, int unit) {
    if (want < 1) return 0; if (want > DMSTK_CAP) want = DMSTK_CAP; if (unit <= 0 || gold_after_first < 0) return 1;
    long more = gold_after_first / unit; int t = 1 + (int)(more > DMSTK_CAP ? DMSTK_CAP : more); return t < want ? t : want;
}

// 叠数角标画在哪个容器：物品栏 = 页 0，个人仓库 = 页 4（存档里个人仓库的当前页就放在页 4），别的容器不画
// Which container gets count badges: the inventory = page 0, the personal stash = page 4 (the stash's current page lives on page 4
// in the save); no other container
enum { DMSTK_PANEL_INVENTORY, DMSTK_PANEL_STASH, DMSTK_PANEL_OTHER };
enum { DMSTK_PAGE_INVENTORY = 0, DMSTK_PAGE_STASH = 4 };
static inline int dmstk_badge_page(int panel) {
    return panel == DMSTK_PANEL_INVENTORY ? DMSTK_PAGE_INVENTORY : panel == DMSTK_PANEL_STASH ? DMSTK_PAGE_STASH : -1;
}
// 这一格要不要画角标：容器页对上、且是方式 0（背包 / 仓库里的物品，不是腰带 / 装备位） | whether this cell gets a badge
static inline int dmstk_badges_on(int panel, int page) { int p = dmstk_badge_page(panel); return p >= 0 && page == p; }

// 角标最多摆几格 = 布局里的控件数（wide_stash.py 与 pouch_layout.py 各生成 24 个 TouchQtyS00..S23 / N00..N23）；超出的叠不画角标。
// Badges at most this many cells = the widget count in the layouts (wide_stash.py and pouch_layout.py each
// generate 24 TouchQtyS00..S23 / N00..N23). Stacks beyond that get no badge.
#define DMSTK_BADGE_MAX 24

// 角标计划：扫物品时把要摆的格子记下来，界面照着摆、收起循环从 shown 之后开始、日志报 logged。
// 计数只有 shown 这一份：收起循环不能再数一遍（那一路走到 DMSTK_BADGE_MAX，0 / 1 / 2 格都会记成 24 格，审计 P2-01）。
// Badge plan: record the cells to place while scanning; the UI places them, the hide loop starts after
// `shown`, and the log reports `logged`. There is exactly one counter, `shown`: the hide loop must not count
// again (that counter reaches DMSTK_BADGE_MAX, so 0 / 1 / 2 cells were all logged as 24, audit P2-01).
typedef struct { int q, x, y; } dmstk_badge;              // 数量与屏幕坐标 | count and screen position
typedef struct { int shown; dmstk_badge cell[DMSTK_BADGE_MAX]; } dmstk_badge_plan;

static inline void dmstk_badge_reset(dmstk_badge_plan *p) { p->shown = 0; }
// 摆一格：一叠 ≥2 瓶才有角标，最多 DMSTK_BADGE_MAX 格。返回 1 = 摆了（数量 / 坐标记进计划），0 = 不摆（单瓶或已满）
// Place one cell: only a stack of 2+ gets a badge and at most DMSTK_BADGE_MAX of them. 1 = placed (count and
// position recorded), 0 = nothing (single bottle or full)
static inline int dmstk_badge_add(dmstk_badge_plan *p, int q, int x, int y) {
    if (!p || q < 2 || p->shown >= DMSTK_BADGE_MAX) return 0;
    p->cell[p->shown].q = q; p->cell[p->shown].x = x; p->cell[p->shown].y = y; p->shown++;
    return 1;
}
// 收起循环的起点 = 实际摆出来的格数；还要收起的格数 = MAX − shown
// Where the hide loop starts = the cells actually placed; how many are still to put away = MAX - shown
static inline int dmstk_badge_hide_from(const dmstk_badge_plan *p) { return p->shown; }
static inline int dmstk_badge_hidden(const dmstk_badge_plan *p) { return DMSTK_BADGE_MAX - p->shown; }
// 日志里的格数：只能是 shown | the cell count in the log line: `shown` and nothing else
static inline int dmstk_badge_logged(const dmstk_badge_plan *p) { return p->shown; }

// 数量框吃键：框开着时 A / B / X / Y 都不给游戏；被框吃过的键要一直扣到松开为止。
// 原先只在框开着的那几帧扣：A / Y 选了档、框当帧收起，键还按着，下一帧游戏把它当成新按下——A 把剩下那叠拿到手上，
// Y 走原版 Y（商店里那一下发 0x5c），剩下的半叠没了、读档也找不回（11-04 整机验收 pvy1 / pvyh2）。
// 返回这一帧要从按键里去掉的位；*eaten 是还按着、被框吃过的键。
// Quantity box key eating: while the box is open A / B / X / Y do not reach the game, and a key the box consumed stays withheld until it is released.
// It used to be withheld only while the box was open: A / Y chose a mode, the box closed that frame, the key was still down and the next frame the game took it
// as a new press - A picked up the rest of the stack, Y ran the original Y (0x5c in a shop) and the remaining half was gone, also after reloading (11-04 Mac
// acceptance pvy1 / pvyh2). Returns the bits to clear this frame; *eaten holds the keys the box consumed that are still down.
static inline uint64_t dmstk_box_eat(uint64_t *eaten, uint64_t held, int box_open, uint64_t keys) {
    if (box_open) *eaten |= held & keys;
    *eaten &= held;
    return box_open ? keys : *eaten;
}
