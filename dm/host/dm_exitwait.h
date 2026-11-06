// 退局等提交（B203）：退局那次存盘由游戏的写盘线程提交，游戏不等它就把玩家卸了、回到前端（实测 110 ms 后已在前端，提交还在后面）。
// 旁路文件模块（百宝囊 / 编年史 / 个人仓库页 / iCloud 并回）的「已读入 / 待证明」状态如果在空玩家那一拍就清掉，或者下一次进局（一键重开约 1.2 秒后）
// 在那次提交之前就重读，提交前钩子就核不到退局前的收进 / 放出 / 存入：收进的丢、放出的复制。
// 规则（同 dm_bag.c「退局空玩家拍不清」，再加一条）：空玩家拍不清状态，只记下当时的提交次数；下一个玩家进局时，如果还有没证明的，等它们都在提交里证明了再读入（最多等 DM_EXITWAIT_MAX 拍，超时照读并报红）；不看提交次数——退局存盘是连着几次提交，头一次常常还不含刚放出 / 存入的件（B203 复核，同 531b1269）。
// Wait for the exit commit (B203): the exit save is committed by the game's disk thread, and the game unloads the player and returns to the front end without waiting for it (110 ms later it is at the front end, the commit still ahead).
// If the side-file modules (pouch / Chronicle / stash pages / iCloud merge-back) clear their "loaded / pending" state on the empty-player tick, or the next entry (about 1.2 s later with the one-tap restart)
// reloads before that commit, the pre-commit hook can no longer prove the stores / withdrawals made just before leaving: stored items are lost, withdrawn ones duplicated.
// Rule (as dm_bag.c "the empty-player tick does not clear", plus one more): the empty-player tick keeps the state and records the commit count; when the next player enters and something is still unproven,
// the load waits until it has all been proven in a commit (not until the commit count moves: the exit save is several commits and the first often lacks the new item) (at most DM_EXITWAIT_MAX ticks; on timeout it loads anyway and logs red).
// 边界（审计 P1-2）：「不丢件」只在等待上限内证明成功时成立。超时放行后各模块照旧（同本单之前）先清掉未证明项再读盘——那几条照样会丢 / 复制，并报红；
//   等待期间按 DM_EXITWAIT_KICK 补请存盘，就是为了把「写失败 / 存盘请求丢了」也在上限内补上。
// commits 参数已不参与判断（放行只看待证明是否清零），留着是为了不改已接入的调用点（dm_bag.c）| the commits argument no longer decides anything; it stays so existing callers (dm_bag.c) need not change
// Limit (audit P1-2): "nothing lost" holds only when the proof lands within the wait cap. After a timeout each module, as before this change, drops its unproven entries and reloads: those few are still lost / duplicated, and logged red;
//   the save requests every DM_EXITWAIT_KICK ticks while waiting exist to recover a failed write / a lost save request within the cap.
#pragma once
#include <stdint.h>

#define DM_EXITWAIT_MAX 250   // 服务端拍，约 10 秒 | server ticks, about 10 seconds
typedef struct { uint64_t cc; int armed, waited; } dm_exitwait;

// 空玩家拍：记下当时的提交次数（只在刚退局那一拍记）| empty-player tick: record the commit count (only on the tick right after leaving)
static inline void dm_exitwait_leave(dm_exitwait *w, int was_in_game, uint64_t commits) { if (was_in_game) { w->armed = 1; w->cc = commits; w->waited = 0; } }
// 新玩家进局那一拍：返回 1 = 这一拍先别读入（还有没证明的）；0 = 可以读入。*timed_out 置 1 表示是等超时放行的
// the tick a new player enters: 1 = do not load yet (the exit commit has not come and something is unproven); 0 = load now. *timed_out is set to 1 when released by the timeout
static inline int dm_exitwait_defer(dm_exitwait *w, int pending, uint64_t commits, int *timed_out) {
    if (timed_out) *timed_out = 0;
    if (!w->armed) return 0;
    (void)commits; if (!pending) { w->armed = 0; return 0; }   // 只认「都证明了」，不看提交次数：退局存盘是连着几次提交，头一次常常还不含刚放出 / 存入的件（同 531b1269，B203 复核）| only "everything proven" releases, not the commit count: the exit save is several commits and the first often lacks the item just withdrawn / stored
    if (++w->waited > DM_EXITWAIT_MAX) { w->armed = 0; w->waited = 0; if (timed_out) *timed_out = 1; return 0; }   // 超时放行：计数一并复位（审计 P2）| released by timeout: the count is reset too
    return 1;
}
// 等待期间每 DM_EXITWAIT_KICK 拍返回 1：模块自己的「再请一次存盘」排在等待之后，等着的时候也被挡住，由调用方在这里补请（审计 P1-1：侧文件写失败 / 存盘请求丢了时，不补请就要白等满 DM_EXITWAIT_MAX 拍）
// every DM_EXITWAIT_KICK ticks while waiting, returns 1: the module's own "ask for another save" comes after the wait and is blocked meanwhile, so the caller asks here (audit P1-1)
#define DM_EXITWAIT_KICK 50
static inline int dm_exitwait_kick(const dm_exitwait *w) { return w->armed && w->waited > 0 && w->waited % DM_EXITWAIT_KICK == 0; }
// 放行之后取一次「等了几拍」（0 = 没等），给模块写日志用 | after release, how many ticks were waited (0 = none), for the module's log
static inline int dm_exitwait_took(dm_exitwait *w) { int n = w->armed ? 0 : w->waited; if (!w->armed) w->waited = 0; return n; }
