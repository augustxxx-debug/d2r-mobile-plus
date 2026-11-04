// 图形页帧率第三格文案：六语「帧率：高刷 %d（实验性）」按 90 / 120 展开都放得进 DM_FPS_CAPTION_MAX，不截断（Opus 审 REQ6 P1：俄语 72 字节曾超 64）。
// Graphics frame-rate third-stop caption: all six languages expanded with 90 / 120 fit DM_FPS_CAPTION_MAX untruncated (Opus REQ6 review P1: Russian at 72 bytes once overflowed 64).
#include <stdio.h>
#include <string.h>
#include "../dm_custom.h"
#include "../dm_i18n_tab.h"
int main(void) {
    int checks = 0, fails = 0, found = 0;
    for (size_t i = 0; i < sizeof k_dm_i18n / sizeof *k_dm_i18n; i++) {
        if (strcmp(k_dm_i18n[i].k, "dmGraphicsFrameRateHigh")) continue; found = 1;
        const char *col[6] = { k_dm_i18n[i].zh, k_dm_i18n[i].en, k_dm_i18n[i].es, k_dm_i18n[i].mx, k_dm_i18n[i].ru, k_dm_i18n[i].ko };
        for (int c = 0; c < 6; c++) for (int hz = 90; hz <= 120; hz += 30) {
            char t[DM_FPS_CAPTION_MAX]; int n = snprintf(t, sizeof t, col[c], hz); checks++;
            if (n < 0 || n >= (int)sizeof t) { fails++; printf("  ✗ 第 %d 列 %d：展开 %d 字节放不进 %d\n", c, hz, n, DM_FPS_CAPTION_MAX); }
            checks++; char want[8]; snprintf(want, sizeof want, "%d", hz); if (!strstr(t, want)) { fails++; printf("  ✗ 第 %d 列没写出 %d\n", c, hz); }
        }
    }
    checks++; if (!found) { fails++; printf("  ✗ 表里没有 dmGraphicsFrameRateHigh\n"); }
    printf("t_fps_caption: %d 项，%d 失败\n", checks, fails); return fails != 0;
}
