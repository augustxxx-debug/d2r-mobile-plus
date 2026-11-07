/* 画面在窗口里的矩形（dm/android/viewport.h）与触控换算的几何回归。
   16:9 的画面放进比它宽 / 比它高的窗口时（折叠内屏近 4:3 就是一例）：同一个可见按钮的中心必须换回游戏里
   那个按钮的中心，黑边上的按下不得落进按钮，靠近边界、旋转、像素不整除时缩放与偏移一致。
   按钮坐标用生产布局（dm_touch_controls.c 的 dm_tc_layout_make）算，命中用生产的 dm_tc_hit，换算用生产的
   dm_vp_fit / dm_vp_map，不另编一套。
   纯几何，不含 Android / Vulkan / 游戏：
     clang -O1 -I <repo> -o test_viewport dm/android/tests/test_viewport.c -lm && ./test_viewport
   Geometry regression for the picture's rectangle inside the window (dm/android/viewport.h) and the touch
   conversion. With a 16:9 picture in a wider or taller window (a fold inner screen near 4:3 is one), the centre of
   a visible button must map back to that button's centre in the game, a press on the black bars must not reach a
   button, and scale and offset must agree near the edges, after a rotation, and with sizes that do not divide evenly.
   Button positions come from the production layout (dm_tc_layout_make), hits from the production dm_tc_hit and the
   conversion from the production dm_vp_fit / dm_vp_map, not from a copy. Pure geometry, no Android / Vulkan / game. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

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

/* 触控这条路（Java 的 TouchOverlay → nativeMap → dm_vp_map）：窗口点换成画面矩形内的点。
   返回 0 = 落在黑边上，调用方丢掉这一下（不占手指、不发事件）。与按下同样的一条换算，没有第二份。
   The touch path (Java's TouchOverlay -> nativeMap -> dm_vp_map): a window point becomes a point inside the picture
   rectangle. 0 = on a black bar and the caller drops the press (no finger taken, no event sent). One conversion,
   the same one both display and touch use, with no second copy. */
static int touch_map(dm_vp_rect r, int content_w, int content_h, double wx, double wy, double *cx, double *cy) {
    double u = 0, v = 0;
    int inside = dm_vp_map(r, wx, wy, &u, &v);
    *cx = u * content_w; *cy = v * content_h;
    return inside;
}

static dm_tc_layout layout;
static void layout_for(int content_w, int content_h) { dm_tc_layout_make(&layout, (float)content_w, (float)content_h); }

typedef struct { int win_w, win_h, content_w, content_h; } screen_case;
/* 折叠内屏（比 16:9 窄 / 近 4:3）、旋转后的高窗口、像素不整除、比 16:9 宽、比例对得上、手机原生比例（输出按预算缩过）。
   Fold inner screen (narrower than 16:9 / near 4:3), the same machine rotated, sizes that do not divide evenly,
   wider than 16:9, matching aspect, and a phone aspect whose output was shrunk by the pixel budget. */
static const screen_case screens[] = {
    {1600, 1200, 1600, 900},    /* 4:3 窗口：上下各 150 像素黑边 | 4:3 window: 150-pixel bars top and bottom */
    {1200, 1600, 1200, 675},    /* 旋转：输出仍是 16:9，窗口变高 | rotated: output still 16:9, window tall */
    {1081, 811, 1600, 900},     /* 像素不整除 | sizes that do not divide evenly */
    {2400, 1080, 1600, 900},    /* 比 16:9 宽：左右黑边 | wider than 16:9: bars left and right */
    {1920, 1080, 1920, 1080},   /* 比例对得上：铺满 | matching aspect: fills the window */
    {2340, 1080, 1600, 738},    /* 手机比例，出图按像素预算缩过 | phone aspect, output shrunk by the pixel budget */
};

/* 内容坐标（像素）→ 窗口坐标（像素）：等价于把控件画进画面矩形。 | content px -> window px: drawing a control into the rectangle */
static void to_window(dm_vp_rect r, int content_w, int content_h, double cx, double cy, double *wx, double *wy) {
    *wx = r.x + cx * (double)r.w / content_w; *wy = r.y + cy * (double)r.h / content_h;
}

int main(void) {
    /* A. 矩形几何：取整与原来的呈现代码一致（blit 目标矩形），四边都落在窗口内、居中。
       Rectangle geometry: the rounding is the present code's (the blit's dest rectangle), inside the window and centred. */
    dm_vp_rect r = dm_vp_fit(1600, 1200, 1600, 900);
    CHECK(r.x == 0 && r.y == 150 && r.w == 1600 && r.h == 900);
    r = dm_vp_fit(2400, 1080, 1600, 900);
    CHECK(r.x == 240 && r.y == 0 && r.w == 1920 && r.h == 1080);
    r = dm_vp_fit(1920, 1080, 1920, 1080);
    CHECK(r.x == 0 && r.y == 0 && r.w == 1920 && r.h == 1080);
    r = dm_vp_fit(1081, 811, 1600, 900);
    CHECK(r.x == 0 && r.y == 101 && r.w == 1081 && r.h == 608);
    CHECK(r.y + r.h == 709 && 811 - (r.y + r.h) == 102);   /* 不整除：余下的一像素在下边 | odd remainder lands at the bottom */
    r = dm_vp_fit(1200, 1600, 1200, 675);
    CHECK(r.y == (1600 - 675) / 2 && r.h == 675 && r.w == 1200);
    r = dm_vp_fit(0, 0, 1600, 900);
    CHECK(r.w == 0 && r.h == 0);
    r = dm_vp_fit(1000, 500, 0, 0);                          /* 尺寸未知：整窗 | unknown size: the whole window */
    CHECK(r.x == 0 && r.y == 0 && r.w == 1000 && r.h == 500);

    /* A2. 交换链矩形 → 窗口矩形（dm_vp_window_rect）：缓冲就是窗口时逐位恒等（普通手机不变）；缓冲是内容时
       合成器把它拉满窗口，矩形就是整窗；缓冲在两者之间时按窗口缩放。几何被驱动拒绝的回退靠这一条成立。
       Swapchain rect -> window rect: identity when the buffer is the window (a phone is unchanged); the whole
       window when the buffer is the content (the compositor stretches it); scaled when the buffer is between. */
    {   dm_vp_rect b = dm_vp_fit(1600, 1200, 1600, 900), w2 = dm_vp_window_rect(b, 1600, 1200, 1600, 1200);
        CHECK(w2.x == b.x && w2.y == b.y && w2.w == b.w && w2.h == b.h);
        CHECK(w2.y == 150 && w2.h == 900);
        b = dm_vp_fit(2340, 1080, 1920, 1080); w2 = dm_vp_window_rect(b, 2340, 1080, 2340, 1080);
        CHECK(w2.x == 210 && w2.y == 0 && w2.w == 1920 && w2.h == 1080);
        w2 = dm_vp_window_rect((dm_vp_rect){0, 0, 1600, 900}, 1600, 900, 1600, 1200);   /* 缓冲是内容 | buffer is the content */
        CHECK(w2.x == 0 && w2.y == 0 && w2.w == 1600 && w2.h == 1200);
        w2 = dm_vp_window_rect((dm_vp_rect){0, 75, 800, 450}, 800, 600, 1600, 1200);      /* 缓冲介于两者之间 | in between */
        CHECK(w2.x == 0 && w2.y == 150 && w2.w == 1600 && w2.h == 900);
        w2 = dm_vp_window_rect((dm_vp_rect){0, 0, 800, 600}, 800, 600, 1600, 1200);       /* 整块缓冲 | the whole buffer */
        CHECK(w2.x == 0 && w2.y == 0 && w2.w == 1600 && w2.h == 1200);
        w2 = dm_vp_window_rect((dm_vp_rect){0, 0, 100, 100}, 0, 0, 1600, 1200);           /* 缓冲尺寸未知 | buffer unknown */
        CHECK(w2.w == 1600 && w2.h == 1200);
        w2 = dm_vp_window_rect((dm_vp_rect){0, 0, 100, 100}, 100, 100, 0, 0);             /* 窗口尺寸未知 | window unknown */
        CHECK(w2.w == 0 && w2.h == 0);
    }

    /* 比例判定的边界：1% 以内算相同（呈现那边据此决定铺满 / 留黑边） | aspect test: within 1% counts as the same */
    CHECK(dm_vp_same_aspect(1920, 1080, 1600, 900));
    CHECK(dm_vp_same_aspect(1600, 1200, 1600, 900) == 0);
    CHECK(dm_vp_same_aspect(1080, 2400, 675, 1200) == 0);
    CHECK(dm_vp_same_aspect(2340, 1080, 1600, 738));         /* 0.05% 的零头算相同 | a 0.05% remainder counts as the same */
    CHECK(dm_vp_same_aspect(0, 0, 1600, 900));               /* 还不知道窗口尺寸：不改做法 | window size unknown: keep the behaviour */
    /* 容差钉在 1%（与 iOS / 鸿蒙同一条）：差 0.9% 算相同、差 1.2% 与 5% 都算不同 —— 放宽到 10% 会把近 16:9 的窗口
       当成同比例交给合成器拉满，画面就被轻微拉伸（B140）。
       The tolerance is pinned at 1% (the iOS / HarmonyOS one): 0.9% counts as the same, 1.2% and 5% do not. Loosened to
       10%, a window near 16:9 would be treated as matching and handed to the compositor to fill, slightly stretching the
       picture (B140). */
    CHECK(dm_vp_same_aspect(1600, 900, 1600, 908));          /* 0.88% */
    CHECK(dm_vp_same_aspect(1600, 900, 1600, 911) == 0);     /* 1.21% */
    CHECK(dm_vp_same_aspect(1600, 900, 1600, 945) == 0);     /* 5% */

    for (unsigned i = 0; i < sizeof screens / sizeof *screens; ++i) {
        screen_case s = screens[i];
        dm_vp_rect q = dm_vp_fit(s.win_w, s.win_h, s.content_w, s.content_h);
        CHECK(q.x >= 0 && q.y >= 0 && q.x + q.w <= s.win_w && q.y + q.h <= s.win_h && q.w > 0 && q.h > 0);
        near((double)q.w / q.h, (double)s.content_w / s.content_h, 0.005 * (double)s.content_w / s.content_h, "rect aspect");
        CHECK(abs((s.win_w - q.w) / 2 - q.x) <= 1 && abs((s.win_h - q.h) / 2 - q.y) <= 1);
        layout_for(s.content_w, s.content_h);

        /* B. 同一个可见按钮中心换回游戏里那个按钮中心 | a visible button centre maps back to that button's centre */
        static const int targets[] = {DM_TC_MAIN, DM_TC_SKILL1, DM_TC_SKILL2, DM_TC_SKILL3, DM_TC_SKILL4,
                                      DM_TC_PORTAL, DM_TC_MAP, DM_TC_BELT1, DM_TC_BELT4, DM_TC_MENU};
        for (unsigned k = 0; k < sizeof targets / sizeof *targets; ++k) {
            int t = targets[k];
            const dm_tc_target *b = &layout.target[t];
            if (b->r <= 0 || b->x < 0 || b->y < 0) continue;
            double wx, wy, cx, cy;
            to_window(q, s.content_w, s.content_h, b->x, b->y, &wx, &wy);
            CHECK(touch_map(q, s.content_w, s.content_h, wx, wy, &cx, &cy));
            near(cx, b->x, 0.75, "button centre x");
            near(cy, b->y, 0.75, "button centre y");
            near(dm_tc_hit(&layout, (float)cx, (float)cy), t, 0.5, "button hit");
        }

        /* 换组键同理 | same for the set switch */
        {   double wx, wy, cx, cy;
            to_window(q, s.content_w, s.content_h, layout.swap.x, layout.swap.y, &wx, &wy);
            CHECK(touch_map(q, s.content_w, s.content_h, wx, wy, &cx, &cy));
            near(dm_tc_hit(&layout, (float)cx, (float)cy), DM_TC_SWAP, 0.5, "swap hit");
        }

        /* C. 黑边：把每个按钮中心按同一条换算放回窗口，落在画面矩形外的那些，那一下必须被丢掉（不占手指、
           不发事件），于是不可能触到任何一个按钮。修好前这里会命中地图键、技能键。
           Black bars: put every button centre back into the window with the same conversion; the ones landing outside
           the picture rectangle must be dropped (no finger, no event), so they cannot reach a button at all. Before the
           fix these hit the map key and the skill keys. */
        for (unsigned k = 0; k < sizeof targets / sizeof *targets; ++k) {
            int t = targets[k];
            const dm_tc_target *b = &layout.target[t];
            if (b->r <= 0 || b->x < 0 || b->y < 0) continue;
            double wx = b->x * (double)s.win_w / s.content_w, wy = b->y * (double)s.win_h / s.content_h;
            double cx, cy;
            int inside = touch_map(q, s.content_w, s.content_h, wx, wy, &cx, &cy);
            if (dm_vp_inside(q, wx, wy)) { CHECK(inside); continue; }   /* 按钮本来就在画面里 | the button is inside the picture */
            CHECK(! inside);
            CHECK(cx >= 0 && cx <= s.content_w && cy >= 0 && cy <= s.content_h);   /* 夹在画面内：拖出画面的手指落在边上 | clamped to the nearest edge */
        }

        /* D. 边界：画面矩形左上角那一点在矩形内、换算到内容原点；再往外一像素在矩形外，且被丢掉。
           Edge: the rectangle's top-left is inside and maps to the content origin; one pixel out is outside and dropped. */
        {   double cx, cy;
            CHECK(dm_vp_inside(q, q.x, q.y));
            CHECK(! dm_vp_inside(q, q.x - 1, q.y));
            CHECK(! dm_vp_inside(q, q.x, q.y - 1));
            CHECK(touch_map(q, s.content_w, s.content_h, q.x, q.y, &cx, &cy));
            near(cx, 0, 0.75, "content origin x");
            near(cy, 0, 0.75, "content origin y");
            CHECK(! touch_map(q, s.content_w, s.content_h, q.x - 1, q.y, &cx, &cy));
            CHECK(! touch_map(q, s.content_w, s.content_h, q.x, q.y - 1, &cx, &cy));
        }

        /* E. 画面矩形右下角之内一像素 → 内容右下角附近；正中 → 内容正中（比例对了，缩放才不会走形）。
           One pixel inside the bottom-right corner -> near the content's; the centre -> the content's centre (with the
           aspect right, the scale cannot distort). */
        {   double cx, cy;
            CHECK(touch_map(q, s.content_w, s.content_h, q.x + q.w - 1, q.y + q.h - 1, &cx, &cy));
            near(cx, s.content_w, 1.5, "content right");
            near(cy, s.content_h, 1.5, "content bottom");
            CHECK(touch_map(q, s.content_w, s.content_h, q.x + q.w / 2.0, q.y + q.h / 2.0, &cx, &cy));
            near(cx, s.content_w / 2.0, 0.75, "content centre x");
            near(cy, s.content_h / 2.0, 0.75, "content centre y");
        }

        /* F. 拖出画面：移动仍夹在边上（松手不会落到画面外）| dragged out of the picture: clamped, the release cannot land outside */
        {   double cx, cy;
            CHECK(! touch_map(q, s.content_w, s.content_h, -50, -50, &cx, &cy));
            near(cx, 0, 0.75, "drag out top-left x");
            near(cy, 0, 0.75, "drag out top-left y");
            CHECK(! touch_map(q, s.content_w, s.content_h, s.win_w + 50.0, s.win_h + 50.0, &cx, &cy));
            near(cx, s.content_w, 0.75, "drag out bottom-right x");
            near(cy, s.content_h, 0.75, "drag out bottom-right y");
        }
    }

    /* G. 同一个尺寸重复取矩形必须一样（旋转 / 折叠来回切时不能累计误差）| the same size must give the same rectangle */
    for (int i = 0; i < 8; ++i) {
        dm_vp_rect a = dm_vp_fit(2350, 1520, 2350, 1322), b = dm_vp_fit(2350, 1520, 2350, 1322);
        CHECK(a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h);
    }
    {   /* 16:9 的内容放进 2350x1520：宽先满，高取 1321，上下 99 / 100 | 16:9 content in 2350x1520: width fills, height 1321, 99 above and 100 below */
        dm_vp_rect c = dm_vp_fit(2350, 1520, 2000, 1125);
        CHECK(c.x == 0 && c.w == 2350 && c.h == 1321 && c.y == 99 && 1520 - (c.y + c.h) == 100);
    }

    /* B141：模拟 Fold8 —— setBuffersGeometry 回 0，但读回的永远是窗口尺寸。present 的每帧逻辑（dm_vp_native_on →
       dm_vp_geometry_request → dm_vp_geom_note）300 帧里只能请求一次；被拒绝的请求（回 -22）每帧都重试；
       折到另一种比例（外屏）时发一次「回到窗口尺寸」，之后不再请求；再展开回内屏又只请求一次。
       B141: a simulated Fold8 - setBuffersGeometry returns 0 but the read-back is always the window size. Present's per-frame
       logic (dm_vp_native_on -> dm_vp_geometry_request -> dm_vp_geom_note) may request only once in 300 frames; a refused
       request (-22) is retried every frame; folding to another aspect (outer screen) sends one "back to the window size" and
       then nothing; unfolding to the inner screen requests once again. */
    {
        struct { int win_w, win_h, cw, ch, result, frames, want; } ph[] = {
            { 2448, 1848, 1924, 1452,   0, 300, 1 },   /* 内屏、驱动接受 | inner, accepted */
            { 2520, 1080, 1924, 1452,   0,  50, 1 },   /* 外屏、出图还没跟上：回窗口尺寸一次 | outer, output not caught up: back to window once */
            { 2520, 1080, 2520, 1080,   0,  50, 0 },   /* 外屏出图 = 窗口：不请求 | outer output = window: no request */
            { 2448, 1848, 1924, 1452,   0,  50, 1 },   /* 再展开 | unfolded again */
        };
        dm_vp_geom_memo memo = {0};
        for (unsigned p = 0; p < sizeof ph / sizeof *ph; p++) {
            int requests = 0;
            for (int f = 0; f < ph[p].frames; f++) {
                int on = dm_vp_native_on(ph[p].win_w, ph[p].win_h, ph[p].cw, ph[p].ch, ph[p].win_w, ph[p].win_h, &memo), rw, rh;
                if (dm_vp_geometry_request(1, ph[p].win_w, ph[p].win_h, ph[p].cw, ph[p].ch, on, &rw, &rh)) { requests++; dm_vp_geom_note(&memo, ph[p].result, rw, rh, ph[p].win_w, ph[p].win_h); }
            }
            if (requests != ph[p].want) fprintf(stderr, "  B141 phase %u: %d requests, want %d\n", p, requests, ph[p].want);
            CHECK(requests == ph[p].want);
        }
        dm_vp_geom_memo m2 = {0}; int requests = 0;
        for (int f = 0; f < 20; f++) { int on = dm_vp_native_on(2448, 1848, 1924, 1452, 2448, 1848, &m2), rw, rh;
            if (dm_vp_geometry_request(1, 2448, 1848, 1924, 1452, on, &rw, &rh)) { requests++; dm_vp_geom_note(&m2, -22, rw, rh, 2448, 1848); } }
        CHECK(requests == 20);   /* 被拒绝：每帧重试，不冒充成功 | refused: retried every frame, never passed off as success */
        /* 接受过之后折到外屏，「回到窗口尺寸」被拒绝：每帧重试，直到被接受，之后不再请求。
           Accepted, then folded to the outer screen where "back to the window size" is refused: retried every frame until
           accepted, then no more requests. */
        { dm_vp_geom_memo m3 = {0}; int rw, rh, n = 0;
          if (dm_vp_geometry_request(1, 2448, 1848, 1924, 1452, dm_vp_native_on(2448, 1848, 1924, 1452, 2448, 1848, &m3), &rw, &rh)) dm_vp_geom_note(&m3, 0, rw, rh, 2448, 1848);
          for (int f = 0; f < 10; f++) { int on = dm_vp_native_on(2520, 1080, 1924, 1452, 2520, 1080, &m3);
              if (dm_vp_geometry_request(1, 2520, 1080, 1924, 1452, on, &rw, &rh)) { n++; CHECK(rw == 0 && rh == 0); dm_vp_geom_note(&m3, f < 5 ? -22 : 0, rw, rh, 2520, 1080); } }
          CHECK(n == 6); }
        /* 渲染分辨率档只改了高（宽相同）：记账不能算命中，要重新请求一次（DeepSeek 复跑存活变异 &&→||）。
           The render level changed only the height (same width): the memo must not match, one new request (DeepSeek re-run
           survivor && -> ||). */
        { dm_vp_geom_memo m4 = {0}; int rw, rh, n = 0;
          if (dm_vp_geometry_request(1, 2448, 1848, 1924, 1452, dm_vp_native_on(2448, 1848, 1924, 1452, 2448, 1848, &m4), &rw, &rh)) dm_vp_geom_note(&m4, 0, rw, rh, 2448, 1848);
          for (int f = 0; f < 10; f++) { int on = dm_vp_native_on(2448, 1848, 1924, 1446, 2448, 1848, &m4);
              if (dm_vp_geometry_request(1, 2448, 1848, 1924, 1446, on, &rw, &rh)) { n++; CHECK(rw == 1924 && rh == 1446); dm_vp_geom_note(&m4, 0, rw, rh, 2448, 1848); } }
          CHECK(n == 1); }
        /* 交叉审 P1：surface 换了（切后台回来）记账清掉，新窗口再请求一次；同一块 surface 不清。
           Cross-review P1: a new surface (back from the background) clears the memo and the new window is asked once more;
           the same surface keeps it. */
        { dm_vp_geom_memo m5 = {0}; int rw, rh, n = 0;
          for (int f = 0; f < 20; f++) { unsigned long long gen = f < 10 ? 1 : 2; dm_vp_geom_rebind(&m5, gen);
              int buf_w = (f < 10 && n) || (f >= 10 && n > 1) ? 1924 : 2448, buf_h = buf_w == 1924 ? 1452 : 1848;   /* 读回正常的手机 | a phone whose read-back is correct */
              int on = dm_vp_native_on(buf_w, buf_h, 1924, 1452, 2448, 1848, &m5);
              if (dm_vp_geometry_request(1, 2448, 1848, 1924, 1452, on, &rw, &rh)) { n++; dm_vp_geom_note(&m5, 0, rw, rh, 2448, 1848); } }
          CHECK(n == 2); }
        CHECK(dm_vp_native_on(1924, 1452, 1924, 1452, 2448, 1848, NULL) == 1);   /* 读回就是出图：照旧算生效 | read-back equals the output: in effect as before */
    }

    /* B140 增量审 P1-1（模型 sim_fold.c / sim2.c）：同一块 Surface 上从「比例相同、present_native」切到「比例不同」且出图尺寸同时变了
       （Fold8 内屏 1924x1602 → 折到外屏 2316x904、出图 1608x904；configChanges 带 screenSize，只有 surfaceChanged，++generation）。
       缓冲几何必须回到窗口尺寸：恰好发一次 (0,0)，之后不再请求；再展开回内屏又只请求一次出图尺寸。
       读回分两种机器：Fold8 型（读回恒为窗口尺寸）和读回正常的（读回 = 实际缓冲）；每种都分「换代」与「不换代」。
       B140 incremental review P1-1 (models sim_fold.c / sim2.c): on the same Surface, going from "same aspect, present_native" to "other
       aspect" while the output size changes too (Fold8 inner 1924x1602 -> fold to outer 2316x904, output 1608x904; configChanges has
       screenSize, so only surfaceChanged, ++generation). The buffer geometry must return to the window size: exactly one (0,0), then
       nothing; unfolding again asks once for the output size. Two read-back kinds (Fold8-like: always the window; correct: the real
       buffer), each with and without a generation change. */
    for (int readback_ok = 0; readback_ok < 2; readback_ok++)
        for (int regen = 0; regen < 2; regen++) {
            dm_vp_geom_memo m = {0}; int user_w = 0, user_h = 0;   /* 窗口上真实的缓冲几何（0 = 窗口尺寸），跨 surfaceChanged 保留 | the real buffer geometry on the window (0 = window size), kept across surfaceChanged */
            unsigned long long gen = 1; int win_w = 2176, win_h = 1812, cw = 1924, ch = 1602, inner = 0, outer = 0, outer_zero = 0, again = 0;
            for (int f = 0; f < 90; f++) {
                if (f == 30) { gen += regen; win_w = 2316; win_h = 904; cw = 1608; ch = 904; }      /* 折到外屏 | fold to the outer screen */
                if (f == 60) { gen += regen; win_w = 2176; win_h = 1812; cw = 1924; ch = 1602; }    /* 再展开 | unfold again */
                int bw = readback_ok && user_w ? user_w : win_w, bh = readback_ok && user_w ? user_h : win_h, rw, rh;
                if (dm_vp_geom_frame(&m, gen, 1, win_w, win_h, cw, ch, bw, bh, &rw, &rh)) {
                    if (f < 30) inner++; else if (f < 60) { outer++; outer_zero += rw == 0 && rh == 0; } else again++;
                    user_w = rw; user_h = rh; dm_vp_geom_note(&m, 0, rw, rh, win_w, win_h);
                }
                if (f == 59 && (user_w || user_h)) fprintf(stderr, "  P1-1 readback_ok=%d regen=%d: outer screen buffer stuck at %dx%d\n", readback_ok, regen, user_w, user_h);
                if (f == 59) CHECK(user_w == 0 && user_h == 0);   /* 外屏上缓冲 = 窗口尺寸 | on the outer screen the buffer is the window size */
            }
            CHECK(inner == 1); CHECK(outer == 1 && outer_zero == 1); CHECK(again == 1);
            CHECK(user_w == 1924 && user_h == 1602);
        }
    /* 同一块 surface、不换代，只是渲染分辨率档改了出图尺寸、比例也从相同变成不同（窗口不变）：同样只发一次 (0,0)。
       Same surface, no new generation, only the render level changed the output size and the aspect went from same to different (window
       unchanged): again exactly one (0,0). */
    { dm_vp_geom_memo m = {0}; int rw, rh, n = 0, zero = 0, user_w = 0;
      for (int f = 0; f < 40; f++) { int cw = f < 20 ? 1924 : 1600, ch = f < 20 ? 1452 : 900;
          if (dm_vp_geom_frame(&m, 7, 1, 2448, 1848, cw, ch, 2448, 1848, &rw, &rh)) { if (f >= 20) { n++; zero += rw == 0 && rh == 0; } user_w = rw; dm_vp_geom_note(&m, 0, rw, rh, 2448, 1848); } }
      CHECK(n == 1 && zero == 1 && user_w == 0); }
    /* 关掉 present_native（设置里切走）时也一样：曾经改过的缓冲几何退回窗口尺寸一次。
       Turning present_native off (from the settings) is the same: a buffer geometry changed earlier goes back to the window once. */
    { dm_vp_geom_memo m = {0}; int rw, rh, n = 0, user_w = 0;
      for (int f = 0; f < 40; f++) { int pn = f < 20, cw = f < 20 ? 1924 : 1600, ch = f < 20 ? 1452 : 900;
          if (dm_vp_geom_frame(&m, 3, pn, 2448, 1848, cw, ch, 2448, 1848, &rw, &rh)) { if (f >= 20) n++; user_w = rw; dm_vp_geom_note(&m, 0, rw, rh, 2448, 1848); } }
      CHECK(n == 1 && user_w == 0); }
    printf("viewport: %d/%d checks passed\n", checks - failures, checks);
    return failures != 0;
}
