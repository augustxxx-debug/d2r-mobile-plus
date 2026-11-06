// 百宝囊待证明的存取什么时候并进盘上状态（B204）。纯函数，单测 t_pouch_proof.c
//   收进囊：本角色档里已没有背包那一串 → 并进（不变）。
//   放到背包：服务端拍按新物品 id 一路跟着那颗（dm_pouch.c pouch_track）：
//     还在玩家自己的物品栏（背包 / 个人仓库页 / 方块）→ 证据 = 它此刻那一串（含位置），在本角色档里找；
//     进了共享仓库 / 高级仓库（附属单位）→ 证据 = 它此刻那一串，在共享仓库档里找；找到 = 这次提交的存档是放出之后序列化的 → 并进。
//     哪个容器都不在了（卖掉 / 丢地 / 用掉）→ 存档里没有可找的：只认「本局内、序列化晚于看到离开」的事实——看到离开时没有在途存盘，记下工作目录角色档的整份指纹 h0（同 dm_pages.c 翻页的做法），
//       之后同一局里提交的角色档指纹 ≠ h0 → 只能是离开之后序列化的 → 并进。不按提交次数（终审 P1：退局只有一次旧提交时，新局补请的存盘仍是放出前的世界，按次数并进会丢那颗）；
//       跨局遗留的离开条目不落定：保守留在囊里（宁可极少数复制也不丢），红日志 + 计数（dm_pouch_kept）。
//   原先只在本角色档里找放出时那一串：放出后拖进共享仓库（那一叠满了不改道）/ 卖掉 / 丢地 / 挪格，永远证明不了，
//   .swpouch 一直留着那颗，重进后囊里一颗、外面一颗 = 复制。符文宝石没有唯一编号，不能在全部存档里找「同一件」，所以按 id 跟着走、按它在哪的文件找。
// When a pending pouch store / release is merged into the on-disk state (B204). Pure; unit test t_pouch_proof.c
//   Stow: this character's save no longer has the inventory bitstream -> merge (unchanged).
//   Release: the server tick follows that rune by its new item id (pouch_track in dm_pouch.c):
//     still in the player's own item container (inventory / personal stash page / cube) -> evidence = its bitstream right now (with position), searched in this character's save;
//     in the shared stash / advanced stash (attached units) -> evidence = its bitstream right now, searched in the shared stash save; found = this commit's saves were serialized after the release -> merge.
//     in no container any more (sold / dropped / used) -> nothing to find in a save: only the fact 'serialized after it was seen leaving, in this run' counts -- when it is seen leaving with no save in flight, the whole-file fingerprint h0 of the working-directory
//       character save is recorded (as the page turn in dm_pages.c does); a character save committed later in the same run with a fingerprint != h0 can only have been serialized after it left -> merge. Not by commit counts (final review P1: with a single old
//       exit commit, the save requested in the next run is still the pre-release world and a count-based merge loses the rune); a gone entry left over from the previous run is not settled: it stays in the pouch (rather a rare duplicate than a loss), red log + counter (dm_pouch_kept).
//   Before, only the release-time bitstream was searched in this character's save: dragged into the shared stash (not rerouted when that stack is full) / sold / dropped / moved, it could never be proven,
//   .swpouch kept it, and after re-entering there was one in the pouch and one outside = duplication. Runes / gems have no unique id, so "the same item" cannot be searched across all saves; it is followed by id and searched in the file of where it is.
#pragma once
#include <stdint.h>
// gone：放出的那颗已不在任何容器；after：这次提交的角色档是本局内、看到离开之后序列化的（事实，由 pouch_hook 判）。1 = 直接证明，2 = 离开后并进，0 = 还等
// gone: the released rune is in no container any more; after: this commit's character save was serialized after it was seen leaving, in this run (a fact, decided in pouch_hook). 1 = proven directly, 2 = merged after it left, 0 = keep waiting
static inline int dm_pouch_proven(int put, int have, int gone, int after) {
    if (put) return !have;
    if (!gone) return have;
    return after ? 2 : 0;
}
