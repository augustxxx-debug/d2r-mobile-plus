// 药水堆叠的数目规则单测：上限、喝 / 喂 / 进腰带各剩几瓶、拆分与卖出的三档、并叠封顶、数量框控件名、角标页。
// 规则本体在 ../dm_stack_rule.h，dm_stack.c 只用那一份，所以这里测的就是跑游戏时用的算术。
// Unit test of the potion stack arithmetic: the cap, how many bottles are left after a sip / a merc feed / moving into the belt,
// the three split and sell modes, the merge ceiling, the quantity-box widget names and the badge pages.
// The rules live in ../dm_stack_rule.h and dm_stack.c uses that one copy, so this tests the same arithmetic the game runs.
#include "../dm_stack_rule.h"
#include <stdio.h>
#include <string.h>
static int checks, fails;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)
int main(void) {
    // 上限：和数据表 dm/ios/tools/stack_potions.py 的 MAX_STACK 是同一个数（改了就要两边一起改） | Cap: the same number as MAX_STACK in the table tool
    CHECK(DMSTK_CAP == 99);
    CHECK(dmstk_cap() == 99);

    // 喝 / 喂一瓶：数量 ≥2 的一叠用掉一瓶后，原叠剩 n−1；单瓶（<2）不归这里管，返回 0 = 什么都不放回
    // 下面几个数来自实跑：腰带里 5 瓶喝一瓶 → 放回 4 瓶（t0/beltq「放回 hp2×4，方式 2」）
    // One sip / one merc feed: a stack of 2+ leaves n-1; a single bottle (below 2) is not this path, 0 = nothing to put back
    // These numbers come from real runs: a 5-bottle belt stack after one sip is put back as 4 (t0/beltq, "put back hp2×4, mode 2")
    CHECK(dmstk_rest_after_one(0) == 0);
    CHECK(dmstk_rest_after_one(1) == 0);
    CHECK(dmstk_rest_after_one(2) == 1);
    CHECK(dmstk_rest_after_one(3) == 2);
    CHECK(dmstk_rest_after_one(5) == 4);
    CHECK(dmstk_rest_after_one(99) == 98);

    // 进腰带：腰带那一格留 1 瓶，其余回背包；单瓶不拆
    // Into the belt: the belt cell keeps one bottle and the rest goes back to the inventory; a single bottle is not split
    CHECK(dmstk_belt_keep() == 1);
    CHECK(dmstk_belt_back(1) == 0);
    CHECK(dmstk_belt_back(2) == 1);
    CHECK(dmstk_belt_back(12) == 11);
    CHECK(dmstk_belt_keep() + dmstk_belt_back(12) == 12);   // 不吞不造 | neither swallows nor creates

    // 拆分框三档：拆 1 / 拆半 / 全部（全部由调用方反复拆 1，拆半向下取整且至少 1）
    // Split box, three modes: one / half / all (all is split one at a time by the caller; half rounds down and is at least 1)
    CHECK(dmstk_split_amount(0, 1) == 0);
    CHECK(dmstk_split_amount(1, 1) == 0);   // 单瓶不开框 | a single bottle never opens the box
    CHECK(dmstk_split_amount(12, 1) == 1);
    CHECK(dmstk_split_amount(12, 2) == 6);
    CHECK(dmstk_split_amount(3, 2) == 1);
    CHECK(dmstk_split_amount(2, 2) == 1);
    CHECK(dmstk_split_amount(12, 3) == 0);   // 全部：不在这里算 | all: not computed here
    CHECK(dmstk_split_amount(0, 3) == 0);

    // 卖出三档：卖 1 / 卖半 / 卖全部。前三档先拆出这么多瓶再卖拆出来的那件；卖全部整叠卖、不拆
    // Sell, three modes: 1 / half / all. The first two split that many off and sell the split-off item; all sells the whole stack, no split
    CHECK(dmstk_sell_amount(1, 1) == 0);
    CHECK(dmstk_sell_amount(12, 1) == 1);    // 12 瓶卖 1：拆出 1，原叠剩 11（t0/sb1） | sell 1 of 12: one split off, 11 left (t0/sb1)
    CHECK(dmstk_sell_amount(12, 2) == 6);    // 12 瓶卖半：拆出 6，原叠剩 6（t0/sbh） | half of 12: six split off, six left (t0/sbh)
    CHECK(dmstk_sell_amount(3, 2) == 1);
    CHECK(dmstk_sell_amount(12, 3) == 12);   // 整叠 | the whole stack
    CHECK(dmstk_sell_amount(0, 3) == 0);
    CHECK(dmstk_sell_amount(12, 9) == 0);    // 未知档不拆 | an unknown mode splits nothing

    // 并叠封顶：一叠最多 99，多的留原叠
    // Merge ceiling: a stack holds at most 99 and the rest stays where it is
    CHECK(dmstk_merge_room(0) == 99);
    CHECK(dmstk_merge_room(98) == 1);
    CHECK(dmstk_merge_room(99) == 0);
    CHECK(dmstk_merge_room(120) == 0);
    CHECK(dmstk_merge_take(0, 12) == 12);
    CHECK(dmstk_merge_take(90, 12) == 9);    // 90 + 9 = 99 | 90 + 9 = 99
    CHECK(dmstk_merge_take(99, 12) == 0);
    CHECK(dmstk_merge_take(50, 0) == 0);
    CHECK(dmstk_merge_fits(50, 49) == 1);
    CHECK(dmstk_merge_fits(50, 50) == 0);

    // 方块里只有一叠：3 瓶小紫留给「3 小紫 → 大紫」的配方，其余拆半
    // A lone stack in the cube: 3 small rejuvenation potions are left for the 3 rvs -> rvl recipe, anything else splits in half
    CHECK(dmstk_cube_lone_split(2, 0) == 1);
    CHECK(dmstk_cube_lone_split(3, 0) == 1);
    CHECK(dmstk_cube_lone_split(3, 1) == 0);
    CHECK(dmstk_cube_lone_split(4, 1) == 1);
    CHECK(dmstk_cube_lone_split(1, 0) == 0);

    // 数量框控件名 → 档位 / 是不是卖出那一组（界面按名字找控件，写错一个字就点不到）
    // Quantity-box widget name -> mode / whether it is the sell set (the UI finds widgets by name; one wrong letter and the tap misses)
    CHECK(dmstk_box_choice("TouchQtyOne") == 1);
    CHECK(dmstk_box_choice("TouchQtyHalf") == 2);
    CHECK(dmstk_box_choice("TouchQtyAll") == 3);
    CHECK(dmstk_box_choice("TouchQtySellOne") == 1);
    CHECK(dmstk_box_choice("TouchQtySellHalf") == 2);
    CHECK(dmstk_box_choice("TouchQtySellAll") == 3);
    CHECK(dmstk_box_choice("TouchQtyTitle") == 0);   // 标题不是按钮 | the title is not a button
    CHECK(dmstk_box_choice("TouchQtyBg") == 0);
    CHECK(dmstk_box_choice("TouchQtyN00") == 0);     // 格子上的数字 | the digits on the cells
    CHECK(dmstk_box_choice("TouchQtyS00") == 0);
    CHECK(dmstk_box_choice("") == 0);
    CHECK(dmstk_box_choice(NULL) == 0);
    // 反查控件名（界面按名字找控件）与来回一致 | the name lookup (the UI finds widgets by name) and the round trip
    CHECK(!strcmp(dmstk_box_name(0, 1), "TouchQtyOne"));
    CHECK(!strcmp(dmstk_box_name(0, 2), "TouchQtyHalf"));
    CHECK(!strcmp(dmstk_box_name(0, 3), "TouchQtyAll"));
    CHECK(!strcmp(dmstk_box_name(1, 1), "TouchQtySellOne"));
    CHECK(!strcmp(dmstk_box_name(1, 2), "TouchQtySellHalf"));
    CHECK(!strcmp(dmstk_box_name(1, 3), "TouchQtySellAll"));
    CHECK(dmstk_box_name(0, 0) == NULL && dmstk_box_name(1, 4) == NULL);
    for (int set = 0; set < 2; set++) for (int m = 1; m <= 3; m++) CHECK(dmstk_box_choice(dmstk_box_name(set, m)) == m);

    // 叠数角标：物品栏在页 0，个人仓库在页 4；别的容器（共享页、高级仓库材料页、方块）不画
    // Count badges: page 0 of the inventory and page 4 of the personal stash; no other container (shared page, materials page, cube)
    CHECK(dmstk_badge_page(DMSTK_PANEL_INVENTORY) == 0);
    CHECK(dmstk_badge_page(DMSTK_PANEL_STASH) == 4);
    CHECK(dmstk_badge_page(DMSTK_PANEL_OTHER) == -1);
    CHECK(dmstk_badge_page(DMSTK_PANEL_INVENTORY) != dmstk_badge_page(DMSTK_PANEL_STASH));
    CHECK(dmstk_badges_on(DMSTK_PANEL_INVENTORY, 0) == 1);
    CHECK(dmstk_badges_on(DMSTK_PANEL_INVENTORY, 4) == 0);
    CHECK(dmstk_badges_on(DMSTK_PANEL_STASH, 4) == 1);
    CHECK(dmstk_badges_on(DMSTK_PANEL_STASH, 0) == 0);
    CHECK(dmstk_badges_on(DMSTK_PANEL_OTHER, 0) == 0);

    // 仓库角标的格数：日志那一行只能报实际摆出来的格数。审计反例是收起循环把同一路计数推到 24，
    // 于是 0 / 1 / 2 格都记成「24 格」（dm_stack.c:279-286，HEAD 9353e58）。
    // 这里按 dm_stack.c 的用法走：边扫边 add，收起从 shown 开始，日志取 logged。
    // Cell count of the stash badges: the log line may only report the cells actually placed. The audit's
    // counterexample is the hide loop pushing the same counter to 24, so 0 / 1 / 2 cells were all logged as
    // "24 cells" (dm_stack.c:279-286, HEAD 9353e58). Here it is driven the way dm_stack.c drives it: add while
    // scanning, hide from shown, log via logged.
    {
        dmstk_badge_plan p;
        dmstk_badge_reset(&p);

        // 0 格：一件叠都没有，日志「0 格」，24 个控件全收起
        // 0 cells: no stack at all, the log says 0 and all 24 widgets are put away
        CHECK(DMSTK_BADGE_MAX == 24);          // 布局里就是 24 个 TouchQtyS00..S23 / N00..N23
        CHECK(dmstk_badge_logged(&p) == 0);
        CHECK(dmstk_badge_hide_from(&p) == 0);
        CHECK(dmstk_badge_hidden(&p) == 24);

        // 1 格：12 瓶那一叠摆一格，日志「1 格 12」（审计反例：HEAD 记 24 格 12），收起 23 格
        // 1 cell: the 12-bottle stack places one cell, the log says 1 (HEAD logged 24), 23 widgets are put away
        CHECK(dmstk_badge_add(&p, 12, 3, 4) == 1);
        CHECK(p.shown == 1 && p.cell[0].q == 12 && p.cell[0].x == 3 && p.cell[0].y == 4);
        CHECK(dmstk_badge_logged(&p) == 1);
        CHECK(dmstk_badge_hide_from(&p) == 1);
        CHECK(dmstk_badge_hidden(&p) == 23);

        // 单瓶 / 空叠不摆角标，也不占格 | a single bottle or an empty stack places nothing and takes no cell
        CHECK(dmstk_badge_add(&p, 1, 0, 0) == 0);
        CHECK(dmstk_badge_add(&p, 0, 9, 9) == 0);
        CHECK(p.shown == 1);

        // 收起时序：照 dm_stack.c 的收起循环从 shown 走到 23，走完计数是 24，但日志的格数仍是 1
        // Hide timing: run the hide loop from shown to 23 the way dm_stack.c does; the loop counter ends at 24
        // while the logged cell count stays 1
        int i, hidden = 0;
        for (i = dmstk_badge_hide_from(&p); i < DMSTK_BADGE_MAX; i++) hidden++;
        CHECK(hidden == 23 && i == 24);
        CHECK(dmstk_badge_logged(&p) == 1);    // 按收起循环的计数记就是审计的恒 24 | logging the loop counter is the audit's constant 24
        CHECK(i != dmstk_badge_logged(&p));

        // 24 格满：布局只有 24 个控件，第 25 件叠不再摆；日志 24 格、收起 0 格
        // 24 cells full: the layout only has 24 widgets so the 25th stack places nothing; log 24, nothing hidden
        dmstk_badge_reset(&p);
        int placed = 0;
        for (int q = 0; q < 30; q++) placed += dmstk_badge_add(&p, 5 + q, q % 12, q / 12);
        CHECK(placed == 24 && p.shown == 24);
        CHECK(dmstk_badge_logged(&p) == 24);
        CHECK(dmstk_badge_hide_from(&p) == 24);
        CHECK(dmstk_badge_hidden(&p) == 0);
        // 摆的顺序就是扫描顺序：第 k 格是第 k 个 ≥2 瓶的叠 | placement order is scan order
        CHECK(p.cell[0].q == 5 && p.cell[0].x == 0 && p.cell[0].y == 0);
        CHECK(p.cell[23].q == 28 && p.cell[23].x == 11 && p.cell[23].y == 1);
    }

    printf("t_stack_rule: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
