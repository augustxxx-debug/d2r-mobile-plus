/* 画面在窗口里的位置（viewport）：呈现 blit 的目标矩形。
   The picture's rectangle inside the window: the present blit's destination rectangle.
   纯函数、无平台依赖，取整与原来的呈现代码逐位相同（scale = min(窗口÷内容)，尺寸截断、偏移整除），
   所以把它抽出来后屏幕上每一像素都不变；触控那条路仍按视图宽高归一（见 tests/test_viewport.c 的反例）。
   Pure functions with no platform dependency; the rounding is bit-for-bit the same as the present code
   it was extracted from (scale = min(window/content), sizes truncated, offsets integer-divided), so the
   picture does not move by one pixel. The touch path still normalizes by the view size (see the
   counter-example in tests/test_viewport.c). */
#pragma once

typedef struct { int x, y, w, h; } dm_vp_rect;

static inline dm_vp_rect dm_vp_fit(int win_w, int win_h, int content_w, int content_h) {
    dm_vp_rect r = {0, 0, win_w > 0 ? win_w : 0, win_h > 0 ? win_h : 0};
    if (win_w <= 0 || win_h <= 0 || content_w <= 0 || content_h <= 0) return r;
    double sx = (double)win_w / content_w, sy = (double)win_h / content_h, s = sx < sy ? sx : sy;
    int w = (int)(content_w * s), h = (int)(content_h * s);
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (w > win_w) w = win_w;
    if (h > win_h) h = win_h;
    r.w = w; r.h = h; r.x = (win_w - w) / 2; r.y = (win_h - h) / 2;
    return r;
}

static inline int dm_vp_inside(dm_vp_rect r, double x, double y) {
    return r.w > 0 && r.h > 0 && x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}
