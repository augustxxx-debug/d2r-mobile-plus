// 左摇杆 → 方向键（dm_stick_nav.h，RC1.1 B006b）：推过一半算按下、回到四分之一以内才松开（回滞）、只取主轴、上为正；按下沿由调用处比较——这里模拟逐拍调用，数出边沿
// Left stick -> direction keys (dm_stick_nav.h, RC1.1 B006b): past half = pressed, released inside a quarter (hysteresis), dominant axis only, up positive; edges are counted by simulating per-sample calls
#include "../dm_stick_nav.h"
#include <stdio.h>
static int checks, fails;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)
static uint64_t edges(dm_stick_nav *st, uint64_t *prev, int32_t x, int32_t y) { uint64_t d = dm_stick_nav_dirs(st, x, y), e = d & ~*prev; *prev = d; return e; }
int main(void) {
    dm_stick_nav st = { 0, 0 }; uint64_t p = 0;
    CHECK(edges(&st, &p, 0, 0) == 0);
    CHECK(edges(&st, &p, 0, 10000) == 0);                    // 不到一半：不算 | under half: nothing
    CHECK(edges(&st, &p, 0, 20000) == DM_BTN_UP);            // 过一半：上 | past half: up
    CHECK(edges(&st, &p, 0, 32767) == 0);                    // 推住：不连发 | held: no repeat
    CHECK(edges(&st, &p, 0, 12000) == 0 && p == DM_BTN_UP);  // 回到半中间：仍按着（回滞） | half way back: still held (hysteresis)
    CHECK(edges(&st, &p, 0, 5000) == 0 && p == 0);           // 回到四分之一以内：松开 | inside a quarter: released
    CHECK(edges(&st, &p, 0, -20000) == DM_BTN_DOWN);         // 下 | down
    CHECK(edges(&st, &p, 0, 0) == 0);
    CHECK(edges(&st, &p, 25000, 0) == DM_BTN_RIGHT); CHECK(edges(&st, &p, 0, 0) == 0);
    CHECK(edges(&st, &p, -25000, 0) == DM_BTN_LEFT); CHECK(edges(&st, &p, 0, 0) == 0);
    CHECK(edges(&st, &p, 20000, 26000) == DM_BTN_UP);        // 斜推：只取推得多的轴 | diagonal: dominant axis only
    CHECK(edges(&st, &p, 0, 0) == 0);
    CHECK(edges(&st, &p, 26000, 20000) == DM_BTN_RIGHT);
    CHECK(dm_stick_nav_dirs(NULL, 30000, 0) == 0);
    printf("t_stick_nav: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
