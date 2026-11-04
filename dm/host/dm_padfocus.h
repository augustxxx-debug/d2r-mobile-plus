// 手柄模式下带网格面板的手柄焦点恢复策略（纯逻辑，不碰游戏内存；dm_padmode.c 用，tests/t_b036.c 测）。
// 背景（B036）：手柄 / 触屏切换事务会重载整套布局，旧控件全部释放、新控件重建；本来就开着的面板不会重走
// 开页时的缺省焦点，焦点管理器的焦点控件（[界面根+0xd0]+0x190）留空，方向键 / 左摇杆没有起点，
// 商人货架、交易、储物箱、赫拉迪克方块这些带网格的页面都动不了（触屏切手柄后最明显）。
// 做法：切换事务开始前记下「开着网格面板里，光标落在网格内的相对位置」；两次重建完成后按同一个相对位置
// 换算新网格里的屏幕点，交给原版悬停刷新（dm_ui_hover_now）把焦点落到那一格。记不到位置（最后一下触屏
// 不在网格上）就落第一格：网格左上角往里 1 像素。相对位置不依赖两套布局的格子像素大小，同一行 / 列数下
// 落回同一格。
// Policy for restoring the controller focus of grid panels in controller mode (pure logic, does not touch game
// memory; used by dm_padmode.c, tested by tests/t_b036.c).
// Background (B036): the controller / touch switch transaction reloads the whole layout, freeing every old widget
// and rebuilding new ones. A panel that was already open does not replay the default focus it gets when opened,
// so the focus manager's focused widget ([UI root+0xd0]+0x190) stays empty, the D-pad / left stick has no starting
// point, and grid pages (vendor shelf, trade, stash, Horadric Cube) cannot move. This is most visible after
// switching touch -> controller.
// Approach: before the switch transaction starts, remember the cursor's position relative to the grid hit rect of
// the open grid panel; after both rebuilds, map the same relative position onto the new grid and hand that screen
// point to the original hover refresh (dm_ui_hover_now) to put the focus on that cell. If no position was recorded
// (the last touch was not on the grid), fall back to the first cell: 1 pixel inside the grid's top-left corner.
// The relative position does not depend on the two layouts' cell pixel sizes, so with the same row / column count
// it lands on the same cell.
#ifndef DM_PADFOCUS_H
#define DM_PADFOCUS_H

typedef struct {
    int panel;      // 面板序号（调用方的登记表下标）；-1 = 没有要恢复的 | panel index (the caller's table); -1 = nothing to restore
    int has_cell;   // 1 = 切换前光标在网格内，u / v 是那一格 | 1 = the cursor was inside the grid before the switch, u / v are that cell
    double u, v;    // 网格命中矩形内的归一化位置 [0,1] | normalized position inside the grid hit rect [0,1]
} dm_padfocus_mem;

static inline double dm_padfocus_frac(double v) { return v < 0.0 ? 0.0 : v > 1.0 ? 1.0 : v; }

// 切换开始前调用（记不下 / 不在网格里都清成「落第一格」）| called before the switch (nothing recorded / not on the grid both mean "first cell")
static inline void dm_padfocus_capture(dm_padfocus_mem *m, int panel, int grid_open, int cursor_in_grid, double u, double v) {
    if (!m) return;
    m->panel = -1; m->has_cell = 0; m->u = m->v = 0.0;
    if (panel < 0 || !grid_open) return;
    m->panel = panel;
    if (cursor_in_grid) { m->has_cell = 1; m->u = dm_padfocus_frac(u); m->v = dm_padfocus_frac(v); }
}

// 重载完成后调用：面板还开着、网格还在 → 返回 1 并给出网格内的屏幕点（坐标已夹在矩形里面，至少落在第一格）；
// 面板关了 / 没有网格 → 返回 0，调用方什么都不做。x0..y1 是本次新网格的屏幕命中矩形。
// Called after the reload: panel still open and the grid still there -> returns 1 and gives a screen point inside
// the grid (clamped inside the rect, falling on the first cell at worst); panel closed / no grid -> returns 0 and the
// caller does nothing. x0..y1 is the screen hit rect of the new grid.
static inline int dm_padfocus_restore(const dm_padfocus_mem *m, int panel_now, int grid_open, int x0, int y0, int x1, int y1, int *x, int *y) {
    if (!m || m->panel < 0 || panel_now != m->panel || !grid_open || x1 <= x0 || y1 <= y0) return 0;
    double u = m->has_cell ? m->u : 0.0, v = m->has_cell ? m->v : 0.0;
    int px = x0 + (int)(u * (double)(x1 - x0) + 0.5), py = y0 + (int)(v * (double)(y1 - y0) + 0.5);
    if (px <= x0) px = x0 + 1;   // 第一格：左上角往里 1 像素，别停在命中矩形的边上 | first cell: 1 pixel inside the top-left, not on the edge of the hit rect
    if (py <= y0) py = y0 + 1;
    if (px >= x1) px = x1 - 1;
    if (py >= y1) py = y1 - 1;
    if (x) *x = px;
    if (y) *y = py;
    return 1;
}

#endif
