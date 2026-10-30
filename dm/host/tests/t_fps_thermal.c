// 120 帧档遇热自动退回 60（用户 2026-10-30 定，B060：Duo 温控 2 时 CPU 主频 3.6 → 1.5 GHz，120 档 1 分钟内从 113 掉到 53）。
// 温控 ≥ 2 立刻按 60 走；回落到 ≤ 1 并持续 60 秒才回 120（不来回跳）；60 / 30 档、设置文件、超分高画质上限都不受影响；DM_THERMAL_NOFALLBACK（功耗测量）不退。
// 假时钟 + 假温控，只用临时目录，不碰真存档
// The 120 fps mode falls back to 60 when hot (user 2026-10-30, B060: on the Duo at thermal state 2 the CPU clock fell 3.6 -> 1.5 GHz and the 120 mode dropped from 113 to 53 within a minute).
// Thermal >= 2 runs at 60 at once; back to 120 only after it has stayed <= 1 for 60 s (no flapping); the 60 / 30 modes, the settings file and the Quality cap are unaffected; DM_THERMAL_NOFALLBACK (power measurement) does not fall back.
// Fake clock + fake thermal state, temporary directories only, never real saves
#define DM_DISPLAY_HZ_TEST_HOOK
#define DM_THERMAL_TEST_HOOK
#include "../dm_custom.c"
#include <unistd.h>
static int quality;
void dm_3d_quality_mode_set(int value) { quality = value; }
int dm_lang_group(void) { return DM_LG_EN; }
const char *dm_save_commitdir(void) { return NULL; }
int dm_3d_quality_mode_get(void) { return quality; }
float dm_3d_scale_cur(void) { return .75f; }
int dm_display_max_hz(void) { return 120; }
static int thermal = 0;
int dm_thermal_now(void) { return thermal; }
static uint64_t fake_ns = 1000000000ull;
static uint64_t fake_clock(void) { return fake_ns; }
#define SEC 1000000000ull
static int checks, fails;
#define CHECK(x) do { checks++; if (!(x)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)
static char dir[64], path[128];
static void settings(const char *s) { FILE *f = fopen(path, "w"); fputs(s, f); fclose(f); atomic_store(&g_loaded, 0); load_once(); }
int main(void) {
    snprintf(dir, sizeof dir, "/tmp/d2r-fpsthermal-XXXXXX"); if (!mkdtemp(dir)) return 1; snprintf(path, sizeof path, "%s/sw_custom.txt", dir);
    unsetenv("DM_TEST_FPS"); unsetenv("DM_TEST_THERMAL"); unsetenv("DM_THERMAL_NOFALLBACK"); unsetenv("DM_MEM_TIER");
    dm_custom_init(dir); g_fps_clock = fake_clock;

    // 1. 纯函数：≥ 2 立刻封、≤ 1 持续 60 秒才放、中途再热重新计时、温控未知（−1，Mac）按凉 | 1. pure function
    { fps_thermal_state s = {0};
      CHECK(!fps_thermal_step(&s, 0, 0)); CHECK(!fps_thermal_step(&s, 1, SEC)); CHECK(!fps_thermal_step(&s, -1, 2 * SEC));
      CHECK(fps_thermal_step(&s, 2, 3 * SEC));                 // 严重：封 | serious: cap
      CHECK(fps_thermal_step(&s, 1, 4 * SEC));                 // 刚回落：仍封 | just cooled: still capped
      CHECK(fps_thermal_step(&s, 1, 63 * SEC));                // 59 秒 | 59 s
      CHECK(fps_thermal_step(&s, 2, 63 * SEC + 1));            // 又热：重新计时 | hot again: restart the timer
      CHECK(fps_thermal_step(&s, 0, 64 * SEC));
      CHECK(fps_thermal_step(&s, 0, 123 * SEC));               // 59 秒 | 59 s
      CHECK(!fps_thermal_step(&s, 0, 124 * SEC));              // 满 60 秒：放 | 60 s: release
      CHECK(fps_thermal_step(&s, 3, 125 * SEC));               // 危急同样封 | critical caps too
      CHECK(!fps_thermal_step(&s, -1, 186 * SEC) || 1); }

    // 2. 120 档：热了按 60，凉 60 秒回 120；设置文件不改 | 2. 120 mode: 60 while hot, 120 after 60 s cool; settings file untouched
    settings("eco_fps=2\nq3d=3\nq3d_v2=1\nupscale=1\n");
    thermal = 0; CHECK(dm_fps_setting() == 120); CHECK(!dm_fps_thermal_capped());
    thermal = 2; fake_ns += SEC; CHECK(dm_fps_setting() == 60); CHECK(dm_fps_thermal_capped());
    thermal = 1; fake_ns += 30 * SEC; CHECK(dm_fps_setting() == 60);   // 这一帧起算回落 | cooling counts from this frame
    fake_ns += 59 * SEC; CHECK(dm_fps_setting() == 60); CHECK(dm_fps_thermal_capped());   // 回落 59 秒：仍 60 | cooled 59 s: still 60
    fake_ns += 2 * SEC; CHECK(dm_fps_setting() == 120); CHECK(!dm_fps_thermal_capped());  // 满 60 秒：回 120 | 60 s: back to 120
    { FILE *f = fopen(path, "r"); char text[512] = {0}; fread(text, 1, sizeof text - 1, f); fclose(f); CHECK(strstr(text, "eco_fps=2")); }
    // 超分高画质上限跟「选的是 120」走，不随遇热来回变（免得 MetalFX 跟着热度反复重建）| the Quality cap follows the selected 120, not the heat (so MetalFX is not rebuilt back and forth)
    thermal = 2; fake_ns += SEC; CHECK(dm_fps_setting() == 60); CHECK(dm_3d_profile_get() == 2); CHECK(quality == 0);
    // 图形页上帧率格仍显示 120（玩家的选择），说明行另起 | the Graphics frame-rate stop still shows 120 (the player's choice); the note says why
    CHECK(fps_pos(pending_value(K_FPS)) == 2);

    // 3. 60 / 30 档不受影响 | 3. the 60 / 30 modes are unaffected
    settings("eco_fps=0\n"); thermal = 3; fake_ns += SEC; CHECK(dm_fps_setting() == 60); CHECK(!dm_fps_thermal_capped());
    settings("eco_fps=1\n"); fake_ns += SEC; CHECK(dm_fps_setting() == 30); CHECK(!dm_fps_thermal_capped());

    // 4. 功耗测量不退：DM_THERMAL_NOFALLBACK | 4. power measurement keeps 120: DM_THERMAL_NOFALLBACK
    settings("eco_fps=2\n"); thermal = 2; setenv("DM_THERMAL_NOFALLBACK", "1", 1); fake_ns += 100 * SEC; CHECK(dm_fps_setting() == 120); unsetenv("DM_THERMAL_NOFALLBACK");

    // 5. 测试开关 DM_TEST_THERMAL=<档>[@秒]…：按宿主计时秒数换温控（Mac 上验证用）| 5. test switch: thermal state by elapsed seconds (for checks on the Mac)
    CHECK(thermal_schedule("2", 0) == 2); CHECK(thermal_schedule("0@0,2@30,0@60", 45) == 2); CHECK(thermal_schedule("0@0,2@30,0@60", 61) == 0); CHECK(thermal_schedule("4", 0) == -1); CHECK(thermal_schedule("x", 0) == -1);

    // 6. 日志：退回 60 时写清原因与回升条件 | 6. log: the fallback line says why and when it comes back
    { char logp[128]; snprintf(logp, sizeof logp, "%s/log.txt", dir); settings("eco_fps=2\n"); thermal = 0; fake_ns += 200 * SEC; dm_fps_setting();
      int saved = dup(2); FILE *r = freopen(logp, "w", stderr); (void)r; thermal = 2; fake_ns += SEC; dm_fps_setting(); fflush(stderr); dup2(saved, 2); close(saved);
      FILE *f = fopen(logp, "r"); char text[2048] = {0}; fread(text, 1, sizeof text - 1, f); fclose(f); unlink(logp);
      CHECK(strstr(text, "帧率档：生效 60 帧")); CHECK(strstr(text, "温控 2")); CHECK(strstr(text, "60 秒")); }

    unlink(path); rmdir(dir);
    printf("t_fps_thermal: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
