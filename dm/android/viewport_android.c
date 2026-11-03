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
        char line[256];
        snprintf(line, sizeof line,
                 "Viewport: picture %dx%d in window %dx%d, buffer %dx%d -> x=%d y=%d w=%d h=%d (%s)",
                 cw, ch, ww, wh, bw, bh, *vx, *vy, *vw, *vh, fills ? "display fills" : "letterboxed");
        if (log_hook) log_hook(line); else { fputs(line, stderr); fputc('\n', stderr); }
    }
    return 1;
}
