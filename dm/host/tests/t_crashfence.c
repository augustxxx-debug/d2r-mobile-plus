// 崩溃围栏（发布前闸 1）：运行标记写 / 清、连续异常退出计数、第三次启动（连续两次之后）实验项全关并写回设置文件、正常结束后清零、iOS 进后台 / 回前台、测试开关 DM_TEST_CRASH_MARK、
// 主菜单只提示一次。只用临时目录，不碰真存档。「重启进程」= 把两个模块的静态状态清回初值，再调 dm_crashfence_start
// Crash fence (release gate 1): running marker written / cleared, consecutive abnormal-exit count, on the third start (after two in a row) every experimental feature is off and saved to the settings file, the count resets after a normal end, iOS background / foreground, the test switch DM_TEST_CRASH_MARK,
// the main menu notice appears only once. Temporary directories only, never real saves. "Restart the process" = reset the static state of both modules to its initial values, then call dm_crashfence_start
#define DM_DISPLAY_HZ_TEST_HOOK
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
void (*g_crash_note_hook)(int sig);   // crash.c 里的定义（这里不链 crash.c） | defined in crash.c (not linked here)
static uint64_t t_present, t_now = 100000000000ull;
uint64_t crash_last_present_ns(void) { return t_present; }
uint64_t crash_now_ns(void) { return t_now; }
static int checks, fails;
#define CHECK(x) do { checks++; if (!(x)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)
static char dir[64], marker[128], settings[128];
static void restart(void) { g_started = g_marked = g_streak = g_test = g_cause = g_cause_arg = 0; g_msg = CF_OTHER; g_hist[0] = 0; g_crash_note_hook = NULL; atomic_store(&g_notice, 0); atomic_store(&g_show, 0); atomic_store(&g_loaded, 0); }
static int marker_streak(void) { FILE *f = fopen(marker, "r"); if (!f) return -1; int k = -2; if (fscanf(f, "streak=%d", &k) != 1) k = -2; fclose(f); return k; }
static void read_settings(char *out, size_t n) { FILE *f = fopen(settings, "r"); size_t r = f ? fread(out, 1, n - 1, f) : 0; out[r] = 0; if (f) fclose(f); }
// 假控件（名字 +8、子控件数组 +0x58、个数 +0x60、虚表 +0x48 = 命中判断）：命中 = x 恰好是 7 | fake widget (name +8, children +0x58, count +0x60, vtable +0x48 = hit test): hit = x is exactly 7
typedef struct { uint64_t vt; const char *name; uint8_t pad[0x48]; uint64_t *kids; uint64_t nkids; } t_widget;
static int t_hit(uint64_t w, uint64_t xy) { (void)w; return (uint32_t)xy == 7; }
static uint64_t t_vt[10];
static void t_gate(uint64_t bt, int32_t lx, uint64_t *o_bt, int32_t *o_lx) { int32_t ly = 0, rx = 0, ry = 0; dm_crashfence_input_gate(&bt, &lx, &ly, &rx, &ry); *o_bt = bt; *o_lx = lx; CHECK(!ly && !rx && !ry); }
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
    CHECK(dm_custom_get("eco_fps") == FPS_120); CHECK(dm_custom_get("rt_level") == 2); CHECK(dm_custom_get("upscale") == 1); CHECK(dm_crashfence_notice_take() == 0);
    // 3. 又闪退：第三次启动看到连续 2 次 → 实验项全关、写回设置文件、主菜单提示一次 | 3. Crashed again: the third start sees 2 in a row → experimental features off, saved, one main-menu notice
    restart(); CHECK(dm_crashfence_start() == 2); CHECK(marker_streak() == 2);
    CHECK(dm_custom_get("eco_fps") == 0); CHECK(dm_custom_get("rt_level") == 0); CHECK(dm_custom_get("upscale") == 0);
    read_settings(text, sizeof text); CHECK(strstr(text, "eco_fps=0\n")); CHECK(strstr(text, "rt_shadow=0\n")); CHECK(strstr(text, "upscale=0\n")); CHECK(strstr(text, "auto_mana=1\n"));
    atomic_store(&g_loaded, 0); CHECK(dm_custom_get("rt_level") == 0);   // 重新读盘也是关 | off after re-reading from disk too
    dm_crashfence_main_menu(); CHECK(atomic_load(&g_show) == 1);   // 挂上游戏内提示框 | the in-game notice box is queued
    atomic_store(&g_show, 0); dm_crashfence_main_menu(); CHECK(atomic_load(&g_show) == 0);   // 只提示一次 | only once
    atomic_store(&g_show, 2); dm_crashfence_on_uimsg("PanelManager", "ClosePanel", "TouchSafeOk"); CHECK(atomic_load(&g_ack) == 1);   // 「确定」消息 | the OK message
    atomic_store(&g_ack, 0); atomic_store(&g_show, 0); dm_crashfence_on_uimsg("PanelManager", "ClosePanel", "TouchSafeOk"); CHECK(atomic_load(&g_ack) == 0);   // 没在显示时不认 | ignored when not shown
    // 4. 这一局正常结束：标记清掉；下次启动清零 | 4. This run ends normally: marker cleared; the next start is back to zero
    dm_crashfence_clean("优雅退出"); CHECK(access(marker, F_OK) != 0);
    restart(); CHECK(dm_crashfence_start() == 0); CHECK(marker_streak() == 0);
    // 5. 正常启动后玩家又开了实验项，再闪退一次：只是第 1 次，不关 | 5. After a normal start the player turns the features back on and it crashes once: only number 1, nothing turned off
    write_settings("eco_fps=2\nrt_shadow=1\nupscale=1\nq3d=0\nq3d_v2=1\n");
    restart(); CHECK(dm_crashfence_start() == 1); CHECK(dm_custom_get("rt_level") == 2); CHECK(dm_custom_get("eco_fps") == FPS_120);
    // 6. iOS 进后台清标记、连续次数清零；回前台重写（之后在前台闪退算第 1 次） | 6. iOS background clears the marker and the streak; foreground writes it again (a later foreground crash is number 1)
    dm_crashfence_clean("进后台"); CHECK(access(marker, F_OK) != 0);
    dm_crashfence_resume(); CHECK(marker_streak() == 0);
    restart(); CHECK(dm_crashfence_start() == 1);
    // 7. 测试开关 DM_TEST_CRASH_MARK=2：不管盘上的标记，按连续 2 次处理 | 7. Test switch DM_TEST_CRASH_MARK=2: regardless of the marker on disk, treated as 2 in a row
    dm_crashfence_clean("优雅退出"); write_settings("eco_fps=2\nrt_shadow=1\nupscale=0\nq3d=0\nq3d_v2=1\n");
    setenv("DM_TEST_CRASH_MARK", "2", 1); restart();
    CHECK(dm_crashfence_start() == 2); CHECK(dm_custom_get("eco_fps") == 0); CHECK(dm_custom_get("rt_level") == 0); CHECK(dm_crashfence_notice_take() == 1);
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
    restart(); CHECK(dm_crashfence_start() == 0); CHECK(dm_custom_get("rt_level") == 2); CHECK(dm_custom_get("eco_fps") == FPS_120);   // 测试局被杀不计：实验项还开着 | the killed test run does not count: features still on
    // 12. 自动关之前的原值备份进设置文件；图形页打开时预选原值，应用后恢复并清掉备份；放弃则保持关、备份留着
    // 12. The original values are backed up in the settings file; opening the Graphics tab pre-selects them, Apply restores them and clears the backup; Discard keeps them off and keeps the backup
    dm_crashfence_clean("优雅退出"); write_settings("eco_fps=2\nrt_shadow=1\nupscale=1\nq3d=0\nq3d_v2=1\n");
    setenv("DM_TEST_CRASH_MARK", "2", 1); restart(); dm_crashfence_start(); unsetenv("DM_TEST_CRASH_MARK");
    read_settings(text, sizeof text); CHECK(strstr(text, "exp_off_backup=eco_fps:2,rt_level:2,upscale:1\n"));
    atomic_store(&g_loaded, 0); CHECK(dm_custom_get("rt_level") == 0);   // 重读：值是关、备份读回 | re-read: values off, backup read back
    CHECK(exp_restore_pending() == 3); CHECK(atomic_load(&g_pend[K_RTLVL]) == 2); CHECK(atomic_load(&g_pend[K_FPS]) == FPS_120); CHECK(atomic_load(&g_pend[K_UPSCALE]) == 1);
    dm_custom_on_uimsg("OptionsPanelMessage", "CancelChanges", NULL);
    CHECK(dm_custom_get("rt_level") == 0); read_settings(text, sizeof text); CHECK(strstr(text, "exp_off_backup="));   // 放弃：还关着、备份还在 | Discard: still off, backup kept
    CHECK(exp_restore_pending() == 3); dm_custom_on_uimsg("OptionsPanelMessage", "AcceptChanges", NULL);
    CHECK(dm_custom_get("rt_level") == 2); CHECK(dm_custom_get("eco_fps") == FPS_120); CHECK(dm_custom_get("upscale") == 1);
    read_settings(text, sizeof text); CHECK(!strstr(text, "exp_off_backup")); CHECK(strstr(text, "rt_shadow=1\n"));
    CHECK(exp_restore_pending() == 0);   // 备份用完 | the backup is used up
    // 12b. 第 4 项 AI 帧生成（fgen_level）：同一套规则——自动关前记原档位、已有备份不覆盖、图形页打开时预选、应用清备份、放弃保留；备份行四项顺序固定；旧名 fgen:1 / rt_shadow:1 照迁
    // 12b. The 4th entry, AI frame generation (fgen_level): the same rules - the level recorded before the automatic off, an existing backup kept, pre-selected when the Graphics tab opens, cleared on Apply, kept on Discard; four entries in a fixed order; old names fgen:1 / rt_shadow:1 still migrate
    dm_crashfence_clean("优雅退出"); write_settings("eco_fps=2\nrt_shadow=1\nupscale=1\nfgen_level=2\nq3d=0\nq3d_v2=1\n");
    setenv("DM_TEST_CRASH_MARK", "2", 1); restart(); dm_crashfence_start(); unsetenv("DM_TEST_CRASH_MARK");
    CHECK(dm_custom_get("fgen_level") == 0); read_settings(text, sizeof text); CHECK(strstr(text, "exp_off_backup=eco_fps:2,rt_level:2,upscale:1,fgen_level:2\n")); CHECK(strstr(text, "fgen_level=0\n"));
    setenv("DM_TEST_CRASH_MARK", "2", 1); restart(); dm_crashfence_start(); unsetenv("DM_TEST_CRASH_MARK");   // 第二轮自动关：值已是关，备份不被覆盖成 0 | a second automatic pass: values already off, the backup is not overwritten with 0
    read_settings(text, sizeof text); CHECK(strstr(text, "exp_off_backup=eco_fps:2,rt_level:2,upscale:1,fgen_level:2\n"));
    atomic_store(&g_loaded, 0); load_once(); CHECK(exp_restore_pending() == 4); CHECK(atomic_load(&g_pend[K_FGEN]) == 2);
    dm_custom_on_uimsg("OptionsPanelMessage", "CancelChanges", NULL); CHECK(dm_custom_get("fgen_level") == 0); read_settings(text, sizeof text); CHECK(strstr(text, "fgen_level:2"));   // 放弃：还关着、备份还在 | Discard: still off, backup kept
    CHECK(exp_restore_pending() == 4); dm_custom_on_uimsg("OptionsPanelMessage", "AcceptChanges", NULL);
    CHECK(dm_custom_get("fgen_level") == 2 && dm_custom_get("rt_level") == 2); read_settings(text, sizeof text); CHECK(!strstr(text, "exp_off_backup")); CHECK(strstr(text, "fgen_level=2\n"));
    write_settings("fgen_level=0\nq3d_v2=1\nexp_off_backup=rt_shadow:1,fgen:1\n"); atomic_store(&g_loaded, 0); load_once();   // 旧包的两个旧名 | the two old names from older packages
    CHECK(exp_restore_pending() == 2 && atomic_load(&g_pend[K_RTLVL]) == 2 && atomic_load(&g_pend[K_FGEN]) == 2); dm_custom_on_uimsg("OptionsPanelMessage", "CancelChanges", NULL);
    write_settings("fgen_level=1\nupscale=0\nq3d_v2=1\n"); atomic_store(&g_loaded, 0); CHECK(dm_custom_safe_mode(what, sizeof what) == 1 && strstr(what, "AI 帧生成"));   // 省电档 1 也算开着 | the reserved level 1 counts as on too
    read_settings(text, sizeof text); CHECK(strstr(text, "exp_off_backup=fgen_level:1\n"));
    // 13. 只有一部分开着：只备份开着的那几项 | 13. Only some were on: only those are backed up
    write_settings("eco_fps=0\nrt_shadow=1\nupscale=0\nq3d=0\nq3d_v2=1\n"); atomic_store(&g_loaded, 0);
    CHECK(dm_custom_safe_mode(what, sizeof what) == 1); read_settings(text, sizeof text); CHECK(strstr(text, "exp_off_backup=rt_level:2\n"));
    // 14. 分场景（用户：「哪里闪退了，应该分场景」）：闪退两次 → 闪退文案；卡死（进后台时 5 秒没出图）两次 → 卡死文案；内存告警后没了两次 → 内存不足文案；原因不同 → 异常退出文案
    // 14. By scenario (user: "where did it crash, it should be split by scenario"): two crashes → crash text; two freezes (no present for 5 s when going to the background) → frozen text; two vanishings after memory warnings → memory text; different causes → generic text
    write_settings("eco_fps=2\nrt_shadow=1\nupscale=1\nq3d=0\nq3d_v2=1\n");
    unlink(marker); restart(); dm_crashfence_start(); CHECK(g_crash_note_hook == cf_on_signal);
    g_crash_note_hook(11);                                                  // 致命信号 → 标记 cause=1:11 | fatal signal → marker cause=1:11
    { FILE *f = fopen(marker, "r"); char m[128] = {0}; fread(m, 1, sizeof m - 1, f); fclose(f); CHECK(strstr(m, "cause=1:11\n")); CHECK(strstr(m, "streak=0\n")); }
    restart(); CHECK(dm_crashfence_start() == 1); CHECK(!strcmp(g_hist, "1"));
    g_crash_note_hook(6); restart(); CHECK(dm_crashfence_start() == 2); CHECK(!strcmp(g_hist, "11")); CHECK(g_msg == CF_CRASH); CHECK(!strcmp(k_cf_msg[g_msg], "dmSafeModeMsgCrash"));
    dm_crashfence_clean("优雅退出"); restart(); dm_crashfence_start(); CHECK(g_hist[0] == 0);
    t_present = t_now - 1000000000ull; dm_crashfence_background(); CHECK(access(marker, F_OK) != 0);   // 画面还在动：进后台清标记 | picture still moving: going to the background clears
    dm_crashfence_resume(); t_present = t_now - 7000000000ull; dm_crashfence_background(); CHECK(access(marker, F_OK) == 0);   // 7 秒没出图：标记留着、记卡死 | 7 s without a present: marker kept, frozen recorded
    { FILE *f = fopen(marker, "r"); char m[128] = {0}; fread(m, 1, sizeof m - 1, f); fclose(f); CHECK(strstr(m, "cause=2:7\n")); }
    dm_crashfence_resume(); { FILE *f = fopen(marker, "r"); char m[128] = {0}; fread(m, 1, sizeof m - 1, f); fclose(f); CHECK(strstr(m, "cause=0:0\n")); }   // 回前台撤销 | back in the foreground: revoked
    dm_crashfence_background();                                             // 又卡住进后台、被划掉 | stuck again, switched away and swiped off
    restart(); CHECK(dm_crashfence_start() == 1); CHECK(!strcmp(g_hist, "2"));
    dm_crashfence_note_memory(); restart(); CHECK(dm_crashfence_start() == 2); CHECK(!strcmp(g_hist, "23")); CHECK(g_msg == CF_OTHER);   // 卡死 + 内存不足：原因不同 → 异常退出文案 | frozen + memory: different → generic text
    dm_crashfence_note_memory(); restart(); dm_crashfence_start(); CHECK(!strcmp(g_hist, "233")); CHECK(g_msg == CF_MEM);   // 最近两次都是内存不足 | the last two are memory
    dm_crashfence_note_memory(); g_crash_note_hook(11); restart(); dm_crashfence_start(); CHECK(g_hist[strlen(g_hist) - 1] == '1');   // 告警之后又闪退：按闪退记 | a crash after the warning counts as a crash
    dm_crashfence_clean("优雅退出");
    setenv("DM_TEST_CRASH_MARK", "2", 1); setenv("DM_TEST_CRASH_CAUSE", "hang", 1); restart(); dm_crashfence_start(); CHECK(g_msg == CF_HANG); CHECK(!strcmp(g_hist, "22"));
    unsetenv("DM_TEST_CRASH_MARK"); unsetenv("DM_TEST_CRASH_CAUSE"); dm_crashfence_clean("优雅退出");
    // 15. 测试局 + 残留连续 ≥ 2：不关、不写回、不提示，连续次数不变；DM_TEST_CRASH_MARK 仍能触发；玩家局照旧关 | 15. Test run + leftover streak >= 2: no shutoff, no write-back, no notice, streak unchanged; DM_TEST_CRASH_MARK still triggers it; a player run still shuts off
    { const char *on = "eco_fps=2\nrt_shadow=1\nupscale=1\nq3d=0\nq3d_v2=1\n", *left = "streak=2\ntest=1\nhist=00\ncause=0:0\n"; FILE *f;
      write_settings(on); atomic_store(&g_loaded, 0); f = fopen(marker, "w"); fputs(left, f); fclose(f);
      setenv("DM_WATCH", "90", 1); restart(); CHECK(dm_crashfence_start() == 2); CHECK(marker_streak() == 2);
      CHECK(dm_custom_get("eco_fps") == FPS_120); CHECK(dm_custom_get("rt_level") == 2); CHECK(dm_custom_get("upscale") == 1); CHECK(dm_crashfence_notice_take() == 0);
      read_settings(text, sizeof text); CHECK(!strcmp(text, on));
      dm_crashfence_clean("优雅退出"); CHECK(marker_streak() == 2); CHECK(g_streak == 2);   // 测试局正常结束：残留不清（N10-01） | a test run ending normally keeps the leftover (N10-01)
      restart(); CHECK(dm_crashfence_start() == 2); CHECK(marker_streak() == 2);              // 再来一局测试局：仍是 2，不加 | another test run: still 2, no increment
      f = fopen(marker, "w"); fputs("streak=1\nhist=0\ncause=0:0\n", f); fclose(f);   // 残留来自玩家局：那次异常退出照算，但不关 | leftover from a player run: that exit still counts, but no shutoff
      restart(); CHECK(dm_crashfence_start() == 2); CHECK(dm_custom_get("upscale") == 1); CHECK(dm_crashfence_notice_take() == 0);
      setenv("DM_TEST_CRASH_MARK", "2", 1); restart(); CHECK(dm_crashfence_start() == 2); CHECK(dm_custom_get("upscale") == 0); CHECK(dm_crashfence_notice_take() == 1);   // 测试开关还能验围栏本身 | the test switch can still exercise the fence itself
      dm_crashfence_clean("优雅退出"); CHECK(marker_streak() < 0);   // 测试开关的局照旧清 | a test-switch run clears as before
      // DM_TEST_CRASH_MARK=1 的局被杀，盘上留 streak=1、test=1：之后的局不把它当玩家局残留再加 1（rc2-perf，17 Pro 的现场） | a killed DM_TEST_CRASH_MARK=1 run leaves streak=1, test=1: later runs do not count it again as a player leftover
      setenv("DM_TEST_CRASH_MARK", "1", 1); restart(); CHECK(dm_crashfence_start() == 1); CHECK(marker_streak() == 1);
      restart(); CHECK(dm_crashfence_start() == 1); CHECK(marker_streak() == 1);   // 又一局：开关只模拟，不累加 | another: the switch only simulates, no growth
      unsetenv("DM_TEST_CRASH_MARK"); restart(); CHECK(dm_crashfence_start() == 1); dm_crashfence_clean("优雅退出"); CHECK(marker_streak() == 1);   // 只带 DM_WATCH 的测试局：不加、正常结束不清 | DM_WATCH only: no growth, kept on a normal end
      restart(); CHECK(dm_crashfence_start() == 1); CHECK(marker_streak() == 1); setenv("DM_TEST_CRASH_MARK", "1", 1);
      restart(); dm_crashfence_start(); dm_crashfence_clean("优雅退出"); CHECK(marker_streak() < 0);   // 开关局正常结束清掉 | a switch run ending normally clears it
      unsetenv("DM_TEST_CRASH_MARK"); unsetenv("DM_WATCH");
      write_settings(on); atomic_store(&g_loaded, 0); f = fopen(marker, "w"); fputs(left, f); fclose(f);
      restart(); CHECK(dm_crashfence_start() == 2); CHECK(dm_custom_get("upscale") == 0); CHECK(dm_crashfence_notice_take() == 1);   // 玩家局：同样的残留照旧关 | player run: the same leftover shuts off as before
      read_settings(text, sizeof text); CHECK(strstr(text, "upscale=0\n")); dm_crashfence_clean("优雅退出"); }
    // 16. 提示框开着输入全归它，关框后恢复（B083）| 16. The notice box takes all input while up, input returns after it closes (B083)
    { uint64_t o; int32_t ox; atomic_store(&g_ack, 0);
      t_gate(DM_BTN_B, 20000, &o, &ox); CHECK(o == DM_BTN_B); CHECK(ox == 20000);                   // 没开：原样放过 | not up: passes through
      t_gate(0, 0, &o, &ox); atomic_store(&g_modal, 1); CHECK(dm_crashfence_modal());
      atomic_store(&g_pad, 0); t_gate(DM_BTN_A | DM_BTN_DOWN, 30000, &o, &ox); CHECK(o == 0); CHECK(ox == 0); CHECK(!atomic_load(&g_ack));   // 触屏模式：全吞，A 不当确定 | touch mode: all swallowed, A is not OK
      t_gate(0, 0, &o, &ox); atomic_store(&g_pad, 1); t_gate(DM_BTN_B | DM_BTN_PLUS, -30000, &o, &ox); CHECK(o == 0); CHECK(ox == 0); CHECK(!atomic_load(&g_ack));   // 手柄模式：别的键也吞 | controller mode: other keys swallowed too
      t_gate(DM_BTN_A, 0, &o, &ox); CHECK(o == 0); CHECK(atomic_exchange(&g_ack, 0) == 1);          // A 按下沿 = 确定，游戏收不到 | A's edge = OK, the game does not get it
      t_gate(DM_BTN_A, 0, &o, &ox); CHECK(!atomic_load(&g_ack));                                    // 按住不重复 | holding does not repeat
      atomic_store(&g_modal, 0); CHECK(!dm_crashfence_modal());
      t_gate(DM_BTN_A, 0, &o, &ox); CHECK(o == 0);                                                  // 关框时还按着：继续压住 | still held at close: withheld
      t_gate(DM_BTN_A | DM_BTN_B, 0, &o, &ox); CHECK(o == 0 || o == DM_BTN_B);
      t_gate(0, 0, &o, &ox); t_gate(DM_BTN_B, 0, &o, &ox); CHECK(o == DM_BTN_B);                    // 松开后恢复 | restored after release
      atomic_store(&g_modal, 1); t_gate(0, 30000, &o, &ox); CHECK(ox == 0); atomic_store(&g_modal, 0);
      t_gate(0, 30000, &o, &ox); CHECK(ox == 0); t_gate(0, 0, &o, &ox); t_gate(0, 30000, &o, &ox); CHECK(ox == 30000);   // 摇杆同样压到回中 | a stick is withheld until centred too
      t_gate(0, 0, &o, &ox);
      // 点按：只认「确定」，别处吞掉；按当前模式找面板 | taps: OK only, anything else swallowed; the panel follows the current mode
      t_vt[9] = (uint64_t)t_hit; uint64_t okp = 0, mmp = 0;
      t_widget ok = { (uint64_t)t_vt, "TouchSafeOk", {0}, NULL, 0 }; okp = (uint64_t)&ok;
      t_widget mm = { (uint64_t)t_vt, "MainMenuPanel", {0}, &okp, 1 }; mmp = (uint64_t)&mm;
      t_widget root = { (uint64_t)t_vt, "root", {0}, &mmp, 1 };
      atomic_store(&g_pad, 0); dm_crashfence_ui_tap((uint64_t)&root, 3, 3); CHECK(!atomic_load(&g_ack));
      dm_crashfence_ui_tap((uint64_t)&root, 7, 3); CHECK(atomic_exchange(&g_ack, 0) == 1);
      atomic_store(&g_pad, 1); dm_crashfence_ui_tap((uint64_t)&root, 7, 3); CHECK(!atomic_load(&g_ack));      // 手柄版面板不在树里：不确认 | the controller panel is not in the tree: no OK
      mm.name = "MainMenuButtonRibbon"; dm_crashfence_ui_tap((uint64_t)&root, 7, 3); CHECK(atomic_exchange(&g_ack, 0) == 1);
      dm_crashfence_ui_tap(0, 7, 3); CHECK(!atomic_load(&g_ack)); atomic_store(&g_pad, 0); }
    // 10. 没有设置目录：不写标记、不崩 | 10. No settings directory: no marker, no crash
    g_dir[0] = 0; restart(); CHECK(dm_crashfence_start() == 0); CHECK(g_marked == 0);
    printf("t_crashfence: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
