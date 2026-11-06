// B163 读档后「看不见的手上物品」纯函数单测（dm_cursor_rescue.h）。
// 缺陷：存盘时手上的物品读档后挂在服务端光标上（inv + 0x40），客户端手上被清空，两端一直不一致，玩家看不见、拿不到。
// 这里钉住：两端不一致满 50 拍才触发；中途客户端有了手上物品就清零；服务端空不触发；触发后 streak 归零；
// 0x18 放回包和 0x17 丢下包的字节逐个核对。
// Unit test of the B163 "invisible held item" pure helpers (dm_cursor_rescue.h).
// Defect: an item held at save time hangs on the server cursor after loading (inv + 0x40) while the client hand is cleared, the two sides
// stay inconsistent, and the player can neither see nor take it. Pinned here: triggers only after 50 inconsistent ticks; a client-held item
// clears the streak; an empty server does not trigger; the streak resets after a trigger; the 0x18 put-back and 0x17 drop packets are
// checked byte by byte.
#include "../dm_cursor_rescue.h"
#include <stdio.h>
#include <string.h>
static int checks, fails;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)
int main(void) {
    int s = 0;
    // 1. 连续 49 拍不触发，第 50 拍触发，触发后 streak 归零
    for (int i = 0; i < 49; i++) CHECK(dm_cursor_rescue_due(1, 0, &s) == 0);
    CHECK(s == 49);
    CHECK(dm_cursor_rescue_due(1, 0, &s) == 1);
    CHECK(s == 0);
    // 2. 中途客户端有了手上物品就清零
    s = 0;
    for (int i = 0; i < 10; i++) dm_cursor_rescue_due(1, 0, &s);
    CHECK(s == 10);
    CHECK(dm_cursor_rescue_due(1, 1, &s) == 0);
    CHECK(s == 0);
    for (int i = 0; i < 49; i++) CHECK(dm_cursor_rescue_due(1, 0, &s) == 0);
    CHECK(dm_cursor_rescue_due(1, 0, &s) == 1);
    // 3. 服务端空则不触发；半截 streak 也被清掉
    s = 0;
    for (int i = 0; i < 100; i++) CHECK(dm_cursor_rescue_due(0, 0, &s) == 0);
    CHECK(s == 0);
    for (int i = 0; i < 25; i++) dm_cursor_rescue_due(1, 0, &s);
    CHECK(s == 25);
    CHECK(dm_cursor_rescue_due(0, 0, &s) == 0);
    CHECK(s == 0);
    // 4. 触发后从零重数：再 49 拍不触发，第 50 拍再触发
    for (int i = 0; i < 50; i++) dm_cursor_rescue_due(1, 0, &s);
    CHECK(s == 0);
    for (int i = 0; i < 49; i++) CHECK(dm_cursor_rescue_due(1, 0, &s) == 0);
    CHECK(dm_cursor_rescue_due(1, 0, &s) == 1);
    // 5. 0x18 放回包：18 [id][x][y][页]，4 字节小端，页 = 0
    uint8_t pk[17]; memset(pk, 0xaa, sizeof pk);
    dm_cursor_rescue_place_pkt(pk, 0x11223344u, 7, 13);
    static const uint8_t want[17] = { 0x18, 0x44, 0x33, 0x22, 0x11, 0x07, 0, 0, 0, 0x0d, 0, 0, 0, 0, 0, 0, 0 };
    CHECK(!memcmp(pk, want, sizeof want));
    CHECK(pk[13] == 0 && pk[14] == 0 && pk[15] == 0 && pk[16] == 0);
    // 6. 0x17 丢下包：17 [id]（依据真实记录 "1726000000"）
    uint8_t dp[5]; memset(dp, 0xaa, sizeof dp);
    dm_cursor_rescue_drop_pkt(dp, 0x26);
    static const uint8_t dwant[5] = { 0x17, 0x26, 0, 0, 0 };
    CHECK(!memcmp(dp, dwant, sizeof dwant));
    CHECK(DM_CURSOR_RESCUE_TICKS == 50);
    // 7. B205 救回去处：护身符（1）× 护身符页有 / 无空位 × 背包页有 / 无空位，8 种组合逐个核对
    //    B205 rescue destination: charm (1) x charm room yes/no x bag room yes/no, all 8 combinations checked one by one
    CHECK(dm_cursor_rescue_plan(1, 1, 1) == DM_RESCUE_CHARM);
    CHECK(dm_cursor_rescue_plan(1, 1, 0) == DM_RESCUE_CHARM);
    CHECK(dm_cursor_rescue_plan(1, 0, 1) == DM_RESCUE_BAG);
    CHECK(dm_cursor_rescue_plan(1, 0, 0) == DM_RESCUE_DROP);
    CHECK(dm_cursor_rescue_plan(0, 1, 1) == DM_RESCUE_BAG);
    CHECK(dm_cursor_rescue_plan(0, 1, 0) == DM_RESCUE_DROP);
    CHECK(dm_cursor_rescue_plan(0, 0, 1) == DM_RESCUE_BAG);
    CHECK(dm_cursor_rescue_plan(0, 0, 0) == DM_RESCUE_DROP);
    // 8. 非护身符时护身符页有没有空位都不影响结果
    //    For a non-charm, whether the charm page has room does not change the result
    for (int bag = 0; bag <= 1; bag++) CHECK(dm_cursor_rescue_plan(0, 0, bag) == dm_cursor_rescue_plan(0, 1, bag));
    CHECK(DM_RESCUE_CHARM != DM_RESCUE_BAG && DM_RESCUE_BAG != DM_RESCUE_DROP && DM_RESCUE_CHARM != DM_RESCUE_DROP);
    // 9. 光标残留：模式 0 是残留在容器里的指针，不动手；光标上 / 地上都不算
    //    Stale cursor: mode 0 is a pointer left inside a container, do not act; on the cursor / on the ground are not stale
    CHECK(dm_cursor_rescue_stale(0) == 1);
    CHECK(dm_cursor_rescue_stale(4) == 0);
    CHECK(dm_cursor_rescue_stale(3) == 0);
    printf("t_cursor_rescue: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
