// B211：旧接口版（iOS 18–25，DM_NVN_M3=1）图形页不出帧生成、光追档位两行（图形层不编这两项）；Metal 4 包照旧出。
// 本文件编两遍：缺省一遍（Metal 4 包）与 -DDM_NVN_M3=1 一遍（旧接口版），断言随 DM_NVN_M3 翻转。
// B211: the older-API build (iOS 18-25, DM_NVN_M3=1) shows no frame-generation and no ray-tracing-level row on the Graphics page (the graphics layer does not
// compile them); the Metal 4 build still does. Built twice: default (Metal 4) and -DDM_NVN_M3=1 (older API); the assertions flip with DM_NVN_M3.
#ifndef DM_NVN_M3
#define DM_NVN_M3 0
#endif
#define DM_DISPLAY_HZ_TEST_HOOK
#define DM_GFX_GATE_TEST_HOOK
#include "../dm_custom.c"
static int quality;
void dm_3d_quality_mode_set(int value) { quality = value; }
int dm_lang_group(void) { return DM_LG_EN; }
const char *dm_save_commitdir(void) { return NULL; }
int dm_3d_quality_mode_get(void) { return quality; }
float dm_3d_scale_cur(void) { return .75f; }
int dm_display_max_hz(void) { return 120; }
int dm_icloud_supported(void) { return 1; }
module_t g_mod[MAX_MOD]; int g_nmod;
int dm_padmode_current(void) { return 0; }
uint32_t dm_ui_gen(void) { return 1; }
uint64_t dm_wref_get(dm_wref *r) { return r ? r->w : 0; }
static int checks, fails;
#define CHECK(x) do { checks++; if (!(x)) { fails++; fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); } } while (0)
int main(void) {
    int rl = key_index("rt_level"), fg = key_index("fgen_level"), up = key_index("upscale"), q3 = key_index("q3d");
    CHECK(rl >= 0 && fg >= 0 && up >= 0 && q3 >= 0);
    int up_show, rt_show; graphics_plat_rows(&up_show, &rt_show);
#if DM_NVN_M3
    CHECK(opt_page(rl) == -1); CHECK(opt_page(fg) == -1);   // 旧接口版：两行不出 | older API: both rows absent
    CHECK(!rt_show); CHECK(!fg_row_show()); CHECK(!fg_platform_ok());
#else
    CHECK(opt_page(rl) == PG_GFX); CHECK(opt_page(fg) == PG_GFX);   // Metal 4 包照旧 | Metal 4 build unchanged
    CHECK(rt_show); CHECK(fg_row_show()); CHECK(fg_platform_ok());
#endif
    CHECK(opt_page(up) == PG_GFX && up_show); CHECK(opt_page(q3) == PG_GFX);   // MetalFX 超分与动态 3D 两版都有 | MetalFX upscaling and dynamic 3D exist in both
    { int v[9], n = graphics_rows_pack(1, up_show, 1, rt_show, fg_row_show(), v), has_rt = 0, has_fg = 0;
      for (int i = 0; i < n; i++) { has_rt |= v[i] == ROW_RT; has_fg |= v[i] == ROW_FG; }
      CHECK(has_rt == !DM_NVN_M3); CHECK(has_fg == !DM_NVN_M3); }   // 手柄行序里也跟着有 / 没有 | the controller row order follows
    printf("t_m3_rows%s: %d 项检查，%d 项失败\n", DM_NVN_M3 ? "（旧接口版）" : "", checks, fails);
    return fails != 0;
}
