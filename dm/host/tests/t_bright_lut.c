// 3D 画面加亮调色表重写的 CPU 同式（B098）：Metal 核 lutk（dm/gfx/nvn/nvn_mtl.m）与 Vulkan xbg_bright_lut.comp 的高光保护。
// 用合成的单调色调曲线（ACES / Reinhard / 硬剪裁，各配伽马 2.2，可带一点跨通道调色）做原表，断言：k = 1 不改表；k = 1.5 / 2 / 3 / 4 时
// 灰阶与典型彩色曝光斜坡的输出亮度单调不减、不比原版暗、颜色只是原版与提亮后两色的同权混合、提亮后亮度 0.65 以下与旧公式相同；旧公式在 k = 4 下确实反转（哨兵）；
// 两个着色器源文件里的公式与这里一致。clang -O1 -I.. -o build/t_bright_lut t_bright_lut.c -lm；运行 build/t_bright_lut <nvn_mtl.m> <xbg_bright_lut.comp>
// CPU copy of the 3D brightening color-table rewrite (B098): the highlight protection of the Metal kernel lutk (dm/gfx/nvn/nvn_mtl.m) and Vulkan
// xbg_bright_lut.comp. With synthetic monotonic tone curves (ACES / Reinhard / hard clip, each with gamma 2.2, optionally a little cross-channel grading)
// as the original table it asserts: k = 1 leaves the table unchanged; at k = 1.5 / 2 / 3 / 4 the output luminance along gray and typical colored exposure
// ramps never decreases, is never darker than the original, the color is only an equal-weight mix of the original and brightened colors, and below a
// brightened luminance of 0.65 the result equals the old formula; the old
// formula really reverses at k = 4 (sentinel); the formula text in both shader sources matches this one.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int checks; static void check(int ok, const char *why) { checks++; if (!ok) { fprintf(stderr, "FAIL %s\n", why); exit(1); } }
static const float m1 = 0.1593017578125f, m2 = 78.84375f, c1 = 0.8359375f, c2 = 18.8515625f, c3 = 18.6875f;
static float pq(float v) { float p = powf(fabsf(v), m1); return powf((c1 + c2 * p) / (1.0f + c3 * p), m2); }
static float pq_inv(float u) { float q = powf(u, 1.0f / m2); return powf(fmaxf(q - c1, 0.0f) / fmaxf(c2 - c3 * q, 1e-6f), 1.0f / m1); }
static float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static float luma(const float *c) { return 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2]; }
enum { N = 32 };
static float lut[N][N][N][3];   // [b][g][r]：与 3D 贴图 (x = r, y = g, z = b) 同序 | same order as the 3D texture (x = r, y = g, z = b)
static int curve; static float expo, grade;
static float tone(float x) {   // 场景线性 → 显示线性 0–1 | scene linear -> display linear 0-1
    switch (curve) { case 0: { float y = x * (2.51f * x + 0.03f) / (x * (2.43f * x + 0.59f) + 0.14f); return clampf(y, 0, 1); }
                     case 1: return x / (1 + x);
                     default: return clampf(x, 0, 1); } }
static void build(void) {   // 原表：逐通道色调曲线 + 伽马 2.2，grade > 0 时把各通道往亮度拉一点（跨通道调色，仍单调） | original table: per-channel tone curve + gamma 2.2; grade > 0 pulls each channel a little toward luminance (cross-channel grading, still monotonic)
    for (int bz = 0; bz < N; bz++) for (int gy = 0; gy < N; gy++) for (int rx = 0; rx < N; rx++) {
        float u[3] = { (rx + 0.5f) / N, (gy + 0.5f) / N, (bz + 0.5f) / N }, c[3];
        for (int i = 0; i < 3; i++) c[i] = powf(tone(expo * pq_inv(u[i]) / 0.008f), 1 / 2.2f);
        float l = luma(c); for (int i = 0; i < 3; i++) lut[bz][gy][rx][i] = c[i] + grade * (l - c[i]); }
}
static void sample(const float *u, float *out) {   // 线性过滤、clamp_to_edge、归一化坐标（同 Metal sampler / Vulkan textureLod） | linear filter, clamp_to_edge, normalized coordinates (as the Metal sampler / Vulkan textureLod)
    float f[3]; int i0[3], i1[3];
    for (int a = 0; a < 3; a++) { float x = clampf(u[a] * N - 0.5f, 0, N - 1); i0[a] = (int)floorf(x); if (i0[a] > N - 2) i0[a] = N - 2; f[a] = x - i0[a]; i1[a] = i0[a] + 1; }
    for (int c = 0; c < 3; c++) { float s = 0;
        for (int dz = 0; dz < 2; dz++) for (int dy = 0; dy < 2; dy++) for (int dx = 0; dx < 2; dx++) {
            float w = (dx ? f[0] : 1 - f[0]) * (dy ? f[1] : 1 - f[1]) * (dz ? f[2] : 1 - f[2]);
            s += w * lut[dz ? i1[2] : i0[2]][dy ? i1[1] : i0[1]][dx ? i1[0] : i0[0]][c]; }
        out[c] = s; }
}
// 着色器同式：u 是这一格（或任意点）的 PQ 坐标，o 是原表在 u 的值 | the shader formula: u is the PQ coordinate of the cell (or any point), o the original table at u
static void rewrite(int old, float k, const float *u, const float *o, float *out) {
    float u2[3], b[3]; for (int i = 0; i < 3; i++) u2[i] = clampf(pq(k * pq_inv(u[i])), 0, 1);
    sample(u2, b);
    if (old) { float t = clampf((luma(b) - 0.65f) / 0.35f, 0, 1), s = t * t * (3 - 2 * t);
        for (int i = 0; i < 3; i++) out[i] = fmaxf(b[i] + (o[i] - b[i]) * s, o[i]); return; }
    float lo = luma(o), lb = luma(b);
    float h = lb <= 0.65f ? lb : 0.65f + 0.35f * (1.0f - expf((0.65f - lb) / 0.35f));
    float d = lb - lo, w = d > 1e-5f ? clampf((fmaxf(lo, h) - lo) / d, 0.0f, 1.0f) : 0.0f;
    for (int i = 0; i < 3; i++) out[i] = o[i] + (b[i] - o[i]) * w;
}
// 沿一条曝光斜坡（场景颜色 col × t）走，返回输出亮度从此前峰值的最大回落；同时核「不比原版暗」「同权混合」 | walk an exposure ramp (scene color col x t), return the largest drop of the output luminance below its running peak; also check "never darker" and "equal-weight mix"
// 提亮后亮度 < 0.65 且各通道 b ≥ o 时新旧公式应逐通道相同（暗处 / 野外白天的加亮不变）；容差 2e-4（不到 1/255 的 1/19）：亮度差 ≤ 1e-5 时新式取原值，
// 近黑格单通道最多差 1e-5 / 0.0722 ≈ 1.4e-4
// Where the brightened luminance < 0.65 and b >= o per channel the new and old forms must match per channel (brightening of dark areas / daytime wilderness
// unchanged); tolerance 2e-4 (under 1/19 of 1/255): when the luminance difference is <= 1e-5 the new form keeps the original, so a near-black cell can differ
// by at most 1e-5 / 0.0722 ~= 1.4e-4 in one channel
static int same_as_old_bad;
static float ramp(int old, float k, const float *col, int *darker, int *spliced) {
    float prev = -1, drop = 0; *darker = *spliced = 0;
    for (int s = 0; s <= 4000; s++) {
        float t = powf(10.0f, -9.0f + 9.0f * s / 4000.0f), u[3], o[3], r[3];
        for (int i = 0; i < 3; i++) u[i] = pq(clampf(t * col[i], 0, 1));
        sample(u, o); rewrite(old, k, u, o, r);
        float l = luma(r); if (l > prev) prev = l; else if (prev - l > drop) drop = prev - l;
        if (l < luma(o) - 1e-5f) (*darker)++;
        if (!old) { float b[3], u2[3]; for (int i = 0; i < 3; i++) u2[i] = clampf(pq(k * pq_inv(u[i])), 0, 1); sample(u2, b);
            float w = -1; for (int i = 0; i < 3; i++) if (fabsf(b[i] - o[i]) > 1e-4f) { float wi = (r[i] - o[i]) / (b[i] - o[i]); if (w < 0) w = wi; else if (fabsf(wi - w) > 1e-3f) (*spliced)++; }
            if (luma(b) < 0.649f && b[0] >= o[0] && b[1] >= o[1] && b[2] >= o[2]) { float r0[3]; rewrite(1, k, u, o, r0); for (int i = 0; i < 3; i++) if (fabsf(r0[i] - r[i]) > 2e-4f) same_as_old_bad++; } }
    }
    return drop;
}
static char *slurp(const char *p) { FILE *f = fopen(p, "rb"); if (!f) return NULL; fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f); char *s = malloc(n + 1); s[fread(s, 1, n, f)] = 0; fclose(f); return s; }
int main(int argc, char **argv) {
    static const float cols[][3] = { { 1, 1, 1 }, { 0.55f, 0.8f, 1.0f }, { 0.35f, 0.7f, 1.0f }, { 1.0f, 0.45f, 0.12f }, { 0.9f, 0.62f, 0.48f }, { 0.2f, 1.0f, 0.25f }, { 0.9f, 0.2f, 0.85f }, { 0.1f, 0.15f, 1.0f } };
    static const char *cn[] = { "gray", "ice", "ice-deep", "fire", "skin", "green", "magenta", "blue" };
    static const float ks[] = { 1.5f, 2.0f, 3.0f, 4.0f };
    static const float expos[] = { 60.5f, 0.65f, 0.109f };   // 让暗部 L(2x)/L(x) 接近 Mac 实测（B098 报告 mapping_113） | so that L(2x)/L(x) in the darks is close to the Mac measurement (B098 report mapping_113)
    float worst_old = 0;
    for (curve = 0; curve < 3; curve++) for (int gi = 0; gi < 2; gi++) {
        expo = expos[curve]; grade = gi ? 0.25f : 0; build();
        // k = 1：每一格都原样（宿主在 k ≤ 1.005 时也根本不派发） | k = 1: every cell unchanged (the host does not even dispatch at k <= 1.005)
        float maxd = 0;
        for (int bz = 0; bz < N; bz++) for (int gy = 0; gy < N; gy++) for (int rx = 0; rx < N; rx++) {
            float u[3] = { (rx + 0.5f) / N, (gy + 0.5f) / N, (bz + 0.5f) / N }, r[3]; const float *o = lut[bz][gy][rx];
            rewrite(0, 1.0f, u, o, r); for (int i = 0; i < 3; i++) if (fabsf(r[i] - o[i]) > maxd) maxd = fabsf(r[i] - o[i]); }
        char why[160]; snprintf(why, sizeof why, "curve %d grade %.2f: k = 1 leaves the table unchanged (max diff %g)", curve, grade, maxd); check(maxd < 2e-4f, why);
        for (unsigned ci = 0; ci < sizeof cols / sizeof *cols; ci++) for (unsigned ki = 0; ki < sizeof ks / sizeof *ks; ki++) {
            int dk, sp; float drop = ramp(0, ks[ki], cols[ci], &dk, &sp);
            snprintf(why, sizeof why, "curve %d grade %.2f %s k=%.1f: output luminance never decreases (drop %g)", curve, grade, cn[ci], ks[ki], drop); check(drop <= 1e-5f, why);
            snprintf(why, sizeof why, "curve %d grade %.2f %s k=%.1f: never darker than the original (%d samples)", curve, grade, cn[ci], ks[ki], dk); check(dk == 0, why);
            snprintf(why, sizeof why, "curve %d grade %.2f %s k=%.1f: one weight for all channels (%d spliced)", curve, grade, cn[ci], ks[ki], sp); check(sp == 0, why);
            if (ks[ki] == 4.0f) { float d2 = ramp(1, 4.0f, cols[ci], &dk, &sp); if (d2 > worst_old) worst_old = d2; }
        }
    }
    { char why[96]; snprintf(why, sizeof why, "below the 0.65 knee the new form equals the old one (%d mismatches)", same_as_old_bad); check(same_as_old_bad == 0, why); }
    // 哨兵：旧公式在 k = 4 至少一条斜坡上反转，说明这套斜坡抓得住 B098 | sentinel: the old formula reverses on at least one ramp at k = 4, so these ramps catch B098
    { char why[96]; snprintf(why, sizeof why, "old formula reverses at k = 4 (largest drop %g)", worst_old); check(worst_old > 1e-3f, why); }
    // 两个着色器源文件的公式与上面同式 | both shader sources carry the formula above
    static const char *must[] = { "0.65 + 0.35 * (1.0 - exp((0.65 - lb) / 0.35))", "d > 1e-5 ? clamp((max(lo, h) - lo) / d, 0.0, 1.0) : 0.0", "mix(o.rgb, b.rgb, w)", "0.2126, 0.7152, 0.0722" };
    for (int a = 1; a < argc; a++) { char *s = slurp(argv[a]); char why[200]; snprintf(why, sizeof why, "readable: %s", argv[a]); check(s != NULL, why);
        for (unsigned m = 0; m < sizeof must / sizeof *must; m++) { snprintf(why, sizeof why, "%s carries \"%s\"", argv[a], must[m]); check(strstr(s, must[m]) != NULL, why); }
        snprintf(why, sizeof why, "%s has no per-channel max(mix(...), o) any more", argv[a]); check(!strstr(s, "max(mix(b.rgb, o.rgb, s), o.rgb)"), why); free(s); }
    printf("PASS t_bright_lut: %d checks (old formula largest drop at k = 4: %.4f)\n", checks, worst_old); return 0;
}
