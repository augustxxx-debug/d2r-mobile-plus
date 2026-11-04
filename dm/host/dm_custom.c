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
#include "dm_crashreport.h"
#include "dm_diagup.h"
#include "dm_community.h"
#include "dm_res_cap.h"
#include "dm_custom_scroll.h"
#include "dm_sel_frame.h"
#include "dm_widget.h"
#include "dm_cursor.h"
#include "hle.h"
#include "dm_input.h"
#include "dm_i18n.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

// 「配音」单选（voice：0 中文配音 / 1 English voice，公开）：缺省包与编进过场解码（DM_MOVIE_FFMPEG=1）的包都有这一行，与过场解码开关无关
// （用户 2026-11-04：「配音放进 RC2」）。布局侧 touch_layout.py 的 _CUSTOM_KEYS 同样恒有这一行，两端行序一致。
// 它只改原版设置文件的 VoiceLanguage，不改 Language；英文配音在基底档案里（hle_fs.c 的 voice_english_present），读不到时回落中文配音
// The voice-over radio (voice: 0 Chinese voice / 1 English voice) is on both default builds and builds with cutscene decoding (DM_MOVIE_FFMPEG=1);
// it is no longer tied to that switch (user, 2026-11-04: "put voice into RC2"). _CUSTOM_KEYS in touch_layout.py always has the row too, so both sides agree.
// It only changes the original settings file's VoiceLanguage, never Language; the English voice lives in the base archive (voice_english_present in hle_fs.c) and falls back to Chinese voice when unreadable
// 下面这个字面量 1 是定的：配音行不跟 DM_MOVIE_FFMPEG 走，缺省包与带过场解码的包都有。t_custom_readlog 用缺省与 -DDM_MOVIE_FFMPEG=1 各编一份，两份都断言它是 1
// The literal 1 below is fixed: the voice row does not follow DM_MOVIE_FFMPEG, and both default builds and builds with cutscene decoding have it. t_custom_readlog is built twice (default and -DDM_MOVIE_FFMPEG=1) and asserts it is 1 in both
#define VOICE_ROW 1
// iCloud 那一行的键名：安卓包里没有这一行（CLAUDE.md §2.8），但槽位要留着（K_ICLOUD 下标、行号与 Apple 对齐）。
// 安卓用占位名，key_other_platform 把它挡在存盘与日志外，没有人按名字读写它。
// The key name of the iCloud row: the Android package has no such row (CLAUDE.md section 2.8), but the slot must stay (the K_ICLOUD index keeps row numbers aligned with Apple).
// Android uses a placeholder name; key_other_platform keeps it out of the settings file and log, and nothing reads or writes it by name.
#ifdef __ANDROID__
#define K_ICLOUD_KEY "save_sync_unused"
#else
#define K_ICLOUD_KEY "icloud"
#endif
static const char *const k_keys[] = { "lang", "voice", "auto_mana", "merc_pot", "drop_light", "map_mark", "item_notes", K_ICLOUD_KEY, "pot_pick", "drop_haptic", "gem_pick", "store_btn", "eco_fps", "q3d", "loot_filter", "auto_buff", "touch_swap_above", "free_tp", "fgen_level", "dyn3d", "belt_refill", "tel_stats", "auto_crash", "bright_level", "fps_hud", "upscale", "render_res", "rt_level" };   // fps_hud 右上角帧数标签（front_ios.m attach_fps_hud，缺省关） | fps_hud: top-right frame-rate label (front_ios.m attach_fps_hud, off by default)   // upscale 开关与上次选择的 q3d 档位分开保存；render_res 整帧输出分辨率档位 0 自动 / 1 省电 / 2 均衡 / 3 原生（dm_res_cap.h） | render_res: whole-frame output resolution level (dm_res_cap.h)
#define NK (int)(sizeof k_keys / sizeof *k_keys)
static const int k_def[NK] = { 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 0, 0, 2, 1, 1, 0, 0 };   // fps_hud 显示帧数缺省开（图形页第二行，用户 2026-10-28）| fps_hud Show frame rate on by default (Graphics tab row 2, user 2026-10-28)   // 末尾 rt_shadow（光追阴影，实验性，图形页）缺省关 | last: rt_shadow (ray-traced shadows, experimental, Graphics tab) off by default   // 缺省：60 FPS、MetalFX 开、Auto 档、渲染分辨率自动 | Defaults: 60 FPS, MetalFX on, Auto profile, render resolution Auto
// q3d 五档：自动／省电／均衡／高画质／原版；q3d_v2 标记避免旧布尔值被误读。
// q3d profiles: Auto / Eco / Balanced / Quality / Original; q3d_v2 distinguishes old boolean preferences.
// voice 恒为第 1 行后，其余下标整体后移一位（键表顺序见上，直接写死，改键表时一并核对）| with voice always row 1 the other indices shift by one (fixed numbers; check them together with the key table)
#define K_VOICE 1
#define K_MERC 3
#define K_ICLOUD 7   // iCloud 存档同步：只有 Apple 平台有（dm_icloud_supported），Android 的自定义页不出这一行 | iCloud save sync: Apple platforms only (dm_icloud_supported); the Android Custom page has no such row
#define K_FPS 12   // eco_fps：0 = 60、1 = 30（省电）、FPS_120 = 120 帧档；旧文件只有 0 / 1，旧包读到 2 当省电 30 | eco_fps: 0 = 60, 1 = 30 (power saving), FPS_120 = 120 fps mode; old files only hold 0 / 1, and old builds read 2 as power saving 30
#define FPS_120 2
#define K_Q3D 13
#define K_TOUCH_SWAP 16
// fgen_level：AI 帧生成档位（实验性）0 关（缺省）/ 1 省电（枚举保留、本车不开放）/ 2 平滑。插在 free_tp 之后、belt_refill 之前：前面写死的下标与后面按 NK - n 倒数的下标都不动
// （t_fgen_level 逐个核 K_* 宏指向的键名）| fgen_level: AI frame generation level (experimental) 0 off (default) / 1 power saving (enum kept, not opened in this train) / 2 smooth.
// Inserted after free_tp and before belt_refill so neither the fixed indices above nor the NK - n ones below move (t_fgen_level checks every K_* macro against its key name)
#define K_FGEN 18
// dyn3d：动态 3D 分辨率（B147，用户 2026-11-04）：0 关（缺省，3D 比例固定为设置值）/ 1 开（掉帧时 hle_oe.c 自动降 3D 比例、3D 到底再降整帧输出）。
// 插在 fgen_level 之后、belt_refill 之前（同 fgen_level 的插法）：前面写死的下标与后面按 NK - n 倒数的下标都不动
// dyn3d: Dynamic 3D resolution (B147, user 2026-11-04): 0 off (default; the 3D scale stays at the configured value) / 1 on (hle_oe.c lowers the 3D scale on dropped frames, then the whole-frame output once 3D is at its floor).
// Inserted after fgen_level and before belt_refill (as fgen_level was): neither the fixed indices above nor the NK - n ones below move
#define K_DYN3D 19
// free_tp（免费回城，dm_freetp.c）缺省开（用户 2026-10-30，RC2 起）：RC1.2 / 安卓 1.0.x 缺省关且存盘写全部键，它们留下的 free_tp=0 分不出是不是玩家关的，所以另记 free_tp_v2：没有这个标记的旧文件迁移一次到开；存盘都带标记，之后玩家再关就一直是关
// free_tp (free town portal, dm_freetp.c) on by default (user 2026-10-30, from RC2): RC1.2 / Android 1.0.x shipped it off and saved every key, so their free_tp=0 cannot be told from a player choice; hence the free_tp_v2 marker: an old file without it migrates once to on; every save writes the marker, so a later off by the player stays off
// auto_crash（自动上报崩溃，dm_crashreport.c）缺省关；第一次打开要在游戏内确认框里同意（dm_diagup.c），同意记一行 auto_crash_ok=1（点「应用」才写）。文件里只有 auto_crash=1 没有同意记录 = 当关 | auto_crash (automatic crash reports) off by default; the first enable needs consent in the in-game box, recorded as auto_crash_ok=1 (written on Apply). auto_crash=1 without that record counts as off
#define K_AUTOCRASH (NK - 6)   // rt_level 取代 rt_shadow / rt_ao 两键后前移一位；方案 C 的 bright_level 排在它后面 | one place up now that rt_level replaces rt_shadow / rt_ao; plan C's bright_level comes after it
// tel_stats（设备与使用统计，dm_telemetry.c）缺省关；告知框（dm_telebox.c）答过才算数：答过记一行 tel_notice=<版本>，之后可在页上分别开关，不再弹框。没答过时勾上 = 勾选框退回去，回主菜单弹告知框（构建没配地址 = 同自动上报的说明页） | tel_stats (device and usage statistics) off by default; it only counts once the notice was answered: tel_notice=<version> records that, afterwards both switches toggle freely with no further box. Ticking it unanswered falls back and the notice shows on the main menu (no origin = the same explanation page as crash reports)
#define K_TELSTATS (NK - 7)
__attribute__((weak)) void dm_telemetry_setting_changed(int on) { (void)on; }
static _Atomic int g_tel_notice;      // 答过的告知版本 | the answered notice version
__attribute__((weak)) int dm_diagup_modal(void) { return 0; }   // 不带提示框模块的单测用这几份；产品里 dm_diagup.c / dm_crashreport.c 的强定义替换 | for unit tests without those modules; the strong definitions replace them in the product
__attribute__((weak)) void dm_diagup_consent_open(void (*cb)(int)) { if (cb) cb(0); }
__attribute__((weak)) void dm_crashreport_setting_changed(int on) { (void)on; }
__attribute__((weak)) int dm_crashreport_configured(void) { return 0; }
static _Atomic int g_ac_ok;           // 文件里有同意记录 | consent on file
static _Atomic int g_ac_ok_pending;   // 确认框里同意了、还没点「应用」 | agreed in the box, Apply not tapped yet
#define K_BRIGHT (NK - 5)   // 画面加亮档位：0 关 / 1 低 / 2 中 / 3 高，缺省中（用户 2026-10-31，B098 定中 = 上限 2），dm_bright.c 每帧读 | brightening level: 0 off / 1 low / 2 mid / 3 high, default mid (user 2026-10-31; B098 made mid = cap 2), read every frame by dm_bright.c
#define K_FPSHUD (NK - 4)   // fps_hud 显示帧数：图形页第二行（原在自定义页末行）| fps_hud Show frame rate: Graphics tab row 2 (was the last Custom row)
#define K_UPSCALE (NK - 3)
#define K_RES (NK - 2)
#define K_RTLVL (NK - 1)   // 光追档位（实验性，缺省关）：0 关、1 低、2 中、3 高，见 dm_rt_level_setting；替换 rt_shadow / rt_ao 两个开关 | ray-tracing level (experimental, off by default): 0 off, 1 low, 2 medium, 3 high, see dm_rt_level_setting; replaces the rt_shadow / rt_ao switches
// 配置项的平台限制：没有 iCloud 的平台（Android：nvn_vk.c 强定义 0）自定义页不出 icloud 行，布局生成器按 DM_TARGET=android 同样不出（touch_layout.py _CUSTOM_KEYS），
// 行号两边对齐；键仍在设置文件里（分享摘要照写），只是没人读。弱定义 = 支持（Apple 不变）
// Per-platform settings: platforms without iCloud (Android: strong 0 in nvn_vk.c) get no icloud row on the Custom page, and the layout generator omits it likewise under
// DM_TARGET=android (touch_layout.py _CUSTOM_KEYS) so row numbers agree on both sides; the key stays in the settings file (the share summary still lists it), nobody reads it. Weak = supported (Apple unchanged)
#ifdef __ANDROID__
// 安卓：nvn_vk.c 强定义 0；这个弱定义与 row_on_page 里的调用都去掉，包里的符号表与调试信息也不留这个名字（§2.8）。
// Android: strong 0 in nvn_vk.c; both this weak definition and the call in row_on_page are gone, so the package's symbol table and debug info hold no such name (section 2.8).
#elif !defined(DM_GFX_GATE_TEST_HOOK)
__attribute__((weak)) int dm_icloud_supported(void) { return 1; }
#else
int dm_icloud_supported(void);
#endif
// 选项页「键 → 页 / 组 / 平台」表（RC2 方案 C，用户 2026-10-30；顺序 = 页内显示顺序，与存盘顺序 k_keys 无关）。布局 touch_layout.py 的 _OPT_TABLE 是同一张，单测 test_options_table.py 逐行比对。
// 组名 = 分组标题文案键 dmOptGrp<组>。还没并入的键（k_keys 里没有）也先占位，key_index 认不出就跳过：并入时只动 k_keys / k_def。Mac 只是实验室宿主，平台上跟 iOS 算一类（布局也只分 Apple / 安卓）
// Options "key -> page / group / platform" table (RC2 plan C, user 2026-10-30; order = display order within a page, unrelated to the save order k_keys). _OPT_TABLE in touch_layout.py is the same table, compared line by line by test_options_table.py.
// Group = heading text key dmOptGrp<group>. Keys not merged yet (absent from k_keys) already hold a place and are skipped when key_index does not know them: merging only touches k_keys / k_def. The Mac is only a lab host and counts with iOS (the layout also only tells Apple / Android apart)
enum { PG_GFX, PG_GAME, PG_SYS };
#ifdef __ANDROID__
// 平台列只用数值（与 enum 完全等价）。安卓构建带 -g，enum 的枚举名会原样进 .debug_str（PF_APPLE 就是这么进的 libdm.so，
// B089 扫到的那个词）；改用宏，值不变，调试信息里不再有平台名。Apple 侧仍是 enum。
// The platform column is used by value only (identical to the enum). The Android build uses -g, so enum names land verbatim in .debug_str
// (that is how PF_APPLE got into libdm.so, the word B089 found); use macros: same values, no platform names in the debug info. Apple keeps the enum.
#define PF_APPLE 1
#define PF_ANDROID 2
#define PF_ALL 3
#else
enum { PF_APPLE = 1, PF_ANDROID = 2, PF_ALL = 3 };
#endif
#ifdef __ANDROID__
#define PF_HERE PF_ANDROID
#else
#define PF_HERE PF_APPLE
#endif
static const struct { const char *key; unsigned char page; const char *group; unsigned char plat; } k_opts[] = {
    { "render_res", PG_GFX, "Display", PF_ALL }, { "bright_level", PG_GFX, "Display", PF_ALL }, { "dyn3d", PG_GFX, "Display", PF_ALL },
    { "fps_hud", PG_GFX, "Frame", PF_ALL }, { "eco_fps", PG_GFX, "Frame", PF_ALL }, { "dyn_refresh", PG_GFX, "Frame", PF_ALL }, { "fgen_level", PG_GFX, "Frame", PF_APPLE },
    { "upscale", PG_GFX, "Upscale", PF_APPLE }, { "q3d", PG_GFX, "Upscale", PF_APPLE },
    { "rt_level", PG_GFX, "Rt", PF_APPLE },   // RC2 光追档位一行取代 rt_shadow / rt_ao 两个开关（23a2ee3e） | RC2's one ray-tracing level row replaces the rt_shadow / rt_ao switches
    { "lang", PG_SYS, "Lang", PF_ALL }, { "voice", PG_SYS, "Lang", PF_ALL },
    { K_ICLOUD_KEY, PG_SYS, "Saves", PF_APPLE },
    { "tel_stats", PG_SYS, "Privacy", PF_ALL }, { "auto_crash", PG_SYS, "Privacy", PF_ALL },
    { "auto_mana", PG_GAME, "Combat", PF_ALL }, { "merc_pot", PG_GAME, "Combat", PF_ALL }, { "auto_buff", PG_GAME, "Combat", PF_ALL }, { "free_tp", PG_GAME, "Combat", PF_ALL },
    { "pot_pick", PG_GAME, "Loot", PF_ALL }, { "gem_pick", PG_GAME, "Loot", PF_ALL }, { "belt_refill", PG_GAME, "Loot", PF_ALL }, { "loot_filter", PG_GAME, "Loot", PF_ALL }, { "drop_light", PG_GAME, "Loot", PF_ALL }, { "drop_haptic", PG_GAME, "Loot", PF_APPLE },
    { "item_notes", PG_GAME, "Ui", PF_ALL }, { "map_mark", PG_GAME, "Ui", PF_ALL }, { "store_btn", PG_GAME, "Ui", PF_ALL }, { "touch_swap_above", PG_GAME, "Ui", PF_ALL },
};
#define N_OPTS (int)(sizeof k_opts / sizeof *k_opts)
static int key_index(const char *k) { if (k) for (int i = 0; i < NK; i++) if (!strcmp(k, k_keys[i])) return i; return -1; }
static int opt_page(int i) { for (int o = 0; o < N_OPTS; o++) if (!strcmp(k_opts[o].key, k_keys[i])) return (k_opts[o].plat & PF_HERE) ? k_opts[o].page : -1; return -1; }   // 这个键在本平台的页；-1 = 本平台没有这一行 | this key's page on this platform; -1 = no such row here
static int graphics_key(int i) { for (int o = 0; o < N_OPTS; o++) if (!strcmp(k_opts[o].key, k_keys[i])) return k_opts[o].page == PG_GFX; return 0; }
#ifdef __ANDROID__
static int row_on_page(int i, int page) { return opt_page(i) == page; }   // 安卓没有 icloud 行（平台列已挡掉），也不再引用 dm_icloud_supported | no iCloud row on Android (the platform column already hides it), and no dm_icloud_supported reference either
#else
static int row_on_page(int i, int page) { return opt_page(i) == page && !(i == K_ICLOUD && !dm_icloud_supported()); }
#endif
// 图形页的平台门控照键表（审计 r1 P2-3）：超分组（upscale）、光追组（rt_level）在本平台有没有行，看 k_opts 的平台列；安卓都没有，Apple 都有
// Graphics page platform gate from the key table (audit r1 P2-3): whether the upscaling group (upscale) and the ray-tracing group (rt_level) have rows here comes from k_opts' platform column; Android has neither, Apple both
static int key_on_here(const char *k) { int i = key_index(k); return i >= 0 && opt_page(i) == PG_GFX; }
static void graphics_plat_rows(int *up_show, int *rt_show) { *up_show = key_on_here("upscale"); *rt_show = key_on_here("rt_level"); }
// iCloud 这台机此刻能不能用（没登录、这个包没权限）：运行时探测，Apple 端 cloud_ios.m 强定义；弱定义 = 能用（Mac 宿主、单测）| whether iCloud is usable right now (not signed in, no entitlement): probed at run time, strong on Apple in cloud_ios.m; weak = usable (Mac host, unit tests)
#ifndef __ANDROID__
__attribute__((weak)) int dm_icloud_available(void) { return 1; }   // 安卓没有 iCloud 探测，名字不进包 | no iCloud probe on Android; the name stays out of the package
#endif
static int custom_row(int i) { return row_on_page(i, PG_SYS); }   // 系统页（借来的视频页 VideoOptionsPanel）上有这一行，语言在最前 | this key has a row on the System page (the borrowed VideoOptionsPanel), language first
static int custom_rows(void) { int n = 0; for (int i = 0; i < NK; i++) n += custom_row(i); return n; }
static int custom_key_at(int row) {
    for (int o = 0; o < N_OPTS; o++) { int i = key_index(k_opts[o].key); if (i >= 0 && custom_row(i) && row-- == 0) return i; }
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
static int voice_running(void) { return lang_running() == 0 ? 0 : 1; }   // 没单独选过时，按原来的「中文配中文、其余英文」显示
// merc_pot（佣兵自动喂药）原先缺省关、现在缺省开。旧文件每次存盘都把全部键写一遍，里面的 merc_pot=0 分不出「没动过」还是「自己关的」，
// 所以另记一行 merc_pot_v2：只有在缺省改成开之后玩家自己在页上改过这一项才写。读盘时有 merc_pot_v2 以它为准；没有 = 没在新缺省下动过 → 开
// （旧文件里的 merc_pot=1 也是开，结果一样）。merc_pot 那一行照旧写当前值，旧版本的包读它不出错
// merc_pot (mercenary auto-feed potions) used to be off by default and is now on by default. Old files write all keys on every save, and the merc_pot=0 in them cannot tell "never touched" from "turned off by the player",
// so a separate line merc_pot_v2 is recorded: it is written only when the player has changed this item on the page after the default was changed to on. On load, if merc_pot_v2 exists it is authoritative; absent = never touched under the new default → on
// (merc_pot=1 in an old file is also on, the same result). The merc_pot line is still written with the current value, so packages of older versions read it without error
static _Atomic int g_merc_explicit = -1;   // 文件里写着的 merc_pot_v2：-1 没写 / 0 / 1 | merc_pot_v2 as written in the file: -1 not written / 0 / 1

// 崩溃围栏自动关实验项之前的原值（-1 = 没备份）：存进设置文件一行 exp_off_backup=eco_fps:2,rt_level:2,upscale:1,fgen_level:2；
// 玩家下次打开图形页时按它预选（勾选框恢复成原来的样子），点「应用」生效并清掉备份，「放弃」保留备份、下次打开再预选
// Original values before the crash fence turned experimental features off (-1 = no backup): saved in the settings file as one line exp_off_backup=eco_fps:2,rt_level:2,upscale:1,fgen_level:2;
// the next time the player opens the Graphics tab they are pre-selected (checkboxes back as they were); Apply makes them effective and clears the backup, Discard keeps the backup and pre-selects again next time
// 帧生成一项记的是档位（0 / 1 / 2）；RC2 之前的包写的是开关，读到旧名 fgen:1 按 2 恢复 | the frame-generation entry holds the level (0 / 1 / 2); packages before RC2 wrote a switch, and the old name fgen:1 restores as 2
#define N_BAK 4
#define BAK_FGEN 3
static const char *const k_bak_name[N_BAK] = { "eco_fps", "rt_level", "upscale", "fgen_level" };
static _Atomic int g_bak[N_BAK] = { -1, -1, -1, -1 };
// 帧生成档位的归一：新键只认 0 / 1 / 2，3（保留给高倍）与任何非法值都读作 0（RC2 草案 §5「读到就当 0」）
// Level normalisation: the new key accepts only 0 / 1 / 2; 3 (reserved for higher multiples) and any invalid value read as 0 (RC2 draft §5)
static int fg_level_norm(long v) { return v == DM_FGEN_ECO ? DM_FGEN_ECO : v == DM_FGEN_SMOOTH ? DM_FGEN_SMOOTH : DM_FGEN_OFF; }
static int fg_level_parse(const char *s) {   // 整串是一个数才算（"2x" / 空 / 后导多余字符都按 0；前导空格与 q3d / render_res 一样被 strtol 跳过）| the whole rest must be a number ("2x" / empty / trailing junk all read as 0; leading space is skipped by strtol as for q3d / render_res)
    char *end; long v = strtol(s, &end, 10);
    if (end == s || (*end != '\n' && *end != '\r' && *end != 0)) return DM_FGEN_OFF;
    return fg_level_norm(v); }
// 备份串 exp_off_backup 里第 b 项的原值（-1 = 没有）。两个旧名照读：光追一项读到旧包的 rt_shadow:1 按中档 2（RC2 23a2ee3e 原有规则）；帧生成一项读到 RC2 之前的 fgen:1 按档位 2（旧的 1 是「开」= 平滑）
// Original value of entry b in the exp_off_backup string (-1 = absent). Two old names are still read: for ray tracing an older package's rt_shadow:1 restores as medium 2 (RC2 23a2ee3e's rule); for frame generation the pre-RC2 fgen:1 restores as level 2 (the old 1 meant "on" = smooth)
static int bak_value(const char *s, int b) {
    const char *q = strstr(s, k_bak_name[b]); size_t L = strlen(k_bak_name[b]);
    if (q && q[L] == ':' && q[L + 1] >= '0' && q[L + 1] <= '9') return b == BAK_FGEN ? fg_level_norm(q[L + 1] - '0') : q[L + 1] - '0';   // 帧生成这一项同样归一（别人写进来的 3 按 0）| the frame-generation entry is normalised too (a 3 written by something else reads as 0)
    if (b == 1) { q = strstr(s, "rt_shadow:"); if (q && q[10] >= '1' && q[10] <= '9') return 2; }   // 旧包的围栏备份：光追开过 = 中 | a crash-fence backup from an older package: ray tracing on = medium
    if (b == BAK_FGEN) { q = strstr(s, "fgen:"); if (q && q[5] >= '0' && q[5] <= '9') return q[5] != '0' ? DM_FGEN_SMOOTH : DM_FGEN_OFF; }   // 旧的开关：1 → 2（不是 1）| the old switch: 1 -> 2 (not 1)
    return -1; }
static _Atomic int g_repaint;
// 内存档位（用户 2026-10-29：「要按机型考虑，不同机型运存不一样」）：按本进程内存上限（phys_footprint + os_proc_available_memory，iOS main.m 在游戏堆分配之前量一次交进来）分三档，
// 阈值按上限、不按物理内存或机型清单（只有两台机实测，见共享笔记《iOS机型内存上限参考》：17 Pro / Duo 无权益都是 3375 MB；带 increased-memory-limit 权益约 6 GB，自然落最高档）：
//   2 档 ≥ 3200 MB：与之前一致（0 副作用）；1 档 2600–3199 MB：不开放 120 帧（图形页帧率行说明）、缺省 MetalFX 省电、渲染分辨率均衡；0 档 < 2600 MB：渲染分辨率也降到省电，主菜单提示一次。
// 档位缺省只在首次（文件里没有 mem_tier）/ 档位变化时套，而且只改还停在旧档缺省上的值——玩家改过的不动；没量到（Mac、单测）= 2 档。DM_MEM_TIER=0/1/2 覆盖实测（Mac 上截图 / 验证用）
// Memory tier (user 2026-10-29: "consider the model, devices have different RAM"): three tiers by this process's memory limit (phys_footprint + os_proc_available_memory, measured once by iOS main.m before the game heap is allocated),
// thresholds on the limit, not physical RAM or a model list (only two devices measured; 17 Pro / Duo are both 3375 MB without the entitlement; with increased-memory-limit about 6 GB, which lands on the top tier):
//   tier 2 >= 3200 MB: as before (no side effect); tier 1 2600-3199 MB: no 120 fps (note on the Graphics frame-rate row), default MetalFX Eco and render resolution Balanced; tier 0 < 2600 MB: render resolution Eco too, one-time main-menu notice.
// Tier defaults apply only on first run (no mem_tier in the file) / tier change, and only to values still at the old tier's default - values the player changed stay; not measured (Mac, unit tests) = tier 2. DM_MEM_TIER=0/1/2 overrides (Mac screenshots / checks)
static _Atomic int g_mem_limit_mb = -1, g_mem_phys_mb = -1;
static char g_mem_model[32];
static _Atomic int g_mem_tier_file = -1, g_mem_notice;
// 启动时生效的渲染分辨率 / 超分档位（本进程第一次读盘、套完档位缺省之后）：游戏按启动预留建渲染目标，局中调低不还内存（10-27 bench footprint 不回落），
// 所以内存 1 / 0 档的启动预留按这时的渲染分辨率档缩小（front_ios.m front_game_res），之后调高要下次启动才生效；分享摘要同时记启动值与当前值
// Render resolution / upscale profile in effect at launch (first load of this process, after tier defaults): the game builds render targets from the startup reserve and lowering mid-run returns no memory,
// so memory tiers 1 / 0 shrink the startup reserve to this render-resolution level (front_ios.m front_game_res) and raising it only takes effect on the next launch; the share summary records both launch and current values
// 「重启后生效」提示（B151，用户 11-04）：启动时读到的那一份值记一次（整局不变），某个要重启才生效的项当前值（含没点「应用」的）≠ 启动值时，
//   该行下方出橙色提示（原版色票 $FontColorOrange、28 号），改回启动值就消失、没改不显示。管的项：配音、物品注释、iCloud、
//   低内存下调高渲染分辨率（res_raise_needs_restart）、光追开关跨越「开 / 关」（启动时开光追才降一档渲染分辨率，dm_render_res_rt_drop 整局不变）
// "Applies after restart" hints (B151, user 11-04): the values read at launch are recorded once (fixed for the run); when an item that needs a restart has a current value (including one not yet applied)
//   different from the launch value, an orange hint (original token $FontColorOrange, 28 pt) appears under its row; changing back hides it, unchanged shows nothing. Items: voice, item notes, iCloud,
//   a higher render resolution under low memory (res_raise_needs_restart), and ray tracing crossing on / off (the one-tier render-resolution drop is decided at launch, dm_render_res_rt_drop is fixed for the run)
static int g_boot_v[NK], g_boot_ok;
static _Atomic int g_boot_res = -1, g_boot_q3d = -1;   // 文件里记的档位（-1 没有）、低内存提示已确认过 | tier recorded in the file (-1 none), low-memory notice already confirmed
static int mem_tier_of(int limit_mb) { return limit_mb < 0 || limit_mb >= 3200 ? 2 : limit_mb >= 2600 ? 1 : 0; }
static int mem_tier_env(void) { const char *e = getenv("DM_MEM_TIER"); return e && e[0] >= '0' && e[0] <= '2' && !e[1] ? e[0] - '0' : -1; }
int dm_mem_tier(void) { int t = mem_tier_env(); return t >= 0 ? t : mem_tier_of(atomic_load(&g_mem_limit_mb)); }
void dm_mem_boot_info(int *tier, int *limit_mb, int *phys_mb, int *res0, int *q3d0) {
    if (tier) *tier = dm_mem_tier(); if (limit_mb) *limit_mb = atomic_load(&g_mem_limit_mb); if (phys_mb) *phys_mb = atomic_load(&g_mem_phys_mb);
    if (res0) *res0 = atomic_load(&g_boot_res); if (q3d0) *q3d0 = atomic_load(&g_boot_q3d); }
const char *dm_mem_model(void) { return g_mem_model[0] ? g_mem_model : "?"; }
// 渲染分辨率档的高低（像素预算从小到大：省电 < 均衡 < 自动 ≤ 原生）| render-resolution levels by pixel budget, low to high: Eco < Balanced < Auto <= Native
static int res_rank(int lv) { static const int r[4] = { 2, 0, 1, 3 }; return lv >= 0 && lv < 4 ? r[lv] : 2; }
void dm_mem_measure_set(int limit_mb, int phys_mb, const char *model) {
    atomic_store(&g_mem_limit_mb, limit_mb); atomic_store(&g_mem_phys_mb, phys_mb); strlcpy(g_mem_model, model ? model : "?", sizeof g_mem_model); }
static int tier_def(int i, int tier) {   // 本档缺省：只有 q3d（MetalFX 档位）与 render_res（渲染分辨率）随档位变 | this tier's default: only q3d and render_res follow the tier
    if (tier < 2 && i == K_Q3D) return 1;               // 省电 | Eco
    if (tier < 2 && i == K_RES) return tier ? 2 : 1;    // 1 档均衡、0 档省电 | tier 1 Balanced, tier 0 Eco
    return k_def[i];
}        // 默认 / 放弃之后：页上的勾选框按待定值 / 生效值重摆一遍 | After default / discard: lay out the checkboxes on the page again according to pending values / effective values

const char *dm_save_commitdir(void);
// 这一端没有的设置项：不写进日志与设置文件（§2.8，B092；旧文件里的这一行读进来也没有作用） | a setting this platform does not have: stays out of the log and the settings file (B092; the line in an old file has no effect)
static int key_other_platform(int i) { (void)i;
#ifdef __ANDROID__
    return i == K_ICLOUD;   // 按槽位认，不再写键名字面量（§2.8：安卓包里不留 icloud 字样） | by slot, not by the key literal (section 2.8: no icloud wording in the Android package)
#else
    return 0;
#endif
}
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

// 120 帧档不给超分「高画质」（用户 2026-10-29，RC1：拿一点画质换内存余量）：存着的高画质（q3d=3）在 120 帧生效时按均衡（2）走，设置文件里仍是 3，回到 60 / 30 帧照旧高画质；
// 图形页在 120 帧下档位滑块只到均衡、档位行下多一行灰色副文（dm_graphics_ui_pump）。纯函数给单测（t_render_profile）
// 120 fps gets no MetalFX Quality (user 2026-10-29, RC1: trade a little image quality for memory headroom): a saved Quality (q3d=3) runs as Balanced (2) while 120 fps is in effect; the file keeps 3 and 60 / 30 fps get Quality back;
// at 120 fps the Graphics tab's profile slider stops at Balanced and a grey subtext line appears under the profile row (dm_graphics_ui_pump). Pure function for unit tests (t_render_profile)
static int q3d_for_fps(int q3d, int fps) { return fps == 120 && q3d == 3 ? 2 : q3d; }
static void save(void);
static int fps_selected(void);
static int fps_effective(int saved);   // 下面帧率档那里（测试开关 + 屏幕刷新率）；读盘时用它，不能走 dm_custom_get（会再进 load_once） | defined with the frame rate mode below (test switch + screen refresh); used while loading, where dm_custom_get would re-enter load_once
// 旧设置迁移（文件里没有 rt_level 时）：阴影开 + AO 开 → 高，阴影开 → 中，阴影关 → 关（公开包只有 rt_shadow，rt_ao 只在 RC2 开发包里出现过）
// Migrating old settings (no rt_level in the file): shadows + AO on → high, shadows on → medium, shadows off → off (public packages only have rt_shadow; rt_ao only ever appeared in RC2 development packages)
static int rt_level_migrate(int rt, int ao) { return !rt ? 0 : ao ? 3 : 2; }
static void load_once(void) {
    if (atomic_load(&g_loaded)) return;
    char p[1100]; if (!file_path(p, sizeof p)) return;   // 存档还没挂上：先按缺省开答，挂上后再读 | The save is not mounted yet: answer with the default (on) for now, and read again after it is mounted
    int v[NK]; for (int i = 0; i < NK; i++) v[i] = k_def[i];
    FILE *f = fopen(p, "r"); char ln[128];
    int lang = -1, voice = -1, merc = -1, qv2 = 0, ftv2 = 0, mem_was = -1, mem_ack = 0;
    int has_rtlvl = 0, old_rt = 0, old_ao = 0, old_fg = -1, has_fg = 0;
    for (int b = 0; b < N_BAK; b++) atomic_store(&g_bak[b], -1);   // 备份以文件为准 | the backup follows the file
    while (f && fgets(ln, sizeof ln, f)) { char *eq = strchr(ln, '='); if (!eq) continue; *eq = 0; int i = key_index(ln); if (i > 0) v[i] = eq[1] != '0';
        if (i == K_Q3D) { char *end; long q = strtol(eq + 1, &end, 10); if (end != eq + 1 && (*end == '\n' || *end == '\r' || *end == 0) && q >= 0 && q <= 4) v[i] = (int)q; else v[i] = 0; }
        if (i == K_BRIGHT) { char *end; long q = strtol(eq + 1, &end, 10); v[i] = end != eq + 1 && (*end == '\n' || *end == '\r' || *end == 0) && q >= 0 && q <= 3 ? (int)q : k_def[i]; }
        if (i == K_RES) { char *end; long q = strtol(eq + 1, &end, 10); if (end != eq + 1 && (*end == '\n' || *end == '\r' || *end == 0) && q >= 0 && q < DM_RES_LEVELS) v[i] = (int)q; else v[i] = 0; }
        if (i == K_RTLVL) { char *end; long q = strtol(eq + 1, &end, 10); v[i] = end != eq + 1 && (*end == '\n' || *end == '\r' || *end == 0) && q >= 0 && q <= 3 ? (int)q : 0; has_rtlvl = 1; }
        if (i == K_FGEN) { v[i] = fg_level_parse(eq + 1); has_fg = 1; }   // 0 / 1 / 2；3 与非法值按 0（RC2 草案 §5）| 0 / 1 / 2; 3 and invalid values read as 0 (RC2 draft §5)
        if (!strcmp(ln, "fgen")) old_fg = eq[1] != '0';   // 旧键（RC2 之前叫 fgen，布尔）| old key (called fgen before RC2, a boolean)
        if (!strcmp(ln, "rt_shadow")) old_rt = eq[1] != '0';
        if (!strcmp(ln, "rt_ao")) old_ao = eq[1] != '0';
        if (i == K_FPS && eq[1] == '2' && (eq[2] == '\n' || eq[2] == '\r' || eq[2] == 0)) v[i] = FPS_120;   // 其余照旧：0 = 60、非 0 = 30 | Otherwise as before: 0 = 60, non-zero = 30
        if (!strcmp(ln, "q3d_v2")) qv2 = eq[1] == '1';
        if (!strcmp(ln, "free_tp_v2")) ftv2 = eq[1] == '1';
        if (!strcmp(ln, "mem_tier") && eq[1] >= '0' && eq[1] <= '2' && (eq[2] == '\n' || eq[2] == '\r' || eq[2] == 0)) mem_was = eq[1] - '0';
        if (!strcmp(ln, "mem_notice")) mem_ack = eq[1] == '1';
        if (i == 0) { int l = atoi(eq + 1); if (l >= 0 && l < LANG_N) lang = v[0] = l; }
        if (i == K_VOICE) { int x = atoi(eq + 1); if (x == 0 || x == 1) voice = v[K_VOICE] = x; }
        if (!strcmp(ln, "merc_pot_v2")) merc = eq[1] != '0';
        if (!strcmp(ln, "auto_crash_ok")) atomic_store(&g_ac_ok, eq[1] == '1');
        if (!strcmp(ln, "tel_notice")) atomic_store(&g_tel_notice, atoi(eq + 1) > 0 ? atoi(eq + 1) : 0);
        if (!strcmp(ln, "exp_off_backup")) for (int b = 0; b < N_BAK; b++) atomic_store(&g_bak[b], bak_value(eq + 1, b)); }   // 旧名 rt_shadow: / fgen: 见 bak_value | old names rt_shadow: / fgen: see bak_value
    if (f) fclose(f);
    // 旧键 fgen 只在新键没写时读一次：fgen=1 → 2（平滑）、fgen=0 → 0。只在内存里迁，不在这里写盘——下一次存盘按新键写、旧行随整份重写消失
    // The old key fgen is read once only while the new key is absent: fgen=1 -> 2 (smooth), fgen=0 -> 0. Migrated in memory only, no write here: the next save writes the new key and the old line disappears with the whole-file rewrite
    if (!has_fg && old_fg >= 0) { v[K_FGEN] = old_fg ? DM_FGEN_SMOOTH : DM_FGEN_OFF;
#ifndef __ANDROID__   // 安卓照迁、不记日志（§2.8：帧生成的日志串不进安卓包）| Android migrates silently (§2.8: no frame-generation log text in the Android package)
        LOG("自定义设置：旧键 fgen=%d 迁移为 fgen_level=%d（只迁一次；新键存在时不再看旧键）", old_fg, v[K_FGEN]); }
#else
        }
#endif
    if (!qv2) v[K_Q3D] = v[K_Q3D] == 1 ? 3 : 0;
    if (!has_rtlvl) { v[K_RTLVL] = rt_level_migrate(old_rt, old_ao); if (old_rt) LOG("光追档位：旧设置 rt_shadow=%d rt_ao=%d 迁移为 rt_level=%d", old_rt, old_ao, v[K_RTLVL]); }
    if (!ftv2) { int ft = key_index("free_tp"); if (f && !v[ft]) LOG("自定义设置：旧文件的 free_tp=0 迁移为开（RC2 起缺省开，free_tp_v2）"); v[ft] = 1; }
    if (v[K_Q3D] == 4) { v[K_Q3D] = 0; v[K_UPSCALE] = 0; }
    { const char *tl = getenv("DM_TEST_LANG"); if (tl && *tl >= '0' && *tl < '0' + LANG_N) { lang = v[0] = *tl - '0'; g_lang_test = 1; LOG("自定义设置：测试开关 DM_TEST_LANG=%d（当作语言行选了 %s 并应用，不写回文件）", lang, k_lang_name[lang]); } }
    { const char *tv = getenv("DM_TEST_VOICE"); if (tv && *tv >= '0' && *tv <= '1') { voice = v[K_VOICE] = *tv - '0'; g_voice_test = 1; LOG("自定义设置：测试开关 DM_TEST_VOICE=%d（当作配音行选了 %s 并应用，不写回文件）", voice, voice ? "English" : "中文"); } }
    if (lang < 0) v[0] = lang_running();   // 没写过：按当前实际语言显示 | Never written: display according to the current actual language
    if (voice < 0) v[K_VOICE] = voice_running();
    char vlog[32]; snprintf(vlog, sizeof vlog, " voice=%d%s", v[K_VOICE], voice < 0 ? "(auto)" : "");   // (auto) = 文件里没写、按当前语言显示
    atomic_store(&g_lang_explicit, lang);
    atomic_store(&g_voice_explicit, voice);
    v[K_MERC] = merc < 0 ? k_def[K_MERC] : merc; atomic_store(&g_merc_explicit, merc);
    // 内存档位缺省：首次（文件里没 mem_tier，按「之前是 2 档」算）/ 档位变了，只改还停在旧档缺省上的值；2 档又是首次 = 什么都不变，也不写文件（0 副作用）
    // Memory-tier defaults: on first run (no mem_tier in the file, counted as "was tier 2") / tier change, only values still at the old tier's default move; tier 2 on first run = nothing changes and the file is not written (no side effect)
    int tier = dm_mem_tier(), mem_save = 0; char mem_what[96] = "缺省不变";
    if (mem_was != tier && (tier != 2 || mem_was >= 0)) {
        int prev = mem_was < 0 ? 2 : mem_was; size_t o = 0; mem_what[0] = 0; mem_save = 1;
        static const int mk[2] = { K_Q3D, K_RES };
        for (int k = 0; k < 2; k++) { int i = mk[k]; if (v[i] == tier_def(i, prev) && v[i] != tier_def(i, tier)) {
            v[i] = tier_def(i, tier); o += (size_t)snprintf(mem_what + o, o < sizeof mem_what ? sizeof mem_what - o : 0, "%s%s=%d", o ? "、" : "", k_keys[i], v[i]); } }
        if (!o) snprintf(mem_what, sizeof mem_what, "玩家改过的保留，缺省没动");
        mem_was = tier; }
    atomic_store(&g_mem_tier_file, mem_was); atomic_store(&g_mem_notice, mem_ack);
    if (!g_boot_ok) { memcpy(g_boot_v, v, sizeof g_boot_v); g_boot_ok = 1; }
    if (atomic_load(&g_boot_res) < 0) { atomic_store(&g_boot_res, v[K_RES]); atomic_store(&g_boot_q3d, v[K_UPSCALE] ? v[K_Q3D] : 4); }
    for (int i = 0; i < NK; i++) { atomic_store(&g_val[i], v[i]); atomic_store(&g_pend[i], -1); }
    dm_3d_quality_mode_set(v[K_UPSCALE] && q3d_for_fps(v[K_Q3D], fps_effective(v[K_FPS])) == 3); LOG("3D profile loaded: %d", v[K_UPSCALE] ? v[K_Q3D] : 4);
    // 读盘行按键表写全每个键（分享诊断日志的摘要靠它看出玩家开了什么；原先写死到 eco_fps，后加的 fps_hud / upscale / render_res 等都不在）
    // The load line lists every key from the key table (the diagnostic share summary relies on it; it used to stop at eco_fps, so later keys such as fps_hud / upscale / render_res were missing)
    char kv[512]; size_t o = 0; kv[0] = 0;
    for (int i = 2; i < NK && o < sizeof kv; i++) if (!key_other_platform(i)) o += (size_t)snprintf(kv + o, sizeof kv - o, " %s=%d", k_keys[i], v[i]);   // 从 2 起：lang / voice 另有 lang= / voice= 字段 | from 2: lang / voice have their own fields
    if (!atomic_exchange(&g_loaded, 1)) {
        LOG("自定义设置：读盘 %s%s lang=%d%s%s%s", p, f ? "" : "（没有文件，全按缺省）", v[0], lang < 0 ? "（没写，按当前语言）" : "", vlog, kv);
        int lim = atomic_load(&g_mem_limit_mb), phys = atomic_load(&g_mem_phys_mb); char pg[16] = "?"; if (phys >= 0) snprintf(pg, sizeof pg, "%.1f", phys / 1024.0);
        LOG("内存档位：上限 %d MB / 物理 %s GB / 机型 %s → 档位 %d（%s；≥ 3200 MB 为 2 档、2600–3199 为 1 档、< 2600 为 0 档）%s%s", lim, pg, g_mem_model[0] ? g_mem_model : "?", tier,
            mem_tier_env() >= 0 ? "DM_MEM_TIER 覆盖" : lim < 0 ? "没量到，按最高档" : "实测", mem_save ? "；套用本档缺省：" : "；", mem_save ? mem_what : "缺省不变");
        if (mem_save) save(); }
}
__attribute__((weak)) void dm_savemgr_open(int from) { (void)from; }   // 没有存档管理模块（单测）| no save-management module (unit tests)
void dm_custom_reload(void) { atomic_store(&g_loaded, 0); load_once(); }   // 存档导入改了设置文件之后（启动期，dm_savemgr_boot）| after a save import changed the settings file (at boot)
int dm_custom_loaded(void) { load_once(); return atomic_load(&g_loaded); }   // 设置文件读到了没有（iOS 壳在 didFinishLaunching 给目录之前一直是 0）| whether the settings file has been read (0 until the iOS shell gives the directory in didFinishLaunching)
int dm_custom_get(const char *key) {
    load_once(); int i = key_index(key); if (i < 0) return 1;
    return atomic_load(&g_loaded) ? atomic_load(&g_val[i]) : tier_def(i, dm_mem_tier());
}
int mtl_upscale_supported(void);   // 弱定义在下面（光追门旁）| weak definition below (next to the ray-tracing gate)
// 生效档：不支持超分的平台固定原版档；120 帧生效时高画质按均衡（q3d_for_fps）。设置档、生效档或帧率一变就记一行「超分档位：」（分享诊断日志摘要取最后一条，dm_sharelog.m perf_block / front_mac.m）
// Effective profile: platforms without upscaling stay on Original; Quality runs as Balanced while 120 fps is in effect (q3d_for_fps). One "超分档位：" line whenever the saved or effective profile or the frame rate changes (the share summary takes the last one, dm_sharelog.m perf_block / front_mac.m)
int dm_3d_profile_get(void) {
    if (!mtl_upscale_supported() || !dm_custom_get("upscale")) return 4;
    int q = dm_custom_get("q3d"), f = (dm_fps_setting(), fps_selected()), e = q3d_for_fps(q, f);   // 高画质上限跟「选的帧率」走，遇热退回 60 时不跟着放开（免得 MetalFX 随热度反复重建）| the Quality cap follows the selected rate, not the thermal fallback (so MetalFX is not rebuilt with the heat)
    static _Atomic int last = -1; int key = (q & 7) * 64 + (e & 7) * 8 + (f >= 90 ? 2 : f == 30);
    static const char *const nm[5] = { "自动", "省电", "均衡", "高画质", "原版" };
    if (atomic_exchange(&last, key) != key)
        LOG("超分档位：帧率 %d → 设置 %s、生效 %s%s", f, q >= 0 && q < 5 ? nm[q] : "?", e >= 0 && e < 5 ? nm[e] : "?", e != q ? "（120 帧档不提供高画质，按均衡）" : "");
    return e;
}
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
// 测试开关 DM_TEST_FPS=<帧率>[@秒][,<帧率>@秒…]（帧率 30 / 60 / 90 / 120）：按宿主初始化起的秒数切帧率档，走与设置页「应用」相同的运行时路径（显示节拍、节拍器、
// ProMotion 表态、3D 比例控制器的周期都每帧读 dm_fps_setting），不读也不写设置文件；第三档（90 / 120）同样只在屏幕撑得住时生效，90 在 120 Hz 屏上就按 90 走。
// 真机验 省电 / 60 / 高刷切换用
// Test switch DM_TEST_FPS=<fps>[@sec][,<fps>@sec...] (fps 30 / 60 / 90 / 120): switches the frame rate by seconds since host init, through the same runtime path as Apply on the settings page (display link, pacer,
// ProMotion declaration and the 3D scale controller period all read dm_fps_setting every frame); reads and writes no settings file; the third stop (90 / 120) likewise takes effect only on a screen that can do it, and 90 stays 90 on a 120 Hz screen.
// For checking 30 / 60 / high-refresh switching on a device
static int fps_schedule(const char *spec, double elapsed) {
    if (!spec || !*spec) return -1;
    int cur = -1; const char *p = spec;
    while (*p) { char *end; long f = strtol(p, &end, 10); if (end == p || (f != 30 && f != 40 && f != 60 && f != 90 && f != 120)) return -1;   // 40 只给帧生成量测（120 Hz 屏的三分之一）| 40 only for frame-generation measurements (a third of a 120 Hz screen)
        double at = 0; p = end; if (*p == '@') { at = strtod(p + 1, &end); if (end == p + 1 || at < 0) return -1; p = end; }
        if (elapsed >= at || cur < 0) cur = (int)f;
        if (*p == ',') p++; else if (*p) return -1; }
    return cur;
}
#ifndef __ANDROID__   // 帧生成决策层只有 Apple（§2.8，审计 P2-1）：安卓不编量测时间表 | the frame-generation decision layer is Apple only (§2.8, audit P2-1)
typedef struct { double at; short fps, on, gen, mult; char name[16]; } fg_seg;
static const fg_seg *fg_sched(void);
#endif
static int fps_test(void) {
#ifndef __ANDROID__
    { const fg_seg *s = fg_sched(); if (s) return s->fps; }
#endif   // 帧生成量测时间表 DM_FG_SCHED 同时定渲染帧率 | the frame-generation measurement schedule DM_FG_SCHED also sets the render rate
    static int on = -1; static const char *spec; static uint64_t t0; static _Atomic int last = -1;
    if (on < 0) { spec = getenv("DM_TEST_FPS"); on = spec && fps_schedule(spec, 0) > 0; t0 = (uint64_t)clock_gettime_nsec_np(CLOCK_MONOTONIC); if (spec && !on) LOG("【红】DM_TEST_FPS 非法：%s", spec); }
    if (!on) return -1;
    double el = ((uint64_t)clock_gettime_nsec_np(CLOCK_MONOTONIC) - t0) / 1e9; int f = fps_schedule(spec, el);
    if (atomic_exchange(&last, f) != f) LOG("测试 DM_TEST_FPS：第 %.0f 秒 → %d 帧档", el, f);
    return f;
}
// 高刷档（帧率第三格）在本机的帧率：屏幕最高刷新率 < 89 → 0（不开放）；89–118 → 90；≥ 119 → 120（144 / 165 等也封顶 120）。
// 屏幕刷新率由图形层给（nvn_mtl.m 的强定义，有窗口时读 UIScreen / CoreGraphics；安卓 front_android.c；未建窗口 0）；单测 / 不链图形层时 0 = 不开放
// The high-refresh stop (the third frame-rate stop) on this machine: screen max < 89 -> 0 (not offered); 89-118 -> 90; >= 119 -> 120 (144 / 165 etc. cap at 120).
// The refresh rate comes from the graphics layer (strong definition in nvn_mtl.m, reading UIScreen / CoreGraphics once there is a window; the Android front end front_android.c; 0 before the window exists); 0 = not offered in unit tests / without the graphics layer
int dm_high_hz(int max_hz) { return max_hz >= 119 ? 120 : max_hz >= 89 ? 90 : 0; }   // 纯函数，单测 t_render_profile 核边界 | pure function; t_render_profile checks the boundaries
#ifndef DM_DISPLAY_HZ_TEST_HOOK   // 单测直接包含本文件并自带强定义时打开（t_render_profile.c） | defined by unit tests that include this file and bring their own strong definition (t_render_profile.c)
__attribute__((weak)) int dm_display_max_hz(void) { return 0; }
#else
int dm_display_max_hz(void);
#endif
static int fps_high_hz(void) { return dm_high_hz(dm_display_max_hz()); }
int dm_fps_high_hz(void) { return fps_high_hz(); }   // 本机高刷档的帧率（0 = 不开放）| this machine's high-refresh rate (0 = not offered)
static int fps_high_ok(void) { return fps_high_hz() > 0 && dm_mem_tier() >= 2; }   // 内存 1 / 0 档不开放高刷 | memory tiers 1 / 0 get no high-refresh stop
static int fps_mem_capped(void) { return fps_high_hz() > 0 && dm_mem_tier() < 2; }   // 屏幕撑得住高刷、只因内存档位不开放：图形页帧率行出说明 | the screen could do high refresh and only the memory tier forbids it: note on the Graphics frame-rate row
// 图形层 mtl_fps_target 每帧读（弱符号的强定义）：eco_fps 0 = 60 / 1 = 30（省电）/ 2 = 高刷档（90 或 120，见 dm_high_hz） | Read every frame by mtl_fps_target in the graphics layer (strong definition of a weak symbol): eco_fps 0 = 60 / 1 = 30 (power saving) / 2 = the high-refresh stop (90 or 120, see dm_high_hz)
// 选/生效帧率：测试开关的值不超过本机高刷档（DM_TEST_FPS=90 在 120 Hz 屏上就是 90，120 在 90 Hz 屏上按 90）；设置里的第三档取本机高刷档。
// 内存 1 / 0 档、屏幕不到 89 Hz、还没建窗 → 高刷档按 60（存盘值不变）
// Selected / effective rate: a test-switch value does not exceed this machine's high-refresh rate (DM_TEST_FPS=90 is 90 on a 120 Hz screen, 120 runs at 90 on a 90 Hz screen); the saved third stop takes the machine's high-refresh rate.
// Memory tiers 1 / 0, a screen below 89 Hz, or no window yet -> the high-refresh stop runs at 60 (the saved value is unchanged)
static int fps_rate(int v, int t) {
    if (t == 40) return 40;   // 40 只有量测开关能到（DM_FG_SCHED / DM_TEST_FPS=40），不过高刷与遇热退回 | 40 only from the measurement switches; it skips the high-refresh and thermal logic
    if (v == 1) return 30;
    if (v != FPS_120 || !fps_high_ok()) return 60;
    int high = fps_high_hz();
    return t >= 90 && t < high ? t : high;
}
// 生效值或原因一变就记一行「帧率档：」（分享诊断日志的摘要按这个前缀取最后一行，dm_sharelog.m perf_block）。屏幕刷新率要等有窗口才读得到，所以「按 60」在建窗前后各可能记一次
// One "帧率档：" (frame rate mode) line whenever the effective value or its reason changes (the share-diagnostics summary takes the last line with this prefix, dm_sharelog.m perf_block). The screen refresh rate is readable only once there is a window, so "runs at 60" may be logged once before and once after
static int fps_effective(int saved) {   // 设置值（eco_fps）+ 测试开关 + 屏幕刷新率 → 生效帧率 30 / 60 / 90 / 120 | saved value (eco_fps) + test switch + screen refresh -> effective 30 / 60 / 90 / 120
    int t = fps_test(), v = t > 0 ? (t == 30 ? 1 : t >= 90 ? FPS_120 : 0) : saved;
    return fps_rate(v, t);
}
// 帧率只听设置（B147，用户 2026-11-04）：原先高刷档（90 / 120）温控 ≥ 2 自动退回 60、回落 60 秒再回高刷（2026-10-30 定，B060），
// 用户 Duo 实测开局约 157 秒就退到 60（dm-work/run/bugs/B147），改为不再按温控改帧率：30 / 60 / 120 只看设置，省电档仍是玩家手选。
// 过热只在游戏内提示一次（dm_thermal_hint_step，diag.c 局内调），不改任何设置。温控读数 thermal_now 只给这条提示用（AI 帧生成同样不再看温控）。
// 测试开关 DM_TEST_THERMAL=<档>[@秒][,<档>@秒…]（0–3）：按宿主计时秒数换温控，Mac 上验证用（单测 t_fps_thermal 核「温控 2 / 3 仍是 120」）
// The frame rate follows the setting only (B147, user 2026-11-04): the high-refresh stop (90 / 120) used to fall back to 60 at thermal >= 2 and return after 60 s cool (2026-10-30, B060);
// on the user's Duo it dropped to 60 about 157 s into a run (dm-work/run/bugs/B147). The rate is no longer changed by heat: 30 / 60 / 120 follow the setting, the power-saving stop is still the player's choice.
// Overheating shows one in-game tip (dm_thermal_hint_step, called in a run from diag.c) and changes no setting. thermal_now feeds only that tip (AI frame generation no longer consults it either).
// Test switch DM_TEST_THERMAL=<state>[@sec][,<state>@sec...] (0-3): thermal state by host seconds, for checks on the Mac (the unit test t_fps_thermal checks "thermal 2 / 3 stays at 120")
static int thermal_schedule(const char *spec, double elapsed) { return res_schedule_level(spec, elapsed); }   // 同渲染档时间表的写法与取值范围 0–3 | same syntax and 0-3 range as the render-level schedule
static uint64_t mono_ns(void) { return (uint64_t)clock_gettime_nsec_np(CLOCK_MONOTONIC); }
static uint64_t (*g_fps_clock)(void) = mono_ns;   // 单测换成假时钟 | unit tests swap in a fake clock
#ifdef DM_THERMAL_TEST_HOOK   // 单测自己给温控 | the unit test supplies the thermal state
int dm_thermal_now(void);
#else
__attribute__((weak)) int dm_thermal_now(void) { return -1; }   // 缺省未知（Mac、不链前端的单测）；iOS front_ios.m 的强定义覆盖 | unknown by default (Mac, unit tests without the front end); the strong definition in iOS front_ios.m overrides it
#endif
static int thermal_now(void) {
    static int on = -1; static const char *spec; static uint64_t t0;
    if (on < 0) { spec = getenv("DM_TEST_THERMAL"); on = spec && thermal_schedule(spec, 0) >= 0; t0 = g_fps_clock(); if (spec && !on) LOG("【红】DM_TEST_THERMAL 非法：%s", spec); }
    return on ? thermal_schedule(spec, (g_fps_clock() - t0) / 1e9) : dm_thermal_now();
}
int dm_fps_thermal_capped(void) { return 0; }   // B147 起帧率不再按温控退回：恒 0（分享摘要 thermalFallback60 照写 0，字段不删，老脚本照读）| from B147 the rate no longer falls back on heat: always 0 (the share summary still writes thermalFallback60=0 so old scripts keep parsing)
int dm_thermal_eff(void) { return thermal_now(); }   // 温控档位（含 DM_TEST_THERMAL）：diag.c 的过热提示用 | thermal state (DM_TEST_THERMAL included): for the overheating tip in diag.c
// 过热提示（B147 ③）：温控第一次到严重（≥ 2）时提示一次（每次启动一次），之后不再提示；未知（−1）不算。纯函数给单测 | overheating tip (B147 ③): once, the first time thermal reaches serious (>= 2) in this launch; unknown (-1) does not count. Pure function for unit tests
int dm_thermal_hint_step(int *shown, int thermal) { if (*shown || thermal < 2) return 0; *shown = 1; return 1; }
// 选的帧率（测试开关 / 设置 + 屏幕刷新率 + 内存档位），未计遇热退回 | the selected rate (test switch / setting + screen refresh + memory tier), before the thermal fallback
static int fps_selected(void) {
    int t = fps_test(), v = t > 0 ? (t == 30 ? 1 : t >= 90 ? FPS_120 : 0) : dm_custom_get("eco_fps");
    return fps_rate(v, t);
}
int dm_fps_setting(void) {
    int t = fps_test(), v = t > 0 ? (t == 30 ? 1 : t >= 90 ? FPS_120 : 0) : dm_custom_get("eco_fps"), hz = dm_display_max_hz();
    int f = fps_rate(v, t), why = (v == FPS_120 && f == 60) * 2 + (t > 0);
    int sel = f;
    static _Atomic int last = -1; int key = f * 16 + why * 4 + (hz > 0) + (hz >= 89) * 2;
    if (atomic_exchange(&last, key) != key) {
        // 生效帧率变了（应用、测试开关、建窗后才读到高刷屏）：3D 比例控制的高画质模式跟着「选的」高刷档上限走 | the effective rate changed (Apply, test switch, a high-refresh screen known once the window exists, thermal fallback / recovery): the 3D scale controller's Quality mode follows the selected high-refresh cap
        if (atomic_load(&g_loaded)) dm_3d_quality_mode_set(atomic_load(&g_val[K_UPSCALE]) && q3d_for_fps(atomic_load(&g_val[K_Q3D]), sel) == 3);
        int req = t == 40 ? 40 : v == 1 ? 30 : v == FPS_120 ? (t >= 90 ? t : fps_high_hz() ? fps_high_hz() : 120) : 60;
        LOG("帧率档：生效 %d 帧（%s%d，%s%d Hz）%s", f, t > 0 ? "测试开关 DM_TEST_FPS " : "设置 ", req, hz > 0 ? "屏幕最高 " : "还没窗口、刷新率 ", hz,
            why >= 2 && hz >= 89 ? "；内存档位不开放高刷档（见「内存档位：」行），高刷档按 60" : why >= 2 && hz > 0 ? "；屏幕不到 90 Hz，高刷档按 60" : why >= 2 ? "；暂按 60"
            : "");
    }
    return f;
}
// 动态 3D 分辨率（B147 ②，键 dyn3d，图形页「显示」组，缺省关）：开 = hle_oe.c 按实际掉帧降 3D 比例（3D 到底再降整帧输出档）；关 = 3D 比例固定为设置值。
// 设置还没读到时按缺省（关）答。测试开关 DM_3D_SCALE_DYN=0/1 在 hle_oe.c 那边钉住，不经这里
// Dynamic 3D resolution (B147 ②, key dyn3d, Graphics "Display" group, off by default): on = hle_oe.c lowers the 3D scale on actual dropped frames (then the whole-frame output level once 3D is at its floor);
// off = the 3D scale stays at the configured value. Answers the default (off) until the settings are read. The DM_3D_SCALE_DYN=0/1 test switch pins it on the hle_oe.c side, not here
int dm_dyn3d_setting(void) { return atomic_load(&g_loaded) ? atomic_load(&g_val[K_DYN3D]) != 0 : k_def[K_DYN3D] != 0; }
// 光追阴影（键 rt_shadow，实验性，缺省关，图形页）：图形层 nvn_rtshadow.inc rts_on 每帧读；机器不过门槛（mtl_rt_shadow_supported）时按关处理，图形页也不显示这一行
// Ray-traced shadows (key rt_shadow, experimental, off by default, Graphics tab): read every frame by rts_on in nvn_rtshadow.inc; treated as off on machines that fail the gate (mtl_rt_shadow_supported), and the Graphics tab hides the row
#ifdef DM_RT_GATE_TEST_HOOK   // 单测自己给强定义（同 DM_GFX_GATE_TEST_HOOK）| the unit test supplies the definition (as DM_GFX_GATE_TEST_HOOK)
int mtl_rt_shadow_supported(void);
#else
__attribute__((weak)) int mtl_rt_shadow_supported(void) { return 0; }   // 弱定义 = 不支持；图形层 nvn_rtshadow.inc 的强定义覆盖它（只有弱引用时，不链图形层的单测在 macOS 上链接不过） | weak definition = unsupported; the strong definition in nvn_rtshadow.inc overrides it (with only a weak reference, unit tests that do not link the graphics layer fail to link on macOS)
#endif
static int rt_shadow_ok(void) { return mtl_rt_shadow_supported(); }
// 超分（MetalFX 空间超分，键 upscale + q3d）与整帧输出分辨率档（键 render_res）只有 Apple 图形层 / iOS 前端实现：弱定义 = 支持（Apple 行为不变）；
// Android 的 nvn_vk.c 强定义返回 0 → 图形页把这两组行藏起来、dm_3d_profile_get 固定原版档（方案：不能显示开着超分却只做普通 blit；render_res 在 Android 的 900p 输出下没有意义）
// Upscaling (MetalFX spatial, keys upscale + q3d) and the whole-frame output resolution level (key render_res) exist only in the Apple graphics layer / iOS front end: weak definition = supported (Apple unchanged);
// Android's nvn_vk.c defines them strongly as 0 -> the Graphics tab hides both groups of rows and dm_3d_profile_get stays on the Original profile (plan: never show an upscaling switch that only blits; render_res is meaningless with Android's 900p output)
#ifdef DM_GFX_GATE_TEST_HOOK   // 单测自己给强定义（同 DM_DISPLAY_HZ_TEST_HOOK 的做法）| the unit test supplies the definitions (as DM_DISPLAY_HZ_TEST_HOOK)
int mtl_upscale_supported(void); int dm_render_res_supported(void);
#else
__attribute__((weak)) int mtl_upscale_supported(void) { return 1; }
__attribute__((weak)) int dm_render_res_supported(void) { return 1; }
#endif
// 图形页的行（与 touch_layout.py _graphics_page 同序）：可见的紧挨着排，返回可见行数，out 依次是布局行号；选中框按可见序号放、行为按布局行号走（单测 t_graphics_gate）
// Graphics-tab rows (same order as touch_layout.py _graphics_page): the visible ones are packed, returns the visible count with the layout row ids in out; the selection frame follows the visible index, behaviour follows the layout row (unit test t_graphics_gate)
enum { ROW_RES, ROW_BRIGHT, ROW_DYN3D, ROW_FPSHUD, ROW_FPS, ROW_FG, ROW_UPSCALE, ROW_PROFILE, ROW_RT };   // 显示帧数行第二（用户 2026-10-28）；AI 帧生成在「帧率」组、帧率滑块之后（k_opts 的 Frame 组）| the Show-frame-rate row is second (user 2026-10-28); AI frame generation sits in the Frame group after the frame-rate slider (k_opts' Frame group)
// 可见行 i 的 y：行紧挨着排，换组的地方多一行分组标题（方案 C：画面 / 帧率 / 超分 / 光线追踪）| y of visible row i: rows are packed, with one heading row where the group changes (plan C)
static int graphics_row_group(int row) { return row <= ROW_DYN3D ? 0 : row <= ROW_FG ? 1 : row <= ROW_PROFILE ? 2 : 3; }
static int graphics_row_y(const int *vis, int i) { int y = 180; for (int k = 1; k <= i; k++) y += graphics_row_group(vis[k]) != graphics_row_group(vis[k - 1]) ? 360 : 180; return y; }
static int graphics_rows_pack(int res_ok, int up_ok, int prof_on, int rt_ok, int fg_ok, int out[9]) {
    int n = 0; if (res_ok) out[n++] = ROW_RES; out[n++] = ROW_BRIGHT; out[n++] = ROW_DYN3D; out[n++] = ROW_FPSHUD; out[n++] = ROW_FPS; if (fg_ok) out[n++] = ROW_FG; if (up_ok) { out[n++] = ROW_UPSCALE; if (prof_on) out[n++] = ROW_PROFILE; } if (rt_ok) out[n++] = ROW_RT; return n;
}
// 图形层 nvn_rtshadow.inc rts_on 每帧读（弱符号的强定义）：玩家档 0–3，已含机器门槛和内存封顶；DM_RT_PRESET=0–3 钉住。生效值一变就记一行「光追档位：生效 rt_level=」（分享诊断日志的摘要按这个前缀取最后一行）
// 内存档位 < 2 时高档不开放（AO、软影多两张贴图，内存紧的机器不值得）：存的还是高，生效值夹到中
// Read every frame by rts_on in nvn_rtshadow.inc (strong definition of a weak symbol): player level 0–3, machine gate and memory cap included; DM_RT_PRESET=0–3 pins it. One "光追档位：生效 rt_level=" line whenever the effective value changes (the share-diagnostics summary takes the last line with this prefix)
// High is not offered below memory tier 2 (AO and soft shadows add textures, not worth it on a tight machine): the stored value stays high, the effective one is clamped to medium
static int rt_level_cap(void) { return dm_mem_tier() < 2 ? 2 : 3; }
static int rt_level_pin(void) { static int v = -2; if (v == -2) { const char *e = getenv("DM_RT_PRESET"); v = e && *e >= '0' && *e <= '3' && !e[1] ? *e - '0' : -1; if (e && v < 0) LOG("【红】DM_RT_PRESET 非法：%s（要求 0–3），按设置", e); } return v; }
int dm_rt_level_setting(void) {
    int ok = rt_shadow_ok(), pin = rt_level_pin(), set = pin >= 0 ? pin : dm_custom_get("rt_level"), cap = rt_level_cap(), on = !ok ? 0 : set > cap ? cap : set;
    // 没有硬件光追的机器（含安卓）：设置里的残留档位不生效，写回关，换设备恢复的设置文件也不会留着（B108）| machines without hardware ray tracing (Android included): a leftover level in the settings has no effect and is written back as off, including from a settings file restored from another device (B108)
    if (!ok && pin < 0 && set) { atomic_store(&g_val[K_RTLVL], 0); atomic_store(&g_pend[K_RTLVL], -1); save(); LOG("光追档位：本机没有硬件光追，设置里的 rt_level=%d 写回关", set); set = 0; }
    static _Atomic int last = -1; int key = on * 64 + set * 8 + cap * 2 + ok;
    if (atomic_exchange(&last, key) != key) LOG("光追档位：生效 rt_level=%d（%s %d，本机最高 %d，机器%s）", on, pin >= 0 ? "DM_RT_PRESET 钉住" : "设置", set, cap, ok ? "可用" : "不可用");
    return on;
}
// 光追开时启动即降一档渲染分辨率（dm_res_cap.h dm_res_rt_budget）：只在第一次被问时（iOS 前端报启动预留那一刻）按当时的光追生效值定，整局不变——游戏的渲染目标按启动尺寸建、局中改不回来。
// 光追开 = 测试开关 DM_RT_SHADOW 钉住时按它，否则按设置；都要过机器门槛。DM_RT_RES_DROP=0 关掉（真机 A/B 量省了多少内存用）
// One render-resolution tier down from startup when RT is on (dm_res_cap.h dm_res_rt_budget): decided on the first call (the moment the iOS front end reports the startup reserve) from the RT state then, fixed for the run -
// the game builds its render targets at the startup size and cannot take them back mid-run. RT on = the DM_RT_SHADOW test switch when it pins, otherwise the setting; both must pass the machine gate. DM_RT_RES_DROP=0 turns it off (device A/B of the memory saved)
// 纯函数给单测：drop_env / rt_env 是两个环境变量（NULL = 没设），gate 机器门槛，setting_on 设置生效值（已含门槛）；*on 回填「启动时光追开没开」
// Pure function for unit tests: drop_env / rt_env are the two environment variables (NULL = unset), gate the machine gate, setting_on the effective setting (gate included); *on returns whether RT was on at startup
static int rt_res_drop_decide(const char *drop_env, const char *rt_env, int gate, int setting_on, int *on) {
    *on = rt_env ? atoi(rt_env) != 0 && gate : setting_on;
    return *on && !(drop_env && !atoi(drop_env));
}
int dm_render_res_rt_drop(void) {
    static _Atomic int v = -1; int r = atomic_load(&v), on; if (r >= 0) return r;
    const char *d = getenv("DM_RT_RES_DROP"), *e = getenv("DM_RT_PRESET") ? NULL : getenv("DM_RT_SHADOW"); int off = d && !atoi(d);
    r = rt_res_drop_decide(d, e, e ? rt_shadow_ok() : 0, e ? 0 : dm_rt_level_setting() > 0, &on);
    // 设置还没读到（也没用测试开关钉住）：先按「不降」答，但不记死——等读到设置再定（十四审 Q14-01：原先在设置可读之前就记死，设置里开光追永远不降）
    // Settings not read yet (and not pinned by the test switch): answer "no drop" without latching - decide once they are read (review 14 Q14-01: it used to latch before the settings were readable, so RT on in settings never dropped)
    if (!e && !dm_custom_loaded()) { static int warned; if (!warned++) LOG("【红】光追降分辨率：设置还没读到就被问（先按不降答、不记死）"); return 0; }
    atomic_store(&v, r);
    LOG("光追降分辨率：判定 %s（启动时光追%s%s；整局不变，改光追设置下次启动生效）", r ? "降一档" : "不降", on ? "开" : "关", off ? "，测试开关 DM_RT_RES_DROP=0 关掉" : "");
    return r;
}
// ---- AI 帧生成决策层：只编进 Apple（§2.8 严格口径，审计 P2-1）。安卓只留键表项 fgen_level、k_opts 项与围栏备份名（两端行号与存盘对齐，同 b95c165e 对 iCloud），
// 档位照读照存、按关生效、不改写；下面 #else 只给一个桩 fg_platform_ok() = 0，函数名、环境变量名、日志串都不进安卓包
// ---- AI frame generation decision layer: Apple builds only (§2.8 strict, audit P2-1). Android keeps only the fgen_level key, its k_opts entry and the fence backup name (row numbers and
// the settings file stay aligned on both sides, as b95c165e did for iCloud): the level is read and saved, takes effect as off and is not rewritten; the #else below is one stub, fg_platform_ok() = 0
#ifndef __ANDROID__
// 探索开关 DM_FG_SCHED=<段>@秒,…（多倍帧生成量测，同一局内按时段交替；公开包没有入口）：段 = off<基准> 或 <生成器><基准>x<倍数>，生成器 mfx / shift / null / ext，基准 30 / 40 / 60，如 off60@0,null60x2@70,shift30x4@110
// Exploration switch DM_FG_SCHED=<segment>@sec,... (multi-frame generation measurement, alternating within one run; no entry in public builds): segment = off<base> or <generator><base>x<multiple>, generators mfx / shift / null / ext, bases 30 / 40 / 60
static fg_seg g_fgs[64]; static int g_fgs_n; static uint64_t g_fgs_t0; static pthread_once_t g_fgs_once = PTHREAD_ONCE_INIT;
static void fg_sched_load(void) {
    const char *e = getenv("DM_FG_SCHED"); g_fgs_t0 = (uint64_t)clock_gettime_nsec_np(CLOCK_MONOTONIC); if (!e) return;
    static const char *const gn[4] = { "mfx", "shift", "null", "ext" };
    for (const char *p = e; *p && g_fgs_n < 64; ) { fg_seg s = { .mult = 2 }; const char *c = strchr(p, ','); size_t n = c ? (size_t)(c - p) : strlen(p); char b[32]; if (n >= sizeof b) goto bad; memcpy(b, p, n); b[n] = 0;
        char *at = strchr(b, '@'); if (at) { s.at = atof(at + 1); *at = 0; } strlcpy(s.name, b, sizeof s.name); char *q = b; s.gen = -1;
        if (!strncmp(q, "off", 3)) { q += 3; s.gen = 0; } else { for (int i = 0; i < 4; i++) if (!strncmp(q, gn[i], strlen(gn[i]))) { s.gen = (short)i; s.on = 1; q += strlen(gn[i]); } }
        if (s.gen < 0) goto bad;
        s.fps = (short)strtol(q, &q, 10); if (s.fps != 30 && s.fps != 40 && s.fps != 60) goto bad;
        if (s.on) { if (*q != 'x') goto bad; s.mult = (short)atoi(q + 1); if (s.mult < 2 || s.mult > 4) goto bad; }
        g_fgs[g_fgs_n++] = s; p += n; if (*p == ',') p++; continue;
      bad: LOG("【红】DM_FG_SCHED 非法：%s", e); g_fgs_n = 0; return; }
}
static const fg_seg *fg_sched(void) {
    pthread_once(&g_fgs_once, fg_sched_load); if (!g_fgs_n) return NULL;
    double el = ((uint64_t)clock_gettime_nsec_np(CLOCK_MONOTONIC) - g_fgs_t0) / 1e9; int i = 0; while (i + 1 < g_fgs_n && el >= g_fgs[i + 1].at) i++;
    static _Atomic int last = -1; if (atomic_exchange(&last, i) != i) LOG("帧生成调度：第 %.0f 秒 → %s", el, g_fgs[i].name);
    return &g_fgs[i];
}
double dm_fg_sched_t(void) { pthread_once(&g_fgs_once, fg_sched_load); return ((uint64_t)clock_gettime_nsec_np(CLOCK_MONOTONIC) - g_fgs_t0) / 1e9; }
const char *dm_fg_sched_name(void) { const fg_seg *s = fg_sched(); return s ? s->name : "-"; }
int dm_fg_gen(void) { const fg_seg *s = fg_sched(); if (s) return s->gen; static int v = -1; if (v < 0) v = getenv("DM_FG_SHIFT") != NULL; return v; }   // 0 插值器 / 1 平移 / 2 零成本（真帧再上屏一次）/ 3 镜头外推 | 0 interpolator / 1 shift / 2 null (the real frame again) / 3 camera extrapolation
int dm_fg_mult(void) { const fg_seg *s = fg_sched(); return s ? s->mult : 2; }
int dm_fg_need_fi(void) { pthread_once(&g_fgs_once, fg_sched_load); if (!g_fgs_n) return !getenv("DM_FG_SHIFT"); for (int i = 0; i < g_fgs_n; i++) if (g_fgs[i].on && g_fgs[i].gen == 0) return 1; return 0; }
// AI 帧生成（实验性，图形页「帧率」组一行，键 fgen_level，缺省关；帧插值，日志前缀「帧生成：」）：帧率档只管真渲染帧率，帧生成只管显示翻倍。
// 两层门控（方案 C，同光追 / 超分）：平台层照键表 k_opts（fgen_level 只有 Apple；安卓整行不出现，§2.8）；本机层 fg_row_ok = 设备支持插值器（图形层 mtl_fg_supported）、
// 屏幕 ≥ 120 Hz、内存档位放开高刷档（档位 ≥ 2）——不满足时这一行还在、置灰、副文写原因。
// 生效（fg_reason）还要：不是 30 帧档（用户 2026-10-27 定）、不是高刷档（90 / 120，RC2 84c04ac2；屏幕已到顶，按「选的」帧率判）、光追关（rt_level 生效档 0；
// 两个实验项的组合没有验证过内存与 GPU 预算，且只能二选一拿预算）。不看温控（B147，用户 2026-11-04「一样取消自动关闭，只按你的设置来」：原先温控 ≥ 2 停、回落 60 秒才恢复）；过热只沿用那条一次性提示。
// 测试开关 DM_TEST_FG=<0|1>[@秒],…（写法同 DM_TEST_FPS）顶替设置值，不读写设置文件。
// 生效值或原因一变就记一行「帧生成：状态 fg=…」（分享诊断日志摘要取最后一行就是当前状态）
// AI frame generation (experimental, one row in the Graphics page's Frame group, key fgen_level, off by default; frame interpolation, log prefix "帧生成："): the frame-rate mode sets only the true render rate, frame generation only doubles what is shown.
// Two-layer gate (plan C, as ray tracing / upscaling): the platform layer follows k_opts (fgen_level is Apple only; on Android the row is absent, §2.8); the machine layer fg_row_ok = interpolator support
// (graphics layer mtl_fg_supported), a >= 120 Hz screen and a memory tier that allows the high-refresh stop (tier >= 2) - failing it the row stays, greyed, with the reason as subtext.
// In effect (fg_reason) also needs: not the 30 fps mode (user decision 2026-10-27), not the high-refresh stop (90 / 120, RC2 84c04ac2; judged by the selected rate), ray tracing off
// (effective rt_level 0; the combination has no verified memory / GPU budget). The thermal state is not consulted (B147, user 2026-11-04 "cancel the automatic stop too, follow my setting": it used to stop at >= 2 and resume after 60 s cool); overheating only shows the one-time tip.
// Test switches DM_TEST_FG=<0|1>[@seconds],... override the setting without touching the settings file. One "帧生成：状态 fg=..." line whenever the effective value or its reason changes
#ifndef DM_FG_TEST_HOOK   // 单测（t_fgen_level.c）自带强定义 | the unit test brings its own strong definitions
__attribute__((weak)) int mtl_fg_supported(void) { return 0; }
__attribute__((weak)) int mtl_fg_backoff(void) { return 0; }
#else
int mtl_fg_supported(void); int mtl_fg_backoff(void);
#endif
// 平台：帧生成只有 Apple 图形层有。安卓 / 鸿蒙壳单独判，好在读到非 0 的 fgen_level（设置文件跨端互通）时不改写、按关生效（日志写「本端暂未接入」）
// Platform: only the Apple graphics layer has frame generation. The Android / Harmony shell is checked separately so a non-zero fgen_level (settings files travel between platforms) is not rewritten and takes effect as off
#if defined(__ANDROID__) || defined(__OHOS__)
#define DM_FG_PLATFORM 0
#else
#define DM_FG_PLATFORM 1
#endif
#ifdef DM_FG_PLATFORM_TEST_HOOK   // 单测自己给（t_fgen_level.c：1 = Apple 壳 / 0 = 安卓壳）| supplied by the unit test (t_fgen_level.c: 1 = Apple shell / 0 = Android shell)
int dm_fg_platform_test(void);
static int fg_platform_ok(void) { return dm_fg_platform_test(); }
#else
static int fg_platform_ok(void) { return DM_FG_PLATFORM; }
#endif
// 设置值（已归一；旧键迁移也在这个值里）。图形页勾选框只做关 / 平滑两态（勾 = 2），省电档不出现在界面
// The setting value (normalised; migration included). The Graphics checkbox has only the off / smooth states (checked = 2); the power-saving level is not on the page
int dm_fgen_level(void) { return dm_custom_get("fgen_level"); }
// DM_FGEN_PRESET=<0|1|2>：钉住档位（真机对比 / 纯设置路径回归用），优先于设置；3（保留档）同样按 0 处理，其余串非法
// DM_FGEN_PRESET=<0|1|2>: pins the level (device comparisons / pure-settings regression), ahead of the setting; 3 (reserved) also reads as 0, anything else is invalid
static int fgen_preset_parse(const char *spec) {
    if (!spec || !*spec || spec[1]) return -1;
    if (spec[0] >= '0' && spec[0] <= '9') { int v = spec[0] - '0'; return v <= 3 ? fg_level_norm(v) : -1; }
    return -1; }
static int fgen_preset(void) {
    static int v = -2; if (v != -2) return v;
    const char *e = getenv("DM_FGEN_PRESET"); v = fgen_preset_parse(e);
    if (e && *e && v < 0) LOG("【红】DM_FGEN_PRESET 非法（只认 0 / 1 / 2）：%s", e);
    else if (v >= 0) LOG("帧生成：测试开关 DM_FGEN_PRESET=%d（钉住档位，优先于设置）", v);
    return v; }
// 档位优先级：DM_TEST_FG（局内按秒切 0 / 1，1 = 平滑）> DM_FGEN_PRESET > 设置。两者同给时 DM_TEST_FG 优先（RC2 草案 §5）
// Level priority: DM_TEST_FG (the per-second in-run switch 0 / 1, 1 = smooth) > DM_FGEN_PRESET > the setting. With both given the test switch wins (RC2 draft §5)
static int fgen_choose_level(int test_level, int preset, int setting) { return test_level >= 0 ? test_level : preset >= 0 ? preset : setting; }
static int fg_schedule(const char *spec, double elapsed) {
    if (!spec || !*spec) return -1;
    int cur = -1; const char *p = spec;
    while (*p) { if (*p != '0' && *p != '1') return -1; int v = *p++ - '0';
        double at = 0; if (*p == '@') { char *end; at = strtod(p + 1, &end); if (end == p + 1 || at < 0) return -1; p = end; }
        if (elapsed >= at || cur < 0) cur = v;
        if (*p == ',') p++; else if (*p) return -1; }
    return cur;
}
static int fg_test_env(const char *env, const char **spec, int *on, uint64_t *t0, double *el) {
    if (*on < 0) { *spec = getenv(env); *on = *spec && fg_schedule(*spec, 0) >= 0; *t0 = (uint64_t)clock_gettime_nsec_np(CLOCK_MONOTONIC); if (*spec && !*on) LOG("【红】%s 非法：%s", env, *spec); }
    if (!*on) return -1;
    *el = ((uint64_t)clock_gettime_nsec_np(CLOCK_MONOTONIC) - *t0) / 1e9; return fg_schedule(*spec, *el);
}
static int fg_test(void) {
    { const fg_seg *s = fg_sched(); if (s) return s->on; }
    static int on = -1; static const char *spec; static uint64_t t0; static _Atomic int last = -1; double el = 0;
    int v = fg_test_env("DM_TEST_FG", &spec, &on, &t0, &el);
    if (v >= 0 && atomic_exchange(&last, v) != v) LOG("测试 DM_TEST_FG：第 %.0f 秒 → 帧生成 %s", el, v ? "开" : "关");
    return v;
}
// 0 = 生效，其余为不生效的原因 | 0 = in effect, otherwise the reason it is not
enum { FG_ON, FG_OFF, FG_UNSUPPORTED, FG_ECO30, FG_CAPPED, FG_RT, FG_ECO_CLOSED, FG_PLATFORM };
static int fg_row_ok(void) { return mtl_fg_supported() && dm_display_max_hz() >= 119 && dm_mem_tier() >= 2; }
// 图形页这一行：平台层（fg_row_show）照键表 k_opts 与平台判定——安卓 / 鸿蒙壳没有这一行；本机层（fg_row_editable）不过门槛时行在、勾选框置灰、副文写原因
// The Graphics row: the platform layer (fg_row_show) follows k_opts and the platform check - the Android / Harmony shell has no such row; the machine layer (fg_row_editable) failing leaves the row, greyed, with the reason as subtext
static int fg_row_show(void) { int i = key_index("fgen_level"); return fg_platform_ok() && i >= 0 && opt_page(i) == PG_GFX; }
static int fg_row_editable(void) { return fg_row_show() && fg_row_ok(); }
static int fg_reason(int level, int row_ok, int fps, int rt_on) {
    if (level == DM_FGEN_OFF) return FG_OFF;
    if (!fg_platform_ok()) return FG_PLATFORM;            // 安卓壳：档位照读、按关生效 | Android shell: the level is read but takes effect as off
    if (level != DM_FGEN_SMOOTH) return FG_ECO_CLOSED;    // 省电枚举保留、本车不开放 | power-saving enum kept, not opened in this train
    if (!row_ok) return FG_UNSUPPORTED;
    if (fps == 30) return FG_ECO30;
    if (fps >= 90) return FG_CAPPED;                      // 高刷档 90 / 120 | the high-refresh stop 90 / 120
    if (rt_on) return FG_RT;
    return FG_ON;
}
// 光追实际开没开（rt_level 生效档 > 0）：DM_RT_PRESET 不在时测试开关 DM_RT_SHADOW 钉住就按它（同 nvn_rtshadow.inc rts_on 与 dm_render_res_rt_drop），否则按 dm_rt_level_setting
// Whether ray tracing is really on (effective rt_level > 0): without DM_RT_PRESET, the DM_RT_SHADOW test pin when given (as rts_on and dm_render_res_rt_drop), otherwise dm_rt_level_setting
static int fg_rt_on(void) { const char *e = getenv("DM_RT_PRESET") ? NULL : getenv("DM_RT_SHADOW"); return e ? atoi(e) != 0 && rt_shadow_ok() : dm_rt_level_setting() > 0; }
int dm_fg_setting(void) {
    int t = fg_test(), preset = fgen_preset(), setting = dm_fgen_level();
    int level = fgen_choose_level(t >= 0 ? (t ? DM_FGEN_SMOOTH : DM_FGEN_OFF) : -1, preset, setting);
    int fps = dm_fps_setting(), sel = fps_selected(), hz = dm_display_max_hz(), rt = fg_rt_on();   // 判定按选的帧率 sel，fps 只进日志 | the decision uses the selected rate sel, fps is for the log only
    static int eco = -1; if (eco < 0) eco = getenv("DM_FG_ECO") != NULL || fg_sched() != NULL;   // 探索开关：渲染 30 + 插到 60（省电方向，真机对比用；产品上 30 帧档仍禁用）| exploration switch: render 30 and interpolate to 60 (power-saving direction, device comparisons only; the product still disables it at 30 fps)
    int why = fg_reason(level, fg_row_ok(), eco && sel < 60 ? 60 : sel, rt);
    static _Atomic int last = -1; int key = why * 4096 + level * 1024 + fps * 2 + (t >= 0);
    if (atomic_exchange(&last, key) != key) {
        // 摘要里的 fgen_level 是设置值（存储的档位），fg 是实际生效状态：安卓壳存储 2 时写 fgen_level=2, fg=0，不把设置值改成 0（主会话 2026-11-03 裁定，设置与生效分离）
        // The summary's fgen_level is the setting (the stored level) and fg the effective state: on the Android shell a stored 2 prints fgen_level=2, fg=0 (ruled by the main session on 2026-11-03: setting and effective state are separate)
        static const char *const reason[] = { "", "开关关着", "本机不支持（插值器 / 屏幕 / 内存档位）", "30 帧档下不可用", "高刷档下不可用", "光追开着", "省电档未开放", "本端暂未接入" };
        char pin[32] = ""; if (level != setting) snprintf(pin, sizeof pin, "（钉住 %d）", level);   // 钉住的档位与设置值不同时写上 | writes the pinned level when it differs from the setting
        LOG("帧生成：状态 fg=%d %s fgen_level=%d%s（%s%s；渲染 %d，显示 %d，屏幕最高 %d Hz；%s）", why == FG_ON, why == FG_ON ? "开" : "关", setting, pin,
            why == FG_ON ? "" : "原因 ", reason[why], fps, why == FG_ON ? fps * 2 : fps, hz,
            t >= 0 ? "测试开关 DM_TEST_FG" : preset >= 0 ? "测试开关 DM_FGEN_PRESET" : "设置");
    }
    return why == FG_ON;
}
// 图形页帧生成行的两种输入（纯函数，t_fgen_level 两种模式各核）：手柄 A / 触屏点勾选框都只在可切时改待定值，勾 = 平滑 2、不勾 = 关 0；置灰（本机不过门槛）时一律不改（返回 -1）
// The two inputs on the Graphics frame-generation row (pure, checked for both modes by t_fgen_level): controller A and a touch on the checkbox change the pending value only when switchable, checked = smooth 2, unchecked = off 0; greyed (machine fails the gate) never changes it (returns -1)
static int fg_pad_press(int editable, int pending) { return editable ? (pending != DM_FGEN_OFF ? DM_FGEN_OFF : DM_FGEN_SMOOTH) : -1; }
static int fg_touch_toggle(int editable, int toggled, int pending) { int to = toggled ? DM_FGEN_SMOOTH : DM_FGEN_OFF; return editable && (to != DM_FGEN_OFF) != (pending != DM_FGEN_OFF) ? to : -1; }
// 帧生成行的副文键（§2.6 副文一句，两种输入模式同一份）：置灰写本机不支持的原因；能生效时写代价，不能生效时写原因（30 帧 / 高刷档 / 光追开 / 省电档 / 退避）
// The frame-generation row's subtext key (one §2.6 subtext line, the same in both input modes): greyed = why this machine is unsupported; otherwise the cost when it can take effect, the reason when not
static const char *fg_note_key(int editable, int why, int pending_on, int backoff) {
    return !editable ? "dmGraphicsFrameGenNoHw"
        : why == FG_ECO_CLOSED ? "dmGraphicsFrameGenEcoClosed" : why == FG_ECO30 ? "dmGraphicsFrameGenNoEco" : why == FG_CAPPED ? "dmGraphicsFrameGenCapped" : why == FG_RT ? "dmGraphicsFrameGenNoRt"
        : pending_on && backoff ? "dmGraphicsFrameGenBackoff" : "dmIosGraphicsFrameGenNote"; }
#else
#define fg_platform_ok() 0   // 安卓壳：没有帧生成 | Android shell: no frame generation
#endif
static void save(void) {
    char p[1100], t[1120]; if (!file_path(p, sizeof p)) { LOG("【红】自定义设置：存档目录没挂上，没存盘"); return; }
    snprintf(t, sizeof t, "%s.tmp", p); FILE *f = fopen(t, "w"); int ok = f != NULL;
    for (int i = 0; ok && i < NK; i++) { if (i == 0 && (atomic_load(&g_lang_explicit) < 0 || g_lang_test)) continue; if (i == K_VOICE && (atomic_load(&g_voice_explicit) < 0 || g_voice_test)) continue; if (key_other_platform(i)) continue; ok = fprintf(f, "%s=%d\n", k_keys[i], atomic_load(&g_val[i])) > 0; }   // lang / voice 只在玩家真改过之后才写进文件
    if (ok) ok = fprintf(f, "rt_shadow=%d\n", atomic_load(&g_val[K_RTLVL]) != 0) > 0;   // 给回退到旧包用（旧包只认 rt_shadow）；本版读盘有 rt_level 时不看它 | for a rollback to an older package (it only knows rt_shadow); this version ignores it when rt_level is present
    if (ok) ok = fprintf(f, "q3d_v2=1\n") > 0;
    if (ok) ok = fprintf(f, "free_tp_v2=1\n") > 0;
    if (ok && atomic_load(&g_ac_ok)) ok = fprintf(f, "auto_crash_ok=1\n") > 0;
    if (ok && atomic_load(&g_tel_notice)) ok = fprintf(f, "tel_notice=%d\n", atomic_load(&g_tel_notice)) > 0;
    if (ok && atomic_load(&g_merc_explicit) >= 0) ok = fprintf(f, "merc_pot_v2=%d\n", atomic_load(&g_merc_explicit)) > 0;
    if (ok && atomic_load(&g_mem_tier_file) >= 0) ok = fprintf(f, "mem_tier=%d\n", atomic_load(&g_mem_tier_file)) > 0;   // 内存档位（首次 / 档位变化才套缺省） | memory tier (defaults apply only on first run / tier change)
    if (ok && atomic_load(&g_mem_notice)) ok = fprintf(f, "mem_notice=1\n") > 0;
    { char bk[96]; size_t o = 0; bk[0] = 0; for (int b = 0; b < N_BAK; b++) if (atomic_load(&g_bak[b]) >= 0) o += (size_t)snprintf(bk + o, sizeof bk - o, "%s%s:%d", o ? "," : "", k_bak_name[b], atomic_load(&g_bak[b]));
      if (ok && o) ok = fprintf(f, "exp_off_backup=%s\n", bk) > 0; }
    if (f) ok = fclose(f) == 0 && ok;
    if (ok) ok = rename(t, p) == 0;   // 先写临时文件再换名：写到一半被杀也不会留下半个文件 | Write a temporary file first and then rename: being killed halfway through the write does not leave half a file behind
    LOG("自定义设置：存盘%s", ok ? "" : "【红】失败");
}
// 内存 0 档的主菜单提示（dm_crashfence.c 借崩溃围栏的游戏内提示框）：还没确认过才提示；玩家点了确定记 mem_notice=1，以后不再提示 | tier-0 main-menu notice (dm_crashfence.c reuses the crash-fence box): only while not yet confirmed; OK saves mem_notice=1 and it never shows again
int dm_custom_mem_notice_due(void) { load_once(); return atomic_load(&g_loaded) && dm_mem_tier() == 0 && !atomic_load(&g_mem_notice); }
void dm_custom_mem_notice_done(void) {
    if (atomic_exchange(&g_mem_notice, 1)) return;
    save(); LOG("内存档位：低内存提示玩家已确认，记进设置文件，以后不再提示");
}
// 崩溃围栏连续两次异常退出时调（dm_crashfence.c）：实验性功能全关并写回设置文件——120 帧档回 60、光追关、MetalFX 空间超分关、AI 帧生成关（安卓壳没有帧生成，那一项不碰）。
// 玩家没开的项不动；改了才存盘。what 写「改了什么」给日志；返回改了几项
// Called by the crash fence after two abnormal exits in a row (dm_crashfence.c): turn every experimental feature off and save - 120 fps mode back to 60, ray tracing off, MetalFX spatial upscaling off, AI frame generation off (the Android shell has none and leaves that entry alone).
// Items the player did not turn on are left alone; the file is saved only if something changed. what receives "what changed" for the log; returns how many items changed
int dm_custom_safe_mode(char *what, size_t n) {
    load_once(); size_t o = 0; int ch = 0; what[0] = 0;
    if (!atomic_load(&g_loaded)) { snprintf(what, n, "设置文件还没法读，没改"); return 0; }
    static const int bk_key[N_BAK] = { K_FPS, K_RTLVL, K_UPSCALE, K_FGEN };   // 改之前记原值（已有备份不覆盖：连着两轮自动关时，第二轮看到的已经是关）；安卓壳不碰帧生成那一项（设置文件跨端互通，不改写） | record the original before changing (an existing backup is kept: on a second automatic pass the value is already off); the Android shell leaves the frame-generation entry alone (shared settings, not rewritten)
    for (int b = 0; b < N_BAK; b++) { if (b == BAK_FGEN && !fg_platform_ok()) continue; int v = atomic_load(&g_val[bk_key[b]]), on = b == 0 ? v == FPS_120 : v != 0; if (on && atomic_load(&g_bak[b]) < 0) atomic_store(&g_bak[b], v); }
    if (atomic_load(&g_val[K_FPS]) == FPS_120) { atomic_store(&g_val[K_FPS], 0); ch++; o += (size_t)snprintf(what + o, o < n ? n - o : 0, "%s120 帧档 → 60", o ? "、" : ""); }
    if (atomic_load(&g_val[K_RTLVL])) { atomic_store(&g_val[K_RTLVL], 0); ch++; o += (size_t)snprintf(what + o, o < n ? n - o : 0, "%s光追 → 关", o ? "、" : ""); }
    if (atomic_load(&g_val[K_UPSCALE])) { atomic_store(&g_val[K_UPSCALE], 0); ch++; o += (size_t)snprintf(what + o, o < n ? n - o : 0, "%s" DM_STR_METALFX " 空间超分 → 关", o ? "、" : ""); dm_3d_quality_mode_set(0); }
#ifndef __ANDROID__
    if (fg_platform_ok() && atomic_load(&g_val[K_FGEN])) { int lv = atomic_load(&g_val[K_FGEN]); atomic_store(&g_val[K_FGEN], DM_FGEN_OFF); ch++; o += (size_t)snprintf(what + o, o < n ? n - o : 0, "%sAI 帧生成 → 关（原档位 %d）", o ? "、" : "", lv); }
#endif
    for (int i = 0; i < NK; i++) atomic_store(&g_pend[i], -1);
    if (!ch) snprintf(what, n, "实验性功能本来都关着");
    else { save(); atomic_fetch_add(&g_repaint, 1); }
    return ch;
}
// 图形页刚打开（dm_graphics_ui_pump）：有崩溃围栏的备份就按原值预选，返回预选了几项 | The Graphics tab just opened (dm_graphics_ui_pump): with a crash-fence backup, pre-select the original values; returns how many were pre-selected
static int exp_restore_pending(void) {
    static const int bk_key[N_BAK] = { K_FPS, K_RTLVL, K_UPSCALE, K_FGEN }; int n = 0;
    // 安卓壳没有帧生成：这一项既不可切，就不预选；备份留在文件里，等 Apple 端打开图形页再恢复（设置文件跨端互通、不改写）
    // The Android shell has no frame generation: the entry is not switchable, so it is not pre-selected; the backup stays in the file for the Apple side to restore later (shared settings, not rewritten)
    // 帧生成一项按界面能选的两态归一（审计 P2-3）：只有平滑 2 预选回 2；省电档 1（枚举保留、本车不开放，生效本来就是关）与其他值按 0——等于当前的关，不预选；备份照留，「应用」时清掉。
    // 不归到 2：那会在连续两次异常退出之后替玩家打开一个他没选过、更重的档位
    // The frame-generation entry is normalised to the two states the page offers (audit P2-3): only smooth 2 pre-selects 2; power saving 1 (reserved, not opened, already off in effect) and anything else
    // count as 0 - equal to the current off, so nothing is pre-selected; the backup stays and Apply clears it. Not 2: that would turn on, right after two abnormal exits, a heavier level the player never chose
    for (int b = 0; b < N_BAK; b++) { if (b == BAK_FGEN && !fg_platform_ok()) continue; int v = atomic_load(&g_bak[b]);
        if (b == BAK_FGEN && v >= 0) { v = v == DM_FGEN_SMOOTH ? DM_FGEN_SMOOTH : DM_FGEN_OFF; if (v == DM_FGEN_OFF) continue; }
        if (v >= 0) { atomic_store(&g_pend[bk_key[b]], v); n++; } }
    if (n) { atomic_fetch_add(&g_repaint, 1); LOG("自定义设置：图形页打开，按崩溃围栏自动关闭前的值预选 %d 项（点应用生效，放弃则保持关闭）", n); }
    return n;
}
// 游戏页分组折叠（方案 C）：Plus 三组与原版表格（整体一组，D2）。缺省收起，同一次运行内记住，不写设置文件 | Gameplay page folding (plan C): the Plus groups and the original table (one unit, D2). Collapsed by default, remembered within a run, not saved
static const char *const k_fold[4] = { "Combat", "Loot", "Ui", "Orig" };
#define GAME_ROWS_Y 660   // 游戏页难度块的下沿（布局 touch_layout.py 的 DIFF_H：标题 / 难度行 / 说明 480 + 「进入恐惧区域」入口行 180）；Plus 分组与原版表格从这里往下摆 | bottom of the Gameplay difficulty block (DIFF_H in the layout touch_layout.py: title / difficulty row / note 480 + the 180-tall Terror Zones entry row); the Plus groups and the original table are stacked from here down
static _Atomic int g_fold_open[4];
static void fold_toggle(const char *g) { for (int i = 0; i < 4; i++) if (!strcmp(g, k_fold[i])) { int on = !atomic_load(&g_fold_open[i]); atomic_store(&g_fold_open[i], on); LOG("选项页：分组 %s %s", g, on ? "展开" : "收起"); } }
void dm_custom_on_uimsg(const char *cat, const char *act, const char *arg) {
    if (cat && act && arg && !strcmp(cat, "PanelManager") && !strcmp(act, "ClosePanel") && !strncmp(arg, "TouchFold", 9)) { fold_toggle(arg + 9); return; }   // 游戏页分组折叠钮 | Gameplay page fold buttons
    if (!cat || !act || strcmp(cat, "OptionsPanelMessage")) return;
    load_once();
    if (!strcmp(act, "AcceptChanges")) { int ch = 0, res_changed = 0, ac0 = atomic_load(&g_val[K_AUTOCRASH]), ts0 = atomic_load(&g_val[K_TELSTATS]);
        // Migrate the former fifth radio option to the independent enable switch.
        if (atomic_load(&g_pend[K_Q3D]) == 4) { atomic_store(&g_pend[K_Q3D], 0); atomic_store(&g_pend[K_UPSCALE], 0); }
        for (int i = 0; i < NK; i++) { int pd = atomic_exchange(&g_pend[i], -1); if (i == K_FGEN && !fg_platform_ok()) continue;   // 安卓壳不改写共享档位（待定值照样丢掉）| the Android shell does not rewrite the shared level (the pending value is still dropped)
            if (pd >= 0 && pd != atomic_load(&g_val[i])) { atomic_store(&g_val[i], pd); ch = 1; if (i == K_RES) res_changed = 1; if (i == K_Q3D) { int dm_3d_quality_mode_get(void); float dm_3d_scale_cur(void); dm_3d_quality_mode_set(pd == 3); LOG("自定义设置：3D 画质 → %s（已交给 3D 比例控制，读回模式 %d，此刻 3D 比例 %.2f）", pd == 3 ? "Quality" : "Adaptive", dm_3d_quality_mode_get(), dm_3d_scale_cur()); } if (i == 0) atomic_store(&g_lang_explicit, pd); if (i == K_VOICE) atomic_store(&g_voice_explicit, pd); if (i == K_MERC) atomic_store(&g_merc_explicit, pd); if (i == 0) LOG("自定义设置：语言 → %s（应用；下次启动生效）", k_lang_name[pd]); else if (i == K_VOICE) LOG("自定义设置：配音 → %s（应用；下次启动生效）", pd ? "English" : "中文"); else if (i == K_RTLVL) LOG("自定义设置：光追档位 → %d（应用）", pd); else if (i == K_FGEN) LOG("自定义设置：%s → %d（应用）", k_keys[i], pd); else LOG("自定义设置：%s → %s（应用）", k_keys[i], pd ? "开" : "关"); } }
        dm_3d_quality_mode_set(atomic_load(&g_val[K_UPSCALE]) && q3d_for_fps(atomic_load(&g_val[K_Q3D]), dm_fps_setting()) == 3);   // 120 帧下高画质按均衡 | Quality runs as Balanced at 120 fps
        { int had = 0; for (int b = 0; b < N_BAK; b++) { if (b == BAK_FGEN && !fg_platform_ok()) continue; had |= atomic_exchange(&g_bak[b], -1) >= 0; }   // 应用 = 玩家已经决定：崩溃围栏的备份用完即清；安卓壳不清帧生成那一项（没有这一行、档位也不可切） | Apply = the player has decided: the crash-fence backup is cleared; the Android shell keeps the frame-generation entry
          if (had) { ch = 1; LOG("自定义设置：应用，清掉崩溃围栏的实验项备份"); } }
        if (res_changed) res_notify();
        { int ac = atomic_load(&g_val[K_AUTOCRASH]); if (ac && atomic_exchange(&g_ac_ok_pending, 0) && !atomic_exchange(&g_ac_ok, 1)) ch = 1;   // 同意随「应用」生效 | consent takes effect with Apply
          if (ac != ac0) dm_crashreport_setting_changed(ac);
          int ts = atomic_load(&g_val[K_TELSTATS]); if (ts != ts0) dm_telemetry_setting_changed(ts); }
        atomic_fetch_add(&g_repaint, 1);
        if (ch) save(); }
    else if (!strcmp(act, "CancelChanges")) { for (int i = 0; i < NK; i++) atomic_store(&g_pend[i], -1); atomic_store(&g_ac_ok_pending, 0); atomic_fetch_add(&g_repaint, 1); }
    else if (!strcmp(act, "ResetToDefaults")) {
        // Preserve upstream global defaults across the two enhanced tabs. Language/voice stay unchanged.
        for (int i = 2; i < NK; i++) { if (i == K_FGEN && !fg_platform_ok()) continue; atomic_store(&g_pend[i], tier_def(i, dm_mem_tier())); }   // 安卓壳不动帧生成（共享设置，不改写）；从 2 起：语言 / 配音两行不跟「默认」走 | from 2: the language / voice rows do not follow Reset. This memory tier's defaults
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
// 置灰（光追 AO 在光追阴影没开时）：只显示「禁用」帧，点了也不算数（dm_graphics_ui_pump 每帧摆回） | greyed out (ray-traced AO while ray-traced shadows are off): shows only the disabled frame, taps do not count (dm_graphics_ui_pump puts it back every frame)
static void set_toggle_grey(uint64_t w) {
    if (!DM_WIDGET_OK(main_base(), w, DM_VT_TOGGLE, "图形页")) return;
    static const int32_t fr[4] = { 2, 2, 2, 2 };
    *(uint8_t *)(w + 0xa88) = 0; memcpy((void *)(w + 0x6e8), fr, sizeof fr); *(int32_t *)(w + 0x610) = 2;
}
static int pending_value(int i) { int p = atomic_load(&g_pend[i]); return p >= 0 ? p : atomic_load(&g_val[i]); }
// 内存 1 / 0 档：图形页把渲染分辨率调到比启动时高的档 = 启动预留已按启动档缩小，要下次启动才生效（分辨率行副文 TouchGraphicsResNote）
// memory tiers 1 / 0: a render-resolution level above the launch level on the Graphics tab only takes effect on the next launch, because the startup reserve was shrunk (subtext TouchGraphicsResNote)
static int restart_pending(int i) { return g_boot_ok && pending_value(i) != g_boot_v[i]; }
static int rt_drop_restart(void) { return g_boot_ok && (pending_value(K_RTLVL) > 0) != (g_boot_v[K_RTLVL] > 0); }
static int key_idx(const char *k) { for (int i = 0; i < NK; i++) if (!strcmp(k_keys[i], k)) return i; return -1; }
static int res_raise_needs_restart(void) { int b = atomic_load(&g_boot_res); return dm_mem_tier() < 2 && b >= 0 && res_rank(pending_value(K_RES)) > res_rank(b); }
static int enhanced_settings_dirty(void) {
    for (int i=0;i<NK;i++) if (pending_value(i)!=atomic_load(&g_val[i])) return 1;
    return 0;
}
// 帧率滑块的格：0 = 30、1 = 60、2 = 高刷（屏幕撑得住 89 Hz 以上才有第三格）；格 ↔ eco_fps 值（存盘仍是 2，90 与 120 共用）。存着第三档、屏幕不到 89 Hz 时停在 60 那格，不当成改动
// Frame-rate slider stops: 0 = 30, 1 = 60, 2 = high refresh (the third stop exists only when the screen can do 89 Hz or more); stop <-> eco_fps value (still saved as 2; 90 and 120 share it). A saved third stop on a screen below 89 Hz sits on the 60 stop and does not count as a change
static int fps_max_pos(void) { return fps_high_ok() ? 2 : 1; }
static int fps_pos(int v) { return v == 1 ? 0 : v == FPS_120 && fps_high_ok() ? 2 : 1; }
static int fps_val(int pos) { return pos == 0 ? 1 : pos == 2 ? FPS_120 : 0; }
static void graphics_select_fps(int pos) {
    if (pos < 0 || pos > fps_max_pos() || pos == fps_pos(pending_value(K_FPS))) return;
    atomic_store(&g_pend[K_FPS], fps_val(pos));
    LOG("图形设置：帧率 → %d FPS（点应用生效）", pos == 0 ? 30 : pos == 2 ? fps_high_hz() : 60);
}
// 图形页的档位滑块：高刷档（120）选着时只到均衡（q3d_for_fps），显示的是落到均衡后的档；读回显示档不算改动，选高画质被挡住。90 帧不设这条上限
// The Graphics profile slider: with the high-refresh stop (120) selected it stops at Balanced (q3d_for_fps) and shows the capped profile; reading back the shown profile is not a change, picking Quality is refused. The 90 fps stop has no such cap
static int profile_fps_pending(void) { return fps_pos(pending_value(K_FPS)) == 2 ? fps_high_hz() : 60; }
static int profile_max(void) { return q3d_for_fps(3, profile_fps_pending()); }
static int profile_shown(void) { return q3d_for_fps(pending_value(K_Q3D), profile_fps_pending()); }
static void graphics_select_profile(int value) {
    if (value < 0 || value > profile_max() || value == profile_shown()) return;
    atomic_store(&g_pend[K_Q3D], value);
    LOG("图形设置：" DM_STR_METALFX " 档位 → %d（点应用生效）", value);
}
static void graphics_select_bright(int value) {
    if (value < 0 || value > 3 || value == pending_value(K_BRIGHT)) return;
    atomic_store(&g_pend[K_BRIGHT], value);
    LOG("图形设置：画面加亮档位 → %d（点应用生效）", value);
}
static void graphics_select_res(int value) {
    if (value < 0 || value >= DM_RES_LEVELS || value == pending_value(K_RES)) return;
    atomic_store(&g_pend[K_RES], value);
    LOG("图形设置：渲染分辨率档位 → %d（点应用生效）", value);
}
static void graphics_select_rt(int value, int max) {
    if (value < 0 || value > max || value == pending_value(K_RTLVL)) return;
    atomic_store(&g_pend[K_RTLVL], value);
    LOG("图形设置：光追档位 → %d（点应用生效）", value);
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
// 行文字运行时右移、选中框左缘不进缺口带（规则与实测见 dm_sel_frame.h）：只动我们的（Touch 开头）、载入时 x = 0 的叶子控件，行容器（Touch…Row）往里找一层
// Row text moves right at run time and the selection frame's left edge stays out of the gap band (rule and measurements: dm_sel_frame.h): only our (Touch-prefixed)
// leaf widgets loaded at x = 0 move; row containers (Touch...Row) are searched one level down
static void indent_rows(uint64_t w, int dep) {
    if (!w || dep > 2) return; uint64_t ch = *(uint64_t *)(w + 0x58), n = *(uint64_t *)(w + 0x60); if (!ch || n > 256) return;
    for (uint64_t i = 0; i < n; i++) { uint64_t c = *(uint64_t *)(ch + 8 * i); const char *nm = c ? *(const char **)(c + 8) : NULL; if (!nm || strncmp(nm, "Touch", 5)) continue;
        uint64_t kids = *(uint64_t *)(c + 0x58) ? *(uint64_t *)(c + 0x60) : 0; size_t l = strlen(nm);
        if (kids) { if (l > 3 && !strcmp(nm + l - 3, "Row")) indent_rows(c, dep + 1); continue; }
        int32_t *r = (int32_t *)(c + 0x70); int x = dm_row_indent_x(r[0], 1);
        if (x != r[0]) { r[0] = x; ((void (*)(uint64_t))*(uint64_t *)(*(uint64_t *)c + 0x10))(c); } }
}
static void move_ours_y(uint64_t w, int y) {
    const char *name = w ? *(const char **)(w + 8) : NULL;
    if (!name || strncmp(name, "Touch", 5)) { if (w) LOG("【红】图形设置：拒绝挪动原版控件"); return; }
    int32_t *r = (int32_t *)(w + 0x70); if (r[1] == y) return;
    r[1] = y; ((void (*)(uint64_t))*(uint64_t *)(*(uint64_t *)w + 0x10))(w);
}
void dm_graphics_ui_pump(unsigned long long root) {
    if (!root || !main_base()) return;
    static dm_wref seen[10]; static int paint;
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
    // 分辨率行、超分开关（文字 + 勾选）在不支持的平台整组藏起来（Android）；藏了就不参与交互 | the resolution row and the upscaling switch (text + checkbox) are hidden as a group on platforms without them (Android); hidden = no interaction
    // 两层门控（用户 2026-10-30）：平台不符不出行（安卓没有 MetalFX / 光追）；本机不支持的行还在，置灰 + 一句说明 | two-layer gating (user 2026-10-30): no row on the wrong platform (Android has no MetalFX / ray tracing); on an unsupported machine the row stays, greyed, with one line of explanation
    int res_ok = dm_render_res_supported(), up_ok = mtl_upscale_supported(), up_show, rt_show; graphics_plat_rows(&up_show, &rt_show);
    { uint64_t resrow = find_w(sp, "TouchGraphicsResRow", 0); if (resrow) ui_show_ours(resrow, res_ok);
      uint64_t uptext = find_w(sp, "TouchGraphicsUpscaleText", 0); if (uptext) ui_show_ours(uptext, up_show); uint64_t upn = find_w(sp, "TouchGraphicsUpscaleNoHw", 0); if (upn) ui_show_ours(upn, up_show && !up_ok); }
    uint64_t upscale = find_w(sp, "TouchGraphicsUpscale", 0);
    if (upscale) ui_show_ours(upscale, up_show);
    if (!up_ok) { if (upscale && up_show) set_toggle_grey(upscale); upscale = 0; }
    if (upscale) {
        int want=pending_value(K_UPSCALE);
        if (dm_wref_get(&seen[1])!=upscale) { dm_wref_set(&seen[1],upscale); set_toggle(upscale,want); }
        else if (TOGGLED(upscale)!=want) atomic_store(&g_pend[K_UPSCALE],TOGGLED(upscale));
    }
    // 光追档位（实验性）：关 / 低 / 中 / 高四档滑块。没有硬件光追的机器置灰：滑块藏起来，文字换灰色那份，副文写明原因；安卓整行不出现（§2.8，键表 k_opts 平台列，布局里也没有这一行）
    // Ray-tracing level (experimental): four-position slider Off / Low / Medium / High. Machines without hardware ray tracing grey it out: slider hidden, the grey caption shown, the subtext says why; on Android the whole row is absent (§2.8; the layout has no such row either)
    int rt_row = rt_show;   // 平台层照键表 k_opts（rt_level 只有 Apple）| platform layer from k_opts (rt_level is Apple only)
    int rt_ok = rt_row && rt_shadow_ok(); uint64_t rtrow = find_w(sp, "TouchGraphicsRtRow", 0); if (rtrow) ui_show_ours(rtrow, rt_row);
    uint64_t rtl = find_w(sp, "TouchGraphicsRtLevel", 0); if (rtl) ui_show_ours(rtl, rt_ok); if (!rt_ok) rtl = 0;
#ifndef __ANDROID__
    // AI 帧生成（实验性，「帧率」组、帧率滑块下一行）：平台层照键表（安卓整行不出现，布局里也没有）；本机不过门槛（插值器 / 120 Hz 屏 / 内存档位）时行在、勾选框置灰、A 不改待定值、副文写原因。
    // 勾选框只做关 / 平滑两态：勾上 = fgen_level 2、取消 = 0（省电档不出现，等 RC2 之后的数据）
    // AI frame generation (experimental, Frame group, the row under the frame-rate slider): the platform layer follows the key table (absent on Android, not in its layout either); a machine failing the gate
    // (interpolator / 120 Hz screen / memory tier) keeps the row with the checkbox greyed, A changing nothing and the reason as subtext. Two states only: checked = fgen_level 2, unchecked = 0
    int fg_show = fg_row_show(), fg_edit = fg_row_editable();
    uint64_t fgrow = find_w(sp, "TouchGraphicsFrameGenRow", 0); if (fgrow) ui_show_ours(fgrow, fg_show);
    uint64_t fg = fg_show ? find_w(sp, "TouchGraphicsFrameGen", 0) : 0;
    if (fg && !fg_edit) { set_toggle_grey(fg); fg = 0; }
    if (fg) {
        int want = pending_value(K_FGEN) != DM_FGEN_OFF;
        if (dm_wref_get(&seen[8]) != fg) { dm_wref_set(&seen[8], fg); set_toggle(fg, want); }
        else { int to = fg_touch_toggle(fg_edit, TOGGLED(fg), pending_value(K_FGEN)); if (to >= 0) atomic_store(&g_pend[K_FGEN], to); }
    }
#else
    int fg_show = 0;   // 安卓没有这一行 | no such row on Android
#endif
    // 显示帧数（fps_hud，图形页第二行，缺省开）：勾选框与超分开关同一套处理 | Show frame rate (fps_hud, Graphics row 2, on by default): the checkbox is handled like the upscale switch
    uint64_t fpshud = find_w(sp, "TouchGraphicsFpsHud", 0);
    if (fpshud) {
        int want = pending_value(K_FPSHUD);
        if (dm_wref_get(&seen[5]) != fpshud) { dm_wref_set(&seen[5], fpshud); set_toggle(fpshud, want); }
        else if (TOGGLED(fpshud) != want) atomic_store(&g_pend[K_FPSHUD], TOGGLED(fpshud));
    }
    // 动态 3D 分辨率（B147 ②，键 dyn3d，「显示」组、加亮下一行，缺省关）：勾选框与显示帧数同一套处理；两端都有（hle_oe.c 双端共用）| Dynamic 3D resolution (B147 ②, key dyn3d, Display group under brightening, off by default): handled like Show frame rate; on both platforms (hle_oe.c is shared)
    uint64_t dyn3d = find_w(sp, "TouchGraphicsDyn3d", 0);
    if (dyn3d) {
        int want = pending_value(K_DYN3D);
        if (dm_wref_get(&seen[9]) != dyn3d) { dm_wref_set(&seen[9], dyn3d); set_toggle(dyn3d, want); }
        else if (TOGGLED(dyn3d) != want) atomic_store(&g_pend[K_DYN3D], TOGGLED(dyn3d));
    }
    int dm_padmode_current(void); uint64_t dm_padmode_page_keys(uint64_t);
    static int selected; static dm_wref focus_panel;
    if (dm_wref_get(&focus_panel) != sp) { dm_wref_set(&focus_panel, sp); selected = 0; }
    int pad = dm_padmode_current() && !dm_ui_modal_active(root);
    uint64_t ev = pad ? dm_padmode_page_keys(DM_BTN_UP | DM_BTN_DOWN | DM_BTN_A | DM_BTN_LEFT | DM_BTN_RIGHT) : 0;
    // 行（与 touch_layout.py _graphics_page 同序，用户 2026-10-27）：0 渲染分辨率 / 1 帧率 / 2 超分开关 / 3 超分档位（开关开着才有）/ 4 光追档位（实验性，只有 Apple；机器不过门槛时置灰）。
    // 可见行紧挨着排：档位行藏起来时光追行上移一格补位（下面按可见行数挪光追行与说明行），所以选中框按可见序号 selected 放，行为按布局行号 row_id 走
    // Rows (same order as touch_layout.py _graphics_page, user 2026-10-27): 0 render resolution / 1 frame rate / 2 upscale switch / 3 upscale profile (only while the switch is on) / 4 ray-tracing level (experimental, Apple only; greyed on machines that fail the gate).
    // Visible rows are packed: with the profile row hidden, the ray-tracing row moves up one slot (the ray-tracing row and the hint are moved below by the visible row count), so the selection frame follows the visible index selected and behaviour follows the layout row row_id
    int prof_on = up_ok && pending_value(K_UPSCALE) != 0, vis[9], rows = graphics_rows_pack(res_ok, up_show, prof_on, rt_show, fg_show, vis);
    if (ev & DM_BTN_DOWN) selected++; else if (ev & DM_BTN_UP) selected--;
    if (selected < 0) selected = 0; if (selected >= rows) selected = rows - 1;
    int row_id = vis[selected];
#ifndef __ANDROID__
    if ((ev & DM_BTN_A) && row_id == ROW_FG && fg) { int to = fg_pad_press(fg_edit, pending_value(K_FGEN)); if (to >= 0) { atomic_store(&g_pend[K_FGEN], to); set_toggle(fg, to != DM_FGEN_OFF); } }   // 置灰时 fg = 0：A 不改 | greyed: fg = 0, A does nothing
#endif
    if ((ev & DM_BTN_A) && row_id == ROW_DYN3D && dyn3d) { int to = !pending_value(K_DYN3D); atomic_store(&g_pend[K_DYN3D], to); set_toggle(dyn3d, to); }
    if ((ev & DM_BTN_A) && row_id == ROW_FPSHUD && fpshud) { int to = !pending_value(K_FPSHUD); atomic_store(&g_pend[K_FPSHUD], to); set_toggle(fpshud, to); }
    if (row_id == ROW_FPS && fps_ok && (ev & (DM_BTN_LEFT | DM_BTN_RIGHT))) {
        int to = fps_pos(pending_value(K_FPS)) + ((ev & DM_BTN_RIGHT) ? 1 : -1);
        if (to>=0 && to<=fps_max_pos()) { graphics_select_fps(to);
            ((void (*)(uint64_t, int, int, int))(main_base() + 0xad7028))(fps,to,0,0); }
    }
    if ((ev & DM_BTN_A) && row_id == ROW_UPSCALE && up_ok) {   // 本机不支持 MetalFX 时这一行置灰：A 不改待定值（审计 r1 P2-2） | greyed on machines without MetalFX: A does not change the pending value (audit r1 P2-2)
        int to=!pending_value(K_UPSCALE); atomic_store(&g_pend[K_UPSCALE],to);
        if (upscale) set_toggle(upscale,to);
    }
    int enabled = prof_on;
    uint64_t row = find_w(sp, "TouchGraphicsProfileRow", 0);
    if (row) ui_show_ours(row, enabled);
    // 可见行紧挨着排：第 i 个可见行放 y = 180 × (i + 1)（文字 / 容器），滑块与勾选框再 +55（touch_layout.py 的 label / toggle 排法）；说明行接在最后一个可见行下面。
    // 全部可见时各行正好在布局原位（180 / 360 / 540 / 720 / 900），Apple 行为不变
    // Visible rows are packed: visible row i sits at y = 180 x (i + 1) (text / container), sliders and checkboxes at +55 (touch_layout.py's label / toggle placement); the hint follows the last visible row.
    // With every row visible each sits at its layout position (180 / 360 / 540 / 720 / 900): Apple unchanged
    { int all_vis[9], nv = graphics_rows_pack(res_ok, up_show, enabled, rt_show, fg_show, all_vis);
      static const char *const heads[4] = { NULL, "TouchOptHeadFrame", "TouchOptHeadUpscale", "TouchOptHeadRt" };
      for (int g = 1; g < 4; g++) { uint64_t hw = find_w(sp, heads[g], 0); int on = 0; for (int i = 0; i < nv; i++) if (graphics_row_group(all_vis[i]) == g) { if (hw) move_ours_y(hw, graphics_row_y(all_vis, i) - 180); on = 1; break; } if (hw) ui_show_ours(hw, on); }
      for (int i = 0; i < nv; i++) { int y = graphics_row_y(all_vis, i);
        switch (all_vis[i]) {
        case ROW_BRIGHT: move_ours_y(find_w(sp, "TouchGraphicsBrightRow", 0), y); break;
        case ROW_RES: move_ours_y(find_w(sp, "TouchGraphicsResRow", 0), y); break;
        case ROW_DYN3D: move_ours_y(find_w(sp, "TouchGraphicsDyn3dRow", 0), y); break;
        case ROW_FPSHUD: move_ours_y(find_w(sp, "TouchGraphicsFpsHudRow", 0), y); break;
        case ROW_FPS: move_ours_y(find_w(sp, "TouchGraphicsFpsText", 0), y); if (fps) move_ours_y(fps, y + 55); move_ours_y(find_w(sp, "TouchGraphicsFpsNote", 0), y + 116); break;
        case ROW_UPSCALE: move_ours_y(find_w(sp, "TouchGraphicsUpscaleText", 0), y); { uint64_t ub = find_w(sp, "TouchGraphicsUpscale", 0); if (ub) move_ours_y(ub, y + 55); } move_ours_y(find_w(sp, "TouchGraphicsUpscaleNoHw", 0), y + 116); break;
        case ROW_PROFILE: if (row) move_ours_y(row, y); break;
        case ROW_RT: if (rtrow) move_ours_y(rtrow, y); break;
#ifndef __ANDROID__
        case ROW_FG: if (fgrow) move_ours_y(fgrow, y); break;
#endif
        } }
      move_ours_y(find_w(sp, "TouchGraphicsApplyHint", 0), graphics_row_y(all_vis, nv - 1) + 180); }
    uint64_t slider = find_w(sp, "TouchGraphicsProfile", 0);
    if (slider && DM_WIDGET_OK(main_base(), slider, DM_VT_SLIDER, "图形设置滑块")) {
        // 3.3.94012: render main+aef3cc reads min/max/current here; main+ad7028
        // clamps and moves the indicator. Native onUpdateMessage has no numeric payload.
        if (dm_wref_get(&seen[2]) != slider) {
            *(int32_t *)(slider + 0x5b0) = 1;
            *(int32_t *)(slider + 0x5b8) = profile_max();
            *(int32_t *)(slider + 0x5bc) = 0;
            *(int32_t *)(slider + 0x5c0) = profile_max();
            ((void (*)(uint64_t, int, int, int))(main_base() + 0xad7028))(slider, profile_shown(), 0, 0);
            dm_wref_set(&seen[2], slider);
        } else if (*(int32_t *)(slider + 0x5b8) != profile_max()) {
            // 帧率格换到 / 换离 120：滑块上限在均衡 / 高画质之间换（同上面刚出现时的写法），指示块摆到显示档；这一帧不读回 | the frame-rate stop moved to / away from 120: the slider maximum switches between Balanced / Quality (written as on first sight above) and the indicator goes to the shown profile; not read back this frame
            *(int32_t *)(slider + 0x5b8) = profile_max(); *(int32_t *)(slider + 0x5c0) = profile_max();
            ((void (*)(uint64_t, int, int, int))(main_base() + 0xad7028))(slider, profile_shown(), 0, 0);
            LOG("图形设置：超分档位滑块上限 → %s（帧率格 %d）", profile_max() == 3 ? "高画质" : "均衡", profile_fps_pending());
        } else if (enabled) graphics_select_profile(*(int32_t *)(slider + 0x5c4));
        if (enabled && row_id == ROW_PROFILE && (ev & (DM_BTN_LEFT | DM_BTN_RIGHT))) {
            int to = profile_shown() + ((ev & DM_BTN_RIGHT) ? 1 : -1);
            if (to >= 0 && to <= profile_max()) { graphics_select_profile(to);
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
    if (rtl && DM_WIDGET_OK(main_base(), rtl, DM_VT_SLIDER, "图形设置光追档位滑块")) {
        int mx = rt_level_cap(), lv = pending_value(K_RTLVL) > mx ? mx : pending_value(K_RTLVL);
        if (dm_wref_get(&seen[4]) != rtl) {
            *(int32_t *)(rtl + 0x5b0) = 1; *(int32_t *)(rtl + 0x5b8) = mx;
            *(int32_t *)(rtl + 0x5bc) = 0; *(int32_t *)(rtl + 0x5c0) = mx;
            ((void (*)(uint64_t, int, int, int))(main_base() + 0xad7028))(rtl, lv, 0, 0);
            dm_wref_set(&seen[4], rtl);
        } else graphics_select_rt(*(int32_t *)(rtl + 0x5c4), mx);
        if (row_id == ROW_RT && (ev & (DM_BTN_LEFT | DM_BTN_RIGHT))) {
            int to = lv + ((ev & DM_BTN_RIGHT) ? 1 : -1);
            if (to >= 0 && to <= mx) { graphics_select_rt(to, mx);
                ((void (*)(uint64_t, int, int, int))(main_base() + 0xad7028))(rtl, to, 0, 0); }
        }
    }
    if (rt_row) { static const char *const rt_captions[4] = { "dmGraphicsRtLevelOff", "dmGraphicsRtLevelLow", "dmGraphicsRtLevelMid", "dmGraphicsRtLevelHigh" };
      int mx = rt_level_cap(), lv = pending_value(K_RTLVL) > mx ? mx : pending_value(K_RTLVL), capped = rt_ok && mx < 3; uint64_t w;
      graphics_text(find_w(sp, "TouchGraphicsRtText", 0), dm_str(main_base(), rt_captions[lv < 0 ? 0 : lv]));
      if ((w = find_w(sp, "TouchGraphicsRtText", 0))) ui_show_ours(w, rt_ok);
      if ((w = find_w(sp, "TouchGraphicsRtTextOff", 0))) ui_show_ours(w, !rt_ok);
      if ((w = find_w(sp, "TouchGraphicsRtNote", 0))) ui_show_ours(w, rt_ok && !capped);
      if ((w = find_w(sp, "TouchGraphicsRtHighNote", 0))) ui_show_ours(w, capped);
      if ((w = find_w(sp, "TouchGraphicsRtNoHw", 0))) ui_show_ours(w, !rt_ok); }
    uint64_t bright = find_w(sp, "TouchGraphicsBright", 0);
    if (bright && DM_WIDGET_OK(main_base(), bright, DM_VT_SLIDER, "图形设置加亮滑块")) {
        if (dm_wref_get(&seen[7]) != bright) {
            *(int32_t *)(bright + 0x5b0) = 1; *(int32_t *)(bright + 0x5b8) = 3;
            *(int32_t *)(bright + 0x5bc) = 0; *(int32_t *)(bright + 0x5c0) = 3;
            ((void (*)(uint64_t, int, int, int))(main_base() + 0xad7028))(bright, pending_value(K_BRIGHT), 0, 0);
            dm_wref_set(&seen[7], bright);
        } else graphics_select_bright(*(int32_t *)(bright + 0x5c4));
        if (row_id == ROW_BRIGHT && (ev & (DM_BTN_LEFT | DM_BTN_RIGHT))) {
            int to = pending_value(K_BRIGHT) + ((ev & DM_BTN_RIGHT) ? 1 : -1);
            if (to >= 0 && to <= 3) { graphics_select_bright(to);
                ((void (*)(uint64_t, int, int, int))(main_base() + 0xad7028))(bright, to, 0, 0); }
        }
    }
    { static const char *const bright_captions[4] = { "dmGraphicsBrightOff", "dmGraphicsBrightLow", "dmGraphicsBrightMid", "dmGraphicsBrightHigh" };
      int lv = pending_value(K_BRIGHT); if (lv < 0 || lv > 3) lv = 2;
      graphics_text(find_w(sp, "TouchGraphicsBrightText", 0), dm_str(main_base(), bright_captions[lv])); }
    { static const char *const res_captions[DM_RES_LEVELS] = { "dmGraphicsResAuto", "dmGraphicsResEco", "dmGraphicsResBalanced", "dmGraphicsResNative" };
      int lv = pending_value(K_RES); if (lv < 0 || lv >= DM_RES_LEVELS) lv = 0;
      graphics_text(find_w(sp, "TouchGraphicsResText", 0), dm_str(main_base(), res_captions[lv])); }
    { static const char *const fps_captions[3] = { "dmGraphicsFrameRate30", "dmGraphicsFrameRate60", "dmGraphicsFrameRateHigh" };
      int fpos = fps_pos(pending_value(K_FPS));
      if (fpos == 2) { char t[DM_FPS_CAPTION_MAX]; snprintf(t, sizeof t, dm_str(main_base(), fps_captions[2]), fps_high_hz()); graphics_text(find_w(sp, "TouchGraphicsFpsText", 0), t); }   // 第三格按本机高刷档写 90 / 120 | the third stop shows 90 / 120, this machine's high-refresh rate
      else graphics_text(find_w(sp, "TouchGraphicsFpsText", 0), dm_str(main_base(), fps_captions[fpos])); }
#ifndef __ANDROID__   // 安卓没有这一行（§2.8）：副文键与文案选择都不编进安卓包 | no such row on Android (§2.8): the subtext keys and choice stay out of the Android build
    // AI 帧生成的副文（§2.6 副文一句）：本机不支持写原因；能生效时写代价，不能生效时写原因（按图形页上选着、还没应用的值算：30 帧 / 高刷档 / 光追开 / 过热 / 退避）
    // AI frame generation subtext: unsupported machines get the reason; otherwise the cost when it can take effect, the reason when not (from the pending values on the page: 30 fps / high refresh / ray tracing on / hot / back-off)
    if (fg_show) { int fpos = fps_pos(pending_value(K_FPS)), fps_p = fpos == 0 ? 30 : fpos == 2 ? fps_high_hz() : 60;
        int why = fg_reason(pending_value(K_FGEN), 1, fps_p, rt_ok && pending_value(K_RTLVL) > 0);
        graphics_text(find_w(sp, "TouchGraphicsFrameGenNote", 0), dm_str(main_base(), fg_note_key(fg_edit, why, pending_value(K_FGEN) != DM_FGEN_OFF, mtl_fg_backoff())));
    }
#endif
    indent_rows(find_w(sp, "TouchGraphicsRows", 0), 0);
    uint64_t focus = find_w(sp, "TouchGraphicsSel", 0);
    if (focus) { int32_t *r = (int32_t *)(focus + 0x70), x = pad ? dm_sel_frame_x(DM_ROW_INDENT) : 99999, y = pad ? graphics_row_y(vis, selected) - 10 : 0;
        if (r[0] != x || r[1] != y) { r[0] = x; r[1] = y; ((void (*)(uint64_t))*(uint64_t *)(*(uint64_t *)focus + 0x10))(focus); } }
    // 这页行数超过一屏（方案 C 分组标题）：手柄选到框外的行时滚到位，同自定义页（滚动量取法、到头判定见下面 custom 页那段）
    // This page now has more rows than fit (plan C group headings): scroll to the controller's row, same as the Custom page (offset reading and end detection: see that block below)
    if (pad) { uint64_t rw = find_w(sp, "TouchGraphicsRows", 0), view = rw ? *(uint64_t *)(rw + 0x30) : 0, sv = view ? *(uint64_t *)(view + 0x30) : 0, dm_guest_frame(void), f = dm_guest_frame(); static uint64_t lf;
        static int last_dir, last_off = -1, last_sel = -1; if (selected != last_sel) { last_sel = selected; last_dir = 0; last_off = -1; }
        if (sv && f - lf >= 4) { int top = selected ? graphics_row_y(vis, selected) - 20 : 0, bot = graphics_row_y(vis, selected) + 180, off = -*(int32_t *)(view + 0x74), vh = *(int32_t *)(sv + 0x7c); if (vh <= 0) vh = 1260; void dm_ui_wheel_on(uint64_t, int);
            int dir = dm_custom_scroll_step(top, bot, off, vh, last_dir, last_off);
            if (dir) { lf = f; last_dir = dir; last_off = off; dm_ui_wheel_on(sv, dir); } } }
    static const char *const captions[] = { "dmGraphicsProfileAuto", "dmGraphicsProfileEco", "dmGraphicsProfileBalanced", "dmGraphicsProfileQuality" };
    int profile = profile_shown(); if (profile < 0 || profile > 3) profile = 0;
    graphics_text(find_w(sp, "TouchGraphicsProfileText", 0), dm_str(main_base(), captions[profile]));
    // 120 帧格选着时档位行下的灰色副文「120 帧下不提供高画质」（布局 touch_layout.py TouchGraphicsProfileNote，缺省藏着）| at the 120 fps stop, the grey subtext under the profile row (layout TouchGraphicsProfileNote, hidden by default)
    { uint64_t note = find_w(sp, "TouchGraphicsProfileNote", 0); if (note) ui_show_ours(note, enabled && profile_max() < 3); }
    // 内存 1 / 0 档：屏幕撑得住 120 却不开放时，帧率行下一行灰色副文说明（布局 TouchGraphicsFpsNote，缺省藏着）| memory tiers 1 / 0: when the screen could do 120 but it is not offered, a grey subtext line under the frame-rate row (layout TouchGraphicsFpsNote, hidden by default)
    // B147 起不再有「过热暂按 60 帧」这一句（帧率不按温控退回）| from B147 there is no "running at 60 while hot" line (the rate no longer falls back on heat)
    { uint64_t note = find_w(sp, "TouchGraphicsFpsNote", 0);
      if (note) { int mem = fps_ok && fps_mem_capped();
          if (mem) graphics_text(note, dm_str(main_base(), "dmGraphicsFps120MemNote"));
          ui_show_ours(note, mem); } }
    { uint64_t note = find_w(sp, "TouchGraphicsResNote", 0); int raise = res_ok && res_raise_needs_restart(), rt = res_ok && rt_drop_restart();
      if (note) { int k = raise ? 1 : rt ? 2 : 0; if (k) graphics_text(note, dm_str(main_base(), k == 1 ? "dmGraphicsResRestartNote" : "dmGraphicsRtResRestart")); ui_show_ours(note, k != 0); } }   // 页重建后字回到布局缺省，所以每帧写（graphics_text 字没变不写） | the page rebuild resets the text to the layout default, so write every frame (graphics_text skips unchanged text)
    uint64_t vt = *(uint64_t *)sp; int valid = 0;
    for (int k = 0; k < 96 && !valid; k++) valid = *(uint64_t *)(vt + 8 * k) == (uint64_t)main_base() + 0xae5f28;
    if (valid) { int dirty = enhanced_settings_dirty();
        if (*(uint8_t *)(sp + 0x1a0) != dirty) { *(uint8_t *)(sp + 0x1a0) = dirty; LOG("图形设置：有未应用改动 → %d", dirty); } }
    else { static int warned; if (!warned++) LOG("【红】图形设置：原生选项页类型校验失败，不写改动标记"); }
}
// 确认框的答复（dm_diagup.c tick，游戏主线程）：同意 = 待定值勾上、重摆勾选框，点「应用」才生效并记下同意；不同意 = 什么都不变 | the consent box's answer: agreed = pending on, checkbox repainted, effective and recorded on Apply; declined = nothing changes
static void ac_consent_cb(int yes) {
    LOG("自定义设置：自动上报崩溃确认框 → %s", yes ? "同意（点「应用」生效）" : "不开启");
    if (yes) { atomic_store(&g_ac_ok_pending, 1); atomic_store(&g_pend[K_AUTOCRASH], 1); }
    atomic_fetch_add(&g_repaint, 1);
}
int dm_custom_auto_crash(void) { load_once(); return atomic_load(&g_loaded) && atomic_load(&g_val[K_AUTOCRASH]) && (atomic_load(&g_ac_ok) || atomic_load(&g_tel_notice)); }
int dm_custom_tel_stats(void) { load_once(); return atomic_load(&g_loaded) && atomic_load(&g_val[K_TELSTATS]) && atomic_load(&g_tel_notice); }
int dm_custom_tel_notice(void) { load_once(); return atomic_load(&g_tel_notice); }
// 告知框的答复（dm_telebox.c，游戏主线程）：当场生效并存盘，不用点「应用」（框开在主菜单，没有待定值） | the notice's answer: effective and saved at once, no Apply (the box is on the main menu, nothing is pending)
void dm_custom_tel_answer(int yes) {
    load_once(); int ac0 = dm_custom_auto_crash(), ts0 = dm_custom_tel_stats();
    atomic_store(&g_tel_notice, 1); atomic_store(&g_val[K_TELSTATS], yes); atomic_store(&g_val[K_AUTOCRASH], yes); atomic_store(&g_pend[K_TELSTATS], -1); atomic_store(&g_pend[K_AUTOCRASH], -1); if (yes) atomic_store(&g_ac_ok, 1);
    save(); atomic_fetch_add(&g_repaint, 1); LOG("自定义设置：告知框 v1 → %s", yes ? "同意（统计与自动上报崩溃都开）" : "不同意（都关）");
    if (dm_custom_auto_crash() != ac0) dm_crashreport_setting_changed(yes);
    if (dm_custom_tel_stats() != ts0) dm_telemetry_setting_changed(yes);
}
// 这一页上的普通勾选行：页刚出现时摆成该显示的值，之后玩家点了就记待定值（两项单选行存一个键）| Plain checkbox rows of a page: set to the value to show when the page appears, then record pending values on taps (two-option radio rows store one key)
// 只有触屏才起作用的那一行（换组键位置）：手柄模式置灰、副文「仅触屏」（D8），设置值不动。
// 「存放」按钮两种模式都在屏上（REQ12），手柄模式下也照常可勾选、焦点链走到它（dm_pouch.c store_pad_nav）。
// The one touch-only row (swap position): greyed in controller mode with the subtext "touch only" (D8), the setting stays.
// The Deposit button is on screen in both modes (REQ12), so in controller mode its row stays a normal, focusable checkbox
// (the focus chain is store_pad_nav in dm_pouch.c).
// iCloud 能不能用：页刚出现时问一次，之后每 60 帧（约 1 秒）再问一次，不每帧调 ubiquityIdentityToken（审计 r1 P2-8） | iCloud usability: asked once when the page appears, then every 60 frames (about 1 s), not every frame (audit r1 P2-8)
#ifndef __ANDROID__
static int g_icloud_probes;   // 单测核调用次数 | call count for the unit test
static int icloud_available_throttled(int fresh) { static int ok = 1, n; if (fresh || n <= 0) { ok = dm_icloud_available(); g_icloud_probes++; n = 60; } n--; return ok; }
#endif
static int touch_only_row(int i) { return i == K_TOUCH_SWAP; }
static void restart_hints(uint64_t sp, int page) {   // 配音 / 物品注释两行的「重启后生效」（iCloud 在 sync_toggles 里跟账号状态一起判） | voice / item notes restart hints (iCloud is judged in sync_toggles with the account state)
    static int k_in = -2; if (k_in == -2) k_in = key_idx("item_notes");
    // 测试开关 DM_TEST_PEND=<键>=<值>|!（! = 启动值取反）[,…]（截「重启后生效」提示用）：第一次进这两页时当作玩家点选了这些值（只进待应用 g_pend，不点「应用」、不写文件）
    // Test switch DM_TEST_PEND=<key>=<value>[,...] (for screenshots of the restart hints): on the first visit to these pages, act as if the player picked these values (pending g_pend only, no Apply, no file write)
    { static int done; const char *e = done ? NULL : getenv("DM_TEST_PEND"); done = 1;
      if (e) { char buf[200]; strlcpy(buf, e, sizeof buf); for (char *sv, *t = strtok_r(buf, ",", &sv); t; t = strtok_r(NULL, ",", &sv)) { char *eq = strchr(t, '='); if (!eq) continue; *eq = 0; int i = key_idx(t);
          if (i >= 0) { int nv = eq[1] == '!' ? !g_boot_v[i] : atoi(eq + 1); atomic_store(&g_pend[i], nv); LOG("自定义设置：测试开关 DM_TEST_PEND %s → %d（启动值 %d，当作玩家点选、没应用）", t, nv, g_boot_v[i]); } else LOG("【红】DM_TEST_PEND：没有键 %s", t); } } }
    if (page == PG_SYS && row_on_page(K_VOICE, page)) { uint64_t w = find_w(sp, "TouchCustom_voice_Restart", 0); if (w) ui_show_ours(w, restart_pending(K_VOICE)); }
    if (k_in >= 0 && row_on_page(k_in, page)) { uint64_t w = find_w(sp, "TouchCustom_item_notes_Note", 0); if (w) ui_show_ours(w, restart_pending(k_in)); }
}
static void sync_toggles(uint64_t sp, int page, dm_wref *seen) {
    restart_hints(sp, page);
    int dm_padmode_current(void); int pad = dm_padmode_current();
    for (int i = 1; i < NK; i++) { if (!row_on_page(i, page)) continue; char nm[48]; snprintf(nm, sizeof nm, "TouchCustom_%s", k_keys[i]);
        uint64_t w = find_w(sp, nm, 0); if (!w || !*(uint8_t *)(w + 0x50)) continue;
        if (touch_only_row(i)) { char pn[56]; snprintf(pn, sizeof pn, "TouchCustom_%s_Pad", k_keys[i]); uint64_t pw = find_w(sp, pn, 0); if (pw) ui_show_ours(pw, pad);
            if (pad) { set_toggle_grey(w); if (rev_box(i)) { uint64_t rz = find_w(sp, rev_box(i), 0); if (rz) set_toggle_grey(rz); } dm_wref_set(&seen[i], 0); continue; } }
#ifndef __ANDROID__
        if (i == K_ICLOUD) { uint64_t nw = find_w(sp, "TouchCustom_icloud_NoAcct", 0), nn = find_w(sp, "TouchCustom_icloud_Note", 0); int ok = icloud_available_throttled(dm_wref_get(&seen[i]) != w); if (nw) ui_show_ours(nw, !ok); if (nn) ui_show_ours(nn, ok && restart_pending(K_ICLOUD));
            if (!ok) { set_toggle_grey(w); dm_wref_set(&seen[i], 0); continue; } }
#endif
        int pd0 = atomic_load(&g_pend[i]), want = pd0 >= 0 ? pd0 : atomic_load(&g_val[i]);
        uint64_t z = rev_box(i) ? find_w(sp, rev_box(i), 0) : 0;   // 两项单选行（3D 画质：上下的「流畅优先」「画质优先」；换组位置）：存的仍是一个键，反面那一项是它的反面 | Two-option radio rows (3D quality: "Smoothness first" / "Quality first" stacked; swap-group position): still stores one key, the other option is its negation
        if (dm_wref_get(&seen[i]) != w) { dm_wref_set(&seen[i], w); set_toggle(w, want); if (z) set_toggle(z, !want); continue; }   // 页刚出现（布局里一律是没勾）：先摆成该显示的值，这一帧不当玩家点了 | The page has just appeared (in the layout everything is unchecked): first set it to the value it should show, and do not treat this frame as a player tap
        int on = TOGGLED(w), pd = atomic_load(&g_pend[i]), shown = pd >= 0 ? pd : atomic_load(&g_val[i]);
        if (z) {   // 点没选中的那一项 = 选它；点已选中的那一项不变（摆回勾上） | Tapping the item that is not selected = select it; tapping the one already selected changes nothing (it is put back to checked)
            int zon = TOGGLED(z), to = on != shown ? (on ? 1 : shown) : zon != !shown ? (zon ? 0 : shown) : shown;
            if (to != shown) { atomic_store(&g_pend[i], to); LOG("自定义设置：%s → %d（点「应用」生效）",k_keys[i],to); }
            if (TOGGLED(w) != to) set_toggle(w, to); if (TOGGLED(z) != !to) set_toggle(z, !to); continue; }
        // 自动上报崩溃：第一次勾上先开确认框，勾选框退回去；同意了才算改（ac_consent_cb） | automatic crash reports: the first tick opens the consent box and the checkbox falls back; it only counts once agreed
        if (i == K_AUTOCRASH && on && !shown && !atomic_load(&g_ac_ok) && !atomic_load(&g_ac_ok_pending) && !atomic_load(&g_tel_notice)) { set_toggle(w, 0); if (!dm_diagup_modal()) dm_diagup_consent_open(ac_consent_cb); continue; }
        if (i == K_TELSTATS && on && !shown && !atomic_load(&g_tel_notice)) { set_toggle(w, 0); if (!dm_diagup_modal() && !dm_crashreport_configured()) dm_diagup_consent_open(ac_consent_cb); else LOG("自定义设置：统计还没答过告知框，回主菜单时弹"); continue; }
        if (on != shown) { atomic_store(&g_pend[i], on); LOG("自定义设置：%s 勾选框 → %s（点「应用」生效）", k_keys[i], on ? "勾上" : "取消"); } }
}
// 原版选项子页的每帧更新 main+0xae5f28 在不在这个虚表里（前 96 项）：在 = +0x1a0 确是「有未应用的改动」（系统页、游戏页共用） | whether the original options subpage per-frame update main+0xae5f28 is in this vtable (first 96 slots): yes = +0x1a0 really is "unapplied changes" (shared by the System and Gameplay pages)
static int panel_vt_has_dirty_update(uint64_t vt) { if (!vt || !main_base()) return 0; for (int k = 0; k < 96; k++) if (*(uint64_t *)(vt + 8 * k) == (uint64_t)main_base() + 0xae5f28) return 1; return 0; }
// 游戏页（GameplayOptionsPanel）上的 Plus 勾选行：同系统页的待定值 / 应用 / 放弃；手柄焦点链在 dm_padmode.c diff_pump。
// 「有改动」位 +0x1a0 这页原版自己也在用：我们只在有待定改动时置位、从不清（见函数里），不抹原版自己的改动
// Plus checkbox rows on the Gameplay page: same pending / Apply / Discard as the System page; the controller focus chain is in dm_padmode.c diff_pump.
// The original also uses the "changed" flag +0x1a0 on this page: we only set it for pending changes and never clear it (see inside), leaving the original's own changes alone
static void game_ui_pump(uint64_t root) {
    static dm_wref seen[NK]; uint64_t sp = dm_ui_top(root, "GameplayOptionsPanel"); if (!sp || !*(uint8_t *)(sp + 0x50)) { memset(seen, 0, sizeof seen); return; }
    load_once(); static int paint; int repaint = atomic_load(&g_repaint); if (paint != repaint) { paint = repaint; memset(seen, 0, sizeof seen); }
    { int y = GAME_ROWS_Y;   // 难度块的下沿：标题 / 难度行 / 说明 + 「进入恐惧区域」入口行，与布局 touch_layout.py 的 DIFF_H 同一个数 | bottom of the difficulty block: title / difficulty row / note + the Terror Zones entry row; the same number as DIFF_H in the layout touch_layout.py
      for (int g = 0; g < 4; g++) { char nm[32]; snprintf(nm, sizeof nm, "TouchGrp%s", k_fold[g]); uint64_t w = find_w(sp, nm, 0); if (!w) continue; int open = atomic_load(&g_fold_open[g]); move_ours_y(w, y);
          uint64_t ch = *(uint64_t *)(w + 0x58), n = *(uint64_t *)(w + 0x60); for (uint64_t i = 0; ch && i < n && i < 64; i++) { uint64_t c = *(uint64_t *)(ch + 8 * i); const char *cn = c ? *(const char **)(c + 8) : NULL; if (cn && !strstr(cn, "Title") && strncmp(cn, "TouchFold", 9)) ui_show_ours(c, open); }
          y += open ? *(int32_t *)(w + 0x7c) : 180; }
      uint64_t hd = find_w(sp, "TouchTableHolder", 0); if (hd) { move_ours_y(hd, y); ui_show_ours(hd, atomic_load(&g_fold_open[3])); } }
    sync_toggles(sp, PG_GAME, seen);
    // 「有改动」位 +0x1a0：同系统页，先核面板虚表里确有 main+0xae5f28（审计 r1 P2-1）。这一页原版表格自己也用这一位（原版勾选框一改就置，见系统页那段），
    // 我们分不出这一位此刻是谁置的，所以只置不清：Plus 行改回原样后「应用」仍亮着，由应用 / 放弃收掉——代价是多亮一会儿，好处是永远不会抹掉原版表格自己的改动
    // "Changed" flag +0x1a0: as on the System page, first check the panel vtable really holds main+0xae5f28 (audit r1 P2-1). The original table on this page uses the same flag (a native
    // checkbox sets it on change, see the System page); we cannot tell who set it at a given moment, so we only ever set it: after reverting a Plus row Apply stays lit until
    // Apply / Discard -- lit a little longer, but an original change of the table is never wiped
    static uint64_t vt_for; static int vt_good; if (vt_for != *(uint64_t *)sp) { vt_for = *(uint64_t *)sp; vt_good = panel_vt_has_dirty_update(vt_for);
        if (!vt_good) LOG("【红】游戏页：面板虚表里没有 main+0xae5f28，不写「有改动」位（原版版本变了？）"); }
    if (vt_good && enhanced_settings_dirty() && !*(uint8_t *)(sp + 0x1a0)) { *(uint8_t *)(sp + 0x1a0) = 1; LOG("游戏页：Plus 勾选有未应用的改动 → 「应用 / 放弃」点亮"); }
}
// 手柄焦点停靠点：本页可选的 Plus 勾选框，按表序；只有触屏才起作用的那一行不停靠（D8）。
// 「存放」按钮两种模式都在屏上（REQ12），手柄模式下也停靠 | Controller focus stops: this page's selectable Plus checkboxes in table order; the one touch-only row is not a stop (D8).
// The Deposit button is on screen in both modes (REQ12), so it is a stop in controller mode too.
int dm_custom_game_stops(unsigned long long gp, unsigned long long *out, int max) {
    int n = 0; const char *grp = NULL; for (int o = 0; o < N_OPTS && n < max; o++) { int i = key_index(k_opts[o].key); if (i < 0 || !row_on_page(i, PG_GAME)) continue; char nm[48];
        if (!grp || strcmp(grp, k_opts[o].group)) { grp = k_opts[o].group; snprintf(nm, sizeof nm, "TouchFold%s", grp); uint64_t f = find_w(gp, nm, 0); if (f && n < max) out[n++] = f; }
        if (i == K_TOUCH_SWAP) continue;
        snprintf(nm, sizeof nm, "TouchCustom_%s", k_keys[i]); uint64_t w = find_w(gp, nm, 0); if (w && *(uint8_t *)(w + 0x50)) out[n++] = w; }
    { uint64_t f = find_w(gp, "TouchFoldOrig", 0); if (f && n < max) out[n++] = f; }
    return n;
}
void dm_custom_game_press(unsigned long long w) { const char *nm = *(const char **)(w + 8); if (nm && !strncmp(nm, "TouchFold", 9)) { fold_toggle(nm + 9); return; } set_toggle(w, !TOGGLED(w)); }   // 手柄 A：翻勾选，下一帧照「玩家点了」记待定值 | controller A: flip the checkbox; next frame it counts as a player tap
void dm_custom_ui_pump(unsigned long long root) {
    if (!root || !main_base()) return;
    game_ui_pump(root);
    static dm_wref seen[NK];   // 每行最近见到的勾选框（带界面代号：重建后取到 0 = 页刚出现） | Most recently seen checkbox of each row (with UI generation: 0 after a rebuild = page just appeared)
    uint64_t sp = dm_ui_top(root, "VideoOptionsPanel"); if (!sp || !*(uint8_t *)(sp + 0x50)) {   // 「自定义」页 = 借来的视频页（界面根的直接子，与设置页并列）。页不在屏上：没点「应用」的勾选作废（同原版不应用就离开），下次出现重摆 | The 「自定义」(Custom) page = the borrowed video page (a direct child of the UI root, alongside the settings page). Page not on screen: checkbox changes without tapping 「应用」(Apply) are discarded (same as leaving without applying in the original game), and it is laid out again the next time it appears
        uint64_t settings = dm_ui_top(root, "SettingsPanel");
        if (!settings || !*(uint8_t *)(settings + 0x50)) { for (int i = 0; i < NK; i++) atomic_store(&g_pend[i], -1); }
        memset(seen, 0, sizeof seen); return; }
    load_once(); static int paint; int repaint = atomic_load(&g_repaint); if (paint != repaint) { paint = repaint; memset(seen, 0, sizeof seen); }
    if (atomic_load(&g_lang_explicit) < 0) atomic_store(&g_val[0], lang_running());   // 语言那一格没点过应用：生效值 = 此刻实际在跑的语言（读盘时游戏还没设语言，那时取到的不准） | Apply never tapped for the language cell: effective value = the language actually running right now (when read from disk the game has not set the language yet, so the value fetched then is not accurate)
    if (atomic_load(&g_voice_explicit) < 0) atomic_store(&g_val[K_VOICE], voice_running());
    // 手柄模式：原版选项页的焦点只认表格里它自己的行，够不着我们的勾选框——十字键上下选行（原版手柄选中框 TouchCustomSel 框住这一行）、A 翻勾选（下面照「玩家点了」处理），其余键照旧归原版
    // Controller mode: the original options page's focus only recognizes its own rows in the table and cannot reach our checkboxes. D-pad up/down selects a row
    // (the original controller selection frame TouchCustomSel frames that row), A toggles the checkbox (handled below as "the player tapped it"), and all other
    // buttons go to the original as before
    { int dm_padmode_current(void); uint64_t dm_padmode_page_keys(uint64_t); static int sel; static dm_wref sel_sp; int pad = dm_padmode_current() && !dm_ui_modal_active(root) && !dm_community_shown() && !dm_diagup_modal();   // 社区提示框显示着时 A / B 归它（dm_community.c） | while the community box shows, A / B belong to it (dm_community.c)
      if (dm_wref_get(&sel_sp) != sp) { dm_wref_set(&sel_sp, sp); sel = 0; }
      indent_rows(find_w(sp, "TouchCustomRows", 0), 0);
      uint64_t dm = find_w(sp, "TouchCustomSel", 0), tw = 0, cw = 0, xw = 0; char nm[64]; int oy = 0, about_bot = -1;   // about_bot：选到「关于」组的社区行时，自动滚动要把整组（含下面的鸣谢子块）滚进框 | about_bot: with the About group's community row selected, auto-scroll brings the whole group (credits sub-block below included) into view. oy：选中行相对 TouchCustomRows 的纵向偏移（「关于」组的社区行套在两层容器里，自己的矩形 y 是 0） | oy: the selected row's vertical offset within TouchCustomRows (the About group's community row sits two containers deep, its own rect y is 0)
      int nc = custom_rows(), rows = nc + 1 + (dm_community_available() ? 1 : 0); if (sel >= rows) sel = rows - 1;   // +1：「存档管理」组（dm_savemgr.c），排在「关于」组之前 | +1: the Save Management group, before the About group
        // 末尾「关于」组的社区行（dm_community.c；布局 touch_layout.py _custom_about）：有可显示的值时多一行可选，A = 打开提示框 | the About group's community row at the end (dm_community.c; layout _custom_about): one more selectable row when there is a value, A = open the box
      if (pad) { uint64_t ev = dm_padmode_page_keys(DM_BTN_UP | DM_BTN_DOWN | DM_BTN_A);
          if (ev & DM_BTN_DOWN) sel = sel + 1 < rows ? sel + 1 : sel; else if (ev & DM_BTN_UP) sel = sel > 0 ? sel - 1 : 0;
          int ci = sel < nc ? custom_key_at(sel) : -1;
          if (ci < 0 && sel == nc) { tw = find_w(sp, "TouchSavesText", 0); cw = find_w(sp, "TouchSavesOpt", 0); uint64_t ar = find_w(sp, "TouchSavesGroup", 0), ac = find_w(sp, "TouchSavesRow", 0);
              if (cw && ((int32_t *)(cw + 0x70))[0] == 99999) cw = find_w(sp, "TouchSavesHint", 0);   // 局内按钮挪走了，框住副文 | in a run the button is away, frame the subtext
              oy = (ar ? *(int32_t *)(ar + 0x74) : 0) + (ac ? *(int32_t *)(ac + 0x74) : 0); if (ar) about_bot = *(int32_t *)(ar + 0x74) + *(int32_t *)(ar + 0x7c); if ((ev & DM_BTN_A) && ac) dm_savemgr_open(2); }
          else if (ci < 0) { tw = find_w(sp, "TouchAboutCommunityText", 0); cw = find_w(sp, "TouchCommunityOpt", 0); uint64_t ar = find_w(sp, "TouchAboutRow", 0), ac = find_w(sp, "TouchAboutCommunityRow", 0);
              oy = (ar ? *(int32_t *)(ar + 0x74) : 0) + (ac ? *(int32_t *)(ac + 0x74) : 0); if (ar) about_bot = *(int32_t *)(ar + 0x74) + *(int32_t *)(ar + 0x7c); if ((ev & DM_BTN_A) && cw && ac) dm_community_open(4); }
          else {
          snprintf(nm, sizeof nm, "TouchCustom_%s", k_keys[ci]); cw = find_w(sp, ci == 0 ? k_lang_box[LANG_N - 1] : nm, 0); snprintf(nm, sizeof nm, "TouchCustom_%s_Text", k_keys[ci]); tw = find_w(sp, nm, 0);   // 语言行：框住标签到最右那一格 | Language row: box from the label to the rightmost cell
          if (ci == 0) xw = find_w(sp, "TouchCustomLangKo_Text", 0);   // B087：最右一格的字「한국어」在勾选框右边，框要连它一起框住（原先只到勾选框右边 30，字露在框外） | B087: the last option's text 「한국어」 sits right of its checkbox; the frame must take it in too (it used to end 30 past the checkbox, leaving the text outside)
          if ((ev & DM_BTN_A) && cw && dm_wref_get(&seen[ci]) == cw) {
              if (ci == K_Q3D) { int pd = atomic_load(&g_pend[ci]), shown = pd >= 0 ? pd : atomic_load(&g_val[ci]); int to = (shown + 1) % 5; atomic_store(&g_pend[ci], to); for (int k = 0; k < 5; k++) { uint64_t z = find_w(sp,k_q3d_box[k],0); if (z) set_toggle(z,k == to); } }
              else if (ci == K_VOICE || ci == K_TOUCH_SWAP) { int p0 = atomic_load(&g_pend[ci]), sh = p0 >= 0 ? p0 : atomic_load(&g_val[ci]); atomic_store(&g_pend[ci], !sh); set_toggle(cw, !sh); { uint64_t z = find_w(sp, rev_box(ci), 0); if (z) set_toggle(z, sh); }
                  LOG("自定义设置：%s → %d（手柄 A 在两项间切换；点「应用」生效）",k_keys[ci],!sh); }
              else if (ci == 0) { int p0 = atomic_load(&g_pend[0]), sh = p0 >= 0 ? p0 : atomic_load(&g_val[0]), to = (sh + 1) % LANG_N; atomic_store(&g_pend[0], to);   // 语言行是单选的五项：A 依次轮换，四个框当场摆好
                  for (int k = 0; k < LANG_N; k++) { uint64_t z = find_w(sp, k_lang_box[k], 0); if (z) set_toggle(z, k == to); }
                  LOG("自定义设置：语言 → %s（手柄 A 在五项间轮换；点「应用」生效）", k_lang_name[to]); }
              else set_toggle(cw, !TOGGLED(cw)); } } }
      if (dm) { int32_t *r = (int32_t *)(dm + 0x70), want[4] = { 99999, 0, r[2], r[3] };   /* 不用时只把 x 挪出屏：它在滚动区里，y 放远了会撑大可滚范围 | when unused only x is moved off screen: it is inside the scroll area, and a far-away y would enlarge the scrollable range */
          if (dm_padmode_current() && tw && cw) { int32_t *t = (int32_t *)(tw + 0x70), *c = (int32_t *)(cw + 0x70); int top = (t[1] < c[1] ? t[1] : c[1]) + oy, bot = (t[1] + t[3] > c[1] + c[3] ? t[1] + t[3] : c[1] + c[3]) + oy;   // 框住整行：文字与勾选框两者的上下沿取并，再各放 10 单位（原先只按文字框的高度算，文字框比字矮，框成了横穿文字中线的一条） | Frame the whole row: take the union of the top and bottom edges of the text and the checkbox, then pad 10 units on each side (previously computed from the text box height only; the text box is shorter than the glyphs, so the frame became a strip cutting across the middle of the text)
              want[0] = dm_sel_frame_x(t[0]); want[1] = top - 10; want[2] = dm_sel_frame_right(c[0] + c[2], xw ? ((int32_t *)(xw + 0x70))[0] + ((int32_t *)(xw + 0x70))[2] : 0) - want[0]; want[3] = bot - top + 20;
              { static int lg; if (lg++ < 2) LOG("自定义设置：手柄选中框 文字框 %d,%d %dx%d 勾选框 %d,%d %dx%d → 框 %d,%d %dx%d", t[0], t[1], t[2], t[3], c[0], c[1], c[2], c[3], want[0], want[1], want[2], want[3]); } }
          if (memcmp(r, want, sizeof want)) { memcpy(r, want, sizeof want); ((void (*)(uint64_t))*(uint64_t *)(*(uint64_t *)dm + 0x10))(dm); } }
      // 这页可以滚（各行在原版滚动区里，touch_layout.py _custom_page）：手柄选到列表框外的行时，让原版滚动区自己滚到位——发滚轮，每 4 帧一格，直到这一行整行在框内。
      // 已滚的量 = 内层视图（行容器的父控件）矩形 y 的相反数；框高 = 滚动区（再上一级）的高。只读这两个数，不写原版控件
      // This page can scroll (the rows are in the original scroll area, touch_layout.py _custom_page): when the controller selects a row outside the list box, let the original scroll area scroll into place by itself -- send wheel events, one notch every 4 frames, until the whole row is inside the box.
      // Amount scrolled = the negative of the rect y of the inner view (the parent widget of the row container); box height = the height of the scroll area (one level further up). Only these two numbers are read; the original game's widgets are not written
      if (pad && tw && cw) { uint64_t rw = find_w(sp, "TouchCustomRows", 0), view = rw ? *(uint64_t *)(rw + 0x30) : 0, sv = view ? *(uint64_t *)(view + 0x30) : 0; static uint64_t lf; uint64_t dm_guest_frame(void); uint64_t f = dm_guest_frame();
          // 到头判定（dm_custom_scroll.h）：上一格同方向、滚动量没变 = 滚到头（末行下沿露不全），不再发——原先一直发、每格一声点击音（奥丁手柄，用户 2026-10-28）；换行重新来
          // End detection (dm_custom_scroll.h): the previous notch in the same direction with an unchanged offset = at the end (the last row's bottom edge never fits), no more notches -- previously they were sent forever, one click sound each (Thor controller, user 2026-10-28); a new row starts over
          static int last_dir, last_off = -1, last_sel = -1; if (sel != last_sel) { last_sel = sel; last_dir = 0; last_off = -1; }
          if (sv && f - lf >= 4) { int32_t *t = (int32_t *)(tw + 0x70), *c = (int32_t *)(cw + 0x70); int top = (t[1] < c[1] ? t[1] : c[1]) + oy - 10, bot = (about_bot > 0 ? about_bot : (t[1] + t[3] > c[1] + c[3] ? t[1] + t[3] : c[1] + c[3]) + oy) + 10;
              int off = -*(int32_t *)(view + 0x74), vh = *(int32_t *)(sv + 0x7c); if (vh <= 0) vh = 1260; void dm_ui_wheel_on(uint64_t, int); if (sel == 0) top = 0;   /* 选回第一行：一直滚到最顶，把标题行露出来 | selection back on the first row: scroll all the way to the top so that the header row is revealed */
              int dir = dm_custom_scroll_step(top, bot, off, vh, last_dir, last_off);
              if (dir) { lf = f; last_dir = dir; last_off = off; dm_ui_wheel_on(sv, dir); }
              else if (last_dir && last_off == off && (top < off || bot > off + vh)) { static int logged; if (logged++ < 2) LOG("自定义设置：第 %d 行滚到头仍露不全（行 %d–%d，已滚 %d，框高 %d），不再发滚轮", sel, top, bot, off, vh); } } } }
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
    sync_toggles(sp, PG_SYS, seen);
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
    static uint64_t vt_ok_for; static int vt_ok; if (vt_ok_for != *(uint64_t *)sp) { vt_ok_for = *(uint64_t *)sp; vt_ok = panel_vt_has_dirty_update(vt_ok_for);
        if (!vt_ok) LOG("【红】自定义设置：借来的视频页虚表里没有 main+0xae5f28，不写「有改动」位（原版版本变了？）"); }
    if (vt_ok) { int dirty = enhanced_settings_dirty();
        if (*(uint8_t *)(sp + 0x1a0) != dirty) { *(uint8_t *)(sp + 0x1a0) = (uint8_t)dirty; LOG("自定义设置：有未应用的改动 → %d（「应用 / 放弃」%s）", dirty, dirty ? "点亮" : "变灰"); } }
}
