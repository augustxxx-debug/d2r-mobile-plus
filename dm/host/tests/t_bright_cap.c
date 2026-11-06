// 3D 画面加亮上限（dm_bright_cap.h）：解析 DM_BRIGHT_MAX（B098 起缺省 2）、对数空间封顶。clang -I.. -o build/t_bright_cap t_bright_cap.c
// Brightening exposure cap (dm_bright_cap.h): parsing DM_BRIGHT_MAX (default 2 since B098) and capping in log space.
#include "../dm_bright_cap.h"
#include <stdio.h>
static int checks; static void check(int ok, const char *why) { checks++; if (!ok) { fprintf(stderr, "FAIL %s\n", why); exit(1); } }
int main(void) {
    int bad;
    check(dm_bright_max_parse(NULL, &bad) == 2.0 && !bad, "unset: default 2 on every platform (B098)");
    check(dm_bright_max_parse("", &bad) == 2.0 && !bad, "empty: default 2");
    check(dm_bright_max_parse("2", &bad) == 2.0 && !bad, "2");
    check(dm_bright_max_parse("1.5", &bad) == 1.5 && !bad, "1.5");
    check(dm_bright_max_parse("1", &bad) == 1.0 && !bad, "1 = original brightness");
    check(dm_bright_max_parse("4", &bad) == 4.0 && !bad, "4 = the original rule (future brightness setting High)");
    check(dm_bright_max_parse("5", &bad) == 2.0 && bad, "above 4 is invalid -> default 2");
    check(dm_bright_max_parse("0.5", &bad) == 2.0 && bad, "below 1 is invalid -> default 2");
    check(dm_bright_max_parse("abc", &bad) == 2.0 && bad, "not a number -> default 2");
    check(dm_bright_max_parse("2x", &bad) == 2.0 && bad, "trailing junk -> default 2");
    check(fabs(dm_bright_cap_logk(log(4.0), 2.0) - log(2.0)) < 1e-12, "maze rule k=4 capped to 2");
    { int b2; check(fabs(dm_bright_cap_logk(log(4.0), dm_bright_max_parse(NULL, &b2)) - log(2.0)) < 1e-12, "default: maze rule k=4 ends at 2 (B098)"); }
    check(fabs(dm_bright_cap_logk(log(1.5), 2.0) - log(1.5)) < 1e-12, "k=1.5 under the cap unchanged");
    check(dm_bright_cap_logk(0.0, 2.0) == 0.0, "k=1 (out of a run) unchanged");
    check(fabs(dm_bright_cap_logk(log(4.0), 4.0) - log(4.0)) < 1e-12, "cap 4 = original rule");
    check(dm_bright_cap_logk(log(3.0), 1.0) == 0.0, "cap 1 = never brighten");
    // 图形页四档 | the Graphics page's four levels
    check(dm_bright_level_cap(0) == 1.0 && dm_bright_level_cap(1) == 1.4 && dm_bright_level_cap(2) == 2.0 && dm_bright_level_cap(3) == 4.0, "levels off / low / mid / high");
    check(dm_bright_level_cap(-1) == DM_BRIGHT_MAX_DEFAULT && dm_bright_level_cap(4) == DM_BRIGHT_MAX_DEFAULT, "bad level: default cap");
    printf("PASS t_bright_cap: %d checks\n", checks); return 0;
}
