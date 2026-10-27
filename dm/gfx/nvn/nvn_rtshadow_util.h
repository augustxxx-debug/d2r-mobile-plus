// 光追阴影的纯计算部分（不碰 Metal 对象），nvn_rtshadow.inc 与单测 dm/gfx/tests/t_rtshadow.m 共用：4×4 求逆、三角带展开、点光挑选
// Pure computations of the ray-traced shadows (no Metal objects), shared by nvn_rtshadow.inc and the unit test dm/gfx/tests/t_rtshadow.m: 4×4 inverse, strip expansion, point-light picking
#pragma once
#include <stdint.h>
#include <math.h>
// 4×4 求逆（伴随矩阵法，双精度；列主序 / 行主序都一样，逆的布局跟输入一致） | 4×4 inverse (adjugate, double precision; layout-agnostic: the inverse has the input's layout)
static inline void rts_inv4(const double *m, double *o) {
    double inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    double det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12]; det = det != 0 ? 1.0 / det : 0;
    for (int i = 0; i < 16; i++) o[i] = inv[i] * det;
}
// 三角带 → 三角列表（退化三角形丢掉）；ib 为空 = 非索引（下标 0..n−1）；out 至少 (n−2)·3 个 uint32；返回三角形数 | strip → list (degenerates dropped); NULL ib = non-indexed; out holds at least (n−2)·3 uint32; returns the triangle count
static inline uint32_t rts_strip_expand(const void *ib, int isz, int n, uint32_t *out) {
    uint32_t t = 0;
    #define RTS_IDX(k) (ib ? (isz == 2 ? (uint32_t)((const uint16_t *)ib)[k] : ((const uint32_t *)ib)[k]) : (uint32_t)(k))
    for (int k = 2; k < n; k++) { uint32_t a = RTS_IDX(k - 2), b = RTS_IDX(k - 1), c = RTS_IDX(k); if (a == b || b == c || a == c) continue; out[t * 3] = a; out[t * 3 + 1] = b; out[t * 3 + 2] = c; t++; }
    #undef RTS_IDX
    return t;
}
// 点光挑选：记录 = 每盏 16 个 float（4 个 float4 texel：[0] 位置 xyz、[1] 颜色 rgb、…），ia = 相机视投影的逆（16 个 double，与 MSL float4x4 同为列主序）；
// 画面中心视线 = ia·(0,0,0,1) 到 ia·(0,0,1,1)；得分 = 最大颜色分量 / (到视线的距离² + 1e4)；取前 maxn（≤ 4），out_rec 为记录序号、out_best 为得分、p0 为视线起点
// Point-light picking: record = 16 floats per light (4 float4 texels: [0] position xyz, [1] color rgb, ...), ia = inverse camera view-projection (16 doubles, column-major like the MSL float4x4);
// screen-center view line = ia·(0,0,0,1) to ia·(0,0,1,1); score = max color component / (distance² to the line + 1e4); top maxn (≤ 4); out_rec = record indices, out_best = scores, p0 = line origin
static inline int rts_pick_lights_cpu(const float *L, uint32_t nrec, const double *ia, int maxn, int *out_rec, double *out_best, double *p0) {
    if (maxn > 4) maxn = 4; if (maxn < 1) return 0;
    double p1[3]; { double c0[4] = {0, 0, 0.5, 1}, c1[4] = {0, 0, 0.9, 1}, o0[4], o1[4];   // 同 rts_pick：深度 0.5 / 0.9 | same as rts_pick: depth 0.5 / 0.9
        for (int r = 0; r < 4; r++) { o0[r] = o1[r] = 0; for (int c = 0; c < 4; c++) { o0[r] += ia[c * 4 + r] * c0[c]; o1[r] += ia[c * 4 + r] * c1[c]; } }
        if (o0[3] == 0 || o1[3] == 0) return 0;
        for (int i = 0; i < 3; i++) { p0[i] = o0[i] / o0[3]; p1[i] = o1[i] / o1[3]; } }
    double dir[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]}; double dl = sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]); if (dl <= 0) return 0; for (int i = 0; i < 3; i++) dir[i] /= dl;
    double best[4] = {0, 0, 0, 0}; int bi[4] = {-1, -1, -1, -1};
    for (uint32_t r = 0; r < nrec; r++) { const float *rec = L + r * 16; float lum = fmaxf(rec[4], fmaxf(rec[5], rec[6]));
        if (!(lum > 0) || !isfinite(rec[0]) || !isfinite(rec[1]) || !isfinite(rec[2])) continue;
        double v[3] = {rec[0] - p0[0], rec[1] - p0[1], rec[2] - p0[2]}; double t = v[0] * dir[0] + v[1] * dir[1] + v[2] * dir[2]; double d2 = 0; for (int i = 0; i < 3; i++) { double q = v[i] - t * dir[i]; d2 += q * q; }
        double score = lum / (d2 + 1e4);
        int k = maxn; for (int i = 0; i < maxn; i++) if (bi[i] < 0 || score > best[i]) { k = i; break; }   // 插入排序（≤ 4） | insertion (≤ 4)
        if (k >= maxn) continue;
        for (int i = maxn - 1; i > k; i--) { best[i] = best[i - 1]; bi[i] = bi[i - 1]; } best[k] = score; bi[k] = (int)r; }
    int n = 0; for (int i = 0; i < maxn && bi[i] >= 0; i++) { out_rec[n] = bi[i]; out_best[n] = best[i]; n++; }
    return n;
}
// 玩家随身光识别：游戏给角色一圈「光照半径」点光，它跟着相机走——相对画面中心视线起点 p0 的偏移连续多帧不变、而 p0 本身在动。每帧喂入候选（得分最高的记录）与 p0；
// 返回 1 = 已认定为玩家光（rec 有效）。换候选、偏移跳变、相机静止都从头计；认定后只要候选还是它就一直成立
// Player-light recognition: the game gives the character a "light radius" point light that follows the camera - its offset from the screen-center view-line origin p0 stays constant over many frames while p0 itself moves.
// Feed the candidate (top-scored record) and p0 every frame; returns 1 = recognized as the player light (rec valid). A new candidate, an offset jump or a static camera restarts the count; once recognized it holds while the candidate stays the same
typedef struct { int rec, frames, sure; double off[3], p0[3], moved; } rts_player_track;
static inline int rts_player_feed(rts_player_track *t, int rec, const float *pos, const double *p0) {
    double off[3] = {pos[0] - p0[0], pos[1] - p0[1], pos[2] - p0[2]};
    if (rec < 0) { t->rec = -1; t->frames = 0; t->sure = 0; return 0; }
    if (rec != t->rec) { t->rec = rec; t->frames = 0; t->moved = 0; t->sure = 0; memcpy(t->off, off, sizeof off); memcpy(t->p0, p0, 3 * sizeof *p0); return 0; }
    double d = 0, m = 0; for (int i = 0; i < 3; i++) { double q = off[i] - t->off[i]; d += q * q; double c = p0[i] - t->p0[i]; m += c * c; }
    double tol = 0.5 + 0.02 * sqrt(t->off[0] * t->off[0] + t->off[1] * t->off[1] + t->off[2] * t->off[2]);   // 偏移允许抖 0.5 单位 + 2% | the offset may jitter by 0.5 units + 2%
    if (d > tol * tol) { t->frames = 0; t->moved = 0; t->sure = 0; memcpy(t->off, off, sizeof off); memcpy(t->p0, p0, 3 * sizeof *p0); return 0; }
    t->frames++; t->moved += sqrt(m); memcpy(t->p0, p0, 3 * sizeof *p0);
    if (!t->sure && t->frames >= 60 && t->moved > 4 * tol) t->sure = 1;   // 一秒以上偏移不变、相机累计走过的路远大于容差 | over a second of constant offset while the camera travelled well beyond the tolerance
    return t->sure;
}
// 太阳方向（指向太阳、y 朝上）：光源视投影（行向量，clip = p · L）下光线是「光源 x、y 不变」的方向 = 第 0 列 × 第 1 列（xyz 部分），对任何仿射（含剪切的）光源投影都成立。
// 正交的光源矩阵上它与第 2 列（深度梯度）同向；Mac 营地实测本作的级联矩阵正交、两种算法一致——曾怀疑过的「影子偏转」后来查明是导出参数块复用的错，与这里无关。返回 0 = 矩阵退化
// Sun direction (toward the sun, y up): with the light view-projection (row vector, clip = p · L) light rays run where light x and y stay constant = column 0 × column 1 (xyz parts), valid for any affine (even sheared) light projection.
// On an orthogonal light matrix it is parallel to column 2 (the depth gradient); measured at the camp on the Mac, this game's cascade matrices are orthogonal and both agree - the "rotated shadows" once suspected turned out to be the export argument-reuse bug, unrelated to this. Returns 0 = degenerate matrix
static inline int rts_sun_from_lvp(const float *L, float sun[3]) {
    double a[3] = {L[0], L[4], L[8]}, b[3] = {L[1], L[5], L[9]};
    double d[3] = {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
    double n = sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]); if (!(n > 0)) return 0;
    double s = (d[1] > 0 ? 1.0 : -1.0) / n; for (int i = 0; i < 3; i++) sun[i] = (float)(d[i] * s);
    return 1;
}
