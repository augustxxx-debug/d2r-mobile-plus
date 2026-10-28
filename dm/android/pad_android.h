// 安卓实体手柄（iOS dm_gamepad.m 的安卓版；原版 Switch 本来就是手柄操作，复用原版手柄交互）：Java 把 KeyEvent / MotionEvent 交来，这里按
// dm_padmap.h 的规则翻成原版手柄键位、各手柄各存各的再合并、定「手柄 / 触屏」模式（dm_padmode_request）、交给宿主 dm_pad_set。
//   · 面键：安卓给的 BUTTON_A/B/X/Y 对非任天堂手柄按物理位置（下 A、右 B、左 X、上 Y）→ 用 Xbox 键位（下确认、右返回、上 = X、左 = Y）；
//     任天堂手柄（厂商 0x057e）系统按键帽字母给 → 只换 A/B。键帽样式：任天堂 → Switch 键帽，索尼（0x054c）→ PS 键帽，其余 → Xbox 键帽。
//   · 手柄来源的 KEYCODE_BACK 当作 B（奥丁 Xbox 布局的 B 键发的是 BACK，不拦就退到桌面）；键盘 / 系统导航来的 BACK 不拦。
//   · 肩键 L1/R1 → L/R，扳机 L2/R2 或模拟扳机 > 0.5 → ZL/ZR，摇杆按下 → LS/RS，START → ＋，SELECT → －，十字键与 HAT 轴 → 方向；MODE（Xbox 徽标）吃掉不发。
//   · 摇杆 ±32767，安卓 Y 轴向下为正、原版向上为正，Y 取反；迟滞：推过 25% 算推出、回到 15% 内算回中；碰过屏幕后要先回中再推出才重新接管。
//   · 谁有明确输入谁接管：按键或推出摇杆 → 手柄模式；碰屏幕（dm_android_pad_touched）→ 触屏模式；失活 / 进后台（dm_android_pad_reset）清零。
//   · 触屏模式不交摇杆；手柄模式下没推出来的摇杆给 0。ZR 空格免费回城同 iOS。
// 单测 tests/pad_android_test.c（定义 DM_PAD_ANDROID_TEST，宿主符号由测试打桩）。
// Android physical controllers (the Android version of iOS dm_gamepad.m; the original Switch game is controller-driven, so the original controller interaction is reused):
// Java hands KeyEvent / MotionEvent over; this translates them to the original controller buttons per dm_padmap.h, keeps each controller separately and merges,
// decides the controller / touch mode (dm_padmode_request) and hands the result to the host's dm_pad_set.
//   - Face buttons: Android reports BUTTON_A/B/X/Y by physical position for non-Nintendo controllers (bottom A, right B, left X, top Y) -> the Xbox layout
//     (bottom confirm, right back, top = X, left = Y); Nintendo controllers (vendor 0x057e) are reported by keycap letter -> only A/B swap. Glyphs: Nintendo ->
//     Switch, Sony (0x054c) -> PlayStation, everything else -> Xbox.
//   - KEYCODE_BACK from a controller source counts as B (the Thor's Xbox-layout B sends BACK; unhandled it exits to the launcher); BACK from the keyboard /
//     system navigation is not intercepted.
//   - L1/R1 -> L/R, L2/R2 or analog triggers > 0.5 -> ZL/ZR, stick clicks -> LS/RS, START -> +, SELECT -> -, D-pad keys and HAT axes -> directions; MODE (the
//     Xbox logo) is swallowed.
//   - Sticks +-32767; Android's Y axis is positive downwards while the original is positive upwards, so Y is negated; hysteresis: past 25% counts as pushed out,
//     back within 15% as centered; after the screen was touched a stick must return to center before it takes over again.
//   - Whoever gives clear input takes over: a button or a pushed-out stick -> controller mode; a screen touch (dm_android_pad_touched) -> touch mode;
//     resign / background (dm_android_pad_reset) clears everything.
//   - Sticks are not delivered in touch mode; in controller mode a stick that is not pushed out gives 0. The empty-ZR free town portal is as on iOS.
// Unit test tests/pad_android_test.c (defines DM_PAD_ANDROID_TEST; the host symbols are stubbed by the test).
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// 安卓常量（android.view.KeyEvent / MotionEvent / InputDevice 的数值，Java 侧原样传） | Android constants (the numeric values of android.view.KeyEvent / MotionEvent / InputDevice, passed as-is by Java)
enum { DM_AKEY_BACK = 4, DM_AKEY_DPAD_UP = 19, DM_AKEY_DPAD_DOWN = 20, DM_AKEY_DPAD_LEFT = 21, DM_AKEY_DPAD_RIGHT = 22, DM_AKEY_MENU = 82,
       DM_AKEY_BUTTON_A = 96, DM_AKEY_BUTTON_B = 97, DM_AKEY_BUTTON_C = 98, DM_AKEY_BUTTON_X = 99, DM_AKEY_BUTTON_Y = 100, DM_AKEY_BUTTON_Z = 101,
       DM_AKEY_BUTTON_L1 = 102, DM_AKEY_BUTTON_R1 = 103, DM_AKEY_BUTTON_L2 = 104, DM_AKEY_BUTTON_R2 = 105, DM_AKEY_BUTTON_THUMBL = 106, DM_AKEY_BUTTON_THUMBR = 107,
       DM_AKEY_BUTTON_START = 108, DM_AKEY_BUTTON_SELECT = 109, DM_AKEY_BUTTON_MODE = 110 };
enum { DM_ASRC_KEYBOARD = 0x101, DM_ASRC_DPAD = 0x201, DM_ASRC_GAMEPAD = 0x401, DM_ASRC_JOYSTICK = 0x01000010 };
enum { DM_AVENDOR_NINTENDO = 0x057e, DM_AVENDOR_SONY = 0x054c, DM_AVENDOR_MICROSOFT = 0x045e };
// 按键：返回 1 = 这个事件归手柄（Java 不再交给系统，BACK 不会退出）；0 = 不是手柄事件 | key: returns 1 = the event belongs to the controller (Java does not pass it on; BACK does not exit); 0 = not a controller event
int dm_android_pad_key(int device, int vendor, int product, int source, int keycode, int down);
// 摇杆 / 扳机 / HAT 轴（安卓原值 −1..1，扳机 0..1）：返回 1 = 已处理 | sticks / triggers / HAT axes (Android raw values -1..1, triggers 0..1): returns 1 = handled
int dm_android_pad_motion(int device, int vendor, int product, int source, float lx, float ly, float rx, float ry, float lt, float rt, float hx, float hy);
void dm_android_pad_drop(int device);   // 设备拔掉 | device removed
void dm_android_pad_touched(void);      // 碰了屏幕 → 触屏模式 | the screen was touched -> touch mode
void dm_android_pad_reset(void);        // 失活 / 进后台：键与摇杆清零 | resign / background: buttons and sticks cleared
int dm_android_pad_mode(void);          // 1 = 手柄模式 | 1 = controller mode
int dm_android_pad_overlay_hidden(void);   // 宿主切换事务完成后的当前模式（front_touch_overlay_mode）：1 = 手柄模式 | current mode after the host's switch transaction (front_touch_overlay_mode): 1 = controller mode
// 触控层在各模式下画什么（Java TouchOverlay 每帧问 nativeHudMask）：触屏模式全画；手柄模式照 iOS 藏触屏控件与触屏底栏（原版手柄界面自己有底栏），
// 但经验行文字（Lv · % · FPS）照画——原版手柄底栏没有这一行（生命 / 法力数字它自带，不重画）（用户 2026-10-28：进手柄模式后 XP 数字和帧数没了）
// What the touch overlay draws in each mode (Java TouchOverlay asks nativeHudMask every frame): touch mode draws everything; controller mode hides the touch controls and the touch
// bottom bar as iOS does (the original controller UI has its own bar) but keeps the experience line text (Lv · % · FPS), which the original controller bar lacks (it has its own life / mana numbers, so those are not redrawn)
// (user 2026-10-28: the XP digits and the frame rate vanished in controller mode)
// 决策表在公共 dm/host/dm_hud_mask.h（iOS 同一份） | the decision table is the shared dm/host/dm_hud_mask.h (the same one iOS uses)
#include "../host/dm_hud_mask.h"
enum { DM_PAD_HUD_CONTROLS = DM_HUD_CONTROLS, DM_PAD_HUD_BAR = DM_HUD_BAR, DM_PAD_HUD_TEXT = DM_HUD_EXPLINE };
int dm_android_pad_hud_mask(int pad_mode);
#ifdef __cplusplus
}
#endif
