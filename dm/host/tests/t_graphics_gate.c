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
module_t g_mod[MAX_MOD]; int g_nmod;   // 统计 / 自动上报崩溃确认框那段要一个假的游戏主模块（勾选框类按 main + 虚表偏移认）| the consent check below needs a fake game main module (checkboxes are recognised by main + vtable offset)
int dm_padmode_current(void) { return 0; }
uint32_t dm_ui_gen(void) { return 1; }
uint64_t dm_wref_get(dm_wref *r) { return r ? r->w : 0; }
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
    // 免费回城迁移（内审 I-03）：旧文件的 free_tp=0 迁移一次到开；带 free_tp_v2 的 0 是玩家关的，保持关；存盘带标记 | free town portal migration (internal audit I-03): an old file's free_tp=0 migrates once to on; a 0 with free_tp_v2 is the player's choice and stays off; saves carry the marker
    f = fopen(path, "w"); CHECK(f); fputs("free_tp=0\nq3d_v2=1\n", f); fclose(f); atomic_store(&g_loaded, 0); CHECK(dm_custom_get("free_tp") == 1);
    f = fopen(path, "w"); CHECK(f); fputs("q3d_v2=1\n", f); fclose(f); atomic_store(&g_loaded, 0); CHECK(dm_custom_get("free_tp") == 1);
    f = fopen(path, "w"); CHECK(f); fputs("free_tp=0\nfree_tp_v2=1\nq3d_v2=1\n", f); fclose(f); atomic_store(&g_loaded, 0); CHECK(dm_custom_get("free_tp") == 0);
    save(); atomic_store(&g_loaded, 0); CHECK(dm_custom_get("free_tp") == 0);
    { char txt[2048] = {0}; f = fopen(path, "r"); CHECK(f); fread(txt, 1, sizeof txt - 1, f); fclose(f); CHECK(strstr(txt, "free_tp=0\n") && strstr(txt, "free_tp_v2=1\n")); }
    f = fopen(path, "w"); CHECK(f); fputs("upscale=1\nq3d=2\nq3d_v2=1\nrender_res=3\n", f); fclose(f); atomic_store(&g_loaded, 0);
    // 行打包 | row packing
    int v[7], n;
    n = graphics_rows_pack(1, 1, 1, 1, v); CHECK(n == 7 && v[0] == ROW_RES && v[1] == ROW_BRIGHT && v[2] == ROW_FPSHUD && v[3] == ROW_FPS && v[4] == ROW_UPSCALE && v[5] == ROW_PROFILE && v[6] == ROW_RT);   // Apple 全开；光追组只有档位一行（rt_level）| Apple, everything; the ray-tracing group is the one level row (rt_level)
    CHECK(graphics_row_y(v, 0) == 180 && graphics_row_y(v, 1) == 360 && graphics_row_y(v, 2) == 720 && graphics_row_y(v, 3) == 900 && graphics_row_y(v, 4) == 1260 && graphics_row_y(v, 6) == 1800);   // 换组多一行标题 | one heading row per group change
    n = graphics_rows_pack(1, 1, 0, 1, v); CHECK(n == 6 && v[5] == ROW_RT);                   // 档位行藏着：光追上移一格（原行为）| profile hidden: ray tracing moves up
    n = graphics_rows_pack(1, 1, 0, 0, v); CHECK(n == 5 && v[4] == ROW_UPSCALE);
    n = graphics_rows_pack(0, 0, 0, 0, v); CHECK(n == 3 && v[0] == ROW_BRIGHT && v[1] == ROW_FPSHUD && v[2] == ROW_FPS);   // 安卓 render_res 关时的最少行 | fewest rows
    n = graphics_rows_pack(1, 0, 0, 0, v); CHECK(n == 4 && v[0] == ROW_RES && v[1] == ROW_BRIGHT && v[2] == ROW_FPSHUD && v[3] == ROW_FPS);   // 安卓 | Android
    n = graphics_rows_pack(0, 0, 1, 1, v); CHECK(n == 4 && v[0] == ROW_BRIGHT && v[1] == ROW_FPSHUD && v[2] == ROW_FPS && v[3] == ROW_RT);
    // 自定义页的平台限制：有 iCloud 时 icloud 行在，没有时整页少一行、后面的行号前移（与 touch_layout.py DM_TARGET=android 对齐）
    // Per-platform Custom rows: with iCloud the icloud row exists; without it the page has one row fewer and later rows move up (matching touch_layout.py DM_TARGET=android)
    int n_gfx = 0, n_game = 0; for (int i = 0; i < NK; i++) { n_gfx += graphics_key(i); n_game += row_on_page(i, PG_GAME); }
    icloud_ok = 1; int rows_apple = custom_rows(); CHECK(n_gfx == 7 && rows_apple == 4 + VOICE_ROW && rows_apple + n_game == NK - n_gfx && custom_key_at(1 + VOICE_ROW) == K_ICLOUD && row_on_page(K_TOUCH_SWAP, PG_GAME));
    // 配音行在缺省包里也在（VOICE_ROW 恒为 1）：语言之后第一行，手柄与触屏都按这张键表出行 | the voice row is on default builds too (VOICE_ROW is always 1): first after language; both controller and touch iterate this key table
    CHECK(VOICE_ROW == 1 && K_VOICE == 1 && custom_row(K_VOICE) && custom_key_at(1) == K_VOICE);
    icloud_ok = 0; CHECK(custom_rows() == rows_apple - 1 && !custom_row(K_ICLOUD) && custom_key_at(rows_apple - 2) == key_index("auto_crash"));
    // RC2 并入的四个键的归属（方案 C × rt_level 移植）| where the four keys RC2 brought in live (plan C x rt_level port)
    { int rl = key_index("rt_level"), ts = key_index("tel_stats"), ac = key_index("auto_crash"), br = key_index("belt_refill"); const char *g_rl = NULL, *g_ts = NULL, *g_ac = NULL, *g_br = NULL, *g_pp = NULL;
      for (int o = 0; o < N_OPTS; o++) { const char *k = k_opts[o].key; if (!strcmp(k, "rt_level")) g_rl = k_opts[o].group; if (!strcmp(k, "tel_stats")) g_ts = k_opts[o].group; if (!strcmp(k, "auto_crash")) g_ac = k_opts[o].group; if (!strcmp(k, "belt_refill")) g_br = k_opts[o].group; if (!strcmp(k, "pot_pick")) g_pp = k_opts[o].group;
          CHECK(strcmp(k, "rt_shadow") && strcmp(k, "rt_ao"));   // 旧的两个光追开关不再占行 | the two old ray-tracing switches hold no row any more
          if (!strcmp(k_opts[o].group, "Rt")) CHECK(!strcmp(k, "rt_level")); }   // 光追组只放档位这一行 | the ray-tracing group holds only the level row
      CHECK(rl == K_RTLVL && opt_page(rl) == PG_GFX && g_rl && !strcmp(g_rl, "Rt")); for (int o = 0; o < N_OPTS; o++) if (!strcmp(k_opts[o].key, "rt_level")) CHECK(k_opts[o].plat == PF_APPLE);
      CHECK(ts == K_TELSTATS && ac == K_AUTOCRASH && custom_row(ts) && custom_row(ac) && g_ts && g_ac && !strcmp(g_ts, "Privacy") && !strcmp(g_ac, "Privacy"));
      CHECK(br >= 0 && key_index("pot_pick") >= 0 && row_on_page(br, PG_GAME) && g_br && g_pp && !strcmp(g_br, "Loot") && !strcmp(g_br, g_pp) && k_def[br] == 1); }
    icloud_ok = 1;
    // 全部可见（Apple）时各行的 y：与布局 touch_layout.py 的初始位置是同一张表（test_touch_layout_options.py 核布局那一侧，审计 r1 P2-5）
    // Row y with everything visible (Apple): the same table as the layout's initial positions in touch_layout.py (test_touch_layout_options.py checks the layout side, audit r1 P2-5)
    { static const int want[7] = { 180, 360, 720, 900, 1260, 1440, 1800 }; n = graphics_rows_pack(1, 1, 1, 1, v); for (int i = 0; i < 7; i++) CHECK(graphics_row_y(v, i) == want[i]); CHECK(graphics_row_y(v, 6) + 180 == 1980); }
    // 平台门控照键表（审计 r1 P2-3）：Apple 有超分组、光追组 | platform gate from the key table (audit r1 P2-3): Apple has the upscaling and ray-tracing groups
    { int up = 0, rt = 0; graphics_plat_rows(&up, &rt); CHECK(up == 1 && rt == 1); }
    // 画面加亮档位（审计 r1 P2-6）：没写过 = 缺省「中」2；0–3 原样；坏值回落到缺省 2，不是 0 | brightening level (audit r1 P2-6): unset = default mid 2; 0-3 kept; bad values fall back to the default 2, not 0
    { struct { const char *file; int want; } bc[] = { { "q3d_v2=1\n", 2 }, { "bright_level=0\nq3d_v2=1\n", 0 }, { "bright_level=3\nq3d_v2=1\n", 3 }, { "bright_level=7\nq3d_v2=1\n", 2 },
          { "bright_level=x\nq3d_v2=1\n", 2 }, { "bright_level=-1\nq3d_v2=1\n", 2 }, { "bright_level=1\r\nq3d_v2=1\n", 1 }, { "bright_level=\nq3d_v2=1\n", 2 } };
      for (unsigned k = 0; k < sizeof bc / sizeof *bc; k++) { f = fopen(path, "w"); CHECK(f); fputs(bc[k].file, f); fclose(f); atomic_store(&g_loaded, 0); CHECK(dm_custom_get("bright_level") == bc[k].want); }
      CHECK(k_def[K_BRIGHT] == 2 && !strcmp(k_keys[K_BRIGHT], "bright_level")); }
    // 游戏页手柄停靠点（审计 r1 P2-6）：组的折叠钮、可见的勾选框按表序；只有触屏才起作用的那一行（换组键位置）不停靠；收起组里藏着的行不停靠；末项是原版表格组的折叠钮。
    // 「存放」按钮两种模式都在屏上（REQ12），手柄模式下照常停靠 | Controller focus stops (audit r1 P2-6): group fold buttons and visible checkboxes in table order; the one touch-only row (swap position) is not a stop; hidden rows of a collapsed group are not stops; the last is the original table's fold button.
    // The Deposit button is on screen in both modes (REQ12), so it is a stop in controller mode too
    { static unsigned char wbuf[48][0x100]; static uint64_t kids[48]; static char names[48][48]; int nw = 0;
      #define W(nm, vis) do { uint64_t w_ = (uint64_t)wbuf[nw]; memset(wbuf[nw], 0, 0x100); snprintf(names[nw], 48, "%s", nm); *(const char **)(w_ + 8) = names[nw]; *(uint8_t *)(w_ + 0x50) = (vis); kids[nw++] = w_; } while (0)
      static const char *const fold[4] = { "TouchFoldCombat", "TouchFoldLoot", "TouchFoldUi", "TouchFoldOrig" }; for (int g = 0; g < 4; g++) W(fold[g], 1);
      for (int i = 1; i < NK; i++) if (row_on_page(i, PG_GAME)) { char nm[48]; snprintf(nm, sizeof nm, "TouchCustom_%s", k_keys[i]); W(nm, strcmp(k_keys[i], "drop_light") != 0); }   // drop_light 扮演收起组里藏着的行 | drop_light plays a hidden row of a collapsed group
      static unsigned char gp[0x100]; *(const char **)(gp + 8) = "GameplayOptionsPanel"; *(uint64_t *)(gp + 0x58) = (uint64_t)kids; *(uint64_t *)(gp + 0x60) = (uint64_t)nw;
      unsigned long long st[32]; int ns = dm_custom_game_stops((unsigned long long)(uint64_t)gp, st, 32), folds = 0, boxes = 0;
      CHECK(ns > 4 && !strcmp(*(const char **)(st[0] + 8), "TouchFoldCombat") && !strcmp(*(const char **)(st[ns - 1] + 8), "TouchFoldOrig"));
      for (int k = 0; k < ns; k++) { const char *nm = *(const char **)(st[k] + 8); folds += !strncmp(nm, "TouchFold", 9); boxes += !strncmp(nm, "TouchCustom_", 12);
          CHECK(strcmp(nm, "TouchCustom_touch_swap_above") && strcmp(nm, "TouchCustom_drop_light")); }   // 商店钮照常停靠（REQ12）| the store button is a stop as usual (REQ12)
      int want_boxes = 0; for (int i = 1; i < NK; i++) want_boxes += row_on_page(i, PG_GAME) && !touch_only_row(i) && strcmp(k_keys[i], "drop_light");
      CHECK(folds == 4 && boxes == want_boxes && ns == folds + boxes);
      #undef W
    }
    // iCloud 可用性探测节流（审计 r1 P2-8）：页刚出现问一次，之后 60 帧一次 | iCloud probe throttling (audit r1 P2-8): once when the page appears, then every 60 frames
    { g_icloud_probes = 0; icloud_available_throttled(1); for (int k = 0; k < 119; k++) icloud_available_throttled(0); CHECK(g_icloud_probes == 2); icloud_available_throttled(1); CHECK(g_icloud_probes == 3); }
    // 旧 sw_custom.txt（方案 C 之前 / RC2 开发包，只有 rt_shadow / rt_ao）读入：走 23a2ee3e 原有的 rt_level_migrate——阴影 + AO → 高，阴影 → 中，阴影关 → 关；有 rt_level 时以它为准；存盘写 rt_level
    // old sw_custom.txt (before plan C / RC2 development builds, rt_shadow / rt_ao only): 23a2ee3e's own rt_level_migrate -- shadows + AO -> high, shadows -> medium, off -> off; rt_level wins when present; saving writes rt_level
    { struct { const char *file; int want; } mc[] = { { "rt_shadow=1\nrt_ao=1\nq3d_v2=1\n", 3 }, { "rt_shadow=1\nq3d_v2=1\n", 2 }, { "rt_shadow=1\nrt_ao=0\nbright_level=1\nq3d_v2=1\n", 2 },
          { "rt_shadow=0\nrt_ao=1\nq3d_v2=1\n", 0 }, { "rt_ao=1\nq3d_v2=1\n", 0 }, { "rt_level=1\nrt_shadow=1\nrt_ao=1\nq3d_v2=1\n", 1 } };
      for (unsigned k = 0; k < sizeof mc / sizeof *mc; k++) { f = fopen(path, "w"); CHECK(f); fputs(mc[k].file, f); fclose(f); atomic_store(&g_loaded, 0); CHECK(dm_custom_get("rt_level") == mc[k].want); }
      f = fopen(path, "w"); CHECK(f); fputs("rt_shadow=1\nrt_ao=1\nq3d_v2=1\n", f); fclose(f); atomic_store(&g_loaded, 0); CHECK(dm_custom_get("rt_level") == 3); save();
      char txt[2048] = {0}; f = fopen(path, "r"); CHECK(f); fread(txt, 1, sizeof txt - 1, f); fclose(f); CHECK(strstr(txt, "rt_level=3\n") && !strstr(txt, "rt_ao="));
      atomic_store(&g_loaded, 0); CHECK(dm_custom_get("rt_level") == 3); }
    // 统计 / 自动上报崩溃（系统页「隐私」组）走表驱动的 sync_toggles 后，确认框逻辑照旧（RC2 abf63236 / 8d68026a 原样）：没答过告知框 / 没同意时勾上 = 勾选框退回、开确认框，
    // 用户不同意（弱定义 dm_diagup_consent_open 答 0）什么都不变；同意（ac_consent_cb(1)）才记待定值。普通行照常记待定值
    // Statistics / automatic crash reports (System page, Privacy group) under the table-driven sync_toggles keep the consent logic (as RC2 abf63236 / 8d68026a): with no notice answered / no consent,
    // ticking falls the checkbox back and opens the box; declining (the weak dm_diagup_consent_open answers 0) changes nothing; only agreeing (ac_consent_cb(1)) records a pending value. Plain rows record pending as usual
    { static uint8_t mainb[16]; snprintf(g_mod[0].name, sizeof g_mod[0].name, "main"); g_mod[0].base = mainb; g_nmod = 1;
      f = fopen(path, "w"); CHECK(f); fputs("q3d_v2=1\n", f); fclose(f); atomic_store(&g_loaded, 0); load_once(); atomic_store(&g_tel_notice, 0); atomic_store(&g_ac_ok, 0); atomic_store(&g_ac_ok_pending, 0);
      static uint8_t wb[3][0xb00]; static uint64_t kids[3]; static const char *const nm[3] = { "TouchCustom_tel_stats", "TouchCustom_auto_crash", "TouchCustom_icloud" };
      for (int k = 0; k < 3; k++) { memset(wb[k], 0, sizeof wb[k]); *(uint64_t *)(uintptr_t)wb[k] = (uint64_t)(uintptr_t)mainb + DM_VT_TOGGLE; *(const char **)(wb[k] + 8) = nm[k]; wb[k][0x50] = 1; kids[k] = (uint64_t)(uintptr_t)wb[k]; }
      static uint8_t sp[0x100]; *(const char **)(sp + 8) = "VideoOptionsPanel"; *(uint64_t *)(sp + 0x58) = (uint64_t)(uintptr_t)kids; *(uint64_t *)(sp + 0x60) = 3;
      uint64_t ts = (uint64_t)(uintptr_t)wb[0], ac = (uint64_t)(uintptr_t)wb[1], ic = (uint64_t)(uintptr_t)wb[2]; static dm_wref seen[NK]; memset(seen, 0, sizeof seen);
      for (int i = 0; i < NK; i++) atomic_store(&g_pend[i], -1);
      sync_toggles((uint64_t)(uintptr_t)sp, PG_SYS, seen); CHECK(!TOGGLED(ts) && !TOGGLED(ac));   // 页刚出现：摆成生效值（缺省关）| page just shown: set to the effective values (off by default)
      set_toggle(ts, 1); set_toggle(ac, 1); set_toggle(ic, !TOGGLED(ic)); sync_toggles((uint64_t)(uintptr_t)sp, PG_SYS, seen);
      CHECK(!TOGGLED(ts) && !TOGGLED(ac) && atomic_load(&g_pend[K_TELSTATS]) == -1 && atomic_load(&g_pend[K_AUTOCRASH]) == -1);   // 不同意：勾选退回，待定值不动 | declined: ticks fall back, nothing pending
      CHECK(atomic_load(&g_pend[K_ICLOUD]) == TOGGLED(ic));   // 普通行照常记待定值 | a plain row records its pending value
      ac_consent_cb(1); CHECK(atomic_load(&g_pend[K_AUTOCRASH]) == 1 && atomic_load(&g_ac_ok_pending) == 1);   // 同意：待定值勾上，点「应用」才生效 | agreed: pending on, effective on Apply
      atomic_store(&g_ac_ok_pending, 0); for (int i = 0; i < NK; i++) atomic_store(&g_pend[i], -1);
      atomic_store(&g_tel_notice, 1); memset(seen, 0, sizeof seen); sync_toggles((uint64_t)(uintptr_t)sp, PG_SYS, seen); set_toggle(ts, 1); sync_toggles((uint64_t)(uintptr_t)sp, PG_SYS, seen);
      CHECK(TOGGLED(ts) && atomic_load(&g_pend[K_TELSTATS]) == 1);   // 告知框答过之后勾上直接记待定值 | once the notice is answered a tick is recorded directly
      atomic_store(&g_tel_notice, 0); for (int i = 0; i < NK; i++) atomic_store(&g_pend[i], -1); g_nmod = 0; }
    unlink(path); rmdir(dir);
    printf("PASS t_graphics_gate: %d checks\n", checks); return 0;
}
