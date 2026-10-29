// 左摇杆 → 方向键（公共，纯函数；RC1.1 B006b）：我们自己画的界面（图形 / 自定义页、社区 / 关于弹窗、积分牌、大秘境、建角页等）手柄导航只认方向键，
// 左摇杆要折算成同样的「方向键按着」才与原版菜单一样能用。推过一半算按下（±16000 / 32767），回到四分之一以内（±8000）才松开（回滞，防抖不连发），
// 两个轴都推着时只取推得多的那个（菜单里斜推不同时走两步）。返回 DM_BTN_UP / DOWN / LEFT / RIGHT 之一或 0；按下沿由调用处与上一拍比较得出。Horizon 摇杆上为正
// Left stick -> direction keys (shared, pure; RC1.1 B006b): the UIs we draw ourselves only navigate with the D-pad, so the left stick is turned into the same "direction key held".
// Past half (±16000 of 32767) counts as pressed, released only back inside a quarter (±8000) (hysteresis, no chatter, no repeat); with both axes pushed only the dominant one counts
// (a diagonal push never moves two steps in a menu). Returns one of DM_BTN_UP / DOWN / LEFT / RIGHT or 0; press edges are derived by the caller against the previous sample. Horizon: up is positive
#pragma once
#include <stdint.h>
#include "dm_input.h"
typedef struct { int hx, hy; } dm_stick_nav;
static inline uint64_t dm_stick_nav_dirs(dm_stick_nav *st, int32_t x, int32_t y) {
    if (!st) return 0;
    st->hx = x > 16000 ? 1 : x < -16000 ? -1 : (x > -8000 && x < 8000) ? 0 : st->hx;
    st->hy = y > 16000 ? 1 : y < -16000 ? -1 : (y > -8000 && y < 8000) ? 0 : st->hy;
    int32_t ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
    if (ay >= ax) return st->hy > 0 ? DM_BTN_UP : st->hy < 0 ? DM_BTN_DOWN : 0;
    return st->hx > 0 ? DM_BTN_RIGHT : st->hx < 0 ? DM_BTN_LEFT : 0;
}
