/* 安卓异形屏参数表（B140，用户 11-04「要考虑各种安卓异形屏」）：每种屏都走生产代码的整条几何链——
   出图尺寸（dm_vp_frontend_res + 渲染分辨率像素预算 dm_res_cap_apply）→ 画面矩形（dm_vp_fit，与 present 同一处）→
   控件坐标系（dm_vp_control_rect）→ TouchOverlay.resizeNative 的安全区换算 → 触屏控件布局（dm_tc_layout_make）。
   逐屏断言：
     1. 画面不变形：画面矩形的宽高比 = 出图的宽高比（取整误差 ≤ 1 像素）；比 16:9 窄的窗口画面铺满（不留边、不拉伸）。
     2. 界面矩形（游戏 HUD 与触屏控件共用）在窗口里，且整块在挖孔下方 / 旁边：上挖孔不压界面矩形。
     3. 每个触屏控件（圆钮、摇杆静止位底盘）整圈落在「窗口 − 挖孔内缩 − 圆角」里；底部手势条只做摇杆起点禁区，不挡按钮。
     4. 挖孔在左、在右（横屏两种朝向）都成立。
   屏幕参数是典型机型的量级（像素、密度、挖孔内缩、圆角半径、手势条高），不是某台机器的实测值；真机只验奥丁与 Fold8 内外屏。
   Android odd-shaped screen table (B140, user 11-04 "consider every kind of odd Android screen"): every screen goes through
   the production geometry chain - output size (dm_vp_frontend_res + the render-resolution pixel budget dm_res_cap_apply) ->
   picture rectangle (dm_vp_fit, the one present uses) -> control coordinate system (dm_vp_control_rect) -> the safe-inset
   conversion of TouchOverlay.resizeNative -> the touch control layout (dm_tc_layout_make). Per screen:
     1. no distortion: the picture rectangle's aspect = the output's (rounding <= 1 px); windows narrower than 16:9 are filled.
     2. the UI rectangle (shared by the game HUD and the touch controls) is in the window and clear of a top cutout.
     3. every touch control (round buttons, the stick's rest base) lies entirely inside window - cutout insets - rounded corners;
        the bottom gesture bar is only a no-stick-start strip and never hides a button.
     4. both landscape orientations (cutout on the left or on the right) hold.
   The parameters are typical magnitudes (pixels, density, cutout insets, corner radius, gesture bar), not measurements of a
   particular phone; on devices only the Thor and the Fold8 inner / outer screens are verified.
     clang -O1 -I <repo> -o test_screens dm/android/tests/test_screens.c -lm && ./test_screens
*/
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "dm/host/dm_res_cap.h"
#include "dm/android/viewport.h"
#include "dm/ios/Sources/dm_touch_controls.c"
#include "dm/android/cutout.h"

static int checks, failures;
#define CHECK(expr, ...) do { ++checks; if (!(expr)) { ++failures; fprintf(stderr, "FAIL %s:%d: %s | ", __FILE__, __LINE__, #expr); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

typedef struct {
    const char *name;
    int w, h;            /* 窗口像素（横屏） | window pixels (landscape) */
    float density;       /* dp → px */
    int cut_l, cut_t, cut_r, cut_b;   /* displayCutout 内缩（像素） | displayCutout insets (px) */
    int corner;          /* 圆角半径（像素，四角相同） | rounded corner radius (px, all four corners) */
    int sw_dp;           /* smallestScreenWidthDp（≥ 600 = 平板布局） | smallestScreenWidthDp (>= 600 = tablet layout) */
} screen;

static const screen screens[] = {
    { "Thor 16:9 1920x1080",          1920, 1080, 2.30f,   0,   0,   0, 0,   0, 469 },
    { "16:9 phone 1920x1080",         1920, 1080, 3.00f,   0,   0,   0, 0,  60, 360 },
    { "18:9 2160x1080 hole left",     2160, 1080, 2.75f,  80,   0,   0, 0,  90, 392 },
    { "19.5:9 2340x1080 hole left",   2340, 1080, 2.625f,100,   0,   0, 0, 100, 411 },
    { "20:9 2400x1080 hole left",     2400, 1080, 2.75f,  90,   0,   0, 0, 110, 392 },
    { "20:9 2400x1080 notch left",    2400, 1080, 2.75f, 132,   0,   0, 0, 110, 392 },
    { "21:9 2520x1080",               2520, 1080, 3.00f,   0,   0,   0, 0,  40, 360 },
    { "OPPO 2772x1272 hole left",     2772, 1272, 3.50f, 105,   0,   0, 0, 150, 363 },
    { "fold outer 23:9 2520x1080",    2520, 1080, 2.625f, 95,   0,   0, 0, 100, 411 },
    { "Fold8 inner 2448x1848 top hole",2448, 1848, 2.625f, 0, 110,   0, 0,  60, 704 },
    { "tablet 16:10 2560x1600",       2560, 1600, 2.00f,   0,   0,   0, 0,  80, 800 },
    { "tablet 4:3 2048x1536",         2048, 1536, 2.00f,   0,   0,   0, 0,  60, 768 },
    { "split inner top half 2448x900", 2448, 900, 2.625f,  0, 110,   0, 0,  60, 342 },
    { "split 4:3-ish 1848x1224",      1848, 1224, 2.625f,  0,   0,   0, 0,  60, 466 },
    { "short split 2448x880 top hole", 2448,  880, 2.625f,  0, 120,   0, 0,  60, 335 },
    { "short split 2400x700 top hole", 2400,  700, 2.75f,   0, 110,   0, 0,  60, 254 },
};

/* 圆角裁掉没有：圆（窗口像素）是否整圈在四角半径 R 的圆角矩形 [0,W]×[0,H] 里。
   Whether a circle (window px) lies entirely inside the rounded rectangle [0,W]x[0,H] with corner radius R. */
static int circle_in_rounded(double x, double y, double r, double W, double H, double R) {
    if (x - r < 0 || y - r < 0 || x + r > W || y + r > H) return 0;
    double cx = x < R ? R : x > W - R ? W - R : x, cy = y < R ? R : y > H - R ? H - R : y;
    if (cx == x || cy == y) return 1;   /* 不在角上那一块 | not in a corner square */
    return hypot(x - cx, y - cy) + r <= R + 1e-6;
}

/* 测试自己的圆—矩形相交（不用生产的 dm_cut_hits，免得自证） | the test's own circle-rectangle intersection (not the production dm_cut_hits, so it is not self-proving) */
static int overlaps(double x, double y, double r, dm_cut_rect c) {
    double dx = x < c.x0 ? c.x0 - x : x > c.x1 ? x - c.x1 : 0, dy = y < c.y0 ? c.y0 - y : y > c.y1 ? y - c.y1 : 0;
    return dx * dx + dy * dy < r * r - 1e-6;
}

static void run(const screen *s, int mirror) {
    const char *side = mirror ? " (mirrored)" : "";
    int cl = mirror ? s->cut_r : s->cut_l, cr = mirror ? s->cut_l : s->cut_r;
    /* 1. 出图 → 预算缩 → 画面矩形（缓冲 = 窗口；比例一致时 present_native 铺满，同一个矩形） */
    unsigned fw = 0, fh = 0; dm_vp_frontend_res(s->w, s->h, 1, &fw, &fh);
    uint64_t r = dm_res_cap_apply(fw, fh, 4000000); unsigned ow = (unsigned)r, oh = (unsigned)(r >> 32);
    /* 比例一致（1%）时 present_native 把缓冲设成出图尺寸、由显示硬件铺满整窗（vulkan_present.cpp）；否则居中 blit 留边。
       With matching aspects (1%) present_native sizes the buffer to the output and the display fills the window
       (vulkan_present.cpp); otherwise a centred blit with bars. */
    int native = dm_vp_same_aspect(s->w, s->h, (int)ow, (int)oh);
    dm_vp_rect pic = native ? (dm_vp_rect){ 0, 0, s->w, s->h } : dm_vp_fit(s->w, s->h, (int)ow, (int)oh);
    double out_aspect = (double)ow / oh, pic_aspect = (double)pic.w / pic.h;
    /* 不变形：铺满时形变 ≤ 0.5%（只剩渲染分辨率档 8 / 2 像素对齐的零头），留边时 ≤ 1 像素 | no distortion: <= 0.5% when filled (only the render-level alignment remainder), <= 1 px with bars */
    if (native) CHECK(fabs(pic_aspect / out_aspect - 1) <= 0.005, "%s%s: filled window %dx%d distorts output %ux%u by %.2f%%", s->name, side, s->w, s->h, ow, oh, fabs(pic_aspect / out_aspect - 1) * 100);
    else CHECK(fabs(pic_aspect - out_aspect) * pic.h <= 1.0 + 1e-9, "%s%s: picture %dx%d vs output %ux%u", s->name, side, pic.w, pic.h, ow, oh);
    if (dm_vp_narrower_than_16_9(s->w, s->h))
        CHECK(pic.x == 0 && pic.y == 0 && pic.w == s->w && pic.h == s->h, "%s%s: narrower than 16:9 must fill, got %d,%d %dx%d", s->name, side, pic.x, pic.y, pic.w, pic.h);
    /* 2. 界面矩形 */
    dm_vp_rect ctrl = dm_vp_control_rect(pic, s->w, s->h);
    CHECK(ctrl.x >= 0 && ctrl.y >= 0 && ctrl.x + ctrl.w <= s->w && ctrl.y + ctrl.h <= s->h, "%s%s: UI rect outside window", s->name, side);
    double ui_aspect = (double)ctrl.w / ctrl.h;
    CHECK(ui_aspect >= 16.0 / 9 - 0.02, "%s%s: UI rect %dx%d narrower than 16:9", s->name, side, ctrl.w, ctrl.h);
    /* 3. 触屏控件（TouchOverlay.resizeNative 同一份换算：从窗口边量的内缩减掉黑边，dp） */
    float d = s->density;
    float l = fmaxf(0, cl / d - ctrl.x / d), rr = fmaxf(0, cr / d - (s->w - ctrl.x - ctrl.w) / d), b = fmaxf(0, s->cut_b / d - (s->h - ctrl.y - ctrl.h) / d);
    dm_tc_set_tall_scale(s->sw_dp >= 600); dm_tc_set_swap_above(0);
    dm_tc_set_safe_insets(l, rr); dm_tc_set_safe_bottom(b); dm_tc_set_edge_bottom(b);
    dm_tc_layout L; dm_tc_layout_make(&L, ctrl.w / d, ctrl.h / d);
    /* 挖孔真实矩形（窗口像素）：横屏打孔 / 刘海贴在被挖那条长边、靠上（摄像头在机身顶端，横过来就在那一侧的上部）；
       顶边孔在右侧三分之一处。换到控件坐标 dp 后走生产的 dm_cut_avoid（touch_android.c 同一调用）。
       Real cutout rectangles (window px): a landscape hole / notch sits on the cut long side, towards the top (the camera is at the
       top of the body, which is that side's upper part in landscape); a top-edge hole sits at the right third. Converted to control
       dp and run through the production dm_cut_avoid (the same call as touch_android.c). */
    dm_cut_rect win_cuts[2]; int nc = 0;
    if (cl) win_cuts[nc++] = (dm_cut_rect){ 0, s->h * 0.06f, (float)cl, s->h * 0.06f + (s->cut_l == 132 || s->cut_r == 132 ? s->h * 0.30f : cl) };
    if (cr) win_cuts[nc++] = (dm_cut_rect){ (float)(s->w - cr), s->h * 0.06f, (float)s->w, s->h * 0.06f + (s->cut_l == 132 || s->cut_r == 132 ? s->h * 0.30f : cr) };
    if (s->cut_t) { float cx = mirror ? s->w * 0.25f : s->w * 0.75f; win_cuts[nc++] = (dm_cut_rect){ cx - s->cut_t / 2.0f, 0, cx + s->cut_t / 2.0f, (float)s->cut_t }; }
    dm_cut_rect cuts_dp[2]; int ncd = 0;
    for (int i = 0; i < nc && ncd < 2; i++) cuts_dp[ncd++] = (dm_cut_rect){ (win_cuts[i].x0 - ctrl.x) / d, (win_cuts[i].y0 - ctrl.y) / d, (win_cuts[i].x1 - ctrl.x) / d, (win_cuts[i].y1 - ctrl.y) / d };
    float before[DM_TC_COUNT][2], xs[DM_TC_COUNT], ys[DM_TC_COUNT], rs[DM_TC_COUNT];
    for (int i = 0; i < DM_TC_COUNT; i++) { xs[i] = before[i][0] = L.target[i].x; ys[i] = before[i][1] = L.target[i].y; rs[i] = L.target[i].r; }
    dm_cut_avoid_set(xs, ys, rs, DM_TC_COUNT, cuts_dp, ncd, 6, ctrl.w / d, ctrl.h / d, 4);   /* touch_android.c 同一调用 | the same call as touch_android.c */
    for (int i = 0; i < DM_TC_COUNT; i++) { L.target[i].x = xs[i]; L.target[i].y = ys[i]; }
    /* 挪过的控件不压别的控件（交叉审 P2）| a moved control does not overlap another (cross-review P2) */
    for (int i = 0; i < DM_TC_COUNT; i++) { if (L.target[i].r <= 0 || (before[i][0] == xs[i] && before[i][1] == ys[i])) continue;
        for (int j = 0; j < DM_TC_COUNT; j++) if (j != i && L.target[j].r > 0)
            CHECK(hypot(xs[i] - xs[j], ys[i] - ys[j]) >= L.target[i].r + L.target[j].r, "%s%s: moved control %d overlaps control %d", s->name, side, i, j); }
    int clipped = 0;
    for (int i = 0; i < DM_TC_COUNT + 1; i++) {
        const dm_tc_target *t = i < DM_TC_COUNT ? &L.target[i] : NULL;
        double x = t ? t->x : L.joy_x, y = t ? t->y : L.joy_y, rad = t ? t->r : L.joy_radius;
        if (rad <= 0) continue;
        double X = ctrl.x + x * d, Y = ctrl.y + y * d, Rr = rad * d;
        int in_round = circle_in_rounded(X, Y, Rr, s->w, s->h, s->corner);
        int clear_cut = 1; for (int c = 0; c < nc; c++) if (overlaps(X, Y, Rr, win_cuts[c])) clear_cut = 0;
        if (!in_round || !clear_cut) {
            clipped++;
            fprintf(stderr, "  %s%s: control %d at %.0f,%.0f r=%.0f px %s\n", s->name, side, i, X, Y, Rr, !in_round ? "cut by a rounded corner" : "under the cutout");
        }
    }
    CHECK(clipped == 0, "%s%s: %d controls clipped", s->name, side, clipped);
}

int main(void) {
    for (size_t i = 0; i < sizeof screens / sizeof *screens; i++) { run(&screens[i], 0); run(&screens[i], 1); }
    /* dm_cut_avoid 单测：左右都被挡（横跨顶边的宽刘海）时只能往下挪；挪完与矩形（含 pad）不相交、整圈仍在区内；没挖孔不动。
       dm_cut_avoid unit cases: blocked on both sides (a wide notch across the top edge) it can only move down; afterwards it clears the
       rectangle (with pad) and stays inside the area; no cutout, no move. */
    { float x = 500, y = 30, r = 22; dm_cut_rect wide = { 300, 0, 980, 60 };
      CHECK(dm_cut_avoid(&x, &y, r, &wide, 1, 6, 1000, 400) == 1, "wide notch: moved");
      CHECK(x == 500 && !overlaps(x, y, r + 6 - 0.01, wide) && y - r >= 0 && y + r <= 400, "wide notch: straight down, clear (x=%.1f y=%.1f)", x, y);
      float x2 = 970, y2 = 30; dm_cut_rect hole = { 940, 10, 1000, 70 };
      CHECK(dm_cut_avoid(&x2, &y2, r, &hole, 1, 6, 1000, 400) == 1 && !overlaps(x2, y2, r + 6 - 0.01, hole) && x2 - r >= 0, "corner hole: moved clear (x=%.1f y=%.1f)", x2, y2);
      float x3 = 500, y3 = 200; CHECK(dm_cut_avoid(&x3, &y3, r, &hole, 1, 6, 1000, 400) == 0 && x3 == 500 && y3 == 200, "far from the hole: unchanged");
      CHECK(dm_cut_avoid(&x3, &y3, r, NULL, 0, 6, 1000, 400) == 0, "no cutouts: unchanged");
      /* 横向擦 pad：圆边离孔 3 dp（pad 是 6）也要挪 | grazing the pad sideways: 3 dp from the hole (pad 6) must still move */
      { float x5 = 940 - r - 3, y5 = 40; CHECK(dm_cut_avoid(&x5, &y5, r, &hole, 1, 6, 1000, 400) == 1 && !overlaps(x5, y5, r + 6 - 0.01, hole), "sideways pad graze: moved (x=%.1f)", x5); }
      /* 擦边：圆心离孔 0.7r（含 pad）也必须挪开 | grazing: a centre 0.7r from the hole (with pad) must still move */
      float x4 = 940 - 6 - 0.7f * r, y4 = 40; CHECK(dm_cut_avoid(&x4, &y4, r, &hole, 1, 6, 1000, 400) == 1 && !overlaps(x4, y4, r + 6 - 0.01, hole), "grazing: moved clear (x=%.1f)", x4); }
    /* 交叉审 P2 原样：矮窗口（高 < 370 dp）右上角顶边有孔，地图钮挪开后不能压传送门 / 别的控件，也不能留在孔上。
       Cross-review P2 as reported: a short window (< 370 dp high) with a hole at the top-right corner - the map button moves off it
       without landing on the portal / another control and without staying on the hole. */
    for (float hh = 300; hh <= 370; hh += 10) {
        dm_tc_set_tall_scale(0); dm_tc_set_safe_insets(0, 0); dm_tc_set_safe_bottom(0); dm_tc_set_edge_bottom(0);
        dm_tc_layout L; dm_tc_layout_make(&L, 900, hh);
        const dm_tc_target *m = &L.target[DM_TC_MAP];
        dm_cut_rect hole = { m->x - 30, 0, 900, m->y + m->r + 10 };
        float xs[DM_TC_COUNT], ys[DM_TC_COUNT], rs[DM_TC_COUNT];
        for (int i = 0; i < DM_TC_COUNT; i++) { xs[i] = L.target[i].x; ys[i] = L.target[i].y; rs[i] = L.target[i].r; }
        dm_cut_avoid_set(xs, ys, rs, DM_TC_COUNT, &hole, 1, 6, 900, hh, 4);
        CHECK(!overlaps(xs[DM_TC_MAP], ys[DM_TC_MAP], rs[DM_TC_MAP], hole), "h=%.0f: map button left on the hole", hh);
        for (int j = 0; j < DM_TC_COUNT; j++) if (j != DM_TC_MAP && rs[j] > 0)
            CHECK(hypot(xs[DM_TC_MAP] - xs[j], ys[DM_TC_MAP] - ys[j]) >= rs[DM_TC_MAP] + rs[j], "h=%.0f: map button on control %d", hh, j);
    }
    printf("screens: %d/%d checks passed (%zu screens x 2 orientations)\n", checks - failures, checks, sizeof screens / sizeof *screens);
    return failures ? 1 : 0;
}
