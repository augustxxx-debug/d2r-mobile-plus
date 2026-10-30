// 远端更新清单（第一期普通 latest.json）的严格解析：iOS / 安卓 / Mac 共用，纯函数（不联网、不读写文件、不分配堆内存），单测 tests/t_manifest.c。
// 格式与规则见服务端说明（交接/服务端_更新清单接口说明_20261029.md 第 2、3 节，与 fork 仓库 docs/latest.json 同一份字段）：
//   整份 ≤ 4096 字节，必须是一个合法的 JSON 根对象（语法全验：尾逗号、根后多余内容、非对象根都算错），深度上限 16；
//   必填 build（正整数 < 2000000000，不许小数 / 指数）、note（字符串）、urgent（true / false）；可选 version（≤ 20 个字符，只许数字字母与 .+-）；
//   可选 community 对象，其下 qq_group（5–12 位数字）、github（https://github.com/ 开头、≤ 160 个 URL 字符）、site（项目官网，https:// 开头、≤ 160 个 URL 字符，服务端说明第 13 节），都不许带转义；
//   我们认的键（build / version / note / urgent / render_safe / community / qq_group / github / site）重复出现 = 错；其余键忽略（向后兼容）。
// 任何一条不满足 = 整份清单不合法（两个来源时换下一个来源；缓存不收）。比 RC1 社区入口（dm_community.c，坏一项只丢那一项）更严：多来源下坏清单应当整份放弃。
// Strict parsing of the remote update manifest (phase-1 plain latest.json): shared by iOS / Android / Mac, pure functions (no network, no file I/O, no heap), unit test tests/t_manifest.c.
// Format and rules are in the server notes (sections 2 and 3; the same fields as docs/latest.json in the fork repository):
//   at most 4096 bytes in all, one valid JSON root object (full syntax check: trailing commas, trailing content and non-object roots are errors), depth capped at 16;
//   required build (positive integer < 2000000000, no fraction / exponent), note (string), urgent (true / false); optional version (≤ 20 characters, digits, letters and .+- only);
//   optional community object with qq_group (5-12 digits), github (starts with https://github.com/, ≤ 160 URL characters) and site (project site, https://, ≤ 160 URL characters, server notes section 13), none may contain escapes;
//   a key we know (build / version / note / urgent / render_safe / community / qq_group / github / site) appearing twice = error; other keys are ignored (forward compatible).
// Any rule broken = the whole manifest is invalid (with two sources, try the next one; the cache does not take it). Stricter than the RC1 community entry (dm_community.c drops only the bad
// item): with several sources a bad manifest should be dropped as a whole.
#pragma once
#include <stddef.h>

enum { DM_MANIFEST_MAX = 4096, DM_MANIFEST_QQ_MAX = 12, DM_MANIFEST_GH_MAX = 160 };
typedef struct {
    int build, urgent, render_safe;          // render_safe：远程应急开关（安卓不用，只解析） | render_safe: remote emergency switch (unused on Android, parsed only)
    char version[21];                        // 空 = 没给 | empty = not given
    char note[200];                          // 原样字节（带转义时不解码；超长截断） | raw bytes (escapes not decoded; truncated when long)
    char qq[DM_MANIFEST_QQ_MAX + 1];         // 空 = 没给 | empty = not given
    char github[DM_MANIFEST_GH_MAX + 1];     // 空 = 没给 | empty = not given
    char site[DM_MANIFEST_GH_MAX + 1];       // 项目官网；空 = 没给 | project site; empty = not given
} dm_manifest;

// 1 = 合法（*m 填好）；0 = 不合法（why 写一句中文原因，进日志）。m / why 可为 NULL
// 1 = valid (*m filled in); 0 = invalid (why gets a one-line Chinese reason for the log). m / why may be NULL
int dm_manifest_parse(const char *json, size_t len, dm_manifest *m, char *why, size_t why_n);
