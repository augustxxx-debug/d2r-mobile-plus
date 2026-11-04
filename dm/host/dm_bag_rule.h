// 大背包的纯规则（无游戏内存）：dm_bag.c 用这一份，tests/t_bag_rule.c 测这一份。
// Large-bag pure rules (no game memory): dm_bag.c uses this copy, tests/t_bag_rule.c tests this copy.
#pragma once
// 「护身符页满了」提示：服务端每拍数背包页（页 0）上等着搬的护身符 nf。页 5 放不下时，nf 比上次多（新捡 / 新买 / 新收进来一件）就提示一次；
// 页 5 有空位的那一拍把记录清零，下次再满照常提示。*last 初值 -1：进局时背包页已有溢出的护身符也提示一次。
// 原先按「第一件没搬走的护身符的 id」去重：背包页已经躺着一件溢出的，再捡多少件都不再提示（11-04 整机验收 f3b）。
// "Charm page is full" notice: the server counts, every tick, the charms waiting on the bag page (page 0), nf. When page 5 has no room and nf is larger than
// last time (one more picked up / bought / received) the notice fires once; a tick where page 5 has room clears the record so the next time it fills it fires again.
// *last starts at -1: overflow charms already on the bag page when entering the run also give one notice.
// It used to dedupe on "the id of the first charm not moved": with one overflow charm already lying on the bag page, no further pickup ever gave a notice (11-04 Mac acceptance f3b).
static inline int dmbag_full_notice(int *last, int nf, int full) {
    if (!full) { *last = 0; return 0; }
    int fire = nf > *last; *last = nf; return fire;
}
