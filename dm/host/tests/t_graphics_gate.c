// 图形页在不支持超分 / 分辨率档的平台（Android）：画质档固定原版、可见行紧挨着排；Apple（都支持）时行序与原来一致。只用临时设置文件。
// Graphics tab on platforms without upscaling / resolution levels (Android): the profile stays Original and the visible rows are packed; on Apple (both supported) the row order is unchanged. Temporary settings only.
#define DM_DISPLAY_HZ_TEST_HOOK
#define DM_GFX_GATE_TEST_HOOK
#include "../dm_custom.c"
#include <unistd.h>
#include <sys/stat.h>
static int quality;
void dm_3d_quality_mode_set(int value) { quality = value; }
int dm_lang_group(void) { return DM_LG_EN; }
const char *dm_save_commitdir(void) { return NULL; }
int dm_3d_quality_mode_get(void) { return quality; }
float dm_3d_scale_cur(void) { return .75f; }
int dm_display_max_hz(void) { return 60; }
static int icloud_ok = 1; int dm_icloud_supported(void) { return icloud_ok; }   // 平台限制：Android 没有 iCloud | per-platform: no iCloud on Android
static int up_ok = 0, res_ok = 0;   // 本测试扮演 Android：强定义盖掉弱定义 | this test plays Android: strong definitions override the weak ones
int mtl_upscale_supported(void) { return up_ok; }
int dm_render_res_supported(void) { return res_ok; }
static int checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main(void) {
    char dir[] = "/tmp/dm_gfx_gate_XXXXXX"; CHECK(mkdtemp(dir)); dm_custom_init(dir);
    char path[256]; snprintf(path, sizeof path, "%s/sw_custom.txt", dir);
    FILE *f = fopen(path, "w"); CHECK(f); fputs("upscale=1\nq3d=2\nq3d_v2=1\nrender_res=3\n", f); fclose(f);
    // Android：开关存着 1、档位 2，也固定原版 4 | Android: with the switch saved as 1 and profile 2 the result is still Original (4)
    atomic_store(&g_loaded, 0); CHECK(dm_custom_get("upscale") == 1 && dm_custom_get("q3d") == 2); CHECK(dm_3d_profile_get() == 4);
    // Apple：同一文件给档位 2 | Apple: the same file yields profile 2
    up_ok = 1; CHECK(dm_3d_profile_get() == 2); up_ok = 0;
    // 行打包 | row packing
    int v[6], n;
    n = graphics_rows_pack(1, 1, 1, 1, v); CHECK(n == 6 && v[0] == ROW_RES && v[1] == ROW_FPSHUD && v[2] == ROW_FPS && v[3] == ROW_UPSCALE && v[4] == ROW_PROFILE && v[5] == ROW_RT);   // Apple 全开：布局原位 | Apple, everything: layout order
    n = graphics_rows_pack(1, 1, 0, 1, v); CHECK(n == 5 && v[4] == ROW_RT);                   // 档位行藏着：光追上移一格（原行为）| profile hidden: ray tracing one slot up (original behaviour)
    n = graphics_rows_pack(1, 1, 0, 0, v); CHECK(n == 4 && v[3] == ROW_UPSCALE);
    n = graphics_rows_pack(0, 0, 0, 0, v); CHECK(n == 2 && v[0] == ROW_FPSHUD && v[1] == ROW_FPS);                  // Android：只剩帧率 | Android: only the frame rate
    n = graphics_rows_pack(1, 0, 0, 0, v); CHECK(n == 3 && v[0] == ROW_RES && v[1] == ROW_FPSHUD && v[2] == ROW_FPS);   // 显示帧数行永远在第二 | the Show-frame-rate row is always second   // Android（渲染分辨率档由安卓前端实现，无超分）：分辨率 + 帧率 | Android (render-resolution level by the Android front end, no upscaling): resolution + frame rate
    n = graphics_rows_pack(0, 0, 1, 1, v); CHECK(n == 3 && v[0] == ROW_FPSHUD && v[1] == ROW_FPS && v[2] == ROW_RT);   // 没有超分时档位行不可能出现 | the profile row cannot appear without upscaling
    // 自定义页的平台限制：有 iCloud 时 icloud 行在，没有时整页少一行、后面的行号前移（与 touch_layout.py DM_TARGET=android 对齐）
    // Per-platform Custom rows: with iCloud the icloud row exists; without it the page has one row fewer and later rows move up (matching touch_layout.py DM_TARGET=android)
    icloud_ok = 1; int rows_apple = custom_rows(); CHECK(rows_apple == NK - N_GFX && custom_key_at(6) == K_ICLOUD);
    icloud_ok = 0; CHECK(custom_rows() == rows_apple - 1 && custom_key_at(6) == K_ICLOUD + 1 && !custom_row(K_ICLOUD) && custom_key_at(rows_apple - 2) == K_TOUCH_SWAP);
    icloud_ok = 1;
    unlink(path); rmdir(dir);
    printf("PASS t_graphics_gate: %d checks\n", checks); return 0;
}
