// 安卓手柄层（pad_android.c）：Xbox 位置键位、任天堂只换 A/B、手柄来源的 BACK 当 B 且被吃掉、键盘 BACK 不吃、轴转换与 Y 取反、扳机阈值、HAT 方向、
// 迟滞与碰屏后回中、模式切换、多手柄合并、断开、失活清零、ZR 空格回城。宿主符号打桩。
// clang -DDM_PAD_ANDROID_TEST -I.. -I../../host -o <仓外产物> pad_android_test.c ../pad_android.c -lm
// Android controller layer (pad_android.c): Xbox positional layout, Nintendo only swaps A/B, BACK from a controller source counts as B and is swallowed, keyboard BACK is
// not, axis conversion with Y negated, trigger threshold, HAT directions, hysteresis and re-centering after a touch, mode switching, multi-controller merge, removal,
// reset on resign, empty-ZR town portal. Host symbols are stubbed.
#include "../pad_android.h"
#include "../../host/dm_input.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint64_t last_b; static int32_t last_ax[4]; static int sets, mode_req = -1, mode_calls, glyph_req = -1, tp, nav = 1, zr_free;
void dm_pad_set(uint64_t b, int32_t lx, int32_t ly, int32_t rx, int32_t ry) { last_b = b; last_ax[0] = lx; last_ax[1] = ly; last_ax[2] = rx; last_ax[3] = ry; sets++; }
void dm_padmode_request(int pad) { mode_req = pad; mode_calls++; }
void dm_padmode_request_glyph(int g) { glyph_req = g; }
void dm_tp_request(void) { tp++; }
int dm_front_navigation(void) { return nav; }
int dm_freetp_zr_free(int zl) { (void)zl; return zr_free; }
static int checks; static void check(int ok, const char *why) { checks++; if (!ok) { fprintf(stderr, "FAIL %s\n", why); exit(1); } }
enum { XBOX = 1, NIN = 2, KBD = 3 };
int main(void) {
    // 键盘 BACK：不归手柄、不改状态 | keyboard BACK: not ours, no state change
    check(dm_android_pad_key(KBD, 0, 0, DM_ASRC_KEYBOARD, DM_AKEY_BACK, 1) == 0 && sets == 0 && mode_calls == 0, "keyboard BACK not consumed");
    // Xbox 布局手柄：下 A → 游戏 A，模式切手柄、键帽 Xbox | Xbox-layout controller: bottom A -> game A, controller mode, Xbox glyphs
    check(dm_android_pad_key(XBOX, DM_AVENDOR_MICROSOFT, 0x02ea, DM_ASRC_GAMEPAD, DM_AKEY_BUTTON_A, 1) == 1, "BUTTON_A consumed");
    check(last_b == DM_BTN_A && mode_req == 1 && glyph_req == DM_GLYPH_XBOX && dm_android_pad_mode() == 1, "A -> game A, controller mode, Xbox glyph");
    dm_android_pad_key(XBOX, DM_AVENDOR_MICROSOFT, 0x02ea, DM_ASRC_GAMEPAD, DM_AKEY_BUTTON_A, 0); check(last_b == 0, "A released");
    dm_android_pad_key(XBOX, DM_AVENDOR_MICROSOFT, 0x02ea, DM_ASRC_GAMEPAD, DM_AKEY_BUTTON_X, 1); check(last_b == DM_BTN_Y, "Xbox left X -> game Y");
    dm_android_pad_key(XBOX, DM_AVENDOR_MICROSOFT, 0x02ea, DM_ASRC_GAMEPAD, DM_AKEY_BUTTON_X, 0);
    dm_android_pad_key(XBOX, DM_AVENDOR_MICROSOFT, 0x02ea, DM_ASRC_GAMEPAD, DM_AKEY_BUTTON_Y, 1); check(last_b == DM_BTN_X, "Xbox top Y -> game X");
    dm_android_pad_key(XBOX, DM_AVENDOR_MICROSOFT, 0x02ea, DM_ASRC_GAMEPAD, DM_AKEY_BUTTON_Y, 0);
    // 手柄来源的 BACK = B，且被吃掉（不会退出）| BACK from the controller = B and consumed (no exit)
    check(dm_android_pad_key(XBOX, DM_AVENDOR_MICROSOFT, 0x02ea, DM_ASRC_GAMEPAD, DM_AKEY_BACK, 1) == 1 && last_b == DM_BTN_B, "controller BACK -> game B, consumed");
    check(dm_android_pad_key(XBOX, DM_AVENDOR_MICROSOFT, 0x02ea, DM_ASRC_GAMEPAD, DM_AKEY_BACK, 0) == 1 && last_b == 0, "controller BACK released");
    // 已知手柄设备发来的 BACK 即使来源标成键盘也归手柄（有的设备 BACK 带键盘来源）| BACK from a known controller device is ours even with a keyboard source (some devices tag BACK that way)
    check(dm_android_pad_key(XBOX, DM_AVENDOR_MICROSOFT, 0x02ea, DM_ASRC_KEYBOARD, DM_AKEY_BACK, 1) == 1 && last_b == DM_BTN_B, "known device BACK with keyboard source still B");
    dm_android_pad_key(XBOX, DM_AVENDOR_MICROSOFT, 0x02ea, DM_ASRC_KEYBOARD, DM_AKEY_BACK, 0);
    // 其余键 | the other buttons
    struct { int key; uint64_t bit; } m[] = { {DM_AKEY_BUTTON_L1, DM_BTN_L}, {DM_AKEY_BUTTON_R1, DM_BTN_R}, {DM_AKEY_BUTTON_L2, DM_BTN_ZL}, {DM_AKEY_BUTTON_THUMBL, DM_BTN_LS}, {DM_AKEY_BUTTON_THUMBR, DM_BTN_RS},
        {DM_AKEY_BUTTON_START, DM_BTN_PLUS}, {DM_AKEY_BUTTON_SELECT, DM_BTN_MINUS}, {DM_AKEY_DPAD_UP, DM_BTN_UP}, {DM_AKEY_DPAD_DOWN, DM_BTN_DOWN}, {DM_AKEY_DPAD_LEFT, DM_BTN_LEFT}, {DM_AKEY_DPAD_RIGHT, DM_BTN_RIGHT} };
    for (unsigned k = 0; k < sizeof m / sizeof *m; k++) { dm_android_pad_key(XBOX, DM_AVENDOR_MICROSOFT, 0x02ea, DM_ASRC_GAMEPAD, m[k].key, 1); check(last_b == m[k].bit, "button mapping"); dm_android_pad_key(XBOX, DM_AVENDOR_MICROSOFT, 0x02ea, DM_ASRC_GAMEPAD, m[k].key, 0); }
    check(dm_android_pad_key(XBOX, DM_AVENDOR_MICROSOFT, 0x02ea, DM_ASRC_GAMEPAD, DM_AKEY_BUTTON_MODE, 1) == 1 && last_b == 0, "MODE swallowed, nothing sent");
    dm_android_pad_key(XBOX, DM_AVENDOR_MICROSOFT, 0x02ea, DM_ASRC_GAMEPAD, DM_AKEY_BUTTON_MODE, 0);
    // 摇杆：Y 取反、迟滞 | sticks: Y negated, hysteresis
    check(dm_android_pad_motion(XBOX, DM_AVENDOR_MICROSOFT, 0x02ea, DM_ASRC_JOYSTICK, 0.5f, -0.5f, 0, 0, 0, 0, 0, 0) == 1, "motion consumed");
    check(last_ax[0] == 16384 && last_ax[1] == 16384 && last_ax[2] == 0, "left stick 0.5 / -0.5 -> +16384 / +16384 (Y negated)");
    dm_android_pad_motion(XBOX, DM_AVENDOR_MICROSOFT, 0x02ea, DM_ASRC_JOYSTICK, 0.18f, 0, 0, 0, 0, 0, 0, 0); check(last_ax[0] == 5898, "18% keeps the stick out (above the 15% release)");
    dm_android_pad_motion(XBOX, DM_AVENDOR_MICROSOFT, 0x02ea, DM_ASRC_JOYSTICK, 0.1f, 0, 0, 0, 0, 0, 0, 0); check(last_ax[0] == 0, "10% centers it");
    dm_android_pad_motion(XBOX, DM_AVENDOR_MICROSOFT, 0x02ea, DM_ASRC_JOYSTICK, 0.2f, 0, 0, 0, 0, 0, 0, 0); check(last_ax[0] == 0, "20% from center is not yet pushed out (25% threshold)");
    // 扳机与 HAT | triggers and HAT
    dm_android_pad_motion(XBOX, DM_AVENDOR_MICROSOFT, 0x02ea, DM_ASRC_JOYSTICK, 0, 0, 0, 0, 0.7f, 0.2f, 1, -1); check(last_b == (DM_BTN_ZL | DM_BTN_RIGHT | DM_BTN_UP), "LT > 0.5 -> ZL, RT 0.2 none, HAT right+up");
    dm_android_pad_motion(XBOX, DM_AVENDOR_MICROSOFT, 0x02ea, DM_ASRC_JOYSTICK, 0, 0, 0, 0, 0, 0, 0, 0); check(last_b == 0, "released");
    // 碰屏幕：触屏模式、摇杆不交；回中后再推出才重新接管 | touch: touch mode, sticks withheld; takes over again only after re-centering
    dm_android_pad_motion(XBOX, DM_AVENDOR_MICROSOFT, 0x02ea, DM_ASRC_JOYSTICK, 0.8f, 0, 0, 0, 0, 0, 0, 0); check(dm_android_pad_mode() == 1 && last_ax[0] == 26214, "stick delivered in controller mode");
    dm_android_pad_touched(); check(dm_android_pad_mode() == 0 && mode_req == 0 && last_ax[0] == 0, "touch -> touch mode, stick withheld");
    dm_android_pad_motion(XBOX, DM_AVENDOR_MICROSOFT, 0x02ea, DM_ASRC_JOYSTICK, 0.9f, 0, 0, 0, 0, 0, 0, 0); check(dm_android_pad_mode() == 0, "a stick still deflected does not take over");
    dm_android_pad_motion(XBOX, DM_AVENDOR_MICROSOFT, 0x02ea, DM_ASRC_JOYSTICK, 0, 0, 0, 0, 0, 0, 0, 0);
    dm_android_pad_motion(XBOX, DM_AVENDOR_MICROSOFT, 0x02ea, DM_ASRC_JOYSTICK, 0.9f, 0, 0, 0, 0, 0, 0, 0); check(dm_android_pad_mode() == 1 && last_ax[0] == 29490, "re-centered then pushed: controller mode again");
    dm_android_pad_motion(XBOX, DM_AVENDOR_MICROSOFT, 0x02ea, DM_ASRC_JOYSTICK, 0, 0, 0, 0, 0, 0, 0, 0);
    dm_android_pad_touched(); dm_android_pad_key(XBOX, DM_AVENDOR_MICROSOFT, 0x02ea, DM_ASRC_GAMEPAD, DM_AKEY_BUTTON_B, 1); check(dm_android_pad_mode() == 1, "a button always takes over");
    dm_android_pad_key(XBOX, DM_AVENDOR_MICROSOFT, 0x02ea, DM_ASRC_GAMEPAD, DM_AKEY_BUTTON_B, 0);
    // 任天堂手柄：只换 A/B；两手柄合并；断开 | Nintendo: only A/B swap; two controllers merged; removal
    dm_android_pad_key(NIN, DM_AVENDOR_NINTENDO, 0x2009, DM_ASRC_GAMEPAD, DM_AKEY_BUTTON_A, 1); check(last_b == DM_BTN_A && glyph_req == DM_GLYPH_PAD, "Nintendo buttonA (right, letter A) -> game A, Switch glyphs");
    dm_android_pad_key(NIN, DM_AVENDOR_NINTENDO, 0x2009, DM_ASRC_GAMEPAD, DM_AKEY_BUTTON_A, 0);
    dm_android_pad_key(NIN, DM_AVENDOR_NINTENDO, 0x2009, DM_ASRC_GAMEPAD, DM_AKEY_BUTTON_X, 1); check(last_b == DM_BTN_X, "Nintendo X unchanged");
    dm_android_pad_key(XBOX, DM_AVENDOR_MICROSOFT, 0x02ea, DM_ASRC_GAMEPAD, DM_AKEY_BUTTON_A, 1); check(last_b == (DM_BTN_X | DM_BTN_A), "two controllers OR their buttons");
    dm_android_pad_drop(NIN); check(last_b == DM_BTN_A && dm_android_pad_mode() == 1, "dropping one keeps the other's buttons and the mode");
    dm_android_pad_drop(XBOX); check(last_b == 0 && dm_android_pad_mode() == 0, "dropping the last returns to touch mode");
    // 失活清零 | reset on resign
    dm_android_pad_key(XBOX, DM_AVENDOR_MICROSOFT, 0x02ea, DM_ASRC_GAMEPAD, DM_AKEY_BUTTON_A, 1); dm_android_pad_motion(XBOX, DM_AVENDOR_MICROSOFT, 0x02ea, DM_ASRC_JOYSTICK, 1, 0, 0, 0, 0, 0, 0, 0);
    dm_android_pad_reset(); check(last_b == 0 && last_ax[0] == 0, "reset clears buttons and sticks");
    // ZR 空格回城 | empty-ZR town portal
    nav = 0; zr_free = 1; dm_android_pad_key(XBOX, DM_AVENDOR_MICROSOFT, 0x02ea, DM_ASRC_GAMEPAD, DM_AKEY_BUTTON_R2, 1); check(tp == 1 && !(last_b & DM_BTN_ZR), "empty ZR in game state -> town portal, ZR withheld");
    dm_android_pad_key(XBOX, DM_AVENDOR_MICROSOFT, 0x02ea, DM_ASRC_GAMEPAD, DM_AKEY_BUTTON_R2, 0);
    zr_free = 0; dm_android_pad_key(XBOX, DM_AVENDOR_MICROSOFT, 0x02ea, DM_ASRC_GAMEPAD, DM_AKEY_BUTTON_R2, 1); check(tp == 1 && (last_b & DM_BTN_ZR), "bound ZR goes to the game");
    // 触控层在手柄模式下只画文字（经验行 + 帧数、生命 / 法力数字），触屏模式全画 | the overlay draws only the texts in controller mode (experience line + fps, life / mana), everything in touch mode
    check(dm_android_pad_hud_mask(1) == DM_PAD_HUD_TEXT, "controller mode: texts only (no touch controls, no touch bar)");
    check(dm_android_pad_hud_mask(0) == (DM_PAD_HUD_CONTROLS | DM_PAD_HUD_BAR | DM_PAD_HUD_TEXT), "touch mode: everything");
    printf("PASS pad_android_test: %d checks\n", checks); return 0;
}
