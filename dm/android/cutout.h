/* 挖孔避让（B140 异形屏）：触屏控件布局（dm_tc_layout_make，iOS 与安卓共用）只认左右 / 底三个内缩，
   右上角的地图钮固定在「宽 − 30k」。安卓横屏挖孔贴在长边的上角（反向横屏时就在右上角），刘海 / 分屏时还会出现在顶边，
   这时地图钮正好压在摄像头孔下面。这里按 DisplayCutout.getBoundingRects 给的真实矩形（已换到控件坐标、dp），
   把和任一矩形（四周外扩 pad）相交的控件挪到最近的不相交位置：先试左 / 右 / 下三个方向里位移最小、且整圈仍在控件区内的那个。
   只在安卓调用（iOS 不调，布局逐位不变）；没有挖孔矩形时什么都不改。纯函数，宿主测试直接编。
   Cutout avoidance (B140 odd screens): the touch layout (dm_tc_layout_make, shared by iOS and Android) only knows left /
   right / bottom insets and pins the map button at "width - 30k". A landscape Android hole punch sits at the top corner of
   a long side (top-right in reverse landscape), and notches / split windows put it on the top edge, right under the map
   button. Using the real rectangles from DisplayCutout.getBoundingRects (already in control coordinates, dp), every control
   that intersects one (grown by pad) moves to the nearest clear spot: of left / right / down, the smallest displacement that
   keeps the whole circle inside the control area. Android only (iOS never calls it, its layout is bit-identical); with no
   cutout rectangle nothing changes. Pure, compiled directly by the host tests. */
#pragma once
#include <math.h>

typedef struct { float x0, y0, x1, y1; } dm_cut_rect;

static inline int dm_cut_hits(float x, float y, float r, dm_cut_rect c, float pad) {
    float nx = x < c.x0 - pad ? c.x0 - pad : x > c.x1 + pad ? c.x1 + pad : x;
    float ny = y < c.y0 - pad ? c.y0 - pad : y > c.y1 + pad ? c.y1 + pad : y;
    return (x - nx) * (x - nx) + (y - ny) * (y - ny) < r * r;
}

/* 一个圆避开所有矩形，同时不压到别的控件（obs：其他控件的圆，gap：控件之间至少留的缝）；返回 1 = 挪过。
   候选：左 / 右 / 下三个方向，从「刚好贴着孔」开始，每次再多挪一个控件直径，最多 6 步，取第一个既不碰孔、也不碰别的控件、
   整圈还在区内的位置里位移最小的那个；都不行就留在原处（交叉审 P2：原来只看孔，矮窗口右上顶边有孔时地图钮会压到传送门）。
   Move one circle clear of every rectangle without landing on another control (obs: the other controls' circles, gap: the minimum
   space between controls); 1 = it moved. Candidates: left / right / down, starting just clear of the hole and stepping one more
   control diameter each time, up to 6 steps; the smallest displacement that touches no hole, no other control and stays inside the
   area wins; if none, it stays (cross-review P2: it used to look at the holes only, so in a short window with a top-right hole the
   map button landed on the portal). */
static inline int dm_cut_avoid_obs(float *x, float *y, float r, const dm_cut_rect *cuts, int n, float pad, float w, float h,
                                   const float *ox, const float *oy, const float *orad, int no, float gap) {
    int hit = 0;
    for (int i = 0; i < n; i++) if (dm_cut_hits(*x, *y, r, cuts[i], pad)) { hit = 1; break; }
    if (!hit) return 0;
    int best = 0; float bx = *x, by = *y, bd = 1e30f;
    for (int i = 0; i < n; i++) {
        dm_cut_rect c = cuts[i];
        if (!dm_cut_hits(*x, *y, r, c, pad)) continue;
        const float base[3][2] = { { c.x0 - pad - r, *y }, { c.x1 + pad + r, *y }, { *x, c.y1 + pad + r } }, dir[3][2] = { { -1, 0 }, { 1, 0 }, { 0, 1 } };
        for (int k = 0; k < 3; k++)
            for (int step = 0; step < 6; step++) {
                float cx = base[k][0] + dir[k][0] * step * (2 * r + gap), cy = base[k][1] + dir[k][1] * step * (2 * r + gap);
                if (cx - r < 0 || cx + r > w || cy - r < 0 || cy + r > h) break;
                int clear = 1;
                for (int j = 0; j < n && clear; j++) if (dm_cut_hits(cx, cy, r, cuts[j], pad)) clear = 0;
                for (int j = 0; j < no && clear; j++) { float dx = cx - ox[j], dy = cy - oy[j], rr = r + orad[j] + gap; if (dx * dx + dy * dy < rr * rr) clear = 0; }
                if (!clear) continue;
                float d = fabsf(cx - *x) + fabsf(cy - *y);
                if (d < bd) { bd = d; bx = cx; by = cy; best = 1; }
                break;
            }
    }
    if (best) { *x = bx; *y = by; }
    return best;
}

/* 只看孔（不看别的控件）的简写 | the holes-only shorthand */
static inline int dm_cut_avoid(float *x, float *y, float r, const dm_cut_rect *cuts, int n, float pad, float w, float h) {
    return dm_cut_avoid_obs(x, y, r, cuts, n, pad, w, h, 0, 0, 0, 0, 0);
}

/* 整套控件：逐个避让，别的控件（按当时的位置）当障碍。| A whole control set: each one in turn, the others (at their current spots) as obstacles. */
static inline int dm_cut_avoid_set(float *xs, float *ys, const float *rs, int count, const dm_cut_rect *cuts, int n, float pad, float w, float h, float gap) {
    int moved = 0; float ox[32], oy[32], orad[32];
    if (count > 33) count = 33;
    for (int i = 0; i < count; i++) {
        if (rs[i] <= 0) continue;
        int no = 0; for (int j = 0; j < count; j++) if (j != i && rs[j] > 0) { ox[no] = xs[j]; oy[no] = ys[j]; orad[no] = rs[j]; no++; }
        moved += dm_cut_avoid_obs(&xs[i], &ys[i], rs[i], cuts, n, pad, w, h, ox, oy, orad, no, gap);
    }
    return moved;
}
