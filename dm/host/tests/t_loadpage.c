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
// 竖向比例（除以屏高）：图标框高、标题顶、条顶、进度组整体高（标题顶到屏底）。参照机也用同一组函数算，避免手抄常数
// Vertical ratios over the screen height: icon box height, title top, bar top, whole progress group (title top to the screen bottom). The reference phones are measured with the same
// functions so the numbers are not hand-copied
static double icon_share(const dm_lp_layout *L, double h) { const double *ib = L->landscape ? k_icon_land : k_icon_port; return L->bg_dst.h * (ib[3] - ib[1]) / h; }
static double at(const dm_lp_layout *L, double h, int what) { return what == 0 ? L->title.y / h : what == 1 ? L->bar.y / h : (h - L->title.y) / h; }   // 0 = 标题顶、1 = 条顶、2 = 进度组整体 | 0 = title top, 1 = bar top, 2 = whole group

int main(void) {
    dm_lp_text t; dm_lp_detail d = { 0 };
    // 1. 两段权重（手机端生成的路：导入 90 秒 + 着色器缓存 270 秒）→ 导入占 25%。导入走到一半 = 12%，导入完成 = 25%，着色器一半 = 62%，全完 = 100%
    // 1. Two weighted stages (on-phone generation path: import 90 s + shader cache 270 s) → import is 25%. Import half way = 12%, import done = 25%, shaders half way = 62%, all done = 100%
    int two[] = { DM_LP_IMPORT, DM_LP_SHADERS }; int w_two[] = { 90000, 270000 };
    dm_loadpage_begin(two, 2, w_two, 1000);
    CHECK(dm_loadpage_state() == DM_LP_RUNNING); CHECK(dm_loadpage_percent() == 0); CHECK(dm_loadpage_stage() == -1);
    dm_loadpage_set(0, 0.5, NULL, 2000); CHECK(dm_loadpage_percent() == 12); CHECK(dm_loadpage_stage() == 0);
    dm_loadpage_set(0, 1.0, NULL, 3000); CHECK(dm_loadpage_percent() == 25);
    dm_loadpage_set(1, 0.5, NULL, 4000); CHECK(dm_loadpage_percent() == 62); CHECK(dm_loadpage_stage() == 1);
    // 回报倒退：着色器阶段报回 0.2、导入阶段再报 0.3，总进度都不动 | backward reports: shaders back to 0.2, import again at 0.3 - overall unchanged
    dm_loadpage_set(1, 0.2, NULL, 4100); CHECK(dm_loadpage_percent() == 62);
    dm_loadpage_set(0, 0.3, NULL, 4200); CHECK(dm_loadpage_percent() == 62); CHECK(dm_loadpage_stage() == 1);
    dm_loadpage_set(1, 1.0, NULL, 5000); CHECK(dm_loadpage_percent() == 100); CHECK(dm_loadpage_state() == DM_LP_RUNNING);   // 到 100% 还不算完成，要平台 finish | 100% is not done yet: the platform must call finish
    dm_loadpage_finish(5500); CHECK(dm_loadpage_state() == DM_LP_DONE); CHECK(dm_loadpage_music_wanted() == 0);
    CHECK(!strncmp(dm_loadpage_last_line(), "加载页：完成，", strlen("加载页：完成，")));   // 安卓交给宿主写进 boot.log 的那一行 | the line Android hands to the host for boot.log
    dm_loadpage_text(&t, 5600); CHECK(!strcmp(t.title, "准备完成，正在进入游戏")); CHECK(!strcmp(t.percent, "100%"));
    CHECK(dm_loadpage_stage_ms(0) == 1000 + 2000 - 2000);   // 导入 2000→3000 | import 2000→3000
    CHECK(dm_loadpage_stage_ms(1) == 1000);                 // 着色器 4000→5000 | shaders 4000→5000
    char sum[256]; dm_loadpage_summary(sum, sizeof sum, 5600); CHECK(!strcmp(sum, "导入 1.0 秒 · 着色器缓存 1.0 秒 · 合计 4.5 秒"));

    // 2. 自定义权重：导入 60 秒 + 着色器 240 秒 → 导入 20%；给 0 的那项用缺省 | 2. Custom weights: import 60 s + shaders 240 s → import 20%; a 0 entry takes the default
    int w2[] = { 60000, 240000 };
    dm_loadpage_begin(two, 2, w2, 0); dm_loadpage_set(0, 1.0, NULL, 10); CHECK(dm_loadpage_percent() == 20);
    int w3[] = { 0, 8500 };   // 导入缺省 8.5 秒 + 着色器 8.5 秒 → 50% | import default 8.5 s + shaders 8.5 s → 50%
    dm_loadpage_begin(two, 2, w3, 0); dm_loadpage_set(0, 1.0, NULL, 10); CHECK(dm_loadpage_percent() == 50);
    // 2b. 电脑出的导入包那条路（缺省权重）：导入 → 准备 = 85 : 15；导入完成 85%，准备一半 92%，导入阶段副文不再说「几分钟」 | 2b. Computer-made pack path (default weights): import → prepare = 85 : 15; import done 85%, prepare half way 92%; the import hint no longer says "a few minutes"
    int kp[] = { DM_LP_IMPORT, DM_LP_PREPARE }; dm_loadpage_begin(kp, 2, NULL, 0);
    dm_loadpage_set(0, 0.5, NULL, 10); CHECK(dm_loadpage_percent() == 42); dm_loadpage_text(&t, 10); CHECK(!strcmp(t.title, "正在导入游戏内容")); CHECK(strstr(t.sub, "分钟") == NULL && strstr(t.sub, "请稍候") != NULL);
    dm_loadpage_set(0, 1.0, NULL, 20); CHECK(dm_loadpage_percent() == 85);
    dm_loadpage_set(1, 0.5, NULL, 30); CHECK(dm_loadpage_percent() == 92); dm_loadpage_text(&t, 30); CHECK(!strcmp(t.title, "正在准备进入游戏"));

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
    dm_loadpage_fail("磁盘空间不足（还差 1.2 GB）", 101000); CHECK(!strncmp(dm_loadpage_last_line(), "加载页：失败于阶段", strlen("加载页：失败于阶段")));
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
    dm_loadpage_begin(two, 2, w_two, 0); dm_loadpage_set(0, 0.5, NULL, 1); dm_loadpage_set(1, 0.16, NULL, 2);   // 25% + 75%×0.16 = 37% | 25% + 75% × 0.16 = 37%
    CHECK(dm_loadpage_percent() == 37);
    dm_lp_layout L; dm_lp_screen duo = { 951, 669, 0, 0, 0, 0 }; dm_loadpage_layout(&duo, &L);
    const double kd = 951 / 762.0;   // B140：Duo 内屏 k = min(669/500, 951/762) = 1.248（宽系数卡住） | B140: the Duo inner screen's k = min(669/500, 951/762) = 1.248 (the width factor binds)
    CHECK(L.landscape && !strcmp(L.bg_name, "loadpage_bg_land.jpg") && NEAR(L.bg_dst.w, 951) && NEAR(L.bg_dst.h, 669));
    CHECK(rect_in(L.bar, 0, 0, 951, 669) && rect_in(L.title, 0, 0, 951, 669) && rect_in(L.sub, 0, 0, 951, 669) && rect_in(L.brand, 0, 0, 951, 669));
    CHECK(NEAR(L.bar.w, (951 - 56 * kd) * 0.62) && NEAR(L.bar_fill.w, L.bar.w * 0.37)); CHECK(NEAR(L.title_pt, 24 * kd) && NEAR(L.body_pt, 18 * kd) && NEAR(L.sub_pt, 14 * kd));
    CHECK(L.title.y + L.title.h <= L.bar.y && L.bar.y + L.bar.h <= L.body.y && L.body.y + L.body.h <= L.sub.y && L.sub.y + L.sub.h <= 669 - 44 + 1e-6);   // 自上而下：标题 / 条 / 正文 / 副文 | top down: title / bar / body / sub
    CHECK(NEAR(L.percent.x + L.percent.w, L.bar.x + L.bar.w));   // 百分数右缘对齐条尾 | percent right edge aligned with the bar end
    CHECK(!strcmp(L.logo_name, "loadpage_logo.png") && L.logo_w == 1600 && L.logo_h == 600);
    CHECK(NEAR(L.logo.w, (951 - 56 * kd) * 0.58) && NEAR(L.logo.h, L.logo.w * 600 / 1600) && NEAR(L.logo.x + L.logo.w / 2, 951 / 2.0));   // 横屏：58% 宽、等比、居中 | landscape: 58% wide, aspect kept, centred
    CHECK(NEAR(L.logo.y + L.logo.h / 2, 669 * 0.36) && L.logo.y + L.logo.h <= L.title.y - 24 + 1e-6 && L.logo.y >= 28 - 1e-6);
    dm_lp_screen ip = { 402, 874, 0, 59, 0, 34 }; dm_loadpage_layout(&ip, &L);   // iPhone 竖屏：顶 59 / 底 34 安全区 | iPhone portrait: 59 top / 34 bottom safe area
    CHECK(!L.landscape && !strcmp(L.bg_name, "loadpage_bg_port.jpg") && NEAR(L.bar.w, 402 - 56) && NEAR(L.bar.x, 28));
    CHECK(L.sub.y + L.sub.h <= 874 - 34 + 1e-6 && L.brand.y >= 59 + 28 - 1e-6);
    CHECK(NEAR(L.logo.w, (402 - 56) * 0.86) && NEAR(L.logo.x + L.logo.w / 2, 201) && NEAR(L.logo.y + L.logo.h / 2, 874 * 0.30) && L.logo.y >= 59 + 28 - 1e-6);   // 竖屏：86% 宽、居中、中心 0.30h、在顶部安全区下 | portrait: 86% wide, centred, centre at 0.30h, below the top safe area
    dm_lp_screen ipl = { 874, 402, 59, 0, 59, 21 }; dm_loadpage_layout(&ipl, &L);   // iPhone 横屏：左右 59 灵动岛、底 21 | iPhone landscape: 59 left / right (island), 21 bottom
    CHECK(L.landscape && L.bar.x >= 59 + 28 - 1e-6 && L.bar.x + L.bar.w <= 874 - 59 - 28 + 1e-6 && L.sub.y + L.sub.h <= 402 - 21 + 1e-6);
    dm_lp_screen an = { 873, 393, 0, 0, 0, 0 }; dm_loadpage_layout(&an, &L);   // 安卓 20:9（2400×1080 @2.75） | Android 20:9 (2400×1080 @2.75)
    CHECK(L.landscape && rect_in(L.bar, 0, 0, 873, 393) && rect_in(L.title, 0, 0, 873, 393) && L.bar.w <= 900);
    dm_lp_screen big = { 3440, 1440, 0, 0, 0, 0 }; dm_loadpage_layout(&big, &L); CHECK(NEAR(L.title_pt, 48) && NEAR(L.bar.w, 1800));   // 很宽的屏：k 封顶 2，条上限 900 点随 k 放大（审计 P2-3） | very wide screen: k capped at 2, the 900 pt bar limit scales with k (audit P2-3)
    dm_lp_screen tiny = { 100, 50, 0, 0, 0, 0 }; dm_loadpage_layout(&tiny, &L); CHECK(L.bar.w > 0 && L.bar.h > 0 && L.logo.w >= 0 && L.logo.h >= 0);   // 很小的屏不出负宽（字标缩到放得下或 0） | a tiny screen never yields negative widths (the wordmark shrinks to fit or to 0)
    dm_lp_screen big2 = { 2000, 1200, 0, 0, 0, 0 }; dm_loadpage_layout(&big2, &L); CHECK(NEAR(L.logo.w, 560));   // 大屏字标不超过 560 点 | on a big screen the wordmark stays within 560 pt
    dm_lp_screen landi = { 874, 402, 59, 0, 59, 21 }; dm_loadpage_layout(&landi, &L); CHECK(L.logo.y + L.logo.h <= L.title.y - 24 + 1e-6 && L.logo.y >= 28 - 1e-6);   // 矮横屏：字标压缩到标题之上 | short landscape: the wordmark is squeezed above the title
    // 静音开关：缺省有声音 → 进行中放；点一下静音就不放；begin 不改它；失败停；开关在右上角安全区内，不压字标 / 字标图；点击判定 | mute toggle: sound on by default → plays while running; one tap mutes; begin keeps it; failure stops; the button sits top right inside the safe area, clear of the wordmark; hit test
    CHECK(!strcmp(dm_loadpage_music_name(), "loadpage_loop.m4a") && dm_loadpage_muted() == 0 && dm_loadpage_music_wanted() == 1);
    CHECK(dm_loadpage_toggle_muted() == 1 && dm_loadpage_music_wanted() == 0);
    dm_loadpage_begin(two, 2, w_two, 0); CHECK(dm_loadpage_muted() == 1 && dm_loadpage_music_wanted() == 0);
    CHECK(dm_loadpage_toggle_muted() == 0 && dm_loadpage_music_wanted() == 1);
    dm_loadpage_fail("x", 1); CHECK(dm_loadpage_music_wanted() == 0); dm_loadpage_set_muted(0);
    // 记住玩家的选择：平台启动时用 set_muted 告诉核心；begin / finish 都不改它，静音时完成也不放 | remember the player's choice: the platform tells the core with set_muted at start; begin / finish leave it alone and a muted page stays silent when done
    dm_loadpage_set_muted(1); dm_loadpage_begin(two, 2, w_two, 0); CHECK(dm_loadpage_muted() == 1 && dm_loadpage_music_wanted() == 0);
    dm_loadpage_finish(1); CHECK(dm_loadpage_muted() == 1 && dm_loadpage_music_wanted() == 0);
    dm_loadpage_begin(two, 2, w_two, 2); CHECK(dm_loadpage_muted() == 1 && dm_loadpage_music_wanted() == 0);
    dm_loadpage_set_muted(0); CHECK(dm_loadpage_muted() == 0 && dm_loadpage_music_wanted() == 1);
    dm_loadpage_layout(&duo, &L); CHECK(NEAR(L.mute.x + L.mute.w, 951 - 28 * kd) && NEAR(L.mute.y, 22 * kd) && NEAR(L.mute.w, 40 * kd) && L.mute.y + L.mute.h <= L.logo.y && L.brand.x + L.brand.w <= L.mute.x);
    CHECK(dm_loadpage_hit_mute(&L, L.mute.x + 20, L.mute.y + 20) && dm_loadpage_hit_mute(&L, L.mute.x - 10, L.mute.y - 10) && !dm_loadpage_hit_mute(&L, L.mute.x - 20, L.mute.y + 20) && !dm_loadpage_hit_mute(&L, 475, 400));
    dm_loadpage_layout(&ip, &L); CHECK(NEAR(L.mute.x + L.mute.w, 402 - 28) && L.mute.y >= 59 - 6 && L.mute.y + L.mute.h <= L.logo.y);   // iPhone 竖屏：在灵动岛下方 | iPhone portrait: below the island
    dm_loadpage_layout(&ipl, &L); CHECK(L.mute.x + L.mute.w <= 874 - 59 - 28 + 1e-6);                                                    // 横屏：避开右侧灵动岛 | landscape: clear of the island on the right

    // 8. 没开始就 finish / set：不崩、状态不变；begin 超过上限的阶段数被截 | 8. finish / set before begin: no crash, state unchanged; begin with too many stages is clamped
    dm_loadpage_begin(two, 0, NULL, 0); CHECK(dm_loadpage_state() == DM_LP_IDLE); dm_loadpage_set(0, 0.5, NULL, 1); dm_loadpage_finish(2); CHECK(dm_loadpage_state() == DM_LP_IDLE);
    dm_loadpage_text(&t, 3); CHECK(t.title[0] != 0);
    int many[12] = { DM_LP_IMPORT, DM_LP_IMPORT, DM_LP_IMPORT, DM_LP_IMPORT, DM_LP_IMPORT, DM_LP_IMPORT, DM_LP_IMPORT, DM_LP_IMPORT, DM_LP_IMPORT, DM_LP_IMPORT, DM_LP_IMPORT, DM_LP_IMPORT };
    dm_loadpage_begin(many, 12, NULL, 0); dm_loadpage_set(7, 1.0, NULL, 1); CHECK(dm_loadpage_percent() == 100); dm_loadpage_set(11, 0.5, NULL, 2); CHECK(dm_loadpage_percent() == 100);
    // 9. 其他语言列取得到（英文 / 韩文），占位符同序 | 9. Other language columns resolve (English / Korean) with matching placeholders
    dm_loadpage_ui_set_group(DM_LG_EN); dm_loadpage_begin(two, 2, w_two, 0); memset(&d, 0, sizeof d); d.items_done = 5; d.items_total = 9; dm_loadpage_set(1, 0.5, &d, 1);
    dm_loadpage_text(&t, 1); CHECK(!strcmp(t.title, "Preparing the shader cache")); CHECK(strstr(t.body, "File 5 of 9") != NULL);
    dm_loadpage_ui_set_group(DM_LG_KO); dm_loadpage_text(&t, 1); CHECK(strstr(t.body, "5 / 9") != NULL); dm_loadpage_ui_set_group(-1);

    // 10. 社区信息：没给 = 不显示、布局不变；给了（进行中）= 两行 + 复制钮贴底、在安全区内、进度组整体上移、不重叠；完成后不显示；失败仍显示；
    //     复制值按语言（中文先群号、英文先链接、只有一项用那一项）；复制反馈 2 秒；手柄模式；判定区；begin 不清社区信息
    // 10. Community info: not given = not shown, layout unchanged; given (running) = two lines + copy button at the bottom, inside the safe area, the progress group moved up, no overlap;
    //     not shown after done; still shown on failure; the copy value by language (Chinese prefers the number, English the link, a single item is used as is); 2 s copy feedback; controller mode; hit area; begin keeps the info
    {
        dm_lp_screen thor = { 832.5, 468.3, 0, 0, 0, 0 }, oppo = { 792, 363.4, 30, 0, 0, 0 }; dm_lp_layout L0, L1;   // 奥丁 1920×1080 @369 dpi、OPPO 2772×1272 @560 dpi（左侧挖孔约 30 点） | Thor 1920×1080 @369 dpi, OPPO 2772×1272 @560 dpi (left cutout about 30 pt)
        dm_loadpage_begin(two, 2, w_two, 0); dm_loadpage_set(0, 0.3, NULL, 10);
        dm_loadpage_layout(&thor, &L0); CHECK(L0.comm == 0 && !dm_loadpage_community_shown()); CHECK(!strcmp(dm_loadpage_copy_value(), ""));
        dm_loadpage_text(&t, 20); CHECK(!t.comm1[0] && !t.comm2[0] && !t.copy[0]); CHECK(!dm_loadpage_hit_copy(&L0, L0.sub.x + 10, L0.sub.y + 5));
        dm_loadpage_community("1129913678", "https://github.com/augustxxx-debug/d2r-mobile-plus", NULL, NULL);
        CHECK(dm_loadpage_community_shown()); CHECK(!strcmp(dm_loadpage_copy_value(), "1129913678"));
        dm_lp_screen scr[2] = { thor, oppo };
        for (int k = 0; k < 2; k++) { dm_loadpage_layout(&scr[k], &L1); double W = scr[k].w, H = scr[k].h;
            CHECK(L1.comm == 1); CHECK(rect_in(L1.comm1, scr[k].inset_l, 0, W - scr[k].inset_r, H - scr[k].inset_b) && rect_in(L1.comm2, 0, 0, W, H - scr[k].inset_b) && rect_in(L1.copy, 0, 0, W - scr[k].inset_r, H - scr[k].inset_b));
            CHECK(L1.sub.y + L1.sub.h <= L1.comm1.y - 10); CHECK(L1.comm1.y + L1.comm1.h <= L1.comm2.y); CHECK(L1.comm1.x + L1.comm1.w <= L1.copy.x - 8);
            CHECK(NEAR(L1.copy.x + L1.copy.w, W - scr[k].inset_r - 28)); CHECK(NEAR(L1.comm1.x, scr[k].inset_l + 28)); CHECK(L1.copy.h >= 22 && L1.copy.w >= 100 && L1.comm_pt == 12 && L1.comm_alpha > 0 && L1.comm_alpha < 255); CHECK(L1.copy.y >= L1.comm1.y && L1.copy.y + L1.copy.h <= L1.comm2.y + L1.comm2.h);
            CHECK(L1.logo.y + L1.logo.h <= L1.title.y - 24 + 1e-6); CHECK(L1.comm2.y + L1.comm2.h <= H - scr[k].inset_b - 16 + 1e-6); }
        dm_loadpage_layout(&thor, &L1); CHECK(L1.title.y < L0.title.y && L1.bar.y < L0.bar.y);
        CHECK(dm_loadpage_hit_copy(&L1, L1.copy.x + 5, L1.copy.y + 5) && dm_loadpage_hit_copy(&L1, L1.copy.x - 6, L1.copy.y) && !dm_loadpage_hit_copy(&L1, L1.copy.x - 12, L1.copy.y + 5) && !dm_loadpage_hit_mute(&L1, L1.copy.x + 5, L1.copy.y + 5));
        dm_loadpage_text(&t, 30); CHECK(!strcmp(t.comm1, "官方免费渠道：QQ 群 1129913678 · GitHub")); CHECK(!strcmp(t.comm2, "本软件免费，付费购买均为倒卖")); CHECK(!strcmp(t.copy, "复制群号") && t.copy_state == 0);
        dm_loadpage_copied(1, 0, 1000); dm_loadpage_text(&t, 1500); CHECK(!strcmp(t.copy, "已复制") && t.copy_state == 1);
        dm_loadpage_text(&t, 3001); CHECK(!strcmp(t.copy, "复制群号") && t.copy_state == 0);
        dm_loadpage_copied(0, 1, 4000); dm_loadpage_text(&t, 4100); CHECK(!strcmp(t.copy, "复制失败") && t.copy_state == -1); dm_loadpage_text(&t, 6100); CHECK(t.copy_state == 0);
        CHECK(dm_loadpage_pad_mode() == 0); dm_loadpage_pad(1); CHECK(dm_loadpage_pad_mode() == 1); dm_loadpage_pad(0); CHECK(dm_loadpage_pad_mode() == 0);
        dm_loadpage_ui_set_group(DM_LG_EN); CHECK(!strcmp(dm_loadpage_copy_value(), "https://github.com/augustxxx-debug/d2r-mobile-plus"));
        dm_loadpage_text(&t, 7000); CHECK(!strcmp(t.copy, "Copy link") && strstr(t.comm1, "QQ group 1129913678 · GitHub"));
        dm_loadpage_community("1129913678", NULL, NULL, NULL); CHECK(!strcmp(dm_loadpage_copy_value(), "1129913678")); dm_loadpage_text(&t, 7000); CHECK(!strcmp(t.copy, "Copy number") && !strstr(t.comm1, "GitHub"));
        dm_loadpage_ui_set_group(-1); dm_loadpage_community("", "https://github.com/a/b", "", NULL); CHECK(!strcmp(dm_loadpage_copy_value(), "https://github.com/a/b"));
        dm_loadpage_text(&t, 7000); CHECK(!strcmp(t.comm1, "官方免费渠道：GitHub") && !strcmp(t.copy, "复制链接"));
        dm_loadpage_begin(two, 2, w_two, 8000); CHECK(dm_loadpage_community_shown());   // begin 不清 | begin keeps it
        dm_loadpage_fail("x", 8100); CHECK(dm_loadpage_community_shown());
        dm_loadpage_begin(two, 2, w_two, 9000); dm_loadpage_finish(9100); CHECK(!dm_loadpage_community_shown()); dm_loadpage_layout(&thor, &L1); CHECK(L1.comm == 0);
        dm_loadpage_copied(1, 0, 9200); dm_loadpage_text(&t, 9300); CHECK(!t.copy[0]);   // 不显示时复制不记 | no copy recorded when hidden
        dm_loadpage_begin(two, 2, w_two, 9400); dm_loadpage_community(NULL, NULL, NULL, NULL); CHECK(!dm_loadpage_community_shown());
        // 有官网（用户 10-29）：显示官网（去 https:// 与结尾斜杠）+ 群号，复制群号（各语言都是）；只有官网时显示并复制官网 | with a site (user 10-29): site + number shown, the number is copied (every language); a site alone is shown and copied
        dm_loadpage_begin(two, 2, w_two, 9500); dm_loadpage_community("1129913678", "https://github.com/a/b", "https://d2rplus.augustusplay.com/", NULL);
        CHECK(!strcmp(dm_loadpage_copy_value(), "1129913678")); dm_loadpage_text(&t, 9600);
        CHECK(!strcmp(t.comm1, "官方免费渠道：官网 d2rplus.augustusplay.com · QQ 群 1129913678")); CHECK(!strcmp(t.copy, "复制群号"));
        dm_loadpage_ui_set_group(DM_LG_EN); CHECK(!strcmp(dm_loadpage_copy_value(), "1129913678")); dm_loadpage_text(&t, 9600); CHECK(!strcmp(t.copy, "Copy number") && strstr(t.comm1, "d2rplus.augustusplay.com · QQ group 1129913678")); dm_loadpage_ui_set_group(-1);
        dm_loadpage_copied(1, 0, 9700); dm_loadpage_community(NULL, NULL, "https://x.example/", NULL); CHECK(dm_loadpage_community_shown()); dm_loadpage_text(&t, 12000); CHECK(!strcmp(t.comm1, "官方免费渠道：官网 x.example"));
        CHECK(!strcmp(dm_loadpage_copy_value(), "https://x.example/") && !strcmp(t.copy, "复制官网"));
        // 国内镜像（RC2）：中文页脚在官网之后多一项「国内镜像」（最多三项），外文没有这一项；没有镜像时与上面完全一致 | mainland mirror (RC2): the Chinese footer gains a "国内镜像" item after the website (at most three), other languages do not; without a mirror it is exactly as above
        dm_loadpage_ui_set_group(DM_LG_ZH); dm_loadpage_community("1129913678", "https://github.com/a/b", "https://d2rplus.augustusplay.com/", "https://gitee.com/augustxxx-debug/d2r-mobile-plus");
        dm_loadpage_text(&t, 12500); CHECK(!strcmp(t.comm1, "官方免费渠道：QQ 群 1129913678 · 官网 d2rplus.augustusplay.com · 国内镜像 gitee.com/augustxxx-debug/d2r-mobile-plus"));
        dm_loadpage_ui_set_group(DM_LG_EN); dm_loadpage_text(&t, 12500); CHECK(!strstr(t.comm1, "Mirror") && strstr(t.comm1, "QQ group 1129913678"));
        dm_loadpage_ui_set_group(-1); dm_loadpage_community(NULL, NULL, NULL, NULL);
        // 提示页（权限）：三层字、无百分数、不画条、标题整宽、不放音乐、不显示页脚；begin 清掉提示 | notice page (permission): three levels, no percent, no bar, full-width title, no music, no footer; begin clears it
        dm_loadpage_prompt("dmLoadPermTitle", "用来读取 X", "dmLoadPermHint"); CHECK(dm_loadpage_prompting() && dm_loadpage_state() == DM_LP_IDLE);
        dm_loadpage_text(&t, 13000); CHECK(!strcmp(t.title, "需要「所有文件访问」权限") && !strcmp(t.body, "用来读取 X") && strstr(t.sub, "打开系统设置") && !t.percent[0] && !t.comm1[0]);
        dm_loadpage_layout(&oppo, &L1); CHECK(L1.show_bar == 0 && L1.comm == 0 && NEAR(L1.title.w, L1.bar.w) && L1.mute.w == 0 && !dm_loadpage_hit_mute(&L1, 0, 0)); CHECK(dm_loadpage_music_wanted() == 0);
        dm_loadpage_begin(two, 2, w_two, 13100); CHECK(!dm_loadpage_prompting()); dm_loadpage_layout(&oppo, &L1); CHECK(L1.show_bar == 1);
        { const int gs[5] = { DM_LG_ZH, DM_LG_EN, DM_LG_ES, DM_LG_RU, DM_LG_KO }; dm_loadpage_layout(&oppo, &L1);
          for (int k = 0; k < 5; k++) { dm_loadpage_ui_set_group(gs[k]); const char *ks[3] = { "dmLoadPermTitle", "dmLoadPermBody", "dmLoadPermHint" }; const double pts[3] = { 24, 18, 14 };
            for (int q = 0; q < 3; q++) { const char *s = dm_loadpage_ui(ks[q]); double est = 0;
              for (const unsigned char *c = (const unsigned char *)s; *c; ) { if (*c < 0x80) { est += 0.55; c++; } else if (*c < 0xe0) { est += 0.6; c += 2; } else { est += 1.0; c += 3; } }
              est *= pts[q]; if (est > L1.bar.w) printf("  宽度：组 %d 键 %s 估 %.0f > %.0f\n", gs[k], ks[q], est, L1.bar.w); CHECK(est <= L1.bar.w); } }
          dm_loadpage_ui_set_group(-1); }
        // 关字标（两端，PLUS 图标背景）：不画字标、文字字标为空；背景按图标外框**等比**缩放（两轴同一系数）、图边自然留底色；
        // 图标外框落在安全区顶与标题之间、在内容宽内；有没有页脚进度组位置不变；iPhone 17 Pro 竖 / 横、Duo 内屏、安卓两种屏都查
        // wordmark off (both platforms, PLUS icon background): no wordmark image, empty text wordmark; the background is scaled to the icon box with a **uniform** factor on both axes;
        // the icon box sits between the safe top and the title, within the content width; the progress group does not move with or without a footer; checked on iPhone 17 Pro portrait /
        // landscape, Duo inner and the two Android screens
        { dm_lp_screen scr2[5] = { thor, oppo, { 402, 874, 0, 59, 0, 34 }, { 874, 402, 59, 0, 59, 21 }, { 951, 669, 0, 0, 0, 0 } }; dm_loadpage_set_wordmark(0);
          for (int k = 0; k < 5; k++) { dm_lp_layout A, B; double W = scr2[k].w;
            dm_loadpage_begin(two, 2, w_two, 14000); dm_loadpage_community(NULL, NULL, NULL, NULL); dm_loadpage_layout(&scr2[k], &A);
            dm_loadpage_community("1129913678", NULL, "https://d2rplus.augustusplay.com/", NULL); dm_loadpage_layout(&scr2[k], &B);
            CHECK(A.logo.w == 0 && A.logo_name == NULL && NEAR(A.title.y, B.title.y) && NEAR(A.bg_dst.y, B.bg_dst.y) && NEAR(A.bg_dst.h, B.bg_dst.h));
            // 等比：两轴同一个缩放系数；宽方向多拉 1.3 倍时这两行报红（审计 P2-2 变异 M3） | uniform: both axes share one scale factor; stretching the width 1.3× fails these two lines (audit P2-2 mutation M3)
            CHECK(B.bg_dst.w > 0 && B.bg_dst.h > 0 && fabs(B.bg_dst.w / B.bg_dst.h - (double)B.bg_w / B.bg_h) < 1e-6);
            CHECK(fabs(B.bg_dst.w / B.bg_w - B.bg_dst.h / B.bg_h) < 1e-6);
            const double *ib = B.landscape ? k_icon_land : k_icon_port;   // 竖图用竖屏的图标外框 | the portrait image uses the portrait icon box
            double iy0 = B.bg_dst.y + B.bg_dst.h * ib[1], iy1 = B.bg_dst.y + B.bg_dst.h * ib[3], ix0 = B.bg_dst.x + B.bg_dst.w * ib[0], ix1 = B.bg_dst.x + B.bg_dst.w * ib[2];
            CHECK(iy0 >= scr2[k].inset_t + 28 - 0.5 && iy1 <= B.title.y - 18 + 0.5); CHECK(ix0 >= scr2[k].inset_l + 28 - 0.5 && ix1 <= W - scr2[k].inset_r - 28 + 0.5);
            CHECK(iy1 - iy0 > 120);   // 图标别缩得太小 | the icon must not shrink too far
            CHECK(B.bg_src.w == B.bg_w && B.bg_src.h == B.bg_h);
            dm_loadpage_text(&t, 14100); CHECK(t.brand[0] == 0); }
          dm_loadpage_set_wordmark(1); dm_loadpage_layout(&thor, &L1); CHECK(L1.logo.w > 0 && L1.logo_name != NULL); dm_loadpage_text(&t, 14200); CHECK(t.brand[0] != 0);
          dm_loadpage_community(NULL, NULL, NULL, NULL); }
        // 六套文字：第一行估宽（拉丁 / 西里尔 0.55 个字号、汉字 / 韩文 1.0 个字号）放得进 OPPO 的行宽，按钮字放得进 108 点（左右各留 6 点，手柄 A 图标另占一个字号 + 6 点）
        // six text sets: the first line's estimated width (Latin / Cyrillic 0.55 em, CJK / Hangul 1.0 em) fits OPPO's line width; button labels fit 128 pt (the controller A glyph takes 22 more)
        dm_loadpage_community("1129913678", "https://github.com/augustxxx-debug/d2r-mobile-plus", "https://d2rplus.augustusplay.com/", NULL); dm_loadpage_layout(&oppo, &L1);
        int groups[5] = { DM_LG_ZH, DM_LG_EN, DM_LG_ES, DM_LG_RU, DM_LG_KO };
        for (int k = 0; k < 5; k++) { dm_loadpage_ui_set_group(groups[k]);
            const char *keys[6] = { "dmLoadCommQqGh", "dmLoadCommFree", "dmLoadCommSiteQq", "dmLoadCommCopyQq", "dmLoadCommCopyGh", "dmLoadCommCopySite" };
            for (int q = 0; q < 6; q++) { char s[256]; if (q == 2) snprintf(s, sizeof s, dm_loadpage_ui(keys[q]), "d2rplus.augustusplay.com", "1129913678"); else snprintf(s, sizeof s, dm_loadpage_ui(keys[q]), "1129913678"); double est = 0;
                for (const unsigned char *c = (const unsigned char *)s; *c; ) { if (*c < 0x80) { est += 0.55; c++; } else if (*c < 0xe0) { est += 0.6; c += 2; } else { est += 1.0; c += 3; } }
                est *= L1.comm_pt; double room = q < 3 ? L1.comm1.w : L1.copy.w - 12 - (L1.comm_pt + 6);
                if (est > room) printf("  宽度：组 %d 键 %s 估 %.0f > %.0f\n", groups[k], keys[q], est, room);
                CHECK(est <= room); } }
        dm_loadpage_ui_set_group(-1); dm_loadpage_community(NULL, NULL, NULL, NULL);
    }

    // 11. 大屏适配（B139 返工）：静态尺寸（字号 / 边距 / 间距 / 页脚 / 条高 / 静音钮）按安全内容高缩放——长边 1000 点 / dp 以内保持手持原值，
    //     更大的屏（iPad Pro 11 横 1194×834、竖 834×1194，安全区上 24 / 下 20）取 k =（屏高 − 底安全区）/ 参照内容高：
    //     横屏 814 / 500 = 1.628（奥丁 468.3 那一档），竖屏 1174 / 840 = 1.3976（17 Pro 874 − 34）。
    //     验收：iPad 横屏三项比例对奥丁（图标 52.4% / 标题 62.2% / 进度组 37.8%）都在 ±5 个百分点内；竖屏对 17 Pro 竖（39.0% / 75.9% / 24.1%）同样。
    //     上一版按「长边 / 1000」（k = 1.194）时横屏图标 62.8%、标题 72.3%、条 78.7%，竖屏图标 58.6%、标题 80.6%——超线。
    //     奥丁（832.5×468.3）、Fold8 内屏（934.2×841.7）、Duo 内屏（951×669）、iPhone 17 Pro 横竖长边都 ≤ 1000，k 恰好是 1，矩形与修前逐位相同——这里逐项钉住。
    // 11. Large-screen fit (B139 rework): static metrics scale with the safe content height - up to 1000 pt / dp on the long side they keep the handheld
    //     values; larger screens (iPad Pro 11 1194×834, 834×1194, safe insets 24 top / 20 bottom) take k = (height - bottom inset) / reference content height:
    //     landscape 814 / 500 = 1.628 (the Thor band), portrait 1174 / 840 = 1.3976 (the 17 Pro's 874 - 34).
    //     Acceptance: the iPad's three ratios are within 5 points of the Thor in landscape (icon 52.4% / title 62.2% / group 37.8%) and of the 17 Pro portrait
    //     in portrait (39.0% / 75.9% / 24.1%). The previous long side / 1000 rule (k = 1.194) gave landscape icon 62.8%, title 72.3%, bar 78.7% and portrait
    //     icon 58.6%, title 80.6% - outside the line. The Thor (832.5×468.3), Fold8 inner (934.2×841.7), Duo inner (951×669) and iPhone 17 Pro both ways
    //     all have a long side ≤ 1000, so k is exactly 1 and every rectangle is bit-identical to before - pinned item by item below.
    {
        dm_lp_screen thor2 = { 832.5, 468.3, 0, 0, 0, 0 }, fold = { 934.2, 841.7, 0, 0, 0, 0 }, duo2 = { 951, 669, 0, 0, 0, 0 };
        dm_lp_screen ipad_l = { 1194, 834, 0, 24, 0, 20 }, ipad_p = { 834, 1194, 0, 24, 0, 20 };
        dm_lp_screen ipp = { 402, 874, 0, 59, 0, 34 }, ipl = { 874, 402, 59, 0, 59, 21 };
        dm_lp_layout L2, REF;
        dm_loadpage_set_wordmark(0); dm_loadpage_begin(two, 2, w_two, 15000); dm_loadpage_set(0, 0.5, NULL, 15001); dm_loadpage_set(1, 0.16, NULL, 15002);
        // 安卓手持：字号仍是 24 / 18 / 14，矩形与修前完全一致（同一份小数） | Android handheld: fonts stay 24 / 18 / 14 and the rectangles are exactly as before
        dm_loadpage_layout(&thor2, &L2);
        CHECK(L2.title_pt == 24 && L2.body_pt == 18 && L2.sub_pt == 14 && L2.comm_pt == 12 && L2.bar_radius == 3);
        CHECK(fabs(L2.title.x - 175.535) < 1e-9 && fabs(L2.title.y - 291.5) < 1e-9 && fabs(L2.title.w - 346.6296) < 1e-9 && fabs(L2.title.h - 32.4) < 1e-9);
        CHECK(fabs(L2.bar.x - 175.535) < 1e-9 && fabs(L2.bar.y - 335.9) < 1e-9 && fabs(L2.bar.w - 481.43) < 1e-9 && L2.bar.h == 8);
        CHECK(fabs(L2.body.y - 353.9) < 1e-9 && fabs(L2.sub.y - 385.1) < 1e-9 && L2.mute.w == 40 && fabs(L2.mute.x - 764.5) < 1e-9 && L2.mute.y == 22);
        CHECK(fabs(L2.bg_dst.x - 141.13609) < 1e-6 && fabs(L2.bg_dst.y - 18.724181) < 1e-6 && fabs(L2.bg_dst.w - 549.678142) < 1e-6 && fabs(L2.bg_dst.h - 309.193955) < 1e-6);
        // B140：折叠屏内屏（Fold8 934.2×841.7、Duo 951×669）长边不到 1000，但横屏内容高超过参照 500，同一条系数规则也要管：
        //     k = 内容高 / 500（Fold8 1.6834、Duo 1.338）。修前 k = 1，Fold8 图标 73.5% / 标题 79.0% / 条 84.3%（奥丁 52.4 / 62.2 / 71.7），
        //     Duo 标题 73.6%——都超线。验收同 iPad 横屏：四项对奥丁 ±5 个百分点。
        // B140: fold inner screens (Fold8 934.2×841.7, Duo 951×669) have a long side under 1000 but a landscape content height above the 500
        //     reference, so the same factor rule applies: k = content height / 500 (Fold8 1.6834, Duo 1.338). Before, k = 1 gave the Fold8 icon 73.5% /
        //     title 79.0% / bar 84.3% (Thor 52.4 / 62.2 / 71.7) and the Duo title 73.6% - outside the line. Acceptance as for the iPad landscape: four
        //     ratios within 5 points of the Thor.
        dm_loadpage_layout(&thor2, &REF);
        { double ri = icon_share(&REF, thor2.h), rt = at(&REF, thor2.h, 0), rb = at(&REF, thor2.h, 1), rg = at(&REF, thor2.h, 2);
          const dm_lp_screen *fs[2] = { &fold, &duo2 }; const char *fn[2] = { "Fold8 内屏", "Duo 内屏" };
          for (int i = 0; i < 2; i++) {
              dm_loadpage_layout(fs[i], &L2); double hh = fs[i]->h, kk = fmin(hh / 500.0, fs[i]->w / 762.0);
              CHECK(NEAR(L2.title_pt, 24 * kk) && NEAR(L2.body_pt, 18 * kk) && NEAR(L2.sub_pt, 14 * kk) && NEAR(L2.mute.w, 40 * kk) && NEAR(L2.bar.h, 8 * kk));
              CHECK(fabs(icon_share(&L2, hh) - ri) <= 0.05 && fabs(at(&L2, hh, 0) - rt) <= 0.05);
              CHECK(fabs(at(&L2, hh, 1) - rb) <= 0.05 && fabs(at(&L2, hh, 2) - rg) <= 0.05);
              CHECK(rect_in(L2.bar, 0, 0, fs[i]->w, hh) && rect_in(L2.title, 0, 0, fs[i]->w, hh) && rect_in(L2.sub, 0, 0, fs[i]->w, hh) && rect_in(L2.mute, 0, 0, fs[i]->w, hh));
              printf("  %s：图标 %.1f%% / 标题 %.1f%% / 条 %.1f%% / 进度组 %.1f%%（奥丁 %.1f / %.1f / %.1f / %.1f）\n", fn[i],
                  icon_share(&L2, hh) * 100, at(&L2, hh, 0) * 100, at(&L2, hh, 1) * 100, at(&L2, hh, 2) * 100, ri * 100, rt * 100, rb * 100, rg * 100); } }
        // 外屏 / 普通横屏手机内容高 ≤ 500：k 仍是 1（OPPO 20:9 在第 10 节逐位钉住）| outer screens / regular landscape phones have content ≤ 500: k stays 1 (OPPO 20:9 pinned bit-for-bit in section 10)
        { dm_lp_screen a209 = { 915, 412, 0, 0, 0, 0 }; dm_loadpage_layout(&a209, &L2); CHECK(L2.title_pt == 24 && L2.body_pt == 18 && L2.mute.w == 40); }   // 安卓锁横屏（sensorLandscape），竖屏只有 iOS：17 Pro 竖屏两个系数都恰好是 1，逐位钉在第 11 节 | Android is landscape-locked (sensorLandscape); portrait is iOS only, where the 17 Pro has both factors at exactly 1, pinned in section 11
        // iPhone 17 Pro 横竖也都在 1000 以内：字号与矩形同样逐位不变 | iPhone 17 Pro in both orientations is under 1000 too: same fonts and rectangles
        dm_loadpage_layout(&ipp, &L2);
        CHECK(L2.title_pt == 24 && L2.body_pt == 18 && fabs(L2.title.y - 663.2) < 1e-9 && fabs(L2.bar.w - 346) < 1e-9 && fabs(L2.bg_dst.x - (-2.900118)) < 1e-6 && fabs(L2.bg_dst.h - 722.809766) < 1e-6);
        dm_loadpage_layout(&ipl, &L2);
        CHECK(L2.title_pt == 24 && fabs(L2.title.y - 204.2) < 1e-9 && fabs(L2.bar.w - 434) < 1e-9 && fabs(L2.bg_dst.x - 259.716821) < 1e-6 && fabs(L2.bg_dst.h - 199.244332) < 1e-6);
        // 验收线（本轮返工第 1 条）：iPad 横屏对奥丁、竖屏对 17 Pro 竖，图标框高 / 标题顶 / 条顶 / 进度组整体四项比例都在 ±5 个百分点内。
        // 变异证明：把 dm_loadpage.c 的大屏系数改回 k = 1（横屏图标 62.8 / 标题 72.3 / 条 78.7，竖屏 58.6 / 80.6 / 85.1），下面这 8 行全部报红。
        // Acceptance line (rework item 1): iPad landscape against the Thor, portrait against the 17 Pro portrait - icon box height, title top, bar top and
        // whole group all within 5 points. Mutation: forcing the large-screen factor back to k = 1 in dm_loadpage.c (landscape icon 62.8 / title 72.3 / bar
        // 78.7, portrait 58.6 / 80.6 / 85.1) turns all eight lines below red.
        dm_loadpage_layout(&thor2, &REF);
        { double ri = icon_share(&REF, thor2.h), rt = at(&REF, thor2.h, 0), rb = at(&REF, thor2.h, 1), rg = at(&REF, thor2.h, 2);
          dm_loadpage_layout(&ipad_l, &L2);
          CHECK(fabs(icon_share(&L2, ipad_l.h) - ri) <= 0.05 && fabs(at(&L2, ipad_l.h, 0) - rt) <= 0.05);
          CHECK(fabs(at(&L2, ipad_l.h, 1) - rb) <= 0.05 && fabs(at(&L2, ipad_l.h, 2) - rg) <= 0.05);
          printf("  iPad 横：图标 %.1f%% / 标题 %.1f%% / 条 %.1f%% / 进度组 %.1f%%（奥丁 %.1f / %.1f / %.1f / %.1f）\n",
              icon_share(&L2, ipad_l.h) * 100, at(&L2, ipad_l.h, 0) * 100, at(&L2, ipad_l.h, 1) * 100, at(&L2, ipad_l.h, 2) * 100,
              ri * 100, rt * 100, rb * 100, rg * 100); }
        dm_loadpage_layout(&ipp, &REF);
        { double ri = icon_share(&REF, ipp.h), rt = at(&REF, ipp.h, 0), rb = at(&REF, ipp.h, 1), rg = at(&REF, ipp.h, 2);
          dm_loadpage_layout(&ipad_p, &L2);
          CHECK(fabs(icon_share(&L2, ipad_p.h) - ri) <= 0.05 && fabs(at(&L2, ipad_p.h, 0) - rt) <= 0.05);
          CHECK(fabs(at(&L2, ipad_p.h, 1) - rb) <= 0.05 && fabs(at(&L2, ipad_p.h, 2) - rg) <= 0.05);
          printf("  iPad 竖：图标 %.1f%% / 标题 %.1f%% / 条 %.1f%% / 进度组 %.1f%%（17 Pro 竖 %.1f / %.1f / %.1f / %.1f）\n",
              icon_share(&L2, ipad_p.h) * 100, at(&L2, ipad_p.h, 0) * 100, at(&L2, ipad_p.h, 1) * 100, at(&L2, ipad_p.h, 2) * 100,
              ri * 100, rt * 100, rb * 100, rg * 100); }
        // iPad：静态尺寸按 k = 1194/762 = 1.5669（横，B140 起宽系数卡住，进度组上提到 62.2%）/ 1.3976（竖）放大；图标框占屏比例与进度组相对位置逐项钉住（防止以后调参悄悄跑偏）
        // iPad: static metrics ×1.5669 (landscape, 1194/762: since B140 the width factor binds and the group is lifted to 62.2%) / ×1.3976 (portrait); icon box share and progress group position pinned item by item (so later tuning cannot drift silently)
        dm_loadpage_layout(&ipad_l, &L2);
        { const double ki = 1194 / 762.0; CHECK(NEAR(L2.title_pt, 24 * ki) && NEAR(L2.body_pt, 18 * ki) && NEAR(L2.sub_pt, 14 * ki) && NEAR(L2.mute.w, 40 * ki) && NEAR(L2.bar.h, 8 * ki)); }
        { const double *ib = k_icon_land;
          double ix0 = L2.bg_dst.x + L2.bg_dst.w * ib[0], iy0 = L2.bg_dst.y + L2.bg_dst.h * ib[1];
          double ix1 = L2.bg_dst.x + L2.bg_dst.w * ib[2], iy1 = L2.bg_dst.y + L2.bg_dst.h * ib[3];
          CHECK(fabs((iy1 - iy0) / ipad_l.h - 0.5068) < 0.002 && fabs((ix1 - ix0) / ipad_l.w - 0.3590) < 0.002);   // 图标框高 50.7%（奥丁 52.4%）/ 宽 35.9% | icon box 50.7% high (Thor 52.4%) / 35.9% wide
          const double ki = 1194 / 762.0; CHECK(iy0 >= 24 + 28 * ki - 0.5 && iy1 <= L2.title.y - 18 * ki + 0.5 && ix0 >= 28 * ki - 0.5 && ix1 <= 1194 - 28 * ki + 0.5); }
        CHECK(fabs(L2.title.y / ipad_l.h - 0.6220) < 0.002 && fabs(L2.bar.y / ipad_l.h - 0.7054) < 0.002);   // 标题 62.2%（奥丁 62.2%）/ 条 70.5%（71.7%） | title 62.2% (Thor 62.2%) / bar 70.5% (71.7%)
        dm_loadpage_layout(&ipad_p, &L2);
        CHECK(NEAR(L2.title_pt, 24 * 1.3976190476) && NEAR(L2.mute.w, 40 * 1.3976190476));
        { const double *ib = k_icon_port;   // 竖屏：图标框高封顶 40.0%（17 Pro 39.0%）、宽 58.1%；标题 77.6% / 条 82.8%（17 Pro 75.9% / 81.0%）
          double ix0 = L2.bg_dst.x + L2.bg_dst.w * ib[0], iy0 = L2.bg_dst.y + L2.bg_dst.h * ib[1];
          double ix1 = L2.bg_dst.x + L2.bg_dst.w * ib[2], iy1 = L2.bg_dst.y + L2.bg_dst.h * ib[3];
          CHECK(fabs((ix1 - ix0) / ipad_p.w - 0.5808) < 0.002 && fabs((iy1 - iy0) / ipad_p.h - 0.4000) < 0.002);
          CHECK(ix0 >= 28 * 1.3976190476 - 0.5 && ix1 <= 834 - 28 * 1.3976190476 + 0.5 && iy1 <= L2.title.y - 18 * 1.3976190476 + 0.5 && iy0 >= 24 + 28 * 1.3976190476 - 0.5); }
        CHECK(fabs(L2.title.y / ipad_p.h - 0.759) < 0.002 && fabs(L2.bar.y / ipad_p.h - 0.811) < 0.002);   // B140 起进度组按高系数渐变上提到 17 Pro 的 75.9% | since B140 the group is lifted (weighted by the height factor) to the 17 Pro's 75.9%
        // 12. B139 审计 P2-1 / P2-3（B140）：系数连续（长边 1000 / 1001 不跳）、封顶 2；近方形 Mac 窗口、超大窗口、页脚开、六语最长行都放得下。
        // 12. B139 audit P2-1 / P2-3 (B140): the factor is continuous (no step at a 1000 / 1001 long side) and capped at 2; near-square Mac windows,
        //     huge windows, the footer on, and the longest line of every language all fit.
        { dm_lp_layout A, B;
          dm_lp_screen e0 = { 1000, 600, 0, 0, 0, 0 }, e1 = { 1001, 600, 0, 0, 0, 0 }, p0 = { 600, 1000, 0, 0, 0, 0 }, p1 = { 600, 1001, 0, 0, 0, 0 };
          dm_loadpage_layout(&e0, &A); dm_loadpage_layout(&e1, &B); CHECK(fabs(A.title_pt - B.title_pt) < 0.05 && fabs(A.title.y - B.title.y) < 1.0);
          dm_loadpage_layout(&p0, &A); dm_loadpage_layout(&p1, &B); CHECK(fabs(A.title_pt - B.title_pt) < 0.05 && fabs(A.title.y - B.title.y) < 1.0);
          dm_lp_screen mq = { 1001, 1000, 0, 0, 0, 0 }; dm_loadpage_layout(&mq, &A); CHECK(NEAR(A.title_pt, 24 * 1001 / 762.0) && A.title_pt < 33);   // 审计时 k = 2.0（48 pt） | k = 2.0 (48 pt) at audit time
          CHECK(fabs(A.title.y / 1000 - 0.622) < 0.002);
          // 交叉审 P1-2：iPhone Air / Plus / Pro Max 竖屏（长边 ≤ 1000）k = 1、不上提 | cross-review P1-2: iPhone Air / Plus / Pro Max portrait (long side <= 1000): k = 1, no lift
          { const dm_lp_screen ph[3] = { { 420, 912, 0, 59, 0, 34 }, { 430, 932, 0, 59, 0, 34 }, { 440, 956, 0, 59, 0, 34 } };
            for (int i = 0; i < 3; i++) { dm_loadpage_layout(&ph[i], &A); CHECK(A.title_pt == 24 && A.mute.w == 40 && A.bar.h == 8); } }
          { dm_lp_screen pm = { 440, 956, 0, 59, 0, 34 }; dm_loadpage_set_wordmark(0); dm_loadpage_begin(two, 2, w_two, 19000); dm_loadpage_community(NULL, NULL, NULL, NULL);
            dm_loadpage_layout(&pm, &A); CHECK(fabs(A.title.y - 745.2) < 1e-6 && fabs(A.bar.y - 789.6) < 1e-6); }   // e1cd2e16 的逐位值 | e1cd2e16's exact values
          // 交叉审 P2：宽高系数交叉处（1194 宽、高 783 / 783.5）进度组不跳 | cross-review P2: no jump where the factors cross (1194 wide, 783 / 783.5 high)
          { dm_lp_screen c0 = { 1194, 783, 0, 0, 0, 0 }, c1 = { 1194, 783.5, 0, 0, 0, 0 }; dm_loadpage_layout(&c0, &A); dm_loadpage_layout(&c1, &B);
            CHECK(fabs(A.title.y / 783 - B.title.y / 783.5) < 0.005); }
          // 高系数跨过 1（横屏高 500 → 501、竖屏长边 1000 → 1001）也连续 | also continuous where the height factor crosses 1 (landscape height 500 -> 501, portrait long side 1000 -> 1001)
          { dm_lp_screen c0 = { 1000, 500, 0, 0, 0, 0 }, c1 = { 1000, 501, 0, 0, 0, 0 }; dm_loadpage_layout(&c0, &A); dm_loadpage_layout(&c1, &B); CHECK(fabs(A.title.y - B.title.y) < 2); }
          { dm_lp_screen c0 = { 700, 1000, 0, 0, 0, 0 }, c1 = { 700, 1001, 0, 0, 0, 0 }; dm_loadpage_layout(&c0, &A); dm_loadpage_layout(&c1, &B); CHECK(fabs(A.title.y - B.title.y) < 2 && fabs(A.title_pt - B.title_pt) < 0.1); }
          dm_lp_screen hu = { 2560, 1440, 0, 0, 0, 0 }, pt = { 1440, 2560, 0, 0, 0, 0 }; dm_loadpage_layout(&hu, &A); dm_loadpage_layout(&pt, &B); CHECK(NEAR(A.title_pt, 48) && NEAR(B.title_pt, 48));   // 封顶 2（审计时 2.88 / 3.05） | capped at 2 (2.88 / 3.05 at audit time)
          const dm_lp_screen ws[7] = { { 934.2, 841.7, 0, 0, 0, 0 }, { 951, 669, 0, 0, 0, 0 }, { 1194, 834, 0, 24, 0, 20 }, { 1366, 1024, 0, 24, 0, 20 }, { 1001, 1000, 0, 0, 0, 0 }, { 2560, 1440, 0, 0, 0, 0 }, { 834, 1194, 0, 24, 0, 20 } };
          const int gs[5] = { DM_LG_ZH, DM_LG_EN, DM_LG_ES, DM_LG_RU, DM_LG_KO }; const char *pk[3] = { "dmLoadPermTitle", "dmLoadPermBody", "dmLoadPermHint" };
          for (int i = 0; i < 7; i++) { double W = ws[i].w, H = ws[i].h;
              // 页脚开：两行 + 复制钮在安全区内、与进度组不重叠，图标在标题上方 | footer on: two lines + copy button inside the safe area, clear of the progress group, icon above the title
              dm_loadpage_begin(two, 2, w_two, 20000); dm_loadpage_community("1129913678", NULL, "https://d2rplus.augustusplay.com/", NULL); dm_loadpage_layout(&ws[i], &A);
              CHECK(A.comm == 1 && rect_in(A.comm1, 0, 0, W, H - ws[i].inset_b) && rect_in(A.copy, 0, 0, W, H - ws[i].inset_b) && A.sub.y + A.sub.h <= A.comm1.y - 10 && A.comm1.x + A.comm1.w <= A.copy.x - 8);
              CHECK(rect_in(A.title, 0, 0, W, H) && rect_in(A.bar, 0, 0, W, H) && A.title.y >= 0.3 * H);
              // 六语提示页三行放进条宽（估宽同第 10 节） | the six-language notice page's three lines fit the bar width (estimate as in section 10)
              dm_loadpage_prompt("dmLoadPermTitle", "x", "dmLoadPermHint"); dm_loadpage_layout(&ws[i], &B); const double pts[3] = { B.title_pt, B.body_pt, B.sub_pt };
              for (int g = 0; g < 5; g++) { dm_loadpage_ui_set_group(gs[g]);
                  for (int q = 0; q < 3; q++) { double e = 0; for (const unsigned char *c = (const unsigned char *)dm_loadpage_ui(pk[q]); *c; ) { if (*c < 0x80) { e += 0.55; c++; } else if (*c < 0xe0) { e += 0.6; c += 2; } else { e += 1.0; c += 3; } }
                      if (e * pts[q] > B.bar.w + 0.5) printf("  宽度：%gx%g 组 %d 键 %s 估 %.0f > %.0f\n", W, H, gs[g], pk[q], e * pts[q], B.bar.w);
                      CHECK(e * pts[q] <= B.bar.w + 0.5); } }
              dm_loadpage_ui_set_group(-1); dm_loadpage_begin(two, 2, w_two, 20001); dm_loadpage_community(NULL, NULL, NULL, NULL); } }
        dm_loadpage_set_wordmark(1);
    }

    printf("t_loadpage：%d 项检查，%d 项失败\n", checks, fails);
    return fails ? 1 : 0;
}
