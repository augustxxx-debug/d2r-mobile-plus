#pragma once
#include <stddef.h>
#include <android/native_window.h>
#ifdef __cplusplus
extern "C" {
#endif
/* UI线程：保留窗口；NULL暂停呈现。只更新窗口快照，不调用Vulkan或等待GPU。
 * UI thread: retains window; NULL suspends presentation. Only updates the window
 * snapshot; never calls Vulkan or waits for the GPU. */
void dm_android_vk_set_window(ANativeWindow *window);
/* UI线程：SurfaceView 的像素尺寸（surfaceChanged）。present_native 会改写窗口缓冲尺寸，之后
 * ANativeWindow_getWidth 读不出窗口本身，触控层换算用这个。
 * UI thread: the SurfaceView's pixel size (surfaceChanged). present_native rewrites the window buffer size,
 * after which ANativeWindow_getWidth no longer reports the window; the touch layer converts with this. */
void dm_android_vk_set_window_size(int w,int h);
/* UI线程：画面在窗口里的矩形（窗口像素）。1 = 已知，0 = 还没呈现过 / 尺寸未知（按整个视图算）。
 * UI thread: the picture's rectangle inside the window, in window pixels. 1 = known, 0 = nothing presented
 * yet or a size unknown (use the whole view). */
int dm_android_vk_viewport(int *x,int *y,int *w,int *h);
/* 宿主线程：在dm_main前初始化一次；Surface暂时撤回时等待恢复。
 * Host thread: before dm_main, after set_window. Waits across surface withdrawal
 * and initializes genuine Vulkan replay once.
 * shader_directory remains external, is copied internally, and must hold .xgs. */
int dm_android_vk_initialize(const char *shader_directory,char *error,size_t bytes);
int dm_android_vk_has_window(void);
#ifdef __cplusplus
}
#endif
