// 选角页的显示版本与新版本提示。地址为 3.3.94012 原映像偏移。
// 画面上的东西全是游戏自己的控件：布局覆盖层在主菜单面板（MainMenuPanel，选角页那一层）下加了五个（dm/ios/tools/touch_layout.py 的 _UPDATE）——
//   TouchBuild 显示版本「v1.0.0+28 · b346e40」（左下角常显，外壳给的字串，dm/tools/version.sh）；TouchUpdNote 普通更新的一行短字「新版本 v1.1.0」（不带说明）；
//   TouchUpdBg / TouchUpdTitle / TouchUpdDesc 建议立即更新的底板、红字标题与说明（「进入游戏」上方）。
// 只提示、不提供更新动作（四审 Q4-04）：没有「更新」按钮、手柄键或鼠标点击入口，玩家自己去下载页更新。
// 布局里字是空的、底板在屏幕外；这里在游戏主线程上写字、把要显示的挪回原位。没有这些控件（Mac 宿主跑原版布局）或没人调 dm_update_init = 什么都不做。
// 联网、比新旧都在外壳那边；这里只认 dm_update_offer 给的结果。
// Display version and new version notice on the character select page. Addresses are offsets in the 3.3.94012 original image.
// Everything on screen is the game's own widgets: the layout overlay adds five under the main menu panel (MainMenuPanel, the character select page layer) (_UPDATE in dm/ios/tools/touch_layout.py) -
//   TouchBuild display version "v1.0.0+28 · b346e40" (always shown at the bottom left, a string from the shell, dm/tools/version.sh); TouchUpdNote the short line "New v1.1.0" for an ordinary update (no note);
//   TouchUpdBg / TouchUpdTitle / TouchUpdDesc the backplate, red title and description for a recommended update (above 「进入游戏」(Play)).
// Notice only, no update action (fourth review Q4-04): no 「更新」(Update) button, controller key or mouse click entry; players update from the download page themselves.
// In the layout the text is empty and the backplate is off screen; here, on the game main thread, we write the text and move what should be shown back into place. With no such widgets (Mac host running the original layout) or nobody calling dm_update_init = do nothing.
// Networking and comparing versions are on the shell side; here only the result given by dm_update_offer is honored.
#include "hle.h"
#include "dm.h"
#include "dm_i18n.h"
#include "dm_cursor.h"
#include "dm_uiscreen.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdatomic.h>
#include <pthread.h>

static int g_own = -1; static char g_label[64]; static void (*g_on_select)(void);   // g_own 只用来判「初始化过没有」，比新旧在外壳 | g_own only tells whether init ran; comparing versions is on the shell side
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER; static struct { int build, urgent; char ver[24], note[160]; } g_offer;   // g_mu 护 g_offer | g_mu guards g_offer
static _Atomic int g_safe_l = -1, g_dirty, g_appear, g_fg;   // g_safe_l < 0 = 外壳还没给左安全区，先不写字（免得字先出在安全区里再跳一下）。g_dirty：还要重写几帧（>0 时每帧找控件、写字、摆位置；写完一次后再跟 30 帧，等文字框量出新宽度） | g_safe_l < 0 = the shell has not supplied the left safe area yet, so do not write text for now (to avoid the text first showing inside the safe area and then jumping). g_dirty: how many more frames to rewrite (when >0, every frame find widgets, write text, set positions; after one write keep following for 30 more frames, waiting for the text box to measure the new width)

// label = 显示版本（空 = 退回写构建号） | label = display version (empty = fall back to the build number)
void dm_update_init(int own_build, const char *label, void (*on_select)(void)) { g_own = own_build; if (label && *label) strlcpy(g_label, label, sizeof g_label); else snprintf(g_label, sizeof g_label, "%d", own_build); g_on_select = on_select; atomic_store(&g_dirty, 30); }   // 不在这里置「选角页出现」：启动时游戏自己会发一次 OpenPanel CharacterSelectPanel，这里再置就每次启动查两遍 | Do not set "character select page appears" here: at startup the game sends OpenPanel CharacterSelectPanel once by itself, and setting it here too would check twice on every startup
// 外壳查完的结果：任意线程调。new_build > 0 = 有比自己新的构建（ver = 版本文件里给人看的版本号，空 = 写构建号；note = 一句话更新说明，UTF-8；urgent = 建议立即更新）；0 = 没有，撤掉已有提示
// Result of the shell's check: callable from any thread. new_build > 0 = there is a build newer than ours (ver = the human version from the version file, empty = show the build number; note = one-sentence update note, UTF-8; urgent = immediate update recommended); 0 = none, remove any existing notice
void dm_update_offer(int new_build, const char *ver, const char *note, int urgent) {
    pthread_mutex_lock(&g_mu); g_offer.build = new_build; g_offer.urgent = urgent; if (ver && *ver) strlcpy(g_offer.ver, ver, sizeof g_offer.ver); else snprintf(g_offer.ver, sizeof g_offer.ver, "%d", new_build); strlcpy(g_offer.note, note ? note : "", sizeof g_offer.note); pthread_mutex_unlock(&g_mu);
    atomic_store(&g_dirty, 30);
}
// 查新版本失败（玩家停在旧包、更新查询静默失败，玩家不知道有新版）：任意线程调。
// 0 = 这次取到了（撤掉失败提示）；1 = 系统说这个 App 没有无线数据许可（玩家自己能在设置里开）；2 = 其余取不到（地址失效、断网、格式不对…）。
// 失败提示只在没有新版本提示时显示（已有的新版本提示照旧留着），写在普通更新提示那一行（TouchUpdNote），不出「更新」按钮
// New version check failed (the player stays on an old package, the update query fails silently, and the player does not know there is a new version): callable from any thread.
// 0 = fetched this time (remove the failure notice); 1 = the system says this App has no wireless data permission (the player can turn it on in Settings); 2 = any other fetch failure (URL dead, offline, bad format...).
// The failure notice is shown only when there is no new version notice (an existing new version notice stays as before), written on the ordinary update notice line (TouchUpdNote), without the 「更新」(Update) button
static _Atomic int g_fail;
static char g_site[96];   // 手动更新去哪：版本文件地址里的主机名（外壳在启动时给，之后不变）；没配更新地址就为空 | Where to go for a manual update: the host name in the version file URL (given by the shell at startup, unchanged afterwards); empty if no update URL is configured
const char *dm_update_label(void) { return g_label; }   // 提示框「关于」段的版本行（dm_community.c）；外壳没初始化时为空 | the version line of the About section (dm_community.c); empty before the shell initializes it
void dm_update_site(const char *host) { snprintf(g_site, sizeof g_site, "%s", host ? host : ""); }
void dm_update_fail(int kind) { if (atomic_exchange(&g_fail, kind) != kind) atomic_store(&g_dirty, 30); }
// 左安全区（布局单位，2160 高；iPhone 横屏约 304，iPad 0）：前端在 iPhone 上铺满整屏，左下角那行字要让到安全线内。任意线程调
// Left safe area (layout units, 2160 high; about 304 on iPhone landscape, 0 on iPad): the front end fills the whole screen on iPhone, so the bottom-left line of text must move inside the safe line. Callable from any thread
void dm_update_safe_left(int units) { atomic_store(&g_safe_l, units < 0 ? 0 : units > 800 ? 800 : units); atomic_store(&g_dirty, 30); }
// 从后台回到前台：外壳调（任意线程）。回来时人若正停在选角页，按「选角页出现」同一条路重查一次（进首页就查）；在局内 / 别的页就作废，不留到以后
// Returning from background to foreground: called by the shell (any thread). If the user is sitting on the character select page on return, recheck once via the same path as "character select page appears" (checked on entering the home page); in a run / on another page it is discarded, not kept for later
void dm_update_foreground(void) { if (g_own >= 0) atomic_store(&g_fg, 1); }
// 模式切换事务完成、界面整套重建之后（dm_mode_items.h 登记的「进」）：控件全是新的、字是空的，重写一遍。重建时游戏不发 OpenPanel MainMenuPanel，等不到下面那条消息
// After the mode switch transaction completes and the whole UI has been rebuilt (the "enter" registered in dm_mode_items.h): the widgets are all new and their text is empty, so write it again. During the rebuild the game does not send OpenPanel MainMenuPanel, so the message below never arrives
void dm_update_mode_enter(void) { if (g_own >= 0) atomic_store(&g_dirty, 30); }
void dm_update_on_uimsg(const char *cat, const char *act, const char *arg) {   // 界面消息回调（diag.c 转来，游戏主线程） | UI message callback (forwarded from diag.c, game main thread)
    if (g_own < 0 || strcmp(cat, "PanelManager")) return;
    // 「选页出现」= 玩家落脚的第一页：触屏版是选角页（CharacterSelectPanel），手柄版是原版手柄主菜单（MainMenuButtonRibbon：左侧按钮栏 + 角色预览，玩家多半从这里直接进游戏、不开角色列表）。
    // 原先只认选角页：手柄玩家不开角色列表，外壳就一直不给左安全区（iOS dm_update.m on_select），版本字一直不写、新版本也不查（用户 2026-10-28：手柄模式下版本号不显示）
    // "Page appears" = the first page the player lands on: the character select page in the touch layout (CharacterSelectPanel), the original controller main menu in the controller layout (MainMenuButtonRibbon: button ribbon + character preview; players mostly enter the game from there without opening the character list).
    // Only the character select page counted before: a controller player who never opened the character list never got the left safe area from the shell (iOS dm_update.m on_select), so the version label was never written and no update check ran (user 2026-10-28: version label missing in controller mode)
    int appear = !strcmp(arg, "CharacterSelectPanel") || !strcmp(arg, "MainMenuButtonRibbon");
    if (!strcmp(act, "OpenPanel") && (appear || !strcmp(arg, "MainMenuPanel"))) { atomic_store(&g_dirty, 30);   // 面板重建后控件是新的，重写一遍 | After a panel rebuild the widgets are new, so write everything again
        if (appear) atomic_store(&g_appear, 1); }   // 选角页出现一次（启动、从局里退回来、列表收起后重开）：外壳重查一次新版本 | The character select page appears once (startup, returning from a run, list reopened after being collapsed): the shell rechecks once for a new version
}
static uint8_t *tb(void) { static uint8_t *b; if (!b) for (int i = 0; i < g_nmod; i++) if (!strcmp(g_mod[i].name, "main")) b = (uint8_t *)g_mod[i].base; return b; }
static uint64_t find(uint64_t w, const char *name, int dep) {   // 按名字找控件：名字 +8、子控件数组 +0x58、个数 +0x60（同 dm_cursor.c ui_find） | Find a widget by name: name +8, child array +0x58, count +0x60 (same as dm_cursor.c ui_find)
    if (!w || dep > 6) return 0; const char *nm = *(const char **)(w + 8); if (nm && !strcmp(nm, name)) return w;
    uint64_t ch = *(uint64_t *)(w + 0x58), n = *(uint64_t *)(w + 0x60); if (!ch || n > 256) return 0;
    for (uint64_t i = 0; i < n; i++) { uint64_t r = find(*(uint64_t *)(ch + 8 * i), name, dep + 1); if (r) return r; }
    return 0;
}
static void set_text(uint8_t *b, uint64_t w, const char *s);
static void place(uint64_t w, int x, int y) {   // 矩形 x / y 在 +0x70 / +0x74；改完照原版补一次虚表 +0x10 让它重算（同 dm_cursor.c 安全区那处） | Rectangle x / y at +0x70 / +0x74; after changing them, call vtable +0x10 once as the original game does so it recomputes (same as the safe area site in dm_cursor.c)
    int32_t *r = (int32_t *)(w + 0x70); if (r[0] == x && r[1] == y) return; r[0] = x; r[1] = y; ((void (*)(uint64_t))*(uint64_t *)(*(uint64_t *)w + 0x10))(w);
}
static void test_on_select(void) { LOG("更新：测试开关的外壳回调——给左安全区 0"); dm_update_safe_left(0); }   // 同 iOS dm_update.m on_select：页出现时外壳才给左安全区 | like iOS dm_update.m on_select: the shell supplies the left safe area only when the page appears
void dm_update_pump(void) {   // 取手柄处每帧调（游戏主线程） | Called every frame at the controller poll (game main thread)
    if (g_own < 0) {   // 测试开关 DM_TEST_UPDATE_LABEL=<字串>：没有包信息的宿主（Mac 原生宿主 dmhost）也按这串字写选角页版本标签，用来核两种布局下的位置 | test switch DM_TEST_UPDATE_LABEL=<text>: a host without bundle info (the Mac native host dmhost) also writes the character-select version label with this text, to check its position in both layouts
        static int once; const char *e = once++ ? NULL : getenv("DM_TEST_UPDATE_LABEL"); if (!e || !*e) return;
        dm_update_init(1, e, test_on_select); LOG("更新：测试开关 DM_TEST_UPDATE_LABEL，版本标签写「%s」（安全区同 iOS 外壳：等选页回调再给）", e); }
    if (atomic_exchange(&g_fg, 0)) {   // 选角列表面板在且显示着（可见位 +0x50）、构建号控件也在 = 人停在选角页 | Character select list panel present and shown (visible bit +0x50) and the build number widget also present = the user is sitting on the character select page
        uint8_t *fb = tb(); uint64_t fr = fb ? *(uint64_t *)(fb + 0x2f05458) : 0, fm = fr ? find(fr, "MainMenuPanel", 0) : 0, fl = fr ? find(fr, "CharacterSelectPanel", 0) : 0;
        if (fm && find(fm, "TouchBuild", 0) && fl && *(uint8_t *)(fl + 0x50)) { atomic_store(&g_appear, 1); atomic_store(&g_dirty, 30); LOG("更新：回到前台时停在选角页，重查一次新版本"); } }
    int bottom=dm_ui_bottom_offset(24); static int last_bottom=-99999;
    if (bottom!=last_bottom) { last_bottom=bottom; atomic_store(&g_dirty,30); }
    int left = atomic_load(&g_dirty); if (left <= 0) return;
    uint8_t *b = tb(); uint64_t root = b ? *(uint64_t *)(b + 0x2f05458) : 0, mm = root ? find(root, "MainMenuPanel", 0) : 0, bw = mm ? find(mm, "TouchBuild", 0) : 0;
    if (!bw) return;   // 主菜单面板还没建（或跑的是没有这些控件的布局）：留着，建好那帧再写 | Main menu panel not built yet (or running a layout without these widgets): keep it, and write on the frame it is built
    if (atomic_exchange(&g_appear, 0) && g_on_select) g_on_select();   // 外壳这时给左安全区、查一次新版本 | At this point the shell supplies the left safe area and checks once for a new version
    if (atomic_load(&g_safe_l) < 0) return;
    atomic_compare_exchange_strong(&g_dirty, &left, left - 1);   // 期间又被置回 30（新结果 / 面板重建）就不减 | If it was set back to 30 in the meantime (new result / panel rebuilt), do not decrement
    int build, urgent; char ver[24], note[160]; pthread_mutex_lock(&g_mu); build = g_offer.build; urgent = g_offer.urgent; memcpy(ver, g_offer.ver, sizeof ver); memcpy(note, g_offer.note, sizeof note); pthread_mutex_unlock(&g_mu);
    char t[256]; snprintf(t, sizeof t, dm_str(b, "dmUpdBuild"), g_label); set_text(b, bw, t);
    int sl = atomic_load(&g_safe_l), bx = sl + 6 > 79 ? sl + 6 : 79; int dm_padmode_current(void); int pad = dm_padmode_current();
    // 手柄模式（原版手柄主菜单）：左下角是原版的按钮栏和提示条，这两行改放右上角、靠右对齐。原版手柄主菜单面板的矩形是 0×0（锚点无从算起），
    // 这份控件在布局里不带锚点（touch_layout.pad_mainmenu），位置全由这里按屏宽（布局单位：2160 = 屏高）算
    // Controller mode (the original controller main menu): the bottom left holds the original button ribbon and legend, so these two lines go to the top right instead, right-aligned. The original controller main menu panel's rectangle is 0×0 (nothing to compute an anchor from),
    // so these widgets carry no anchor in the layout (touch_layout.pad_mainmenu), and their position is computed entirely here from the screen width (layout units: 2160 = screen height)
    int vw = *(int32_t *)(b + 0x211da38), vh = *(int32_t *)(b + 0x211da3c), xr = vh > 0 ? (int)((long)vw * 2160 / vh) : 3840;
    if (pad) place(bw, xr - 600 - bx, 60); else place(bw, bx, 2046+bottom);
  // 触屏：左下角那行，离屏幕左边 79 单位，有安全区时贴安全线内 6 单位
  // Touch: the line at the bottom left, 79 units from the left edge of the screen, or 6 units inside the safe line when there is a safe area
    uint64_t nt = find(mm, "TouchUpdNote", 0), bg = find(mm, "TouchUpdBg", 0), ti = find(mm, "TouchUpdTitle", 0), de = find(mm, "TouchUpdDesc", 0);
    if (!nt || !bg || !ti || !de) { LOG("【红】更新：主菜单面板里的提示控件不全，不显示提示"); return; }
    int normal = build > 0 && !urgent, urg = build > 0 && urgent, fail = build > 0 ? 0 : atomic_load(&g_fail);
    snprintf(t, sizeof t, dm_str(b, "dmUpdNew"), ver);
    char ft[192]; if (g_site[0]) snprintf(ft, sizeof ft, dm_str(b, "dmUpdFailSite"), g_site); else snprintf(ft, sizeof ft, "%s", dm_str(b, "dmUpdFail"));
    set_text(b, nt, normal ? t : fail == 1 ? dm_str(b, "dmUpdNoNet") : fail ? ft : "");
    snprintf(t, sizeof t, dm_str(b, "dmUpdNewUrgent"), ver, note); set_text(b, de, urg ? t : ""); set_text(b, ti, urg ? dm_str(b, "dmUpdUrgentTitle") : "");
    if (pad) { place(bg, urg ? xr / 2 - 700 : 99999, urg ? 1506 : 99999); place(ti, xr / 2 - 700, 1524); place(de, xr / 2 - 700, 1610); }
    else place(bg, urg ? -700 : 99999, urg ? 1506 : 99999);
    // 提示字在显示版本右边起：至少 341 单位，版本字更宽就跟在它后面隔 34 单位。
    // 两个文字框都开了 fitToText，每帧更新（0xadc8e8）把量出的宽度写回 +0x78（显示版本没量出来前是布局宽 420）
    // The notice text starts right of the display version: at least 341 units, or 34 units after the version text when that is wider.
    // Both text boxes have fitToText on, and the per-frame update (0xadc8e8) writes the measured width back to +0x78 (the layout width 420 until the display version is measured)
    int nw = *(int32_t *)(nt + 0x78), tw = *(int32_t *)(bw + 0x78), nx = bx + (tw + 34 > 341 ? tw + 34 : 341);
    if (pad) place(nt, xr - 2000 - bx, 130);   // 手柄：提示字在构建号下面一行 | Controller: the hint text goes on the line below the build number
    else place(nt, nx, 2046+bottom);
    static dm_wref lw; static int ls = -1, lnw, ltw; int st = normal ? 1 : urg ? 2 : fail ? 2 + fail : 0;
    if (dm_wref_get(&lw) != bw || ls != st || ltw != tw || (normal && lnw != nw)) { dm_wref_set(&lw, bw); ls = st; lnw = nw; ltw = tw; LOG("更新：选角页写上版本 %s%s（版本字宽 %d，提示字宽 %d）", g_label, normal ? "，普通更新提示" : urg ? "，建议立即更新提示" : fail == 1 ? "，查更新失败提示（无线数据许可）" : fail ? "，查更新失败提示（浏览器手动更新）" : "，无提示", tw, normal ? nw : 0); }
}
// 文字框（TextBoxWidget，虚表 main+0x20ba4c8）设文本 = 游戏自己到处内联的两步（如 0x38f5bc–0x38f5cc、0x3d8480–0x3d849c）：
// 字符串赋值 0x136e5c(控件+0x88, 字节, 字节数)，再 0xadc434(控件) 刷新省略号截断串。绘制每帧现取 +0x88 现量现画，不用另调重排。不是文字框就不动
// Setting text on a text box (TextBoxWidget, vtable main+0x20ba4c8) = the two steps the game itself inlines everywhere (e.g. 0x38f5bc-0x38f5cc, 0x3d8480–0x3d849c):
// string assign 0x136e5c(widget+0x88, bytes, byte count), then 0xadc434(widget) to refresh the ellipsis-truncated string. Drawing fetches +0x88, measures and draws fresh every frame, so no separate re-layout call is needed. If it is not a text box, leave it alone
static void set_text(uint8_t *b, uint64_t w, const char *s) {
    if (*(uint64_t *)w - (uint64_t)(uintptr_t)b != 0x20ba4c8) return;
    size_t n = strlen(s); if (*(uint64_t *)(w + 0x90) == n && !memcmp(*(const char **)(w + 0x88), s, n)) return;
    ((void *(*)(uint64_t, const char *, uint64_t))(b + 0x136e5c))(w + 0x88, s, n); ((void (*)(uint64_t))(b + 0xadc434))(w);
}
