// dm_padmode.c 游戏页里两段可以单测的判断（t_padmode_pure、t_notice_dispatch）：手柄在最后一个停靠点按下往哪走；难度说明（B034 / B035）怎么拼。
// Two decisions of the Gameplay page in dm_padmode.c that unit tests can drive (t_padmode_pure, t_notice_dispatch): where controller Down goes from the last stop;
// how the difficulty tip (B034 / B035) is put together.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
// 难度行之后的停靠点上按下：下一个停靠点在就去那里；没有了才回原版表格——只有原版表格那组展开（TouchTableHolder +0x50 可见）且记得回哪一行时，
// 否则留在原地（表格藏着时焦点落进去，金框画在看不见的行上，原版的上下 / A 会改看不见的设置，审计 r1 P1-2）
// Down on a stop after the difficulty row: go to the next stop when there is one; only past the last stop return to the original table -- and only while that
// group is expanded (TouchTableHolder +0x50 visible) and the row to return to is known; otherwise stay (a focus inside the hidden table puts the gold frame on an
// invisible row and lets the original Up / Down / A change invisible settings, audit r1 P1-2)
enum { DM_PM_DOWN_NEXT, DM_PM_DOWN_TABLE, DM_PM_DOWN_STAY };
static inline int dm_pm_down_target(int si, int ns, int next_ok, int table_shown, int have_back) {
    if (si >= 0 && si + 1 < ns && next_ok) return DM_PM_DOWN_NEXT;
    return table_shown && have_back ? DM_PM_DOWN_TABLE : DM_PM_DOWN_STAY;
}
// 格式串（来自翻译表）正好是两个 %d、别的 % 只能是 %%：不然不拿它当格式串用（翻译出错时不读野参数） | the format (from the translation table) holds exactly two %d and no other conversion except %%; otherwise it is not used (a bad translation never reads stray arguments)
static inline int dm_fmt_two_d(const char *f) {
    int n = 0; for (; f && *f; f++) if (*f == '%') { if (f[1] == '%') { f++; continue; } if (f[1] != 'd') return 0; n++; f++; }
    return n == 2;
}
// 难度说明：n = 0 选择条刚展开（只有副文一句），1–8 刚选中这一档（正文「游戏难度 n：相当于同局 n 人」+ 副文）。正文 36 号主色、副文 28 号浅灰，
// 由 send（dm_bossrush.c dm_game_notice_ui_pair）原样排进提示队列，两级显示见 dm_notice_pair.h
// Difficulty tip: n = 0 the selector just opened (subtext line only), 1-8 this level was just picked (body "Game Difficulty n: same as n players" + subtext). Body
// at 36 pt primary, subtext at 28 pt light grey, queued unchanged by send (dm_game_notice_ui_pair in dm_bossrush.c); two-level display in dm_notice_pair.h
static inline void dm_diff_tip_send(uint8_t *b, int n, const char *body_fmt, const char *sub, void (*send)(uint8_t *, const char *, const char *)) {
    char body[400] = ""; if (n > 0 && n <= 8 && dm_fmt_two_d(body_fmt)) snprintf(body, sizeof body, body_fmt, n, n);
    send(b, body, sub ? sub : "");
}
