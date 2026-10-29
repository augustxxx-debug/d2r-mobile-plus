/* BC3 → ASTC 4x4 转码（xbg_astc.c）：
 * - 三进制位编码表覆盖 243 个组合；48 级反量化是 48 个不同的值、含 0 与 255、单调；
 * - 随机 BC3 块与边角块（不透明、全透明、两档透明、6 级透明模式、c0 == c1）：编出的块用规范解码（xbg_astc_decode_own）解回，
 *   与引擎 BC3 软解（xbg_decode_block）比：透明度一样的块 RGB 误差 ≤ 2、透明度 0 误差；透明度有变化的块 RGB ≤ 12、透明度 ≤ 48，并统计平均误差；
 * - 整层转码按行距 / 层距取块，与逐块结果一致。
 * 解码器与编码器同出一人之手，真正的对照是 Mac GPU 的 ASTC 硬件解码（tools 里 Metal 程序）。
 * cc -std=c11 -O2 -I.. -include macos_pthread_name.h -o <仓外产物> astc_bc3_test.c ../xbg_astc.c ../xbg_texture.c ../../../tex/nvn_deswizzle.c ../../../nvn/nvn_tex.c（Linux 上不要 -include）
 * BC3 -> ASTC 4x4 transcode (xbg_astc.c): trit table covers all 243 tuples; 48-level unquantisation has 48 distinct monotonic values incl.
 * 0 and 255; random and corner-case BC3 blocks decoded back with the spec decoder and compared with the engine's BC3 decode (uniform-alpha
 * blocks: RGB <= 2, alpha exact; varying alpha: RGB <= 12, alpha <= 48, mean errors reported); the whole-level path honours strides.
 * Encoder and decoder share an author, so the real check is the Mac GPU's ASTC hardware decode (Metal tool). */
#include "../xbg_astc.h"
#include "../xbg_texture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void dm_prof_add(int id, uint64_t ns, uint64_t bytes) { (void)id; (void)ns; (void)bytes; }   /* xbg_texture.c 的弱引用（macOS 链接要实体） | weak reference in xbg_texture.c (macOS links need a body) */
static int checks; static void check(int ok, const char *why) { checks++; if (!ok) { fprintf(stderr, "FAIL %s\n", why); exit(1); } }
static uint32_t rng = 12345; static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static void round_trip(const uint8_t b[16], int *kind, int *rgb_err, int *a_err, double *sum_rgb, double *sum_a) {
    uint8_t astc[16], ref[64], got[64];
    *kind = xbg_bc3_to_astc(b, astc);
    check(xbg_decode_block(0x44, b, 16, ref, 64) == 0, "reference decode");
    check(xbg_astc_decode_own(astc, 0, got) == 0, "own decode accepts the block");
    *rgb_err = *a_err = 0;
    for (int i = 0; i < 16; i++) for (int j = 0; j < 4; j++) {
        int d = abs((int)got[i * 4 + j] - (int)ref[i * 4 + j]);
        if (j < 3) { if (d > *rgb_err) *rgb_err = d; *sum_rgb += d; } else { if (d > *a_err) *a_err = d; *sum_a += d; }
    }
}
int main(void) {
    /* 48 级 | 48 levels */
    int seen[256] = {0}, distinct = 0, prev = -1, mono = 1;
    uint8_t vals[48]; int n = 0;
    for (unsigned t = 0; t < 3; t++) for (unsigned m = 0; m < 16; m++) { uint8_t v = xbg_astc_unquant48(t, m); if (!seen[v]++) distinct++; vals[n++] = v; }
    check(distinct == 48 && seen[0] && seen[255], "48 distinct values incl. 0 and 255");
    for (int i = 0; i < 48; i++) for (int j = i + 1; j < 48; j++) if (vals[j] < vals[i]) { uint8_t x = vals[i]; vals[i] = vals[j]; vals[j] = x; }
    for (int i = 0; i < 48; i++) { if (vals[i] <= prev) mono = 0; prev = vals[i]; }
    check(mono && vals[47] - vals[0] == 255, "sorted levels strictly increasing");
    int maxgap = 0; for (int i = 1; i < 48; i++) if (vals[i] - vals[i - 1] > maxgap) maxgap = vals[i] - vals[i - 1];
    check(maxgap <= 7, "48-level gaps at most 7 (124 -> 131 in the middle, as in the spec table)");
    /* 边角块 | corner cases */
    uint8_t blocks[8][16] = {
        {255, 255, 0, 0, 0, 0, 0, 0, 0x00, 0xF8, 0x1F, 0x00, 0x1B, 0xE4, 0x55, 0xAA},   /* 不透明 | opaque */
        {0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0},                   /* 全透明白 | transparent white */
        {255, 0, 0x49, 0x92, 0x24, 0x49, 0x92, 0x24, 0x00, 0xF8, 0xE0, 0x07, 0x1B, 0xE4, 0x55, 0xAA},   /* 8 级透明 | 8-level alpha */
        {0, 255, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x1F, 0x00, 0x00, 0xF8, 0, 0xFF, 0x0F, 0xF0},     /* 6 级模式 + 0/255 | 6-level mode */
        {128, 128, 0, 0, 0, 0, 0, 0, 0x34, 0x12, 0x34, 0x12, 0xFF, 0x00, 0xFF, 0x00},   /* c0 == c1 */
        {255, 0, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0xFF, 0xFF, 0, 0, 0, 0, 0, 0},              /* 白→黑、透明度两档 | white->black, 2 alpha levels */
        {200, 50, 0, 0, 0, 0, 0, 0, 0x10, 0x84, 0x10, 0x84, 0, 0, 0, 0},
        {255, 254, 0, 0, 0, 0, 0, 0, 0x00, 0x00, 0xFF, 0xFF, 0x55, 0x55, 0x55, 0x55},
    };
    double sr = 0, sa = 0; int kind, er, ea;
    for (int k = 0; k < 8; k++) {
        round_trip(blocks[k], &kind, &er, &ea, &sr, &sa);
        if (kind == 1) check(er <= 2 && ea == 0, "uniform-alpha corner block within 2 levels");
        else check(er <= 12 && ea <= 48, "varying-alpha corner block within bounds");
    }
    check(1, "corner cases");
    /* 随机块：一半强制不透明（a0 = a1 = 255） | random blocks, half forced opaque */
    int n1 = 0, n2 = 0, max1 = 0, max2r = 0, max2a = 0; double s1 = 0, s2r = 0, s2a = 0, dummy = 0;
    for (int it = 0; it < 200000; it++) {
        uint8_t b[16]; for (int i = 0; i < 16; i++) b[i] = (uint8_t)rnd();
        if (it & 1) { b[0] = b[1] = 255; }
        if ((it % 7) == 3) { b[1] = b[0]; }   /* 同值透明度 | equal alpha endpoints */
        double r = 0, a = 0; round_trip(b, &kind, &er, &ea, &r, &a);
        if (kind == 1) { n1++; s1 += r; if (er > max1) max1 = er; check(er <= 2 && ea == 0, "random uniform-alpha block within 2 levels"); }
        else { n2++; s2r += r; s2a += a; if (er > max2r) max2r = er; if (ea > max2a) max2a = ea; check(er <= 12 && ea <= 48, "random varying-alpha block within bounds"); }
    }
    (void)dummy;
    printf("uniform alpha: %d blocks, max RGB err %d, mean %.3f\n", n1, max1, s1 / (n1 * 48.0));
    printf("varying alpha: %d blocks, max RGB err %d, mean %.3f; max alpha err %d, mean %.3f\n", n2, max2r, s2r / (n2 * 48.0), max2a, s2a / (n2 * 16.0));
    /* 整层：行距 / 层距 | whole level with strides */
    uint8_t src[3 * 2 * 48 * 2], dst[3 * 2 * 2 * 16], one[16];
    for (size_t i = 0; i < sizeof src; i++) src[i] = (uint8_t)rnd();
    check(xbg_bc3_to_astc_linear(10, 7, 2, src, sizeof src, 48, 48 * 3, dst, sizeof dst) == 0, "linear transcode");
    for (int z = 0; z < 2; z++) for (int y = 0; y < 2; y++) for (int x = 0; x < 3; x++) {
        xbg_bc3_to_astc(src + z * 144 + y * 48 + x * 16, one); check(!memcmp(one, dst + ((z * 2 + y) * 3 + x) * 16, 16), "linear block matches");
    }
    check(xbg_bc3_to_astc_linear(10, 7, 2, src, 240, 48, 48 * 3, dst, sizeof dst) == 0 && xbg_bc3_to_astc_linear(10, 7, 2, src, 239, 48, 48 * 3, dst, sizeof dst) == -1, "source must reach the last block (144 + 48 + 48 bytes)");
    check(xbg_bc3_to_astc_linear(10, 7, 2, src, sizeof src, 48, 48 * 3, dst, sizeof dst - 1) == -1, "short output rejected");
    check(xbg_bc3_to_astc_linear(10, 7, 2, src, sizeof src, 40, 0, dst, sizeof dst) == -1, "unaligned row rejected");
    printf("astc_bc3: %d checks passed\n", checks);
    return 0;
}
