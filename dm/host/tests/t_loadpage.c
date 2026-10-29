// 加载页核心（dm_loadpage.c）回归：进度合成（两段权重、单段退化、回报倒退不倒退、跳阶段补完成）、剩余时间与细节行、失败 / 完成文案、
// 素材选择（横 / 竖）与 cover 裁切（Duo 内屏 3142×2160、iPhone 17 Pro 402×874 两种方向、安卓 20:9 2400×1080）、布局在安全区内、汇总日志行。时钟全由测试传入。
// Regression for the loading page core (dm_loadpage.c): progress composition (two weighted stages, single-stage degenerate case, no regression on backward reports,
// skipped stages marked complete), ETA and the detail line, failure / done text, art selection (landscape / portrait) and cover crop (Duo inner 3142×2160, iPhone 17 Pro 402×874
// both orientations, Android 20:9 2400×1080), layout inside the safe area, the summary log line. The clock is passed in by the test throughout.
#include "../dm_loadpage.c"
#include <stdio.h>
#include <string.h>
#include <math.h>
const char *dm_ui(const char *key) { return dm_ui_group(key, DM_LG_ZH); }   // 不链 dm_i18n.c：按中文列取 | dm_i18n.c is not linked: take the Chinese column
#include "../dm_i18n_tab.h"
const char *dm_ui_group(const char *key, int group) {
    for (size_t i = 0; i < sizeof k_dm_i18n / sizeof *k_dm_i18n; i++) if (!strcmp(k_dm_i18n[i].k, key))
        return group == DM_LG_ZH ? k_dm_i18n[i].zh : group == DM_LG_ES ? k_dm_i18n[i].es : group == DM_LG_MX ? k_dm_i18n[i].mx : group == DM_LG_RU ? k_dm_i18n[i].ru : group == DM_LG_KO ? k_dm_i18n[i].ko : k_dm_i18n[i].en;
    return key;
}
static int checks, fails;
#define CHECK(x) do { checks++; if (!(x)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)
#define NEAR(a, b) (fabs((a) - (b)) < 0.51)
static int rect_in(dm_lp_rect r, double x0, double y0, double x1, double y1) { return r.x >= x0 - 1e-6 && r.y >= y0 - 1e-6 && r.x + r.w <= x1 + 1e-6 && r.y + r.h <= y1 + 1e-6; }

int main(void) {
    dm_lp_text t; dm_lp_detail d = { 0 };
    // 1. 两段权重：导入 90 秒 + 着色器缓存 270 秒 → 导入占 25%。导入走到一半 = 12%，导入完成 = 25%，着色器一半 = 62%，全完 = 100%
    // 1. Two weighted stages: import 90 s + shader cache 270 s → import is 25%. Import half way = 12%, import done = 25%, shaders half way = 62%, all done = 100%
    int two[] = { DM_LP_IMPORT, DM_LP_SHADERS };
    dm_loadpage_begin(two, 2, NULL, 1000);
    CHECK(dm_loadpage_state() == DM_LP_RUNNING); CHECK(dm_loadpage_percent() == 0); CHECK(dm_loadpage_stage() == -1);
    dm_loadpage_set(0, 0.5, NULL, 2000); CHECK(dm_loadpage_percent() == 12); CHECK(dm_loadpage_stage() == 0);
    dm_loadpage_set(0, 1.0, NULL, 3000); CHECK(dm_loadpage_percent() == 25);
    dm_loadpage_set(1, 0.5, NULL, 4000); CHECK(dm_loadpage_percent() == 62); CHECK(dm_loadpage_stage() == 1);
    // 回报倒退：着色器阶段报回 0.2、导入阶段再报 0.3，总进度都不动 | backward reports: shaders back to 0.2, import again at 0.3 - overall unchanged
    dm_loadpage_set(1, 0.2, NULL, 4100); CHECK(dm_loadpage_percent() == 62);
    dm_loadpage_set(0, 0.3, NULL, 4200); CHECK(dm_loadpage_percent() == 62); CHECK(dm_loadpage_stage() == 1);
    dm_loadpage_set(1, 1.0, NULL, 5000); CHECK(dm_loadpage_percent() == 100); CHECK(dm_loadpage_state() == DM_LP_RUNNING);   // 到 100% 还不算完成，要平台 finish | 100% is not done yet: the platform must call finish
    dm_loadpage_finish(5500); CHECK(dm_loadpage_state() == DM_LP_DONE); CHECK(dm_loadpage_music_wanted() == 0);
    dm_loadpage_text(&t, 5600); CHECK(!strcmp(t.title, "准备完成，正在进入游戏")); CHECK(!strcmp(t.percent, "100%"));
    CHECK(dm_loadpage_stage_ms(0) == 1000 + 2000 - 2000);   // 导入 2000→3000 | import 2000→3000
    CHECK(dm_loadpage_stage_ms(1) == 1000);                 // 着色器 4000→5000 | shaders 4000→5000
    char sum[256]; dm_loadpage_summary(sum, sizeof sum, 5600); CHECK(!strcmp(sum, "导入 1.0 秒 · 着色器缓存 1.0 秒 · 合计 4.5 秒"));

    // 2. 自定义权重：导入 60 秒 + 着色器 240 秒 → 导入 20%；给 0 的那项用缺省 | 2. Custom weights: import 60 s + shaders 240 s → import 20%; a 0 entry takes the default
    int w2[] = { 60000, 240000 };
    dm_loadpage_begin(two, 2, w2, 0); dm_loadpage_set(0, 1.0, NULL, 10); CHECK(dm_loadpage_percent() == 20);
    int w3[] = { 0, 90000 };   // 导入缺省 90 秒 + 着色器 90 秒 → 50% | import default 90 s + shaders 90 s → 50%
    dm_loadpage_begin(two, 2, w3, 0); dm_loadpage_set(0, 1.0, NULL, 10); CHECK(dm_loadpage_percent() == 50);

    // 3. 单段退化（iOS 只有下载）：几成就是几成 | 3. Single-stage degenerate case (iOS has only the download): the fraction is the overall
    int one[] = { DM_LP_DOWNLOAD };
    dm_loadpage_begin(one, 1, NULL, 0);
    d.bytes_total = 4130000000LL; d.bytes_done = 1527900000LL; dm_loadpage_set(0, 0.37, &d, 1000); CHECK(dm_loadpage_percent() == 37);
    dm_loadpage_text(&t, 1000); CHECK(!strcmp(t.title, "正在下载游戏内容")); CHECK(!strcmp(t.percent, "37%"));
    CHECK(strstr(t.body, "1.5 / 4.1 GB") != NULL); CHECK(strstr(t.body, "MB/s") == NULL);   // 还没有第二个采样点：不显示速度 | no second sample yet: no speed
    CHECK(strstr(t.body, "正在估算剩余时间") != NULL);                                     // 没过 2 秒：不估 | under 2 s: no estimate
    CHECK(!strcmp(t.sub, "第一次要几分钟，请留在这个页面"));
    d.bytes_done = 1540000000LL; dm_loadpage_set(0, 0.373, &d, 2000);   // 1 秒 12.1 MB → 12.1 MB/s | 12.1 MB in 1 s → 12.1 MB/s
    dm_loadpage_text(&t, 2000); CHECK(strstr(t.body, "12.1 MB/s") != NULL);
    d.bytes_done = 1552100000LL; dm_loadpage_set(0, 0.3758, &d, 3000);
    dm_loadpage_text(&t, 3000); CHECK(strstr(t.body, "预计还要 4 分钟") != NULL);   // 剩 2.58 GB ÷ 12.1 MB/s ≈ 213 秒 → 4 分钟（有字节与速度就按它们估） | 2.58 GB left ÷ 12.1 MB/s ≈ 213 s → 4 min (bytes and speed win when known)
    dm_loadpage_note("把游戏资源文件夹拷进「我的 iPhone → D2R Mobile」"); dm_loadpage_text(&t, 3000); CHECK(strstr(t.body, "拷进") != NULL);   // 平台说明替换正文 | platform note replaces the body
    dm_loadpage_note(NULL); dm_loadpage_text(&t, 3000); CHECK(strstr(t.body, "GB") != NULL);
    dm_loadpage_hint("dmLoadHintShaders"); dm_loadpage_text(&t, 3000); CHECK(strstr(t.sub, "着色器") != NULL); dm_loadpage_hint(NULL);

    // 4. 跳阶段：直接报第 3 个阶段 = 前两个算完成；文件计数进细节行 | 4. Skipping: reporting stage 3 directly marks the first two complete; the file count goes into the detail line
    int six[] = { DM_LP_PATCH, DM_LP_ASSETS, DM_LP_OVERLAY, DM_LP_PACKS, DM_LP_SHADERS, DM_LP_INSTALL };
    dm_loadpage_begin(six, 6, NULL, 0);
    memset(&d, 0, sizeof d); d.items_done = 1234; d.items_total = 7062;
    dm_loadpage_set(4, 1234.0 / 7062, &d, 100000);
    CHECK(dm_loadpage_stage() == 4); CHECK(dm_loadpage_stage_ms(0) == 0 || dm_loadpage_stage_ms(0) >= 0);
    double expect = (20000 + 30000 + 20000 + 30000 + 270000 * 1234.0 / 7062) / 385000.0;
    CHECK(fabs(dm_loadpage_overall() - expect) < 1e-9);
    dm_loadpage_text(&t, 100000); CHECK(!strcmp(t.title, "正在准备着色器缓存")); CHECK(strstr(t.body, "第 1234 / 7062 个文件") != NULL); CHECK(strstr(t.sub, "只做这一次") != NULL);
    // 5. 失败：标题换、正文 = 原因、音乐停、进度留在原处 | 5. Failure: title changes, body = reason, music stops, progress stays
    int before = dm_loadpage_percent();
    dm_loadpage_fail("磁盘空间不足（还差 1.2 GB）", 101000);
    CHECK(dm_loadpage_state() == DM_LP_FAILED); CHECK(dm_loadpage_music_wanted() == 0); CHECK(dm_loadpage_percent() == before);
    dm_loadpage_text(&t, 101000); CHECK(!strcmp(t.title, "无法继续准备游戏内容")); CHECK(strstr(t.body, "1.2 GB") != NULL); CHECK(strstr(t.sub, "诊断日志") != NULL);
    dm_loadpage_set(4, 1.0, NULL, 102000); CHECK(dm_loadpage_percent() == before);   // 失败后不再动 | frozen after failure

    // 6. 素材选择与 cover 裁切 | 6. Art selection and cover crop
    int bw, bh; dm_lp_rect r;
    CHECK(!strcmp(dm_loadpage_bg_name(951, 669, &bw, &bh), "loadpage_bg_land.jpg") && bw == 2048 && bh == 1152);   // Duo 内屏 | Duo inner
    CHECK(!strcmp(dm_loadpage_bg_name(402, 874, &bw, &bh), "loadpage_bg_port.jpg") && bw == 1152 && bh == 2048);   // iPhone 17 Pro 竖 | iPhone 17 Pro portrait
    CHECK(!strcmp(dm_loadpage_bg_name(874, 402, NULL, NULL), "loadpage_bg_land.jpg"));
    // Duo 内屏 3142×2160（1.455:1）在 2048×1152（1.778:1）上：高对齐，宽取 1152×1.455 = 1676，水平居中 | Duo inner on the landscape image: height fitted, width 1676, centred horizontally
    dm_loadpage_cover(2048, 1152, 3142, 2160, 0.5, 0.58, &r);
    CHECK(NEAR(r.h, 1152) && NEAR(r.w, 1675.7) && NEAR(r.x, 186.2) && NEAR(r.y, 0));
    // 安卓 20:9 横 2400×1080（2.222:1）：宽对齐，高取 2048/2.222 = 921.6，焦点 0.58 → y = 0.58×1152 − 460.8 = 207.4 | Android 20:9 landscape: width fitted, height 921.6, focus 0.58 → y = 207.4
    dm_loadpage_cover(2048, 1152, 2400, 1080, 0.5, 0.58, &r);
    CHECK(NEAR(r.w, 2048) && NEAR(r.h, 921.6) && NEAR(r.x, 0) && NEAR(r.y, 207.4));
    // iPhone 17 Pro 竖 1206×2622（0.46:1）在 1152×2048 上：高对齐，宽 2048×0.46 = 942，水平居中 | iPhone 17 Pro portrait on the portrait image: height fitted, width 942, centred
    dm_loadpage_cover(1152, 2048, 1206, 2622, 0.5, 0.58, &r);
    CHECK(NEAR(r.h, 2048) && NEAR(r.w, 941.9) && NEAR(r.x, 105.0) && NEAR(r.y, 0));
    // 极端：很矮很宽（32:9）焦点压到图底 → 裁切框贴底不越界 | extreme: very wide (32:9) pushes the focus to the bottom → the crop hugs the bottom edge, never past it
    dm_loadpage_cover(2048, 1152, 3200, 900, 0.5, 0.9, &r); CHECK(NEAR(r.y + r.h, 1152) && r.y >= 0);
    dm_loadpage_cover(2048, 1152, 3200, 900, 0.5, 0.0, &r); CHECK(NEAR(r.y, 0));
    dm_loadpage_cover(0, 0, 100, 100, 0.5, 0.5, &r); CHECK(r.w == 1 && r.h == 1);   // 坏输入不除零 | bad input does not divide by zero

    // 7. 布局：条与四行字都在安全区内；竖屏条铺满内容宽；横屏条 62%；bar_fill 跟总进度；灵动岛 / 挖孔内缩生效 | 7. Layout: bar and text inside the safe area; portrait bar spans the content width; landscape bar 62%; bar_fill follows overall; island / cutout insets honoured
    dm_loadpage_begin(two, 2, NULL, 0); dm_loadpage_set(0, 0.5, NULL, 1); dm_loadpage_set(1, 0.16, NULL, 2);   // 25% + 75%×0.16 = 37% | 25% + 75% × 0.16 = 37%
    CHECK(dm_loadpage_percent() == 37);
    dm_lp_layout L; dm_lp_screen duo = { 951, 669, 0, 0, 0, 0 }; dm_loadpage_layout(&duo, &L);
    CHECK(L.landscape && !strcmp(L.bg_name, "loadpage_bg_land.jpg") && NEAR(L.bg_dst.w, 951) && NEAR(L.bg_dst.h, 669));
    CHECK(rect_in(L.bar, 0, 0, 951, 669) && rect_in(L.title, 0, 0, 951, 669) && rect_in(L.sub, 0, 0, 951, 669) && rect_in(L.brand, 0, 0, 951, 669));
    CHECK(NEAR(L.bar.w, (951 - 56) * 0.62) && NEAR(L.bar_fill.w, L.bar.w * 0.37)); CHECK(L.title_pt == 24 && L.body_pt == 18 && L.sub_pt == 14);
    CHECK(L.title.y + L.title.h <= L.bar.y && L.bar.y + L.bar.h <= L.body.y && L.body.y + L.body.h <= L.sub.y && L.sub.y + L.sub.h <= 669 - 44 + 1e-6);   // 自上而下：标题 / 条 / 正文 / 副文 | top down: title / bar / body / sub
    CHECK(NEAR(L.percent.x + L.percent.w, L.bar.x + L.bar.w));   // 百分数右缘对齐条尾 | percent right edge aligned with the bar end
    CHECK(!strcmp(L.logo_name, "loadpage_logo.png") && L.logo_w == 1600 && L.logo_h == 600);
    CHECK(NEAR(L.logo.w, (951 - 56) * 0.58) && NEAR(L.logo.h, L.logo.w * 600 / 1600) && NEAR(L.logo.x + L.logo.w / 2, 951 / 2.0));   // 横屏：58% 宽、等比、居中 | landscape: 58% wide, aspect kept, centred
    CHECK(NEAR(L.logo.y + L.logo.h / 2, 669 * 0.36) && L.logo.y + L.logo.h <= L.title.y - 24 + 1e-6 && L.logo.y >= 28 - 1e-6);
    dm_lp_screen ip = { 402, 874, 0, 59, 0, 34 }; dm_loadpage_layout(&ip, &L);   // iPhone 竖屏：顶 59 / 底 34 安全区 | iPhone portrait: 59 top / 34 bottom safe area
    CHECK(!L.landscape && !strcmp(L.bg_name, "loadpage_bg_port.jpg") && NEAR(L.bar.w, 402 - 56) && NEAR(L.bar.x, 28));
    CHECK(L.sub.y + L.sub.h <= 874 - 34 + 1e-6 && L.brand.y >= 59 + 28 - 1e-6);
    CHECK(NEAR(L.logo.w, (402 - 56) * 0.86) && NEAR(L.logo.x + L.logo.w / 2, 201) && NEAR(L.logo.y + L.logo.h / 2, 874 * 0.30) && L.logo.y >= 59 + 28 - 1e-6);   // 竖屏：86% 宽、居中、中心 0.30h、在顶部安全区下 | portrait: 86% wide, centred, centre at 0.30h, below the top safe area
    dm_lp_screen ipl = { 874, 402, 59, 0, 59, 21 }; dm_loadpage_layout(&ipl, &L);   // iPhone 横屏：左右 59 灵动岛、底 21 | iPhone landscape: 59 left / right (island), 21 bottom
    CHECK(L.landscape && L.bar.x >= 59 + 28 - 1e-6 && L.bar.x + L.bar.w <= 874 - 59 - 28 + 1e-6 && L.sub.y + L.sub.h <= 402 - 21 + 1e-6);
    dm_lp_screen an = { 873, 393, 0, 0, 0, 0 }; dm_loadpage_layout(&an, &L);   // 安卓 20:9（2400×1080 @2.75） | Android 20:9 (2400×1080 @2.75)
    CHECK(L.landscape && rect_in(L.bar, 0, 0, 873, 393) && rect_in(L.title, 0, 0, 873, 393) && L.bar.w <= 900);
    dm_lp_screen big = { 2000, 1200, 0, 0, 0, 0 }; dm_loadpage_layout(&big, &L); CHECK(NEAR(L.bar.w, 900));   // 很宽的屏：条不超过 900 点 | very wide screen: the bar stays within 900 pt
    dm_lp_screen tiny = { 100, 50, 0, 0, 0, 0 }; dm_loadpage_layout(&tiny, &L); CHECK(L.bar.w > 0 && L.bar.h > 0 && L.logo.w >= 0 && L.logo.h >= 0);   // 很小的屏不出负宽（字标缩到放得下或 0） | a tiny screen never yields negative widths (the wordmark shrinks to fit or to 0)
    dm_lp_screen big2 = { 2000, 1200, 0, 0, 0, 0 }; dm_loadpage_layout(&big2, &L); CHECK(NEAR(L.logo.w, 560));   // 大屏字标不超过 560 点 | on a big screen the wordmark stays within 560 pt
    dm_lp_screen landi = { 874, 402, 59, 0, 59, 21 }; dm_loadpage_layout(&landi, &L); CHECK(L.logo.y + L.logo.h <= L.title.y - 24 + 1e-6 && L.logo.y >= 28 - 1e-6);   // 矮横屏：字标压缩到标题之上 | short landscape: the wordmark is squeezed above the title
    CHECK(!strcmp(dm_loadpage_music_name(), "loadpage_loop.m4a") && dm_loadpage_music_wanted() == 1);

    // 8. 没开始就 finish / set：不崩、状态不变；begin 超过上限的阶段数被截 | 8. finish / set before begin: no crash, state unchanged; begin with too many stages is clamped
    dm_loadpage_begin(two, 0, NULL, 0); CHECK(dm_loadpage_state() == DM_LP_IDLE); dm_loadpage_set(0, 0.5, NULL, 1); dm_loadpage_finish(2); CHECK(dm_loadpage_state() == DM_LP_IDLE);
    dm_loadpage_text(&t, 3); CHECK(t.title[0] != 0);
    int many[12] = { DM_LP_IMPORT, DM_LP_IMPORT, DM_LP_IMPORT, DM_LP_IMPORT, DM_LP_IMPORT, DM_LP_IMPORT, DM_LP_IMPORT, DM_LP_IMPORT, DM_LP_IMPORT, DM_LP_IMPORT, DM_LP_IMPORT, DM_LP_IMPORT };
    dm_loadpage_begin(many, 12, NULL, 0); dm_loadpage_set(7, 1.0, NULL, 1); CHECK(dm_loadpage_percent() == 100); dm_loadpage_set(11, 0.5, NULL, 2); CHECK(dm_loadpage_percent() == 100);
    // 9. 其他语言列取得到（英文 / 韩文），占位符同序 | 9. Other language columns resolve (English / Korean) with matching placeholders
    dm_loadpage_ui_set_group(DM_LG_EN); dm_loadpage_begin(two, 2, NULL, 0); memset(&d, 0, sizeof d); d.items_done = 5; d.items_total = 9; dm_loadpage_set(1, 0.5, &d, 1);
    dm_loadpage_text(&t, 1); CHECK(!strcmp(t.title, "Preparing the shader cache")); CHECK(strstr(t.body, "File 5 of 9") != NULL);
    dm_loadpage_ui_set_group(DM_LG_KO); dm_loadpage_text(&t, 1); CHECK(strstr(t.body, "5 / 9") != NULL); dm_loadpage_ui_set_group(-1);

    printf("t_loadpage：%d 项检查，%d 项失败\n", checks, fails);
    return fails ? 1 : 0;
}
