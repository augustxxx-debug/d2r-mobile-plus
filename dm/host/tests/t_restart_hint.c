// B151「重启后生效」提示判定的单测（dm_restart_hint.h，宿主 dm_custom.c 用同一份）：改值→显示、改回→消失、没改→不显示、「放弃」→按已应用值、「应用」后仍不同→保留；
// 光追按生效值比、无硬件光追恒不提示；分辨率行两种原因合并。
// Unit test of the B151 restart-hint decisions (dm_restart_hint.h, the same code dm_custom.c uses): change -> shown, change back -> hidden, unchanged -> hidden, Cancel -> compared with the applied value,
// still different after Apply -> kept; RT compares effective values and never shows without hardware RT; the resolution row merges both reasons.
#include "../dm_restart_hint.h"
#include <stdio.h>
static int checks, fails;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)
int main(void) {
    // 启动值 0、已应用 0 | launch 0, applied 0
    CHECK(!dm_restart_pending(1, 0, 0, -1));   // 没改 | unchanged
    CHECK(dm_restart_pending(1, 0, 0, 1));     // 点选了 1（没应用）| picked 1, not applied
    CHECK(!dm_restart_pending(1, 0, 0, 0));    // 又点回 0 | picked back 0
    CHECK(dm_restart_pending(1, 0, 1, -1));    // 应用了 1 | applied 1 -> stays until restart
    CHECK(!dm_restart_pending(1, 0, 1, 0));    // 应用 1 后又点回 0 | after applying 1, picked 0 again
    CHECK(!dm_restart_pending(1, 0, 0, -1));   // 点了「放弃」：待应用清掉、已应用仍 0 | Cancel: pending cleared, applied still 0
    CHECK(!dm_restart_pending(0, 0, 0, 1));    // 启动值还没记 | launch value not recorded yet
    CHECK(dm_restart_pending(1, 2, 2, 3) && !dm_restart_pending(1, 2, 3, 2));   // 多档值同理 | multi-level values alike
    // 光追：启动时没降（光追关），现在选了 2 档 | RT: launch did not drop (off), now level 2
    CHECK(dm_rt_restart(1, 1, 0, 2));
    CHECK(!dm_rt_restart(1, 1, 0, 0));   // 选回关 | back to off
    CHECK(!dm_rt_restart(1, 1, 1, 3));   // 启动时就开着（降了），改档不提示（改档当场生效） | on at launch (dropped): changing level gets no hint
    CHECK(dm_rt_restart(1, 1, 1, 0));    // 启动开着、现在关 | on at launch, now off
    CHECK(!dm_rt_restart(1, 0, 0, 2));   // 无硬件光追（含安卓 / B108）：设置里带着档也不提示 | no hardware RT: never, even with a level in the file
    CHECK(!dm_rt_restart(0, 1, 0, 2));
    // 分辨率行 | resolution row
    CHECK(dm_res_note_kind(0, 0) == 0 && dm_res_note_kind(1, 0) == 1 && dm_res_note_kind(0, 1) == 2 && dm_res_note_kind(1, 1) == 3);
    printf("t_restart_hint: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
