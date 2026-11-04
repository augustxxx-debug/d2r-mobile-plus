// 选项页平台门控（CLAUDE.md §2.8，审计 r1 P2-3）：dm_custom.c 按安卓编（-D__ANDROID__），键表 k_opts 的平台列决定这一端出哪些行——
// 图形页没有超分组（MetalFX）、光追组；系统页没有 iCloud；游戏页没有掉落震动。Apple 一侧由 t_graphics_gate 核（都有）。只用临时设置文件
// Options platform gate (CLAUDE.md section 2.8, audit r1 P2-3): dm_custom.c built for Android (-D__ANDROID__); the platform column of the key table k_opts decides which
// rows this platform gets -- no upscaling (MetalFX) or ray-tracing group on the Graphics page, no iCloud on the System page, no drop haptics on the Gameplay page.
// The Apple side is checked by t_graphics_gate (all present). Temporary settings only
#define DM_DISPLAY_HZ_TEST_HOOK
#define DM_GFX_GATE_TEST_HOOK
#include "../dm_custom.c"
#include <unistd.h>
static int quality;
void dm_3d_quality_mode_set(int value) { quality = value; }
int dm_lang_group(void) { return DM_LG_EN; }
const char *dm_save_commitdir(void) { return NULL; }
int dm_3d_quality_mode_get(void) { return quality; }
float dm_3d_scale_cur(void) { return .75f; }
int dm_display_max_hz(void) { return 60; }
int dm_icloud_supported(void) { return 0; }   // nvn_vk.c 的强定义 | strong definition in nvn_vk.c
int mtl_upscale_supported(void) { return 0; }
int dm_render_res_supported(void) { return 1; }
static int checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main(void) {
    CHECK(PF_HERE == PF_ANDROID);
    { int up = 1, rt = 1; graphics_plat_rows(&up, &rt); CHECK(up == 0 && rt == 0); }   // 安卓不出 MetalFX 行、光追行 | no MetalFX or ray-tracing rows on Android
    CHECK(!fg_platform_ok() && opt_page(key_index("fgen_level")) == -1);   // 安卓没有 AI 帧生成这一行，决策层也不编（§2.8，审计 P2-1）；键还在（存盘对齐）| no AI frame generation row and no decision layer on Android (§2.8, audit P2-1); the key stays (settings alignment)
    int vis[8]; int up = 0, rt = 0; graphics_plat_rows(&up, &rt); int n = graphics_rows_pack(dm_render_res_supported(), up, 0, rt, 0, vis);
    CHECK(n == 4 && vis[0] == ROW_RES && vis[1] == ROW_BRIGHT && vis[2] == ROW_FPSHUD && vis[3] == ROW_FPS);   // 分辨率 / 加亮 / 显示帧数 / 帧率 | resolution / brightening / show frame rate / frame rate
    for (int i = 0; i < NK; i++) { const char *k = k_keys[i];
        if (!strcmp(k, "upscale") || !strcmp(k, "q3d") || !strcmp(k, "rt_level") || !strcmp(k, "fgen_level") || !strcmp(k, "icloud") || !strcmp(k, "drop_haptic"))
            CHECK(!row_on_page(i, PG_GFX) && !row_on_page(i, PG_GAME) && !row_on_page(i, PG_SYS)); }
    CHECK(row_on_page(key_index("bright_level"), PG_GFX) && row_on_page(key_index("render_res"), PG_GFX) && row_on_page(key_index("touch_swap_above"), PG_GAME));
    // RC2 并入的四个键在安卓的归属（方案 C × rt_level 移植）：光追档位只有 Apple；统计 / 自动上报崩溃在系统页；战斗中补腰带在游戏页「物品拾取」组
    // the four keys RC2 brought in, on Android (plan C x rt_level port): the ray-tracing level is Apple only; statistics / automatic crash reports on the System page; belt refill on the Gameplay page's Loot group
    CHECK(key_index("rt_level") >= 0 && opt_page(key_index("rt_level")) == -1);
    // AI 帧生成在安卓：键照读照存（设置文件跨端互通），档位非 0 也按关生效、存盘不改写、围栏不碰 | AI frame generation on Android: the key is read and saved (shared settings), a non-zero level takes effect as off, saves do not rewrite it, the fence leaves it alone
    { char dir[] = "/tmp/dm_opt_and_XXXXXX"; CHECK(mkdtemp(dir)); dm_custom_init(dir); char path[256]; snprintf(path, sizeof path, "%s/sw_custom.txt", dir);
      FILE *f = fopen(path, "w"); CHECK(f); fputs("fgen_level=2\nupscale=0\nq3d_v2=1\nexp_off_backup=fgen_level:2\n", f); fclose(f); atomic_store(&g_loaded, 0);
      CHECK(dm_custom_get("fgen_level") == DM_FGEN_SMOOTH && atomic_load(&g_bak[BAK_FGEN]) == DM_FGEN_SMOOTH);
      char what[256]; CHECK(dm_custom_safe_mode(what, sizeof what) == 0 && atomic_load(&g_val[K_FGEN]) == DM_FGEN_SMOOTH);
      CHECK(exp_restore_pending() == 0 && pending_value(K_FGEN) == DM_FGEN_SMOOTH);
      dm_custom_on_uimsg("OptionsPanelMessage", "ResetToDefaults", NULL); CHECK(pending_value(K_FGEN) == DM_FGEN_SMOOTH);
      dm_custom_on_uimsg("OptionsPanelMessage", "AcceptChanges", NULL); CHECK(dm_custom_get("fgen_level") == DM_FGEN_SMOOTH);
      char txt[2048] = {0}; f = fopen(path, "r"); CHECK(f); fread(txt, 1, sizeof txt - 1, f); fclose(f); CHECK(strstr(txt, "fgen_level=2\n") && strstr(txt, "fgen_level:2"));
      unlink(path); rmdir(dir); }
    CHECK(row_on_page(key_index("tel_stats"), PG_SYS) && row_on_page(key_index("auto_crash"), PG_SYS) && row_on_page(key_index("belt_refill"), PG_GAME));
    printf("PASS t_options_android: %d checks\n", checks); return 0;
}
