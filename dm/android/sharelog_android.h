// 「分享诊断日志」的安卓实现（五项适配之一；iOS 版 dm/ios/Sources/dm_sharelog.m）：纯 C，可在 Mac 上单测（tests/sharelog_android_test.c）。
//   dm_sharelog_build：头部（导出时间、显示版本、设备行、系统退出原因）+ 本局与上一局关键读数（各键取最后一行）+ 本局日志 boot.log + 上一局 boot.previous.log（各最多 768 KiB 尾部），
//   整份经 dm_sharelog_scrub 脱敏后写到 out_path。角色名从 files/save*/<名字>/<名字>.d2s 的目录名收集（只读目录名，不读存档内容）。
//   dm_sharelog_scrub：导出专用脱敏（规则照 dm_diagnostic_scrub.h 移植）：应用包名 → <app-id>、/storage/emulated/0 与 home → ~、角色名整词 → <character>、
//   /Users/<名> 与邮箱与 UUID / 8-16 位十六进制标识 → <redacted>、「屏幕：<名字>，」→ <display>。不改原始日志文件。
// Android implementation of "Share Diagnostics" (one of the five adaptations; iOS: dm/ios/Sources/dm_sharelog.m): pure C, unit-tested on the Mac (tests/sharelog_android_test.c).
//   dm_sharelog_build: header (export time, display version, device line, system exit reasons) + key readings of this and the previous run (last line per key) + this run's boot.log + the previous
//   boot.previous.log (tail of at most 768 KiB each), the whole file scrubbed by dm_sharelog_scrub and written to out_path. Character names come from the directory names files/save*/<name>/<name>.d2s (names only, never save contents).
//   dm_sharelog_scrub: export-only redaction (rules ported from dm_diagnostic_scrub.h): app id -> <app-id>, /storage/emulated/0 and home -> ~, whole-word character names -> <character>,
//   /Users/<name>, e-mail addresses and UUID / 8-16 hex identifiers -> <redacted>, "屏幕：<name>，" -> <display>. The original log files are never modified.
#pragma once
#include <stddef.h>
#define DM_SHARELOG_TAIL (768 * 1024)
// 返回 malloc 的脱敏文本（调用者 free），*out_len 为长度；names 最多 64 个 | returns the malloc'd scrubbed text (caller frees), *out_len its length; at most 64 names
char *dm_sharelog_scrub(const char *text, size_t len, const char *home, const char *appid, const char *const *names, int nnames, size_t *out_len);
// 从文本里的 /save(.test)?(.work)?/<名字>/ 路径收集角色名（去掉 Settings 与 ModernSharedStash*），写进 names（最多 cap 个），返回个数 | collect character names from save paths in the text
int dm_sharelog_names_in_text(const char *text, size_t len, char names[][64], int have, int cap);
// 读文件尾部至多 limit 字节（malloc，调用者 free）；*omitted 记被略去的前缀字节数；文件不存在返回 NULL | tail of a file (malloc), *omitted = bytes skipped at the front; NULL when absent
char *dm_sharelog_read_tail(const char *path, size_t limit, size_t *len, unsigned long long *omitted);
// 组装并脱敏后写出；返回写出的字节数，失败 -1。device_line：型号 / 系统 / GPU 一行（调用者给，已不含标识）| assemble, scrub and write; returns bytes written or -1
long dm_sharelog_build(const char *runtime_dir, const char *files_dir, const char *out_path, const char *version_label, const char *device_line, const char *appid);
