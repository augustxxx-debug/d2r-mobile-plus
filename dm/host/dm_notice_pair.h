// B035 两级提示（正文 36 号主色 + 副文 28 号浅灰）只属于「它那一条」通知：投递时记下（键 = 交给原版通知的那串字，正文 / 副文），
// 提示框排版（dm_cursor.c ui_notice_prompt_center）每帧拿原版 Prompt 此刻显示的字来查，查到才把我们的两个控件摆出来，查不到就清空、挪到屏外。
// 原版通知有自己的队列（tests/notice_queue.py），Prompt 显示的永远是「此刻轮到的那一条」，所以按字绑定后：原版系统提示、别的 Plus 通知、
// 排在难度提示前后的通知都不会挂上副文；难度提示在原版队列里排着时，副文也不会提前挂到别人下面。纯函数，单测 t_notice_dispatch / t_notice_widgets。
// 只许游戏主线程用（投递在 hle_hid.c fill 的取手柄时刻，排版在 ui_note_dock_pump，都是游戏主线程）
// B035 two-level tips (36 pt primary body + 28 pt light-grey subtext) belong to "their own" notice only: recorded on delivery (key = the exact text handed to the
// original notice, plus body / subtext). The notice layout (ui_notice_prompt_center in dm_cursor.c) looks up the text the native Prompt is showing right now, every
// frame; only a hit shows our two widgets, a miss clears and parks them. The original notice has its own queue (tests/notice_queue.py) and Prompt always shows
// the entry whose turn it is, so with the text binding no original system notice, other Plus notice or notice queued around the tip gets the subtext, and a tip
// still waiting in the native queue does not hang its subtext under someone else. Pure functions, unit tests t_notice_dispatch / t_notice_widgets.
// Game main thread only (delivery happens at the controller poll in hle_hid.c fill, layout in ui_note_dock_pump, both on the game main thread)
#pragma once
#include <string.h>
#include <stddef.h>
enum { DM_NOTICE_PAIR_N = 4, DM_NOTICE_PAIR_BYTES = 1024 };
typedef struct { char body[DM_NOTICE_PAIR_BYTES], sub[DM_NOTICE_PAIR_BYTES]; } dm_notice_pair;
typedef struct { dm_notice_pair e[DM_NOTICE_PAIR_N]; unsigned next; } dm_notice_pairs;
// 交给原版通知的那串字：正文为空（选择条刚展开，只有一句副文）时用副文，原版通知从不收到空串（审计 N1） | the text handed to the original notice: the subtext when the body is empty (selector just opened, one subtext line only), so the original never receives an empty string (audit N1)
static inline const char *dm_notice_pair_key(const char *body, const char *sub) { return body && *body ? body : sub ? sub : ""; }
// 记下一条两级提示；没有副文的不记（普通通知不占位）。返回 1 = 记了 | record one two-level tip; entries without a subtext are not recorded (plain notices take no slot). Returns 1 = recorded
static inline int dm_notice_pair_add(dm_notice_pairs *p, const char *body, const char *sub) {
    body = body ? body : ""; if (!sub || !*sub || strlen(body) >= DM_NOTICE_PAIR_BYTES || strlen(sub) >= DM_NOTICE_PAIR_BYTES) return 0;
    dm_notice_pair *e = &p->e[p->next++ % DM_NOTICE_PAIR_N]; strcpy(e->body, body); strcpy(e->sub, sub); return 1;
}
// Prompt 此刻显示的字（不一定以 0 结尾，按长度比）属于哪一条两级提示；不属于 = NULL | which two-level tip the text Prompt shows right now belongs to (not necessarily 0-terminated, compared by length); none = NULL
static inline const dm_notice_pair *dm_notice_pair_find(const dm_notice_pairs *p, const char *text, size_t len) {
    if (!p || !text || !len) return NULL;
    for (unsigned k = 0; k < DM_NOTICE_PAIR_N; k++) { const dm_notice_pair *e = &p->e[(p->next + DM_NOTICE_PAIR_N - 1 - k) % DM_NOTICE_PAIR_N];   // 新的先查 | newest first
        const char *key = dm_notice_pair_key(e->body, e->sub); if (*e->sub && strlen(key) == len && !memcmp(key, text, len)) return e; }
    return NULL;
}
