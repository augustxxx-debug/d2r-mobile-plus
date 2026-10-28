// 内存档位（用户 2026-10-29：「要按机型考虑，不同机型运存不一样」）：按本进程内存上限分三档——≥ 3200 MB 照旧（0 副作用）；2600–3199 MB 不开放 120 帧（图形页说明）、
// 缺省 MetalFX 省电、渲染分辨率均衡；< 2600 MB 再降到渲染分辨率省电、主菜单提示一次。缺省只在首次 / 档位变化时套，玩家改过的值不动。只用临时目录，不碰真存档
// Memory tier (user 2026-10-29: "consider the model, devices have different RAM"): three tiers by this process's memory limit - >= 3200 MB unchanged (no side effect); 2600-3199 MB no 120 fps (Graphics note),
// default MetalFX Eco and render resolution Balanced; < 2600 MB also render resolution Eco and a one-time main-menu notice. Defaults apply only on first run / tier change, values the player changed stay. Temporary directories only, never real saves
#define DM_DISPLAY_HZ_TEST_HOOK
#include "../dm_custom.c"
#include "../dm_crashfence.c"
#include <unistd.h>
#include <sys/stat.h>
static int quality;
void dm_3d_quality_mode_set(int value) { quality = value; }
int dm_lang_group(void) { return DM_LG_EN; }
const char *dm_save_commitdir(void) { return NULL; }
int dm_3d_quality_mode_get(void) { return quality; }
float dm_3d_scale_cur(void) { return .75f; }
static int display_hz = 120;
int dm_display_max_hz(void) { return display_hz; }
const char *dm_ui(const char *key) { return key; }
void (*g_crash_note_hook)(int sig);   // crash.c 里的定义（这里不链 crash.c） | defined in crash.c (not linked here)
uint64_t crash_last_present_ns(void) { return 0; }
uint64_t crash_now_ns(void) { return 0; }
static int checks, fails;
#define CHECK(x) do { checks++; if (!(x)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)
static char dir[64], settings[128];
static void read_settings(char *out, size_t n) { FILE *f = fopen(settings, "r"); size_t r = f ? fread(out, 1, n - 1, f) : 0; out[r] = 0; if (f) fclose(f); }
static void write_settings(const char *s) { FILE *f = fopen(settings, "w"); fputs(s, f); fclose(f); }
static void restart(int limit_mb) {   // 「重启进程」：设置模块重读、提示框状态清回初值、按新上限量一次 | "restart the process": settings re-read, notice state reset, measured with the new limit
    atomic_store(&g_loaded, 0); atomic_store(&g_boot_res, -1); atomic_store(&g_boot_q3d, -1); atomic_store(&g_notice, 0); atomic_store(&g_show, 0); atomic_store(&g_ack, 0); g_kind = 0;
    for (int i = 0; i < NK; i++) atomic_store(&g_pend[i], -1);
    dm_mem_measure_set(limit_mb, 12288, "iPhoneTest,1"); load_once();
}
int main(void) {
    snprintf(dir, sizeof dir, "/tmp/d2r-memtier-XXXXXX"); if (!mkdtemp(dir)) return 1;
    snprintf(settings, sizeof settings, "%s/sw_custom.txt", dir);
    unsetenv("DM_MEM_TIER"); unsetenv("DM_SAVE"); unsetenv("DM_TEST_FPS");
    dm_custom_init(dir);
    char text[4096];

    // 1. 阈值按上限（不是物理内存）：没量到（-1，Mac / 单测）= 最高档 | 1. Thresholds by the limit (not physical RAM): not measured (-1, Mac / unit tests) = top tier
    CHECK(mem_tier_of(-1) == 2); CHECK(mem_tier_of(6144) == 2); CHECK(mem_tier_of(3375) == 2); CHECK(mem_tier_of(3200) == 2);
    CHECK(mem_tier_of(3199) == 1); CHECK(mem_tier_of(2600) == 1); CHECK(mem_tier_of(2599) == 0); CHECK(mem_tier_of(2200) == 0);
    // 2. DM_MEM_TIER 覆盖实测；非法值不认 | 2. DM_MEM_TIER overrides the measurement; invalid values are ignored
    dm_mem_measure_set(3375, 12288, "iPhoneTest,1");
    setenv("DM_MEM_TIER", "1", 1); CHECK(dm_mem_tier() == 1); setenv("DM_MEM_TIER", "0", 1); CHECK(dm_mem_tier() == 0);
    setenv("DM_MEM_TIER", "9", 1); CHECK(dm_mem_tier() == 2); setenv("DM_MEM_TIER", "1x", 1); CHECK(dm_mem_tier() == 2); unsetenv("DM_MEM_TIER");

    // 3. ≥ 3.2 GB：与现在一致——缺省不变、120 帧照常、不写设置文件、不提示 | 3. >= 3.2 GB: as today - defaults unchanged, 120 fps as before, settings file not written, no notice
    unlink(settings); restart(3375);
    for (int i = 1 + VOICE_ROW; i < NK; i++) CHECK(atomic_load(&g_val[i]) == k_def[i]);
    CHECK(fps_max_pos() == 2); CHECK(!fps_mem_capped()); CHECK(access(settings, F_OK) != 0); CHECK(!dm_custom_mem_notice_due());
    write_settings("q3d=0\nq3d_v2=1\neco_fps=2\nrender_res=0\n"); restart(3375); read_settings(text, sizeof text);
    CHECK(!strcmp(text, "q3d=0\nq3d_v2=1\neco_fps=2\nrender_res=0\n")); CHECK(dm_fps_setting() == 120);   // 文件一字不改 | file byte-identical
    restart(6144); CHECK(dm_mem_tier() == 2); CHECK(fps_max_pos() == 2);   // 带权益的包（约 6 GB）自然落最高档 | a build with the entitlement (~6 GB) lands on the top tier
    dm_custom_on_uimsg("OptionsPanelMessage", "ResetToDefaults", NULL); CHECK(pending_value(K_Q3D) == 0); CHECK(pending_value(K_RES) == 0);
    dm_custom_on_uimsg("OptionsPanelMessage", "CancelChanges", NULL);

    // 4. 2.6–3.2 GB 首次：缺省 MetalFX 省电、渲染均衡；120 帧格不出现、存着的 120 按 60；图形页帧率行有说明；记下档位 | 4. 2.6-3.2 GB first run
    unlink(settings); restart(2900);
    CHECK(dm_mem_tier() == 1); CHECK(atomic_load(&g_val[K_Q3D]) == 1); CHECK(atomic_load(&g_val[K_RES]) == 2); CHECK(atomic_load(&g_val[K_UPSCALE]) == 1);
    CHECK(fps_max_pos() == 1); CHECK(fps_mem_capped()); CHECK(dm_fps_setting() == 60);
    read_settings(text, sizeof text); CHECK(strstr(text, "mem_tier=1\n")); CHECK(strstr(text, "q3d=1\n")); CHECK(strstr(text, "render_res=2\n"));
    CHECK(!dm_custom_mem_notice_due());
    write_settings("q3d=0\nq3d_v2=1\neco_fps=2\nmem_tier=1\n"); restart(2900); CHECK(dm_fps_setting() == 60); CHECK(fps_pos(pending_value(K_FPS)) == 1);   // 存着 120：按 60 | saved 120 runs at 60
    display_hz = 60; CHECK(!fps_mem_capped()); display_hz = 120;   // 60 Hz 屏本来就没有 120，不出内存说明 | a 60 Hz screen never had 120: no memory note
    dm_custom_on_uimsg("OptionsPanelMessage", "ResetToDefaults", NULL); CHECK(pending_value(K_Q3D) == 1); CHECK(pending_value(K_RES) == 2);   // 默认 = 本档缺省 | Defaults = this tier's defaults
    dm_custom_on_uimsg("OptionsPanelMessage", "CancelChanges", NULL);

    // 5. 玩家改过的值不动：旧文件里 q3d / render_res 已不是缺省 → 首次进 1 档也保留；同档再启动不再套 | 5. values the player changed stay
    write_settings("q3d=3\nq3d_v2=1\nrender_res=3\n"); restart(2900);
    CHECK(atomic_load(&g_val[K_Q3D]) == 3); CHECK(atomic_load(&g_val[K_RES]) == 3); read_settings(text, sizeof text); CHECK(strstr(text, "mem_tier=1\n"));
    write_settings("q3d=0\nq3d_v2=1\nrender_res=0\nmem_tier=1\n"); restart(2900);   // 同档：玩家把它们改回自动 / 自动，不再被改 | same tier: the player set them back to Auto, left alone
    CHECK(atomic_load(&g_val[K_Q3D]) == 0); CHECK(atomic_load(&g_val[K_RES]) == 0);

    // 6. 档位变化 1 → 2（换了带权益的包）：还停在 1 档缺省的回到通用缺省，玩家改过的不动 | 6. tier change 1 -> 2: values still at tier-1 defaults go back to the general defaults
    write_settings("q3d=1\nq3d_v2=1\nrender_res=3\nmem_tier=1\n"); restart(6144);
    CHECK(atomic_load(&g_val[K_Q3D]) == 0); CHECK(atomic_load(&g_val[K_RES]) == 3); read_settings(text, sizeof text); CHECK(strstr(text, "mem_tier=2\n"));

    // 7. < 2.6 GB 首次：缺省最低档（MetalFX 省电、渲染省电），主菜单提示一次；点确定后记下，下次不再提示 | 7. < 2.6 GB first run
    unlink(settings); restart(2200);
    CHECK(dm_mem_tier() == 0); CHECK(atomic_load(&g_val[K_Q3D]) == 1); CHECK(atomic_load(&g_val[K_RES]) == 1); CHECK(fps_max_pos() == 1);
    CHECK(dm_custom_mem_notice_due());
    dm_crashfence_main_menu(); CHECK(atomic_load(&g_show) == 1); CHECK(g_kind == NOTICE_LOWMEM);
    atomic_store(&g_show, 2); dm_crashfence_on_uimsg("PanelManager", "ClosePanel", "TouchSafeOk"); CHECK(atomic_load(&g_ack) == 1);
    notice_dismissed(); CHECK(!dm_custom_mem_notice_due()); read_settings(text, sizeof text); CHECK(strstr(text, "mem_notice=1\n"));
    restart(2200); CHECK(!dm_custom_mem_notice_due()); dm_crashfence_main_menu(); CHECK(atomic_load(&g_show) == 0);   // 只提示一次 | only once
    // 崩溃围栏的提示优先；内存提示留到下次 | the crash-fence notice wins; the memory notice waits for the next launch
    unlink(settings); restart(2200); atomic_store(&g_notice, 1);
    dm_crashfence_main_menu(); CHECK(g_kind == NOTICE_CRASH); CHECK(dm_custom_mem_notice_due());

    // 8. 日志：「内存档位：」行带上限、物理内存、机型、档位与套用的缺省 | 8. log: the "内存档位：" line carries limit, physical RAM, model, tier and the defaults applied
    { char logp[128]; snprintf(logp, sizeof logp, "%s/log.txt", dir); unlink(settings);
      int saved = dup(2); CHECK(saved >= 0); CHECK(freopen(logp, "w", stderr) != NULL); restart(2900); fflush(stderr); dup2(saved, 2); close(saved);
      FILE *f = fopen(logp, "r"); size_t r = f ? fread(text, 1, sizeof text - 1, f) : 0; text[r] = 0; if (f) fclose(f); unlink(logp);
      char *line = strstr(text, "内存档位："); CHECK(line != NULL);
      if (line) { char *e = strchr(line, '\n'); if (e) *e = 0; CHECK(strstr(line, "上限 2900 MB")); CHECK(strstr(line, "物理 12.0 GB")); CHECK(strstr(line, "iPhoneTest,1")); CHECK(strstr(line, "档位 1")); } }

    // 9. 启动时生效的值（分享摘要用）与「调高分辨率要下次启动」：只在 1 / 0 档、只在调到比启动档高时 | 9. launch values (share summary) and "raising resolution needs a restart": only tiers 1 / 0, only above the launch level
    { int t = -9, l = -9, ph = -9, r0 = -9, q0 = -9; unlink(settings); restart(2900); dm_mem_boot_info(&t, &l, &ph, &r0, &q0);
      CHECK(t == 1 && l == 2900 && ph == 12288 && r0 == 2 && q0 == 1); CHECK(!strcmp(dm_mem_model(), "iPhoneTest,1"));
      CHECK(!res_raise_needs_restart());
      graphics_select_res(1); CHECK(!res_raise_needs_restart());   // 调低到省电：当场生效 | lowering to Eco applies at once
      graphics_select_res(0); CHECK(res_raise_needs_restart());    // 自动 > 均衡 | Auto > Balanced
      graphics_select_res(3); CHECK(res_raise_needs_restart());    // 原生 | Native
      dm_custom_on_uimsg("OptionsPanelMessage", "AcceptChanges", NULL); CHECK(res_raise_needs_restart()); CHECK(atomic_load(&g_boot_res) == 2);   // 应用了也一样：启动值不变 | still after Apply: the launch value stays
      write_settings("q3d=0\nq3d_v2=1\nrender_res=0\nupscale=0\n"); restart(3375); dm_mem_boot_info(&t, NULL, NULL, &r0, &q0);
      CHECK(t == 2 && r0 == 0 && q0 == 4); graphics_select_res(3); CHECK(!res_raise_needs_restart());   // 2 档：启动预留不缩，没有这条 | tier 2: reserve not shrunk, no note
      dm_custom_on_uimsg("OptionsPanelMessage", "CancelChanges", NULL); }

    unlink(settings); rmdir(dir);
    printf("t_mem_tier: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
