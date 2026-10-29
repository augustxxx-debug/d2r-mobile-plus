// 左摇杆 → 方向键（公共，纯函数；RC1.1 B006b）：我们自己画的界面（图形 / 自定义页、社区 / 关于弹窗、积分牌、大秘境、建角页等）手柄导航只认方向键，
// 左摇杆要折算成同样的「方向键按着」才与原版菜单一样能用。推过一半算按下（±16000 / 32767），回到四分之一以内（±8000）才松开（回滞，防抖不连发），
// 两个轴都推着时只取推得多的那个（菜单里斜推不同时走两步）。返回 DM_BTN_UP / DOWN / LEFT / RIGHT 之一或 0；按下沿由调用处与上一拍比较得出。Horizon 摇杆上为正
// Left stick -> direction keys (shared, pure; RC1.1 B006b): the UIs we draw ourselves only navigate with the D-pad, so the left stick is turned into the same "direction key held".
// Past half (±16000 of 32767) counts as pressed, released only back inside a quarter (±8000) (hysteresis, no chatter, no repeat); with both axes pushed only the dominant one counts
// (a diagonal push never moves two steps in a menu). Returns one of DM_BTN_UP / DOWN / LEFT / RIGHT or 0; press edges are derived by the caller against the previous sample. Horizon: up is positive
#pragma once
#include <stdint.h>
#include "dm_input.h"
// 主轴锁定（RC1.1 独立审计 P2）：原先每一拍按两轴大小重新选主轴，没有回滞——斜推在 45 度附近抖、或在中间带里斜着回中时，主轴来回换、每换一次多一个按下沿，菜单多跳一两格。
// 现在主轴一旦选定就锁住，直到两个轴都回到四分之一以内（各自松开）才重新选；锁住期间另一个轴推得再多也不出方向（要换方向先回中）
// Dominant-axis lock (RC1.1 independent audit P2): the axis used to be re-chosen every sample with no hysteresis, so a diagonal push jittering around 45 degrees, or a diagonal return
// through the middle band, swapped axes and each swap added a press edge (the menu jumped an extra step or two). Now the chosen axis stays locked until both axes are back inside a quarter
// (each released); while locked, the other axis gives no direction however far it is pushed (return to centre to change direction)
typedef struct { int hx, hy, axis; } dm_stick_nav;   // axis：0 未选 / 1 横 / 2 纵 | axis: 0 none / 1 horizontal / 2 vertical
static inline void dm_stick_nav_reset(dm_stick_nav *st) { if (st) st->hx = st->hy = st->axis = 0; }
static inline uint64_t dm_stick_nav_dirs(dm_stick_nav *st, int32_t x, int32_t y) {
    if (!st) return 0;
    st->hx = x > 16000 ? 1 : x < -16000 ? -1 : (x > -8000 && x < 8000) ? 0 : st->hx;
    st->hy = y > 16000 ? 1 : y < -16000 ? -1 : (y > -8000 && y < 8000) ? 0 : st->hy;
    if (!st->hx && !st->hy) st->axis = 0;   // 两轴都松开：解锁 | both released: unlock
    else if (!st->axis) { int32_t ax = x < 0 ? -x : x, ay = y < 0 ? -y : y; st->axis = !st->hx ? 2 : !st->hy ? 1 : ay >= ax ? 2 : 1; }   // 只有一个轴按着取它；两个都按着取推得多的 | one axis held: that one; both: the larger
    if (st->axis == 2) return st->hy > 0 ? DM_BTN_UP : st->hy < 0 ? DM_BTN_DOWN : 0;
    if (st->axis == 1) return st->hx > 0 ? DM_BTN_RIGHT : st->hx < 0 ? DM_BTN_LEFT : 0;
    return 0;
}
// 页接管时摇杆值哪部分不交给游戏（RC1.1 终版审计 P1-1）：只扣下页声明了的那个方向——纵轴正值只在声明了「上」时清零、负值只在声明了「下」时清零，横轴同理。
// 原先页只要声明了上或下就把整个纵轴清零：「游戏」页焦点在表格第一行时只声明「上」（B015），往下推摇杆原版收不到、页也不要，「下」就丢了
// Which part of the stick a page withholds from the game (RC1.1 final audit P1-1): only the declared direction -- a positive vertical value is zeroed only when Up is declared,
// a negative one only when Down is, likewise horizontally. Previously any Up or Down declaration zeroed the whole vertical axis: with the Gameplay page's focus on the table's
// first row only Up is declared (B015), so pushing the stick down reached neither the game nor the page and Down was lost
static inline void dm_stick_nav_withhold(uint64_t declared, int32_t *x, int32_t *y) {
    if (!x || !y) return;
    if ((*y > 0 && (declared & DM_BTN_UP)) || (*y < 0 && (declared & DM_BTN_DOWN))) *y = 0;
    if ((*x > 0 && (declared & DM_BTN_RIGHT)) || (*x < 0 && (declared & DM_BTN_LEFT))) *x = 0;
}
