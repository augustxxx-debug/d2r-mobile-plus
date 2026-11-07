// 手柄物品栏焦点导航的行为用例（B235 / B239 / B189 ①–④）：按 dm_bag_nav.h 的规则表逐格断言「从这个元素按上 / 下 / 左 / 右各一次落到哪」。
// 期望表 K_EXPECT 是手写的规格（原版几何 + 我们的页签规则，空手、无记忆）；之后是记忆、复位、拿着物品、腰带第二排、切页键、可达性各一组。
// --sim：给 Mac 整机判据（ui_regress/bag_nav_judge.py）用，从标准输入读 reset / held <0|1> / step <起点> <方向 U|D|L|R> <实测落点>（step2 = 同方向连按两下），每个 step 输出期望落点。
// Behaviour cases for controller inventory focus navigation (B235 / B239 / B189 items 1-4): per element, assert where one press of up / down / left / right lands, by the rule table in dm_bag_nav.h.
// K_EXPECT is the hand-written spec (original geometry + our tab rules, empty-handed, no memory); then groups for memory, reset, item in hand, the belt's second row, page keys and reachability.
// --sim: for the Mac whole-machine judge (ui_regress/bag_nav_judge.py): reads reset / held <0|1> / step <from> <dir U|D|L|R> <observed> (step2 = two quick presses) from stdin and prints the expected landing for each step.
#include "dm_bag_nav.h"
#include <stdio.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
static const char D4[] = "UDLR";

// 期望表：<起点> U:<落点> D:<落点> L:<落点> R:<落点>。ORIG = 交原版（对面面板 / 顶栏 / 边界 / 腰带展开）；网格只列四边（内部逐格走）。
//   空手；拿着物品时的差别见第 5 组。要点：第 0 行 0–1 列 ↑ 右手武器、2 列手套、3–8 列腰带四格、9 列鞋、10–11 列左手武器——没有一列进页签（B235）；
//   页签只从腰带第 0 格 ← 进（TAB1），TAB1 ← TAB0、→ 回腰带第 0 格，↓ 回第 0 行；腰带位 ↓ 进腰带第 1 格、腰带 ↓ 进网格（B239）；底排钮 ↑ 回网格，其余方向原版；武器 I / II 钮与左右武器、头 / 项链上下左右相连（Mac 实测原版）。
// Expected table: <from> U:<to> D:<to> L:<to> R:<to>. ORIG = left to the original (opposite panel / top bar / border / expanded belt); for the grid only the four edges are listed (inside it walks cell by cell).
//   Empty-handed; the differences with an item in hand are group 5. Key points: row 0 Up goes columns 0-1 right weapon, 2 gloves, 3-8 the belt cells, 9 boots, 10-11 left weapon -- no column enters a tab (B235); the tabs are entered only by Left
//   on belt cell 0 (TAB1), TAB1 Left TAB0, Right back to belt cell 0, Down back to row 0; belt slot Down enters belt cell 1 and the belt Down enters the grid (B239); bottom buttons Up back to the grid, other directions original; the weapon I / II buttons link to the weapons and head / amulet (measured original).
static const char *const K_EXPECT[] = {
    "grid(0,0) U:RARM D:grid(0,1) L:ORIG R:grid(1,0)",
    "grid(1,0) U:RARM D:grid(1,1) L:grid(0,0) R:grid(2,0)",
    "grid(2,0) U:GLOVES D:grid(2,1) L:grid(1,0) R:grid(3,0)",
    "grid(3,0) U:BELT0 D:grid(3,1) L:grid(2,0) R:grid(4,0)",
    "grid(4,0) U:BELT0 D:grid(4,1) L:grid(3,0) R:grid(5,0)",
    "grid(5,0) U:BELT1 D:grid(5,1) L:grid(4,0) R:grid(6,0)",
    "grid(6,0) U:BELT2 D:grid(6,1) L:grid(5,0) R:grid(7,0)",
    "grid(7,0) U:BELT3 D:grid(7,1) L:grid(6,0) R:grid(8,0)",
    "grid(8,0) U:BELT3 D:grid(8,1) L:grid(7,0) R:grid(9,0)",
    "grid(9,0) U:FEET D:grid(9,1) L:grid(8,0) R:grid(10,0)",
    "grid(10,0) U:LARM D:grid(10,1) L:grid(9,0) R:grid(11,0)",
    "grid(11,0) U:LARM D:grid(11,1) L:grid(10,0) R:ORIG",
    "grid(0,1) U:grid(0,0) D:grid(0,2) L:ORIG R:grid(1,1)",
    "grid(11,1) U:grid(11,0) D:grid(11,2) L:grid(10,1) R:ORIG",
    "grid(0,2) U:grid(0,1) D:grid(0,3) L:ORIG R:grid(1,2)",
    "grid(11,2) U:grid(11,1) D:grid(11,3) L:grid(10,2) R:ORIG",
    "grid(0,3) U:grid(0,2) D:grid(0,4) L:ORIG R:grid(1,3)",
    "grid(11,3) U:grid(11,2) D:grid(11,4) L:grid(10,3) R:ORIG",
    "grid(0,4) U:grid(0,3) D:grid(0,5) L:ORIG R:grid(1,4)",
    "grid(11,4) U:grid(11,3) D:grid(11,5) L:grid(10,4) R:ORIG",
    "grid(0,5) U:grid(0,4) D:grid(0,6) L:ORIG R:grid(1,5)",
    "grid(11,5) U:grid(11,4) D:grid(11,6) L:grid(10,5) R:ORIG",
    "grid(0,6) U:grid(0,5) D:POUCH L:ORIG R:grid(1,6)",
    "grid(1,6) U:grid(1,5) D:POUCH L:grid(0,6) R:grid(2,6)",
    "grid(2,6) U:grid(2,5) D:POUCH L:grid(1,6) R:grid(3,6)",
    "grid(3,6) U:grid(3,5) D:POUCH L:grid(2,6) R:grid(4,6)",
    "grid(4,6) U:grid(4,5) D:GOLD L:grid(3,6) R:grid(5,6)",
    "grid(5,6) U:grid(5,5) D:GOLD L:grid(4,6) R:grid(6,6)",
    "grid(6,6) U:grid(6,5) D:GOLD L:grid(5,6) R:grid(7,6)",
    "grid(7,6) U:grid(7,5) D:GOLD L:grid(6,6) R:grid(8,6)",
    "grid(8,6) U:grid(8,5) D:STORE L:grid(7,6) R:grid(9,6)",
    "grid(9,6) U:grid(9,5) D:STORE L:grid(8,6) R:grid(10,6)",
    "grid(10,6) U:grid(10,5) D:STORE L:grid(9,6) R:grid(11,6)",
    "grid(11,6) U:grid(11,5) D:STORE L:grid(10,6) R:ORIG",
    "RARM U:SWAPR D:grid(0,0) L:ORIG R:GLOVES",
    "HEAD U:ORIG D:GLOVES L:SWAPR R:TORSO",
    "GLOVES U:HEAD D:BELT0 L:RARM R:BELTSLOT",
    "TORSO U:ORIG D:BELTSLOT L:HEAD R:RRING",
    "BELTSLOT U:TORSO D:BELT1 L:GLOVES R:FEET",
    "NECK U:ORIG D:LRING L:TORSO R:SWAPL",
    "RRING U:NECK D:FEET L:TORSO R:LRING",
    "LRING U:NECK D:FEET L:RRING R:LARM",
    "FEET U:RRING D:BELT3 L:BELTSLOT R:LARM",   // 鞋格 ↑ 右戒指（原版，等距单写） | boots Up: right ring (original; equidistant tie written out)
    "LARM U:SWAPL D:grid(11,0) L:LRING R:ORIG",
    "SWAPR U:ORIG D:RARM L:ORIG R:HEAD",
    "SWAPL U:ORIG D:LARM L:NECK R:ORIG",
    "TAB0 U:RARM D:grid(0,0) L:ORIG R:TAB1",
    "TAB1 U:RARM D:grid(2,0) L:TAB0 R:BELT0",
    "BELT0 U:ORIG D:grid(4,0) L:TAB1 R:BELT1",
    "BELT1 U:ORIG D:grid(5,0) L:BELT0 R:BELT2",
    "BELT2 U:ORIG D:grid(6,0) L:BELT1 R:BELT3",
    "BELT3 U:ORIG D:grid(8,0) L:BELT2 R:grid(9,0)",
    "BELTU0 U:ORIG D:BELT0 L:ORIG R:ORIG",
    "BELTU1 U:ORIG D:BELT1 L:ORIG R:ORIG",
    "BELTU2 U:ORIG D:BELT2 L:ORIG R:ORIG",
    "BELTU3 U:ORIG D:BELT3 L:ORIG R:ORIG",
    "POUCH U:grid(2,6) D:ORIG L:ORIG R:ORIG",
    "GOLD U:grid(5,6) D:ORIG L:ORIG R:ORIG",
    "STORE U:grid(9,6) D:ORIG L:ORIG R:ORIG"
};

static int step(dm_bag_nav_state *s, const char *from, int dir) { return dm_bag_nav_step(s, dm_bag_nav_parse(from), dir); }
static void want(dm_bag_nav_state *s, const char *from, int dir, const char *to, const char *why) {
    char b[16]; int got = step(s, from, dir);
    if (got != dm_bag_nav_parse(to)) { printf("FAIL %s：%s %c → %s（期望 %s）\n", why, from, D4[dir], dm_bag_nav_name(got, b, sizeof b), to); fails++; }
}
// 走一步并按实测更新记忆（同宿主每帧比对） | take a step and update the memory as observed (as the host does every frame)
static int go(dm_bag_nav_state *s, int from, int dir) { int to = dm_bag_nav_step(s, from, dir); if (to >= 0) dm_bag_nav_moved(s, from, to); return to >= 0 ? to : from; }

static int sim(void) {
    dm_bag_nav_state s = { 0, 1, 1, -1, -1 }; char ln[128], a[32], b[32], d, n1[16];
    while (fgets(ln, sizeof ln, stdin)) {
        int h, two = !strncmp(ln, "step2 ", 6);
        if (!strncmp(ln, "reset", 5)) { dm_bag_nav_panel(&s, 0); continue; }
        if (sscanf(ln, "held %d", &h) == 1) { s.held = h; continue; }
        if (sscanf(ln + (two ? 6 : 5), "%31s %c %31s", a, &d, b) != 3 || strncmp(ln, "step", 4)) continue;
        const char *p = strchr(D4, d); int dir = p ? (int)(p - D4) : -1, from = dm_bag_nav_parse(a), to = BN_ORIG;
        if (two && dir >= 0 && from >= 0) {   // 同方向连按两下、中间那步没观测：中间按表走 | two quick presses in one direction, the unobserved middle follows the table
            int mid = dm_bag_nav_step(&s, from, dir); if (mid >= 0) { dm_bag_nav_moved(&s, from, mid); from = mid; } else if (mid == BN_ORIG) from = -1;
        }
        if (dir >= 0 && from >= 0) to = dm_bag_nav_step(&s, from, dir);
        printf("%s\n", dm_bag_nav_name(to, n1, sizeof n1)); fflush(stdout);
        int obs = dm_bag_nav_parse(b); if (from >= 0 && obs >= 0) dm_bag_nav_moved(&s, from, obs);
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "--sim")) return sim();
    if (argc > 1 && !strcmp(argv[1], "--rects")) { char b[16]; for (int e = 0; e < BN_N; e++) { dm_bag_nav_rect r = dm_bag_nav_rect_of(e); printf("%s\t%d\t%d\t%d\t%d\n", dm_bag_nav_name(e, b, sizeof b), r.x, r.y, r.w, r.h); } return 0; }
    dm_bag_nav_state s0 = { 0, 1, 1, -1, -1 }, s;
    int rows = 0;
    // 1. 期望表逐格逐向 | 1. the expected table, element by element, direction by direction
    for (unsigned i = 0; i < sizeof K_EXPECT / sizeof K_EXPECT[0]; i++) {
        char f[16], t[4][16]; if (sscanf(K_EXPECT[i], "%15s U:%15s D:%15s L:%15s R:%15s", f, t[0], t[1], t[2], t[3]) != 5) { printf("FAIL 表行写错：%s\n", K_EXPECT[i]); fails++; continue; }
        for (int d = 0; d < 4; d++) { s = s0; want(&s, f, d, t[d], "期望表"); }
        rows++;
    }
    // 表要覆盖每个非网格元素 | the table covers every non-grid element
    for (int e = BN_CELLS; e < BN_N; e++) { char b[16], c[24]; int hit = 0; snprintf(c, sizeof c, "%s ", dm_bag_nav_name(e, b, sizeof b));
        for (unsigned i = 0; i < sizeof K_EXPECT / sizeof K_EXPECT[0]; i++) hit |= !strncmp(K_EXPECT[i], c, strlen(c));
        if (!hit) { printf("FAIL 期望表缺 %s\n", c); fails++; } }

    // 2. B235：第 0 行任意一列按上都不落到页签（空手走表；拿着物品照原版、落到页签由 held_fix 改放，见第 5 组）
    // 2. B235: Up from any column of row 0 never lands on a tab (empty-handed by the table; holding, the original's, with held_fix putting a tab landing back, group 5)
    for (int c = 0; c < BN_COLS; c++) { s = s0; int to = dm_bag_nav_step(&s, dm_bag_nav_cell(c, 0), BN_UP); CHECK(to >= BN_CELLS && !dm_bag_nav_is_tab(to));
        s.held = 1; to = dm_bag_nav_held_fix(&s, dm_bag_nav_cell(c, 0), c < 2 ? BN_TAB0 : BN_TAB1); CHECK(to == dm_bag_nav_cell(c, 0)); }

    // 3. 记忆 ③：进来那一格优先（往下回第 0 行、底排钮按上） | 3. memory item 3: the entry cell wins (down back to row 0, up from a bottom button)
    s = s0; { int e = go(&s, dm_bag_nav_cell(3, 0), BN_UP); CHECK(e == BN_BELT0); CHECK(go(&s, e, BN_DOWN) == dm_bag_nav_cell(3, 0)); }   // 几何会回 (4,0) | geometry alone would give (4,0)
    s = s0; { int e = go(&s, dm_bag_nav_cell(7, 0), BN_UP); CHECK(e == BN_BELT3); CHECK(go(&s, e, BN_DOWN) == dm_bag_nav_cell(7, 0)); }
    s = s0; { int e = go(&s, dm_bag_nav_cell(1, 0), BN_UP); CHECK(e == BN_RARM); CHECK(go(&s, e, BN_DOWN) == dm_bag_nav_cell(1, 0)); }
    s = s0; { int e = go(&s, dm_bag_nav_cell(10, 0), BN_UP); CHECK(e == BN_LARM); CHECK(go(&s, e, BN_DOWN) == dm_bag_nav_cell(10, 0)); }
    s = s0; { int e = go(&s, dm_bag_nav_cell(3, 0), BN_UP); e = go(&s, e, BN_RIGHT); CHECK(e == BN_BELT1); CHECK(go(&s, e, BN_DOWN) == dm_bag_nav_cell(5, 0)); }   // 走开了：几何 | moved on: geometry
    s = s0; { int e = go(&s, dm_bag_nav_cell(2, 0), BN_UP); CHECK(e == BN_GLOVES); CHECK(go(&s, e, BN_DOWN) == dm_bag_nav_cell(2, 0)); }   // 几何会进腰带第 0 格 | geometry alone would enter belt cell 0
    s = s0; { int e = go(&s, dm_bag_nav_cell(6, 0), BN_UP); e = go(&s, e, BN_UP); CHECK(e == BN_BELT2); e = go(&s, BN_TORSO, BN_DOWN); CHECK(e == BN_BELTSLOT); }   // 别处不被旧记忆拉回网格 | elsewhere the old memory does not pull back to the grid
    // 页签：(1,0) ↑ 武器 → 手套 → ↓ 腰带 0 → ← 护身符页签 ← 背包页签 → 护身符页签 → ↓ 正下方 (2,0) | tabs: (1,0) up weapon -> gloves -> down belt 0 -> left charm tab, bag tab -> right charm tab -> down to (2,0) below
    s = s0; { int e = go(&s, dm_bag_nav_cell(1, 0), BN_UP); e = go(&s, e, BN_RIGHT); CHECK(e == BN_GLOVES); e = go(&s, e, BN_DOWN); CHECK(e == BN_BELT0); e = go(&s, e, BN_LEFT); CHECK(e == BN_TAB1);
        e = go(&s, e, BN_LEFT); CHECK(e == BN_TAB0); e = go(&s, e, BN_RIGHT); CHECK(e == BN_TAB1); CHECK(go(&s, e, BN_DOWN) == dm_bag_nav_cell(2, 0)); }
    s = s0; { int e = go(&s, dm_bag_nav_cell(7, 6), BN_DOWN); CHECK(e == BN_GOLD); CHECK(go(&s, e, BN_UP) == dm_bag_nav_cell(7, 6)); }   // 几何会回 (5,6) | geometry alone gives (5,6)
    s = s0; { int e = go(&s, dm_bag_nav_cell(3, 6), BN_DOWN); CHECK(e == BN_POUCH); CHECK(go(&s, e, BN_UP) == dm_bag_nav_cell(3, 6)); }
    s = s0; { int e = go(&s, dm_bag_nav_cell(11, 6), BN_DOWN); CHECK(e == BN_STORE); CHECK(go(&s, e, BN_UP) == dm_bag_nav_cell(11, 6)); }
    s = s0; { go(&s, dm_bag_nav_cell(2, 6), BN_DOWN); CHECK(s.entry_bot == 2); CHECK(dm_bag_nav_step(&s, BN_GOLD, BN_UP) == dm_bag_nav_cell(5, 6)); }   // 百宝囊 → 金币（原版 →）：不回 (2,6) | pouch -> gold (original Right): not back to (2,6)
    // 停稳格 ≠ 进来那格：记忆只认离开网格那一下（按得快时宿主读原版的选中格，不等光标停稳） | settled cell != entry cell: the memory only takes the leaving step (on quick presses the host reads the original's selected cell)
    s = s0; { dm_bag_nav_moved(&s, dm_bag_nav_cell(9, 5), dm_bag_nav_cell(9, 6)); int e = go(&s, dm_bag_nav_cell(9, 6), BN_DOWN); CHECK(e == BN_STORE); CHECK(go(&s, e, BN_UP) == dm_bag_nav_cell(9, 6)); }

    // 逻辑格（B189 ③ Mac 实测：从 (7,6) 下到金币钮，光标斜着扫过 (6,6)）：按下沿推算，出网格那一下不动；连按两下 ↓ 从 (7,5) 记到 (7,6)
    // logical cell (B189 item 3 measured: from (7,6) down to gold the cursor sweeps over (6,6)): advanced on press edges, the leaving press does not move it; two quick Downs from (7,5) record (7,6)
    { int l = dm_bag_nav_cell(7, 5); l = dm_bag_nav_track(l, BN_DOWN); l = dm_bag_nav_track(l, BN_DOWN); CHECK(l == dm_bag_nav_cell(7, 6));
      s = s0; dm_bag_nav_moved(&s, l, BN_GOLD); CHECK(s.entry_bot == 7); CHECK(dm_bag_nav_step(&s, BN_GOLD, BN_UP) == dm_bag_nav_cell(7, 6));
      CHECK(dm_bag_nav_track(dm_bag_nav_cell(0, 0), BN_LEFT) == dm_bag_nav_cell(0, 0)); CHECK(dm_bag_nav_track(dm_bag_nav_cell(11, 3), BN_RIGHT) == dm_bag_nav_cell(11, 3));
      CHECK(dm_bag_nav_track(dm_bag_nav_cell(4, 0), BN_UP) == dm_bag_nav_cell(4, 0)); CHECK(dm_bag_nav_track(BN_GOLD, BN_UP) == BN_GOLD); }

    // 4. 复位 ①：关面板 / 开面板清空记忆（换角色必经关面板） | 4. reset item 1: closing / opening the panel clears the memory (a character switch always closes it)
    s = s0; go(&s, dm_bag_nav_cell(7, 6), BN_DOWN); go(&s, dm_bag_nav_cell(3, 0), BN_UP); CHECK(s.entry_bot == 7 && s.entry_top == 3);
    dm_bag_nav_panel(&s, 0); CHECK(s.entry_bot == -1 && s.entry_top == -1);
    want(&s, "GOLD", BN_UP, "grid(5,6)", "关面板后底排按上不回旧格");
    want(&s, "BELT0", BN_DOWN, "grid(4,0)", "关面板后腰带按下不回旧格");
    s = s0; go(&s, dm_bag_nav_cell(7, 6), BN_DOWN); dm_bag_nav_panel(&s, 1); want(&s, "GOLD", BN_UP, "grid(5,6)", "重开面板后底排按上不回旧格");

    // 5. 拿着物品：页签是死路，腰带第 0 格 ← 不动；第 0 行 ↑ 与装备格 / 武器钮照原版（原版挑放得下的栏位）；原版落到页签时放回原处；其余同空手
    // 5. item in hand: tabs are a dead end, Left on belt cell 0 stays; row 0 Up and the slots / weapon buttons stay the original's (it picks a slot that takes the item);
    //    a landing on a tab is put back where it came from; the rest as empty-handed
    s = s0; s.held = 1; CHECK(dm_bag_nav_step(&s, BN_BELT0, BN_LEFT) == BN_STAY);
    for (int c = 0; c < BN_COLS; c++) CHECK(dm_bag_nav_step(&s, dm_bag_nav_cell(c, 0), BN_UP) == BN_ORIG);
    for (int e = BN_RARM; e <= BN_SWAPL; e++) for (int d = 0; d < 4; d++) CHECK(dm_bag_nav_step(&s, e, d) == BN_ORIG);
    for (int e = 0; e < BN_N; e++) for (int d = 0; d < 4; d++) { dm_bag_nav_state a = s0, b = s0; b.held = 1; int x = dm_bag_nav_step(&a, e, d), y = dm_bag_nav_step(&b, e, d);
        int orig = (e < BN_CELLS && e / BN_COLS == 0 && d == BN_UP) || (e >= BN_RARM && e <= BN_SWAPL);
        if (!(e == BN_BELT0 && d == BN_LEFT) && !orig) CHECK(x == y); CHECK(!dm_bag_nav_is_tab(y) || dm_bag_nav_is_tab(e)); }
    CHECK(dm_bag_nav_held_fix(&s, dm_bag_nav_cell(0, 0), BN_TAB0) == dm_bag_nav_cell(0, 0)); CHECK(dm_bag_nav_held_fix(&s, dm_bag_nav_cell(2, 0), BN_TAB1) == dm_bag_nav_cell(2, 0));
    CHECK(dm_bag_nav_held_fix(&s, BN_BELTU0, BN_TAB1) == BN_BELT0);   // Mac 实测：拿着物品从腰带第二排按左，原版进了护身符页签 | measured: holding, Left from the belt's second row entered the charm tab
    CHECK(dm_bag_nav_held_fix(&s, dm_bag_nav_cell(9, 0), BN_FEET) == -1); CHECK(dm_bag_nav_held_fix(&s, BN_TAB0, BN_TAB1) == -1);   // 只改「落到页签」 | only a landing on a tab is fixed
    CHECK(dm_bag_nav_held_fix(&s0, dm_bag_nav_cell(0, 0), BN_TAB0) == -1);   // 空手不改（空手由宿主自己走表） | not empty-handed (the host walks the table itself then)

    // 6. B239：腰带位 ↓ 进腰带、腰带两排 ↓ 都走得出去 | 6. B239: belt slot Down enters the belt, Down from both belt rows gets out
    s = s0; want(&s, "BELTSLOT", BN_DOWN, "BELT1", "B239"); want(&s, "BELTU1", BN_DOWN, "BELT1", "B239 第二排"); want(&s, "BELT1", BN_DOWN, "grid(5,0)", "B239 腰带下网格");
    for (int i = 0; i < BN_BELT_N; i++) { s = s0; int e = go(&s, BN_BELTU0 + i, BN_DOWN); CHECK(e == BN_BELT0 + i); e = go(&s, e, BN_DOWN); CHECK(e >= 0 && e < BN_CELLS && e / BN_COLS == 0); }

    // 7. 切页键：ZL / ZR 不开对面页签条时归物品栏；L / R 只在商店点按、空手 | 7. page keys: ZL / ZR are the inventory's without an opposite tab bar; L / R only as a tap in a shop, empty-handed
    CHECK(dm_bag_nav_page_key(BN_KEY_ZL, 0, 0, 0, 1) == 0); CHECK(dm_bag_nav_page_key(BN_KEY_ZR, 0, 0, 0, 1) == 1); CHECK(dm_bag_nav_page_key(BN_KEY_ZR, 0, 0, 1, 1) == 1);   // 拿着护身符也能手动切（D1 C8） | also with a charm in hand (D1 C8)
    CHECK(dm_bag_nav_page_key(BN_KEY_ZR, 1, 1, 0, 1) == -1); CHECK(dm_bag_nav_page_key(BN_KEY_ZL, 0, 1, 0, 1) == -1);   // 商店 / 储藏箱的页签条 | the shop's / stash's tab bar
    CHECK(dm_bag_nav_page_key(BN_KEY_L, 1, 1, 0, 1) == 0); CHECK(dm_bag_nav_page_key(BN_KEY_R, 1, 1, 0, 1) == 1);
    CHECK(dm_bag_nav_page_key(BN_KEY_R, 1, 1, 0, 0) == -1); CHECK(dm_bag_nav_page_key(BN_KEY_R, 1, 1, 1, 1) == -1);   // 按住是排序；手上有物品不切 | a hold is sort; not with an item in hand
    CHECK(dm_bag_nav_page_key(BN_KEY_L, 0, 0, 0, 1) == -1); CHECK(dm_bag_nav_page_key(BN_KEY_R, 0, 0, 0, 1) == -1);   // 不开商店：原版页组换页 | no shop: the original page group

    // 8. 可达性（B189：没有走不到的盲区）：从开面板的 (6,1) 只走表里的边，空手能到每个元素（腰带第二排靠原版 ↑，不算）；拿着物品时表里没有一条边进页签
    // 8. reachability (B189: no blind spots): from (6,1) where the panel opens, walking only the table's edges reaches every element empty-handed (the belt's second row comes from
    //    the original Up and is excluded); with an item in hand no table edge enters a tab
    for (int h = 0; h < 2; h++) {
        int seen[BN_N] = { 0 }, q[BN_N], qn = 0; s = s0; s.held = h; q[qn++] = dm_bag_nav_cell(6, 1); seen[q[0]] = 1;
        for (int i = 0; i < qn; i++) for (int d = 0; d < 4; d++) { int t = dm_bag_nav_step(&s, q[i], d); if (t >= 0 && !seen[t]) { seen[t] = 1; q[qn++] = t; } }
        if (!h) for (int e = 0; e < BN_N; e++) { char b[16]; if (dm_bag_nav_is_beltu(e)) continue; if (!seen[e]) { printf("FAIL 走不到 %s\n", dm_bag_nav_name(e, b, sizeof b)); fails++; } }
        if (h) for (int e = BN_TAB0; e <= BN_TAB1; e++) CHECK(!seen[e]);
    }

    if (fails) { printf("t_bag_nav：%d 条失败\n", fails); return 1; }
    printf("✓ t_bag_nav：期望表 %d 行 × 4 向、B235 页签不抢「上」、记忆 ③、复位 ①、拿着物品、B239 腰带、切页键、可达性\n", rows); return 0;
}
