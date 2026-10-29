// 左摇杆 → 方向键（dm_stick_nav.h，RC1.1 B006b）：推过一半算按下、回到四分之一以内才松开（回滞）、只取主轴且主轴锁到两轴都松开、上为正；按下沿由调用处比较——这里模拟逐拍调用，数出边沿
// Left stick -> direction keys (dm_stick_nav.h, RC1.1 B006b): past half = pressed, released inside a quarter (hysteresis), dominant axis only and locked until both axes are released, up positive; edges are counted by simulating per-sample calls
#include "../dm_stick_nav.h"
#include <stdio.h>
static int checks, fails;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)
static uint64_t edges(dm_stick_nav *st, uint64_t *prev, int32_t x, int32_t y) { uint64_t d = dm_stick_nav_dirs(st, x, y), e = d & ~*prev; *prev = d; return e; }
int main(void) {
    dm_stick_nav st = { 0, 0, 0 }; uint64_t p = 0;
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
    // 主轴锁定（RC1.1 独立审计 P2，复现程序 stick_chatter.c 的两条路径）：整段只许一个按下沿 | dominant-axis lock (audit P2, the two paths of stick_chatter.c): one press edge per run
    { static const int32_t a[][2] = { {0, 0}, {0, 24000}, {12000, 10000}, {11000, 12000}, {12000, 10000}, {11000, 12000}, {0, 0} };   // A：上推后在中间带里斜着回中 | A: up, then a diagonal return through the middle band
      static const int32_t b[][2] = { {0, 0}, {20000, 20100}, {20100, 20000}, {20000, 20100}, {20100, 20000}, {0, 0} };               // B：45 度斜推满附近抖 | B: jitter around a full 45-degree push
      const int32_t (*runs[2])[2] = { a, b }; const int lens[2] = { 7, 6 }; const uint64_t want[2] = { DM_BTN_UP, DM_BTN_UP };
      for (int r = 0; r < 2; r++) { dm_stick_nav n = { 0, 0, 0 }; uint64_t pv = 0, all = 0; int cnt = 0;
          for (int i = 0; i < lens[r]; i++) { uint64_t e = edges(&n, &pv, runs[r][i][0], runs[r][i][1]); if (e) { cnt++; all |= e; } }
          CHECK(cnt == 1); CHECK(all == want[r]); CHECK(pv == 0); } }
    // 不回中直接转到另一个方向：锁着的轴松开后不出方向，回中再推才算 | rolling to another direction without centring: nothing until the stick is centred and pushed again
    { dm_stick_nav n = { 0, 0, 0 }; uint64_t pv = 0;
      CHECK(edges(&n, &pv, 0, 30000) == DM_BTN_UP);
      CHECK(edges(&n, &pv, 30000, 20000) == 0 && pv == DM_BTN_UP);   // 横轴推满、纵轴仍按着：还是上 | x full, y still held: still up
      CHECK(edges(&n, &pv, 30000, 0) == 0 && pv == 0);                // 纵轴松开、横轴按着：锁未解，不出右 | y released, x held: still locked, no right
      CHECK(edges(&n, &pv, 0, 0) == 0);                                // 回中：解锁 | centred: unlocked
      CHECK(edges(&n, &pv, 30000, 0) == DM_BTN_RIGHT); }
    { dm_stick_nav n = { 1, -1, 2 }; dm_stick_nav_reset(&n); CHECK(n.hx == 0 && n.hy == 0 && n.axis == 0); }
    printf("t_stick_nav: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
