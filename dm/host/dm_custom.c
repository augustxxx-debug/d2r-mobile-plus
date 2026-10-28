// 「自定义」设置：我们自己加的功能的启停开关。接口见 dm_custom.h。
// 存储：存档提交目录旁的 sw_custom.txt（iOS 上在 Documents 里，升级包不丢），一行一个「键=0/1」；没有这个文件 / 没写的键 = 各键缺省（省电模式关，其余开）；不认识的行（旧版的 drop_filter、ver）读时跳过。
// 掉落筛选照红叶 mod 一件都不藏，整块拆掉，drop_filter 键与它的 ver=2 迁移一起删了
// 不写进游戏自己的设置存档：不依赖它的格式与写盘时机，应用 / 放弃 / 默认由我们自己按原版选项页的语义做。
// 界面：选项面板「自定义」页（借原版视频页 VideoOptionsPanel），每行一个原版勾选框，控件名 TouchCustom_<键>（布局 touch_layout.py 的 _custom_page）。勾选只记成待定值，
// 「应用」（OptionsPanelMessage:AcceptChanges）才生效并存盘，「放弃」（CancelChanges）丢掉待定值，「默认」（ResetToDefaults）待定值回各键缺省（省电模式关、其余开；还要点应用）
// 「自定义」(Custom) settings: on/off switches for the features we added ourselves. For the interface see dm_custom.h.
// Storage: sw_custom.txt next to the save commit directory (on iOS it is in Documents and survives app upgrades), one 'key=0/1' per line; without this file / for keys not written = each key's default (power-saving mode off, the rest on); unrecognized lines (drop_filter and ver from old versions) are skipped when reading.
// Drop filtering follows the Hongye community mod and hides nothing, so the whole block was removed, and the drop_filter key was deleted together with its ver=2 migration
// Not written into the game's own settings save: no dependence on its format or on when it writes to disk; apply / discard / default are done by ourselves following the semantics of the original game's options page.
// UI: the 「自定义」(Custom) page of the options panel (borrowing the original game's video page VideoOptionsPanel), one original-game checkbox per row, widget name TouchCustom_<key> (_custom_page in the layout touch_layout.py). Checking only records a pending value;
// only 「应用」(Apply) (OptionsPanelMessage:AcceptChanges) makes it take effect and saves to disk, 「放弃」(Discard) (CancelChanges) throws away the pending values, and 「默认」(Default) (ResetToDefaults) sets the pending values back to each key's default (power-saving mode off, the rest on; Apply still has to be tapped)
#include "dm_custom.h"
#include "dm_community.h"
#include "dm_res_cap.h"
#include "dm_widget.h"
#include "dm_cursor.h"
#include "hle.h"
#include "dm_input.h"
#include "dm_i18n.h"
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

// 「配音」单选（voice：0 中文配音 / 1 English voice，公开）只在编进过场解码（DM_MOVIE_FFMPEG，Makefile 的 MOVIE_FFMPEG=1）的包里有：
// 布局侧 touch_layout.py 的 _CUSTOM_KEYS 按同一个开关（环境变量 MOVIE_FFMPEG=1）加这一行。缺省包键表、行序、存盘内容与原来一字不差
#ifdef DM_MOVIE_FFMPEG
#define VOICE_ROW 1
#else
#define VOICE_ROW 0
#endif
static const char *const k_keys[] = { "lang",
#if VOICE_ROW
    "voice",
#endif
    "auto_mana", "merc_pot", "drop_light", "map_mark", "item_notes", "icloud", "pot_pick", "drop_haptic", "gem_pick", "store_btn", "eco_fps", "q3d", "loot_filter", "auto_buff", "touch_swap_above", "fps_hud", "upscale", "render_res", "rt_shadow" };   // fps_hud 右上角帧数标签（front_ios.m attach_fps_hud，缺省关） | fps_hud: top-right frame-rate label (front_ios.m attach_fps_hud, off by default)   // upscale 开关与上次选择的 q3d 档位分开保存；render_res 整帧输出分辨率档位 0 自动 / 1 省电 / 2 均衡 / 3 原生（dm_res_cap.h） | render_res: whole-frame output resolution level (dm_res_cap.h)
#define NK (int)(sizeof k_keys / sizeof *k_keys)
static const int k_def[NK] = { 0,
#if VOICE_ROW
    1,
#endif
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 0, 0, 1, 0, 0 };   // 末尾 rt_shadow（光追阴影，实验性，图形页）缺省关 | last: rt_shadow (ray-traced shadows, experimental, Graphics tab) off by default   // 缺省：60 FPS、MetalFX 开、Auto 档、渲染分辨率自动 | Defaults: 60 FPS, MetalFX on, Auto profile, render resolution Auto
// q3d 五档：自动／省电／均衡／高画质／原版；q3d_v2 标记避免旧布尔值被误读。
// q3d profiles: Auto / Eco / Balanced / Quality / Original; q3d_v2 distinguishes old boolean preferences.
#define K_VOICE (VOICE_ROW ? 1 : -2)   // 缺省包没有这一行：-2 不等于任何行号（也不等于 key_index 认不出时的 -1）
#define K_MERC (2 + VOICE_ROW)
#define K_FPS (11 + VOICE_ROW)   // eco_fps：0 = 60、1 = 30（省电）、FPS_120 = 120 帧档；旧文件只有 0 / 1，旧包读到 2 当省电 30 | eco_fps: 0 = 60, 1 = 30 (power saving), FPS_120 = 120 fps mode; old files only hold 0 / 1, and old builds read 2 as power saving 30
#define FPS_120 2
#define K_Q3D (12 + VOICE_ROW)
#define K_TOUCH_SWAP (15 + VOICE_ROW)
#define K_UPSCALE (NK - 3)
#define K_RES (NK - 2)
#define K_RT (NK - 1)
#define N_GFX 5   // 图形页的键数：eco_fps / q3d / upscale / render_res / rt_shadow（自定义页的行数 = NK - N_GFX） | Number of Graphics-tab keys (Custom-page rows = NK - N_GFX)
static int graphics_key(int i) { return i == K_FPS || i == K_Q3D || i == K_UPSCALE || i == K_RES || i == K_RT; }
static int custom_key_at(int row) {
    for (int i = 0; i < NK; i++) if (!graphics_key(i) && row-- == 0) return i;
    return 0;
}
void dm_3d_quality_mode_set(int mode);
static const char *rev_box(int i) { return i == K_VOICE ? "TouchCustom_voice_zh" : i == K_TOUCH_SWAP ? "TouchCustom_touch_swap_above_near" : NULL; }   // 单选行「反面那一项」的勾选框 | Checkbox of the radio row's "inverse option"
static _Atomic int g_val[NK];        // 已生效：1 开 / 0 关 | In effect: 1 on / 0 off
static _Atomic int g_pend[NK];       // 选项页上改了还没应用：-1 = 没改 | Changed on the options page but not applied yet: -1 = not changed
static const char *const k_q3d_box[5] = { "TouchCustom_q3d", "TouchCustom_q3d_eco", "TouchCustom_q3d_balanced", "TouchCustom_q3d_quality", "TouchCustom_q3d_off" };
static _Atomic int g_loaded;         // 0 没读盘 / 1 已读 | 0 not read from disk / 1 read
// lang（语言开关，自定义页第一行）：五项单选 0 中文 / 1 English / 2 Español / 3 Русский / 4 한국어（勾选框 TouchCustom_lang_zh / _en / _es / _ru / _ko 互斥）。它不是功能开关，生效方式不同——
// 下次启动挂存档时由 hle_fs.c 按它改写原版设置文件的 Language / VoiceLanguage（游戏运行中自己会把内存里的语言写回设置文件，所以只能在游戏读设置之前改；
// 见 dm_custom_lang_explicit）。文件里没有这个键（还没点过应用；旧版本写的布尔键 lang_en 不再读——原版设置文件里本来就记着上次选的语言）
// = 不动原版设置，勾选框按当前实际语言显示；「默认」不改它（按一下默认把语言切了不合适）
#define LANG_N 5
static const char *const k_lang_box[LANG_N] = { "TouchCustom_lang_zh", "TouchCustom_lang_en", "TouchCustom_lang_es", "TouchCustom_lang_ru", "TouchCustom_lang_ko" };
static const char *const k_lang_hint[LANG_N] = { "TouchCustomLangHintZh", "TouchCustomLangHintEn", "TouchCustomLangHintEs", "TouchCustomLangHintRu", "TouchCustomLangHintKo" };
static const char *const k_lang_name[LANG_N] = { "zh", "en", "es", "ru", "ko" };   // 只进日志 | Log only
static _Atomic int g_lang_explicit = -1;   // 文件里写着的 lang：-1 没写 / 0 中文 / 1 English / 2 Español / 3 Русский / 4 한국어
static int g_lang_test;   // 测试开关 DM_TEST_LANG=<0–4>：当作玩家选了这一项并应用（走 hle_fs.c 挂存档时的真实改写），但不写回 sw_custom.txt（容器各局共用）
static int lang_running(void) { int g = dm_lang_group(); return g == DM_LG_ZH ? 0 : g == DM_LG_ES || g == DM_LG_MX ? 2 : g == DM_LG_RU ? 3 : g == DM_LG_KO ? 4 : 1; }   // 此刻实际在跑的语言落在哪一项 | Which option the currently running language falls into
static _Atomic int g_voice_explicit = -1;   // 文件里写着的 voice：-1 没写 / 0 中文配音 / 1 英文配音
static int g_voice_test;   // 测试开关 DM_TEST_VOICE=0|1；只用于真机回归，不写回文件
#if VOICE_ROW
static int voice_running(void) { return lang_running() == 0 ? 0 : 1; }   // 没单独选过时，按原来的「中文配中文、其余英文」显示
#endif
// merc_pot（佣兵自动喂药）原先缺省关、现在缺省开。旧文件每次存盘都把全部键写一遍，里面的 merc_pot=0 分不出「没动过」还是「自己关的」，
// 所以另记一行 merc_pot_v2：只有在缺省改成开之后玩家自己在页上改过这一项才写。读盘时有 merc_pot_v2 以它为准；没有 = 没在新缺省下动过 → 开
// （旧文件里的 merc_pot=1 也是开，结果一样）。merc_pot 那一行照旧写当前值，旧版本的包读它不出错
// merc_pot (mercenary auto-feed potions) used to be off by default and is now on by default. Old files write all keys on every save, and the merc_pot=0 in them cannot tell "never touched" from "turned off by the player",
// so a separate line merc_pot_v2 is recorded: it is written only when the player has changed this item on the page after the default was changed to on. On load, if merc_pot_v2 exists it is authoritative; absent = never touched under the new default → on
// (merc_pot=1 in an old file is also on, the same result). The merc_pot line is still written with the current value, so packages of older versions read it without error
static _Atomic int g_merc_explicit = -1;   // 文件里写着的 merc_pot_v2：-1 没写 / 0 / 1 | merc_pot_v2 as written in the file: -1 not written / 0 / 1

// 崩溃围栏自动关实验项之前的原值（-1 = 没备份）：存进设置文件一行 exp_off_backup=eco_fps:2,rt_shadow:1,upscale:1；
// 玩家下次打开图形页时按它预选（勾选框恢复成原来的样子），点「应用」生效并清掉备份，「放弃」保留备份、下次打开再预选
// Original values before the crash fence turned experimental features off (-1 = no backup): saved in the settings file as one line exp_off_backup=eco_fps:2,rt_shadow:1,upscale:1;
// the next time the player opens the Graphics tab they are pre-selected (checkboxes back as they were); Apply makes them effective and clears the backup, Discard keeps the backup and pre-selects again next time
static const char *const k_bak_name[3] = { "eco_fps", "rt_shadow", "upscale" };
static _Atomic int g_bak[3] = { -1, -1, -1 };
static _Atomic int g_repaint;        // 默认 / 放弃之后：页上的勾选框按待定值 / 生效值重摆一遍 | After default / discard: lay out the checkboxes on the page again according to pending values / effective values

const char *dm_save_commitdir(void);
static int key_index(const char *k) { if (k) for (int i = 0; i < NK; i++) if (!strcmp(k, k_keys[i])) return i; return -1; }
static char g_dir[1024];   // iOS 外壳启动时给的 Documents（main.m）：启动期（存档挂上之前）就要能读，覆盖层选择用 | Documents given by the iOS shell at startup (main.m): must be readable already during startup (before the save is mounted), used for overlay selection
void dm_custom_init(const char *dir) { if (dir) strlcpy(g_dir, dir, sizeof g_dir); }
// 设置文件所在目录：iOS 是外壳给的 Documents；Mac 是存档已提交目录的上一级，存档还没挂上时按 $DM_SAVE（挂上后已提交目录就是它）——崩溃围栏在宿主入口就要读写这里
// Directory of the settings file: Documents given by the iOS shell; on Mac the parent of the committed save directory, and before the save is mounted $DM_SAVE (which becomes the committed directory once mounted) - the crash fence reads and writes here right at host entry
int dm_custom_dir(char *p, size_t n) {
    if (g_dir[0]) { snprintf(p, n, "%s", g_dir); return 1; }
    const char *c = dm_save_commitdir(); if (!c) c = getenv("DM_SAVE"); if (!c || !*c) return 0;
    snprintf(p, n, "%s/..", c); return 1; }
static int file_path(char *p, size_t n) {
    char d[1024]; if (!dm_custom_dir(d, sizeof d)) return 0;
    snprintf(p, n, "%s/sw_custom.txt", d); return 1; }

static void load_once(void) {
    if (atomic_load(&g_loaded)) return;
    char p[1100]; if (!file_path(p, sizeof p)) return;   // 存档还没挂上：先按缺省开答，挂上后再读 | The save is not mounted yet: answer with the default (on) for now, and read again after it is mounted
    int v[NK]; for (int i = 0; i < NK; i++) v[i] = k_def[i];
    FILE *f = fopen(p, "r"); char ln[128];
    int lang = -1, voice = -1, merc = -1, qv2 = 0;
    for (int b = 0; b < 3; b++) atomic_store(&g_bak[b], -1);   // 备份以文件为准 | the backup follows the file
    while (f && fgets(ln, sizeof ln, f)) { char *eq = strchr(ln, '='); if (!eq) continue; *eq = 0; int i = key_index(ln); if (i > 0) v[i] = eq[1] != '0';
        if (i == K_Q3D) { char *end; long q = strtol(eq + 1, &end, 10); if (end != eq + 1 && (*end == '\n' || *end == '\r' || *end == 0) && q >= 0 && q <= 4) v[i] = (int)q; else v[i] = 0; }
        if (i == K_RES) { char *end; long q = strtol(eq + 1, &end, 10); if (end != eq + 1 && (*end == '\n' || *end == '\r' || *end == 0) && q >= 0 && q < DM_RES_LEVELS) v[i] = (int)q; else v[i] = 0; }
        if (i == K_FPS && eq[1] == '2' && (eq[2] == '\n' || eq[2] == '\r' || eq[2] == 0)) v[i] = FPS_120;   // 其余照旧：0 = 60、非 0 = 30 | Otherwise as before: 0 = 60, non-zero = 30
        if (!strcmp(ln, "q3d_v2")) qv2 = eq[1] == '1';
        if (i == 0) { int l = atoi(eq + 1); if (l >= 0 && l < LANG_N) lang = v[0] = l; }
#if VOICE_ROW
        if (i == K_VOICE) { int x = atoi(eq + 1); if (x == 0 || x == 1) voice = v[K_VOICE] = x; }
#endif
        if (!strcmp(ln, "merc_pot_v2")) merc = eq[1] != '0';
        if (!strcmp(ln, "exp_off_backup")) for (int b = 0; b < 3; b++) { const char *q = strstr(eq + 1, k_bak_name[b]); size_t L = strlen(k_bak_name[b]);
            if (q && q[L] == ':' && q[L + 1] >= '0' && q[L + 1] <= '9') atomic_store(&g_bak[b], q[L + 1] - '0'); } }
    if (f) fclose(f);
    if (!qv2) v[K_Q3D] = v[K_Q3D] == 1 ? 3 : 0;
    if (v[K_Q3D] == 4) { v[K_Q3D] = 0; v[K_UPSCALE] = 0; }
    { const char *tl = getenv("DM_TEST_LANG"); if (tl && *tl >= '0' && *tl < '0' + LANG_N) { lang = v[0] = *tl - '0'; g_lang_test = 1; LOG("自定义设置：测试开关 DM_TEST_LANG=%d（当作语言行选了 %s 并应用，不写回文件）", lang, k_lang_name[lang]); } }
#if VOICE_ROW
    { const char *tv = getenv("DM_TEST_VOICE"); if (tv && *tv >= '0' && *tv <= '1') { voice = v[K_VOICE] = *tv - '0'; g_voice_test = 1; LOG("自定义设置：测试开关 DM_TEST_VOICE=%d（当作配音行选了 %s 并应用，不写回文件）", voice, voice ? "English" : "中文"); } }
#endif
    if (lang < 0) v[0] = lang_running();   // 没写过：按当前实际语言显示 | Never written: display according to the current actual language
#if VOICE_ROW
    if (voice < 0) v[K_VOICE] = voice_running();
    char vlog[32]; snprintf(vlog, sizeof vlog, " voice=%d%s", v[K_VOICE], voice < 0 ? "(auto)" : "");   // (auto) = 文件里没写、按当前语言显示
#else
    const char *vlog = "";
#endif
    atomic_store(&g_lang_explicit, lang);
    atomic_store(&g_voice_explicit, voice);
    v[K_MERC] = merc < 0 ? k_def[K_MERC] : merc; atomic_store(&g_merc_explicit, merc);
    for (int i = 0; i < NK; i++) { atomic_store(&g_val[i], v[i]); atomic_store(&g_pend[i], -1); }
    dm_3d_quality_mode_set(v[K_UPSCALE] && v[K_Q3D] == 3); LOG("3D profile loaded: %d", v[K_UPSCALE] ? v[K_Q3D] : 4);
    // 读盘行按键表写全每个键（分享诊断日志的摘要靠它看出玩家开了什么；原先写死到 eco_fps，后加的 fps_hud / upscale / render_res 等都不在）
    // The load line lists every key from the key table (the diagnostic share summary relies on it; it used to stop at eco_fps, so later keys such as fps_hud / upscale / render_res were missing)
    char kv[512]; size_t o = 0; kv[0] = 0;
    for (int i = 1 + VOICE_ROW; i < NK && o < sizeof kv; i++) o += (size_t)snprintf(kv + o, sizeof kv - o, " %s=%d", k_keys[i], v[i]);
    if (!atomic_exchange(&g_loaded, 1)) LOG("自定义设置：读盘 %s%s lang=%d%s%s%s", p, f ? "" : "（没有文件，全按缺省）", v[0], lang < 0 ? "（没写，按当前语言）" : "", vlog, kv);
}
int dm_custom_get(const char *key) {
    load_once(); int i = key_index(key); if (i < 0) return 1;
    return atomic_load(&g_loaded) ? atomic_load(&g_val[i]) : k_def[i];
}
int dm_3d_profile_get(void) { return dm_custom_get("upscale") ? dm_custom_get("q3d") : 4; }
int dm_custom_lang_explicit(void) { load_once(); return atomic_load(&g_loaded) ? atomic_load(&g_lang_explicit) : -1; }
int dm_custom_voice_explicit(void) { load_once(); return atomic_load(&g_loaded) ? atomic_load(&g_voice_explicit) : -1; }
// 测试开关 DM_TEST_RENDER_RES=<档>[@秒][,<档>@秒…]（功耗基准 power_bench.sh 用）：按宿主初始化起的秒数切档，不读也不写设置文件；切档时同样通知游戏重读。
// 纯函数 res_schedule_level 给单测：返回 elapsed 秒时应生效的档，非法串返回 -1
// Test switch DM_TEST_RENDER_RES=<level>[@sec][,<level>@sec...] (used by the power benchmark power_bench.sh): switches the level by seconds since host init, reads and writes no settings file; a switch notifies the game to re-read just the same.
// Pure function res_schedule_level for unit tests: returns the level in effect at elapsed seconds, -1 for an invalid string
void dm_oe_res_changed(void);   // 弱定义在下面 | weak definition below
static int res_schedule_level(const char *spec, double elapsed) {
    if (!spec || !*spec) return -1;
    int cur = -1; const char *p = spec;
    while (*p) { char *end; long lv = strtol(p, &end, 10); if (end == p || lv < 0 || lv >= DM_RES_LEVELS) return -1;
        double at = 0; p = end; if (*p == '@') { at = strtod(p + 1, &end); if (end == p + 1 || at < 0) return -1; p = end; }
        if (elapsed >= at && cur < 0) cur = (int)lv; else if (elapsed >= at) cur = (int)lv;
        if (*p == ',') p++; else if (*p) return -1; }
    return cur;
}
static _Atomic int g_res_test_last = -1;
static int res_test_level(void) {
    static int on = -1; static const char *spec; static uint64_t t0;
    if (on < 0) { spec = getenv("DM_TEST_RENDER_RES"); on = spec && res_schedule_level(spec, 0) >= 0; t0 = (uint64_t)clock_gettime_nsec_np(CLOCK_MONOTONIC); if (spec && !on) LOG("【红】DM_TEST_RENDER_RES 非法：%s", spec); }
    if (!on) return -1;
    int lv = res_schedule_level(spec, ((uint64_t)clock_gettime_nsec_np(CLOCK_MONOTONIC) - t0) / 1e9); if (lv < 0) lv = 0;
    int prev = atomic_exchange(&g_res_test_last, lv);
    if (prev != lv) { LOG("测试 DM_TEST_RENDER_RES：第 %.0f 秒 → 渲染分辨率档位 %d%s", ((uint64_t)clock_gettime_nsec_np(CLOCK_MONOTONIC) - t0) / 1e9, lv, prev < 0 ? "" : "（通知游戏重读输出尺寸）"); if (prev >= 0) dm_oe_res_changed(); }
    return lv;
}
int dm_render_res_level(void) { int t = res_test_level(); if (t >= 0) return t; int v = dm_custom_get("render_res"); return v >= 0 && v < DM_RES_LEVELS ? v : 0; }   // iOS 前端 front_game_res 每拍读 | Read every tick by the iOS front end's front_game_res
// 档位改了要让游戏重读输出尺寸（hle_oe.c 推性能模式通知 31，与折叠屏换屏同一条路）；弱定义给不链 hle_oe.c 的单测。计数只供单测核对
// A changed level makes the game re-read the output size (hle_oe.c pushes performance-mode notification 31, the same path as the fold-screen switch); the weak definition serves unit tests that do not link hle_oe.c. The counter is only for unit tests
__attribute__((weak)) void dm_oe_res_changed(void) {}
static _Atomic unsigned g_res_notify_n;
unsigned dm_render_res_notify_count(void) { return atomic_load(&g_res_notify_n); }
static void res_notify(void) { atomic_fetch_add(&g_res_notify_n, 1); LOG("图形设置：渲染分辨率档位 → %d（通知游戏重读输出尺寸）", dm_render_res_level()); dm_oe_res_changed(); }
// 测试开关 DM_TEST_FPS=<帧率>[@秒][,<帧率>@秒…]（帧率 30 / 60 / 120）：按宿主初始化起的秒数切帧率档，走与设置页「应用」相同的运行时路径（显示节拍、节拍器、
// ProMotion 表态、3D 比例控制器的周期都每帧读 dm_fps_setting），不读也不写设置文件；120 同样只在屏幕撑得住时生效。真机验 省电 / 60 / 120 切换用
// Test switch DM_TEST_FPS=<fps>[@sec][,<fps>@sec...] (fps 30 / 60 / 120): switches the frame rate by seconds since host init, through the same runtime path as Apply on the settings page (display link, pacer,
// ProMotion declaration and the 3D scale controller period all read dm_fps_setting every frame); reads and writes no settings file; 120 likewise takes effect only on a screen that can do it. For checking 30 / 60 / 120 switching on a device
static int fps_schedule(const char *spec, double elapsed) {
    if (!spec || !*spec) return -1;
    int cur = -1; const char *p = spec;
    while (*p) { char *end; long f = strtol(p, &end, 10); if (end == p || (f != 30 && f != 60 && f != 120)) return -1;
        double at = 0; p = end; if (*p == '@') { at = strtod(p + 1, &end); if (end == p + 1 || at < 0) return -1; p = end; }
        if (elapsed >= at || cur < 0) cur = (int)f;
        if (*p == ',') p++; else if (*p) return -1; }
    return cur;
}
static int fps_test(void) {
    static int on = -1; static const char *spec; static uint64_t t0; static _Atomic int last = -1;
    if (on < 0) { spec = getenv("DM_TEST_FPS"); on = spec && fps_schedule(spec, 0) > 0; t0 = (uint64_t)clock_gettime_nsec_np(CLOCK_MONOTONIC); if (spec && !on) LOG("【红】DM_TEST_FPS 非法：%s", spec); }
    if (!on) return -1;
    double el = ((uint64_t)clock_gettime_nsec_np(CLOCK_MONOTONIC) - t0) / 1e9; int f = fps_schedule(spec, el);
    if (atomic_exchange(&last, f) != f) LOG("测试 DM_TEST_FPS：第 %.0f 秒 → %d 帧档", el, f);
    return f;
}
// 120 帧档只在屏幕最高刷新率 ≥ 120 时开放（ProMotion iPhone / iPad、高刷 Mac 屏）：设置页滑块才有第三格，存着 120 的设置换到 60 Hz 屏上按 60 走。
// 屏幕刷新率由图形层给（nvn_mtl.m 的强定义，有窗口时读 UIScreen / CoreGraphics；未建窗口 0）；单测 / 不链图形层时 0 = 不开放
// The 120 fps mode is offered only when the screen's maximum refresh rate is >= 120 (ProMotion iPhone / iPad, high-refresh Mac displays): only then does the settings slider get a third stop, and a saved 120 setting runs at 60 on a 60 Hz screen.
// The refresh rate comes from the graphics layer (strong definition in nvn_mtl.m, reading UIScreen / CoreGraphics once there is a window; 0 before the window exists); 0 = not offered in unit tests / without the graphics layer
#ifndef DM_DISPLAY_HZ_TEST_HOOK   // 单测直接包含本文件并自带强定义时打开（t_render_profile.c） | defined by unit tests that include this file and bring their own strong definition (t_render_profile.c)
__attribute__((weak)) int dm_display_max_hz(void) { return 0; }
#else
int dm_display_max_hz(void);
#endif
static int fps120_ok(void) { return dm_display_max_hz() >= 119; }   // 119：容 119.88 这类档 | 119: tolerates rates like 119.88
// 图形层 nvn_mtl.m mtl_fps_target 每帧读（弱符号的强定义）：eco_fps 0 = 60 / 1 = 30（省电）/ 2 = 120 | Read every frame by mtl_fps_target in the graphics layer nvn_mtl.m (strong definition of a weak symbol): eco_fps 0 = 60 / 1 = 30 (power saving) / 2 = 120
// 生效值或原因一变就记一行「帧率档：」（分享诊断日志的摘要按这个前缀取最后一行，dm_sharelog.m perf_block）。屏幕刷新率要等有窗口才读得到，所以「按 60」在建窗前后各可能记一次
// One "帧率档：" (frame rate mode) line whenever the effective value or its reason changes (the share-diagnostics summary takes the last line with this prefix, dm_sharelog.m perf_block). The screen refresh rate is readable only once there is a window, so "runs at 60" may be logged once before and once after
int dm_fps_setting(void) {
    int t = fps_test(), v = t > 0 ? (t == 30 ? 1 : t == 120 ? FPS_120 : 0) : dm_custom_get("eco_fps"), hz = dm_display_max_hz();
    int f = v == 1 ? 30 : v == FPS_120 && fps120_ok() ? 120 : 60, why = (v == FPS_120 && f == 60) * 2 + (t > 0);
    static _Atomic int last = -1; int key = f * 16 + why * 4 + (hz > 0);
    if (atomic_exchange(&last, key) != key)
        LOG("帧率档：生效 %d 帧（%s%s，%s%d Hz）%s", f, t > 0 ? "测试开关 DM_TEST_FPS " : "设置 ", v == 1 ? "30" : v == FPS_120 ? "120" : "60", hz > 0 ? "屏幕最高 " : "还没窗口、刷新率 ", hz, why >= 2 && hz > 0 ? "；屏幕不到 120 Hz，120 档按 60" : why >= 2 ? "；暂按 60" : "");
    return f;
}
// 光追阴影（键 rt_shadow，实验性，缺省关，图形页）：图形层 nvn_rtshadow.inc rts_on 每帧读；机器不过门槛（mtl_rt_shadow_supported）时按关处理，图形页也不显示这一行
// Ray-traced shadows (key rt_shadow, experimental, off by default, Graphics tab): read every frame by rts_on in nvn_rtshadow.inc; treated as off on machines that fail the gate (mtl_rt_shadow_supported), and the Graphics tab hides the row
__attribute__((weak)) int mtl_rt_shadow_supported(void) { return 0; }   // 弱定义 = 不支持；图形层 nvn_rtshadow.inc 的强定义覆盖它（只有弱引用时，不链图形层的单测在 macOS 上链接不过） | weak definition = unsupported; the strong definition in nvn_rtshadow.inc overrides it (with only a weak reference, unit tests that do not link the graphics layer fail to link on macOS)
static int rt_shadow_ok(void) { return mtl_rt_shadow_supported(); }
// 图形层 nvn_rtshadow.inc rts_on 每帧读（弱符号的强定义）。生效值、设置或机器判定一变就记一行「光追阴影：生效」（分享诊断日志的摘要按这个前缀取最后一行）
// Read every frame by rts_on in nvn_rtshadow.inc (strong definition of a weak symbol). One "光追阴影：生效" (ray-traced shadows: effective) line whenever the effective value, the setting or the machine gate changes (the share-diagnostics summary takes the last line with this prefix)
int dm_rt_shadow_setting(void) {
    int ok = rt_shadow_ok(), set = dm_custom_get("rt_shadow") != 0, on = ok && set;
    static _Atomic int last = -1; int key = on * 4 + set * 2 + ok;
    if (atomic_exchange(&last, key) != key) LOG("光追阴影：生效 %s（设置 %s，机器%s）", on ? "开" : "关", set ? "开" : "关", ok ? "可用" : "不可用");
    return on;
}
static void save(void) {
    char p[1100], t[1120]; if (!file_path(p, sizeof p)) { LOG("【红】自定义设置：存档目录没挂上，没存盘"); return; }
    snprintf(t, sizeof t, "%s.tmp", p); FILE *f = fopen(t, "w"); int ok = f != NULL;
    for (int i = 0; ok && i < NK; i++) { if (i == 0 && (atomic_load(&g_lang_explicit) < 0 || g_lang_test)) continue; if (i == K_VOICE && (atomic_load(&g_voice_explicit) < 0 || g_voice_test)) continue; ok = fprintf(f, "%s=%d\n", k_keys[i], atomic_load(&g_val[i])) > 0; }   // lang / voice 只在玩家真改过之后才写进文件
    if (ok) ok = fprintf(f, "q3d_v2=1\n") > 0;
    if (ok && atomic_load(&g_merc_explicit) >= 0) ok = fprintf(f, "merc_pot_v2=%d\n", atomic_load(&g_merc_explicit)) > 0;
    { char bk[96]; size_t o = 0; bk[0] = 0; for (int b = 0; b < 3; b++) if (atomic_load(&g_bak[b]) >= 0) o += (size_t)snprintf(bk + o, sizeof bk - o, "%s%s:%d", o ? "," : "", k_bak_name[b], atomic_load(&g_bak[b]));
      if (ok && o) ok = fprintf(f, "exp_off_backup=%s\n", bk) > 0; }
    if (f) ok = fclose(f) == 0 && ok;
    if (ok) ok = rename(t, p) == 0;   // 先写临时文件再换名：写到一半被杀也不会留下半个文件 | Write a temporary file first and then rename: being killed halfway through the write does not leave half a file behind
    LOG("自定义设置：存盘%s", ok ? "" : "【红】失败");
}
// 崩溃围栏连续两次异常退出时调（dm_crashfence.c）：实验性功能全关并写回设置文件——120 帧档回 60、光追阴影关、MetalFX 空间超分关（光追 AO 只在光追里跑，随它关）。
// 玩家没开的项不动；改了才存盘。what 写「改了什么」给日志；返回改了几项
// Called by the crash fence after two abnormal exits in a row (dm_crashfence.c): turn every experimental feature off and save - 120 fps mode back to 60, ray-traced shadows off, MetalFX spatial upscaling off (ray-traced AO only runs inside ray tracing and goes off with it).
// Items the player did not turn on are left alone; the file is saved only if something changed. what receives "what changed" for the log; returns how many items changed
int dm_custom_safe_mode(char *what, size_t n) {
    load_once(); size_t o = 0; int ch = 0; what[0] = 0;
    if (!atomic_load(&g_loaded)) { snprintf(what, n, "设置文件还没法读，没改"); return 0; }
    static const int bk_key[3] = { K_FPS, K_RT, K_UPSCALE };   // 改之前记原值（已有备份不覆盖：连着两轮自动关时，第二轮看到的已经是关） | record the original before changing (an existing backup is kept: on a second automatic pass the value is already off)
    for (int b = 0; b < 3; b++) { int v = atomic_load(&g_val[bk_key[b]]), on = b == 0 ? v == FPS_120 : v != 0; if (on && atomic_load(&g_bak[b]) < 0) atomic_store(&g_bak[b], v); }
    if (atomic_load(&g_val[K_FPS]) == FPS_120) { atomic_store(&g_val[K_FPS], 0); ch++; o += (size_t)snprintf(what + o, o < n ? n - o : 0, "%s120 帧档 → 60", o ? "、" : ""); }
    if (atomic_load(&g_val[K_RT])) { atomic_store(&g_val[K_RT], 0); ch++; o += (size_t)snprintf(what + o, o < n ? n - o : 0, "%s光追阴影 → 关（AO 随它关）", o ? "、" : ""); }
    if (atomic_load(&g_val[K_UPSCALE])) { atomic_store(&g_val[K_UPSCALE], 0); ch++; o += (size_t)snprintf(what + o, o < n ? n - o : 0, "%sMetalFX 空间超分 → 关", o ? "、" : ""); dm_3d_quality_mode_set(0); }
    for (int i = 0; i < NK; i++) atomic_store(&g_pend[i], -1);
    if (!ch) snprintf(what, n, "实验性功能本来都关着");
    else { save(); atomic_fetch_add(&g_repaint, 1); }
    return ch;
}
// 图形页刚打开（dm_graphics_ui_pump）：有崩溃围栏的备份就按原值预选，返回预选了几项 | The Graphics tab just opened (dm_graphics_ui_pump): with a crash-fence backup, pre-select the original values; returns how many were pre-selected
static int exp_restore_pending(void) {
    static const int bk_key[3] = { K_FPS, K_RT, K_UPSCALE }; int n = 0;
    for (int b = 0; b < 3; b++) { int v = atomic_load(&g_bak[b]); if (v >= 0) { atomic_store(&g_pend[bk_key[b]], v); n++; } }
    if (n) { atomic_fetch_add(&g_repaint, 1); LOG("自定义设置：图形页打开，按崩溃围栏自动关闭前的值预选 %d 项（点应用生效，放弃则保持关闭）", n); }
    return n;
}
void dm_custom_on_uimsg(const char *cat, const char *act, const char *arg) {
    (void)arg; if (!cat || !act || strcmp(cat, "OptionsPanelMessage")) return;
    load_once();
    if (!strcmp(act, "AcceptChanges")) { int ch = 0, res_changed = 0;
        // Migrate the former fifth radio option to the independent enable switch.
        if (atomic_load(&g_pend[K_Q3D]) == 4) { atomic_store(&g_pend[K_Q3D], 0); atomic_store(&g_pend[K_UPSCALE], 0); }
        for (int i = 0; i < NK; i++) { int pd = atomic_exchange(&g_pend[i], -1); if (pd >= 0 && pd != atomic_load(&g_val[i])) { atomic_store(&g_val[i], pd); ch = 1; if (i == K_RES) res_changed = 1; if (i == K_Q3D) { int dm_3d_quality_mode_get(void); float dm_3d_scale_cur(void); dm_3d_quality_mode_set(pd == 3); LOG("自定义设置：3D 画质 → %s（已交给 3D 比例控制，读回模式 %d，此刻 3D 比例 %.2f）", pd == 3 ? "Quality" : "Adaptive", dm_3d_quality_mode_get(), dm_3d_scale_cur()); } if (i == 0) atomic_store(&g_lang_explicit, pd); if (i == K_VOICE) atomic_store(&g_voice_explicit, pd); if (i == K_MERC) atomic_store(&g_merc_explicit, pd); if (i == 0) LOG("自定义设置：语言 → %s（应用；下次启动生效）", k_lang_name[pd]); else if (i == K_VOICE) LOG("自定义设置：配音 → %s（应用；下次启动生效）", pd ? "English" : "中文"); else LOG("自定义设置：%s → %s（应用）", k_keys[i], pd ? "开" : "关"); } }
        dm_3d_quality_mode_set(atomic_load(&g_val[K_UPSCALE]) && atomic_load(&g_val[K_Q3D]) == 3);
        { int had = 0; for (int b = 0; b < 3; b++) had |= atomic_exchange(&g_bak[b], -1) >= 0;   // 应用 = 玩家已经决定：崩溃围栏的备份用完即清 | Apply = the player has decided: the crash-fence backup is cleared
          if (had) { ch = 1; LOG("自定义设置：应用，清掉崩溃围栏的实验项备份"); } }
        if (res_changed) res_notify();
        atomic_fetch_add(&g_repaint, 1);
        if (ch) save(); }
    else if (!strcmp(act, "CancelChanges")) { for (int i = 0; i < NK; i++) atomic_store(&g_pend[i], -1); atomic_fetch_add(&g_repaint, 1); }
    else if (!strcmp(act, "ResetToDefaults")) {
        // Preserve upstream global defaults across the two enhanced tabs. Language/voice stay unchanged.
        for (int i = 1 + VOICE_ROW; i < NK; i++) atomic_store(&g_pend[i], k_def[i]);
        atomic_fetch_add(&g_repaint, 1); }

}

// ---- 选项页上的勾选框
// ---- Checkboxes on the options page
static uint8_t *main_base(void) { static uint8_t *b; if (!b) for (int i = 0; i < g_nmod; i++) if (!strcmp(g_mod[i].name, "main")) b = (uint8_t *)g_mod[i].base; return b; }
static uint64_t find_w(uint64_t w, const char *name, int dep) {   // 按名字找控件：名字 +8、子控件数组 +0x58、个数 +0x60 | find a widget by name: name +8, child widget array +0x58, count +0x60
    if (!w || dep > 14) return 0; const char *nm = (const char *)*(uint64_t *)(w + 8); if (nm && !strcmp(nm, name)) return w;
    uint64_t ch = *(uint64_t *)(w + 0x58), n = *(uint64_t *)(w + 0x60); if (!ch || n > 256) return 0;
    for (uint64_t i = 0; i < n; i++) { uint64_t r = find_w(*(uint64_t *)(ch + 8 * i), name, dep + 1); if (r) return r; }
    return 0;
}
// 勾选框的状态（本机逐字节差分，点一下只变这几处）：+0xa88 是它自己的「勾上」标志（点击按它翻转）；
// +0x6e8..+0x6f4 是「常态 / 按下 / 悬停 / 禁用」四个帧号，勾上 = 4 5 6 -1、没勾 = 0 1 3 2（= 布局里 toggled* / untoggled* 那几项）；+0x610 是此刻画的帧
// 读写这几处之前核控件是勾选框那个类（dm_widget.h）：不是就当没勾、不写，并记红
// Checkbox state (byte-by-byte diff on this machine; one tap changes only these places): +0xa88 is its own "checked" flag (a click toggles it);
// +0x6e8..+0x6f4 are the four frame ids "normal / pressed / hover / disabled", checked = 4 5 6 -1, unchecked = 0 1 3 2 (= the toggled* / untoggled* entries in the layout); +0x610 is the frame drawn right now
// Before reading or writing these, check the widget is of the checkbox class (dm_widget.h): if not, treat it as unchecked, do not write, and log a failure
DM_WIDGET_RED_DEF
#define TOGGLED(w) (dm_widget_is(main_base(), (w), DM_VT_TOGGLE) && *(uint8_t *)((w) + 0xa88) != 0)
static void set_toggle(uint64_t w, int on) {
    if (!DM_WIDGET_OK(main_base(), w, DM_VT_TOGGLE, "自定义页")) return;
    static const int32_t fr[2][4] = { { 0, 1, 3, 2 }, { 4, 5, 6, -1 } };
    *(uint8_t *)(w + 0xa88) = on != 0; memcpy((void *)(w + 0x6e8), fr[on != 0], sizeof fr[0]); *(int32_t *)(w + 0x610) = fr[on != 0][0];
}
static int pending_value(int i) { int p = atomic_load(&g_pend[i]); return p >= 0 ? p : atomic_load(&g_val[i]); }
static int enhanced_settings_dirty(void) {
    for (int i=0;i<NK;i++) if (pending_value(i)!=atomic_load(&g_val[i])) return 1;
    return 0;
}
// 帧率滑块的格：0 = 30、1 = 60、2 = 120（屏幕撑得住 120 才有第三格）；格 ↔ eco_fps 值。存着 120、屏幕只有 60 时停在 60 那格，不当成改动
// Frame-rate slider stops: 0 = 30, 1 = 60, 2 = 120 (the third stop exists only when the screen can do 120); stop <-> eco_fps value. A saved 120 on a 60-only screen sits on the 60 stop and does not count as a change
static int fps_max_pos(void) { return fps120_ok() ? 2 : 1; }
static int fps_pos(int v) { return v == 1 ? 0 : v == FPS_120 && fps120_ok() ? 2 : 1; }
static int fps_val(int pos) { return pos == 0 ? 1 : pos == 2 ? FPS_120 : 0; }
static void graphics_select_fps(int pos) {
    if (pos < 0 || pos > fps_max_pos() || pos == fps_pos(pending_value(K_FPS))) return;
    atomic_store(&g_pend[K_FPS], fps_val(pos));
    LOG("图形设置：帧率 → %d FPS（点应用生效）", pos == 0 ? 30 : pos == 2 ? 120 : 60);
}
static void graphics_select_profile(int value) {
    if (value < 0 || value > 3 || value == pending_value(K_Q3D)) return;
    atomic_store(&g_pend[K_Q3D], value);
    LOG("图形设置：MetalFX 档位 → %d（点应用生效）", value);
}
static void graphics_select_res(int value) {
    if (value < 0 || value >= DM_RES_LEVELS || value == pending_value(K_RES)) return;
    atomic_store(&g_pend[K_RES], value);
    LOG("图形设置：渲染分辨率档位 → %d（点应用生效）", value);
}
static void graphics_text(uint64_t w, const char *s) {
    if (!w || !s || !DM_WIDGET_OK(main_base(), w, DM_VT_TEXT, "图形设置文字")) return;
    size_t n = strlen(s); const char *old = *(const char **)(w + 0x88);
    if (*(uint64_t *)(w + 0x90) == n && old && !memcmp(old, s, n)) return;
    ((void *(*)(uint64_t, const char *, uint64_t))(main_base() + 0x136e5c))(w + 0x88, s, n);
    ((void (*)(uint64_t))(main_base() + 0xadc434))(w);
}
static void ui_show_ours(uint64_t w, int on) {
    const char *name = w ? *(const char **)(w + 8) : NULL;
    if (!name || strncmp(name, "Touch", 5)) { if (w) LOG("【红】图形设置：拒绝修改原版控件可见性"); return; }
    *(uint8_t *)(w + 0x50) = on != 0; *(uint8_t *)(w + 0x51) = on != 0;
}
// 只挪自己加的控件（名字以 Touch 开头）的 y：矩形 y 在 +0x74，改完照原版补一次虚表 +0x10 让它重算（同下面的选中框、dm_update.c place）
// Moves only our own widgets (names starting with Touch) vertically: rectangle y at +0x74; after the change the vtable +0x10 is called once as the original does, so it recomputes (same as the selection frame below and place in dm_update.c)
static void move_ours_y(uint64_t w, int y) {
    const char *name = w ? *(const char **)(w + 8) : NULL;
    if (!name || strncmp(name, "Touch", 5)) { if (w) LOG("【红】图形设置：拒绝挪动原版控件"); return; }
    int32_t *r = (int32_t *)(w + 0x70); if (r[1] == y) return;
    r[1] = y; ((void (*)(uint64_t))*(uint64_t *)(*(uint64_t *)w + 0x10))(w);
}
void dm_graphics_ui_pump(unsigned long long root) {
    if (!root || !main_base()) return;
    static dm_wref seen[5]; static int paint;
    uint64_t sp = dm_ui_top(root, "3PlusGraphicsOptionsPanel");
    static int was_open;
    if (!sp || !*(uint8_t *)(sp + 0x50)) { memset(seen, 0, sizeof seen); was_open = 0; return; }
    load_once();
    if (!was_open) { was_open = 1; exp_restore_pending(); }   // 刚打开：崩溃围栏关过实验项就按原值预选 | just opened: if the crash fence turned experimental features off, pre-select the originals
    int repaint = atomic_load(&g_repaint);
    if (paint != repaint) { paint = repaint; memset(seen, 0, sizeof seen); }
    uint64_t fps = find_w(sp, "TouchGraphicsFps", 0);
    int fps_ok = fps && DM_WIDGET_OK(main_base(), fps, DM_VT_SLIDER, "图形设置帧率");
    if (fps_ok) {
        if (dm_wref_get(&seen[0]) != fps) {
            *(int32_t *)(fps + 0x5b0) = 1; *(int32_t *)(fps + 0x5b8) = fps_max_pos();
            *(int32_t *)(fps + 0x5bc) = 0; *(int32_t *)(fps + 0x5c0) = fps_max_pos();
            ((void (*)(uint64_t, int, int, int))(main_base() + 0xad7028))(fps, fps_pos(pending_value(K_FPS)), 0, 0);
            dm_wref_set(&seen[0], fps);
        } else graphics_select_fps(*(int32_t *)(fps + 0x5c4));
    }
    uint64_t upscale = find_w(sp, "TouchGraphicsUpscale", 0);
    if (upscale) {
        int want=pending_value(K_UPSCALE);
        if (dm_wref_get(&seen[1])!=upscale) { dm_wref_set(&seen[1],upscale); set_toggle(upscale,want); }
        else if (TOGGLED(upscale)!=want) atomic_store(&g_pend[K_UPSCALE],TOGGLED(upscale));
    }
    // 光追阴影（实验性）：机器不过门槛时整行藏起来；勾选框与超分开关同一套处理 | Ray-traced shadows (experimental): the whole row is hidden on machines that fail the gate; the checkbox is handled like the upscale switch
    int rt_ok = rt_shadow_ok(); uint64_t rtrow = find_w(sp, "TouchGraphicsRtRow", 0); if (rtrow) ui_show_ours(rtrow, rt_ok);
    uint64_t rt = rt_ok ? find_w(sp, "TouchGraphicsRtShadow", 0) : 0;
    if (rt) {
        int want = pending_value(K_RT);
        if (dm_wref_get(&seen[4]) != rt) { dm_wref_set(&seen[4], rt); set_toggle(rt, want); }
        else if (TOGGLED(rt) != want) atomic_store(&g_pend[K_RT], TOGGLED(rt));
    }
    int dm_padmode_current(void); uint64_t dm_padmode_page_keys(uint64_t);
    static int selected; static dm_wref focus_panel;
    if (dm_wref_get(&focus_panel) != sp) { dm_wref_set(&focus_panel, sp); selected = 0; }
    int pad = dm_padmode_current() && !dm_ui_modal_active(root);
    uint64_t ev = pad ? dm_padmode_page_keys(DM_BTN_UP | DM_BTN_DOWN | DM_BTN_A | DM_BTN_LEFT | DM_BTN_RIGHT) : 0;
    // 行（与 touch_layout.py _graphics_page 同序，用户 2026-10-27）：0 渲染分辨率 / 1 帧率 / 2 超分开关 / 3 超分档位（开关开着才有）/ 4 光追阴影（实验性，机器过门槛才有）。
    // 可见行紧挨着排：档位行藏起来时光追行上移一格补位（下面按可见行数挪光追行与说明行），所以选中框按可见序号 selected 放，行为按布局行号 row_id 走
    // Rows (same order as touch_layout.py _graphics_page, user 2026-10-27): 0 render resolution / 1 frame rate / 2 upscale switch / 3 upscale profile (only while the switch is on) / 4 ray-traced shadows (experimental, only on machines that pass the gate).
    // Visible rows are packed: with the profile row hidden, the ray-tracing row moves up one slot (the ray-tracing row and the hint are moved below by the visible row count), so the selection frame follows the visible index selected and behaviour follows the layout row row_id
    enum { ROW_RES, ROW_FPS, ROW_UPSCALE, ROW_PROFILE, ROW_RT };
    int prof_on = pending_value(K_UPSCALE) != 0, rows = 3 + prof_on + (rt != 0);
    if (ev & DM_BTN_DOWN) selected++; else if (ev & DM_BTN_UP) selected--;
    if (selected < 0) selected = 0; if (selected >= rows) selected = rows - 1;
    int row_id = selected == 3 && !prof_on ? ROW_RT : selected;
    if ((ev & DM_BTN_A) && row_id == ROW_RT && rt) { int to = !pending_value(K_RT); atomic_store(&g_pend[K_RT], to); set_toggle(rt, to); }
    if (row_id == ROW_FPS && fps_ok && (ev & (DM_BTN_LEFT | DM_BTN_RIGHT))) {
        int to = fps_pos(pending_value(K_FPS)) + ((ev & DM_BTN_RIGHT) ? 1 : -1);
        if (to>=0 && to<=fps_max_pos()) { graphics_select_fps(to);
            ((void (*)(uint64_t, int, int, int))(main_base() + 0xad7028))(fps,to,0,0); }
    }
    if ((ev & DM_BTN_A) && row_id == ROW_UPSCALE) {
        int to=!pending_value(K_UPSCALE); atomic_store(&g_pend[K_UPSCALE],to);
        if (upscale) set_toggle(upscale,to);
    }
    int enabled = pending_value(K_UPSCALE);
    uint64_t row = find_w(sp, "TouchGraphicsProfileRow", 0);
    if (row) ui_show_ours(row, enabled);
    // 光追行接在最后一个可见行下面（档位行藏着时上移一格），说明行再往下一格 | the ray-tracing row follows the last visible row (one slot up while the profile row is hidden), the hint one slot below that
    if (rtrow) move_ours_y(rtrow, enabled ? 900 : 720);
    move_ours_y(find_w(sp, "TouchGraphicsApplyHint", 0), 180 + 180 * (3 + (enabled != 0) + (rt_ok != 0)));
    uint64_t slider = find_w(sp, "TouchGraphicsProfile", 0);
    if (slider && DM_WIDGET_OK(main_base(), slider, DM_VT_SLIDER, "图形设置滑块")) {
        // 3.3.94012: render main+aef3cc reads min/max/current here; main+ad7028
        // clamps and moves the indicator. Native onUpdateMessage has no numeric payload.
        if (dm_wref_get(&seen[2]) != slider) {
            *(int32_t *)(slider + 0x5b0) = 1;
            *(int32_t *)(slider + 0x5b8) = 3;
            *(int32_t *)(slider + 0x5bc) = 0;
            *(int32_t *)(slider + 0x5c0) = 3;
            ((void (*)(uint64_t, int, int, int))(main_base() + 0xad7028))(slider, pending_value(K_Q3D), 0, 0);
            dm_wref_set(&seen[2], slider);
        } else if (enabled) graphics_select_profile(*(int32_t *)(slider + 0x5c4));
        if (enabled && row_id == ROW_PROFILE && (ev & (DM_BTN_LEFT | DM_BTN_RIGHT))) {
            int to = pending_value(K_Q3D) + ((ev & DM_BTN_RIGHT) ? 1 : -1);
            if (to >= 0 && to < 4) { graphics_select_profile(to);
                ((void (*)(uint64_t, int, int, int))(main_base() + 0xad7028))(slider, to, 0, 0); }
        }
    }
    uint64_t res = find_w(sp, "TouchGraphicsRes", 0);
    if (res && DM_WIDGET_OK(main_base(), res, DM_VT_SLIDER, "图形设置分辨率滑块")) {
        if (dm_wref_get(&seen[3]) != res) {
            *(int32_t *)(res + 0x5b0) = 1; *(int32_t *)(res + 0x5b8) = DM_RES_LEVELS - 1;
            *(int32_t *)(res + 0x5bc) = 0; *(int32_t *)(res + 0x5c0) = DM_RES_LEVELS - 1;
            ((void (*)(uint64_t, int, int, int))(main_base() + 0xad7028))(res, pending_value(K_RES), 0, 0);
            dm_wref_set(&seen[3], res);
        } else graphics_select_res(*(int32_t *)(res + 0x5c4));
        if (row_id == ROW_RES && (ev & (DM_BTN_LEFT | DM_BTN_RIGHT))) {
            int to = pending_value(K_RES) + ((ev & DM_BTN_RIGHT) ? 1 : -1);
            if (to >= 0 && to < DM_RES_LEVELS) { graphics_select_res(to);
                ((void (*)(uint64_t, int, int, int))(main_base() + 0xad7028))(res, to, 0, 0); }
        }
    }
    { static const char *const res_captions[DM_RES_LEVELS] = { "dmGraphicsResAuto", "dmGraphicsResEco", "dmGraphicsResBalanced", "dmGraphicsResNative" };
      int lv = pending_value(K_RES); if (lv < 0 || lv >= DM_RES_LEVELS) lv = 0;
      graphics_text(find_w(sp, "TouchGraphicsResText", 0), dm_str(main_base(), res_captions[lv])); }
    { static const char *const fps_captions[3] = { "dmGraphicsFrameRate30", "dmGraphicsFrameRate60", "dmGraphicsFrameRate120" };
      graphics_text(find_w(sp, "TouchGraphicsFpsText", 0), dm_str(main_base(), fps_captions[fps_pos(pending_value(K_FPS))])); }
    uint64_t focus = find_w(sp, "TouchGraphicsSel", 0);
    if (focus) { int32_t *r = (int32_t *)(focus + 0x70), x = pad ? -20 : 99999, y = pad ? 170 + 180 * selected : 0;
        if (r[0] != x || r[1] != y) { r[0] = x; r[1] = y; ((void (*)(uint64_t))*(uint64_t *)(*(uint64_t *)focus + 0x10))(focus); } }
    static const char *const captions[] = { "dmGraphicsProfileAuto", "dmGraphicsProfileEco", "dmGraphicsProfileBalanced", "dmGraphicsProfileQuality" };
    int profile = pending_value(K_Q3D); if (profile < 0 || profile > 3) profile = 0;
    graphics_text(find_w(sp, "TouchGraphicsProfileText", 0), dm_str(main_base(), captions[profile]));
    uint64_t vt = *(uint64_t *)sp; int valid = 0;
    for (int k = 0; k < 96 && !valid; k++) valid = *(uint64_t *)(vt + 8 * k) == (uint64_t)main_base() + 0xae5f28;
    if (valid) { int dirty = enhanced_settings_dirty();
        if (*(uint8_t *)(sp + 0x1a0) != dirty) { *(uint8_t *)(sp + 0x1a0) = dirty; LOG("图形设置：有未应用改动 → %d", dirty); } }
    else { static int warned; if (!warned++) LOG("【红】图形设置：原生选项页类型校验失败，不写改动标记"); }
}
void dm_custom_ui_pump(unsigned long long root) {
    if (!root || !main_base()) return;
    static dm_wref seen[NK];   // 每行最近见到的勾选框（带界面代号：重建后取到 0 = 页刚出现） | Most recently seen checkbox of each row (with UI generation: 0 after a rebuild = page just appeared)
    uint64_t sp = dm_ui_top(root, "VideoOptionsPanel"); if (!sp || !*(uint8_t *)(sp + 0x50)) {   // 「自定义」页 = 借来的视频页（界面根的直接子，与设置页并列）。页不在屏上：没点「应用」的勾选作废（同原版不应用就离开），下次出现重摆 | The 「自定义」(Custom) page = the borrowed video page (a direct child of the UI root, alongside the settings page). Page not on screen: checkbox changes without tapping 「应用」(Apply) are discarded (same as leaving without applying in the original game), and it is laid out again the next time it appears
        uint64_t settings = dm_ui_top(root, "SettingsPanel");
        if (!settings || !*(uint8_t *)(settings + 0x50)) { for (int i = 0; i < NK; i++) atomic_store(&g_pend[i], -1); }
        memset(seen, 0, sizeof seen); return; }
    load_once(); static int paint; int repaint = atomic_load(&g_repaint); if (paint != repaint) { paint = repaint; memset(seen, 0, sizeof seen); }
    if (atomic_load(&g_lang_explicit) < 0) atomic_store(&g_val[0], lang_running());   // 语言那一格没点过应用：生效值 = 此刻实际在跑的语言（读盘时游戏还没设语言，那时取到的不准） | Apply never tapped for the language cell: effective value = the language actually running right now (when read from disk the game has not set the language yet, so the value fetched then is not accurate)
#if VOICE_ROW
    if (atomic_load(&g_voice_explicit) < 0) atomic_store(&g_val[K_VOICE], voice_running());
#endif
    // 手柄模式：原版选项页的焦点只认表格里它自己的行，够不着我们的勾选框——十字键上下选行（原版手柄选中框 TouchCustomSel 框住这一行）、A 翻勾选（下面照「玩家点了」处理），其余键照旧归原版
    // Controller mode: the original options page's focus only recognizes its own rows in the table and cannot reach our checkboxes. D-pad up/down selects a row
    // (the original controller selection frame TouchCustomSel frames that row), A toggles the checkbox (handled below as "the player tapped it"), and all other
    // buttons go to the original as before
    { int dm_padmode_current(void); uint64_t dm_padmode_page_keys(uint64_t); static int sel; static dm_wref sel_sp; int pad = dm_padmode_current() && !dm_ui_modal_active(root) && !dm_community_shown();   // 社区提示框显示着时 A / B 归它（dm_community.c） | while the community box shows, A / B belong to it (dm_community.c)
      if (dm_wref_get(&sel_sp) != sp) { dm_wref_set(&sel_sp, sp); sel = 0; }
      uint64_t dm = find_w(sp, "TouchCustomSel", 0), tw = 0, cw = 0; char nm[64];
      int rows = NK - N_GFX + (dm_community_available() ? 1 : 0); if (sel >= rows) sel = rows - 1;   // 末尾「关于」组的社区行（dm_community.c；布局 touch_layout.py _custom_about）：有可显示的值时多一行可选，A = 打开提示框 | the About group's community row at the end (dm_community.c; layout _custom_about): one more selectable row when there is a value, A = open the box
      if (pad) { uint64_t ev = dm_padmode_page_keys(DM_BTN_UP | DM_BTN_DOWN | DM_BTN_A);
          if (ev & DM_BTN_DOWN) sel = sel + 1 < rows ? sel + 1 : sel; else if (ev & DM_BTN_UP) sel = sel > 0 ? sel - 1 : 0;
          int ci = sel < NK - N_GFX ? custom_key_at(sel) : -1;
          if (ci < 0) { tw = find_w(sp, "TouchAboutCommunityText", 0); cw = find_w(sp, "TouchCommunityOpt", 0); if ((ev & DM_BTN_A) && cw && find_w(sp, "TouchAboutRow", 0)) dm_community_open(3); }
          else {
          snprintf(nm, sizeof nm, "TouchCustom_%s", k_keys[ci]); cw = find_w(sp, ci == 0 ? k_lang_box[LANG_N - 1] : nm, 0); snprintf(nm, sizeof nm, "TouchCustom_%s_Text", k_keys[ci]); tw = find_w(sp, nm, 0);   // 语言行：框住标签到最右那一格 | Language row: box from the label to the rightmost cell
          if ((ev & DM_BTN_A) && cw && dm_wref_get(&seen[ci]) == cw) {
              if (ci == K_Q3D) { int pd = atomic_load(&g_pend[ci]), shown = pd >= 0 ? pd : atomic_load(&g_val[ci]); int to = (shown + 1) % 5; atomic_store(&g_pend[ci], to); for (int k = 0; k < 5; k++) { uint64_t z = find_w(sp,k_q3d_box[k],0); if (z) set_toggle(z,k == to); } }
              else if (ci == K_VOICE || ci == K_TOUCH_SWAP) { int p0 = atomic_load(&g_pend[ci]), sh = p0 >= 0 ? p0 : atomic_load(&g_val[ci]); atomic_store(&g_pend[ci], !sh); set_toggle(cw, !sh); { uint64_t z = find_w(sp, rev_box(ci), 0); if (z) set_toggle(z, sh); }
                  LOG("自定义设置：%s → %d（手柄 A 在两项间切换；点「应用」生效）",k_keys[ci],!sh); }
              else if (ci == 0) { int p0 = atomic_load(&g_pend[0]), sh = p0 >= 0 ? p0 : atomic_load(&g_val[0]), to = (sh + 1) % LANG_N; atomic_store(&g_pend[0], to);   // 语言行是单选的五项：A 依次轮换，四个框当场摆好
                  for (int k = 0; k < LANG_N; k++) { uint64_t z = find_w(sp, k_lang_box[k], 0); if (z) set_toggle(z, k == to); }
                  LOG("自定义设置：语言 → %s（手柄 A 在五项间轮换；点「应用」生效）", k_lang_name[to]); }
              else set_toggle(cw, !TOGGLED(cw)); } } }
      if (dm) { int32_t *r = (int32_t *)(dm + 0x70), want[4] = { 99999, 0, r[2], r[3] };   /* 不用时只把 x 挪出屏：它在滚动区里，y 放远了会撑大可滚范围 | when unused only x is moved off screen: it is inside the scroll area, and a far-away y would enlarge the scrollable range */
          if (dm_padmode_current() && tw && cw) { int32_t *t = (int32_t *)(tw + 0x70), *c = (int32_t *)(cw + 0x70); int top = t[1] < c[1] ? t[1] : c[1], bot = t[1] + t[3] > c[1] + c[3] ? t[1] + t[3] : c[1] + c[3];   // 框住整行：文字与勾选框两者的上下沿取并，再各放 10 单位（原先只按文字框的高度算，文字框比字矮，框成了横穿文字中线的一条） | Frame the whole row: take the union of the top and bottom edges of the text and the checkbox, then pad 10 units on each side (previously computed from the text box height only; the text box is shorter than the glyphs, so the frame became a strip cutting across the middle of the text)
              want[0] = t[0] - 30; want[1] = top - 10; want[2] = c[0] + c[2] + 30 - want[0]; want[3] = bot - top + 20;
              { static int lg; if (lg++ < 2) LOG("自定义设置：手柄选中框 文字框 %d,%d %dx%d 勾选框 %d,%d %dx%d → 框 %d,%d %dx%d", t[0], t[1], t[2], t[3], c[0], c[1], c[2], c[3], want[0], want[1], want[2], want[3]); } }
          if (memcmp(r, want, sizeof want)) { memcpy(r, want, sizeof want); ((void (*)(uint64_t))*(uint64_t *)(*(uint64_t *)dm + 0x10))(dm); } }
      // 这页可以滚（各行在原版滚动区里，touch_layout.py _custom_page）：手柄选到列表框外的行时，让原版滚动区自己滚到位——发滚轮，每 4 帧一格，直到这一行整行在框内。
      // 已滚的量 = 内层视图（行容器的父控件）矩形 y 的相反数；框高 = 滚动区（再上一级）的高。只读这两个数，不写原版控件
      // This page can scroll (the rows are in the original scroll area, touch_layout.py _custom_page): when the controller selects a row outside the list box, let the original scroll area scroll into place by itself -- send wheel events, one notch every 4 frames, until the whole row is inside the box.
      // Amount scrolled = the negative of the rect y of the inner view (the parent widget of the row container); box height = the height of the scroll area (one level further up). Only these two numbers are read; the original game's widgets are not written
      if (pad && tw && cw) { uint64_t rw = find_w(sp, "TouchCustomRows", 0), view = rw ? *(uint64_t *)(rw + 0x30) : 0, sv = view ? *(uint64_t *)(view + 0x30) : 0; static uint64_t lf; uint64_t dm_guest_frame(void); uint64_t f = dm_guest_frame();
          if (sv && f - lf >= 4) { int32_t *t = (int32_t *)(tw + 0x70), *c = (int32_t *)(cw + 0x70); int top = (t[1] < c[1] ? t[1] : c[1]) - 10, bot = (t[1] + t[3] > c[1] + c[3] ? t[1] + t[3] : c[1] + c[3]) + 10;
              int off = -*(int32_t *)(view + 0x74), vh = *(int32_t *)(sv + 0x7c); if (vh <= 0) vh = 1260; void dm_ui_wheel_on(uint64_t, int); if (sel == 0) top = 0;   /* 选回第一行：一直滚到最顶，把标题行露出来 | selection back on the first row: scroll all the way to the top so that the header row is revealed */
              if (top < off) { lf = f; dm_ui_wheel_on(sv, 1); } else if (bot > off + vh) { lf = f; dm_ui_wheel_on(sv, -1); } } } }
    { uint64_t bx[LANG_N]; int ok = 1; for (int k = 0; k < LANG_N; k++) { bx[k] = find_w(sp, k_lang_box[k], 0); ok &= bx[k] && *(uint8_t *)(bx[k] + 0x50); }   // 语言行：四项单选，存一个键 lang（0 中 / 1 英 / 2 西 / 3 俄） | Language row: four-option radio, stores one key lang (0 Chinese / 1 English / 2 Spanish / 3 Russian)
      if (ok) { int pd0 = atomic_load(&g_pend[0]), shown = pd0 >= 0 ? pd0 : atomic_load(&g_val[0]), to = shown;
          if (dm_wref_get(&seen[0]) != bx[0]) dm_wref_set(&seen[0], bx[0]);   // 页刚出现（布局里一律是没勾）：先摆成该显示的值，这一帧不当玩家点了 | The page has just appeared (in the layout everything is unchecked): first set it to the value it should show, and do not treat this frame as a player tap
          else for (int k = 0; k < LANG_N; k++) if (k != shown && TOGGLED(bx[k])) { to = k; break; }   // 点没选中的那一项 = 选它；点已选中的那一项不变（下面摆回勾上） | Tapping the unselected option = select it; tapping the selected option changes nothing (it gets re-checked below)
          if (to != shown) { atomic_store(&g_pend[0], to); LOG("自定义设置：语言 → %s（点「应用」生效）", k_lang_name[to]); }
          for (int k = 0; k < LANG_N; k++) if (TOGGLED(bx[k]) != (k == to)) set_toggle(bx[k], k == to); } }
    { uint64_t boxes[5]; int ok = 1; for (int k = 0; k < 5; k++) { boxes[k] = find_w(sp, k_q3d_box[k], 0); ok &= boxes[k] != 0; }
      if (ok) { int pd = atomic_load(&g_pend[K_Q3D]), shown = pd >= 0 ? pd : atomic_load(&g_val[K_Q3D]), to = shown;
          if (dm_wref_get(&seen[K_Q3D]) != boxes[0]) dm_wref_set(&seen[K_Q3D], boxes[0]);
          else for (int k = 0; k < 5; k++) if (k != shown && TOGGLED(boxes[k])) { to = k; break; }
          if (to != shown) atomic_store(&g_pend[K_Q3D], to);
          for (int k = 0; k < 5; k++) if (TOGGLED(boxes[k]) != (k == to)) set_toggle(boxes[k], k == to);
      } }
    for (int i = 1; i < NK; i++) { if (graphics_key(i)) continue; char nm[48]; snprintf(nm, sizeof nm, "TouchCustom_%s", k_keys[i]);
        uint64_t w = find_w(sp, nm, 0); if (!w || !*(uint8_t *)(w + 0x50)) continue;
        int pd0 = atomic_load(&g_pend[i]), want = pd0 >= 0 ? pd0 : atomic_load(&g_val[i]);
        uint64_t z = rev_box(i) ? find_w(sp, rev_box(i), 0) : 0;   // 两项单选行（3D 画质：上下的「流畅优先」「画质优先」；换组位置）：存的仍是一个键，反面那一项是它的反面 | Two-option radio rows (3D quality: "Smoothness first" / "Quality first" stacked; swap-group position): still stores one key, the other option is its negation
        if (dm_wref_get(&seen[i]) != w) { dm_wref_set(&seen[i], w); set_toggle(w, want); if (z) set_toggle(z, !want); continue; }   // 页刚出现（布局里一律是没勾）：先摆成该显示的值，这一帧不当玩家点了 | The page has just appeared (in the layout everything is unchecked): first set it to the value it should show, and do not treat this frame as a player tap
        int on = TOGGLED(w), pd = atomic_load(&g_pend[i]), shown = pd >= 0 ? pd : atomic_load(&g_val[i]);
        if (z) {   // 点没选中的那一项 = 选它；点已选中的那一项不变（摆回勾上） | Tapping the item that is not selected = select it; tapping the one already selected changes nothing (it is put back to checked)
            int zon = TOGGLED(z), to = on != shown ? (on ? 1 : shown) : zon != !shown ? (zon ? 0 : shown) : shown;
            if (to != shown) { atomic_store(&g_pend[i], to); LOG("自定义设置：%s → %d（点「应用」生效）",k_keys[i],to); }
            if (TOGGLED(w) != to) set_toggle(w, to); if (TOGGLED(z) != !to) set_toggle(z, !to); continue; }
        if (on != shown) { atomic_store(&g_pend[i], on); LOG("自定义设置：%s 勾选框 → %s（点「应用」生效）", k_keys[i], on ? "勾上" : "取消"); } }
    // 语言那一行下面的「点应用后把 App 关掉重开」提示（TouchCustomLangHint，布局里先放在屏外）：勾选框此刻显示的值与正在跑的语言不一致时挪到标签正下方（横坐标取标签的）
    // The "tap Apply, then close and reopen the app" hint under the language row (TouchCustomLangHint, placed off screen in the layout): moved right under the label (taking the label's x) when the value the checkboxes currently show differs from the language now running
    { uint64_t lw = find_w(sp, "TouchCustom_lang_Text", 0); int pd = atomic_load(&g_pend[0]), shown = pd >= 0 ? pd : atomic_load(&g_val[0]), diff = shown != lang_running();
      for (int k = 0; k < LANG_N && lw; k++) { uint64_t hw = find_w(sp, k_lang_hint[k], 0); if (!hw) continue;   // 提示用「选中的那种语言」写：选了哪种语言就显示哪种语言的那句 | Write the hint in "the selected language": whichever language is selected, show that language's sentence
          int x = diff && shown == k ? ((int32_t *)(lw + 0x70))[0] : 99999; int32_t *r = (int32_t *)(hw + 0x70);
          if (r[0] != x) { r[0] = x; ((void (*)(uint64_t))*(uint64_t *)(*(uint64_t *)hw + 0x10))(hw); } } }
    // 「应用 / 放弃」亮不亮（自定义页改了勾选，「应用」仍是灰的）：原版各选项子页的每帧更新 main+0xae5f28 把自己 +0x1a0「有未应用的改动」
    // 交给 main+0xaed9e4(设置页, 亮否)，按钮 +0x57a 是灰的标志；原版的勾选框一改就由面板置 +0x1a0，我们的勾选框原版面板不认识，这一位一直是 0。
    // 按待定值与生效值比（改回原样 = 没改动 = 灭）写这一位，走原版自己的亮灭与「有改动离开要确认」。只在面板虚表里确有 0xae5f28 时写（确认 +0x1a0 是这个意思）
    // Whether 「应用 / 放弃」(Apply / Discard) light up (a checkbox on the custom page was changed, yet 「应用」(Apply) is still gray): the per-frame update main+0xae5f28 of each original-game option subpage passes its own +0x1a0 'has unapplied changes'
    // to main+0xaed9e4(settings page, lit or not), and button +0x57a is the gray flag; an original-game checkbox has +0x1a0 set by the panel as soon as it changes, but the original panel does not know our checkboxes, so this bit stays 0.
    // Write this bit by comparing pending values with effective values (changed back to the original = no change = off), using the original game's own lit / unlit behavior and its 'confirm when leaving with changes'. Written only when the panel vtable really contains 0xae5f28 (confirming that +0x1a0 has this meaning)
    static uint64_t vt_ok_for; static int vt_ok; if (vt_ok_for != *(uint64_t *)sp) { vt_ok_for = *(uint64_t *)sp; vt_ok = 0;
        for (int k = 0; k < 96 && !vt_ok; k++) vt_ok = *(uint64_t *)(vt_ok_for + 8 * k) == (uint64_t)main_base() + 0xae5f28;
        if (!vt_ok) LOG("【红】自定义设置：借来的视频页虚表里没有 main+0xae5f28，不写「有改动」位（原版版本变了？）"); }
    if (vt_ok) { int dirty = enhanced_settings_dirty();
        if (*(uint8_t *)(sp + 0x1a0) != dirty) { *(uint8_t *)(sp + 0x1a0) = (uint8_t)dirty; LOG("自定义设置：有未应用的改动 → %d（「应用 / 放弃」%s）", dirty, dirty ? "点亮" : "变灰"); } }
}
