// NPC 对话菜单的第一帧（台账 B010）：Duo 内屏真全屏下菜单先在原版位置画一帧、下一帧才跳到 NPC 头顶，看着是对话框闪一下。
// 照原版一帧的顺序回放：取手柄处（dm_ui_mouse_pump 那一路，now = 0）→ 原版装内容写经典坐标并当场按它摆位 → 渲染入口（观测门 13，now = 1）→ 画。
// 断言第一次画出来的就是换算后的位置、之后不重复换算；16:9（界面矩形 = 画面）与「给的已是屏幕坐标」的请求不动。
// The NPC dialog menu's first frame (ledger B010): on the Duo inner screen in true fullscreen the menu was drawn for one frame at the game's position and jumped above the NPC on
// the next, which looked like the dialog box flashing. Replay the game's frame order: controller-fetch site (the dm_ui_mouse_pump path, now = 0) → the game fills the panel,
// writes the classic coordinates and places it from them right away → render entry (gate 13, now = 1) → draw. Assert the first drawn position is the converted one and
// nothing is converted twice; at 16:9 (UI rect = screen) and for requests already in screen coordinates nothing changes.
#include "../dm_cursor.c"
module_t g_mod[MAX_MOD]; int g_nmod;
static int busy, ingame = 1, uy = 402, uh = 1604, real_h = 2006; static uint64_t frame = 100;
uint64_t dm_guest_frame(void) { return frame; }
uint64_t now_ns(void) { return frame * 16666667; }
int dm_padmode_busy(void) { return busy; }
int dm_uiscreen_ingame(void) { return ingame; }
int dm_ui_native_frontend(void) { return 0; }
int dm_ui_bottom_offset(int margin) { (void)margin; return 0; }
int dm_screen_real(int *w, int *h) { *w = 2852; *h = real_h; return 1; }
void dm_ui_rect(int *x, int *y, int *w, int *h) { *x = 0; *y = uy; *w = 2852; *h = uh; }
void dm_uiscreen_set_ingame(int on) { ingame = on; }
void dm_custom_on_uimsg(const char *cat, const char *act, const char *arg) { (void)cat; (void)act; (void)arg; }
void dm_elysium_arm(int on) { (void)on; }
static int checks, fails;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)
static _Alignas(8) unsigned char root[160], panel[0x300];
static uint64_t children[1];
static uint64_t ptr(void *p) { return (uint64_t)(uintptr_t)p; }
static int32_t *cls(void) { return (int32_t *)(panel + 0x220); }
static int drawn_x, drawn_y, places;
// 原版 0x3dab64 的替身：按经典坐标摆位（只取纵向：经典 y − 偏移 → 界面像素，同原版用界面高换算），写进面板矩形 +0x70 / +0x74
// stand-in for the game's 0x3dab64: place from the classic coordinates (界面 pixels from the UI height, as the game does) into the panel rect +0x70 / +0x74
static void place(uint64_t p) { int32_t *f = (int32_t *)(p + 0x220), off = *(int32_t *)(p + 0x228); places++;
    *(int32_t *)(p + 0x70) = f[0] * uh / 600; *(int32_t *)(p + 0x74) = (f[1] - off) * uh / 600; }
static void draw(void) { drawn_x = *(int32_t *)(panel + 0x70); drawn_y = *(int32_t *)(panel + 0x74); }
// 原版装内容（0x3d9d68）：置跟随位、写屏幕坐标位与经典坐标，当场摆位 | the game's fill (0x3d9d68): set the follow flag, the screen-coordinate flag and the classic coordinates, place right away
static void game_fill(int x, int y, int screen) { panel[0x239] = 1; panel[0x238] = (unsigned char)screen; cls()[0] = x; cls()[1] = y; place(ptr(panel)); }
static void game_frame(void) { if (panel[0x239]) place(ptr(panel)); }   // 每帧更新：跟随位在就按坐标摆位（0x3da8f0） | per-frame update: place from the coordinates while following (0x3da8f0)
// 一帧：取手柄处 → 原版更新（含装内容）→ 渲染入口 → 画；with_gate = 0 是没有门 13 的旧顺序 | one frame: controller fetch → game update (incl. fill) → render entry → draw
static void one_frame(int fill_x, int fill_y, int screen, int with_gate) { frame++;
    ui_npcdlg_pump(ptr(root), 0);
    if (fill_x || fill_y) game_fill(fill_x, fill_y, screen); else game_frame();
    if (with_gate) dm_ui_render_entry();
    draw(); }
static void reset(void) { memset(panel, 0, sizeof panel); *(const char **)(panel + 8) = "NpcDialogPanel"; *(int32_t *)(panel + 0x228) = 20; panel[0x50] = 1; places = 0; }
int main(void) {
    unsigned char *base = calloc(1, 0x2f05460); if (!base) return 2;
    g_nmod = 1; strcpy(g_mod[0].name, "main"); g_mod[0].base = base; g_npcdlg_place = place;
    *(uint64_t *)(base + 0x2f05458) = ptr(root); *(uint64_t *)(root + 0x58) = ptr(children); *(uint64_t *)(root + 0x60) = 1; children[0] = ptr(panel);
    // 换算值（实测 Duo 内屏贴底：经典 (418,151) → (423,33)，宿主日志 akara_duo_touch） | converted values (measured on the Duo inner screen, bottom-aligned)
    const int want_x = 423 * uh / 600, want_y = (33 - 20) * uh / 600, orig_y = (151 - 20) * uh / 600;
    // 旧顺序（没有渲染入口）：第一帧画在原版位置，下一帧才到位——这就是闪的那一下 | old order (no render entry): the first frame is at the game's position, the next one corrects it
    reset(); one_frame(0, 0, 0, 0); one_frame(418, 151, 0, 0); CHECK(drawn_y == orig_y && drawn_y != want_y);
    one_frame(0, 0, 0, 0); CHECK(drawn_x == want_x && drawn_y == want_y);
    // 新顺序：菜单第一次画出来就在头顶 | new order: the menu's first draw is already above the head
    reset(); one_frame(0, 0, 0, 1); CHECK(places == 0);
    one_frame(418, 151, 0, 1); CHECK(drawn_x == want_x && drawn_y == want_y && cls()[0] == 423 && cls()[1] == 33);
    for (int k = 0; k < 30; k++) { one_frame(0, 0, 0, 1); CHECK(drawn_x == want_x && drawn_y == want_y && cls()[1] == 33); }   // 不重复换算 | no double conversion
    // 换一个 NPC（同一个面板对象，装新坐标）：新坐标当帧换算 | another NPC (same panel object, new coordinates): converted in the same frame
    one_frame(500, 200, 0, 1); { int32_t nx = cls()[0], ny = cls()[1]; CHECK(nx == (int)lround(400 + 100.0 * real_h / uh) && ny == (int)lround((180 * real_h / 600.0 - uy) * 600.0 / uh) + 20);
        CHECK(drawn_x == nx * uh / 600 && drawn_y == (ny - 20) * uh / 600); }
    // 给的已是屏幕坐标（+0x238）：不换算、不额外摆位 | already screen coordinates (+0x238): no conversion, no extra placement
    reset(); one_frame(300, 120, 1, 1); CHECK(cls()[0] == 300 && cls()[1] == 120 && places == 1);
    // 讲话（还没给坐标）：不动 | speech with no coordinates yet: untouched
    reset(); panel[0x239] = 1; one_frame(0, 0, 0, 1); CHECK(places == 1 && cls()[0] == 0 && cls()[1] == 0);
    // 16:9（界面矩形 = 画面）：不换算 | 16:9 (UI rect = screen): no conversion
    reset(); uy = 0; uh = real_h = 1080; one_frame(418, 151, 0, 1); CHECK(cls()[0] == 418 && cls()[1] == 151 && places == 1);
    uy = 402; uh = 1604; real_h = 2006;
    // 局外不动 | not in a run: untouched
    reset(); ingame = 0; one_frame(418, 151, 0, 1); CHECK(cls()[0] == 418 && cls()[1] == 151); ingame = 1;
    free(base); printf("t_npcdlg_first_frame: %d checks, %d failures\n", checks, fails); return fails != 0;
}
