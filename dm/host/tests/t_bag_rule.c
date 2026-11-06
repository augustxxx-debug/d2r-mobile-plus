// 「护身符页满了」提示规则的单测（../dm_bag_rule.h dmbag_full_notice；dm_bag.c 服务端每拍调这一份）。
// 场景按 11-04 整机验收：f2 溢出 2 件 → 提示一次；f3b 页满、背包页已躺着溢出的，再捡一件 → 要再提示（原先不提示）。
// Unit test of the "charm page is full" notice rule (dmbag_full_notice in ../dm_bag_rule.h, called by dm_bag.c every server tick).
// Scenarios from the 11-04 Mac acceptance: f2 overflows by 2 -> one notice; f3b page full with overflow already on the bag page, one more picked up -> must notify again (it did not).
#include "../dm_bag_rule.h"
#include <stdio.h>
static int checks, fails;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)
int main(void) {
    int last = -1;
    // 进局：背包页已有 2 件溢出、页 5 满 → 提示一次，之后每拍不重复 | Entering with 2 overflow charms and page 5 full -> one notice, not repeated every tick
    CHECK(dmbag_full_notice(&last, 2, 1) == 1);
    for (int t = 0; t < 100; t++) CHECK(dmbag_full_notice(&last, 2, 1) == 0);
    // 再捡一件（f3b）→ 再提示 | One more picked up (f3b) -> notify again
    CHECK(dmbag_full_notice(&last, 3, 1) == 1);
    CHECK(dmbag_full_notice(&last, 3, 1) == 0);
    // 卖掉 / 扔掉一件：少了不提示；再捡回来到 3 件要提示 | One sold / dropped: fewer gives no notice; back to 3 notifies
    CHECK(dmbag_full_notice(&last, 2, 1) == 0);
    CHECK(dmbag_full_notice(&last, 3, 1) == 1);
    // 页 5 腾出位：搬动的那几拍不提示；再满时（剩 1 件）提示 | Room on page 5: no notice while moving; full again with 1 left notifies
    CHECK(dmbag_full_notice(&last, 3, 0) == 0);
    CHECK(dmbag_full_notice(&last, 2, 0) == 0);
    CHECK(dmbag_full_notice(&last, 1, 1) == 1);
    CHECK(dmbag_full_notice(&last, 1, 1) == 0);
    // 一件一件填满（f1 / f2 的搬动过程）：每拍都有空位就一次都不提示 | Filling one per tick (f1 / f2) with room every tick: never notifies
    last = -1; for (int nf = 64; nf > 0; nf--) CHECK(dmbag_full_notice(&last, nf, 0) == 0);
    printf("t_bag_rule: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
