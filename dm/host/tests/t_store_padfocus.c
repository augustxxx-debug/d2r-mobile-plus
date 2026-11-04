// 「存放」按钮手柄焦点进出的纯函数单测（秒级）：dm_store_nav.h，不碰游戏。期望值都从规则手算。
// Unit test of the pure functions for controller focus in / out of the 「存放」(Deposit) button (takes seconds): dm_store_nav.h, no game.
// Expected values are all computed by hand from the rules.
#include <stdio.h>
#include "../dm_store_nav.h"
static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("  ✗ "); printf(__VA_ARGS__); printf("\n"); } } while (0)
int main(void) {
    // 网格：左上 (100,600)，每格 100×100，12 列 7 行（最下一行 y 1200–1299）；按钮在网格下方 (900,1320) 300×60，中心 (1050,1350)
    // Grid: top-left (100,600), cells 100×100, 12 columns 7 rows (last row y 1200-1299); button below the grid, (900,1320) 300×60, centre (1050,1350)
    dm_store_grid g = { { 100, 600, 1200, 700 }, 100, 7 };
    dm_store_rect b = { 900, 1320, 300, 60 };
    int x, y;
    // 1) 最下一行 + x 在按钮水平范围 → 接管 ↓，给按钮中心 | last row + x within the button -> take Down, give the button centre
    CHECK(dm_store_down_target(&g, &b, 1000, 1250, &x, &y) == 1 && x == 1050 && y == 1350, "最下一行 + 按钮上方应接管 ↓ 并给按钮中心，得到 %d,%d", x, y);
    CHECK(dm_store_down_target(&g, &b, 900, 1299, &x, &y) == 1, "按钮左沿、最下一行下沿应算在范围内");
    CHECK(dm_store_down_target(&g, &b, 1199, 1200, &x, &y) == 1, "按钮右沿内 1 像素、最下一行上沿应算在范围内");
    // 2) 不在最下一行 / x 不在按钮范围 / 不在网格 → 不接管 | not the last row / x outside the button / outside the grid -> no takeover
    CHECK(dm_store_down_target(&g, &b, 1000, 1199, &x, &y) == 0, "倒数第二行不接管 ↓");
    CHECK(dm_store_down_target(&g, &b, 1000, 1300, &x, &y) == 0, "网格下沿外不接管 ↓");
    CHECK(dm_store_down_target(&g, &b, 700, 1250, &x, &y) == 0, "x 在按钮左边不接管 ↓");
    CHECK(dm_store_down_target(&g, &b, 1200, 1250, &x, &y) == 0, "x 在按钮右沿外不接管 ↓");
    CHECK(dm_store_down_target(&g, &b, 1000, 500, &x, &y) == 0, "网格上方不接管 ↓");
    // 3) 从按钮 ↑ 回到进来那一格 | Up from the button returns to the cell we came from
    dm_store_back(&g, &b, 1, 350, 1250, &x, &y);
    CHECK(x == 350 && y == 1250, "有记录应回到进来那一格，得到 %d,%d", x, y);
    dm_store_back(&g, &b, 1, 50, 1250, &x, &y);   // 记录不在网格里 = 没有记录 | a record outside the grid counts as none
    CHECK(x == 1050 && y == 1250, "记录在网格外应按无记录处理，得到 %d,%d", x, y);
    // 4) 没有记录 → 按钮正上方那格（列按按钮中心，夹在网格内）| no record -> the cell above the button, column from the button centre, clamped
    dm_store_back(&g, &b, 0, 0, 0, &x, &y);
    CHECK(x == 1050 && y == 1250, "无记录应回按钮正上方那格，得到 %d,%d", x, y);
    dm_store_rect br = { 1400, 1320, 200, 60 };   // 中心 x 1500，在网格右边 1300 外 | centre x 1500, right of the grid edge 1300
    dm_store_back(&g, &br, 0, 0, 0, &x, &y);
    CHECK(x == 1250 && y == 1250, "按钮在网格右边应夹到最后一列，得到 %d,%d", x, y);
    dm_store_rect bl = { 0, 1320, 100, 60 };      // 中心 x 50，在网格左边 100 外 | centre x 50, left of the grid edge 100
    dm_store_back(&g, &bl, 0, 0, 0, &x, &y);
    CHECK(x == 150 && y == 1250, "按钮在网格左边应夹到第一列，得到 %d,%d", x, y);
    printf("t_store_padfocus: %d 项，%d 失败\n", checks, fails); return fails != 0;
}
