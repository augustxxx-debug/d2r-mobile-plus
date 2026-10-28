// 说明见 pad_android.h。| See pad_android.h.
#include "pad_android.h"
#include "../host/dm_input.h"
#include "../host/dm_padmap.h"
#include <math.h>
#include <stdatomic.h>
#include <string.h>
#ifdef DM_PAD_ANDROID_TEST
#include <stdio.h>
#define LOG(...) do { fprintf(stderr, "[dm] " __VA_ARGS__); fputc('\n', stderr); } while (0)
#else
#include "../host/dm.h"
#endif
void dm_padmode_request(int pad); void dm_tp_request(void); int dm_front_navigation(void), dm_freetp_zr_free(int zl);

enum { NSLOT = 6 };
static struct { int dev, used, vendor, product, nintendo, glyph; int a, b, x, y; uint64_t keys, trig, hat; int32_t ax[4]; } g_s[NSLOT];
static int g_pad_mode, g_need_center, g_glyph = DM_GLYPH_XBOX, g_on[2]; static uint64_t g_prev_b;
static _Atomic int g_overlay_hidden;
int dm_android_pad_mode(void) { return g_pad_mode; }
int dm_android_pad_overlay_hidden(void) { return atomic_load(&g_overlay_hidden); }
#ifndef DM_PAD_ANDROID_TEST
void front_touch_overlay_mode(int pad) { atomic_store(&g_overlay_hidden, pad ? 1 : 0); }   // 宿主 dm_padmode.c 的前端钩子（强定义）| the host's front-end hook in dm_padmode.c (strong definition)
#endif
static void set_mode(int pad) { if (g_pad_mode == pad) return; g_pad_mode = pad; dm_padmode_request(pad); LOG("手柄：%s", pad ? "手柄有输入 → 手柄模式" : "碰了屏幕 / 手柄断开 → 触屏模式"); }
static int glyph_of(int vendor) { return vendor == DM_AVENDOR_NINTENDO ? DM_GLYPH_PAD : vendor == DM_AVENDOR_SONY ? DM_GLYPH_PS : DM_GLYPH_XBOX; }
static int slot_of(int dev, int vendor, int product, int make) {
    int fr = -1; for (int i = 0; i < NSLOT; i++) { if (g_s[i].used && g_s[i].dev == dev) return i; if (fr < 0 && !g_s[i].used) fr = i; }
    if (!make || fr < 0) return -1;
    memset(&g_s[fr], 0, sizeof g_s[fr]); g_s[fr].used = 1; g_s[fr].dev = dev; g_s[fr].vendor = vendor; g_s[fr].product = product; g_s[fr].nintendo = vendor == DM_AVENDOR_NINTENDO; g_s[fr].glyph = glyph_of(vendor);
    LOG("手柄：连上 设备 %d（厂商 %#x 产品 %#x）→ %s", dev, vendor, product, g_s[fr].nintendo ? "任天堂手柄：面键按键帽字母（只换 A/B）、Switch 键帽" : vendor == DM_AVENDOR_SONY ? "PlayStation 手柄：面键按位置（下 A 确认）、PS 键帽" : "Xbox 键位：面键按位置（下 A 右 B 上 X 左 Y）、Xbox 键帽");
    return fr;
}
// 任天堂手柄：系统按键帽字母给，字母直发（右 A 确认，同 Switch 原样）；其余按物理位置 → Xbox 键位（同 iOS dm_gamepad.m publish）
// Nintendo: the system reports by keycap letter, letters go straight through (right A confirms, as on the Switch); everything else by position -> the Xbox layout (as iOS dm_gamepad.m publish)
static uint64_t slot_buttons(int i) { return (g_s[i].nintendo ? dm_padmap_face(g_s[i].a, g_s[i].b, g_s[i].x, g_s[i].y) : dm_padmap_face_xbox(g_s[i].a, g_s[i].b, g_s[i].x, g_s[i].y, 0)) | g_s[i].keys | g_s[i].trig | g_s[i].hat; }
static int32_t axis(float v) { if (v > 1) v = 1; if (v < -1) v = -1; return (int32_t)lroundf(v * DM_STICK_MAX); }
static int stick_on(int was, int32_t x, int32_t y) { double m = hypot(x, y) / DM_STICK_MAX; return was ? m >= 0.15 : m >= 0.25; }
// 合并各手柄、定模式、交给宿主（同 iOS dm_gamepad.m apply）| merge the controllers, decide the mode, hand over to the host (as iOS dm_gamepad.m apply)
static void apply(void) {
    uint64_t b = 0; int32_t ax[4] = {0}; double best[2] = {0};
    for (int i = 0; i < NSLOT; i++) { if (!g_s[i].used) continue; b |= slot_buttons(i);
        for (int k = 0; k < 2; k++) { double m = hypot(g_s[i].ax[2 * k], g_s[i].ax[2 * k + 1]); if (m > best[k]) { best[k] = m; ax[2 * k] = g_s[i].ax[2 * k]; ax[2 * k + 1] = g_s[i].ax[2 * k + 1]; } } }
    g_on[0] = stick_on(g_on[0], ax[0], ax[1]); g_on[1] = stick_on(g_on[1], ax[2], ax[3]);
    if (!g_on[0] && !g_on[1]) g_need_center = 0;
    if ((b & ~g_prev_b) || ((g_on[0] || g_on[1]) && !g_need_center)) { dm_padmode_request_glyph(g_glyph); set_mode(1); }
    g_prev_b = b;
    if (!g_pad_mode || !g_on[0]) ax[0] = ax[1] = 0; if (!g_pad_mode || !g_on[1]) ax[2] = ax[3] = 0;
    { static uint64_t prev; static int mine;   // ZR 空格 = 免费回城（同 iOS）| empty ZR = free town portal (as iOS)
      if ((b & DM_BTN_ZR) && !(prev & DM_BTN_ZR)) { mine = !dm_front_navigation() && dm_freetp_zr_free((b & DM_BTN_ZL) != 0); if (mine) { dm_tp_request(); LOG("手柄：ZR（这一格空着）→ 免费回城"); } }
      prev = b; if (mine) b &= ~(uint64_t)DM_BTN_ZR; if (!(b & DM_BTN_ZR) && !(prev & DM_BTN_ZR)) mine = 0; }
    dm_pad_set(b, ax[0], ax[1], ax[2], ax[3]);
}
static void clear_input(int i) { int32_t z[4] = {0}; g_s[i].a = g_s[i].b = g_s[i].x = g_s[i].y = 0; g_s[i].keys = g_s[i].trig = g_s[i].hat = 0; memcpy(g_s[i].ax, z, sizeof z); }
static int pad_source(int source) { return (source & 0x400) || (source & 0x01000000) || (source & 0x200); }   // GAMEPAD / JOYSTICK / DPAD 类 | the GAMEPAD / JOYSTICK / DPAD classes
int dm_android_pad_key(int device, int vendor, int product, int source, int keycode, int down) {
    int known = slot_of(device, vendor, product, 0) >= 0, button = keycode >= DM_AKEY_BUTTON_A && keycode <= DM_AKEY_BUTTON_MODE;
    int dpad = keycode >= DM_AKEY_DPAD_UP && keycode <= DM_AKEY_DPAD_RIGHT;
    if (!button && !(pad_source(source) && (dpad || keycode == DM_AKEY_BACK)) && !(known && keycode == DM_AKEY_BACK)) return 0;   // 键盘 / 系统导航的 BACK 不归手柄 | BACK from the keyboard / system navigation is not ours
    int i = slot_of(device, vendor, product, 1); if (i < 0) return 1;
    uint64_t bit = 0; int *face = 0;
    switch (keycode) {
    case DM_AKEY_BUTTON_A: face = &g_s[i].a; break; case DM_AKEY_BUTTON_B: case DM_AKEY_BACK: face = &g_s[i].b; break;
    case DM_AKEY_BUTTON_X: face = &g_s[i].x; break; case DM_AKEY_BUTTON_Y: face = &g_s[i].y; break;
    case DM_AKEY_BUTTON_L1: bit = DM_BTN_L; break; case DM_AKEY_BUTTON_R1: bit = DM_BTN_R; break;
    case DM_AKEY_BUTTON_L2: bit = DM_BTN_ZL; break; case DM_AKEY_BUTTON_R2: bit = DM_BTN_ZR; break;
    case DM_AKEY_BUTTON_THUMBL: bit = DM_BTN_LS; break; case DM_AKEY_BUTTON_THUMBR: bit = DM_BTN_RS; break;
    case DM_AKEY_BUTTON_START: bit = DM_BTN_PLUS; break; case DM_AKEY_BUTTON_SELECT: bit = DM_BTN_MINUS; break;
    case DM_AKEY_DPAD_UP: bit = DM_BTN_UP; break; case DM_AKEY_DPAD_DOWN: bit = DM_BTN_DOWN; break; case DM_AKEY_DPAD_LEFT: bit = DM_BTN_LEFT; break; case DM_AKEY_DPAD_RIGHT: bit = DM_BTN_RIGHT; break;
    default: return 1;   // MODE / C / Z 等：吃掉不发 | MODE / C / Z etc.: swallowed
    }
    if (face) *face = down ? 1 : 0; else if (down) g_s[i].keys |= bit; else g_s[i].keys &= ~bit;
    if (down) g_glyph = g_s[i].glyph;
    apply(); return 1;
}
int dm_android_pad_motion(int device, int vendor, int product, int source, float lx, float ly, float rx, float ry, float lt, float rt, float hx, float hy) {
    if (!(source & 0x01000000) && !(source & 0x400)) return 0;
    int i = slot_of(device, vendor, product, 1); if (i < 0) return 1;
    g_s[i].ax[0] = axis(lx); g_s[i].ax[1] = axis(-ly); g_s[i].ax[2] = axis(rx); g_s[i].ax[3] = axis(-ry);   // 安卓 Y 向下为正 → 原版向上为正 | Android Y down-positive -> original up-positive
    g_s[i].trig = (lt > 0.5f ? DM_BTN_ZL : 0) | (rt > 0.5f ? DM_BTN_ZR : 0);
    g_s[i].hat = (hx < -0.5f ? DM_BTN_LEFT : hx > 0.5f ? DM_BTN_RIGHT : 0) | (hy < -0.5f ? DM_BTN_UP : hy > 0.5f ? DM_BTN_DOWN : 0);
    if (hypot(g_s[i].ax[0], g_s[i].ax[1]) >= 0.25 * DM_STICK_MAX || hypot(g_s[i].ax[2], g_s[i].ax[3]) >= 0.25 * DM_STICK_MAX || g_s[i].trig || g_s[i].hat) g_glyph = g_s[i].glyph;
    apply(); return 1;
}
void dm_android_pad_drop(int device) {
    int i = slot_of(device, 0, 0, 0); if (i < 0) return; LOG("手柄：断开 设备 %d", device); memset(&g_s[i], 0, sizeof g_s[i]); apply();
    for (int k = 0; k < NSLOT; k++) if (g_s[k].used) return;
    set_mode(0);
}
void dm_android_pad_touched(void) { if (g_pad_mode) { g_need_center = 1; set_mode(0); apply(); } }
void dm_android_pad_reset(void) {
    int any = 0; for (int i = 0; i < NSLOT; i++) if (g_s[i].used) { any |= slot_buttons(i) != 0 || g_s[i].ax[0] || g_s[i].ax[1] || g_s[i].ax[2] || g_s[i].ax[3]; clear_input(i); }
    if (any) { LOG("手柄：失活，手柄的键和摇杆清零"); apply(); }
}
