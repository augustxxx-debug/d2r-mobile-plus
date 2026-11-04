// 「存放」按钮的手柄焦点进出（纯函数，不碰游戏内存）：网格最下一行判定、按钮中心、回网格的点。
// 手柄模式下按钮本来就在屏上（dm_pouch.c ui_pump 按 store_btn 摆位，两种输入模式叠同一份布局），只是原版焦点导航走不到我们加的控件；
// 这里只算坐标，真正接管按键、移动原版光标在 dm_pouch.c 的 store_pad_nav()。
// Controller focus in / out of the 「存放」(Deposit) button (pure functions, no game memory): last-row test, button centre, and the
// point to return to in the grid. In controller mode the button is already on screen (placed by store_btn in ui_pump; both input
// modes stack the same layout) and only the original focus navigation cannot reach widgets we add. This header only computes
// coordinates; taking the keys over and moving the original cursor is store_pad_nav() in dm_pouch.c.
#pragma once

#define DM_STORE_CELL_UNITS 98   // 布局单位 / 格（同 dm_bag_layout.h：1 格 = 98）| layout units per cell (same as dm_bag_layout.h: 1 cell = 98)

typedef struct { int x, y, w, h; } dm_store_rect;                    // 屏幕像素矩形 | screen-pixel rect
typedef struct { dm_store_rect r; int cell, rows; } dm_store_grid;   // 网格屏幕矩形、格距（像素）、行数 | grid screen rect, cell pitch (px), row count

static inline int dm_store_in_rect(const dm_store_rect *r, int x, int y) {
    return r && r->w > 0 && r->h > 0 && x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h;
}

// 光标所在行（0 起；不在网格里 -1）| Row under the cursor (0-based; -1 when outside the grid)
static inline int dm_store_row(const dm_store_grid *g, int cy) {
    if (!g || g->cell <= 0 || g->rows <= 0 || cy < g->r.y) return -1;
    int row = (cy - g->r.y) / g->cell;
    return row >= 0 && row < g->rows ? row : -1;
}

static inline int dm_store_btn_covers_x(const dm_store_rect *b, int x) {
    return b && b->w > 0 && x >= b->x && x < b->x + b->w;
}

static inline void dm_store_btn_center(const dm_store_rect *b, int *x, int *y) {
    *x = b->x + b->w / 2; *y = b->y + b->h / 2;
}

// 网格最下一行、光标 x 在按钮水平范围 → 该接管 ↓，*x / *y = 按钮中心；否则 0（↓ 照常交原版）
// On the grid's last row with the cursor x within the button's horizontal range -> take over Down; *x / *y = button centre;
// otherwise 0 (Down stays with the original).
static inline int dm_store_down_target(const dm_store_grid *g, const dm_store_rect *b, int cx, int cy, int *x, int *y) {
    int row = dm_store_row(g, cy);
    if (row < 0 || row != g->rows - 1 || !dm_store_btn_covers_x(b, cx)) return 0;
    dm_store_btn_center(b, x, y);
    return 1;
}

// 从按钮按 ↑ 回网格：有记录且记录点在网格里 → 回那一格（进来前那一格优先）；否则回按钮正上方那格，
// 列按按钮中心取、夹在网格内。| Up from the button: with a valid record return to that cell (the cell we came from wins);
// otherwise the cell above the button, its column taken from the button centre and clamped inside the grid.
static inline void dm_store_back(const dm_store_grid *g, const dm_store_rect *b, int has_prev, int px, int py, int *x, int *y) {
    if (has_prev && dm_store_in_rect(&g->r, px, py)) { *x = px; *y = py; return; }
    int cell = g->cell > 0 ? g->cell : 1, cols = g->r.w / cell; if (cols < 1) cols = 1;
    int bx, by; dm_store_btn_center(b, &bx, &by); (void)by;
    int col = (bx - g->r.x) / cell; if (col < 0) col = 0; if (col > cols - 1) col = cols - 1;
    int row = g->rows > 0 ? g->rows - 1 : 0;
    *x = g->r.x + col * cell + cell / 2;
    *y = g->r.y + row * cell + cell / 2;
    if (*x < g->r.x) *x = g->r.x; if (*x >= g->r.x + g->r.w) *x = g->r.x + g->r.w - 1;
    if (*y < g->r.y) *y = g->r.y; if (*y >= g->r.y + g->r.h) *y = g->r.y + g->r.h - 1;
}
