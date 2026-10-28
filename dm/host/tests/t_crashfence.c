// 崩溃围栏（发布前闸 1）：运行标记写 / 清、连续异常退出计数、第三次启动（连续两次之后）实验项全关并写回设置文件、正常结束后清零、iOS 进后台 / 回前台、测试开关 DM_TEST_CRASH_MARK、
// 主菜单只提示一次。只用临时目录，不碰真存档。「重启进程」= 把两个模块的静态状态清回初值，再调 dm_crashfence_start
// Crash fence (release gate 1): running marker written / cleared, consecutive abnormal-exit count, on the third start (after two in a row) every experimental feature is off and saved to the settings file, the count resets after a normal end, iOS background / foreground, the test switch DM_TEST_CRASH_MARK,
// the main menu notice appears only once. Temporary directories only, never real saves. "Restart the process" = reset the static state of both modules to its initial values, then call dm_crashfence_start
#define DM_DISPLAY_HZ_TEST_HOOK
#define DM_CRASHFENCE_TEST
#include "../dm_custom.c"
#include "../dm_crashfence.c"
#include <unistd.h>
#include <sys/stat.h>
void dm_3d_quality_mode_set(int value) { (void)value; }
int dm_lang_group(void) { return DM_LG_EN; }
const char *dm_save_commitdir(void) { return NULL; }
int dm_3d_quality_mode_get(void) { return 0; }
float dm_3d_scale_cur(void) { return .75f; }
int dm_display_max_hz(void) { return 120; }
const char *dm_ui(const char *key) { return key; }
static int notices; static char notice_title[64];
void front_notice(const char *title, const char *msg) { (void)msg; notices++; snprintf(notice_title, sizeof notice_title, "%s", title); }
static int checks, fails;
#define CHECK(x) do { checks++; if (!(x)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)
static char dir[64], marker[128], settings[128];
static void restart(void) { g_started = g_marked = g_streak = g_test = 0; atomic_store(&g_notice, 0); atomic_store(&g_loaded, 0); }
static int marker_streak(void) { FILE *f = fopen(marker, "r"); if (!f) return -1; int k = -2; if (fscanf(f, "streak=%d", &k) != 1) k = -2; fclose(f); return k; }
static void read_settings(char *out, size_t n) { FILE *f = fopen(settings, "r"); size_t r = f ? fread(out, 1, n - 1, f) : 0; out[r] = 0; if (f) fclose(f); }
static void write_settings(const char *s) { FILE *f = fopen(settings, "w"); fputs(s, f); fclose(f); }
int main(void) {
    snprintf(dir, sizeof dir, "/tmp/d2r-crashfence-XXXXXX"); if (!mkdtemp(dir)) return 1;
    snprintf(marker, sizeof marker, "%s/dm_running.txt", dir); snprintf(settings, sizeof settings, "%s/sw_custom.txt", dir);
    unsetenv("DM_TEST_CRASH_MARK"); unsetenv("DM_SAVE");
    dm_custom_init(dir);
    write_settings("eco_fps=2\nrt_shadow=1\nupscale=1\nq3d=0\nq3d_v2=1\nauto_mana=1\n");   // 玩家开了 120 帧、光追、MetalFX | the player turned on 120 fps, ray tracing, MetalFX
    char text[4096];

    // 1. 第一次启动：没有残留，写标记 streak=0 | 1. First start: no leftover, marker streak=0 written
    restart(); CHECK(dm_crashfence_start() == 0); CHECK(marker_streak() == 0);
    // 2. 上一局没清标记就没了（闪退）：第 1 次，实验项不动 | 2. Previous run vanished without clearing (crash): number 1, experimental features untouched
    restart(); CHECK(dm_crashfence_start() == 1); CHECK(marker_streak() == 1);
    CHECK(dm_custom_get("eco_fps") == FPS_120); CHECK(dm_custom_get("rt_shadow") == 1); CHECK(dm_custom_get("upscale") == 1); CHECK(dm_crashfence_notice_take() == 0);
    // 3. 又闪退：第三次启动看到连续 2 次 → 实验项全关、写回设置文件、主菜单提示一次 | 3. Crashed again: the third start sees 2 in a row → experimental features off, saved, one main-menu notice
    restart(); CHECK(dm_crashfence_start() == 2); CHECK(marker_streak() == 2);
    CHECK(dm_custom_get("eco_fps") == 0); CHECK(dm_custom_get("rt_shadow") == 0); CHECK(dm_custom_get("upscale") == 0);
    read_settings(text, sizeof text); CHECK(strstr(text, "eco_fps=0\n")); CHECK(strstr(text, "rt_shadow=0\n")); CHECK(strstr(text, "upscale=0\n")); CHECK(strstr(text, "auto_mana=1\n"));
    atomic_store(&g_loaded, 0); CHECK(dm_custom_get("rt_shadow") == 0);   // 重新读盘也是关 | off after re-reading from disk too
    dm_crashfence_main_menu(); CHECK(notices == 1); CHECK(!strcmp(notice_title, "dmSafeModeTitle"));
    dm_crashfence_main_menu(); CHECK(notices == 1);   // 只提示一次 | only once
    // 4. 这一局正常结束：标记清掉；下次启动清零 | 4. This run ends normally: marker cleared; the next start is back to zero
    dm_crashfence_clean("优雅退出"); CHECK(access(marker, F_OK) != 0);
    restart(); CHECK(dm_crashfence_start() == 0); CHECK(marker_streak() == 0);
    // 5. 正常启动后玩家又开了实验项，再闪退一次：只是第 1 次，不关 | 5. After a normal start the player turns the features back on and it crashes once: only number 1, nothing turned off
    write_settings("eco_fps=2\nrt_shadow=1\nupscale=1\nq3d=0\nq3d_v2=1\n");
    restart(); CHECK(dm_crashfence_start() == 1); CHECK(dm_custom_get("rt_shadow") == 1); CHECK(dm_custom_get("eco_fps") == FPS_120);
    // 6. iOS 进后台清标记、连续次数清零；回前台重写（之后在前台闪退算第 1 次） | 6. iOS background clears the marker and the streak; foreground writes it again (a later foreground crash is number 1)
    dm_crashfence_clean("进后台"); CHECK(access(marker, F_OK) != 0);
    dm_crashfence_resume(); CHECK(marker_streak() == 0);
    restart(); CHECK(dm_crashfence_start() == 1);
    // 7. 测试开关 DM_TEST_CRASH_MARK=2：不管盘上的标记，按连续 2 次处理 | 7. Test switch DM_TEST_CRASH_MARK=2: regardless of the marker on disk, treated as 2 in a row
    dm_crashfence_clean("优雅退出"); write_settings("eco_fps=2\nrt_shadow=1\nupscale=0\nq3d=0\nq3d_v2=1\n");
    setenv("DM_TEST_CRASH_MARK", "2", 1); restart(); notices = 0;
    CHECK(dm_crashfence_start() == 2); CHECK(dm_custom_get("eco_fps") == 0); CHECK(dm_custom_get("rt_shadow") == 0); CHECK(dm_crashfence_notice_take() == 1);
    unsetenv("DM_TEST_CRASH_MARK");
    // 8. 实验项本来都关着：照样提示，但设置文件不重写 | 8. Experimental features already off: still notify, but the settings file is not rewritten
    write_settings("eco_fps=0\nrt_shadow=0\nupscale=0\nq3d=0\nq3d_v2=1\n"); struct stat st0; stat(settings, &st0);
    char what[256]; atomic_store(&g_loaded, 0); CHECK(dm_custom_safe_mode(what, sizeof what) == 0); CHECK(strstr(what, "本来都关着"));
    read_settings(text, sizeof text); CHECK(!strstr(text, "auto_mana"));   // 没有重写成全量键表 | not rewritten into the full key table
    // 9. 坏标记（内容乱）也算一次异常退出 | 9. A garbled marker still counts as one abnormal exit
    { FILE *f = fopen(marker, "w"); fputs("garbage", f); fclose(f); } restart(); CHECK(dm_crashfence_start() == 1);
    // 11. 测试局（带 DM_WATCH 等测试环境变量）被强杀：下次启动只记「测试局，不计」，连续次数不加、实验项不动
    // 11. A test run (started with test environment variables such as DM_WATCH) killed: the next start only logs "test run, not counted", the streak does not grow, experimental features untouched
    dm_crashfence_clean("优雅退出"); write_settings("eco_fps=2\nrt_shadow=1\nupscale=1\nq3d=0\nq3d_v2=1\n");
    restart(); CHECK(dm_crashfence_start() == 0);                                     // 正常局 | a real run
    restart(); CHECK(dm_crashfence_start() == 1);                                     // 它闪退：第 1 次 | it crashed: number 1
    setenv("DM_WATCH", "150", 1); restart(); CHECK(dm_crashfence_start() == 2);   // 测试局开始时看到真闪退第 2 次 → 关实验项（真闪退照算） | the test run sees the 2nd real crash → features off (real crashes still count)
    CHECK(g_test == 1); { FILE *f = fopen(marker, "r"); char m[64] = {0}; fread(m, 1, sizeof m - 1, f); fclose(f); CHECK(strstr(m, "test=1")); }
    unsetenv("DM_WATCH"); write_settings("eco_fps=2\nrt_shadow=1\nupscale=1\nq3d=0\nq3d_v2=1\n");
    restart(); CHECK(dm_crashfence_start() == 2);                                     // 测试局被强杀：不加，仍是 2 | the test run was killed: no increment, still 2
    dm_crashfence_clean("优雅退出");
    restart(); CHECK(dm_crashfence_start() == 0); dm_crashfence_clean("优雅退出"); write_settings("eco_fps=2\nrt_shadow=1\nupscale=1\nq3d=0\nq3d_v2=1\n");
    setenv("DEVICECTL_CHILD_DM_TEST_FPS", "120", 1); setenv("DM_TEST_FPS", "120", 1); restart(); dm_crashfence_start(); CHECK(g_test == 1);   // devicectl 传的测试开关 | test switches passed by devicectl
    unsetenv("DM_TEST_FPS"); unsetenv("DEVICECTL_CHILD_DM_TEST_FPS");
    restart(); CHECK(dm_crashfence_start() == 0); CHECK(dm_custom_get("rt_shadow") == 1); CHECK(dm_custom_get("eco_fps") == FPS_120);   // 测试局被杀不计：实验项还开着 | the killed test run does not count: features still on
    // 12. 自动关之前的原值备份进设置文件；图形页打开时预选原值，应用后恢复并清掉备份；放弃则保持关、备份留着
    // 12. The original values are backed up in the settings file; opening the Graphics tab pre-selects them, Apply restores them and clears the backup; Discard keeps them off and keeps the backup
    dm_crashfence_clean("优雅退出"); write_settings("eco_fps=2\nrt_shadow=1\nupscale=1\nq3d=0\nq3d_v2=1\n");
    setenv("DM_TEST_CRASH_MARK", "2", 1); restart(); dm_crashfence_start(); unsetenv("DM_TEST_CRASH_MARK");
    read_settings(text, sizeof text); CHECK(strstr(text, "exp_off_backup=eco_fps:2,rt_shadow:1,upscale:1\n"));
    atomic_store(&g_loaded, 0); CHECK(dm_custom_get("rt_shadow") == 0);   // 重读：值是关、备份读回 | re-read: values off, backup read back
    CHECK(exp_restore_pending() == 3); CHECK(atomic_load(&g_pend[K_RT]) == 1); CHECK(atomic_load(&g_pend[K_FPS]) == FPS_120); CHECK(atomic_load(&g_pend[K_UPSCALE]) == 1);
    dm_custom_on_uimsg("OptionsPanelMessage", "CancelChanges", NULL);
    CHECK(dm_custom_get("rt_shadow") == 0); read_settings(text, sizeof text); CHECK(strstr(text, "exp_off_backup="));   // 放弃：还关着、备份还在 | Discard: still off, backup kept
    CHECK(exp_restore_pending() == 3); dm_custom_on_uimsg("OptionsPanelMessage", "AcceptChanges", NULL);
    CHECK(dm_custom_get("rt_shadow") == 1); CHECK(dm_custom_get("eco_fps") == FPS_120); CHECK(dm_custom_get("upscale") == 1);
    read_settings(text, sizeof text); CHECK(!strstr(text, "exp_off_backup")); CHECK(strstr(text, "rt_shadow=1\n"));
    CHECK(exp_restore_pending() == 0);   // 备份用完 | the backup is used up
    // 13. 只有一部分开着：只备份开着的那几项 | 13. Only some were on: only those are backed up
    write_settings("eco_fps=0\nrt_shadow=1\nupscale=0\nq3d=0\nq3d_v2=1\n"); atomic_store(&g_loaded, 0);
    CHECK(dm_custom_safe_mode(what, sizeof what) == 1); read_settings(text, sizeof text); CHECK(strstr(text, "exp_off_backup=rt_shadow:1\n"));
    // 10. 没有设置目录：不写标记、不崩 | 10. No settings directory: no marker, no crash
    g_dir[0] = 0; restart(); CHECK(dm_crashfence_start() == 0); CHECK(g_marked == 0);
    printf("t_crashfence: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
