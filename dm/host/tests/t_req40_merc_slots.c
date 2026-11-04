// 佣兵六格缺省全开（REQ40）回归：直接包含真的 dm_rift.c，不调用任何原版入口。
// 现场 = 新角色、普通难度、0 层秘境：记录结构 R 清零（best = 0），且不设 DM_TEST_MERC_SLOTS。
// 期望：dm_rift_merc_unlocked() 仍返回 1（旧行为要秘境最高通关 ≥ 10 层，会返回 0）。
// Mercenary six slots open by default (REQ40) regression: includes the real dm_rift.c and calls no original entry.
// Scene = a new character, Normal difficulty, 0 rift levels: the record struct R is zeroed (best = 0) and DM_TEST_MERC_SLOTS is not set.
// Expected: dm_rift_merc_unlocked() still returns 1 (the old behaviour required highest rift cleared >= 10 and returned 0).
#include "../dm_rift.c"

void (*g_dm_pre_commit_hook[6])(const char *);
uint64_t dm_fs_commit_count(void) { return 0; }

int main(void) {
    int fails = 0;
    unsetenv("DM_TEST_MERC_SLOTS");
    memset(&R, 0, sizeof R);                 // 新角色，0 层秘境 | new character, 0 rift levels
    if (!dm_rift_merc_unlocked()) {
        fails++; printf("  ✗ 新角色 / 0 层秘境：六个佣兵格应为缺省可用，dm_rift_merc_unlocked() 返回 0\n");
    }
    R.best = 0; R.clears = 0; R.unlock = 0; // 普通难度、没通关过 | Normal difficulty, never cleared
    if (!dm_rift_merc_unlocked()) {
        fails++; printf("  ✗ 普通难度 / 0 层秘境：六个佣兵格应为缺省可用，dm_rift_merc_unlocked() 返回 0\n");
    }
    printf("t_req40_merc_slots %s：新角色 / 普通难度 / 0 层秘境下六个佣兵格缺省可用\n", fails ? "失败" : "OK");
    return fails != 0;
}
