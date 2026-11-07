/* 自适应全屏的几何与触控回归：窗口比例一变（折叠内屏、平板、分屏、超宽、旋转、像素不整除、渲染分辨率档缩过），
   游戏出图比例必须等于实际窗口比例（不拉伸、不裁切、不靠系统插件），界面与触控控件按 16:9 贴底那一块摆，
   按钮中心换回窗口再换回来仍是同一个中心、同一个命中目标；普通 16:9 手机逐点与改动前相同。
   全部调生产代码：dm/android/viewport.h 的 dm_vp_frontend_res / dm_vp_ui_band / dm_vp_control_rect /
   dm_vp_map_ctrl / dm_vp_pic_norm / dm_vp_fit、dm/host/dm_res_cap.h 的像素预算、dm_touch_controls.c 的
   dm_tc_layout_make 与 dm_tc_hit。不另编一套公式。
   Adaptive-fullscreen geometry and touch regression: whenever the window's aspect changes (fold inner screen,
   tablet, split screen, ultrawide, rotation, sizes that do not divide evenly, an output shrunk by the pixel
   budget) the game's output aspect must equal the window's (no stretch, no crop, no system plug-in), the UI and
   the touch controls live in the 16:9 block at the bottom, a button centre converted into the window and back is
   the same centre with the same hit target, and a 16:9 phone maps point by point exactly as before.
   Everything calls production code: dm_vp_frontend_res / dm_vp_ui_band / dm_vp_control_rect / dm_vp_map_ctrl /
   dm_vp_pic_norm / dm_vp_fit from dm/android/viewport.h, the pixel budget from dm/host/dm_res_cap.h, and
   dm_tc_layout_make / dm_tc_hit from dm_touch_controls.c. No second copy of any formula.
     clang -O1 -I <repo> -o test_adaptive dm/android/tests/test_adaptive.c -lm && ./test_adaptive
*/
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "dm/host/dm_res_cap.h"
#include "dm/android/viewport.h"
#include "dm/ios/Sources/dm_touch_controls.c"

static int checks, failures;
#define CHECK(expr) do { \
    ++checks; \
    if (!(expr)) { ++failures; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); } \
} while (0)
static void near(double got, double want, double tol, const char *what) {
    ++checks;
    if (!(fabs(got - want) <= tol)) { ++failures; fprintf(stderr, "FAIL %s: got %.4f want %.4f (tol %.3f)\n", what, got, want, tol); }
}

/* 窗口（长边 × 短边，前端已把横竖归一）：
   · 2448×1848 = Fold8 内屏（1.32:1，比 16:9 窄得多）
   · 1600×1200 = 4:3
   · 1081×811  = 4:3，像素不整除
   · 2448×1847 = 高度奇数（对齐要退到偶数，比例只剩 0.05% 的零头）
   · 1920×1080 = 16:9 手机（行为必须逐点不变）
   · 2340×1080 = 19.5:9 手机（比 16:9 宽，本来就铺满）
   · 2400×1080 = 超宽（同上）
   The window (long side x short side, the front end already normalised the orientation). */
typedef struct { int w, h; const char *name; } win_case;
static const win_case windows[] = {
    {2448, 1848, "fold inner 2448x1848"},
    {1600, 1200, "4:3 1600x1200"},
    {1081, 811,  "odd 4:3 1081x811"},
    {2448, 1847, "odd height 2448x1847"},
    {1920, 1080, "16:9 phone 1920x1080"},
    {2340, 1080, "phone 2340x1080"},
    {2400, 1080, "ultrawide 2400x1080"},
};

static dm_tc_layout layout;
static void layout_for(int w, int h) { dm_tc_layout_make(&layout, (float)w, (float)h); }
static int narrower(const win_case *s) { return dm_vp_narrower_than_16_9(s->w, s->h); }

/* 窗口点 → 控件坐标（Java 的 TouchOverlay → nativeMap → dm_vp_map_ctrl）。 */
static int map_ctrl(dm_vp_rect pic, dm_vp_rect ctrl, double wx, double wy, double *cx, double *cy) {
    return dm_vp_map_ctrl(pic, ctrl, wx, wy, cx, cy);
}

int main(void) {
    /* A. 出图尺寸：旧做法（adaptive=0）在比 16:9 窄的窗口上按宽取 16:9，自适应（adaptive=1）取窗口自己的比例。
       这就是「16:9 letterbox」与「自适应全屏」的分界，DM_ADAPTIVE_FULL=0 时前端仍走 adaptive=0。
       Output size: the old rule (adaptive=0) takes 16:9 by width on a window narrower than 16:9, adaptive takes
       the window's own aspect. That is the boundary between 16:9 letterbox and adaptive fullscreen; with
       DM_ADAPTIVE_FULL=0 the front end still passes adaptive=0. */
    for (unsigned i = 0; i < sizeof windows / sizeof *windows; ++i) {
        const win_case *s = &windows[i];
        unsigned lw = 0, lh = 0, aw = 0, ah = 0;
        dm_vp_frontend_res(s->w, s->h, 0, &lw, &lh);
        dm_vp_frontend_res(s->w, s->h, 1, &aw, &ah);

        /* 旧做法：比 16:9 窄就按宽取 16:9；不窄就与自适应相同（本来就没有边）。 */
        if (narrower(s)) {
            CHECK(lh == (((unsigned)s->w * 9 / 16) & ~1u));
            CHECK((double)lw / lh > 1.7 && (double)lw / lh < 1.8);
        } else {
            CHECK(lw == aw && lh == ah);
        }
        /* 自适应：出图比例 = 实际窗口比例（0.2% 内，偶数对齐的零头）。 */
        CHECK(aw > 0 && ah > 0);
        near((double)aw / (double)ah, (double)s->w / (double)s->h, 0.002, "output aspect == window aspect");
        CHECK((aw & 1u) == 0 && (ah & 1u) == 0);          /* 偶数 | even */
        CHECK(aw <= 4096);
        if (narrower(s)) CHECK(ah > lh);                  /* 自适应比旧做法高：补回上下黑边那一截 */

        /* B. 画面矩形必须铺满窗口：不裁切（不超出）、不留边（四个方向各 1 像素以内）。 */
        dm_vp_rect pic = dm_vp_fit(s->w, s->h, (int)aw, (int)ah);
        CHECK(abs(pic.x) <= 1 && abs(pic.y) <= 1);
        CHECK(abs(pic.w - s->w) <= 1 && abs(pic.h - s->h) <= 1);
        CHECK(pic.w <= s->w && pic.h <= s->h);

        /* C. 控件坐标系：自适应全屏下是「宽 = 窗口宽、高 = 宽×9/16、贴底」那块；比 16:9 宽或比例一致时就是画面。 */
        dm_vp_rect ctrl = dm_vp_control_rect(pic, s->w, s->h);
        if (narrower(s)) {
            CHECK(ctrl.x == 0 && ctrl.w == s->w);
            CHECK(ctrl.h == (int)(((long long)s->w * 9 / 16) & ~1ll));
            CHECK(ctrl.y == s->h - ctrl.h);
            CHECK(ctrl.h < s->h);                          /* 上方那条是 3D：铺满，不裁 */
            dm_vp_rect band = dm_vp_ui_band(s->w, s->h);
            CHECK(band.x == ctrl.x && band.y == ctrl.y && band.w == ctrl.w && band.h == ctrl.h);
        } else {
            CHECK(ctrl.x == pic.x && ctrl.y == pic.y && ctrl.w == pic.w && ctrl.h == pic.h);
        }

        /* D. 按钮中心：控件按界面矩形摆（游戏 HUD 就在这块里），中心换到窗口再换回来必须是同一个中心、同一个命中目标。 */
        layout_for(ctrl.w, ctrl.h);
        static const int targets[] = {DM_TC_MAIN, DM_TC_SKILL1, DM_TC_SKILL2, DM_TC_SKILL3, DM_TC_SKILL4,
                                      DM_TC_PORTAL, DM_TC_MAP, DM_TC_BELT1, DM_TC_BELT4, DM_TC_MENU};
        for (unsigned k = 0; k < sizeof targets / sizeof *targets; ++k) {
            const dm_tc_target *b = &layout.target[targets[k]];
            if (b->r <= 0 || b->x < 0 || b->y < 0) continue;
            double wx = ctrl.x + b->x, wy = ctrl.y + b->y, cx = 0, cy = 0;
            CHECK(map_ctrl(pic, ctrl, wx, wy, &cx, &cy));
            near(cx, b->x, 0.75, "button centre x");
            near(cy, b->y, 0.75, "button centre y");
            near(dm_tc_hit(&layout, (float)cx, (float)cy), targets[k], 0.5, "button hit");
        }
        {   double wx = ctrl.x + layout.swap.x, wy = ctrl.y + layout.swap.y, cx = 0, cy = 0;
            CHECK(map_ctrl(pic, ctrl, wx, wy, &cx, &cy));
            near(dm_tc_hit(&layout, (float)cx, (float)cy), DM_TC_SWAP, 0.5, "swap hit");
        }

        /* E. 界面矩形上方那条 3D 带：自适应全屏下它仍是画面的一部分——按下不丢（手指算一只手，纵向为负），
           但那里没有控件，不能被它碰到任何一个按钮。旧做法（16:9 输出）那里是黑边，按下必须丢掉。
           The 3D strip above the UI rectangle: in adaptive fullscreen it is still part of the picture, so a press
           is kept (a finger, negative y) but reaches no control. With the old 16:9 output that strip is a black
           bar and the press must be dropped. */
        {   double cx = 0, cy = 0;
            double wy = (double)ctrl.y * 0.5;              /* 界面矩形与窗口顶边之间 | between the band and the top */
            int inside = map_ctrl(pic, ctrl, s->w * 0.5, wy, &cx, &cy);
            if (narrower(s)) {
                CHECK(inside == 1);
                CHECK(cy < 0);
                near(dm_tc_hit(&layout, (float)cx, (float)cy), DM_TC_NONE, 0.5, "strip hits no button");
                near(cy, wy - ctrl.y, 0.75, "strip maps to the picture point");
            } else if (wy < pic.y) {
                CHECK(inside == 0);                          /* 黑边：丢掉 | a black bar: dropped */
            }
        }

        /* F. 世界点选用的归一化坐标相对整个画面，不相对界面矩形（点怪、点箱子、点地走路）。 */
        {   double nx = 0, ny = 0;
            dm_vp_pic_norm(ctrl.w * 0.5, ctrl.h * 0.5, ctrl.x, ctrl.y, pic.w, pic.h, &nx, &ny);
            near(nx, (ctrl.x + ctrl.w * 0.5) / (double)pic.w, 0.001, "world nx");
            near(ny, (ctrl.y + ctrl.h * 0.5) / (double)pic.h, 0.001, "world ny");
            CHECK(ny > 0.5 || !narrower(s));                 /* 界面矩形在下半部分 | the band sits in the lower half */
            dm_vp_pic_norm(10, 10, 0, 0, 0, 0, &nx, &ny);    /* 画面尺寸未知：0（不做换算）| unknown picture: zeroes */
            CHECK(nx == 0 && ny == 0);
        }
    }

    /* G. 16:9 手机逐点不变：控件坐标系就是整个窗口，换算是恒等（一个像素都不动）。 */
    {   dm_vp_rect pic = dm_vp_fit(1920, 1080, 1920, 1080), ctrl = dm_vp_control_rect(pic, 1920, 1080);
        CHECK(ctrl.x == 0 && ctrl.y == 0 && ctrl.w == 1920 && ctrl.h == 1080);
        for (int y = 0; y < 1080; y += 120) for (int x = 0; x < 1920; x += 160) {
            double cx = -1, cy = -1;
            CHECK(dm_vp_map_ctrl(pic, ctrl, x, y, &cx, &cy) == 1);
            CHECK(fabs(cx - x) < 1e-6 && fabs(cy - y) < 1e-6);
        }
        {   double cx = 0, cy = 0;   /* 右 / 下边界半开：与改动前的 dm_vp_map 同一口径 | the right / bottom edge is half-open, as dm_vp_map was */
            CHECK(dm_vp_map_ctrl(pic, ctrl, 1919, 1079, &cx, &cy) == 1);
            CHECK(dm_vp_map_ctrl(pic, ctrl, 1920, 1079, &cx, &cy) == 0);
            CHECK(dm_vp_map_ctrl(pic, ctrl, 1919, 1080, &cx, &cy) == 0);
        }
    }

    /* H. 渲染分辨率档缩过出图尺寸（像素预算）：比例仍在 1% 内、画面仍铺满、控件仍是界面矩形。 */
    {   unsigned aw = 0, ah = 0; dm_vp_frontend_res(2448, 1848, 1, &aw, &ah);
        uint64_t capped = dm_res_cap_apply(aw, ah, 2800000);
        unsigned cw = (unsigned)capped, ch = (unsigned)(capped >> 32);
        CHECK(cw < aw && ch < ah);                                   /* 真的缩了 | it really shrank */
        near((double)cw / ch, 2448.0 / 1848.0, 0.005, "capped aspect");
        dm_vp_rect pic = dm_vp_fit(2448, 1848, (int)cw, (int)ch);
        CHECK(abs(pic.w - 2448) <= 1 && abs(pic.h - 1848) <= 1);
        dm_vp_rect ctrl = dm_vp_control_rect(pic, 2448, 1848);
        CHECK(ctrl.h == (int)(((long long)2448 * 9 / 16) & ~1ll) && ctrl.y == 1848 - ctrl.h);
    }

    /* I. 分辨状态变化（内屏 → 外屏 → 内屏）：每回一次算出来的矩形与第一次逐位相同，不累计误差；
       布局也跟着重算，外屏（比 16:9 宽）的控件位置与内屏不同（不能沿用旧尺寸）。 */
    {   unsigned w = 0, h = 0;
        dm_vp_frontend_res(2448, 1848, 1, &w, &h);
        dm_vp_rect apic = dm_vp_fit(2448, 1848, (int)w, (int)h), a = dm_vp_control_rect(apic, 2448, 1848);
        dm_vp_frontend_res(2340, 1080, 1, &w, &h);
        dm_vp_rect bpic = dm_vp_fit(2340, 1080, (int)w, (int)h), b = dm_vp_control_rect(bpic, 2340, 1080);
        dm_vp_frontend_res(2448, 1848, 1, &w, &h);
        dm_vp_rect cpic = dm_vp_fit(2448, 1848, (int)w, (int)h), c = dm_vp_control_rect(cpic, 2448, 1848);
        CHECK(b.h == 1080 && b.y == 0);                              /* 外屏比 16:9 宽：整个画面 | the outer screen is wider than 16:9 */
        CHECK(a.x == c.x && a.y == c.y && a.w == c.w && a.h == c.h);
        layout_for(a.w, a.h); float inner_main_y = layout.target[DM_TC_MAIN].y;
        layout_for(b.w, b.h); float outer_main_y = layout.target[DM_TC_MAIN].y;
        CHECK(inner_main_y != outer_main_y);
    }

    /* J. 竖着的一对（锁横屏，旋转动画中间可能瞬时出现）：不算「比 16:9 窄」，控件仍按整个画面，
       不会套一条横屏的 16:9 带。| A portrait pair (the app is locked to landscape; a rotation transient can show
       one): never "narrower than 16:9", the controls follow the picture, no landscape 16:9 band is imposed. */
    {   CHECK(dm_vp_narrower_than_16_9(1080, 1920) == 0);
        unsigned w = 0, h = 0; dm_vp_frontend_res(1080, 1920, 1, &w, &h);
        CHECK(w == 1080 && h == 1920);
        dm_vp_rect pic = dm_vp_fit(1080, 1920, (int)w, (int)h), ctrl = dm_vp_control_rect(pic, 1080, 1920);
        CHECK(ctrl.x == 0 && ctrl.y == 0 && ctrl.w == 1080 && ctrl.h == 1920);
    }

    /* 出图宽上限 4096（引擎保留尺寸）：4K 平板 3840 宽原样，5120 宽按比例压到 4096。DeepSeek 复跑时把上限改成 3000 的变异存活过，
       这两行补上。 | The 4096 output-width cap (the engine reserve): a 4K tablet's 3840 stays, 5120 scales to 4096 keeping
       the aspect. A DeepSeek mutant that moved the cap to 3000 survived before these two lines. */
    { unsigned ow = 0, oh = 0;
      dm_vp_frontend_res(3840, 2400, 1, &ow, &oh); CHECK(ow == 3840 && oh == 2400);
      dm_vp_frontend_res(5120, 2880, 1, &ow, &oh); CHECK(ow == 4096 && oh == 2304); }
    printf("adaptive: %d/%d checks passed\n", checks - failures, checks);
    return failures != 0;
}
