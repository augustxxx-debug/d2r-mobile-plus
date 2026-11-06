// 自动收符文宝石的「玩家自己丢的不捡」名单（B171）：在玩家物品栏里见过的符文 / 宝石物品 id（环形，64 个），带最后一次见到的帧。
// 物品丢到地上单位 id 不变，所以地上的符文 / 宝石若 id 在名单里 = 玩家自己丢的，自动捡跳过它（玩家亲手再捡起来仍照常收进囊）。
// 恢复正常自动捡（OMP 11-05 定）：玩家离开它一段距离（dm_pouch_own_far：任一轴超过 DM_POUCH_OWN_LEAVE 格）就从名单删掉；
// 既不在物品栏也不在地上（换了区域、被别人捡走）超过 DM_POUCH_OWN_STALE 帧也删（dm_pouch_own_sweep），再见到就当新的。纯函数，单测 t_pouch_own.c
// Auto-stow's "do not pick up what the player dropped" list (B171): ids of rune / gem items seen in the player's inventory (ring, 64), with the frame last seen.
// An item keeps its unit id when dropped, so a listed rune / gem on the ground was dropped by the player and auto-pick skips it (picking it up by hand still stows it).
// Back to normal auto-pick (OMP 11-05): once the player is more than DM_POUCH_OWN_LEAVE tiles away on either axis it is removed; an entry seen neither in the inventory
// nor on the ground for DM_POUCH_OWN_STALE frames (another area, picked up by someone else) is removed too, so it counts as new when seen again. Pure; unit test t_pouch_own.c
#pragma once
#include <stdint.h>
#define DM_POUCH_OWN_N 64
#define DM_POUCH_OWN_LEAVE 12     // 格（自动捡半径 PICK_R = 3 的 4 倍） | tiles (4 × the auto-pick radius PICK_R = 3)
#define DM_POUCH_OWN_STALE 300    // 帧（5 秒） | frames (5 s)
typedef struct { uint32_t id[DM_POUCH_OWN_N]; uint64_t seen[DM_POUCH_OWN_N]; unsigned n; } dm_pouch_own;
static inline int dm_pouch_own_find(const dm_pouch_own *o, uint32_t id) { if (!id) return -1; for (int i = 0; i < DM_POUCH_OWN_N; i++) if (o->id[i] == id) return i; return -1; }
static inline int dm_pouch_own_has(const dm_pouch_own *o, uint32_t id) { return dm_pouch_own_find(o, id) >= 0; }
static inline void dm_pouch_own_touch(dm_pouch_own *o, uint32_t id, uint64_t fr) { int i = dm_pouch_own_find(o, id); if (i >= 0) o->seen[i] = fr; }
static inline void dm_pouch_own_add(dm_pouch_own *o, uint32_t id, uint64_t fr) {
    if (!id) return; int i = dm_pouch_own_find(o, id); if (i < 0) { i = (int)(o->n++ % DM_POUCH_OWN_N); o->id[i] = id; } o->seen[i] = fr; }
static inline void dm_pouch_own_del(dm_pouch_own *o, uint32_t id) { int i = dm_pouch_own_find(o, id); if (i >= 0) { o->id[i] = 0; o->seen[i] = 0; } }
static inline int dm_pouch_own_far(int dx, int dy) { return dx > DM_POUCH_OWN_LEAVE || dx < -DM_POUCH_OWN_LEAVE || dy > DM_POUCH_OWN_LEAVE || dy < -DM_POUCH_OWN_LEAVE; }
static inline void dm_pouch_own_sweep(dm_pouch_own *o, uint64_t fr) { for (int i = 0; i < DM_POUCH_OWN_N; i++) if (o->id[i] && fr - o->seen[i] > DM_POUCH_OWN_STALE) { o->id[i] = 0; o->seen[i] = 0; } }
