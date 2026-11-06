/* 画面在窗口里的矩形（viewport）：显示与触控共用这一份换算。
   The picture's rectangle inside the window: the one conversion presentation and touch share.
   纯函数、无平台依赖。呈现（vulkan_present.cpp 的 blit 目标矩形）与安卓触控层（把窗口点换回画面点）
   都调这里，于是画面比例对不上窗口（折叠屏内屏近 4:3 而游戏输出 16:9 之类）时，按钮画在哪、按下算在哪
   不会各用一套尺寸；窗口旋转、内外屏切换、像素不整除也都走同一条取整。
   Pure functions with no platform dependency. Presentation (the blit destination rectangle in
   vulkan_present.cpp) and the Android touch layer (window point back to picture point) both call these,
   so when the picture's aspect does not match the window (a fold inner screen near 4:3 while the game
   outputs 16:9) where a button is drawn and where a press counts cannot use two different sizes; window
   rotation, inner/outer screen switches and sizes that do not divide evenly all take the same rounding. */
#pragma once

typedef struct { int x, y, w, h; } dm_vp_rect;

/* 按内容比例放进窗口、居中；取整与原先的呈现代码逐位相同（scale = min(窗口÷内容)，尺寸截断、偏移整除）。
   Fit the content into the window keeping its aspect, centred; the rounding is bit-for-bit the one the
   present code used (scale = min(window/content), sizes truncated, offsets integer-divided). */
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

/* 窗口点 → 画面矩形内的归一化坐标（0..1）。返回 1 = 在画面里，0 = 落在黑边上；两种都夹进 [0,1]，
   于是「黑边上的按下」可以由调用方丢掉（返回 0），拖出画面的手指仍落在最近的边上。
   window point -> normalized coordinates inside the picture rectangle (0..1). 1 = inside the picture,
   0 = on a black bar; both clamp into [0,1], so the caller can drop a press on the bars (return 0) while a
   finger dragged out of the picture still lands on the nearest edge. */
static inline int dm_vp_map(dm_vp_rect r, double x, double y, double *u, double *v) {
    int inside = dm_vp_inside(r, x, y);
    *u = r.w > 0 ? (x - r.x) / r.w : 0;
    *v = r.h > 0 ? (y - r.y) / r.h : 0;
    if (*u < 0) *u = 0;
    if (*u > 1) *u = 1;
    if (*v < 0) *v = 0;
    if (*v > 1) *v = 1;
    return inside;
}

/* 交换链（窗口缓冲）里的矩形 → 窗口里的矩形。合成器把缓冲按窗口尺寸缩放，所以这一段必须由「实际的缓冲像素」
   算，不能由「请求的缓冲像素」算：setBuffersGeometry 被驱动拒绝时缓冲仍是旧尺寸，被拉满窗口的正是那块缓冲。
   buf_w == win_w && buf_h == win_h 时逐位恒等（普通手机、缓冲本来就是窗口尺寸的情况不变）。
   A rectangle inside the swapchain (window buffer) -> the rectangle inside the window. The compositor scales the
   buffer to the window, so this must start from the buffer's *actual* pixels, never the requested ones: when the
   driver refuses setBuffersGeometry the buffer keeps its old size and that buffer is what gets stretched to the
   window. With buf == window this is the identity (a phone whose buffer already is the window is unchanged). */
static inline dm_vp_rect dm_vp_window_rect(dm_vp_rect buf, int buf_w, int buf_h, int win_w, int win_h) {
    dm_vp_rect r = {0, 0, win_w > 0 ? win_w : 0, win_h > 0 ? win_h : 0};
    if (buf_w <= 0 || buf_h <= 0 || win_w <= 0 || win_h <= 0) return r;
    double sx = (double)win_w / buf_w, sy = (double)win_h / buf_h;
    int x = (int)(buf.x * sx), y = (int)(buf.y * sy);
    int w = (int)(buf.w * sx + 0.5), h = (int)(buf.h * sy + 0.5);
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (w > win_w - x) w = win_w - x;
    if (h > win_h - y) h = win_h - y;
    if (w < 1 || h < 1) { r.w = win_w; r.h = win_h; return r; }
    r.x = x; r.y = y; r.w = w; r.h = h;
    return r;
}

/* 两边的比例是否算相同（相对差 1% 以内）。呈现那边只在相同时把窗口缓冲设成游戏出图尺寸、由显示硬件
   铺满（present_native）；不同就按画面矩形 blit 并留黑边，触控按同一个矩形换算。
   Whether the two aspects count as the same (within 1% relative). Presentation takes the window buffer at
   the game's output size and lets the display hardware fill it (present_native) only when they match;
   otherwise it blits into the picture rectangle with bars, and touch uses that same rectangle. */
static inline int dm_vp_same_aspect(int a_w, int a_h, int b_w, int b_h) {
    if (a_w <= 0 || a_h <= 0 || b_w <= 0 || b_h <= 0) return 1;   /* 尺寸还不知道：不改变现在的做法 | size unknown: keep the current behaviour */
    long long l = (long long)a_w * b_h, r = (long long)b_w * a_h;
    long long d = l > r ? l - r : r - l;
    return d * 100 <= r;
}

/* 要不要改写窗口缓冲尺寸（present_native 铺满 / 退回窗口尺寸），以及写成多少。present 每帧调用一次；
   native_on = 窗口缓冲**实际**就是游戏出图尺寸（由 ANativeWindow_getWidth/Height 读回，不按下一次请求记账）。
   返回 1 = 调 ANativeWindow_setBuffersGeometry(*req_w,*req_h,0)；req 为 0,0 表示回到窗口尺寸。
   请求被驱动拒绝时这里不改任何「以为已经生效」的状态：下一帧读回的实际尺寸仍是旧值，于是继续请求；
   呈现与触控按实际尺寸走，不会出现「画面拉满而触控按 letterbox」。
   Whether to rewrite the window buffer size (present_native fill, or back to the window size) and to what.
   present calls it once per frame; native_on = the window buffer's *actual* size is the game's output (read back
   with ANativeWindow_getWidth/Height, never from the previous request). 1 = call ANativeWindow_setBuffersGeometry
   with the request; 0,0 means back to the window size. A refused request changes no "assume applied" state here:
   the next frame reads the old geometry back and asks again, and presentation and touch both follow the actual
   size, so "picture stretched while touch uses letterbox" cannot arise. */
static inline int dm_vp_geometry_request(int present_native,int win_w,int win_h,int content_w,int content_h,int native_on,int *req_w,int *req_h) {
    *req_w = 0; *req_h = 0;
    if (content_w <= 0 || content_h <= 0) return 0;
    if (present_native && dm_vp_same_aspect(win_w,win_h,content_w,content_h)) {
        if (native_on) return 0;
        *req_w = content_w; *req_h = content_h;
        return 1;
    }
    return native_on ? 1 : 0;   /* 请求 (0,0)：回到窗口尺寸 | request (0,0): back to the window size */
}
