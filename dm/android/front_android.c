// 安卓前端（front.h 的安卓实现，对应 Mac 的 front_mac.m、iOS 的 front_ios.m）。
// 两种形态：
//   命令行（adb shell 直接跑 dmhost，开发验证用）：没有窗口，front_run 在当前线程直接跑客体，front_metal_layer 为 NULL（图形层走无窗口路径）。
//   App（Java 外壳经 JNI 交来 ANativeWindow，见 jni_android.c）：客体在独立线程跑，front_metal_layer 返回 ANativeWindow*（图形层出图的目标面）。
// Android front end (the Android implementation of front.h, counterpart of Mac's front_mac.m and iOS's front_ios.m).
// Two forms:
//   Command line (dmhost run directly from adb shell, for development checks): no window, front_run runs the guest directly on the current thread, front_metal_layer is NULL (the graphics layer takes the no-window path).
//   App (the Java shell hands over an ANativeWindow through JNI, see jni_android.c): the guest runs on its own thread, front_metal_layer returns the ANativeWindow* (the surface the graphics layer presents to).
#include "../host/dm.h"
#include "../host/front.h"
#include "../host/dm_input.h"
#include "touch_android.h"
#include "../ios/Sources/dm_touch_game_state.h"
#include "../host/dm_res_cap.h"
#include <math.h>
#include <stdatomic.h>

static void *_Atomic g_window;      // ANativeWindow*；命令行形态为 NULL | ANativeWindow*; NULL in the command-line form
static _Atomic int g_screen_w, g_screen_h;
void front_android_set_window(void *win, int w, int h) { atomic_store(&g_window, win); atomic_store(&g_screen_w, w); atomic_store(&g_screen_h, h); }

// UI 线程从当前 Display 读实际刷新率；0 表示未知，不冒充实测 60 Hz。
// The UI thread reads the current Display refresh rate; 0 means unknown, not a measured 60 Hz.
static _Atomic int g_refresh_millihz, g_refresh_max_millihz;
static int millihz(float hz) { return isfinite(hz) && hz > 0 && hz < 1000 ? (int)lroundf(hz * 1000) : 0; }
void front_android_set_refresh(float hz, float max_hz) {
    atomic_store(&g_refresh_millihz, millihz(hz)); atomic_store(&g_refresh_max_millihz, millihz(max_hz));
}
// 屏幕最高刷新率（整数 Hz；给宿主 dm_custom.c：设置页要不要给 120 那一格、120 档生不生效），对应 iOS nvn_mtl.m 的强定义。Java 侧按 Display.getSupportedModes 里与当前模式同尺寸的最高刷新率报
// Maximum screen refresh rate (integer Hz; for the host's dm_custom.c: whether the settings page offers the 120 stop and whether it takes effect), the counterpart of the strong definition in iOS nvn_mtl.m.
// The Java side reports the highest refresh rate among Display.getSupportedModes with the current mode's size
int dm_display_max_hz(void) { int m = atomic_load(&g_refresh_max_millihz), c = atomic_load(&g_refresh_millihz); return ((m > c ? m : c) + 500) / 1000; }
double front_display_refresh(char *name, size_t n) {
    if (name && n) *name = 0;
    return atomic_load(&g_refresh_millihz) / 1000.0;
}

void *front_metal_layer(void) { return atomic_load(&g_window); }

void front_run(void (*guest)(void)) {
    // 观测门在装载后才可订阅；这里沿用 iOS 在 front_run 接入的时机。
    // Observation gates exist only after loading; subscribe at front_run as iOS does.
    if (front_metal_layer()) {
        void uimsg_subscribe(void (*)(const char *, const char *, const char *));
        uimsg_subscribe(dm_android_touch_uimsg);
        dm_pad_hook = dm_android_touch_step;
        dm_touch_game_attach();
        dm_touch_game_live_check(1);
    }
    guest();   // 两种形态都在调用线程上跑：App 形态由 JNI 侧先起好客体线程再进 dm_main | Both forms run on the calling thread: in the App form the JNI side starts the guest thread first and then enters dm_main
}

// 游戏输出分辨率（与 iOS front_ios.m 同一规则，渲染分辨率开关沿用 iPhone 的那一行）：
//   主机档 = 屏幕原生像素（界面文字原生；3D 由游戏自带的渲染比例另缩，见 hle_oe.c scale3d_tick）；比 16:9 窄的屏按屏宽取 16:9（上下留边）；
//   启动预留 = 这个原生尺寸（游戏只接受不超过启动尺寸的新值）；窗口建好之后，实际出图尺寸按「渲染分辨率」档（dm_custom.c render_res：0 自动 / 1 省电 207 万 /
//   2 均衡 280 万 / 3 原生）的像素预算等比缩小（dm_res_cap.h；安卓没有 Metal 代际表，自动档按通用 400 万预算，超大屏取均衡），图形层出图时整屏放大铺回窗口；
//   自动档闭环（hle_oe.c 按实际掉帧再降档）同 iOS。档位改了由 dm_custom.c 推 31 让游戏重读。掌机档仍按原版高度 720 × 屏幕比例。DM_GAME_RES=宽x高 钉住（负对照 1600x900 = 原版主机档）。
//   奥丁 Thor 1920x1080（207 万）：四档都 = 原生；之前固定 1600x900 再由系统合成器拉伸到 1080p，画面糊。
// Game output resolution (the same rule as iOS front_ios.m; the render-resolution switch is the iPhone row):
//   docked = the screen's native pixels (UI text native; 3D is scaled separately by the game's own render scale, see hle_oe.c scale3d_tick); screens narrower than 16:9 take 16:9 by width (bars top and bottom);
//   the startup reserve is that native size (the game only accepts new values no larger than the startup size); once the window is live, the actual output size is shrunk proportionally to the pixel budget of the
//   render-resolution level (dm_custom.c render_res: 0 Auto / 1 Eco 2.07 MP / 2 Balanced 2.8 MP / 3 Native) per dm_res_cap.h (Android has no Metal generation table: Auto takes the generic 4.0 MP budget, huge
//   screens Balanced), and the graphics layer's full-screen upscale fills the window again; the Auto closed loop (hle_oe.c, one more step down on measured drops) is the same as on iOS. A level change pushes 31 from
//   dm_custom.c. Handheld stays at the original height 720 × the screen aspect. DM_GAME_RES=WxH pins it (negative control 1600x900 = the original docked size).
//   AYN Thor 1920x1080 (2.07 MP): all four levels = native; before this the output was fixed at 1600x900 and stretched to 1080p by the system compositor, which blurred the picture.
uint64_t front_game_res(int docked) {
    unsigned w, h; const char *e = getenv("DM_GAME_RES");
    if (e && sscanf(e, "%ux%u", &w, &h) == 2) return docked ? (uint64_t)h << 32 | w : (uint64_t)(h * 4 / 5) << 32 | (w * 4 / 5 + 3) / 4 * 4;
    int sw = atomic_load(&g_screen_w), sh = atomic_load(&g_screen_h);
    unsigned fw = sw > sh ? (unsigned)sw : (unsigned)sh, fh = sw > sh ? (unsigned)sh : (unsigned)sw;   // 长边 × 短边 | long side × short side
    if (!docked) {   // 掌机档：高 720、宽按屏幕比例（≥16:9），8 对齐 | handheld: height 720, width by the screen aspect (≥ 16:9), aligned to 8
        unsigned bw = 1280; if (fw && fh && (uint64_t)fw * 9 > (uint64_t)fh * 16) { bw = (unsigned)(llround(720.0 * fw / fh / 8) * 8); if (bw > 4096) bw = 4096; }
        return 720ull << 32 | bw;
    }
    if (!fw || !fh) return 900ull << 32 | 1600;   // 还没有窗口（命令行形态）：原版 16:9 | no window yet (command-line form): the original 16:9
    if ((uint64_t)fw * 9 < (uint64_t)fh * 16) fh = (unsigned)(((uint64_t)fw * 9 / 16) & ~1ull);   // 比 16:9 窄：按屏宽取 16:9 | narrower than 16:9: 16:9 by width
    fw &= ~1u; fh &= ~1u; if (fw > 4096) { fh = (unsigned)(((uint64_t)fh * 4096 / fw) & ~1ull); fw = 4096; }
    static _Atomic int logged_native; if (!atomic_exchange(&logged_native, 1)) LOG("画面：屏幕 %dx%d → 游戏窗口按 %ux%u 预留（主机档 = 原生；出图尺寸按渲染分辨率档的像素预算缩）", sw, sh, fw, fh);
    int dm_uiscreen_res_live(void); if (!dm_uiscreen_res_live()) return (uint64_t)fh << 32 | fw;   // 启动预留不缩 | the startup reserve is never shrunk
    int dm_render_res_level(void), dm_res_auto_step_get(void), dm_3d_quality_mode_get(void); void dm_res_auto_base_set(uint32_t);
    int lv = dm_render_res_level();
    uint32_t budget = dm_res_cap_budget_screen(lv, 0, 0, 0, fw, fh);   // 安卓无代际表：自动档 = 通用 400 万，超大屏 280 万 | no generation table on Android: Auto = generic 4.0 MP, huge screens 2.8 MP
    uint64_t win_px = (uint64_t)fw * fh; uint32_t base = budget && budget < win_px ? budget : (uint32_t)(win_px > 0xffffffffu ? 0xffffffffu : win_px);
    if (lv == 0) dm_res_auto_base_set(base);
    int st = dm_res_auto_step_effective(dm_res_auto_step_get(), lv, dm_3d_quality_mode_get() == 1); budget = st ? dm_res_auto_budget(base, st) : budget;
    uint64_t r = dm_res_cap_apply(fw, fh, budget);
    static _Atomic uint64_t logged_res; if (atomic_exchange(&logged_res, r | (uint64_t)lv << 62) != (r | (uint64_t)lv << 62)) LOG("画面：渲染分辨率档 %d（像素预算 %u，自动档闭环第 %d 步）→ 出图 %ux%u", lv, budget, st, (unsigned)r, (unsigned)(r >> 32));
    return r;
}

// 文本输入框：App 用 text_input_jni.c 的强实现弹系统输入框；命令行无窗口，返回 -1。
// Text input: the App overrides this with text_input_jni.c; windowless command-line runs return -1.
__attribute__((weak)) int front_android_text_input(const char *title, const char *sub, const char *guide, const char *initial, int max_chars, char *out, int out_cap) {
    (void)title; (void)sub; (void)guide; (void)initial; (void)max_chars; (void)out; (void)out_cap; return -1;
}
int front_text_input(const char *title, const char *sub, const char *guide, const char *initial, int max_chars, char *out, int out_cap) {
    if (!front_metal_layer()) return -1;
    return front_android_text_input(title, sub, guide, initial, max_chars, out, out_cap);
}
