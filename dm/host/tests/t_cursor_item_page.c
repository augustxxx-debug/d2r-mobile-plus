// 物品格页号 → 容器的单测（dm_cursor_item.h dm_item_container_of_page）。
// 缺陷（11-04 整机验收）：护身符页（页 5）掉进 default 被当成交易格，dm_bag.c 的手柄键位条提示「存入仓库 / 卖出 / 放入魔盒」永远不出。
// 这里钉住：页 5 = 护身符页，与 dm_bag.c 的 CHARM_PAGE 同一个数；0 / 3 / 4 不变；其余仍是交易。
// Unit test of item grid page -> container (dm_item_container_of_page in dm_cursor_item.h).
// Defect (11-04 Mac acceptance): the charm page (page 5) fell into the default case and was taken as a trade grid, so the controller
// legend hints in dm_bag.c ("To stash / Sell / To cube") never appeared. Pinned here: page 5 = charm page, the same number as CHARM_PAGE
// in dm_bag.c; 0 / 3 / 4 unchanged; everything else is still trade.
#include "../dm_cursor_item.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int checks, fails;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)
int main(void) {
    CHECK(dm_item_container_of_page(0) == DM_ITEM_AT_INVENTORY);
    CHECK(dm_item_container_of_page(3) == DM_ITEM_AT_CUBE);
    CHECK(dm_item_container_of_page(4) == DM_ITEM_AT_STASH);
    CHECK(dm_item_container_of_page(5) == DM_ITEM_AT_CHARM);
    CHECK(dm_item_container_of_page(5) != DM_ITEM_AT_TRADE);
    for (int p = 0; p < 256; p++) if (p != 0 && p != 3 && p != 4 && p != 5) CHECK(dm_item_container_of_page(p) == DM_ITEM_AT_TRADE);
    // 护身符页号和 dm_bag.c 写进 grid +0x630 的是同一个数 | The charm page number is the one dm_bag.c writes into grid +0x630
    FILE *f = fopen("../dm_bag.c", "r"); CHECK(f != NULL);
    if (f) { static char s[1 << 20]; size_t n = fread(s, 1, sizeof s - 1, f); fclose(f); s[n] = 0; const char *m = strstr(s, "CHARM_PAGE = "); CHECK(m && atoi(m + 13) == 5);
        // 键位条提示要认护身符页这一类，不能再比 INVENTORY | The legend hint must test for the charm kind, not INVENTORY
        const char *h = strstr(s, "\"dmCharmHintSell\""); const char *c = h ? strstr(h, "ci.container == ") : NULL; CHECK(c && !strncmp(c + 16, "DM_ITEM_AT_CHARM", 16)); }
    printf("t_cursor_item_page: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
