// B143：界面判类里「模态框外」与「设置页盖着的暂停菜单按钮」两道闸门的先后（dm_ui_gate.h），外加 dm_cursor.c ui_classify 真的按它走的源码核对。
// 用户路径：选项 → 音频改 NPC 语音 → 点「图形」页签 → 确认框；框的「应用 / 放弃」压在暂停菜单按钮上方，原先被判「被盖住、不点」，框外又是「模态框外」= 整屏点不动。
// B143: order of the "outside the modal" and "pause menu button covered by the settings page" gates in UI classification (dm_ui_gate.h), plus a source check that ui_classify in dm_cursor.c really goes through it.
#include "../dm_ui_gate.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int fails;
#define EQ(a, b, what) do { int _a = (a), _b = (b); if (_a != _b) { printf("FAIL %s: %d != %d\n", what, _a, _b); fails++; } } while (0)
int main(void) {
    // 模态框开着 | a modal is open
    EQ(dm_ui_gate(1, 1, 1), -1, "确认框里的「应用」压在暂停菜单按钮上：照常判（B143 主用例）| Apply over a pause menu button: carry on (B143)");
    EQ(dm_ui_gate(1, 1, 0), -1, "框里、底下没按钮：照常判 | inside, nothing underneath: carry on");
    EQ(dm_ui_gate(1, 0, 1), 9, "框外、压着暂停菜单按钮：模态框外 | outside, over a pause button: outside the modal");
    EQ(dm_ui_gate(1, 0, 0), 9, "框外：模态框外 | outside: outside the modal");
    // 没有模态框：设置页那道照旧 | no modal: the settings gate as before
    EQ(dm_ui_gate(0, 0, 1), 11, "设置页底板下的暂停菜单按钮：不点 | covered pause button: no click");
    EQ(dm_ui_gate(0, 0, 0), -1, "普通一点：照常判 | an ordinary tap: carry on");
    EQ(dm_ui_gate(0, 1, 1), 11, "没有模态框时 in_modal 不起作用 | in_modal is ignored without a modal");
    // 源码核对：ui_classify 里设置页那道不再直接 return 11，两道都经 dm_ui_gate | source check: the settings gate no longer returns 11 itself; both go through dm_ui_gate
    FILE *f = fopen("../dm_cursor.c", "rb"); if (!f) { puts("FAIL 读不到 ../dm_cursor.c"); return 1; }
    fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f); char *s = malloc(n + 1); fread(s, 1, n, f); s[n] = 0; fclose(f);
    char *fn = strstr(s, "static int ui_classify(module_t *m, uint64_t root, int x, int y, char *hit, size_t hn) {");
    char *st = fn ? strstr(fn, "int cover = settings_cover(m, root, x, y);") : 0, *gate = fn ? strstr(fn, "dm_ui_gate(mp != 0, in_modal, cover)") : 0;
    char *mt = fn ? strstr(fn, "modal_top(m, root, &mn)") : 0;
    EQ(fn && st && gate && mt ? 1 : 0, 1, "ui_classify 里找到设置页那道、modal_top 与 dm_ui_gate 调用 | found the settings gate, modal_top and dm_ui_gate in ui_classify");
    if (st && gate && mt) {
        EQ(st < gate && mt < gate, 1, "dm_ui_gate 在两道判读之后 | dm_ui_gate comes after both readings");
        char save = *gate; *gate = 0;
        EQ(strstr(st, "return 11") == 0, 1, "设置页那道到 dm_ui_gate 之间没有直接 return 11 | no direct return 11 before dm_ui_gate");
        EQ(strstr(st, "return 9") == 0, 1, "模态框那段到 dm_ui_gate 之间没有直接 return 9 | no direct return 9 before dm_ui_gate");
        *gate = save;
    }
    { char *sc = strstr(s, "static int settings_cover(module_t *m, uint64_t root, int x, int y) {"), *se = sc ? strstr(sc, "\n}\n") : 0;
      EQ(sc && se && strstr(sc, "slider_part_hit(page, x, y, (uint64_t)m->base, *(int32_t *)(m->base + 0x211da3c))") && strstr(sc, "slider_part_hit(page, x, y,") < se ? 1 : 0, 1, "B145：设置页那道（settings_cover）认滑块箭头 | B145: the settings gate (settings_cover) accepts slider arrows");
      { char sv = se ? *se : 0; if (se) *se = 0; int no11 = sc && !strstr(sc, "return 11"); if (se) *se = sv; EQ(no11, 1, "settings_cover 只报 cover、不自己 return 11 | settings_cover only reports cover"); } }
    // B158：真正发点击处也判设置页盖层（模态框不在时）| B158: the click path judges the settings cover too (when no modal)
    EQ(strstr(s, "if (!modal_top(m, root, &mn0) && settings_cover(m, root, x, y))") != NULL, 1, "B158：点击路径过 settings_cover | B158: click path goes through settings_cover");
    // B157：任务日志里点页签 / X 不再判成空处 | B157: in the quest log taps on tabs / the X are no longer judged empty space
    { char *q = strstr(s, "static int quest_log_tap(module_t *m, uint64_t root, int x, int y) {"), *qe = q ? strstr(q, "\n}\n") : 0, *c = q ? strstr(q, "if (ql_close(root, x, y)) return 0;") : 0, *t = q ? strstr(q, "if (ql_tab(m, root, x, y, &d, &top))") : 0, *sn = q ? strstr(q, "ui_snap(m, root, &sx, &sy) ? 1 : 2") : 0;
      EQ(q && c && t && sn && c < sn && t < sn && sn < qe ? 1 : 0, 1, "B157：quest_log_tap 先认 X / 页签再判空处 | B157: quest_log_tap checks the X / tabs before empty space");
      EQ(strstr(s, "else if (qt == 3) {") && strstr(strstr(s, "else if (qt == 3) {"), "dm_ui_tab_go();") ? 1 : 0, 1, "B157：qt == 3 切页签 | B157: qt == 3 switches tabs"); }
    free(s);
    if (fails) { printf("t_ui_gate: %d FAIL\n", fails); return 1; }
    puts("PASS B143 ui gate: modal first (inside = carry on, outside = 9), settings cover 11 only without a modal; ui_classify goes through dm_ui_gate");
    return 0;
}
