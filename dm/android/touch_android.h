#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* UI线程仅排队；step必须挂在dm_pad_hook游戏采样线程。 | UI only queues; step must run on the guest sampling thread through dm_pad_hook. */
void dm_android_touch_resize(float width_dp, float height_dp, float left_dp, float right_dp, float bottom_dp);
/* 画面矩形（dp）与界面矩形在画面里的偏移（dp）：世界点选（点怪、点箱子、点地走路）要真实画面归一化坐标，
   而界面自己的归一化坐标是相对界面矩形的（自适应全屏下两者不是一块，见 dm/android/viewport.h）。
   pic_w / pic_h <= 0 = 还没读到（界面矩形就是画面，旧行为）。
   The picture rectangle in dp and the UI rectangle's offset inside it, in dp: world taps (monsters, chests,
   walk-to-ground) need normalized *picture* coordinates while the UI's own normalized coordinates are relative to
   the UI rectangle (in adaptive fullscreen those are two different rectangles, see dm/android/viewport.h).
   pic_w / pic_h <= 0 = not read yet (the UI rectangle is the picture, the old behaviour). */
void dm_android_touch_picture(float pic_w_dp, float pic_h_dp, float ctrl_x_dp, float ctrl_y_dp);
/* 挖孔矩形（控件坐标 dp，每个 x0,y0,x1,y1，最多 4 个）与底部手势条高（dp）；改了就重排控件 | cutout rectangles (control dp, x0,y0,x1,y1 each, up to 4) and the bottom gesture bar (dp); a change re-lays the controls */
void dm_android_touch_cutouts(const float *xyxy, int n, float gesture_bottom_dp);
void dm_android_touch_tablet(int tablet);
/* phase: 0 down, 1 move, 2 up, 3 cancel; slot: stable 0..15; coordinates in dp. | 阶段：按下/移动/抬起/取消；槽固定0..15；坐标dp。 */
void dm_android_touch_event(int phase, int slot, float x, float y);
void dm_android_touch_focus(int active);
void dm_android_touch_uimsg(const char *cat, const char *act, const char *arg);
void dm_android_touch_step(uint64_t guest_now, uint64_t *buttons, int32_t *lx, int32_t *ly, int32_t *rx, int32_t *ry);
/* 绘图快照：header[width,height,navigation,skill_set]，23个[x,y,r,enabled,pressed]；12键、摇杆、换组、难度及8选择格。 | Drawing snapshot: header followed by 23 records (12 keys, stick, set switch, difficulty, 8 choices). */
/* 后接六个技能图标记录（状态、图集、裁切矩形、副手/充能、次数）和16个HUD数值。
   Append six icon records (status, atlas, crop, secondary/charge flags, charges) and 16 HUD values. */
enum { DM_ANDROID_TOUCH_GLYPHS = 4 + 23 * 5, DM_ANDROID_TOUCH_HUD = DM_ANDROID_TOUCH_GLYPHS + 6 * 8,
       DM_ANDROID_TOUCH_DRAW_FLOATS = DM_ANDROID_TOUCH_HUD + 18 };
void dm_android_touch_layout(float out[DM_ANDROID_TOUCH_DRAW_FLOATS]);
#ifdef __cplusplus
}
#endif
