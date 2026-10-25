// 游戏有一对「屏幕宽高」全局量 [main+0x211da38]（两个 32 位）：界面按其中的「高」定缩放、按宽高定摆位矩形；3D 出图与命中框不看它（实验，
// 实验得出）。画面比 16:9 窄时把「高」写成 宽÷16×9，界面就完整落在 16:9 里、3D 仍铺满；界面是贴着画面顶边画的，
// 要居中 / 贴底由图形层把界面那一趟绘制整体下移 dm_ui_rect 给的 y（界面独占一趟、帧末合成，量过）。
// 写这对量的只有游戏的窗口初始化（main+0xe10e8c、0xe12ecc，切主机 / 掌机档会再走）。这里不逐个找时机，每帧比一次：
// 全局量不等于我们上次写的值 = 游戏刚重写过 → 记下新的真实值、再写一次。
// 配套：点世界按真实画面算（dm_world.c 用 dm_screen_real）；dm_ui_* 的入口吃的是「界面矩形里的归一化坐标」，由调用方（触摸前端 / 测试点按通路）换算。
// 缺省关。打开：前端调 dm_uiscreen_enable；Mac 宿主测试用环境变量 DM_UI_169=1（等于贴顶）与 DM_UI_YPOS=top | center | bottom。
// The game has a pair of "screen width / height" globals [main+0x211da38] (two 32-bit values): the UI derives its scale from the "height" and its placement rectangle from width and height; 3D rendering and hit boxes do not look at it (experiment,
// obtained by experiment). When the frame is narrower than 16:9, write "height" as width÷16×9, and the UI falls entirely within 16:9 while 3D still fills the screen; the UI is drawn flush with the top edge of the frame,
// so centering / bottom-aligning is done by the graphics layer shifting the whole UI draw pass down by the y given by dm_ui_rect (the UI has a pass of its own, composited at end of frame; measured).
// The only writer of this pair is the game's window initialization (main+0xe10e8c, 0xe12ecc; switching docked / handheld mode goes through it again). Rather than hunting for each occasion, compare once per frame here:
// globals not equal to the value we last wrote = the game has just rewritten them → record the new real values and write once more.
// Companion pieces: world taps are computed on the real frame (dm_world.c uses dm_screen_real); the dm_ui_* entry points take "normalized coordinates within the UI rectangle", converted by the caller (touch front end / test tap path).
// Off by default. To turn on: the front end calls dm_uiscreen_enable; for Mac host testing use the environment variables DM_UI_169=1 (equivalent to top-aligned) and DM_UI_YPOS=top | center | bottom.
#include "dm_uiscreen.h"
#include "hle.h"
#include "dm.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

static _Atomic int g_mode = -1;              // -1 = 还没定（第一次用时看环境变量） | -1 = not decided yet (checks the environment variable on first use)
static _Atomic int g_rw, g_rh, g_uh;         // 真实宽高、写给游戏的界面高；g_rw == 0 = 没在改写 | Real width and height, and the UI height written to the game; g_rw == 0 = not rewriting
static uint8_t *main_base(void) { static uint8_t *b; if (!b) for (int i = 0; i < g_nmod; i++) if (!strcmp(g_mod[i].name, "main")) b = (uint8_t *)g_mod[i].base; return b; }
static int mode(void) { int m = atomic_load(&g_mode); if (m >= 0) return m;
    const char *e = getenv("DM_UI_169"), *p = getenv("DM_UI_YPOS"); m = !(e && strcmp(e, "0")) ? DM_UISCREEN_OFF : p && !strcmp(p, "center") ? DM_UISCREEN_CENTER : p && !strcmp(p, "bottom") ? DM_UISCREEN_BOTTOM : DM_UISCREEN_TOP;
    int exp = -1; atomic_compare_exchange_strong(&g_mode, &exp, m); return atomic_load(&g_mode); }
void dm_uiscreen_enable(int m) { atomic_store(&g_mode, m < DM_UISCREEN_OFF || m > DM_UISCREEN_BOTTOM ? DM_UISCREEN_OFF : m); }
// 局内 / 局外：局外 UI 居中，局内 UI 贴底；3D 默认两者都铺满。DM_FRONTEND_FULL=0 恢复局外整页 16:9 留边。
// 唯一信号：dm_uiscreen_ingame()。定义 = HUDPanel 在且读取画面（LoadScreenPanel）不在，即读取画面两头都归局外：进局时读取画面撤掉后的第一帧起才算局内，
// 出局时读取画面一盖上来就算局外。由界面每帧那一处（dm_cursor.c，游戏主线程）锁存，宿主各处与图形层都只读这一个值。
// 界面矩形：局外居中，局内按前端选的摆法（贴底）。局外原生比例出图时只把 UI 挪到正中，不缩放 3D。
// In a run / out of a run: frontend UI is centered, in-game UI is bottom-aligned; both scenes fill the window. DM_FRONTEND_FULL=0 restores the letterboxed frontend.
// The single signal: dm_uiscreen_ingame(). Definition = HUDPanel present and the loading screen (LoadScreenPanel) absent, i.e. both ends of the loading screen count as out of a run: on entering, in-run starts from the first frame after the loading screen is removed,
// and on leaving, out-of-run starts as soon as the loading screen covers the view. It is latched at the per-frame UI site (dm_cursor.c, game main thread), and everywhere in the host and the graphics layer reads only this one value.
// UI rectangle: centered out of a run, placed as the front end chooses in a run (bottom-aligned). Center only the UI on native-aspect frontend scenes; do not scale the 3D scene.
static _Atomic int g_ingame;
void (*g_dm_charsel_hook)(void);   // 退出一局回到前端那一刻（diag.c 界面消息 FrontEndNavigation:ToFrontEnd 里调） | The moment of quitting a run back to the front end (called in diag.c's UI message FrontEndNavigation:ToFrontEnd)
void (*g_dm_ingame_hook)(int on);   // 进局 / 出局那一刻（读取画面也算一次出局）；iOS：cloud_ios.m 拿「进局」记一局开始 | The moment of entering / leaving a run (the loading screen also counts as leaving once); iOS: cloud_ios.m uses "entering a run" to mark the start of a run
void dm_uiscreen_set_ingame(int on) { if (atomic_exchange(&g_ingame, on != 0) != (on != 0)) { LOG("界面画面：%s", on ? "进局（界面贴底）" : "出局（界面居中）"); if (g_dm_ingame_hook) g_dm_ingame_hook(on != 0); } }
int dm_uiscreen_ingame(void) { return atomic_load(&g_ingame); }
// 局内开着大面板：整块界面移到正中、上下留同样宽的边，关掉回落贴底；不做过渡（直接跳）。
// 判据就是触摸前端的「面板模式」（摇杆收起、单指当光标的那个状态，front_ios.m 在界面消息回调里同步算出），只开着 NPC 对话菜单不算（菜单跟着 NPC 摆，留在贴底）。
// 消息回调与游戏排界面在同一线程、先后同一帧，所以开 / 关那一帧界面就在新位置；图形层下移、前端游戏视图、世界字投影、点世界换算都每帧现读 yoff，自然跟着走
// A large panel open in a run: the whole UI moves to the center with equal margins above and below, and drops back to bottom-aligned when closed; no transition (a direct jump).
// The criterion is the touch front end's "panel mode" (the state where the stick is hidden and a single finger acts as the cursor, computed synchronously by front_ios.m in the UI message callback); having only the NPC dialog menu open does not count (the menu is placed following the NPC and stays bottom-aligned).
// The message callback and the game's UI layout run on the same thread, one after the other in the same frame, so on the open / close frame the UI is already at the new position; the graphics layer shift, the front end game view, the world text projection and the world tap conversion all read yoff fresh every frame and follow along naturally
static _Atomic int g_panel;
int dm_uiscreen_panel(void) { return atomic_load(&g_panel); }
void dm_uiscreen_set_panel(int on) { if (atomic_exchange(&g_panel, on != 0) != (on != 0) && atomic_load(&g_ingame) && atomic_load(&g_rw)) LOG("界面画面：%s", on ? "开大面板 → 界面移到正中" : "大面板关了 → 界面回落"); }
static int yoff(void) { int rh = atomic_load(&g_rh), uh = atomic_load(&g_uh), m = mode(); if (!atomic_load(&g_rw) || rh <= uh || m == DM_UISCREEN_OFF) return 0;
    if (!atomic_load(&g_ingame) || atomic_load(&g_panel)) m = DM_UISCREEN_CENTER; return m == DM_UISCREEN_CENTER ? (rh - uh) / 2 : m == DM_UISCREEN_BOTTOM ? rh - uh : 0; }

// 跟着世界走的界面字（地上物品名、NPC 名字牌、血条、飘字）：游戏用镜头的「世界点 → 屏幕」main+0x1237c60(镜头, 点, out) 算出真实画面像素（y = [镜头+0x144] × (0.5 − 0.5·ndc_y)），
// 直接当界面坐标用（调用方只有 0x90d110 四处与 0xa71bb0）。界面整趟下移 y0 之后这些字就低了 y0（名字牌落到脚下）。
// 镜头对象同时是出图用的，投影矩阵（+0x80 起 4 列）每帧由 0x162f9ac 无条件重算，往对象里写值留不住也不该写。所以在函数入口（观测门 OBS_UI_PROJECT，只在 touch 规则域）
// 把 x0 换成镜头的一份副本，副本的投影每列 y += (2·y0 / 高) × 该列 w ⇒ 出来的 y 正好少 y0；游戏的镜头一个字节不动。y0 = 0（没开 / 贴顶 / 16:9 屏）时不换。
// UI text that follows the world (ground item names, NPC nameplates, health bars, floating text): the game uses the camera's "world point → screen" main+0x1237c60(camera, point, out) to compute real frame pixels (y = [camera+0x144] × (0.5 − 0.5·ndc_y)),
// and uses them directly as UI coordinates (the only callers are four sites in 0x90d110 and 0xa71bb0). Once the whole UI pass is shifted down by y0, this text ends up y0 too low (nameplates land at the feet).
// The camera object is also used for rendering, and the projection matrix (4 columns from +0x80) is unconditionally recomputed every frame by 0x162f9ac, so values written into the object would not stick and should not be written anyway. So at the function entry (observation gate OBS_UI_PROJECT, only in the touch rule domain)
// swap x0 for a copy of the camera, with each projection column of the copy getting y += (2·y0 / height) × that column's w ⇒ the resulting y is smaller by exactly y0; not one byte of the game's camera is changed. When y0 = 0 (off / top-aligned / 16:9 screen), no swap.
static _Atomic int g_proj_on;
static void ui_project_obs(const uint64_t *regs) {
    int dy = atomic_load(&g_ingame) ? yoff() : 0; const uint8_t *c = (const uint8_t *)(uintptr_t)regs[0]; if (!dy || !c || c[0x160]) return;   // +0x160 = 投影待重算：函数自己会重算并盖掉副本里的偏移，这一次不换（只在改分辨率后的第一次调用） | +0x160 = projection pending recompute: the function will recompute it itself and overwrite the offset in the copy, so do not swap this time (only on the first call after a resolution change)
    float H = *(const float *)(c + 0x144); if (!(H > 0)) return;
    static _Thread_local _Alignas(16) uint8_t cam[0x170]; memcpy(cam, c, sizeof cam); float *m = (float *)(cam + 0x80), k = 2.0f * dy / H;
    for (int j = 0; j < 4; j++) m[j * 4 + 1] += k * m[j * 4 + 3];
    ((uint64_t *)(uintptr_t)regs)[0] = (uint64_t)(uintptr_t)cam;
}
int dm_ui_proj_dy(void) { return atomic_load(&g_proj_on) && atomic_load(&g_ingame) ? yoff() : 0; }

// 游戏的出图分辨率（主机 / 掌机档各一个参数位，见 thread.c 与 hle_oe.c）：默认始终用原生比例。
// 只有 DM_FRONTEND_FULL=0 时按玩家单位决定：不在 = 16:9，在 = 原生比例。状态变化通知游戏重读参数位。
// 玩家单位的建立 / 销毁都落在读取画面期间；读取画面期间界面矩形是居中的 16:9（dm_uiscreen_ingame 为 0），所以读取画面肉眼位置不变。
// Render resolution (docked / handheld slots, thread.c and hle_oe.c): native aspect by default.
// Only DM_FRONTEND_FULL=0 chooses 16:9 while no player unit exists. State changes notify the game to reread its resolution slots.
// The player unit's creation / destruction both fall within the loading screen; during the loading screen the UI rectangle is a centered 16:9 (dm_uiscreen_ingame is 0), so the loading screen does not visibly move.
uint64_t front_game_res(int docked); void dm_oe_res_changed(void);
// 游戏处理 31 时拿「屏幕宽高」全局量和新分辨率比（main+0xe10e10），相同就不切。出局时这对量里还是我们写的 16:9 高，正好等于要切去的值，游戏会以为已经切好了。
// 所以在游戏取走 31 的那一刻（hle_oe.c，同一个线程、紧挨着它的比较之前）把真实高写回：同一帧里写回、比较、切换、游戏自己写成新值，中间没有一帧按 4:3 排界面。
// When handling 31 the game compares the "screen width / height" globals with the new resolution (main+0xe10e10) and does not switch if they are equal. On leaving a run this pair still holds the 16:9 height we wrote, which happens to equal the value to switch to, so the game would think the switch is already done.
// So at the moment the game takes 31 (hle_oe.c, same thread, right before its comparison) write the real height back: within the same frame come the write-back, comparison, switch, and the game writing the new value itself, with not a single frame laying out the UI at 4:3 in between.
void dm_uiscreen_res_reread(void) { uint8_t *b = main_base(); int rw = atomic_load(&g_rw); if (!b || !rw) return; int32_t *g = (int32_t *)(b + 0x211da38);
    if (g[0] == rw && g[1] == atomic_load(&g_uh)) g[1] = atomic_load(&g_rh); atomic_store(&g_rw, 0); }
// 游戏处理 31 时（main+0xe10df0–0xe10e0c）只接受「宽高都不超过窗口初始化时的尺寸 [main+0x211da30]」的新值：窗口贴图按启动时的参数位一次建好，之后只在里面改出图区域。
// 所以启动时给屏幕原生值（最大的那个），仅旧留边模式在窗口初始化完后切到 16:9。
// When handling 31 (main+0xe10df0–0xe10e0c) the game only accepts new values where "both width and height do not exceed the size at window initialization [main+0x211da30]": the window texture is created once from the startup parameter slot, and afterwards only the render region inside it changes.
// Give native dimensions at startup; only legacy letterbox mode switches to 16:9 after window initialization.
static _Atomic int g_world, g_res_live;
// 原生比例前端：UI 保持居中 16:9 安全矩形，3D 场景铺满；合成与触控共用矩形。
// Native-aspect frontend: keep its UI in a centered 16:9 safe
// rectangle, but let the 3D menu/character scene fill the window. The existing
// UI rect is shared by compositing and touch conversion, so neither is stretched.
// DM_FRONTEND_FULL=0 retains the upstream letterboxed frontend.
static int frontend_full(void) {
    const char *e = getenv("DM_FRONTEND_FULL");
    return !e || atoi(e) != 0;
}
int dm_ui_native_frontend(void) { return frontend_full() && mode() != DM_UISCREEN_OFF; }
// 前端相机保持纵向视野；较高屏幕的职业场景横向展开，仅场景命中需逆变换。
// The frontend camera keeps its vertical field of view. On a taller screen its
// lineup spreads horizontally, while the class hit strips stay in 16:9 UI space.
// Undo that horizontal spread only for scene hit tests, never for UI buttons.
int dm_ui_class_x(int x) {
    int rw = atomic_load(&g_rw), rh = atomic_load(&g_rh), uh = atomic_load(&g_uh);
    if (!frontend_full() || mode() == DM_UISCREEN_OFF || rw <= 0 || uh <= 0 || rh <= uh) return x;
    return rw / 2 + (int)(((int64_t)x - rw / 2) * uh / rh);
}
static int32_t point_fixed(float v) {
    if (!(v >= -4 && v <= 4)) v = v < 0 ? -4 : v > 0 ? 4 : 0;
    return (int32_t)(v * 65536.0f);
}
uint64_t dm_ui_point_pack(float x, float y) { return (uint64_t)(uint32_t)point_fixed(x) << 32 | (uint32_t)point_fixed(y); }
float dm_ui_point_x(uint64_t p) { return (int32_t)(p >> 32) / 65536.0f; }
float dm_ui_point_y(uint64_t p) { return (int32_t)p / 65536.0f; }
static _Atomic int g_bottom_inset;
void dm_ui_bottom_inset(int units) { atomic_store(&g_bottom_inset, units < 0 ? 0 : units > 2160 ? 2160 : units); }
int dm_ui_bottom_offset(int margin) {
    int x, y, w, h, rw = 0, rh = 0; dm_ui_rect(&x, &y, &w, &h); dm_screen_real(&rw, &rh);
    if (!frontend_full() || mode() == DM_UISCREEN_OFF || h <= 0) return 0;
    int reserve = atomic_load(&g_bottom_inset) - margin;
    return (int)((int64_t)(rh - y - h) * 2160 / h) - (reserve > 0 ? reserve : 0);
}
int dm_uiscreen_res_live(void) { return atomic_load(&g_res_live); }
uint64_t dm_game_res(int docked) { uint64_t r = front_game_res(docked); uint32_t w = (uint32_t)r, h = (uint32_t)(r >> 32);   // 先问前端：iOS 前端在第一次被问时才决定开不开真全屏 | Ask the front end first: the iOS front end only decides whether to turn on true fullscreen the first time it is asked
    if (atomic_load(&g_res_live) && mode() != DM_UISCREEN_OFF && !atomic_load(&g_world) && !frontend_full() && (uint64_t)w * 9 < (uint64_t)h * 16) h = (uint32_t)((uint64_t)w * 9 / 16) & ~1u;
    return (uint64_t)h << 32 | w; }

void dm_uiscreen_pump(void) {
    uint8_t *b = main_base(); if (!b) return;
    { int world = ((uint64_t (*)(uint32_t))(b + 0x45f9d0))(*(uint32_t *)(b + 0x2d20e4c)) != 0;
      int first = !atomic_exchange(&g_res_live, 1);
      int changed = atomic_exchange(&g_world, world) != world;
      // 原生全屏加载前后分辨率相同；多余通知会临时重建底部锚点，导致标志跳动。
      // Native fullscreen uses the same render size on both sides of a load.
      // A redundant resolution notification briefly restores the real UI height
      // and rebuilds bottom anchors there, making loading/save icons jump.
      if (((changed && !frontend_full()) || (first && !world)) && mode() != DM_UISCREEN_OFF) { LOG("界面画面：%s → 游戏分辨率切到 %u x %u", world ? "玩家单位建立" : first ? "窗口已建好、在前端" : "玩家单位销毁", (unsigned)dm_game_res(1), (unsigned)(dm_game_res(1) >> 32)); dm_oe_res_changed(); } }
    static int reg; if (!reg) { reg = 1; if (gate_obs_present(OBS_UI_PROJECT)) { gate_obs_set(OBS_UI_PROJECT, ui_project_obs); atomic_store(&g_proj_on, 1); } } int32_t *g = (int32_t *)(b + 0x211da38); int m = mode(), rw = atomic_load(&g_rw);
    if (!atomic_load(&g_world) && m != DM_UISCREEN_OFF && !frontend_full()) return;   // 传统前端以 16:9 渲染；原生前端与输入及合成共用居中安全矩形 | Legacy frontend renders at 16:9. Native-aspect frontend shares the centered UI-safe rectangle with the input and composite paths.
    if (m == DM_UISCREEN_OFF) { if (rw) { if (g[0] == rw && g[1] == atomic_load(&g_uh)) g[1] = atomic_load(&g_rh); atomic_store(&g_rw, 0); LOG("界面画面：关掉，屏幕高写回 %d", g[1]); } return; }   // 没打开过就什么都不碰 | If it was never turned on, touch nothing
    int w = g[0], h = g[1]; if (w <= 0 || h <= 0) return;
    if (rw && w == rw && h == atomic_load(&g_uh)) return;   // 还是我们上次写的值 | Still the value we wrote last time
    static int n; int uh = h; if ((long)w * 9 < (long)h * 16) { uh = (int)((long)w * 9 / 16) & ~1; g[1] = uh; }
    atomic_store(&g_rh, h); atomic_store(&g_uh, uh); atomic_store(&g_rw, w);
    LOG("界面画面：游戏写入屏幕宽高 %d x %d（第 %d 次）→ 界面用 %d x %d，纵向位置 %d", w, h, ++n, w, uh, yoff());
}
int dm_screen_real(int *w, int *h) { int rw = atomic_load(&g_rw);
    if (rw) { *w = rw; *h = atomic_load(&g_rh); return 1; }
    uint8_t *b = main_base(); if (!b) return 0; *w = *(int32_t *)(b + 0x211da38); *h = *(int32_t *)(b + 0x211da3c); return *w > 0 && *h > 0; }   // 没在改写：就是游戏的值（与改之前逐字节同） | Not rewriting: it is simply the game's value (byte-for-byte the same as before the change)
void dm_ui_rect(int *x, int *y, int *w, int *h) { int rw = 0, rh = 0; dm_screen_real(&rw, &rh); *x = 0; *y = yoff(); *w = rw; *h = atomic_load(&g_rw) ? atomic_load(&g_uh) : rh; }
float dm_ui_ny(float ny) { int x, y, w, h, rw = 0, rh = 0; dm_ui_rect(&x, &y, &w, &h); if (!dm_screen_real(&rw, &rh) || h <= 0 || h == rh) return ny; return (ny * rh - y) / h; }
float dm_ui_ny_back(float ny) { int x, y, w, h, rw = 0, rh = 0; dm_ui_rect(&x, &y, &w, &h); if (!dm_screen_real(&rw, &rh) || rh <= 0 || h == rh) return ny; return (ny * h + y) / rh; }
