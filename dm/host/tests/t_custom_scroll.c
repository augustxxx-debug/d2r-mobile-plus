// 自定义页手柄滚动决策（dm_custom_scroll.h）：行在框内不发；行在下面发 -1、上面发 +1；上一格没真的滚动（到头）就停；滚动了就继续；换行从头来。
// clang -I.. -o build/t_custom_scroll t_custom_scroll.c
// Custom page controller scroll decision (dm_custom_scroll.h): no notch when the row is inside; -1 below, +1 above; stop when the previous notch did not move (at the end); continue when it did; a new row starts over.
#include "../dm_custom_scroll.h"
#include <stdio.h>
#include <stdlib.h>
static int checks; static void check(int ok, const char *why) { checks++; if (!ok) { fprintf(stderr, "FAIL %s\n", why); exit(1); } }
int main(void) {
    check(dm_custom_scroll_step(200, 380, 0, 1260, 0, -1) == 0, "row inside the box: no notch");
    check(dm_custom_scroll_step(2700, 2890, 0, 1260, 0, -1) == -1, "row below: scroll down");
    check(dm_custom_scroll_step(2700, 2890, 1500, 1260, -1, 0) == -1, "offset moved since the last notch: keep scrolling");
    check(dm_custom_scroll_step(2700, 2890, 1620, 1260, -1, 1500) == -1, "still outside, offset moved again: another notch");
    check(dm_custom_scroll_step(2700, 2890, 1620, 1260, -1, 1620) == 0, "last row: bottom edge never fits and the offset did not move -> at the end, stop (no more click sounds)");
    check(dm_custom_scroll_step(2700, 2890, 1620, 1260, -1, 1620) == 0, "stays stopped while nothing changes");
    check(dm_custom_scroll_step(100, 280, 1620, 1260, 0, -1) == 1, "new row above (caller reset last_dir / last_off): scroll up");
    check(dm_custom_scroll_step(100, 280, 0, 1260, 1, 0) == 0, "top reached with the row inside: no notch");
    check(dm_custom_scroll_step(-10, 170, 0, 1260, 1, 0) == 0, "row above the top with offset stuck at 0: stop");
    check(dm_custom_scroll_step(2700, 2890, 1620, 1260, 1, 1620) == -1, "direction changed since the last notch: send even with the same offset");
    printf("PASS t_custom_scroll: %d checks\n", checks); return 0;
}
