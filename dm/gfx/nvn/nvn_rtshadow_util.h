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
// 三角带 → 三角列表：位置 k 的三角形偶数为 (k, k+1, k+2)、奇数为 (k+1, k, k+2)（绕序交替）；带索引的遇到重启值（16 位 0xFFFF、32 位 0xFFFFFFFF，Metal 带图元恒开重启）重新起算；
// 退化三角形丢掉（奇偶照常推进）。ib 为空 = 非索引（下标 0..n−1）；out 为空只数；out 至少 (n−2)·3 个 uint32；返回三角形数（审计分支 2fec943 的口径）
// strip → list: the triangle at position k is (k, k+1, k+2) when even and (k+1, k, k+2) when odd (alternating winding); an indexed strip restarts after the restart value (16-bit 0xFFFF, 32-bit 0xFFFFFFFF; Metal always enables restart for strips);
// 非索引四边形 → 三角形列表索引（七审 Q7-02）：与原版光栅化同一拓扑（nvn_mtl.m draw：每 4 个顶点 0,1,2,0,2,3，共 (n/4)·6 个索引，不足 4 个的尾巴不画）；
// 索引相对本次绘制的第一个顶点（first 由解码时的 vbase 处理）。o 为 NULL 只数三角形；返回三角形数
// Non-indexed quads → triangle-list indices (seventh review Q7-02): the same topology as the original rasterizer (nvn_mtl.m draw: 0,1,2,0,2,3 per 4 vertices, (n/4)·6 indices, a tail of fewer than 4 vertices is not drawn);
// indices are relative to the draw's first vertex (first is handled by vbase in the decode). With o NULL only the triangles are counted; returns the triangle count
static inline uint32_t rts_quad_expand(int n, uint32_t *o) {
    uint32_t q = n >= 4 ? (uint32_t)(n / 4) : 0;
    if (o) for (uint32_t i = 0; i < q; i++) { uint32_t b = 4 * i, *t = o + 6 * i; t[0] = b; t[1] = b + 1; t[2] = b + 2; t[3] = b; t[4] = b + 2; t[5] = b + 3; }
    return q * 2;
}
// degenerate triangles are dropped (parity still advances). NULL ib = non-indexed (indices 0..n−1); NULL out only counts; out holds at least (n−2)·3 uint32; returns the triangle count (the audit branch 2fec943 convention)
static inline uint32_t rts_strip_expand(const void *ib, int isz, int n, uint32_t *out) {
    uint32_t t = 0, w[3] = {0}, rs = isz == 2 ? 0xFFFFu : 0xFFFFFFFFu; int run = 0, par = 0;
    for (int k = 0; k < n; k++) {
        uint32_t v = !ib ? (uint32_t)k : isz == 2 ? ((const uint16_t *)ib)[k] : ((const uint32_t *)ib)[k];
        if (ib && v == rs) { run = 0; par = 0; continue; }
        w[0] = w[1]; w[1] = w[2]; w[2] = v;
        if (++run < 3) continue;
        if (w[0] != w[1] && w[1] != w[2] && w[0] != w[2]) { if (out) { uint32_t *o = out + t * 3; if (par) { o[0] = w[1]; o[1] = w[0]; } else { o[0] = w[0]; o[1] = w[1]; } o[2] = w[2]; } t++; }
        par ^= 1;
    }
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
// 档位 → 实际参数（七审三轮。用户：「不能开了等于没开啊！」——每一档都要看得见光追，暂停不在自动降档序列里）：
// 0 = 全开（按设置：太阳全分辨率、点光、光追 AO、镂空判定）；1 = 太阳全分辨率 + 点光 ≤ 2 盏 1/4 分辨率 + 关 AO；
// 2 = 太阳半分辨率 + 点光 1 盏（最近最亮、排除随身光）；3 = 太阳半分辨率 + 关点光 + 静态镂空遮挡体（树叶、草）照原版画进阴影图、不跑判定（人物 / 怪的抖动淡出也是镂空：蒙皮的照样光追，但不判定、按不透明剪影，免得森林里成群的怪把判定的开销又带回来）
// （判定是森林的大头：Mac 森林光追 3.6 → 0.6 ms；人物 / 怪 / 不透明物件照样光追）。
// 「暂停」（原版阴影）只给 GPU 挂起保护用（rts_lvl_params.off，由调用方置），太阳 1/4 分辨率那档去掉了（石墙脚下锯齿爬动，真机 79eaa3f 用户录屏）
// Level → effective parameters (seventh review round 3. The user: "turning it on must not amount to turning nothing on" - every level shows ray tracing, and pausing is not part of the automatic sequence):
// 0 = everything (as configured: full-resolution sun, point lights, ray-traced AO, alpha judge); 1 = full-resolution sun + at most 2 point lights at quarter resolution + AO off;
// 2 = half-resolution sun + 1 point light (closest / brightest, the character's own light excluded); 3 = half-resolution sun + point lights off + alpha-tested occluders (leaves, grass) drawn into the shadow map as original, no judge
// (the judge is the forest's main cost: Mac forest ray tracing 3.6 → 0.6 ms; characters / monsters / opaque objects stay ray traced).
// "Pause" (original shadows) is only for the GPU-hang protection (rts_lvl_params.off, set by the caller); the quarter-resolution sun level is gone (crawling stair steps along the stone wall, user recording on the device with 79eaa3f)
#define RTS_LVL_MAX 3
typedef struct { int point_on, point_n, point_div, sun_div, soft, ao, off, no_alpha, excl_player; } rts_lvl_params;
static inline rts_lvl_params rts_lvl_apply(int lvl, int point_on, int point_n, int point_div, int sun_half, int soft, int ao) {
    rts_lvl_params p = {point_on, point_n, point_div, sun_half ? 2 : 1, soft, ao, 0, 0, 0};
    if (lvl >= 1) { if (p.point_n > 2) p.point_n = 2; p.point_div = 4; p.ao = 0; }
    if (lvl >= 2) { p.sun_div = 2; p.soft = 0; if (p.point_n > 1) p.point_n = 1; p.excl_player = 1; }
    if (lvl >= 3) { p.point_on = 0; p.no_alpha = 1; }
    return p;
}

// ---- 光追内存（2026-10-28 真机内存 P1 之后） | ray-tracing memory (after the 2026-10-28 device memory P1) ----
// 按渲染尺寸 w×h 与某档参数估各项常驻字节（R8 / RGBA8 贴图按像素算，不含驱动对齐）：全亮遮罩 + 太阳遮罩（R8 全分辨率，_rt 变体按像素坐标读，必须盖住目标，不能缩）、
// 降分辨率太阳遮罩（sun_div > 1）、软阴影两张 RG8、点光遮罩（RGBA8 1/point_div）及其全分辨率上采样版（RGBA8）、AO 三张（半分辨率 R8 两张 + 全分辨率 R8）、网格缓存上限、
// 固定部分（TLAS + 暂存 + 每帧槽，调用方给）
// Estimated resident bytes per item for render size w×h and one level's parameters (R8 / RGBA8 textures by pixel count, without driver alignment): lit mask + sun mask (full-resolution R8; the _rt variants read them by pixel coordinates, so they must cover the target and cannot shrink),
// reduced-resolution sun mask (sun_div > 1), the two RG8 soft-shadow textures, the point mask (RGBA8 at 1/point_div) and its full-resolution upsampled copy (RGBA8), the three AO textures (two half-resolution R8 + one full-resolution R8), the mesh cache cap,
// and the fixed part (TLAS + scratch + per-frame slots, given by the caller)
typedef struct { uint64_t masks, point, ao, mesh, fixed; } rts_mem_est;
static inline uint64_t rts_mem_est_total(const rts_mem_est *e) { return e->masks + e->point + e->ao + e->mesh + e->fixed; }
static inline rts_mem_est rts_mem_estimate(uint64_t w, uint64_t h, rts_lvl_params p, uint64_t mesh_cap, uint64_t fixed) {
    rts_mem_est e = {0, 0, 0, mesh_cap, fixed}; uint64_t full = w * h;
    e.masks = 2 * full;   // 全亮遮罩 + 太阳遮罩 | lit mask + sun mask
    if (!p.soft && p.sun_div > 1) e.masks += ((w + p.sun_div - 1) / p.sun_div) * ((h + p.sun_div - 1) / p.sun_div);
    if (p.soft) e.masks += 2 * 2 * full;
    if (p.point_on) { uint64_t d = (uint64_t)(p.point_div ? p.point_div : 2); e.point = ((w + d - 1) / d) * ((h + d - 1) / d) * 4 + full * 4; }
    if (p.ao) e.ao = 2 * ((w + 1) / 2) * ((h + 1) / 2) + full;
    return e;
}
// 启动预算：离系统内存上限（os_proc_available_memory）的 1/8，夹在 [48, 192] MB；不知道（Mac、读不到）= 不限
// Start-up budget: 1/8 of the distance to the system memory limit (os_proc_available_memory), clamped to [48, 192] MB; unknown (Mac, unreadable) = unlimited
#define RTS_MEM_BUDGET_MIN (48ull << 20)
#define RTS_MEM_BUDGET_MAX (192ull << 20)
#define RTS_MESH_CAP_MIN (16ull << 20)
static inline uint64_t rts_mem_budget(uint64_t avail) { if (!avail) return UINT64_MAX; uint64_t b = avail / 8; return b < RTS_MEM_BUDGET_MIN ? RTS_MEM_BUDGET_MIN : b > RTS_MEM_BUDGET_MAX ? RTS_MEM_BUDGET_MAX : b; }
// 按预算定方案：缺省档（lvl 0）估算放得下就什么都不动（不降缺省画质）；放不下依次：不建 AO → 网格缓存上限缩（到 16 MB 为止）→ 最低档抬到 3（关点光，放掉两张点光遮罩）
// The plan for a budget: if the default level (lvl 0) estimate fits, nothing changes (default quality is not lowered); otherwise in order: no AO → shrink the mesh cache cap (down to 16 MB) → raise the floor level to 3 (point lights off, both point masks released)
typedef struct { int ao_off, lvl_floor; uint64_t mesh_cap, est; } rts_mem_plan;
static inline rts_mem_plan rts_mem_plan_for(uint64_t budget, uint64_t w, uint64_t h, rts_lvl_params p0, rts_lvl_params p3, uint64_t mesh_cap, uint64_t fixed) {
    rts_mem_plan r = {0, 0, mesh_cap, 0}; rts_mem_est e = rts_mem_estimate(w, h, p0, mesh_cap, fixed); r.est = rts_mem_est_total(&e);
    if (r.est <= budget) return r;
    if (e.ao) { r.ao_off = 1; r.est -= e.ao; e.ao = 0; if (r.est <= budget) return r; }
    uint64_t over = r.est - budget, room = mesh_cap > RTS_MESH_CAP_MIN ? mesh_cap - RTS_MESH_CAP_MIN : 0, cut = over < room ? over : room;
    r.mesh_cap = mesh_cap - cut; r.est -= cut; if (r.est <= budget) return r;
    rts_mem_est e3 = rts_mem_estimate(w, h, p3, r.mesh_cap, fixed); e3.ao = 0; r.lvl_floor = 3; r.est = rts_mem_est_total(&e3);
    return r;
}
// 系统内存告警降档（状态机，时间由调用方给）：每次告警最低档 = max(现档, 最低档) + 1；10 秒内连着第 2 次起 = 连续告警：关 AO、网格缓存上限减半、直接到最低档（第 3 档）。
// 恢复要迟滞：最后一次告警 60 秒后才放宽一档，之后每 30 秒再放一档，放到 0 时 AO / 网格缓存上限也恢复；放宽只是允许，升不升档仍由自动降档按 GPU 余量定
// System memory-warning downgrade (a state machine, the caller gives the time): each warning sets the floor = max(current level, floor) + 1; from the 2nd warning within 10 s = consecutive warnings: AO off, mesh cache cap halved, straight to the lowest level (3).
// Recovery has hysteresis: one level is loosened only 60 s after the last warning, then one more every 30 s; at 0 AO / the mesh cache cap come back too. Loosening only allows it: whether to step up is still decided by the automatic downgrade from GPU headroom
#define RTS_MEMW_STREAK_S 10.0
#define RTS_MEMW_HOLD_S 60.0
#define RTS_MEMW_STEP_S 30.0
typedef struct { int floor, streak, ao_off, mesh_half; uint64_t n; double t_last, t_step; } rts_memw;
static inline int rts_memw_warn(rts_memw *m, int cur_lvl, int max_lvl, double now) {   // 返回新的最低档 | returns the new floor
    m->streak = m->n && now - m->t_last < RTS_MEMW_STREAK_S ? m->streak + 1 : 1; m->n++; m->t_last = m->t_step = now;
    int base = cur_lvl > m->floor ? cur_lvl : m->floor; m->floor = base + 1 > max_lvl ? max_lvl : base + 1;
    if (m->streak >= 2) { m->floor = max_lvl; m->ao_off = 1; m->mesh_half = 1; }
    return m->floor;
}
static inline int rts_memw_tick(rts_memw *m, double now) {   // 放宽了一档返回 1 | returns 1 when one level was loosened
    if (m->floor <= 0 || now - m->t_last < RTS_MEMW_HOLD_S || now - m->t_step < (m->t_step > m->t_last ? RTS_MEMW_STEP_S : RTS_MEMW_HOLD_S)) return 0;
    m->floor--; m->t_step = now; if (!m->floor) { m->ao_off = 0; m->mesh_half = 0; m->streak = 0; }
    return 1;
}
