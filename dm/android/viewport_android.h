#pragma once
/* 画面矩形（触控层与呈现共用）的运行状态：窗口、交换链、内容的像素尺寸，以及由**实际**交换链尺寸算出的
   窗口内矩形。没有 Android / Vulkan 依赖，宿主测试可以直接编译这一个文件（dm/android/tests/test_touch_wiring.c）。
   状态由三处写：Java 的 surfaceChanged（窗口像素）、present 每帧（交换链与内容像素）、以及测试。
   Runtime state of the picture rectangle (shared by touch and presentation): the window's, the swapchain's and
   the content's pixel sizes, and the window-space rectangle computed from the *actual* swapchain size. No Android
   or Vulkan dependency, so a host test can compile this one file (dm/android/tests/test_touch_wiring.c). Three
   writers: Java's surfaceChanged (window pixels), present once per frame (swapchain and content pixels), and tests. */
#ifdef __cplusplus
extern "C" {
#endif
/* UI线程：SurfaceView 的像素尺寸（surfaceChanged）。present_native 会改写窗口缓冲尺寸，之后
 * ANativeWindow_getWidth 读不出窗口本身，触控层换算用这个。
 * UI thread: the SurfaceView's pixel size (surfaceChanged). present_native rewrites the window buffer size,
 * after which ANativeWindow_getWidth no longer reports the window; the touch layer converts with this. */
void dm_android_vk_set_window_size(int w,int h);
/* 窗口像素尺寸读回（present 判要不要改窗口缓冲用）。1 = 已知。
 * Read the window's pixel size back (present decides whether to rewrite the window buffer). 1 = known. */
int dm_android_vk_window_size(int *w,int *h);
/* 宿主线程：这一帧交换链的**实际**像素尺寸，以及游戏内容（出图）的像素尺寸。present 在 blit 前写一次。
 * Host thread: the swapchain's *actual* pixel size for this frame and the game content's pixel size; present
 * stores it once per frame, before the blit. */
void dm_android_vk_set_present_geometry(int buffer_w,int buffer_h,int content_w,int content_h);
/* UI线程：画面在窗口里的矩形（窗口像素）。1 = 已知，0 = 还没呈现过 / 尺寸未知（按整个视图算）。
 * UI thread: the picture's rectangle inside the window, in window pixels. 1 = known, 0 = nothing presented
 * yet or a size unknown (use the whole view). */
int dm_android_vk_picture_rect(int *x,int *y,int *w,int *h);
/* UI线程：触控与覆盖层的坐标系（窗口像素）。自适应全屏下是界面矩形（16:9 贴底），其余情况就是画面矩形。
 * 1 = 已知，0 = 还没呈现过（按整个视图算）。
 * UI thread: the coordinate system for touch and the overlay, in window pixels. The UI rectangle (16:9,
 * bottom-anchored) in adaptive fullscreen, the picture rectangle otherwise. 1 = known, 0 = nothing presented
 * yet (use the whole view). */
int dm_android_vk_control_rect(int *x,int *y,int *w,int *h);
/* 宿主线程：界面矩形（画面像素，dm_ui_rect 原样）。present 每帧写一次；不写就是整个画面（旧行为）。
 * Host thread: the UI rectangle in frame pixels, exactly as dm_ui_rect gives it. present stores it once per
 * frame; unset means the whole frame (the old behaviour). */
void dm_android_vk_set_ui_rect(int x,int y,int w,int h);
/* 同上，并在矩形变化时记一行「Viewport:」（呈现初始化把日志接到 vlog；没接就写 stderr）。
 * The same, plus one "Viewport:" line whenever the rectangle changes (presentation connects the log to vlog;
 * without that it goes to stderr). */
int dm_android_vk_viewport(int *x,int *y,int *w,int *h);
/* 呈现初始化时调用一次：把「Viewport:」日志接到 vlog（整行不含换行）。NULL = 写 stderr。
 * Called once from presentation init: route the "Viewport:" line to vlog (one line, no newline). NULL = stderr. */
void dm_android_vk_set_viewport_log(void (*fn)(const char *line));
#ifdef __cplusplus
}
#endif
