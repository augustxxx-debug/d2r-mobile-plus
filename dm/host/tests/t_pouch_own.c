// B171：自动收符文宝石不捡玩家自己丢的（dm_pouch_own.h） | B171: auto-stow skips what the player dropped
#include "../dm_pouch_own.h"
#include <stdio.h>
static int fails, checks;
#define CK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)
int main(void) {
    dm_pouch_own o = {0};
    CK(!dm_pouch_own_has(&o, 45)); CK(!dm_pouch_own_has(&o, 0));
    dm_pouch_own_add(&o, 45, 100); CK(dm_pouch_own_has(&o, 45)); CK(!dm_pouch_own_has(&o, 46));
    dm_pouch_own_add(&o, 45, 110); CK(o.n == 1);                       // 重复不占位 | duplicates take no slot
    dm_pouch_own_add(&o, 0, 110); CK(o.n == 1);                        // id 0 不记 | id 0 is not recorded
    // 离开一段距离 | walking away
    CK(!dm_pouch_own_far(3, -3)); CK(!dm_pouch_own_far(12, 0)); CK(dm_pouch_own_far(13, 0)); CK(dm_pouch_own_far(0, -13));
    dm_pouch_own_del(&o, 45); CK(!dm_pouch_own_has(&o, 45));
    // 久不见（换区域）| not seen for a while (another area)
    dm_pouch_own_add(&o, 50, 1000); dm_pouch_own_sweep(&o, 1000 + DM_POUCH_OWN_STALE); CK(dm_pouch_own_has(&o, 50));
    dm_pouch_own_touch(&o, 50, 1200); dm_pouch_own_sweep(&o, 1000 + DM_POUCH_OWN_STALE + 1); CK(dm_pouch_own_has(&o, 50));   // 见到就续 | seeing it renews
    dm_pouch_own_sweep(&o, 1200 + DM_POUCH_OWN_STALE + 1); CK(!dm_pouch_own_has(&o, 50));
    // 环满挤掉最旧 | the oldest drops out when full
    dm_pouch_own_add(&o, 7, 0); for (uint32_t i = 100; i < 100 + DM_POUCH_OWN_N; i++) dm_pouch_own_add(&o, i, 0);
    CK(!dm_pouch_own_has(&o, 7)); CK(dm_pouch_own_has(&o, 100 + DM_POUCH_OWN_N - 1));
    printf("t_pouch_own: %d checks, %d failed\n", checks, fails); return fails != 0;
}
