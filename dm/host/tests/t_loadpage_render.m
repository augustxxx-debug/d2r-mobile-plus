// 加载页出图核对（Mac，离屏）：用 iOS 同一个 Core Graphics 画函数（ios/Sources/dm_loadpage_cg.m）把「导入 → 着色器缓存」走到 37% 的中间态渲染成 PNG，
// 五种屏：Duo 内屏 951×669 pt @3.3、iPhone 17 Pro 竖 402×874 pt @3（顶 59 / 底 34 安全区）与横 874×402（左右 59、底 21）、安卓 20:9 873×393 dp @2.75，
// 以及 iPad Pro 11 横 1194×834 / 竖 834×1194 pt（安全区上 24 / 下 20；B139 大屏按长边放大静态尺寸）。
// 背景是 App 图标美术（带 MOBILE PLUS），不画金色字标（用户 2026-10-29，与安卓同一个开关）：这里断言布局没有字标图、字标文字为空、背景图中心亮四角黑，
// 并把「开字标」多渲染一张对比：字标区的金色像素在正图里必须明显少于对比图，证明金色字标真的没画上去。
// 用法：t_loadpage_render <素材目录 dm/assets/loadpage> <输出目录> [语言 zh|en|es|ru|ko]。不开窗口、不起宿主、不截屏。
// Loading page render check (Mac, offscreen): the same Core Graphics draw function as iOS (ios/Sources/dm_loadpage_cg.m) renders the mid state "import → shader cache at 37%" to PNG
// for three screens: Duo inner 951×669 pt @3.3, iPhone 17 Pro portrait 402×874 pt @3 (59 top / 34 bottom safe area) and landscape 874×402 (59 sides, 21 bottom), Android 20:9 873×393 dp @2.75.
// The background is the App icon art (reads MOBILE PLUS) with no gold wordmark (user 2026-10-29, the same switch as Android): the layout must have no wordmark image, the text wordmark is
// empty and the background is bright in the centre and near black in the corners; one extra "wordmark on" render is compared so the gold pixels in the wordmark area of the real picture
// must be clearly fewer, proving the gold wordmark is not drawn.
// Usage: t_loadpage_render <asset dir dm/assets/loadpage> <output dir> [lang zh|en|es|ru|ko]. No window, no host, no screen capture.
#include "../dm_loadpage.h"
#include "../dm_i18n.h"
#import <CoreGraphics/CoreGraphics.h>
#import <ImageIO/ImageIO.h>
#import <Foundation/Foundation.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>
CGImageRef dm_loadpage_cg_image(const char *path);
int dm_loadpage_cg_png(const char *path, CGImageRef bg, CGImageRef logo, const dm_lp_screen *scr, double scale, uint64_t now_ms);
// 一张图的亮度（0..255）：画进 64×64 缩略图后取一块的平均；坐标是图内 0..1 | luminance of an image (0..255): mean of a box in a 64×64 thumbnail; coordinates 0..1 in the image
static double img_lum(CGImageRef img, double x0, double y0, double x1, double y1) {
    enum { N = 64 }; unsigned char px[N * N * 4]; memset(px, 0, sizeof px);
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef c = CGBitmapContextCreate(px, N, N, 8, N * 4, cs, kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    if (!c) { CGColorSpaceRelease(cs); return -1; }
    CGContextSetInterpolationQuality(c, kCGInterpolationHigh); CGContextDrawImage(c, CGRectMake(0, 0, N, N), img);
    CGContextRelease(c); CGColorSpaceRelease(cs);
    int a = (int)(x0 * N), b = (int)(y0 * N), e = (int)(x1 * N), f = (int)(y1 * N); double sum = 0; int n = 0;
    for (int y = b; y < f; y++) for (int x = a; x < e; x++) { unsigned char *p = px + (y * N + x) * 4; sum += 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2]; n++; }
    return n ? sum / n : -1;
}
// 读回 PNG 的像素（RGBA，行 0 = 图顶） | pixels of a PNG (RGBA, row 0 = image top)
static unsigned char *png_pixels(const char *path, size_t *W, size_t *H) {
    CGImageRef img = dm_loadpage_cg_image(path); if (!img) return NULL;
    *W = CGImageGetWidth(img); *H = CGImageGetHeight(img);
    unsigned char *buf = calloc(*W * *H * 4, 1); if (!buf) { CGImageRelease(img); return NULL; }
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef c = CGBitmapContextCreate(buf, *W, *H, 8, *W * 4, cs, kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    if (c) { CGContextDrawImage(c, CGRectMake(0, 0, *W, *H), img); CGContextRelease(c); }
    CGColorSpaceRelease(cs); CGImageRelease(img); return buf;
}
// 图四角与指定点的 RGB（0..255）；角点取内缩 2 像素（与视图 / 安卓同规则） | RGB (0..255) of the four corners and of a point; corners sit 2 px in (same rule as the view / Android)
static void corners_rgb(const unsigned char *px, size_t W, size_t H, int out[3]) {
    size_t xs[2] = { W > 4 ? 2 : 0, W > 4 ? W - 3 : W - 1 }, ys[2] = { H > 4 ? 2 : 0, H > 4 ? H - 3 : H - 1 };
    int sum[3] = { 0, 0, 0 };
    for (int a = 0; a < 2; a++) for (int b = 0; b < 2; b++) { const unsigned char *p = px + (ys[b] * W + xs[a]) * 4; for (int i = 0; i < 3; i++) sum[i] += p[i]; }
    for (int i = 0; i < 3; i++) out[i] = sum[i] / 4;
}
static void pixel_rgb(const unsigned char *px, size_t W, size_t H, size_t x, size_t y, int out[3]) {
    if (x >= W) x = W - 1; if (y >= H) y = H - 1;
    const unsigned char *p = px + (y * W + x) * 4; for (int i = 0; i < 3; i++) out[i] = p[i];
}
// P2-1 源码闸门：出图测试走的是画函数，绕开了 iOS 视图本身。直接读视图源码，钉住 initWithFrame 里「关字标 + 不解码字标图」这几行，
// 并把截图开关传的 logo 钉在视图的 self.logo 上——把它们改回去（审计变异 M1）这里就报红
// P2-1 source gate: the render test goes through the draw function and bypasses the iOS view itself. This reads the view source and pins the lines in initWithFrame
// (wordmark off + no wordmark decode) and the logo the screenshot switch passes, so restoring them (audit mutation M1) turns this red
static char *read_all(const char *path) {
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long sz = ftell(f); if (sz < 0) { fclose(f); return NULL; }
    rewind(f); char *b = malloc((size_t)sz + 1); if (!b) { fclose(f); return NULL; }
    size_t got = fread(b, 1, (size_t)sz, f); fclose(f); b[got] = 0; return b;
}
static int gate_ios_view(void) {
    const char *cand[] = { "../../ios/Sources/dm_loadpage_cg.m", "../ios/Sources/dm_loadpage_cg.m", "dm/ios/Sources/dm_loadpage_cg.m" };
    char *src = NULL; const char *used = NULL;
    for (unsigned i = 0; i < sizeof cand / sizeof *cand && !src; i++) { src = read_all(cand[i]); if (src) used = cand[i]; }
    if (!src) { fprintf(stderr, "【红】找不到 iOS 视图源码 dm_loadpage_cg.m（P2-1 闸门）\n"); return 1; }
    int fails = 0;
#define GATE(cond, msg) do { if (!(cond)) { fprintf(stderr, "【红】iOS 视图源码闸门（P2-1，%s）：%s\n", used, msg); fails++; } } while (0)
    char *a = strstr(src, "- (instancetype)initWithFrame"), *b = a ? strstr(a + 1, "\n- (") : NULL;
    GATE(a != NULL && b != NULL, "找不到完整的 initWithFrame（闸门失效，需同步改这里）");
    if (a && b) { char save = *b; *b = 0;
        GATE(strstr(a, "dm_loadpage_set_wordmark(0)") != NULL, "initWithFrame 里没有 dm_loadpage_set_wordmark(0)（iOS 会重新画出金色字标）");
        GATE(strstr(a, "self.logo = NULL") != NULL, "initWithFrame 里没有 self.logo = NULL（iOS 会解码字标图）");
        GATE(strstr(a, "dm_loadpage_logo_name") == NULL && strstr(a, "loadpage_logo") == NULL, "initWithFrame 又去取 / 解码字标图了");
        GATE(strstr(a, "self.logo = dm_loadpage_cg_image") == NULL, "initWithFrame 又在解码字标图（self.logo = dm_loadpage_cg_image）");
        *b = save; }
    char *v = strstr(src, "void dm_loadpage_view_shot"), *ve = v ? strstr(v + 1, "\n#endif") : NULL;
    GATE(v != NULL && ve != NULL, "找不到 dm_loadpage_view_shot（截图开关可能绕开了视图）");
    if (v && ve) { char save = *ve; *ve = 0;
        GATE(strstr(v, "p.logo") != NULL, "dm_loadpage_view_shot 没有把视图的 self.logo 传下去");
        GATE(strstr(v, "dm_loadpage_logo_name") == NULL && strstr(v, "loadpage_logo") == NULL, "dm_loadpage_view_shot 又去取字标图了");
        *ve = save; }
#undef GATE
    free(src);
    if (!fails) printf("  iOS 视图源码闸门（P2-1）：initWithFrame 关字标、不解码字标图 ✓\n");
    return fails;
}
// 字标框内的金色像素数（r ≥ 170、g ≥ 110、b ≤ 100，且红多于绿多于蓝）：金色字标会填满一大片，图标自身的暗铜字远少于它
// gold pixels inside the wordmark box (r ≥ 170, g ≥ 110, b ≤ 100, red > green > blue): a gold wordmark fills a large patch; the icon's own dark copper lettering is far sparser
static long gold_pixels(const unsigned char *px, size_t W, size_t H, dm_lp_rect box, double scale) {
    int x0 = (int)(box.x * scale), x1 = (int)((box.x + box.w) * scale), y0 = (int)(box.y * scale), y1 = (int)((box.y + box.h) * scale);
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 > (int)W) x1 = (int)W; if (y1 > (int)H) y1 = (int)H;
    long n = 0;
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) { const unsigned char *p = px + (y * W + x) * 4;   // 位图行 0 = 图顶（与布局同为 y 向下） | bitmap row 0 = image top (layout y goes down too)
        if (p[0] >= 170 && p[1] >= 110 && p[2] <= 100 && p[0] > p[1] + 20 && p[1] > p[2] + 20) n++; }
    return n;
}
int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "用法：%s <素材目录> <输出目录> [zh|en|es|ru|ko]\n", argv[0]); return 2; }
    const char *lang = argc > 3 ? argv[3] : "zh";
    setenv("DM_LANG", !strcmp(lang, "zh") ? "zh-Hans" : !strcmp(lang, "en") ? "en-US" : !strcmp(lang, "es") ? "es-ES" : !strcmp(lang, "ru") ? "ru-RU" : "ko-KR", 1); unsetenv("DM_SAVE");
    // 两段：导入（已完成）→ 着色器缓存 2612 / 7062，总进度 25% + 75% × 0.16 = 37%；第二阶段已走 40 秒，有「预计还要」 | two stages: import (done) → shaders 2612 / 7062, overall 25% + 75% × 0.16 = 37%; 40 s into stage two, so an ETA shows
    int kinds[] = { DM_LP_IMPORT, DM_LP_SHADERS }; int weights[] = { 90000, 270000 }; uint64_t t = 1000;   // 手机端生成那条路的权重 | weights of the on-phone generation path
    dm_loadpage_begin(kinds, 2, weights, t);
    dm_lp_detail d = { 0, 0, 0, 0 }; d.bytes_total = 690000000; d.bytes_done = 690000000; dm_loadpage_set(0, 1.0, &d, t + 60000);
    dm_lp_detail e = { 2612, 7062, 0, 0 }; dm_loadpage_set(1, 0.16, &e, t + 60000); dm_loadpage_set(1, 0.16, &e, t + 100000);
    uint64_t now = t + 100000;
    if (dm_loadpage_percent() != 37) { fprintf(stderr, "【红】总进度 %d%% ≠ 37%%\n", dm_loadpage_percent()); return 1; }
    dm_loadpage_set_wordmark(0);   // iOS 现状：PLUS 图标背景，不画金色字标 | iOS today: PLUS icon background, no gold wordmark
    struct { const char *name; dm_lp_screen s; double scale; } shots[] = {
        { "duo_inner_951x669", { 951, 669, 0, 0, 0, 0 }, 3142.0 / 951 },
        { "iphone17pro_port_402x874", { 402, 874, 0, 59, 0, 34 }, 3 },
        { "iphone17pro_land_874x402", { 874, 402, 59, 0, 59, 21 }, 3 },
        { "android_20x9_873x393", { 873, 393, 0, 0, 0, 0 }, 2400.0 / 873 },
        { "ipad_pro11_land_1194x834", { 1194, 834, 0, 24, 0, 20 }, 1 },   // B139：大屏按长边 1.194 放大静态尺寸 | large screen, static metrics ×1.194
        { "ipad_pro11_port_834x1194", { 834, 1194, 0, 24, 0, 20 }, 1 },
    };
    int fails = gate_ios_view(); char path[1024];
    // B139 返工根因守卫：iPad 的静态尺寸按安全内容高缩放——横屏 k = (834 − 20) / 500 = 1.628（24 → 39.07 pt），
    // 竖屏 k = (1194 − 20) / 840 = 1.3976（24 → 33.54 pt）。进度组不再贴底：横屏标题在 63.1% 屏高、条 71.8%（奥丁 62.2% / 71.7%）；
    // 竖屏 77.6% / 82.8%（17 Pro 竖 75.9% / 81.0%）。上一版按长边 k = 1.194 时是横屏 72.3% / 78.7%、竖屏 80.6% / 85.1%。
    // 安卓参照（奥丁 832.5×468.3、Fold8 934.2×841.7、出图的 873×393）长边都 ≤ 1000，字号仍是 24 pt、矩形逐位不变。
    // B139 rework root-cause gate: the iPad's static metrics scale with the safe content height - landscape k = (834 - 20) / 500 = 1.628
    // (24 → 39.07 pt), portrait k = (1194 - 20) / 840 = 1.3976 (24 → 33.54 pt). The progress group no longer hugs the bottom: landscape
    // title at 63.1% of the height, bar 71.8% (Thor 62.2% / 71.7%); portrait 77.6% / 82.8% (17 Pro portrait 75.9% / 81.0%). The previous
    // long side / 1000 rule (k = 1.194) gave 72.3% / 78.7% and 80.6% / 85.1%. The Android references (Thor 832.5×468.3, Fold8 934.2×841.7,
    // the 873×393 shot) all have a long side ≤ 1000: their fonts stay 24 pt and every rectangle is bit-identical.
    {
        dm_lp_screen ip[2] = { { 1194, 834, 0, 24, 0, 20 }, { 834, 1194, 0, 24, 0, 20 } };
        const double kt[2] = { 39.072, 33.542857142857 }, want[2][2] = { { 0.6309, 0.7176 }, { 0.7763, 0.8283 } };
        for (int i = 0; i < 2; i++) {
            dm_lp_layout L; dm_loadpage_layout(&ip[i], &L);
            printf("  iPad %s：标题 %.2f pt，标题在 %.1f%% 高，条在 %.1f%% 高\n", i ? "竖" : "横", L.title_pt, L.title.y / ip[i].h * 100, L.bar.y / ip[i].h * 100);
            if (fabs(L.title_pt - kt[i]) > 0.01) { fprintf(stderr, "【红】iPad %s 的字号没按安全内容高放大（%.2f pt，期望 %.2f）\n", i ? "竖" : "横", L.title_pt, kt[i]); fails++; }
            if (fabs(L.title.y / ip[i].h - want[i][0]) > 0.002 || fabs(L.bar.y / ip[i].h - want[i][1]) > 0.002) {
                fprintf(stderr, "【红】iPad %s 的进度组相对位置不对（标题 %.3f、条 %.3f，期望 %.3f / %.3f）\n", i ? "竖" : "横", L.title.y / ip[i].h, L.bar.y / ip[i].h, want[i][0], want[i][1]); fails++; }
        }
        dm_lp_screen an = { 873, 393, 0, 0, 0, 0 }; dm_lp_layout A; dm_loadpage_layout(&an, &A);
        if (A.title_pt != 24 || A.bar.h != 8) { fprintf(stderr, "【红】安卓手持屏被大屏缩放碰到了（字号 %.1f、条高 %.1f）\n", A.title_pt, A.bar.h); fails++; }
    }
    // 新背景：两张都应是 App 图标美术——中心（图标）明显亮于四角（四周渐隐到黑），且只有横 / 竖各一张 | new background: both are the App icon art - the centre (the icon) is clearly brighter than the four corners (edges fade to black), one landscape and one portrait
    for (int i = 0; i < 2; i++) {
        int bw, bh; const char *bgn = dm_loadpage_bg_name(i ? 402 : 951, i ? 874 : 669, &bw, &bh);
        snprintf(path, sizeof path, "%s/%s", argv[1], bgn); CGImageRef bg = dm_loadpage_cg_image(path);
        if (!bg) { fprintf(stderr, "【红】背景图读不到：%s\n", path); fails++; continue; }
        double c = img_lum(bg, 0.4, 0.4, 0.6, 0.6), k = (img_lum(bg, 0, 0, 0.12, 0.12) + img_lum(bg, 0.88, 0.88, 1, 1)) / 2;
        printf("  背景 %s：中心亮度 %.0f、角亮度 %.0f\n", bgn, c, k);
        if (c < 40 || k > 25 || c < k + 25) { fprintf(stderr, "【红】背景不是 PLUS 图标画面（中心不够亮 / 四角不够暗）：%s\n", bgn); fails++; }
        CGImageRelease(bg);
    }
    // 字标关：布局没有字标图、文本框为空；多渲染一张「开字标」对比，正图字标区的金色像素必须少得多 | wordmark off: no wordmark image, empty text; one "on" render for comparison, the real picture must have far fewer gold pixels in the wordmark area
    { dm_lp_layout L; dm_lp_text T; dm_loadpage_layout(&shots[1].s, &L); dm_loadpage_text(&T, now);
      if (L.logo.w != 0 || L.logo_name != NULL) { fprintf(stderr, "【红】关字标时还有字标图（w=%.0f name=%s）\n", L.logo.w, L.logo_name ? L.logo_name : "NULL"); fails++; }
      if (T.brand[0]) { fprintf(stderr, "【红】关字标时还有文字字标「%s」\n", T.brand); fails++; }
      dm_lp_rect wm; dm_loadpage_set_wordmark(1); dm_loadpage_layout(&shots[1].s, &L); wm = L.logo;
      if (wm.w <= 0) { fprintf(stderr, "【红】对比图没有字标框，测不了\n"); fails++; }
      char onpath[1024], offpath[1024]; snprintf(onpath, sizeof onpath, "%s/t_loadpage_render_wordmark_on_%s.png", NSTemporaryDirectory().UTF8String, lang);
      snprintf(offpath, sizeof offpath, "%s/loadpage_iphone17pro_port_402x874_%s_37.png", argv[2], lang);
      int bw, bh; const char *bgn = dm_loadpage_bg_name(shots[1].s.w, shots[1].s.h, &bw, &bh);
      snprintf(path, sizeof path, "%s/%s", argv[1], bgn); CGImageRef bg = dm_loadpage_cg_image(path);
      snprintf(path, sizeof path, "%s/%s", argv[1], dm_loadpage_logo_name(NULL, NULL)); CGImageRef logo = dm_loadpage_cg_image(path);
      if (!bg || !logo || !dm_loadpage_cg_png(onpath, bg, logo, &shots[1].s, shots[1].scale, now)) { fprintf(stderr, "【红】对比图（开字标）渲染失败\n"); fails++; }
      dm_loadpage_set_wordmark(0);
      if (!bg || !dm_loadpage_cg_png(offpath, bg, NULL, &shots[1].s, shots[1].scale, now)) { fprintf(stderr, "【红】对比图（关字标）渲染失败\n"); fails++; }
      if (bg) CGImageRelease(bg); if (logo) CGImageRelease(logo);
      size_t W1, H1, W2, H2; unsigned char *p1 = png_pixels(offpath, &W1, &H1), *p2 = png_pixels(onpath, &W2, &H2);
      if (p1 && p2) { long g1 = gold_pixels(p1, W1, H1, wm, shots[1].scale), g2 = gold_pixels(p2, W2, H2, wm, shots[1].scale);
        printf("  字标区金色像素：正图 %ld、开字标对比图 %ld\n", g1, g2);
        if (!(g2 > 2000 && g1 * 2 < g2)) { fprintf(stderr, "【红】金色字标没去掉（正图 %ld / 对比 %ld）\n", g1, g2); fails++; } }
      else { fprintf(stderr, "【红】读不回渲染出的 PNG\n"); fails++; }
      free(p1); free(p2); unlink(onpath); }
    for (unsigned i = 0; i < sizeof shots / sizeof *shots; i++) {
        int bw, bh; const char *bgn = dm_loadpage_bg_name(shots[i].s.w, shots[i].s.h, &bw, &bh); char srcpath[1024];
        snprintf(srcpath, sizeof srcpath, "%s/%s", argv[1], bgn); CGImageRef bg = dm_loadpage_cg_image(srcpath);
        if (!bg) { fprintf(stderr, "【红】背景图读不到：%s\n", srcpath); fails++; continue; }
        if ((int)CGImageGetWidth(bg) != bw || (int)CGImageGetHeight(bg) != bh) { fprintf(stderr, "【红】%s 实际 %zux%zu ≠ 布局假定 %dx%d\n", bgn, CGImageGetWidth(bg), CGImageGetHeight(bg), bw, bh); fails++; }
        snprintf(path, sizeof path, "%s/loadpage_%s_%s_37.png", argv[2], shots[i].name, lang);
        int ok = dm_loadpage_cg_png(path, bg, NULL, &shots[i].s, shots[i].scale, now); CGImageRelease(bg);   // 字标图不传：关字标 | no wordmark image: wordmark off
        printf("%s %s\n", ok ? "✓" : "✗", path); if (!ok) { fails++; continue; }
        // P2-3 接缝：底色必须等于背景图四角的平均色；查屏幕上方两角（下方有渐暗底板）。写死 (5,4,3) 与图边 (8,2,0) 差 3，会报红
        // P2-3 seam: the base must equal the mean of the image corners; check the two top corners (the bottom is under the darkening plate). The old hard-coded (5,4,3) differs from the image edge (8,2,0) by 3 and fails
        size_t SW, SH, OW, OH; unsigned char *sp = png_pixels(srcpath, &SW, &SH), *op = png_pixels(path, &OW, &OH);
        if (sp && op) { int c[3], p[3], bad = 0; corners_rgb(sp, SW, SH, c);
            size_t pt[2][2] = { { OW > 4 ? 2 : 0, OH > 4 ? 2 : 0 }, { OW > 4 ? OW - 3 : OW - 1, OH > 4 ? 2 : 0 } };
            for (int q = 0; q < 2; q++) { pixel_rgb(op, OW, OH, pt[q][0], pt[q][1], p);
                if (abs(p[0] - c[0]) > 2 || abs(p[1] - c[1]) > 2 || abs(p[2] - c[2]) > 2) bad = 1; }
            printf("  底色接缝 %s：图四角 %d,%d,%d；屏上角 %d,%d,%d\n", shots[i].name, c[0], c[1], c[2], p[0], p[1], p[2]);
            if (bad) { fprintf(stderr, "【红】%s 的底色与背景图四角平均色接不上（P2-3 矩形接缝）\n", path); fails++; } }
        else { fprintf(stderr, "【红】读不回底色检查用的 PNG\n"); fails++; }
        free(sp); free(op);
    }
    return fails ? 1 : 0;
}
