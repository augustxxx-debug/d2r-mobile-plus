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

// Render-thread state. Reductions are prompt; recovery needs 60 seconds of
// uninterrupted healthy observations. Long gaps (background/loading) do not
// count as evidence of recovery. Manual profiles bypass this controller.
#include <stdint.h>
typedef struct {
    float current, candidate;
    uint64_t healthy_since, last_sample;
    int recovering;
} nvn_metalfx_auto_state;
static inline float nvn_metalfx_auto_step(nvn_metalfx_auto_state *s,
        unsigned pixels, int heat, int low_power, uint64_t now) {
    const uint64_t sec = 1000000000ull;
    float want = nvn_metalfx_scale(0, pixels, heat, low_power);
    if (!s->current || now < s->last_sample || now - s->last_sample > 3 * sec)
        s->recovering = 0;
    s->last_sample = now;
    if (!s->current || want <= s->current) {
        s->current = want; s->recovering = 0;
    } else {
        if (!s->recovering || s->candidate != want) {
            s->candidate = want; s->healthy_since = now; s->recovering = 1;
        }
        if (now - s->healthy_since >= 60 * sec) {
            s->current = want; s->recovering = 0;
        }
    }
    return s->current;
}
