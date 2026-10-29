// 加载页出图核对（Mac，离屏）：用 iOS 同一个 Core Graphics 画函数（ios/Sources/dm_loadpage_cg.m）把「导入 → 着色器缓存」走到 37% 的中间态渲染成 PNG，
// 三种屏：Duo 内屏 951×669 pt @3.3、iPhone 17 Pro 竖 402×874 pt @3（顶 59 / 底 34 安全区）与横 874×402（左右 59、底 21）、安卓 20:9 873×393 dp @2.75。
// 用法：t_loadpage_render <素材目录 dm/assets/loadpage> <输出目录> [语言 zh|en|es|ru|ko]。不开窗口、不起宿主、不截屏。
// Loading page render check (Mac, offscreen): the same Core Graphics draw function as iOS (ios/Sources/dm_loadpage_cg.m) renders the mid state "import → shader cache at 37%" to PNG
// for three screens: Duo inner 951×669 pt @3.3, iPhone 17 Pro portrait 402×874 pt @3 (59 top / 34 bottom safe area) and landscape 874×402 (59 sides, 21 bottom), Android 20:9 873×393 dp @2.75.
// Usage: t_loadpage_render <asset dir dm/assets/loadpage> <output dir> [lang zh|en|es|ru|ko]. No window, no host, no screen capture.
#include "../dm_loadpage.h"
#include "../dm_i18n.h"
#import <CoreGraphics/CoreGraphics.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
CGImageRef dm_loadpage_cg_image(const char *path);
int dm_loadpage_cg_png(const char *path, CGImageRef bg, CGImageRef logo, const dm_lp_screen *scr, double scale, uint64_t now_ms);
int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "用法：%s <素材目录> <输出目录> [zh|en|es|ru|ko]\n", argv[0]); return 2; }
    const char *lang = argc > 3 ? argv[3] : "zh";
    setenv("DM_LANG", !strcmp(lang, "zh") ? "zh-Hans" : !strcmp(lang, "en") ? "en-US" : !strcmp(lang, "es") ? "es-ES" : !strcmp(lang, "ru") ? "ru-RU" : "ko-KR", 1); unsetenv("DM_SAVE");
    // 两段：导入（已完成）→ 着色器缓存 2612 / 7062，总进度 25% + 75% × 0.16 = 37%；第二阶段已走 40 秒，有「预计还要」 | two stages: import (done) → shaders 2612 / 7062, overall 25% + 75% × 0.16 = 37%; 40 s into stage two, so an ETA shows
    int kinds[] = { DM_LP_IMPORT, DM_LP_SHADERS }; uint64_t t = 1000;
    dm_loadpage_begin(kinds, 2, NULL, t);
    dm_lp_detail d = { 0, 0, 0, 0 }; d.bytes_total = 690000000; d.bytes_done = 690000000; dm_loadpage_set(0, 1.0, &d, t + 60000);
    dm_lp_detail e = { 2612, 7062, 0, 0 }; dm_loadpage_set(1, 0.16, &e, t + 60000); dm_loadpage_set(1, 0.16, &e, t + 100000);
    uint64_t now = t + 100000;
    if (dm_loadpage_percent() != 37) { fprintf(stderr, "【红】总进度 %d%% ≠ 37%%\n", dm_loadpage_percent()); return 1; }
    struct { const char *name; dm_lp_screen s; double scale; } shots[] = {
        { "duo_inner_951x669", { 951, 669, 0, 0, 0, 0 }, 3142.0 / 951 },
        { "iphone17pro_port_402x874", { 402, 874, 0, 59, 0, 34 }, 3 },
        { "iphone17pro_land_874x402", { 874, 402, 59, 0, 59, 21 }, 3 },
        { "android_20x9_873x393", { 873, 393, 0, 0, 0, 0 }, 2400.0 / 873 },
    };
    int fails = 0; char path[1024];
    snprintf(path, sizeof path, "%s/%s", argv[1], dm_loadpage_logo_name(NULL, NULL)); CGImageRef logo = dm_loadpage_cg_image(path);
    if (!logo) { fprintf(stderr, "【红】字标图读不到：%s\n", path); fails++; }
    for (unsigned i = 0; i < sizeof shots / sizeof *shots; i++) {
        int bw, bh; const char *bgn = dm_loadpage_bg_name(shots[i].s.w, shots[i].s.h, &bw, &bh);
        snprintf(path, sizeof path, "%s/%s", argv[1], bgn); CGImageRef bg = dm_loadpage_cg_image(path);
        if (!bg) { fprintf(stderr, "【红】背景图读不到：%s\n", path); fails++; continue; }
        if ((int)CGImageGetWidth(bg) != bw || (int)CGImageGetHeight(bg) != bh) { fprintf(stderr, "【红】%s 实际 %zux%zu ≠ 布局假定 %dx%d\n", bgn, CGImageGetWidth(bg), CGImageGetHeight(bg), bw, bh); fails++; }
        snprintf(path, sizeof path, "%s/loadpage_%s_%s_37.png", argv[2], shots[i].name, lang);
        int ok = dm_loadpage_cg_png(path, bg, logo, &shots[i].s, shots[i].scale, now); CGImageRelease(bg);
        printf("%s %s\n", ok ? "✓" : "✗", path); if (!ok) fails++;
    }
    return fails ? 1 : 0;
}
