// 紫闪探针·网格「连片」判据（nvn_mp_grid.h）：真机长测的孤立 1–2 点不记红；成块的（通知栏那种局部块、整屏没写过的显存）照记 | the magenta probe grid's "contiguous" criterion (nvn_mp_grid.h): the long test's isolated 1–2 points log no red line; blocks (the notification-shade local block, a whole never-written screen) still do
#include <stdio.h>
#include "../nvn/nvn_mp_grid.h"
static int fail, count;
#define EXPECT(name, want, got) do { long long w_=(long long)(want), g_=(long long)(got); count++; printf("%s 期望 %lld → 真结果 %lld %s\n", name, w_, g_, w_==g_?"✓":"✗"); fail += w_ != g_; } while (0)
int main(void) {
    EXPECT("真机：1 点（出图 16466，网格 (15,19)）不记红", 0, mp_grid_block(1, 15, 15, 19, 19));
    EXPECT("真机：2 点隔开（(13,19)–(14,20)）不记红", 0, mp_grid_block(2, 13, 14, 19, 20));
    EXPECT("4 点散在大框里（整屏特效）不记红", 0, mp_grid_block(4, 0, 63, 0, 35));
    EXPECT("真机 30 分钟局：施法光晕 4 点小团（网格 (27,9)–(29,12)）不记红", 0, mp_grid_block(4, 27, 29, 9, 12));
    EXPECT("真机 30 分钟局：施法光晕 5 点小团（网格 (28,2)–(29,3)）不记红", 0, mp_grid_block(5, 28, 29, 2, 3));
    EXPECT("真机 30 分钟局：施法光晕 5 点小团（网格 (37,5)–(41,6)）不记红", 0, mp_grid_block(5, 37, 41, 5, 6));
    EXPECT("3×3 实心块（9 点）记红", 1, mp_grid_block(9, 10, 12, 10, 12));
    EXPECT("通知栏那种局部块 12×4 里 30 点记红", 1, mp_grid_block(30, 0, 11, 0, 3));
    EXPECT("整屏没写过的显存 64×36 全品红记红", 1, mp_grid_block(64 * 36, 0, 63, 0, 35));
    EXPECT("外接框反了不记红", 0, mp_grid_block(5, 3, 2, 0, 1));
    {   // 持续性 | persistence
        mp_persist_t st = {0}; int blk = mp_grid_block(8, 24, 29, 10, 13);
        EXPECT("能耗对比局：施法光晕 8 点在 6×4 格里——按连片判是块", 1, blk);
        EXPECT("只闪 1 帧：连续 1 次，不到 MP_PERSIST，不记红", 1, mp_grid_persist(&st, 26327, blk, 24, 29, 10, 13));
        EXPECT("下一帧没了：清零", 0, mp_grid_persist(&st, 26328, 0, 0, 0, 0, 0));
        mp_persist_t a = {0}; mp_grid_persist(&a, 100, 1, 10, 20, 5, 9); mp_grid_persist(&a, 101, 1, 11, 21, 5, 9);
        EXPECT("缺贴图：同一处连续 3 帧 → 3，记红", 3, mp_grid_persist(&a, 102, 1, 11, 21, 6, 10));
        mp_persist_t b = {0}; mp_grid_persist(&b, 100, 1, 10, 20, 5, 9);
        EXPECT("隔一帧再出现：不连续，从 1 起", 1, mp_grid_persist(&b, 102, 1, 10, 20, 5, 9));
        mp_persist_t c = {0}; mp_grid_persist(&c, 100, 1, 0, 5, 0, 5);
        EXPECT("下一帧换到远处（光效换位置）：不重叠，从 1 起", 1, mp_grid_persist(&c, 101, 1, 40, 50, 20, 30));
        mp_persist_t d = {0}; mp_grid_persist(&d, 100, 1, 10, 12, 10, 12);
        EXPECT("相邻 1 格算重叠", 2, mp_grid_persist(&d, 101, 1, 13, 15, 10, 12));
    }
    printf("== %d 项，失败 %d ==\n", count, fail);
    return fail != 0;
}
