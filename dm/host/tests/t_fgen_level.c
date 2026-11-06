// 帧生成档位 fgen_level（RC2 第一片）：新键读盘与极端值、旧键 fgen 迁移、DM_FGEN_PRESET 与 DM_TEST_FG 的优先级、
// 崩溃围栏备份（旧名 fgen:1 按 2 恢复）、安卓壳读到非 0 按关且不改写文件、分享摘要里的设置值与生效状态。只用临时设置文件。
// Frame-generation level fgen_level (first RC2 slice): parsing the new key and extreme values, migration of the old fgen key,
// DM_FGEN_PRESET vs DM_TEST_FG priority, the crash-fence backup (old name fgen:1 restores as 2), the Android shell reading a
// non-zero level as off without rewriting the file, and the setting value plus the effective state in the share summary. Temporary settings only.
#define DM_DISPLAY_HZ_TEST_HOOK
#define DM_FG_TEST_HOOK
#define DM_THERMAL_TEST_HOOK
#define DM_FG_PLATFORM_TEST_HOOK
#include "../dm_custom.c"
#include "../dm_i18n_tab.h"
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
static int quality;
void dm_3d_quality_mode_set(int value) { quality = value; }
int dm_lang_group(void) { return DM_LG_EN; }
const char *dm_save_commitdir(void) { return NULL; }
int dm_3d_quality_mode_get(void) { return quality; }
float dm_3d_scale_cur(void) { return .75f; }
static int display_hz = 120;   // 屏幕最高刷新率替身（120 = 撑得住平滑档）| stand-in for the screen's maximum refresh rate (120 = smooth level fits)
int dm_display_max_hz(void) { return display_hz; }
static int fg_supported = 1;   // 插值器支持替身 | stand-in for interpolator support
int mtl_fg_supported(void) { return fg_supported; }
static int thermal_stub = -1;   // 温控档位替身（-1 = 没读到，不算热）| stand-in for the thermal state (-1 = unread, not hot)
int dm_thermal_now(void) { return thermal_stub; }
int mtl_fg_backoff(void) { return 0; }
static int platform_apple = 1;   // 1 = Apple 壳 / 0 = 安卓壳 | 1 = Apple shell / 0 = Android shell
int dm_fg_platform_test(void) { return platform_apple; }
static int checks, fails;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); fails++; } } while (0)
static char directory[512], path[1024];
static void write_settings(const char *body) { FILE *f = fopen(path, "w"); if (!f) { fprintf(stderr, "FAIL line %d: 写不进 %s\n", __LINE__, path); fails++; return; } fputs("q3d=0\nq3d_v2=1\n", f); fputs(body, f); fclose(f); atomic_store(&g_loaded, 0); }
static void read_settings(char *out, size_t n) { out[0] = 0; FILE *f = fopen(path, "r"); if (!f) return; size_t got = fread(out, 1, n - 1, f); out[got] = 0; fclose(f); }   // 读到多少都补 NUL：短文件不能把上次留下的尾巴留给 strstr | always NUL-terminate: a short file must not leave the previous tail visible to strstr
// 同一进程里 DM_TEST_FG / DM_FGEN_PRESET 只读一次环境（静态缓存），换一套环境变量就另起一个全新进程（重新 exec 自己：静态量、读盘都是第一次）：
// 环境变量 DM_FGEN_CHILD_* 交给子进程，子进程按 DM_FGEN_CHILD_LEVEL 写一份设置文件再问 dm_fg_setting()，退出码 = 生效值
// Within one process DM_TEST_FG / DM_FGEN_PRESET are read from the environment once (static cache), so another set of variables needs a brand-new process
// (re-exec itself: every static and the settings load are a first call). The DM_FGEN_CHILD_* variables go to the child, which writes a settings file with
// DM_FGEN_CHILD_LEVEL and then asks dm_fg_setting(); the exit code is the effective value
static char self_path[4096];
static void child_mode(void) {
    if (!getenv("DM_FGEN_CHILD")) return;
    const char *d = getenv("DM_FGEN_CHILD_DIR"), *p = getenv("DM_FGEN_CHILD_PRESET"), *t = getenv("DM_FGEN_CHILD_TEST"), *l = getenv("DM_FGEN_CHILD_LEVEL");
    snprintf(directory, sizeof directory, "%s", d ? d : ""); snprintf(path, sizeof path, "%s/sw_custom.txt", directory); dm_custom_init(directory);
    { FILE *f = fopen(path, "w"); if (!f) _exit(3); fputs("q3d=0\nq3d_v2=1\n", f); fprintf(f, "fgen_level=%d\n", l ? atoi(l) : 0); fclose(f); }
    if (p && *p) setenv("DM_FGEN_PRESET", p, 1);
    if (t && *t) setenv("DM_TEST_FG", t, 1);
    _exit(dm_fg_setting() ? 1 : 0); }
static int child_fg(const char *preset, const char *test_spec, int level) {
    pid_t pid = fork(); if (pid < 0) return -1;
    if (!pid) { char l[8];
        setenv("DM_FGEN_CHILD", "1", 1); setenv("DM_FGEN_CHILD_DIR", directory, 1);
        setenv("DM_FGEN_CHILD_PRESET", preset ? preset : "", 1);
        setenv("DM_FGEN_CHILD_TEST", test_spec ? test_spec : "", 1);
        snprintf(l, sizeof l, "%d", level); setenv("DM_FGEN_CHILD_LEVEL", l, 1);
        execl(self_path, self_path, (char *)NULL); _exit(3); }
    int st = 0; if (waitpid(pid, &st, 0) != pid) return -1;
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1; }
// 抓一段 stderr（日志行）：f 里跑一次，返回抓到的文本 | Capture stderr (log lines): run f once and return the captured text
static void capture(void (*run)(void), char *out, size_t n) {
    char logp[1100]; snprintf(logp, sizeof logp, "%s/log.txt", directory);
    out[0] = 0; int saved = dup(2); if (saved < 0) return; FILE *lf = freopen(logp, "w", stderr); if (!lf) { dup2(saved, 2); close(saved); return; }
    run(); fflush(stderr); dup2(saved, 2); close(saved);
    FILE *f = fopen(logp, "r"); if (!f) return; size_t got = fread(out, 1, n - 1, f); out[got] = 0; fclose(f); unlink(logp);
}
static int run_buf_len;
static void run_wrapper(void) { run_buf_len = dm_fg_setting(); }
static void run_short_log(void) { LOG("短日志"); }
int main(int argc, char **argv) {
    (void)argc; child_mode();   // 子进程模式（重新 exec 自己）：写完设置就问一次并退出 | child mode (re-exec of itself): write the settings, ask once, exit
    if (!realpath(argv[0], self_path)) snprintf(self_path, sizeof self_path, "%s", argv[0]);
    snprintf(directory, sizeof directory, "/tmp/d2r-fgen-level-XXXXXX"); if (!mkdtemp(directory)) return 1;
    snprintf(path, sizeof path, "%s/sw_custom.txt", directory); dm_custom_init(directory);
    char text[8192];
    // 1. 键名与缺省：新键叫 fgen_level、缺省 0（关）
    // 1. key name and default: the new key is fgen_level and it defaults to 0 (off)
    CHECK(!strcmp(k_keys[K_FGEN], "fgen_level")); CHECK(k_def[K_FGEN] == DM_FGEN_OFF);
    // 插进 fgen_level 之后每个 K_* 宏仍指向它名字的键（前面写死的、后面 NK - n 倒数的都核）| after inserting fgen_level every K_* macro still names its key (both the fixed and the NK - n ones)
    { static const struct { int k; const char *name; } km[] = { { K_VOICE, "voice" }, { K_MERC, "merc_pot" }, { K_ICLOUD, K_ICLOUD_KEY }, { K_FPS, "eco_fps" }, { K_Q3D, "q3d" }, { K_TOUCH_SWAP, "touch_swap_above" },
          { K_FGEN, "fgen_level" }, { K_DYN3D, "dyn3d" }, { K_TELSTATS, "tel_stats" }, { K_AUTOCRASH, "auto_crash" }, { K_BRIGHT, "bright_level" }, { K_FPSHUD, "fps_hud" }, { K_UPSCALE, "upscale" }, { K_RES, "render_res" }, { K_RTLVL, "rt_level" } };
      for (unsigned i = 0; i < sizeof km / sizeof *km; i++) CHECK(km[i].k >= 0 && km[i].k < NK && !strcmp(k_keys[km[i].k], km[i].name));
      CHECK((int)(sizeof k_def / sizeof *k_def) == NK && k_def[key_index("belt_refill")] == 1 && k_def[key_index("free_tp")] == 1 && k_def[K_BRIGHT] == 2 && k_def[K_DYN3D] == 0);
      CHECK(!strcmp(k_bak_name[BAK_FGEN], "fgen_level") && N_BAK == 4 && !strcmp(k_bak_name[1], "rt_level")); }
    write_settings("");
    CHECK(dm_fgen_level() == DM_FGEN_OFF); CHECK(dm_custom_get("fgen_level") == 0); CHECK(!dm_fg_setting());
    CHECK(dm_custom_get("fgen") == 1);   // 旧键名不再当设置读（不认识的键返回 1，别处也不许再用它）| the old key name is no longer a setting (unknown keys return 1; nothing may use it any more)
    // 测试读取自己也要终止：先读长文件再读短文件，同一个缓冲区不能留下上次的尾巴（否则 strstr 假绿/假红）
    // The test reader itself must terminate: after a long file then a short one, the same buffer must not keep the previous tail (otherwise strstr can be falsely green or red)
    { char probe[4096];
      write_settings("fgen_level=2\nstale_marker=1\n"); read_settings(probe, sizeof probe); CHECK(strstr(probe, "stale_marker=1"));
      write_settings("fgen_level=0\n"); read_settings(probe, sizeof probe); CHECK(!strstr(probe, "stale_marker")); }
    // 2. 读盘：0 / 1 / 2 照读；3（保留档）与任何非法值都读作 0
    // 2. parsing: 0 / 1 / 2 as written; 3 (reserved) and any invalid value read as 0
    { static const char *const body[] = { "fgen_level=0\n", "fgen_level=1\n", "fgen_level=2\n", "fgen_level=3\n", "fgen_level=4\n",
          "fgen_level=-1\n", "fgen_level=2x\n", "fgen_level=\n", "fgen_level= 2\n" };
      static const int want[] = { 0, 1, 2, 0, 0, 0, 0, 0, 2 };   // 前导空格与 q3d / render_res 一样被 strtol 跳过（后导空格不算）| leading space is skipped by strtol as for q3d / render_res (trailing space is not)
      for (unsigned i = 0; i < sizeof want / sizeof *want; i++) { write_settings(body[i]); (void)dm_fgen_level(); CHECK(dm_fgen_level() == want[i]); } }
    write_settings("fgen_level=2 \n"); CHECK(dm_fgen_level() == DM_FGEN_OFF);   // 后导空格：整串不是一个数 → 0 | trailing space: the rest is not exactly a number -> 0
    // 3. 旧键迁移：只有旧键时 fgen=1 → 2、fgen=0 → 0；新键存在就不再理旧键（哪怕两个都在、哪怕新键是 0）
    // 3. legacy migration: with only the old key, fgen=1 -> 2 and fgen=0 -> 0; an existing new key wins and the old one is ignored
    write_settings("fgen=1\n"); CHECK(dm_fgen_level() == DM_FGEN_SMOOTH); CHECK(dm_fg_setting());
    write_settings("fgen=0\n"); CHECK(dm_fgen_level() == DM_FGEN_OFF); CHECK(!dm_fg_setting());
    write_settings("fgen_level=0\nfgen=1\n"); CHECK(dm_fgen_level() == DM_FGEN_OFF);   // 新键优先 | the new key wins
    write_settings("fgen_level=1\nfgen=0\n"); CHECK(dm_fgen_level() == DM_FGEN_ECO);
    write_settings("fgen_level=2\nfgen=0\n"); CHECK(dm_fgen_level() == DM_FGEN_SMOOTH);
    // 迁移只发生在内存里：读盘不写文件（旧键原样留着，等下一次存盘才换成新键）| migration happens in memory only: loading writes no file (the old key stays until the next save writes the new one)
    write_settings("fgen=1\n"); (void)dm_fgen_level(); read_settings(text, sizeof text);
    CHECK(strstr(text, "fgen=1\n")); CHECK(!strstr(text, "fgen_level="));
    // 4. 存盘按新键写、旧键随整份重写消失：改别的设置也一样（迁移出来的档位 2 会落盘）；没改动时不动文件
    // 4. A save writes the new key and the old line disappears with the whole-file rewrite: the same when another setting changes (the migrated level 2 is persisted); an Apply with no change leaves the file alone
    write_settings("fgen=1\nupscale=0\n"); (void)dm_fgen_level();
    { int idx = key_index("auto_mana"); atomic_store(&g_pend[idx], 0); }
    dm_custom_on_uimsg("OptionsPanelMessage", "AcceptChanges", NULL);
    read_settings(text, sizeof text); CHECK(strstr(text, "fgen_level=2\n")); CHECK(!strstr(text, "\nfgen="));
    atomic_store(&g_loaded, 0); CHECK(dm_fgen_level() == DM_FGEN_SMOOTH); CHECK(dm_fg_setting());
    write_settings("fgen=1\n"); (void)dm_fgen_level();
    atomic_store(&g_pend[K_FGEN], DM_FGEN_OFF); dm_custom_on_uimsg("OptionsPanelMessage", "AcceptChanges", NULL);
    read_settings(text, sizeof text); CHECK(strstr(text, "fgen_level=0\n")); CHECK(!strstr(text, "\nfgen="));
    atomic_store(&g_loaded, 0); CHECK(dm_fgen_level() == DM_FGEN_OFF); CHECK(!dm_fg_setting());
    // 升级后第一局就连着崩两次：旧键迁移出的档位 2 先被围栏备份再关掉，存盘按新键写、旧行消失
    // Two crashes in a row on the first run after upgrading: the migrated level 2 is backed up, then turned off; the save writes the new key and the old line is gone
    write_settings("fgen=1\nupscale=0\n"); (void)dm_fgen_level();
    { char what[256]; CHECK(dm_custom_safe_mode(what, sizeof what) == 1); }
    CHECK(atomic_load(&g_bak[3]) == DM_FGEN_SMOOTH);
    read_settings(text, sizeof text); CHECK(strstr(text, "fgen_level=0\n")); CHECK(!strstr(text, "\nfgen=")); CHECK(strstr(text, "fgen_level:2"));
    // 5. 图形页的勾选框 = 关 / 平滑两档：勾上存 2、取消存 0 | the Graphics checkbox has two states: checked saves 2, unchecked saves 0
    { int idx = K_FGEN; atomic_store(&g_pend[idx], -1);
      dm_custom_on_uimsg("OptionsPanelMessage", "ResetToDefaults", NULL); CHECK(atomic_load(&g_pend[idx]) == DM_FGEN_OFF);
      dm_custom_on_uimsg("OptionsPanelMessage", "AcceptChanges", NULL); CHECK(dm_fgen_level() == DM_FGEN_OFF);
      atomic_store(&g_pend[idx], DM_FGEN_SMOOTH); dm_custom_on_uimsg("OptionsPanelMessage", "AcceptChanges", NULL); CHECK(dm_fgen_level() == DM_FGEN_SMOOTH); }
    // 6. DM_TEST_FG 优先于 DM_FGEN_PRESET，DM_FGEN_PRESET 优先于设置（纯函数只核优先级；实时读环境见 11）
    // 6. DM_TEST_FG beats DM_FGEN_PRESET and DM_FGEN_PRESET beats the setting (the pure function checks the order; live env in 11)
    CHECK(fgen_choose_level(DM_FGEN_SMOOTH, -1, DM_FGEN_OFF) == DM_FGEN_SMOOTH);
    CHECK(fgen_choose_level(DM_FGEN_OFF, DM_FGEN_SMOOTH, DM_FGEN_SMOOTH) == DM_FGEN_OFF);
    CHECK(fgen_choose_level(-1, DM_FGEN_OFF, DM_FGEN_SMOOTH) == DM_FGEN_OFF);
    CHECK(fgen_choose_level(-1, -1, DM_FGEN_SMOOTH) == DM_FGEN_SMOOTH);
    CHECK(fgen_choose_level(-1, DM_FGEN_SMOOTH, DM_FGEN_OFF) == DM_FGEN_SMOOTH);
    CHECK(fgen_preset_parse("0") == DM_FGEN_OFF); CHECK(fgen_preset_parse("1") == DM_FGEN_ECO); CHECK(fgen_preset_parse("2") == DM_FGEN_SMOOTH);
    CHECK(fgen_preset_parse("3") == DM_FGEN_OFF);   // 保留档按 0 | the reserved level reads as 0
    CHECK(fgen_preset_parse("") == -1); CHECK(fgen_preset_parse("10") == -1); CHECK(fgen_preset_parse("x") == -1); CHECK(fgen_preset_parse("-1") == -1);
    // 7. 崩溃围栏备份：存的是档位（不是开关）；旧名 fgen:1 按 2 恢复 | crash-fence backup: stores the level, and the old name fgen:1 restores as 2
    write_settings("fgen_level=2\nexp_off_backup=fgen_level:2\n"); (void)dm_fgen_level(); CHECK(atomic_load(&g_bak[3]) == DM_FGEN_SMOOTH);
    write_settings("exp_off_backup=fgen:1\n"); (void)dm_fgen_level(); CHECK(atomic_load(&g_bak[3]) == DM_FGEN_SMOOTH);
    write_settings("exp_off_backup=eco_fps:2,fgen:1\n"); (void)dm_fgen_level(); CHECK(atomic_load(&g_bak[3]) == DM_FGEN_SMOOTH);
    write_settings("exp_off_backup=fgen:0\n"); (void)dm_fgen_level(); CHECK(atomic_load(&g_bak[3]) == DM_FGEN_OFF);
    write_settings("exp_off_backup=fgen_level:2\n"); (void)dm_fgen_level();
    exp_restore_pending(); CHECK(pending_value(K_FGEN) == DM_FGEN_SMOOTH);   // 图形页打开时按原档位预选 | pre-selected when the Graphics tab opens
    // 8. 围栏关掉再恢复：备份原档位、写 0；图形页预选回 2，应用后回到 2
    // 8. the fence turns it off and restores later: back up the level, write 0; the page pre-selects 2 again and Apply returns to 2
    write_settings("fgen_level=2\nupscale=0\n"); (void)dm_fgen_level();
    { char what[256]; CHECK(dm_custom_safe_mode(what, sizeof what) == 1); CHECK(strstr(what, "AI 帧生成")); }
    CHECK(atomic_load(&g_val[K_FGEN]) == DM_FGEN_OFF); CHECK(atomic_load(&g_bak[3]) == DM_FGEN_SMOOTH);
    read_settings(text, sizeof text); CHECK(strstr(text, "fgen_level=0\n")); CHECK(strstr(text, "fgen_level:2"));
    exp_restore_pending(); CHECK(pending_value(K_FGEN) == DM_FGEN_SMOOTH);
    dm_custom_on_uimsg("OptionsPanelMessage", "AcceptChanges", NULL); CHECK(dm_fgen_level() == DM_FGEN_SMOOTH); CHECK(atomic_load(&g_bak[3]) == -1);
    // 档位 1（省电，枚举保留）走围栏也照样备份、关掉；预选不恢复成 1（见下）| level 1 (power saving, reserved enum) is backed up and turned off just the same; pre-selection does not bring 1 back (below)
    write_settings("fgen_level=1\nupscale=0\n"); (void)dm_fgen_level();
    { char what[256]; CHECK(dm_custom_safe_mode(what, sizeof what) == 1); }
    // 预选按界面两态归一（审计 P2-3）：备份里的 1 按 0（等于当前的关，不预选），不升成 2；应用清掉备份、档位停在 0 | pre-selection is normalised to the page's two states (audit P2-3): a backed-up 1 reads as 0 (equal to the current off, not pre-selected), never 2; Apply clears the backup and the level stays 0
    CHECK(atomic_load(&g_bak[3]) == DM_FGEN_ECO); CHECK(exp_restore_pending() == 0); CHECK(pending_value(K_FGEN) == DM_FGEN_OFF);
    dm_custom_on_uimsg("OptionsPanelMessage", "AcceptChanges", NULL); CHECK(dm_fgen_level() == DM_FGEN_OFF); CHECK(atomic_load(&g_bak[3]) == -1); CHECK(!dm_fg_setting());
    write_settings("fgen_level=0\nexp_off_backup=eco_fps:2,fgen_level:1\n"); (void)dm_fgen_level();   // 混着别的项：别的照预选，帧生成那项不预选 | mixed with another entry: that one pre-selects, frame generation does not
    CHECK(exp_restore_pending() == 1 && pending_value(K_FPS) == FPS_120 && pending_value(K_FGEN) == DM_FGEN_OFF); dm_custom_on_uimsg("OptionsPanelMessage", "CancelChanges", NULL);
    write_settings("fgen_level=0\nexp_off_backup=fgen_level:3\n"); (void)dm_fgen_level(); CHECK(exp_restore_pending() == 0 && pending_value(K_FGEN) == DM_FGEN_OFF);   // 保留档 3 读盘已归 0 | the reserved 3 already reads as 0
    // 9. 安卓壳：档位设置照读（设置文件跨端互通、不改写），但一律按关生效；图形页没有这一行（方案 C 平台层，§2.8）
    // 9. Android shell: the level is still parsed (the settings file is shared across platforms and is not rewritten) but always takes effect as off; the Graphics page has no such row (plan C platform layer, §2.8)
    platform_apple = 0; write_settings("fgen_level=2\n");
    CHECK(dm_fgen_level() == DM_FGEN_SMOOTH); CHECK(!dm_fg_setting());
    CHECK(fg_row_show() == 0); CHECK(fg_row_editable() == 0);
    { int idx = key_index("auto_mana"); atomic_store(&g_pend[idx], 0); dm_custom_on_uimsg("OptionsPanelMessage", "AcceptChanges", NULL); }
    read_settings(text, sizeof text); CHECK(strstr(text, "fgen_level=2\n"));   // 别的设置存盘也不把档位改成 0 | saving another setting does not rewrite the level to 0
    write_settings("fgen_level=2\nupscale=0\n"); (void)dm_fgen_level();
    { char what[256]; CHECK(dm_custom_safe_mode(what, sizeof what) == 0); }   // 安卓壳的围栏不碰帧生成那一项 | the Android fence leaves the frame-generation entry alone
    CHECK(atomic_load(&g_val[K_FGEN]) == DM_FGEN_SMOOTH); CHECK(atomic_load(&g_bak[3]) == -1);
    read_settings(text, sizeof text); CHECK(strstr(text, "fgen_level=2\n"));
    // 9b. 围栏备份在安卓壳上：帧生成那一项不预选（不可切），应用别的设置也不清它、不改档位
    // 9b. The fence backup on the Android shell: the frame-generation entry is not pre-selected (not switchable),
    // and applying another setting neither clears it nor changes the level
    write_settings("fgen_level=0\nexp_off_backup=fgen_level:2\n"); (void)dm_fgen_level();
    CHECK(atomic_load(&g_bak[3]) == DM_FGEN_SMOOTH);
    CHECK(exp_restore_pending() == 0);
    CHECK(pending_value(K_FGEN) == DM_FGEN_OFF);
    { int idx = key_index("auto_mana"); atomic_store(&g_pend[idx], 0); dm_custom_on_uimsg("OptionsPanelMessage", "AcceptChanges", NULL); }
    read_settings(text, sizeof text);
    CHECK(strstr(text, "fgen_level=0\n"));
    CHECK(strstr(text, "exp_off_backup=fgen_level:2"));
    // 旧名字 fgen:1 同样按档位 2 保留（存盘把备份名换成新名，值不动），也不预选
    // The old name fgen:1 keeps level 2 the same way (a save rewrites the backup name, not the value), and is not pre-selected
    write_settings("fgen_level=0\nexp_off_backup=fgen:1\n"); (void)dm_fgen_level();
    CHECK(atomic_load(&g_bak[3]) == DM_FGEN_SMOOTH);
    CHECK(exp_restore_pending() == 0); CHECK(pending_value(K_FGEN) == DM_FGEN_OFF);
    { int idx = key_index("auto_mana"); atomic_store(&g_pend[idx], 0); dm_custom_on_uimsg("OptionsPanelMessage", "AcceptChanges", NULL); }
    read_settings(text, sizeof text);
    CHECK(strstr(text, "fgen_level=0\n")); CHECK(strstr(text, "fgen_level:2")); CHECK(!strstr(text, "fgen:1"));
    // 其他实验项的备份照旧预选、应用后清掉；帧生成那一项不动 | other entries still pre-select and clear; the frame-generation entry stays
    write_settings("fgen_level=0\neco_fps=0\nexp_off_backup=eco_fps:2,fgen_level:2\n"); (void)dm_fgen_level();
    CHECK(exp_restore_pending() == 1); CHECK(pending_value(K_FPS) == FPS_120); CHECK(pending_value(K_FGEN) == DM_FGEN_OFF);
    dm_custom_on_uimsg("OptionsPanelMessage", "AcceptChanges", NULL);
    read_settings(text, sizeof text);
    CHECK(strstr(text, "exp_off_backup=fgen_level:2\n")); CHECK(!strstr(text, "eco_fps:2")); CHECK(strstr(text, "fgen_level=0\n"));
    // 「默认」也只在支持帧生成的平台改这一项：安卓上不把待定档位设成 0，应用别的缺省不回写共享档位
    // Reset to defaults is gated too: on Android it does not stage 0, so applying other defaults does not rewrite the shared level
    write_settings("fgen_level=2\nupscale=0\n"); (void)dm_fgen_level();
    dm_custom_on_uimsg("OptionsPanelMessage", "ResetToDefaults", NULL);
    CHECK(pending_value(K_FGEN) == DM_FGEN_SMOOTH);
    dm_custom_on_uimsg("OptionsPanelMessage", "AcceptChanges", NULL);
    read_settings(text, sizeof text); CHECK(strstr(text, "fgen_level=2\n"));
    // 安卓没消费掉的备份留给 Apple：切回 Apple 后还能按 2 预选并恢复 | a backup Android did not consume is left for Apple: after switching back it still pre-selects 2 and restores it
    write_settings("fgen_level=0\nexp_off_backup=fgen_level:2\n"); (void)dm_fgen_level();
    exp_restore_pending(); CHECK(pending_value(K_FGEN) == DM_FGEN_OFF);
    { int idx = key_index("auto_mana"); atomic_store(&g_pend[idx], 0); dm_custom_on_uimsg("OptionsPanelMessage", "AcceptChanges", NULL); }
    platform_apple = 1; atomic_store(&g_loaded, 0); (void)dm_fgen_level();
    CHECK(atomic_load(&g_bak[3]) == DM_FGEN_SMOOTH);
    exp_restore_pending(); CHECK(pending_value(K_FGEN) == DM_FGEN_SMOOTH);
    dm_custom_on_uimsg("OptionsPanelMessage", "AcceptChanges", NULL);
    CHECK(dm_fgen_level() == DM_FGEN_SMOOTH); CHECK(atomic_load(&g_bak[3]) == -1);
    write_settings("fgen_level=0\n"); CHECK(dm_fgen_level() == DM_FGEN_OFF); CHECK(!dm_fg_setting()); CHECK(fg_row_show() == 1);
    platform_apple = 1; write_settings("fgen_level=2\n"); CHECK(fg_row_show() == 1); CHECK(fg_row_editable() == 1);
    // Apple 上不过门槛的机型（方案 C 两层门控，同光追 / 超分）：行在、不能切（勾选框置灰、副文写原因）| Apple machines failing the gate (plan C two-layer gate, as ray tracing / upscaling): the row stays, not switchable (greyed, reason as subtext)
    fg_supported = 0; CHECK(fg_row_show() == 1); CHECK(fg_row_editable() == 0); fg_supported = 1;
    display_hz = 90; CHECK(fg_row_show() == 1); CHECK(fg_row_editable() == 0); display_hz = 120;   // 90 Hz 屏有高刷 90 档、但帧生成要 120 Hz 显示 | a 90 Hz screen has the 90 stop, but frame generation needs a 120 Hz display
    // 9c. 图形页帧生成行，两种输入模式（§2.7）：手柄 A（fg_pad_press）与触屏点勾选框（fg_touch_toggle）都只在可切时改待定值；置灰时都不改；副文键两种模式同一份
    // 9c. The Graphics frame-generation row in both input modes (§2.7): controller A (fg_pad_press) and a touch on the checkbox (fg_touch_toggle) change the pending value only when switchable; greyed changes nothing; one subtext key for both modes
    CHECK(fg_pad_press(1, DM_FGEN_OFF) == DM_FGEN_SMOOTH); CHECK(fg_pad_press(1, DM_FGEN_SMOOTH) == DM_FGEN_OFF); CHECK(fg_pad_press(1, DM_FGEN_ECO) == DM_FGEN_OFF);   // 手柄：开 ↔ 关，省电档按「开着」取消 | controller: on <-> off; the reserved level counts as on
    CHECK(fg_pad_press(0, DM_FGEN_OFF) == -1); CHECK(fg_pad_press(0, DM_FGEN_SMOOTH) == -1);                                                                         // 置灰：A 不改 | greyed: A does nothing
    CHECK(fg_touch_toggle(1, 1, DM_FGEN_OFF) == DM_FGEN_SMOOTH); CHECK(fg_touch_toggle(1, 0, DM_FGEN_SMOOTH) == DM_FGEN_OFF);                                         // 触屏：勾 = 2、取消 = 0 | touch: checked = 2, unchecked = 0
    CHECK(fg_touch_toggle(1, 1, DM_FGEN_SMOOTH) == -1); CHECK(fg_touch_toggle(1, 0, DM_FGEN_OFF) == -1); CHECK(fg_touch_toggle(1, 1, DM_FGEN_ECO) == -1);            // 没变不写 | no change, no write
    CHECK(fg_touch_toggle(0, 1, DM_FGEN_OFF) == -1);                                                                                                                    // 置灰：点了不算 | greyed: a tap does not count
    CHECK(!strcmp(fg_note_key(0, FG_OFF, 0, 0), "dmGraphicsFrameGenNoHw")); CHECK(!strcmp(fg_note_key(0, FG_ON, 1, 1), "dmGraphicsFrameGenNoHw"));                     // 置灰优先写原因 | greyed: the reason first
    CHECK(!strcmp(fg_note_key(1, FG_OFF, 0, 0), "dmIosGraphicsFrameGenNote")); CHECK(!strcmp(fg_note_key(1, FG_ON, 1, 0), "dmIosGraphicsFrameGenNote"));               // 关着 / 生效：写代价 | off / in effect: the cost
    CHECK(!strcmp(fg_note_key(1, FG_ECO30, 1, 0), "dmGraphicsFrameGenNoEco")); CHECK(!strcmp(fg_note_key(1, FG_CAPPED, 1, 0), "dmGraphicsFrameGenCapped"));
    CHECK(!strcmp(fg_note_key(1, FG_RT, 1, 0), "dmGraphicsFrameGenNoRt"));
    CHECK(!strcmp(fg_note_key(1, FG_ECO_CLOSED, 1, 0), "dmGraphicsFrameGenEcoClosed")); CHECK(!strcmp(fg_note_key(1, FG_ON, 1, 1), "dmGraphicsFrameGenBackoff")); CHECK(!strcmp(fg_note_key(1, FG_ON, 0, 1), "dmIosGraphicsFrameGenNote"));
    // 页上选着高刷档（90 / 120）或光追（rt_level > 0）时副文写原因、设置照留；选回 60 / 关光追就是代价副文 | high refresh (90 / 120) or ray tracing selected on the page: the reason as subtext, the setting kept; back to 60 / ray tracing off gives the cost again
    CHECK(fg_reason(DM_FGEN_SMOOTH, 1, 90, 0) == FG_CAPPED && fg_reason(DM_FGEN_SMOOTH, 1, 120, 0) == FG_CAPPED && fg_reason(DM_FGEN_SMOOTH, 1, 60, 1) == FG_RT && fg_reason(DM_FGEN_SMOOTH, 1, 40, 0) == FG_ON);
    // 副文键都在六语表里、都标 iOS 专属（§2.8 不进安卓包）| every subtext key is in the six-language table and marked iOS-only (§2.8: out of the Android package)
    { static const char *const nk[] = { "dmGraphicsFrameGen", "dmGraphicsFrameGenNoHw", "dmIosGraphicsFrameGenNote", "dmGraphicsFrameGenNoEco", "dmGraphicsFrameGenCapped", "dmGraphicsFrameGenNoRt", "dmGraphicsFrameGenBackoff", "dmGraphicsFrameGenEcoClosed" };
      for (unsigned i = 0; i < sizeof nk / sizeof *nk; i++) { int found = 0; for (size_t k = 0; k < sizeof k_dm_i18n / sizeof *k_dm_i18n; k++) found |= !strcmp(k_dm_i18n[k].k, nk[i]) && k_dm_i18n[k].zh[0] && k_dm_i18n[k].en[0] && k_dm_i18n[k].ko[0]; CHECK(found); } }
    // 10. 分享摘要：设置值与生效状态分开（选了平滑、实际正在退档时两处对得上）| share summary: setting value and effective state separately (a chosen smooth level that is currently backed off shows both)
    write_settings("fgen_level=2\n"); (void)dm_fgen_level();
    char logbuf[8192];
    capture(run_wrapper, logbuf, sizeof logbuf); CHECK(run_buf_len == 1); CHECK(strstr(logbuf, "帧生成：状态 fg=1 开 fgen_level=2"));
    fg_supported = 0; capture(run_wrapper, logbuf, sizeof logbuf); CHECK(run_buf_len == 0); CHECK(strstr(logbuf, "帧生成：状态 fg=0 关 fgen_level=2")); fg_supported = 1;
    write_settings("fgen_level=1\n"); (void)dm_fgen_level(); capture(run_wrapper, logbuf, sizeof logbuf);
    CHECK(run_buf_len == 0); CHECK(strstr(logbuf, "fgen_level=1")); CHECK(strstr(logbuf, "省电档未开放"));
    // 10a. 安卓摘要口径（主会话 2026-11-03 裁定）：存储值照写 fgen_level=2、生效写成 fg=0，不能把设置值冒充成 0
    // 10a. Android summary (ruled by the main session on 2026-11-03): the stored value is printed as fgen_level=2 and the effective state as fg=0; the setting must not be passed off as 0
    platform_apple = 0; write_settings("fgen_level=2\n"); (void)dm_fgen_level(); capture(run_wrapper, logbuf, sizeof logbuf);
    CHECK(run_buf_len == 0); CHECK(strstr(logbuf, "帧生成：状态 fg=0 关 fgen_level=2")); CHECK(strstr(logbuf, "本端暂未接入")); CHECK(!strstr(logbuf, "fgen_level=0"));
    // capture 复用同一个缓冲区，第二次短日志不能读到上一次长日志的尾巴 | capture reuses one buffer; a second, shorter log must not expose the previous long log's tail
    { char stale[8192]; capture(run_wrapper, stale, sizeof stale); capture(run_short_log, stale, sizeof stale); CHECK(!strstr(stale, "帧生成：")); }
    platform_apple = 1;
    // 11. 实时环境：DM_TEST_FG 与 DM_FGEN_PRESET 同给时 DM_TEST_FG 优先；DM_FGEN_PRESET 压过设置
    // 11. live environment: with both DM_TEST_FG and DM_FGEN_PRESET the test switch wins; DM_FGEN_PRESET beats the setting
    write_settings("fgen_level=2\n"); (void)dm_fgen_level();
    CHECK(child_fg(NULL, NULL, DM_FGEN_SMOOTH) == 1);
    CHECK(child_fg("0", NULL, DM_FGEN_SMOOTH) == 0);              // 钉 0 压过设置 2 | pinning 0 beats setting 2
    CHECK(child_fg("2", NULL, DM_FGEN_OFF) == 1);                 // 钉 2 压过设置 0 | pinning 2 beats setting 0
    CHECK(child_fg("2", "0@0", DM_FGEN_OFF) == 0);                // 两者同给：DM_TEST_FG 赢 | both given: the test switch wins
    CHECK(child_fg("0", "1@0", DM_FGEN_SMOOTH) == 1);
    CHECK(child_fg("1", NULL, DM_FGEN_OFF) == 0);                 // 省电档不开放 | the power-saving level is not open
    CHECK(child_fg("3", NULL, DM_FGEN_SMOOTH) == 0);              // 保留档按 0 | the reserved level reads as 0
    CHECK(child_fg("x", NULL, DM_FGEN_SMOOTH) == 1);              // 非法串当没给，按设置 | an invalid string counts as unset
    // 12. B165：帧生成和高刷档在图形页上互斥（按待定值）：高刷格选着 → 帧生成置灰；帧生成勾着 → 帧率滑块最高 60 那格；两个都开着的旧档不压滑块
    // 12. B165: frame generation and the high-refresh stop exclude each other on the Graphics page (pending values): high stop -> frame generation greyed; frame generation checked -> slider tops out at 60; an old save with both on does not cap the slider
    CHECK(fg_blocked_by_fps(1, 2) == 1); CHECK(fg_blocked_by_fps(1, 1) == 0); CHECK(fg_blocked_by_fps(1, 0) == 0); CHECK(fg_blocked_by_fps(0, 2) == 0);
    CHECK(fps_capped_by_fg(1, DM_FGEN_SMOOTH, 1) == 1); CHECK(fps_capped_by_fg(1, DM_FGEN_OFF, 1) == 0); CHECK(fps_capped_by_fg(0, DM_FGEN_SMOOTH, 1) == 0); CHECK(fps_capped_by_fg(1, DM_FGEN_SMOOTH, 2) == 0);
    write_settings("fgen_level=0\neco_fps=0\n"); (void)dm_fgen_level();
    if (fps_max_pos_raw() == 2 && fg_row_editable()) {
        atomic_store(&g_pend[K_FGEN], DM_FGEN_SMOOTH); CHECK(fps_fg_cap() == 1); CHECK(fps_max_pos() == 1);
        graphics_select_fps(2); CHECK(pending_value(K_FPS) == 0);                 // 勾着帧生成时选不到 120 | 120 cannot be picked while frame generation is checked
        atomic_store(&g_pend[K_FGEN], DM_FGEN_OFF); CHECK(fps_max_pos() == 2); CHECK(fg_fps_block() == 0);
        graphics_select_fps(2); CHECK(pending_value(K_FPS) == FPS_120); CHECK(fg_fps_block() == 1);   // 选了 120 → 帧生成置灰 | picking 120 greys frame generation
        CHECK(fg_pad_press(fg_row_editable() && !fg_fps_block(), DM_FGEN_OFF) == -1);  // 置灰时手柄 A 不改 | greyed: controller A changes nothing
        atomic_store(&g_pend[K_FGEN], DM_FGEN_SMOOTH); CHECK(fps_fg_cap() == 0); CHECK(fps_max_pos() == 2);   // 两个都开（旧档）：不压滑块，帧生成那行置灰说明 | both on (old save): no cap, the frame-generation row is greyed with the reason
        display_hz = 60; CHECK(fps_max_pos_raw() == 1); CHECK(fps_max_pos() == 1); CHECK(fg_row_editable() == 0); CHECK(fps_fg_cap() == 0); display_hz = 120;   // 60 Hz 屏：没有高刷格也没有帧生成，不出上限说明 | 60 Hz screen: neither stop nor frame generation, no cap note
        atomic_store(&g_pend[K_FGEN], -1); atomic_store(&g_pend[K_FPS], -1);
    } else { fprintf(stderr, "FAIL line %d: 测试环境没有高刷格 / 帧生成（fps_max_pos_raw=%d fg_row_editable=%d）\n", __LINE__, fps_max_pos_raw(), fg_row_editable()); fails++; }
    unlink(path); rmdir(directory);
    printf("%s t_fgen_level: %d checks, %d failed\n", fails ? "FAIL" : "PASS", checks, fails);
    return fails ? 1 : 0;
}
