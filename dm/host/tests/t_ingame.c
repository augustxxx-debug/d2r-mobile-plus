// 在不在局内（dm_ingame.h，RC1.1 真机：局内开光追 → 保存退出回选角页，人物被光追压黑）：受控玩家快照只在拍入口更新，离局后最后一拍的「有效」留着；
// 局内必须同时有活的局号。这里用真的 dm_hud.c 发布 / 读快照，配一张假的局号槽表（1024 个）：
// ① 快照有效 + 有活局 → 局内；② 快照有效 + 局号槽全空（存盘退出之后）→ 不在局内（旧判据只看快照，这里会错报局内）；③ 槽里是 −1（空）同样不算；
// ④ 暂停菜单（不出拍、快照不变、局还活着）→ 仍在局内；⑤ 快照无效（还没进过局）→ 不在局内；⑥ 认不出主模块（槽表 NULL）→ 照快照
// In a run or not (dm_ingame.h; RC1.1 device: ray tracing on in a run → save and exit to character select, the character darkened by ray tracing): the controlled-player snapshot is only updated at the tick entry, and the last tick's "valid" stays after leaving;
// being in a run also needs a live run number. The real dm_hud.c publishes / reads the snapshot, with a fake run-number slot table (1024 entries):
// ① snapshot valid + live run → in a run; ② snapshot valid + all slots empty (after save and exit) → not in a run (the old criterion looked at the snapshot alone and would say "in a run"); ③ −1 (empty) in a slot does not count either;
// ④ pause menu (no ticks, snapshot unchanged, run still live) → still in a run; ⑤ snapshot invalid (never joined) → not in a run; ⑥ main module not recognized (slot table NULL) → follow the snapshot
#include <stdio.h>
#include <string.h>
#include "dm_hud.h"
#include "dm_ingame.h"
static int fail, count;
#define EXPECT(name, want, got) do { int w_ = (want), g_ = (got); count++; printf("%s 期望 %d → 真结果 %d %s\n", name, w_, g_, w_ == g_ ? "✓" : "✗"); fail += w_ != g_; } while (0)
static uint64_t slots[0x400];
static int now(void) { dm_hud_info h; dm_hud_get(&h); return dm_ingame_from(h.valid, slots); }
int main(void) {
    dm_hud_info h; memset(&h, 0, sizeof h);
    EXPECT("⑤ 还没进过局（快照无效、槽全空）：不在局内", 0, now());
    h.valid = 1; dm_hud_publish(&h); slots[37] = 0x1234;
    EXPECT("① 快照有效、有活局：局内", 1, now());
    EXPECT("④ 暂停菜单（不出拍、快照不变、局还活着）：仍在局内", 1, now());
    slots[37] = 0;   // 存盘退出：局号槽清空；拍不再来，快照仍是最后一拍的「有效」 | save and exit: the slot is cleared; no more ticks, the snapshot keeps the last tick's "valid"
    EXPECT("② 存盘退出后（快照仍有效、槽全空）：不在局内", 0, now());
    slots[100] = UINT64_MAX;
    EXPECT("③ 槽里是 −1（空）：不在局内", 0, now());
    slots[100] = 0; slots[5] = 9;
    EXPECT("再进局：局内", 1, now());
    EXPECT("⑥ 认不出主模块（槽表 NULL）：照快照", 1, dm_ingame_from(1, NULL));
    printf("== %d 项，失败 %d ==\n", count, fail);
    return fail != 0;
}
