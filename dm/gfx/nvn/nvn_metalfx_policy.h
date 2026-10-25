// 超分初始比例策略；自动档再交由实际帧间隔控制器降档，界面尺寸不参与缩放。
// Initial upscale ratios; Auto also uses the real frame-interval controller, without scaling UI dimensions.
#pragma once
static inline float nvn_metalfx_scale(int mode, unsigned pixels, int heat, int low_power) {
    if (mode < 0 || mode >= 4) return 0;
    if (mode == 1) return .60f;
    if (mode == 2) return .75f;
    if (mode == 3) return .85f;
    if (low_power || heat >= 2) return .65f;
    return pixels > 4000000 ? .70f : .80f;
}
