/* 画面在窗口里的矩形（dm/android/viewport.h）与触控换算的几何回归。
   16:9 的画面放进比它宽 / 比它高的窗口时：同一个可见按钮的中心必须换回游戏里那个按钮的中心，黑边上的
   按下不得落进按钮，靠近边界、旋转、像素不整除时缩放与偏移一致。
   按钮坐标用生产布局（dm_touch_controls.c 的 dm_tc_layout_make）算，命中用生产的 dm_tc_hit，不另编一套。
   纯几何，不含 Android / Vulkan / 游戏：clang -O1 -I <repo> -o test_viewport dm/android/tests/test_viewport.c
   -lm && ./test_viewport
   Geometry regression for the picture's rectangle inside the window (dm/android/viewport.h) and the touch
   conversion: with a 16:9 picture in a wider or taller window, the centre of a visible button must map back
   to that button's centre in the game; a press on the black bars must not reach a button; near the edges, after
   a rotation, and with pixel sizes that do not divide evenly the scale and the offset must agree.
   Button positions come from the production layout (dm_tc_layout_make) and hits from the production dm_tc_hit,
   not from a copy. Pure geometry, no Android / Vulkan / game. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dm/android/viewport.h"
#include "dm/ios/Sources/dm_touch_controls.c"

static int checks, failures;
#define CHECK(expr) do { \
    ++checks; \
    if (!(expr)) { ++failures; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); } \
} while (0)
static void near(double got, double want, double tol, const char *what) {
    ++checks;
    if (!(fabs(got - want) <= tol)) { ++failures; fprintf(stderr, "FAIL %s: got %.4f want %.4f\n", what, got, want); }
}

/* ---- 今天的触控换算：TouchOverlay.java event() / publishRaw() 把视图里的点按视图宽高归一， ---- */
/* ---- 没有画面矩形、也不减黑边偏移。RED 反例就靠它；修好后换成 dm_vp_map。                     ---- */
/* ---- Today's touch conversion: TouchOverlay.java event()/publishRaw() normalizes the view point by  ---- */
/* ---- the view size, with no picture rectangle and no bar offset. The RED counter-example uses it;   ---- */
/* ---- the fix replaces it with dm_vp_map.                                                           ---- */
static void touch_today(int win_w, int win_h, double wx, double wy, double *u, double *v) {
    *u = wx / win_w; *v = wy / win_h;
}

static dm_tc_layout layout;
static void layout_for(int content_w, int content_h) { dm_tc_layout_make(&layout, (float)content_w, (float)content_h); }

typedef struct { int win_w, win_h, content_w, content_h; } screen_case;
/* 折叠内屏（比 16:9 窄 / 近 4:3）、外屏、常见手机、以及同一台机器旋转后的尺寸。
   Fold inner screen (narrower than 16:9 / near 4:3), outer screen, ordinary phones, and the same machine rotated. */
static const screen_case screens[] = {
    {1600, 1200, 1600, 900},    /* 4:3 窗口：上下各 150 像素黑边 | 4:3 window: 150-pixel bars top and bottom */
    {1200, 1600, 1200, 675},    /* 旋转后仍是 16:9 输出，高窗口 | rotated: output still 16:9, tall window */
    {1081, 811, 1600, 900},     /* 像素不整除 | sizes that do not divide evenly */
    {2400, 1080, 1600, 900},    /* 比 16:9 宽：左右黑边 | wider than 16:9: bars left and right */
    {1920, 1080, 1920, 1080},   /* 比例对得上：铺满 | matching aspect: fills the window */
    {2340, 1080, 1600, 738},    /* 手机原生比例，输出按预算缩过（比例有零头） | phone aspect, output shrunk by the budget */
};

/* 内容坐标（像素）→ 窗口坐标（像素）：等价于把控件画到画面矩形里。 | content px -> window px: drawing a control into the picture rectangle */
static void to_window(dm_vp_rect r, int content_w, int content_h, double cx, double cy, double *wx, double *wy) {
    *wx = r.x + cx * (double)r.w / content_w; *wy = r.y + cy * (double)r.h / content_h;
}

int main(void) {
    /* A. 矩形几何 | rectangle geometry */
    dm_vp_rect r = dm_vp_fit(1600, 1200, 1600, 900);
    CHECK(r.x == 0 && r.y == 150 && r.w == 1600 && r.h == 900);
    r = dm_vp_fit(2400, 1080, 1600, 900);
    CHECK(r.x == 240 && r.y == 0 && r.w == 1920 && r.h == 1080);
    r = dm_vp_fit(1920, 1080, 1920, 1080);
    CHECK(r.x == 0 && r.y == 0 && r.w == 1920 && r.h == 1080);
    r = dm_vp_fit(1081, 811, 1600, 900);
    CHECK(r.x == 0 && r.y == 101 && r.w == 1081 && r.h == 608);
    CHECK(r.y + r.h == 709 && 811 - (r.y + r.h) == 102);   /* 余下的一像素落在下边 | the odd pixel lands at the bottom */
    r = dm_vp_fit(1200, 1600, 1200, 675);
    CHECK(r.y == (1600 - 675) / 2 && r.h == 675 && r.w == 1200);
    r = dm_vp_fit(0, 0, 1600, 900);
    CHECK(r.w == 0 && r.h == 0);
    r = dm_vp_fit(1000, 500, 0, 0);                          /* 内容尺寸未知：整窗，与今天的行为一致 | unknown content size: whole window, as today */
    CHECK(r.x == 0 && r.y == 0 && r.w == 1000 && r.h == 500);

    for (unsigned i = 0; i < sizeof screens / sizeof *screens; ++i) {
        screen_case s = screens[i];
        dm_vp_rect q = dm_vp_fit(s.win_w, s.win_h, s.content_w, s.content_h);
        /* 矩形始终在窗口内，比例与内容一致（取整误差 < 0.5%），居中。 | inside the window, aspect kept (<0.5% rounding), centred */
        CHECK(q.x >= 0 && q.y >= 0 && q.x + q.w <= s.win_w && q.y + q.h <= s.win_h && q.w > 0 && q.h > 0);
        near((double)q.w / q.h, (double)s.content_w / s.content_h, 0.005 * (double)s.content_w / s.content_h, "rect aspect");
        CHECK(abs((s.win_w - q.w) / 2 - q.x) <= 1 && abs((s.win_h - q.h) / 2 - q.y) <= 1);
        layout_for(s.content_w, s.content_h);

        /* B. 同一个可见按钮中心换回游戏里那个按钮中心 | a visible button centre maps back to that button's centre */
        static const int targets[] = {DM_TC_MAIN, DM_TC_SKILL1, DM_TC_SKILL2, DM_TC_SKILL3, DM_TC_SKILL4,
                                      DM_TC_PORTAL, DM_TC_MAP, DM_TC_BELT1, DM_TC_BELT4, DM_TC_MENU};
        for (unsigned k = 0; k < sizeof targets / sizeof *targets; ++k) {
            int t = targets[k];
            const dm_tc_target *b = t == DM_TC_MAP ? &layout.target[DM_TC_MAP] : &layout.target[t];
            if (b->r <= 0 || b->x < 0 || b->y < 0) continue;
            double wx, wy, u, v;
            to_window(q, s.content_w, s.content_h, b->x, b->y, &wx, &wy);
            touch_today(s.win_w, s.win_h, wx, wy, &u, &v);
            near(u * s.content_w, b->x, 0.75, "button centre x");
            near(v * s.content_h, b->y, 0.75, "button centre y");
            near(dm_tc_hit(&layout, (float)(u * s.content_w), (float)(v * s.content_h)), t, 0.5, "button hit");
        }

        /* 换组键与难度格同理 | same for the set switch and the difficulty cell */
        {   double wx, wy, u, v;
            to_window(q, s.content_w, s.content_h, layout.swap.x, layout.swap.y, &wx, &wy);
            touch_today(s.win_w, s.win_h, wx, wy, &u, &v);
            near(dm_tc_hit(&layout, (float)(u * s.content_w), (float)(v * s.content_h)), DM_TC_SWAP, 0.5, "swap hit");
        }

        /* C. 黑边：把按钮中心按今天的触控换算放回窗口——落在画面矩形外的那些，那一下不得触发任何按钮。
           Black bars: put each button centre back into the window with today's touch conversion; the ones that
           land outside the picture rectangle must not reach any button. */
        for (unsigned k = 0; k < sizeof targets / sizeof *targets; ++k) {
            int t = targets[k];
            const dm_tc_target *b = &layout.target[t];
            if (b->r <= 0 || b->x < 0 || b->y < 0) continue;
            double u, v;
            double wx = b->x * (double)s.win_w / s.content_w, wy = b->y * (double)s.win_h / s.content_h;
            if (dm_vp_inside(q, wx, wy)) continue;   /* 这个按钮本来就在画面里 | this button is inside the picture */
            CHECK(! dm_vp_inside(q, wx, wy));
            touch_today(s.win_w, s.win_h, wx, wy, &u, &v);
            near(dm_tc_hit(&layout, (float)(u * s.content_w), (float)(v * s.content_h)), DM_TC_NONE, 0.5, "bar tap hits nothing");
        }

        /* D. 边界：画面矩形左上角那一点在矩形内、换算到内容原点；再往外一点在矩形外。
           Edge: the picture rectangle's top-left is inside and maps to the content origin; one step out is outside. */
        {   double u, v;
            CHECK(dm_vp_inside(q, q.x, q.y));
            CHECK(! dm_vp_inside(q, q.x - 1, q.y));
            CHECK(! dm_vp_inside(q, q.x, q.y - 1));
            touch_today(s.win_w, s.win_h, q.x, q.y, &u, &v);
            near(u * s.content_w, 0, 0.75, "content origin x");
            near(v * s.content_h, 0, 0.75, "content origin y");
        }

        /* E. 屏幕右下角（画面矩形右下角之内一点）→ 内容右下角附近 | just inside the bottom-right corner -> near the content's bottom-right */
        {   double u, v;
            touch_today(s.win_w, s.win_h, q.x + q.w - 1, q.y + q.h - 1, &u, &v);
            near(u * s.content_w, s.content_w, 1.5, "content right");
            near(v * s.content_h, s.content_h, 1.5, "content bottom");
        }
    }

    printf("viewport: %d/%d checks passed\n", checks - failures, checks);
    return failures != 0;
}
