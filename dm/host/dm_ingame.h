#pragma once
#include <stdint.h>
// 在不在局内的两个判据（diag.c 的 game_live / dm_ingame_now 与单测共用）：
// ① 服务端局号槽（main+0x2d665d8 起 1024 个，0 / -1 = 空）里有没有活的局——不靠拍，暂停菜单时局照样是活的，存盘退出、被踢出等任何离局路径后为空；
// ② 拍线程发布的受控玩家快照有效。快照只在拍入口里更新，离局后不再出拍，最后一拍的「有效」会一直留着（RC1.1 真机：局内开光追 → 保存退出回选角页，人物被光追压黑），
//    所以局内 = ② 且 ①
// The two criteria for "in a run" (shared by diag.c game_live / dm_ingame_now and the unit test):
// ① is there a live run in the server run-number slots (1024 from main+0x2d665d8, 0 / -1 = empty) - independent of ticks: the run is still live in the pause menu, and empty after save-and-exit, a kick or any other way out;
// ② the controlled-player snapshot published by the tick thread is valid. It is only updated at the tick entry, and after leaving a run no more ticks come, so the last tick's "valid" stays
//    (RC1.1 device: ray tracing on in a run → save and exit to character select, the character was darkened by ray tracing), hence in a run = ② and ①
static inline int dm_run_live(const uint64_t *slots) {
    if (!slots) return 1;   // 认不出主模块：不改原判断 | main module not found: keep the old answer
    for (int i = 0; i < 0x400; i++) if (slots[i] + 1 >= 2) return 1;
    return 0;
}
static inline int dm_ingame_from(int hud_valid, const uint64_t *slots) { return hud_valid && dm_run_live(slots); }
