// 加载页出图核对（Mac，离屏）：用 iOS 同一个 Core Graphics 画函数（ios/Sources/dm_loadpage_cg.m）把「导入 → 着色器缓存」走到 37% 的中间态渲染成 PNG，
// 三种屏：Duo 内屏 951×669 pt @3.3、iPhone 17 Pro 竖 402×874 pt @3（顶 59 / 底 34 安全区）与横 874×402（左右 59、底 21）、安卓 20:9 873×393 dp @2.75。
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
    };
    int fails = 0; char path[1024];
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
        int bw, bh; const char *bgn = dm_loadpage_bg_name(shots[i].s.w, shots[i].s.h, &bw, &bh);
        snprintf(path, sizeof path, "%s/%s", argv[1], bgn); CGImageRef bg = dm_loadpage_cg_image(path);
        if (!bg) { fprintf(stderr, "【红】背景图读不到：%s\n", path); fails++; continue; }
        if ((int)CGImageGetWidth(bg) != bw || (int)CGImageGetHeight(bg) != bh) { fprintf(stderr, "【红】%s 实际 %zux%zu ≠ 布局假定 %dx%d\n", bgn, CGImageGetWidth(bg), CGImageGetHeight(bg), bw, bh); fails++; }
        snprintf(path, sizeof path, "%s/loadpage_%s_%s_37.png", argv[2], shots[i].name, lang);
        int ok = dm_loadpage_cg_png(path, bg, NULL, &shots[i].s, shots[i].scale, now); CGImageRelease(bg);   // 字标图不传：关字标 | no wordmark image: wordmark off
        printf("%s %s\n", ok ? "✓" : "✗", path); if (!ok) fails++;
    }
    return fails ? 1 : 0;
}
