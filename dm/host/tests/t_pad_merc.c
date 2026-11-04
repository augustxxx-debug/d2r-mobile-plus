// 手柄佣兵页纯函数（dm_pad_merc.h，B099 / B100）。不带参数：自检；带 11 个参数（起始格 x y 宽 高、目标格 x y 宽 高、k、光标 x y）：打印「落点 x y 原版弃不弃」给 test_b099_b100_merc_pad.py 拿真布局核。
// Pure functions of the controller mercenary page (dm_pad_merc.h, B099 / B100). No arguments: self-test; with 11 arguments (start cell x y w h, target cell x y w h, k, cursor x y): prints
// "target x y dropped-or-not" for test_b099_b100_merc_pad.py to check against the real layout.
#include "dm_pad_merc.h"
#include <stdio.h>
#include <stdlib.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main(int argc, char **argv) {
    if (argc == 12) {
        int32_t a[4], h[4]; for (int i = 0; i < 4; i++) { a[i] = atoi(argv[1 + i]); h[i] = atoi(argv[5 + i]); }
        int tx, ty; dm_pad_slot_jump(atoi(argv[10]), atoi(argv[11]), a, h, atof(argv[9]), &tx, &ty);
        printf("%d %d %d\n", tx, ty, dm_pad_up_dropped(a, h)); return 0;
    }
    // B099：0x10 = 已鉴定；0x110 里 0x100 不影响「未鉴定」的判断（原版另拦，这里只提示未鉴定） | B099: 0x10 = identified; 0x100 does not change the unidentified answer
    CHECK(dm_item_flags_unid(0x0)); CHECK(dm_item_flags_unid(0x100)); CHECK(dm_item_flags_unid(0x800));
    CHECK(!dm_item_flags_unid(0x10)); CHECK(!dm_item_flags_unid(0x110)); CHECK(!dm_item_flags_unid(0x200810));
    // B100 ①：头盔格与副手格同一排（头盔下沿 255 > 副手上沿 188）= 原版弃；躯干格在头盔正下（上沿 281 ≥ 255）= 原版认
    // B100 (1): helm and off-hand in one row (helm bottom 255 > off-hand top 188) = dropped; torso right below the helm (top 281 ≥ 255) = honoured
    const int32_t head[4] = { 607, 59, 196, 196 }, larm[4] = { 994, 188, 196, 392 }, torso[4] = { 607, 281, 196, 294 };
    CHECK(dm_pad_up_dropped(larm, head)); CHECK(!dm_pad_up_dropped(torso, head));
    { const int32_t edge[4] = { 0, 255, 10, 10 }; CHECK(!dm_pad_up_dropped(edge, head)); }   // 贴着下沿 = 在上面 | touching the bottom edge = above
    // 落点 = 头盔格中心：副手格在屏上左上角 (x0, y0) + (头盔中心 − 副手左上) × k | landing = helm centre: off-hand top-left on screen + (helm centre − off-hand top-left) × k
    double k = 0.5; int x0 = 517, y0 = 120, tx, ty; dm_pad_slot_jump(x0, y0, larm, head, k, &tx, &ty);
    CHECK(tx == x0 + (int)((607 + 98 - 994) * k)); CHECK(ty == y0 + (int)((59 + 98 - 188) * k));
    CHECK(tx >= x0 + (607 - 994) * k && tx <= x0 + (607 + 196 - 994) * k && ty >= y0 + (59 - 188) * k && ty <= y0 + (59 + 196 - 188) * k);   // 在头盔格里 | inside the helm cell
    // B100 ②：四个条件缺一不收 | B100 (2): any missing condition → not taken
    CHECK(dm_pad_tab_a_take(1, 1, 1, 0));
    CHECK(!dm_pad_tab_a_take(0, 1, 1, 0)); CHECK(!dm_pad_tab_a_take(1, 0, 1, 0)); CHECK(!dm_pad_tab_a_take(1, 1, 0, 0)); CHECK(!dm_pad_tab_a_take(1, 1, 1, 1));
    if (fails) { printf("t_pad_merc：%d 条失败\n", fails); return 1; }
    printf("✓ t_pad_merc：未鉴定判断、副手格按上的弃 / 认与落点、页签 A 收不收\n"); return 0;
}
