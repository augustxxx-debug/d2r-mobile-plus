// 游戏内「社区入口」：提示框里是渠道卡片（RC1.1 重构；RC2 中文加国内镜像）——中文界面 QQ 群 / 官网 / 国内镜像 / GitHub、其他语言 官网 / GitHub，取有值的前三张，每张卡片有自己的「复制」（复制到剪贴板）。入口在暂停菜单「分享诊断日志」旁、前端主菜单和选项「自定义」页末尾的「关于」组，几处点开同一个游戏内提示框。
// 群号、官网、国内镜像、GitHub 地址与网盘都不写在代码里：来自外壳每次拉的远端版本文件（iOS latest.json / Mac latest_mac.json）里的 community 字段（qq_group / site / mirror / github / pans），缓存在设置目录的 community.txt；
// 清单没有这个字段 = 入口整体不显示（旧清单、旧包兼容）；这次拉不到 = 沿用缓存；没有缓存 = 不显示。只放群号、官网、镜像、仓库地址与网盘链接，不放任何个人联系方式。
// In-game community entry: the box shows channel cards (RC1.1 redesign; RC2 adds the mainland mirror in Chinese) -- Chinese QQ group / website / mainland mirror / GitHub, other languages website / GitHub, the first three present, each card with its own Copy (to the clipboard). Entries: the pause menu next to Share Diagnostics, the front-end main menu and the About group at the end of the Custom options page; all open the same in-game notice box.
// The number, website, mainland mirror, GitHub address and cloud drives are not in the code: they come from the community field (qq_group / site / mirror / github / pans) of the remote version file the shell already fetches (iOS latest.json / Mac latest_mac.json), cached as community.txt in the settings directory;
// no such field in the manifest = the entry is hidden altogether (old manifests and old packages stay compatible); fetch failed this time = keep the cache; no cache = hidden. Only the group, the website, the mirror, the repository address and cloud-drive links, never any personal contact.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "dm_pan.h"
// 渠道卡片上限：布局 touch_layout.py 的 CO_CH_ROWS 与这里同一个数（Python 测试核对两边）
// Channel-card limit: CO_CH_ROWS in touch_layout.py is the same number (the Python test checks both sides)
#define DM_COMMUNITY_CHANNEL_ROWS 3
int dm_community_manifest(const char *json, size_t len);   // 外壳每次拉完清单调（任意线程）：json / len = 原文（NULL = 这次没拉到）。返回 1 有字段（已写缓存）/ 0 无字段或清单结构坏（入口不显示，缓存已删）/ -1 没拉到，或给了渠道但全不合规，空串也算（沿用缓存；要撤下渠道就省略键，撤下入口就省略 community 或给 community: {}） | called by the shell after every fetch (any thread): json / len = raw text (NULL = fetch failed). Returns 1 field present (cache written) / 0 absent or a malformed manifest (entry hidden, cache deleted) / -1 not fetched, or channels given but all invalid, empty strings included (cache kept; to withdraw a channel omit its key, to withdraw the entry omit community or give community: {})
void dm_community_load(void);                              // 宿主入口调一次（可重入）：读缓存；测试局另认 DM_TEST_COMMUNITY=<清单文件> | called once at host entry (idempotent): read the cache; test runs also honor DM_TEST_COMMUNITY=<manifest file>
int dm_community_value(int lang_group, char *out, size_t n);   // 按语言组（DM_LG_*）选要显示 / 复制的值：0 没有 / 1 群号 / 2 链接。中文：群号，没有群号才给链接；其他语言只给链接。链接 = 官网（清单 site），没有官网时中文先用国内镜像、再退回 GitHub；其他语言退回 GitHub | the value to show / copy for a language group (DM_LG_*): 0 none / 1 group number / 2 link. Chinese: the number, the link only without one; other languages: the link only. The link = the website (manifest site), else the mainland mirror for Chinese, else GitHub
enum { DM_CH_QQ = 1, DM_CH_SITE = 2, DM_CH_GITHUB = 3, DM_CH_MIRROR = 4 };
int dm_community_channels(int lang_group, int out[3]);       // 框里的渠道行（最多三行）：中文 [QQ 群, 官网, 国内镜像, GitHub]、其他语言 [官网, GitHub] 取有值的前三个；返回行数 | the box's channel rows (at most three); returns the count
int dm_community_channel_value(int ch, char *full, size_t nf, char *shown, size_t ns);   // 完整值（复制）与显示值（官网 / 国内镜像去 https:// 与末尾 /，GitHub 只留仓库名） | full value (copy) and shown value
int dm_community_fit(char *s, size_t cap, int pt, int maxw); // 按 §2.6 估宽超过 maxw 就截断加「…」；cap = 缓冲区容量，只走合法 UTF-8 前缀、保证 NUL 结尾；返回是否改了串 | cut with "…" over maxw; cap = buffer size, valid UTF-8 prefix only, always NUL-terminated
int dm_community_pans(dm_pan_entry out[DM_PAN_MAX]);       // 清单 / 缓存里的网盘（已按 dm_pan.h 校验、最多 4 条），写进 out 并返回条数；out 可为 NULL（只问条数）。界面卡下一张用它 | the validated cloud-drive entries (at most 4) fill out; returns the count; out may be NULL. The next card draws them
int dm_community_available(void);                          // 当前语言下入口该不该显示 | whether the entry shows in the current language
void dm_community_open(int from);                          // 玩家点了「加入社区讨论」（from 只进日志：1 暂停菜单 / 2 主菜单 / 3 自定义页按钮 / 4 自定义页手柄 A）：有值就挂上社区框（社区段 + 「关于」段） | the player used Join the community (from goes to the log only: 1 pause menu / 2 main menu / 3 Custom page button / 4 Custom page controller A): queue the community box (community + About section) when there is a value
void dm_community_open_about(void);                        // 主菜单「关于」行：纯「关于」框（版本 + 鸣谢），没有社区值也开 | the main menu's About row: the About-only box (version + credits), opens without a community value too
void dm_community_copy_init(int (*cb)(const char *text));  // 外壳登记剪贴板回调（iOS UIPasteboard / Mac NSPasteboard）；在游戏主线程被调，返回 1 成功 | the shell registers its clipboard callback (iOS UIPasteboard / Mac NSPasteboard); called on the game main thread, returns 1 on success
unsigned dm_community_copy_count(void);                    // 复制成功次数（单测核对） | successful copies (unit tests)
int dm_community_url_openable(const char *url);   // 1 = 白名单网盘分享链接（外壳打开前复核用） | 1 = a whitelisted cloud-drive share link (the shells re-check with it)
void dm_community_open_init(int (*cb)(const char *url));   // 外壳登记「用系统浏览器打开」回调（iOS UIApplication / Android Intent / Mac NSWorkspace）；在游戏主线程被调，返回 1 已交给系统 | the shell registers its open-in-browser callback (iOS UIApplication / Android Intent / Mac NSWorkspace); called on the game main thread, returns 1 when handed to the system
int dm_community_open_pan(int idx);                        // 唯一的打开入口：取第 idx 条网盘，过 dm_pan_open_allowed 再调回调；idx 越界 / 没登记 / 不合规 → 0、记【红】、不调回调。github / site / mirror 不走这里 | the only open entry: take drive idx, pass dm_pan_open_allowed, then call the callback; out-of-range idx / no callback / invalid → 0, red log, callback not called. github / site / mirror never come here
int dm_community_shown(void);                              // 提示框正显示着（手柄模式下自定义页此时不再接管十字键 / A，免得一下 A 既复制又翻勾选） | the box is showing (in controller mode the Custom page then stops taking over the D-pad / A, so one A does not both copy and toggle)
void dm_community_on_uimsg(const char *cat, const char *act, const char *arg);   // 界面消息（diag.c）：两个入口按钮、提示框的复制 / 确定 | UI messages (diag.c): the two entry buttons, the box's Copy / OK
void dm_community_pump(void);                              // 取手柄处每帧调（hle_hid.c）：入口显隐、写字、摆位置、手柄 A 复制 / B 收起 | every frame at the controller poll (hle_hid.c): entry show / hide, text, placement, controller A copies / B dismisses
// 弹窗吞输入（RC1.1）：开着时触屏点按只认弹窗的按钮、手柄键与摇杆全截给弹窗 | modal input (RC1.1): while the box is up, taps only reach its buttons and every controller key / stick goes to it
int dm_community_modal(void);                                 // 弹窗开着（含将要显示） | the box is up (or about to show)
void dm_community_ui_tap(uint64_t root, int x, int y);        // dm_cursor.c：弹窗开着时的一次点按（游戏窗口像素） | dm_cursor.c: a tap while the box is up (game window pixels)
void dm_community_input_gate(uint64_t *bt, int32_t *lx, int32_t *ly, int32_t *rx, int32_t *ry);   // hle_hid.c 取手柄处最后调 | called last in hle_hid.c's controller read
void dm_community_input_leak(int leaked);                       // 闸门之后仍有非零输入交给游戏（计数，应为 0） | a non-zero input still reached the game after the gate (counted, should be 0)
