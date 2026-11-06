// B036 回归：面板开着 → 切模式（触屏 → 手柄）→ 布局重载 → 焦点恢复到网格里的合法格子，方向键能继续移动。
// 纯逻辑测试（秒级，不碰游戏）：切换事务用 dm_modetx.h 跑一遍真实的开始 / 两次重建 / 完成时序；
// 焦点恢复策略用 dm_padfocus.h。网格用屏幕命中矩形建模，格子编号按同一套行列数算——相对位置换算回来
// 必须落在同一格；记不到光标位置时落第一格。
// B036 regression: panel open -> mode switch (touch -> controller) -> layout reload -> the focus is restored to a
// valid cell of the grid and the D-pad can keep moving. Pure logic test (takes seconds, does not touch the game):
// the switch transaction runs the real begin / two rebuilds / done sequence through dm_modetx.h, and the focus
// restore policy comes from dm_padfocus.h. The grid is modelled as a screen hit rect, and cells are numbered with
// the same row / column count -- the relative position must map back to the same cell; when no cursor position was
// recorded it falls on the first cell.
#include <stdio.h>
#include "../dm_modetx.h"
#include "../dm_padfocus.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("  ✗ "); printf(__VA_ARGS__); printf("\n"); } } while (0)

typedef struct { int x0, y0, x1, y1, cols, rows; } grid;

// 屏幕点 → 格子号（-1 = 不在网格里）| screen point -> cell index (-1 = outside the grid)
static int cell_at(const grid *g, int x, int y) {
    if (x < g->x0 || x > g->x1 || y < g->y0 || y > g->y1) return -1;
    int cx = (x - g->x0) * g->cols / (g->x1 - g->x0 + 1), cy = (y - g->y0) * g->rows / (g->y1 - g->y0 + 1);
    if (cx >= g->cols) cx = g->cols - 1;
    if (cy >= g->rows) cy = g->rows - 1;
    return cy * g->cols + cx;
}
// 方向键移动一格（-1 = 移不出去 / 不在网格里）| move one cell with the D-pad (-1 = cannot move / outside the grid)
static int move_cell(const grid *g, int cell, int dx, int dy) {
    if (cell < 0 || cell >= g->cols * g->rows) return -1;
    int cx = cell % g->cols + dx, cy = cell / g->cols + dy;
    if (cx < 0 || cx >= g->cols || cy < 0 || cy >= g->rows) return -1;
    return cy * g->cols + cx;
}
static int movable(const grid *g, int cell) {
    return move_cell(g, cell, 1, 0) >= 0 || move_cell(g, cell, -1, 0) >= 0 || move_cell(g, cell, 0, 1) >= 0 || move_cell(g, cell, 0, -1) >= 0;
}

int main(void) {
    {   // 正常走法：商人货架开着，光标在第 (5,3) 格；切到手柄、两次重建后落回同一格，方向键仍可移动 | normal path: vendor shelf open with the cursor on cell (5,3); switch to controller, after both rebuilds it lands on the same cell and the D-pad can still move
        dm_modetx t = {0}; uint64_t f = 100; int tips = 7; dm_padfocus_mem mem;
        grid pre = { 80, 120, 479, 319, 10, 4 }, post = { 40, 60, 439, 259, 10, 4 };
        int cx = pre.x0 + (int)(0.55 * (pre.x1 - pre.x0)), cy = pre.y0 + (int)(0.80 * (pre.y1 - pre.y0)), pre_cell = cell_at(&pre, cx, cy);
        CHECK(pre_cell == 3 * pre.cols + 5, "前置：光标应落在第 (5,3) 格（实际 %d）", pre_cell);
        CHECK(dm_modetx_step(&t, f, 1, 0, tips) == DM_TXA_BEGIN, "触屏→手柄事务应开始");
        dm_padfocus_capture(&mem, 0, 1, cell_at(&pre, cx, cy) >= 0, (double)(cx - pre.x0) / (pre.x1 - pre.x0), (double)(cy - pre.y0) / (pre.y1 - pre.y0));   // 事务开始那一帧记下 | recorded on the begin frame
        CHECK(dm_modetx_step(&t, f + 6, 1, 0, tips + 1) == DM_TXA_NONE, "第一次重建：回读信号刚动，等稳定");
        CHECK(dm_modetx_step(&t, f + 10, 1, 0, tips + 1) == DM_TXA_REPORT_BACK, "第一次重建完成 → 报回原运行模式");
        CHECK(dm_modetx_step(&t, f + 11, 1, 0, tips + 2) == DM_TXA_NONE, "第二次重建：回读信号刚动，等稳定");
        CHECK(dm_modetx_step(&t, f + 15, 1, 0, tips + 2) == DM_TXA_DONE, "第二次重建完成 → 事务完成");
        int x = 0, y = 0;
        CHECK(dm_padfocus_restore(&mem, 0, 1, post.x0, post.y0, post.x1, post.y1, &x, &y) == 1, "重载后应给出恢复点");
        CHECK(x > post.x0 && x < post.x1 && y > post.y0 && y < post.y1, "恢复点应在网格矩形里面（%d,%d）", x, y);
        CHECK(cell_at(&post, x, y) == pre_cell, "应恢复到切换前那一格（期望 %d，实际 %d）", pre_cell, cell_at(&post, x, y));
        CHECK(movable(&post, cell_at(&post, x, y)), "恢复后方向键应能移动"); }
    {   // 触屏最后一次不在网格上：落第一格，方向键能移动 | the last touch was not on the grid: fall on the first cell, the D-pad can move
        dm_padfocus_mem mem; grid post = { 100, 200, 499, 399, 8, 4 }; int x = 0, y = 0;
        dm_padfocus_capture(&mem, 0, 1, 0, 0.0, 0.0);
        CHECK(dm_padfocus_restore(&mem, 0, 1, post.x0, post.y0, post.x1, post.y1, &x, &y) == 1, "没有位置也应落第一格");
        CHECK(cell_at(&post, x, y) == 0, "应落在第 0 格（实际 %d）", cell_at(&post, x, y));
        CHECK(movable(&post, 0), "第一格方向键应能移动"); }
    {   // 两套布局格子像素大小不同：同一相对位置仍落同一格 | different cell pixel sizes in the two layouts: the same relative position still lands on the same cell
        dm_padfocus_mem mem; grid pre = { 10, 20, 309, 219, 6, 4 }, post = { 500, 400, 1099, 799, 6, 4 }; int x = 0, y = 0;
        int cx = 10 + 200, cy = 20 + 60, pre_cell = cell_at(&pre, cx, cy);
        dm_padfocus_capture(&mem, 0, 1, 1, (double)(cx - pre.x0) / (pre.x1 - pre.x0), (double)(cy - pre.y0) / (pre.y1 - pre.y0));
        CHECK(dm_padfocus_restore(&mem, 0, 1, post.x0, post.y0, post.x1, post.y1, &x, &y) == 1, "尺寸变了也应恢复");
        CHECK(cell_at(&post, x, y) == pre_cell, "尺寸变了仍应落同一格（期望 %d，实际 %d）", pre_cell, cell_at(&post, x, y)); }
    {   // 面板关了 / 换了面板：不硬塞焦点 | panel closed / another panel: do not force the focus
        dm_padfocus_mem mem; int x = 0, y = 0;
        dm_padfocus_capture(&mem, 0, 1, 1, 0.5, 0.5);
        CHECK(dm_padfocus_restore(&mem, 0, 0, 10, 20, 109, 219, &x, &y) == 0, "面板关了不该恢复");
        CHECK(dm_padfocus_restore(&mem, 1, 1, 10, 20, 109, 219, &x, &y) == 0, "换了面板不该恢复");
        dm_padfocus_capture(&mem, -1, 0, 0, 0.0, 0.0);
        CHECK(dm_padfocus_restore(&mem, 0, 1, 10, 20, 109, 219, &x, &y) == 0, "没记下面板不该恢复"); }
    {   // 光标在网格边缘：恢复点夹在矩形里面 | cursor on the grid edge: the restore point is clamped inside the rect
        dm_padfocus_mem mem; grid g = { 30, 40, 229, 239, 4, 4 }; int x = 0, y = 0;
        dm_padfocus_capture(&mem, 0, 1, 1, 1.0, 1.0);
        CHECK(dm_padfocus_restore(&mem, 0, 1, g.x0, g.y0, g.x1, g.y1, &x, &y) == 1, "边缘也应恢复");
        CHECK(cell_at(&g, x, y) >= 0 && x < g.x1 && y < g.y1, "恢复点应在矩形里面（%d,%d）", x, y); }
    printf("t_b036: %d 项，%d 失败\n", checks, fails); return fails != 0;
}
