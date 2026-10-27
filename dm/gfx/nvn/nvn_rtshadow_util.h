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
// 自动降档（光追 GPU 耗时 × 整帧 GPU 时间）：每帧喂一次「光追计算通道 GPU 起止跨度」与「整帧 GPU 时间」（未知传 < 0），按出图节拍的预算判。
// 降：整帧 > 92% 预算且光追 > 12% 预算（光追真是负担）；整帧不知道时光追单独 > 50% 预算；持续 1 秒 → 降一档（整帧有余量时光追再贵也不降）；两次换档至少隔 1 秒。
// 升：整帧 < 70% 预算（整帧不知道时光追 < 20%），持续 hold_up 秒（起始 8 秒）→ 升一档；升上去 5 秒内又被迫降回来，hold_up 翻倍（≤ 120 秒），免得来回跳。
// 返回 +1 = 降了一档（lvl 加一）、−1 = 升了一档、0 = 不变；why 写原因（给日志）
// Automatic downgrade (ray-tracing GPU cost × whole-frame GPU time): fed once per frame with the span of the ray-tracing compute pass and the frame's GPU time (< 0 when unknown), judged against the present-rate budget.
// Down: frame > 92% of the budget with ray tracing > 12% (it really is a burden); with the frame time unknown, ray tracing alone > 50%; sustained for 1 s → one level down (no step down while the frame has headroom, however costly ray tracing is); at least 1 s between changes.
// Up: frame < 70% of the budget (ray tracing < 20% when the frame time is unknown), sustained for hold_up seconds (8 s at first) → one level up; forced back down within 5 s of a step up doubles hold_up (≤ 120 s) to avoid oscillation.
// Returns +1 = one level down (lvl + 1), −1 = one level up, 0 = unchanged; why receives the reason (for the log)
typedef struct { int lvl, max_lvl, n; double rt, fr, t_change, t_over, t_under, hold_up, t_up; } rts_gov;
static inline void rts_gov_init(rts_gov *g, int max_lvl) { *g = (rts_gov){.max_lvl = max_lvl, .hold_up = 8, .t_over = -1, .t_under = -1, .t_change = -1e9, .t_up = -1e9, .fr = -1}; }
static inline int rts_gov_step(rts_gov *g, double rt_ms, double fr_ms, double budget_ms, double now, const char **why) {
    if (!(rt_ms >= 0) || !(budget_ms > 0)) return 0;
    const double a = 0.1; g->rt = g->n ? g->rt + a * (rt_ms - g->rt) : rt_ms;
    if (fr_ms > 0) g->fr = g->fr > 0 ? g->fr + a * (fr_ms - g->fr) : fr_ms;
    if (++g->n < 30) return 0;   // 先攒 30 帧 | warm up over 30 frames first
    int fk = g->fr > 0;
    int over = fk ? g->fr > 0.92 * budget_ms && g->rt > 0.12 * budget_ms : g->rt > 0.5 * budget_ms;
    int under = fk ? g->fr < 0.70 * budget_ms : g->rt < 0.20 * budget_ms;
    g->t_over = over ? (g->t_over < 0 ? now : g->t_over) : -1;
    g->t_under = under && !over ? (g->t_under < 0 ? now : g->t_under) : -1;
    if (now - g->t_change < 1.0) return 0;
    if (over && g->lvl < g->max_lvl && now - g->t_over >= 1.0) {
        if (now - g->t_up < 5.0) g->hold_up = g->hold_up * 2 > 120 ? 120 : g->hold_up * 2;
        g->lvl++; g->t_change = now; g->t_over = g->t_under = -1;
        if (why) *why = fk ? "整帧 GPU 超预算且光追占比高" : "整帧时间不知道、光追单独超过半帧预算";
        return 1; }
    if (under && g->lvl > 0 && now - g->t_under >= g->hold_up) {
        g->lvl--; g->t_change = g->t_up = now; g->t_over = g->t_under = -1;
        if (why) *why = fk ? "整帧 GPU 有余量" : "光追耗时降下来了";
        return -1; }
    return 0;
}
// 档位 → 实际参数：0 = 按设置；1 = 点光最多 1 盏、点光射线 1/4、关光追 AO（实验性）；2 = 再关点光阴影、太阳强制半分辨率硬阴影；3 = 太阳 1/4 分辨率
// Level → effective parameters: 0 = as configured; 1 = at most 1 point light, quarter-resolution point rays, ray-traced AO (experimental) off; 2 = point-light shadows off as well, sun forced to half-resolution hard shadows; 3 = sun at quarter resolution
#define RTS_LVL_MAX 3
typedef struct { int point_on, point_n, point_div, sun_div, soft, ao; } rts_lvl_params;
static inline rts_lvl_params rts_lvl_apply(int lvl, int point_on, int point_n, int point_div, int sun_half, int soft, int ao) {
    rts_lvl_params p = {point_on, point_n, point_div, sun_half ? 2 : 1, soft, ao};
    if (lvl >= 1) { if (p.point_n > 1) p.point_n = 1; p.point_div = 4; p.ao = 0; }
    if (lvl >= 2) { p.point_on = 0; p.sun_div = 2; p.soft = 0; }
    if (lvl >= 3) p.sun_div = 4;
    return p;
}
