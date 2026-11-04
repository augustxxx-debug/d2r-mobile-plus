// B144：设置页焦点行滚出列表可见区时清焦点的判断（dm_focus_clip_want），外加源码核对：焦点指针只在 focus_clip_clear / focus_clip_put 里写、每帧泵已接上。
// B144: the decision to clear the focus when the focused row scrolls out of the visible list (dm_focus_clip_want), plus source checks: the focus pointer is written only in focus_clip_clear / focus_clip_put, and the per-frame pump is wired.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int dm_focus_clip_want(int touch_mode, int settings_page, int focus_in_list, int frame_in_view);
#include "../dm_cursor.c"
module_t g_mod[MAX_MOD]; int g_nmod; uint64_t dm_guest_frame(void) { return 0; }
static int fails;
#define EQ(a, b, w) do { if ((a) != (b)) { printf("FAIL %s\n", w); fails++; } } while (0)
int main(void) {
    EQ(dm_focus_clip_want(1, 1, 1, 0), 1, "触屏 + 设置页 + 焦点在列表里 + 金框不在可见区 → 清 | touch, settings, in list, out of view -> clear");
    EQ(dm_focus_clip_want(1, 1, 1, 1), 0, "金框在可见区 → 不动 | in view -> keep");
    EQ(dm_focus_clip_want(0, 1, 1, 0), 0, "手柄模式 → 不动 | controller mode -> keep");
    EQ(dm_focus_clip_want(1, 0, 1, 0), 0, "不在设置页 → 不动 | not settings -> keep");
    EQ(dm_focus_clip_want(1, 1, 0, 0), 0, "焦点在列表外（应用 / 默认 / 放弃）→ 不动 | focus outside the list (Apply / Default / Discard) -> keep");
    FILE *f = fopen("../dm_cursor.c", "rb"); if (!f) { puts("FAIL 读不到 ../dm_cursor.c"); return 1; }
    fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f); char *s = malloc(n + 1); fread(s, 1, n, f); s[n] = 0; fclose(f);
    // 写焦点指针的地方只许两处，且都在 focus_clip_clear / focus_clip_put 那两行里 | only two writes of the focus pointer, both on the focus_clip_clear / focus_clip_put lines
    int bad = 0, cnt = 0; for (char *line = strtok(s, "\n"); line; line = strtok(NULL, "\n")) { char *w = strstr(line, "*(uint64_t *)(fm + 0x190) ="); if (!w) continue; cnt++; if (!strstr(line, "static void focus_clip_clear") && !strstr(line, "static void focus_clip_put")) bad++; }
    EQ(cnt, 2, "焦点指针恰好写两处 | exactly two focus-pointer writes"); EQ(bad, 0, "只在 focus_clip_clear / focus_clip_put 里写 | only inside focus_clip_clear / focus_clip_put");
    f = fopen("../dm_cursor.c", "rb"); fread(s, 1, n, f); s[n] = 0; fclose(f);
    EQ(strstr(s, "focus_clip_pump(proot, phh);") != NULL, 1, "每帧泵已接上 | per-frame pump wired");
    free(s);
    if (fails) { printf("t_focus_clip: %d FAIL\n", fails); return 1; }
    puts("PASS B144 focus clip: clear only in touch mode on a settings page with the focused row in the list and out of view; focus pointer written only in the two guarded helpers; pump wired");
    return 0;
}
