// iPhone Duo 切屏的方向判定（B115）。模拟器 Duo 上查到的系统行为（2026-11-01，iOS 27.1 的 SpringBoard 日志 + 探针）：
// ① 内屏 951x669 的「宽」握法是设备竖持（铰链竖着），此时界面方向是 4（LandscapeLeft）；合上时设备方向还是竖持，游戏只许横屏，系统就把 4 原样带到外屏。
// ② 系统在切屏全程（到完全合上）扣住设备方向更新，放开时带动画（0.6 秒）转过去。
// 于是玩家合上后逆时针转到横向（设备方向 3，要界面方向 3）时，先看到倒着的画面，再转一圈才正。
// 所以：合上且设备没给出横向时，落到 3（Info.plist 首选方向，也是逆时针转横的方向）。那个转动是系统在进程外做的（探针里本进程各层都没有动画），App 去不掉，只能让它不发生。
// 设备方向已是横向时系统自己算对了（内外屏的同一姿态方向相反，它已换算），不插手。翻开也不插手，内屏只有系统选的方向是宽的。
// Orientation decisions for an iPhone Duo screen switch (B115). System behaviour found on the Duo simulator (2026-11-01, iOS 27.1 SpringBoard log + a probe):
// (1) The wide hold of the 951x669 inner screen is device portrait (hinge vertical), where the interface orientation is 4 (LandscapeLeft); on closing the device is still portrait, the game allows landscape only, so the system carries 4 over to the cover screen as is.
// (2) The system holds back device orientation updates for the whole switch (until fully closed) and applies them animated (0.6 s) when released.
// So a player who closes and then turns the phone counter-clockwise to landscape (device 3, wanting interface 3) first sees the picture upside down, then watches it spin upright.
// Hence: on closing with no landscape device orientation, land on 3 (the Info.plist preferred one, and the counter-clockwise turn). The spin is done by the system outside this process (the probe saw no animation on any of our layers), so the App cannot remove it, only avoid needing it.
// When the device orientation is already landscape the system got it right (the same pose reads opposite on the two screens and it converts), so it is left alone. Opening is also left alone: on the inner screen only the system's choice is wide.
#include "dm_foldland.h"

int dm_foldland_screen(int *last_short, int native_short) {
    if (native_short <= 0 || native_short == *last_short) return DM_FL_NONE;
    int old = *last_short; *last_short = native_short;
    if (old <= 0) return DM_FL_NONE;   // 第一次是启动时的屏，不算切屏 | The first one is the launch screen, not a switch
    return native_short < old ? DM_FL_CLOSING : DM_FL_OPENING;
}

int dm_foldland_target(int kind, int cur_io, int dev) {
    if (kind != DM_FL_CLOSING) return 0;
    if (dev == DM_FL_DEV_LEFT || dev == DM_FL_DEV_RIGHT) return 0;
    if (cur_io != DM_FL_IO_LEFT) return 0;   // 已是 3，或不是横屏（系统还没定）| Already 3, or not landscape (the system has not settled)
    return DM_FL_IO_RIGHT;
}
