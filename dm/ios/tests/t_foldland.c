// B115：iPhone Duo 切屏的方向判定。序列取自 Duo 模拟器（iOS 27.1）上探针与 SpringBoard 日志的实测
// B115: orientation decisions for an iPhone Duo screen switch. The sequences come from a probe and the SpringBoard log on the Duo simulator (iOS 27.1)
#include <assert.h>
#include <stdio.h>
#include "dm_foldland.h"
enum { COVER = 1398, INNER = 2007, DEV_UNKNOWN = 0, DEV_PORTRAIT = 1, DEV_UPSIDE = 2, DEV_UP = 5, DEV_DOWN = 6 };
int main(void) {
    int s = 0;
    // 启动那一屏不算切屏 | The launch screen is not a switch
    assert(dm_foldland_screen(&s, INNER) == DM_FL_NONE);
    // 同一屏反复回调（一次切屏实测来好几次）| Repeated callbacks on one screen (one switch delivers several)
    assert(dm_foldland_screen(&s, INNER) == DM_FL_NONE);
    assert(dm_foldland_screen(&s, 0) == DM_FL_NONE);
    assert(dm_foldland_screen(&s, COVER) == DM_FL_CLOSING);
    assert(dm_foldland_screen(&s, COVER) == DM_FL_NONE);
    assert(dm_foldland_screen(&s, INNER) == DM_FL_OPENING);
    // 内屏宽握法（设备竖持、界面 4）合上：改落到 3，这是 B115 的现场 | Closing from the wide inner hold (device portrait, interface 4): land on 3, the B115 scene
    assert(dm_foldland_target(DM_FL_CLOSING, DM_FL_IO_LEFT, DEV_PORTRAIT) == DM_FL_IO_RIGHT);
    for (int d = 0; d < 7; d++) if (d != DM_FL_DEV_LEFT && d != DM_FL_DEV_RIGHT) assert(dm_foldland_target(DM_FL_CLOSING, DM_FL_IO_LEFT, d) == DM_FL_IO_RIGHT);
    (void)DEV_UNKNOWN; (void)DEV_UPSIDE; (void)DEV_UP; (void)DEV_DOWN;
    // 已是 3 不动 | Already 3: leave it
    assert(dm_foldland_target(DM_FL_CLOSING, DM_FL_IO_RIGHT, DEV_PORTRAIT) == 0);
    // 设备已给出横向：系统已换算（实测内屏 3 → 外屏界面 4、设备方向下一拍才更新），两个方向都不插手
    // Device already landscape: the system converted it (measured inner 3 -> cover interface 4, the device value updates one event later); leave both alone
    assert(dm_foldland_target(DM_FL_CLOSING, DM_FL_IO_LEFT, DM_FL_DEV_LEFT) == 0);
    assert(dm_foldland_target(DM_FL_CLOSING, DM_FL_IO_LEFT, DM_FL_DEV_RIGHT) == 0);
    assert(dm_foldland_target(DM_FL_CLOSING, DM_FL_IO_RIGHT, DM_FL_DEV_LEFT) == 0);
    assert(dm_foldland_target(DM_FL_CLOSING, DM_FL_IO_RIGHT, DM_FL_DEV_RIGHT) == 0);
    // 翻开、没切、方向未定都不动 | Opening, no switch, or an unsettled orientation: leave it
    for (int io = 0; io <= 4; io++) for (int d = 0; d < 7; d++) { assert(dm_foldland_target(DM_FL_OPENING, io, d) == 0); assert(dm_foldland_target(DM_FL_NONE, io, d) == 0); }
    assert(dm_foldland_target(DM_FL_CLOSING, 0, DEV_PORTRAIT) == 0);
    assert(dm_foldland_target(DM_FL_CLOSING, 1, DEV_PORTRAIT) == 0);
    puts("t_foldland OK");
    return 0;
}
