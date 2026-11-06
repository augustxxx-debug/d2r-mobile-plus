// B157 / B158 行为测试（交叉审 P1 / 变异存活）：① dm_ql_decide 真值表——页签条下面第一条任务行（放宽档命中页签、触点本身在任务行上）不切页签；
// ② settings_cover 跑合成控件树：设置页盖着暂停菜单时，点在设置页真实控件上（应用按钮、滑条左箭头）不算盖着，只有空白处压着暂停菜单按钮才算
// B157 / B158 behaviour tests (cross-review P1 / surviving mutant): (1) the dm_ql_decide truth table -- the first quest row under the tab bar (widened step hits a tab, the touch point itself on the row)
// does not switch tabs; (2) settings_cover on a synthetic widget tree: with the settings page over the pause menu, real settings widgets (Apply button, a slider's left arrow) are not "covered"; only blank space over a pause button is
#include "../dm_cursor.c"
#include <assert.h>
module_t g_mod[MAX_MOD]; int g_nmod; uint64_t dm_guest_frame(void) { return 0; }
typedef struct { int x0, y0, x1, y1; } box;
static _Alignas(16) unsigned char vtb[0x70000];
static _Alignas(8) unsigned char root[160], pause[160], exitb[160], settings[160], apply[160], page[160], slider[160];
static uint64_t root_ch[3], pause_ch[1], set_ch[1], page_ch[1];
static box bx_exit = { 1100, 400, 1520, 700 }, bx_apply = { 1100, 640, 1300, 690 }, bx_slider = { 1458, 495, 1788, 519 };
static uint64_t P(void *p) { return (uint64_t)(uintptr_t)p; }
static int hit_fn(uint64_t w, uint64_t xy) { box *b = w == P(exitb) ? &bx_exit : w == P(apply) ? &bx_apply : w == P(slider) ? &bx_slider : 0; int x = (int)(uint32_t)xy, y = (int)(uint32_t)(xy >> 32); return b && x >= b->x0 && x <= b->x1 && y >= b->y0 && y <= b->y1; }
static int none_fn(uint64_t w, uint64_t xy) { (void)w; (void)xy; return 0; }
static void mk(unsigned char *w, const char *nm, uint64_t vt, uint64_t *ch, uint64_t n) { *(uint64_t *)w = vt; *(const char **)(w + 8) = nm; *(uint64_t *)(w + 0x58) = P(ch); *(uint64_t *)(w + 0x60) = n; w[0x50] = w[0x51] = 1; }
int main(void) {
    // ① 任务日志 | quest log
    assert(dm_ql_decide(1, 1, 1, 1) == 0);    // 点在 X 上：照常点 | on the X: click as usual
    assert(dm_ql_decide(0, 1, 0, 0) == 3);    // 正点在页签上：切页签 | right on a tab: switch
    assert(dm_ql_decide(0, 1, 1, 1) == 3);    // 正点在页签上（哪怕也挨着控件）| right on a tab even next to a widget
    assert(dm_ql_decide(0, 0, 1, 0) == 3);    // 页签条稍下方的空处：放宽档算页签 | blank just under the tab bar: the widened step counts
    assert(dm_ql_decide(0, 0, 1, 1) == -1);   // 页签条下面第一条任务行：不切页签，接着按任务行判（显示当前目标）| first quest row under the tab bar: no switch, judged as the quest row (shows the objective)
    assert(dm_ql_decide(0, 0, 0, 1) == -1);   // 普通任务行 | an ordinary quest row
    assert(dm_ql_decide(0, 0, 0, 0) == -1);   // 空处 | empty space
    // ② 设置页盖层 | settings cover
    uint64_t base = P(vtb) - 0x20b7b00;   // base + 0x20b7b00 = 按钮类虚表（click_walk 认）| button vtable recognized by click_walk
    uint64_t vt_btn = base + 0x20b7b00, vt_sl = base + 0x20bad70, vt_pl = P(vtb) + 0x800;
    *(uint64_t *)(vt_btn + 0x48) = P(hit_fn); *(uint64_t *)(vt_sl + 0x48) = P(hit_fn); *(uint64_t *)(vt_pl + 0x48) = P(none_fn);
    *(int32_t *)(base + 0x211da3c) = 1206;   // 画面高（slider_part_hit 换算布局单位用）| screen height used by slider_part_hit
    module_t mm; memset(&mm, 0, sizeof mm); mm.base = (void *)(uintptr_t)base;
    pause_ch[0] = P(exitb); set_ch[0] = P(apply); page_ch[0] = P(slider);
    mk(exitb, "Exit", vt_btn, 0, 0); mk(apply, "AcceptButton", vt_btn, 0, 0); mk(slider, "TouchGraphicsBright", vt_sl, 0, 0);
    mk(pause, "PauseLayoutGarden", vt_pl, pause_ch, 1); mk(settings, "SettingsPanel", vt_pl, set_ch, 1); mk(page, "SoundOptionsPanel", vt_pl, page_ch, 1);
    root_ch[0] = P(pause); root_ch[1] = P(settings); root_ch[2] = P(page); mk(root, "root", vt_pl, root_ch, 3);
    assert(settings_cover(&mm, P(root), 1400, 600) == 1);    // 列表空白处、底下是「保存并退出」：盖着（B158 要挡）| blank space over Save and Exit: covered (B158 blocks it)
    assert(settings_cover(&mm, P(root), 1200, 660) == 0);    // 「应用」按钮（底下也有暂停菜单按钮）：设置页自己的，照常点 | the Apply button: the settings page's own, clicks as usual
    assert(settings_cover(&mm, P(root), 1437, 507) == 0);    // 滑条左箭头（压在暂停菜单按钮列上）：照常点 | the slider's left arrow over the pause column: clicks as usual
    assert(settings_cover(&mm, P(root), 1600, 507) == 0);    // 滑槽 | the track
    assert(settings_cover(&mm, P(root), 300, 300) == 0);     // 底下没有暂停菜单按钮 | no pause button underneath
    settings[0x50] = 0; assert(settings_cover(&mm, P(root), 1400, 600) == 0); settings[0x50] = 1;   // 设置页没开：不管 | settings closed: not involved
    puts("PASS B157/B158 behaviour: first quest row under the tab bar is not a tab tap; settings cover blocks blank space over pause buttons but not Apply / slider arrows / track");
    return 0;
}
