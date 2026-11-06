// 游戏页手柄焦点（dm_padmode_pure.h，审计 r1 P1-2）：最后一个停靠点（「更多选项」折叠钮）按下——原版表格那组收起时留在原地，展开时才回表格；中间的停靠点照常往下
// Gameplay controller focus (dm_padmode_pure.h, audit r1 P1-2): Down on the last stop (the "More options" fold button) stays put while the original table's group is
// collapsed and returns to the table only when it is expanded; middle stops move down as usual
#include "../dm_padmode_pure.h"
#include <assert.h>
int main(void) {
    enum { NS = 9 };
    assert(dm_pm_down_target(0, NS, 1, 0, 1) == DM_PM_DOWN_NEXT && dm_pm_down_target(NS - 2, NS, 1, 1, 1) == DM_PM_DOWN_NEXT);
    assert(dm_pm_down_target(NS - 1, NS, 0, 0, 1) == DM_PM_DOWN_STAY);    // 缺省收起：留在末个停靠点 | collapsed by default: stay on the last stop
    assert(dm_pm_down_target(NS - 1, NS, 0, 1, 1) == DM_PM_DOWN_TABLE);   // 展开：回原版表格那一行 | expanded: back to the original table row
    assert(dm_pm_down_target(NS - 1, NS, 0, 1, 0) == DM_PM_DOWN_STAY);    // 不知道回哪一行：留着 | row to return to unknown: stay
    assert(dm_pm_down_target(3, NS, 0, 0, 1) == DM_PM_DOWN_STAY);         // 下一个停靠点没找到（控件不在）且表格收起：留着 | next stop missing and the table collapsed: stay
    assert(dm_pm_down_target(-1, NS, 1, 0, 1) == DM_PM_DOWN_STAY);
    puts("PASS t_padmode_pure: Down from the last stop never enters the collapsed original table");
    return 0;
}
