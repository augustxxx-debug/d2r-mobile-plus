/* 画面矩形（窗口内）的运行状态与下发：触控层与呈现共用，见 viewport_android.h。
   这里只用实际尺寸：交换链的实际像素（present 每帧写）、窗口像素（Java 的 surfaceChanged）、内容像素。
   于是 setBuffersGeometry 被驱动拒绝时，触控按「那块实际缓冲被拉满窗口」算矩形，与屏幕上所见一致，
   不会出现画面拉满而触控按 letterbox；差额只在日志里如实记下（result= 与 buffers now=）。
   Runtime state and delivery of the picture rectangle (inside the window): shared by touch and presentation, see
   viewport_android.h. Only actual sizes are used here: the swapchain's actual pixels (written by present every
   frame), the window's pixels (Java's surfaceChanged) and the content's pixels. So when the driver refuses
   setBuffersGeometry, touch uses the rectangle of "that actual buffer stretched to the window", which is what is on
   screen; the picture cannot be stretched while touch uses letterbox. The discrepancy is only recorded honestly in
   the log (result= and buffers now=). */
#include <stdatomic.h>
#include <stdio.h>
#include "viewport.h"
#include "viewport_android.h"

static atomic_int window_w = 0, window_h = 0;    /* 窗口像素（Java） | window pixels (Java) */
static atomic_int buffer_w = 0, buffer_h = 0;    /* 交换链实际像素 | the swapchain's actual pixels */
static atomic_int frame_w = 0, frame_h = 0;      /* 内容像素（游戏出图）| content pixels (the game's output) */
/* 界面矩形（画面像素）：present 每帧从 dm_ui_rect 取（游戏「屏幕宽高」那块；不含自适应时就是整个画面）。
   The UI rectangle in frame pixels: present reads it from dm_ui_rect every frame (the game's "screen width /
   height" block; the whole frame when adaptive fullscreen is off). */
static atomic_int ui_x = 0, ui_y = 0, ui_w = 0, ui_h = 0;
static void (*log_hook)(const char *line);

void dm_android_vk_set_viewport_log(void (*fn)(const char *line)) { log_hook = fn; }

void dm_android_vk_set_window_size(int w,int h) {
    atomic_store(&window_w, w > 0 ? w : 0);
    atomic_store(&window_h, h > 0 ? h : 0);
}

int dm_android_vk_window_size(int *w,int *h) {
    int ww = atomic_load(&window_w), wh = atomic_load(&window_h);
    if (w) *w = ww;
    if (h) *h = wh;
    return ww > 0 && wh > 0;
}

void dm_android_vk_set_present_geometry(int bw,int bh,int cw,int ch) {
    atomic_store(&buffer_w, bw > 0 ? bw : 0);
    atomic_store(&buffer_h, bh > 0 ? bh : 0);
    atomic_store(&frame_w, cw > 0 ? cw : 0);
    atomic_store(&frame_h, ch > 0 ? ch : 0);
}

int dm_android_vk_picture_rect(int *vx,int *vy,int *vw,int *vh) {
    int ww = atomic_load(&window_w), wh = atomic_load(&window_h);
    int bw = atomic_load(&buffer_w), bh = atomic_load(&buffer_h);
    int cw = atomic_load(&frame_w), ch = atomic_load(&frame_h);
    if (ww <= 0 || wh <= 0 || cw <= 0 || ch <= 0) return 0;
    if (bw <= 0 || bh <= 0) { bw = ww; bh = wh; }   /* 还没呈现过：缓冲就是窗口 | nothing presented yet: the buffer is the window */
    /* 交换链里的 blit 矩形（与 present 的 blit 目标同出一处），再按合成器的缩放换到窗口。 */
    /* The blit rectangle inside the swapchain (the same one present blits into), then scaled to the window as the
       compositor does. */
    dm_vp_rect r = dm_vp_window_rect(dm_vp_fit(bw, bh, cw, ch), bw, bh, ww, wh);
    *vx = r.x; *vy = r.y; *vw = r.w; *vh = r.h;
    return 1;
}

void dm_android_vk_set_ui_rect(int x,int y,int w,int h) {
    atomic_store(&ui_x, x);
    atomic_store(&ui_y, y);
    atomic_store(&ui_w, w > 0 ? w : 0);
    atomic_store(&ui_h, h > 0 ? h : 0);
}

int dm_android_vk_control_rect(int *vx,int *vy,int *vw,int *vh) {
    /* 触控与覆盖层的坐标系：自适应全屏下是界面矩形，其余情况就是画面矩形（旧行为逐位不变）。
       界面矩形按「画面 → 缓冲 → 窗口」换算（与画面矩形同一条缩放），于是缓冲被驱动拒绝、被合成器
       拉满时，控件与画面仍在一起。游戏还没写出窄的界面高（首帧前）时退回 dm_vp_ui_band 那条 16:9 贴底。
       还没呈现过时返回 0，调用方按整个视图算。
       The coordinate system for touch and the overlay: the UI rectangle in adaptive fullscreen, the picture
       rectangle otherwise (bit-for-bit the old behaviour). The UI rectangle is scaled frame -> buffer -> window
       exactly as the picture rectangle is, so it stays with the picture when the driver refuses the buffer
       geometry and the compositor stretches it. Before the game has written a shorter UI height (the first
       frame) it falls back to the 16:9 band of dm_vp_ui_band. 0 before anything was presented, and the caller
       then uses the whole view. */
    int px = 0, py = 0, pw = 0, ph = 0;
    if (!dm_android_vk_picture_rect(&px, &py, &pw, &ph)) return 0;
    int ww = atomic_load(&window_w), wh = atomic_load(&window_h);
    int bw = atomic_load(&buffer_w), bh = atomic_load(&buffer_h);
    int cw = atomic_load(&frame_w), ch = atomic_load(&frame_h);
    int uw = atomic_load(&ui_w), uh = atomic_load(&ui_h);
    dm_vp_rect pic = {px, py, pw, ph}, r;
    if (uw > 0 && uh > 0 && cw > 0 && ch > 0 && !(uw >= cw && uh >= ch && dm_vp_narrower_than_16_9(ww, wh))) {
        /* 界面矩形（画面像素）→ 缓冲 → 窗口。uw >= cw 且窗口比 16:9 窄 = 游戏还在用整个画面的高（自适应
           刚开、界面高还没写进来），这时用下面那条 16:9 贴底，控件不会先铺满整窗再跳一次。
           The UI rectangle (frame px) -> buffer -> window. uw >= cw with a narrower-than-16:9 window means the
           game still uses the whole frame's height (adaptive just turned on, the UI height is not written yet);
           the 16:9 band below is used then, so the controls do not first spread over the whole window and jump. */
        /* 画面像素 → 缓冲里画面的 blit 矩形（dm_vp_fit，与 present 同一处）→ 窗口。画面铺满缓冲时 blit 矩形就是整块缓冲，
           与原来逐位相同；画面带黑边时控件跟着画面，不铺进黑边（B140 交叉审 P1-1）。
           Frame px -> the picture's blit rectangle inside the buffer (dm_vp_fit, the same one present uses) -> window.
           When the picture fills the buffer the blit rectangle is the whole buffer, bit-for-bit as before; with bars the
           controls stay with the picture instead of spreading into the bars (B140 cross-review P1-1). */
        int sbw = bw > 0 ? bw : ww, sbh = bh > 0 ? bh : wh;
        dm_vp_rect f = {atomic_load(&ui_x), atomic_load(&ui_y), uw, uh}, dst = dm_vp_fit(sbw, sbh, cw, ch);
        dm_vp_rect in_buf = dm_vp_window_rect(f, cw, ch, dst.w, dst.h); in_buf.x += dst.x; in_buf.y += dst.y;
        r = dm_vp_window_rect(in_buf, sbw, sbh, ww, wh);
    } else {
        r = dm_vp_control_rect(pic, ww, wh);
    }
    *vx = r.x; *vy = r.y; *vw = r.w; *vh = r.h;
    return 1;
}

int dm_android_vk_viewport(int *vx,int *vy,int *vw,int *vh) {
    if (!dm_android_vk_picture_rect(vx, vy, vw, vh)) return 0;
    /* 诊断：矩形一变记一行（折叠内外屏切换、旋转、渲染分辨率档变化都能在日志里对上）。
       Diagnostic: one line per change (fold switch, rotation and render-resolution steps then line up in the log). */
    static atomic_int lx = -1, ly = -1, lw = -1, lh = -1;
    int px = atomic_load(&lx), py = atomic_load(&ly), pw = atomic_load(&lw), ph = atomic_load(&lh);
    if (px != *vx || py != *vy || pw != *vw || ph != *vh) {
        atomic_store(&lx, *vx); atomic_store(&ly, *vy); atomic_store(&lw, *vw); atomic_store(&lh, *vh);
        int ww = atomic_load(&window_w), wh = atomic_load(&window_h);
        int bw = atomic_load(&buffer_w), bh = atomic_load(&buffer_h);
        int cw = atomic_load(&frame_w), ch = atomic_load(&frame_h);
        int fills = *vx == 0 && *vy == 0 && *vw == ww && *vh == wh;
        int cx = 0, cy = 0, cwd = 0, chd = 0;
        dm_android_vk_control_rect(&cx, &cy, &cwd, &chd);
        char line[320];
        snprintf(line, sizeof line,
                 "Viewport: picture %dx%d in window %dx%d, buffer %dx%d -> x=%d y=%d w=%d h=%d (%s); controls x=%d y=%d w=%d h=%d",
                 cw, ch, ww, wh, bw, bh, *vx, *vy, *vw, *vh, fills ? "display fills" : "letterboxed", cx, cy, cwd, chd);
        if (log_hook) log_hook(line); else { fputs(line, stderr); fputc('\n', stderr); }
    }
    return 1;
}
