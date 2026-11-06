// B204 矩阵：百宝囊放出 / 收进之后那颗去了哪，按真实的「局 / 提交」先后存盘重进，这一颗总数守恒（不复制、不丢）。
// 模型：一串事件——本局提交（存档在存取之后 / 之前序列化）、出局、新局提交（新局的世界 = 最后一次提交的存档）。侧文件 = 盘上状态有没有这颗；世界 = 最后一次提交的存档里这一颗。
// 修后规则：离开（卖 / 丢）只认「本局内、序列化晚于离开」；跨局遗留的保守留在囊里。修前规则（-DPROOF_OLD = 并线版 a01a196b：离开后按提交次数 max(gone_cc, rel_cc)+2）：
//   终审探针「退局只一次旧提交 → 新局补存仍是放出前世界」那行必红（丢）。
// B204 matrix: wherever a released / stowed rune goes, saved and re-entered with the real order of runs and commits, this rune is conserved (no duplicate, no loss).
// Model: a list of events -- an in-run commit (its save serialized after / before the store or release), leaving the run, a commit in the next run (whose world = the last committed save). Side = the rune in the on-disk state; world = this rune in the last committed save.
// Fixed rule: gone (sold / dropped) counts only "in this run, serialized after it left"; leftovers from the previous run stay in the pouch. Pre-fix rule (-DPROOF_OLD = merged a01a196b: by commit count max(gone_cc, rel_cc)+2):
//   the final-review probe row "single old exit commit -> the next run's save is still the pre-release world" must fail (loss).
#include "dm_pouch_proof.h"
#include "dm_exitwait.h"
#include <stdio.h>
static int fails;
enum { STAY, SHARED, SOLD };
enum { C_BEFORE, C_AFTER, EXIT, NEW_RUN };   // 本局提交（存取前序列化）/ 本局提交（存取后）/ 出局 / 新局里的提交 | in-run commit serialized before / after, leave the run, a commit in the next run
#ifdef PROOF_OLD
static int old_rule(int put, int have, int gone, unsigned long long cno) { if (put) return !have; if (!gone) return have; return cno >= 0 + 2 ? 2 : 0; }   // 并线版（离开时、放出时都在第 0 次提交后） | merged version (left and released after commit 0)
#endif
// put：1 收进 0 放出；where：放出后去了哪；ev：事件串；want：重进后这一颗应有几颗（卖掉且卖出已提交 = 0）| want: how many of this rune after re-entry
static void run(const char *name, int put, int where, const int *ev, int nev, int want) {
    int side = !put, pend = 1, world = put, run_no = 0, gone = !put && where == SOLD;   // world：最后一次提交的存档里这一颗（开局 = 存取前） | world: this rune in the last committed save (at start = before the store / release)
    unsigned long long cno = 0; int live = 1, after_world = put ? 0 : (where != SOLD);   // after_world：存取之后序列化的存档里这一颗 | this rune in a save serialized after
    for (int i = 0; i < nev; i++) {
        int e = ev[i]; if (e == EXIT) { live = 0; continue; }
        int post = e == C_AFTER;   // 只有本局里存取之后序列化的才算；新局的世界 = 上一次提交的那份，不算 | only an in-run save serialized after counts; the next run's world is the last committed one and does not
        if (e == NEW_RUN && run_no == 0) { run_no = 1;
#ifndef PROOF_OLD
            if (pend && gone) pend = 0;   // 修后：跨局遗留的离开条目不落定，留在囊里（修前照样按提交次数等） | fixed: a gone leftover is not settled and stays in the pouch (before: still waits by count)
#endif
        }
        cno++; int snap = e == C_AFTER ? after_world : world;   // 这次提交的存档里这一颗 | this rune in this commit's save
        int have = put ? snap : (e == C_AFTER && where != SOLD);
#ifdef PROOF_OLD
        int how = old_rule(put, have, gone, cno); (void)live; (void)post;
#else
        int how = dm_pouch_proven(put, have, gone, gone && live && run_no == 0 && post); (void)cno;
#endif
        world = snap;
        if (pend && how) { side = !side; pend = 0; }
    }
    int total = side + world, ok = total == want; if (!ok) fails++;
    printf("%s %s：盘上 %d + 存档 %d = %d（应 %d）\n", ok ? "PASS" : "FAIL", name, side, world, total, want);
}
#define RUN(n, p, w, want, ...) do { const int e_[] = { __VA_ARGS__ }; run(n, p, w, e_, (int)(sizeof e_ / sizeof *e_), want); } while (0)
int main(void) {
    RUN("放出→留在背包→存盘重进", 0, STAY, 1, C_BEFORE, C_AFTER, EXIT, NEW_RUN);
    RUN("放出→拖进共享仓库→存盘重进", 0, SHARED, 1, C_BEFORE, C_AFTER, EXIT, NEW_RUN);
    RUN("放出→卖掉 / 丢地→本局存盘→重进", 0, SOLD, 0, C_BEFORE, C_AFTER, C_AFTER, EXIT, NEW_RUN);
    RUN("放出→卖掉→只一次旧退局提交→新局补存（终审探针）", 0, SOLD, 1, C_BEFORE, EXIT, NEW_RUN, NEW_RUN);   // 修前红（丢） | red before (loss)
    RUN("放出→卖掉→没提交就被杀", 0, SOLD, 1, EXIT, NEW_RUN);
    RUN("放出→共享仓库→只一次旧提交就被杀", 0, SHARED, 1, C_BEFORE, EXIT, NEW_RUN);
    RUN("收进→存盘重进", 1, STAY, 1, C_BEFORE, C_AFTER, EXIT, NEW_RUN);
    RUN("收进→只一次旧提交", 1, STAY, 1, C_BEFORE, EXIT, NEW_RUN);
#ifndef PROOF_OLD
    if (dm_pouch_proven(0, 0, 1, 0) || dm_pouch_proven(0, 0, 1, 1) != 2 || dm_pouch_proven(0, 1, 0, 0) != 1 || dm_pouch_proven(1, 0, 0, 0) != 1 || dm_pouch_proven(1, 1, 0, 1)) { printf("FAIL 规则表\n"); fails++; } else printf("PASS 规则表\n");
    // 终审探针原样（terra_反向丢件探针.c 改成新签名）：退局一次旧提交 + 新局等待期补存 → 新规则不并进，那颗还在囊里 | the final-review probe, with the new signature
    { dm_exitwait w = {0}; int to = 0, side = 1, world = 0, kick = 0; dm_exitwait_leave(&w, 1, 0);
      int how = dm_pouch_proven(0, 0, 1, 0); if (how) side = 0;   // 退局旧提交：不是本局内离开之后 | old exit commit: not in-run after leaving
      for (int t = 1; t <= 50; t++) { if (!dm_exitwait_defer(&w, 0, 1, &to)) break; if (dm_exitwait_kick(&w)) kick++; }   // 跨局遗留不算待证明，不等 | leftovers are not pending, no wait
      how = dm_pouch_proven(0, 0, 1, 0); if (how) side = 0;   // 新局补存：跨局，不算 | next-run save: cross-run, not counted
      int ok = side + world == 1; if (!ok) fails++; printf("%s 终审探针：kicks=%d side=%d world=%d total=%d（应 1）\n", ok ? "PASS" : "FAIL", kick, side, world, side + world); }
#endif
    printf(fails ? "t_pouch_proof: %d 项失败\n" : "t_pouch_proof: 全过\n", fails); return fails != 0;
}
