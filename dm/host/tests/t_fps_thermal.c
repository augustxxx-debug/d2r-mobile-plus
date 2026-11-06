// 帧率只听设置（B147，用户 2026-11-04）：原先 120 帧档温控 ≥ 2 自动退回 60、回落 60 秒再回 120（2026-10-30，B060），Duo 开局约 157 秒就退 60。
// 现在：温控 0–3 任何档、任意时长，30 / 60 / 90 / 120 都按设置出；dm_fps_thermal_capped 恒 0；设置文件不改；不再写「过热 … 自动退回 60」日志。
// 过热提示（B147 ③）：dm_thermal_hint_step 第一次到 ≥ 2 返回 1（每次启动一次），之后、以及 0 / 1 / 未知（−1）都返回 0。
// 假时钟 + 假温控，只用临时目录，不碰真存档。把 dm_fps_setting 里的温控退回加回去，第 2 / 2b / 6 段会红；把提示改成每次都弹，第 7 段会红
// The frame rate follows the setting only (B147, user 2026-11-04): the 120 fps mode used to fall back to 60 at thermal >= 2 and return after 60 s cool (2026-10-30, B060); the Duo dropped to 60 about 157 s into a run.
// Now: at any thermal state 0-3 for any length of time, 30 / 60 / 90 / 120 follow the setting; dm_fps_thermal_capped is always 0; the settings file is untouched; no "hot ... falls back to 60" log line.
// Overheating tip (B147 ③): dm_thermal_hint_step returns 1 the first time it reaches >= 2 (once per launch) and 0 afterwards, and for 0 / 1 / unknown (-1).
// Fake clock + fake thermal state, temporary directories only, never real saves. Putting the thermal fallback back into dm_fps_setting turns blocks 2 / 2b / 6 red; making the tip fire every time turns block 7 red
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
static int display_hz = 120;   // 屏幕最高刷新率替身（本测试在 120 与 90 之间换） | stand-in for the screen's maximum refresh rate (this test switches between 120 and 90)
int dm_display_max_hz(void) { return display_hz; }
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

    // 2. 120 档：温控 0 → 2 → 3 → 1 各停很久，始终 120；设置文件不改 | 2. 120 mode: thermal 0 -> 2 -> 3 -> 1, each held for a long time, always 120; settings file untouched
    settings("eco_fps=2\nq3d=3\nq3d_v2=1\nupscale=1\n");
    thermal = 0; CHECK(dm_fps_setting() == 120); CHECK(!dm_fps_thermal_capped());
    thermal = 2; fake_ns += SEC; CHECK(dm_fps_setting() == 120); CHECK(!dm_fps_thermal_capped());   // B147：原先这里是 60 | B147: this used to be 60
    fake_ns += 600 * SEC; CHECK(dm_fps_setting() == 120);
    thermal = 3; fake_ns += 600 * SEC; CHECK(dm_fps_setting() == 120); CHECK(!dm_fps_thermal_capped());
    thermal = 1; fake_ns += SEC; CHECK(dm_fps_setting() == 120);
    { FILE *f = fopen(path, "r"); char text[512] = {0}; fread(text, 1, sizeof text - 1, f); fclose(f); CHECK(strstr(text, "eco_fps=2")); }
    thermal = 2; fake_ns += SEC; CHECK(dm_3d_profile_get() == 2); CHECK(fps_pos(pending_value(K_FPS)) == 2);   // 超分上限仍按选的 120 | the Quality cap still follows the selected 120

    // 2b. 90 Hz 屏：第三档 = 90，热了也是 90 | 2b. 90 Hz screen: third stop = 90, also when hot
    { display_hz = 90; settings("eco_fps=2\n"); thermal = 2; fake_ns += SEC; CHECK(dm_fps_setting() == 90); CHECK(!dm_fps_thermal_capped());
      thermal = 3; fake_ns += 300 * SEC; CHECK(dm_fps_setting() == 90); display_hz = 120; }

    // 3. 60 / 30 档照旧 | 3. the 60 / 30 modes as before
    settings("eco_fps=0\n"); thermal = 3; fake_ns += SEC; CHECK(dm_fps_setting() == 60); CHECK(!dm_fps_thermal_capped());
    settings("eco_fps=1\n"); fake_ns += SEC; CHECK(dm_fps_setting() == 30); CHECK(!dm_fps_thermal_capped());

    // 4. DM_THERMAL_NOFALLBACK（功耗测量）仍是 120 | 4. DM_THERMAL_NOFALLBACK (power measurement) still 120
    settings("eco_fps=2\n"); thermal = 2; setenv("DM_THERMAL_NOFALLBACK", "1", 1); fake_ns += 100 * SEC; CHECK(dm_fps_setting() == 120); unsetenv("DM_THERMAL_NOFALLBACK");

    // 5. 测试开关 DM_TEST_THERMAL=<档>[@秒]…（Mac 验证与过热提示用）| 5. test switch DM_TEST_THERMAL (Mac checks and the overheating tip)
    CHECK(thermal_schedule("2", 0) == 2); CHECK(thermal_schedule("0@0,2@30,0@60", 45) == 2); CHECK(thermal_schedule("0@0,2@30,0@60", 61) == 0); CHECK(thermal_schedule("4", 0) == -1); CHECK(thermal_schedule("x", 0) == -1);
    thermal = 3; CHECK(dm_thermal_eff() == 3);   // 没设测试开关：读前端的温控 | no test switch: reads the front end's thermal state

    // 6. 日志：热了不再出现「自动退回 60」 | 6. log: no "falls back to 60" line when hot
    { char logp[128]; snprintf(logp, sizeof logp, "%s/log.txt", dir); settings("eco_fps=2\n"); thermal = 0; fake_ns += 200 * SEC; dm_fps_setting();
      int saved = dup(2); FILE *r = freopen(logp, "w", stderr); (void)r; thermal = 2; fake_ns += SEC; dm_fps_setting(); thermal = 3; fake_ns += 90 * SEC; dm_fps_setting(); fflush(stderr); dup2(saved, 2); close(saved);
      FILE *f = fopen(logp, "r"); char text[2048] = {0}; fread(text, 1, sizeof text - 1, f); fclose(f); unlink(logp);
      CHECK(!strstr(text, "自动退回 60")); CHECK(!strstr(text, "帧率档：生效 60 帧")); }

    // 7. 过热提示只一次：−1 / 0 / 1 不提示，第一次 ≥ 2 提示，之后 2 / 3 / 回落再热都不提示 | 7. the overheating tip fires once
    { int shown = 0; CHECK(!dm_thermal_hint_step(&shown, -1)); CHECK(!dm_thermal_hint_step(&shown, 0)); CHECK(!dm_thermal_hint_step(&shown, 1));
      CHECK(dm_thermal_hint_step(&shown, 2) == 1); CHECK(!dm_thermal_hint_step(&shown, 2)); CHECK(!dm_thermal_hint_step(&shown, 3));
      CHECK(!dm_thermal_hint_step(&shown, 0)); CHECK(!dm_thermal_hint_step(&shown, 2));
      int s2 = 0; CHECK(dm_thermal_hint_step(&s2, 3) == 1); }   // 直接到危急也提示 | straight to critical also tips

    unlink(path); rmdir(dir);
    printf("t_fps_thermal: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
