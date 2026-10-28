// 触屏覆盖层按模式画什么（dm_hud_mask.h，两端共用）：触屏模式全画；手柄模式只画经验行文字。clang -I.. -o build/t_hud_mask t_hud_mask.c
// What the touch overlay draws per mode (dm_hud_mask.h, shared by both platforms): touch mode everything; controller mode only the experience line text.
#include "../dm_hud_mask.h"
#include <stdio.h>
#include <stdlib.h>
static int checks; static void check(int ok, const char *why) { checks++; if (!ok) { fprintf(stderr, "FAIL %s\n", why); exit(1); } }
int main(void) {
    int t = dm_hud_draw_mask(0), p = dm_hud_draw_mask(1);
    check(t == (DM_HUD_CONTROLS | DM_HUD_BAR | DM_HUD_EXPLINE), "touch mode: controls + bar + experience line");
    check(p == DM_HUD_EXPLINE, "controller mode: experience line only");
    check(!(p & DM_HUD_CONTROLS) && !(p & DM_HUD_BAR), "controller mode: no touch controls, no touch bar (the original controller bar has its own life / mana numbers)");
    check(dm_hud_draw_mask(2) == DM_HUD_EXPLINE, "any non-zero mode value counts as controller mode");
    printf("PASS t_hud_mask: %d checks\n", checks); return 0;
}
