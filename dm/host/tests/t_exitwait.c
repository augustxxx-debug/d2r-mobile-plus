// B203 退局等提交的判定（dm_exitwait.h）：空玩家拍记提交次数；新玩家进局时有没证明的就等提交次数涨，最多 DM_EXITWAIT_MAX 拍。
// B203 exit-commit wait (dm_exitwait.h): the empty-player tick records the commit count; a new player's load waits for it to move while something is unproven, at most DM_EXITWAIT_MAX ticks.
#include "dm_exitwait.h"
#include <stdio.h>
static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("  ✗ %d ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
int main(void) {
    dm_exitwait w = {0}; int to = 0;
    CHECK(!dm_exitwait_defer(&w, 1, 5, &to), "第一次进局（没退过局）不等");
    dm_exitwait_leave(&w, 0, 5); CHECK(!w.armed, "前端里的空玩家拍（不是刚退局）不记");
    dm_exitwait_leave(&w, 1, 7); CHECK(w.armed && w.cc == 7, "刚退局记下提交次数 7");
    CHECK(dm_exitwait_defer(&w, 1, 7, &to), "有没证明的、提交次数没涨 → 等");
    CHECK(dm_exitwait_defer(&w, 1, 7, &to), "还在等");
    CHECK(dm_exitwait_defer(&w, 1, 8, &to), "提交次数涨了但还有没证明的（退局存盘头一次常不含新件）→ 继续等（B203 复核）");
    CHECK(dm_exitwait_defer(&w, 1, 9, &to), "又一次提交，还没证明 → 还等");
    CHECK(!dm_exitwait_defer(&w, 0, 9, &to) && !to && !w.armed, "都证明了 → 读，不算超时");
    CHECK(dm_exitwait_took(&w) == 4 && dm_exitwait_took(&w) == 0, "放行后报出等了 4 拍，只报一次");
    dm_exitwait_leave(&w, 1, 9); CHECK(!dm_exitwait_defer(&w, 0, 9, &to), "没有没证明的 → 不等");
    dm_exitwait_leave(&w, 1, 9); int n = 0; while (dm_exitwait_defer(&w, 1, 9, &to)) n++;
    CHECK(n == DM_EXITWAIT_MAX && to, "提交一直不来：等满 %d 拍放行并报超时（实际 %d）", DM_EXITWAIT_MAX, n);
    CHECK(!dm_exitwait_defer(&w, 1, 9, &to), "放行之后不再等");
    // 审计 P1-1：侧文件写失败 / 存盘请求丢了 → 等待期间每 DM_EXITWAIT_KICK 拍补请一次存盘；补请那次证明了就放行，不白等满上限
    { dm_exitwait k = {0}; int t2 = 0, kicks = 0, n = 0, pend = 1; dm_exitwait_leave(&k, 1, 3);
      while (dm_exitwait_defer(&k, pend, 3, &t2)) { n++; if (dm_exitwait_kick(&k)) { kicks++; pend = 0; } }   // 第一次补请就写成功 | the first retry succeeds
      CHECK(kicks == 1 && n == DM_EXITWAIT_KICK && !t2, "写失败后第 %d 拍补请存盘、证明后放行（实际等 %d 拍、补请 %d 次、超时 %d）", DM_EXITWAIT_KICK, n, kicks, t2);
      CHECK(dm_exitwait_took(&k) == DM_EXITWAIT_KICK, "放行报出等的拍数");
      dm_exitwait_leave(&k, 1, 3); kicks = 0; while (dm_exitwait_defer(&k, 1, 3, &t2)) kicks += dm_exitwait_kick(&k);
      CHECK(t2 && kicks == DM_EXITWAIT_MAX / DM_EXITWAIT_KICK && dm_exitwait_took(&k) == 0, "一直证明不了：补请 %d 次后超时放行，计数复位（审计 P2）", kicks); }
    printf("t_exitwait: %d 项检查，%d 项失败\n", checks, fails); return fails != 0;
}
