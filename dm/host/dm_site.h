// 社区清单 community.site（项目官网）的唯一合同：加载页（dm_manifest.c）与游戏内（dm_community.c）共用，安卓两者在不同的 .so 里，所以写成头文件内联。样例 tests/site_contract_cases.h。
//   https:// + 主机名（只许 [A-Za-z0-9.-]、至少一个点、不以点或横线开头结尾）+ 可选 /路径；主机名后面只能是串尾或 /，不收端口（B024：接口约定第 13 节没有端口，整串不许出现 :）；拒绝 @（userinfo，浏览器会打开 @ 后面的主机）、? 与 #；
//   总长 ≤ DM_SITE_MAX = 60：值行 36 号按 §2.6 估宽（拉丁 0.7 × 字号 ≈ 25.2 单位 / 字）在框内可用 1560 里最多约 61 字（RC1.1 增量审计 Q11.1-01 / 02）
// The one contract of the manifest's community.site (project website): shared by the loading page (dm_manifest.c) and the game (dm_community.c), which on Android live in different .so files, hence an inline in a header. Samples in tests/site_contract_cases.h.
//   https:// + host ([A-Za-z0-9.-], at least one dot, not starting or ending with . or -) + optional /path; the host is followed only by end-of-string or / and no port is accepted (B024: the interface contract §13 has no port, no : anywhere); rejects @ (userinfo -- a browser opens the host after @), ? and #;
//   at most DM_SITE_MAX = 60 characters (the 36-pt value line fits about 61 in the box's 1560 by the §2.6 estimate; RC1.1 review Q11.1-01 / 02)
#pragma once
#include <ctype.h>
#include <string.h>

#define DM_SITE_MAX 60
static inline int dm_site_ok(const char *s) {
    static const char pre[] = "https://"; size_t n = strlen(s);
    if (n <= sizeof pre - 1 || n > DM_SITE_MAX || strncmp(s, pre, sizeof pre - 1)) return 0;
    const char *h = s + sizeof pre - 1, *e = h; int dot = 0;
    while (*e && *e != '/') { unsigned char c = (unsigned char)*e; if (!(isalnum(c) || c == '.' || c == '-')) return 0; dot |= c == '.'; e++; }
    if (e == h || !dot || h[0] == '.' || h[0] == '-' || e[-1] == '.' || e[-1] == '-') return 0;
    for (; *e; e++) { unsigned char c = (unsigned char)*e; if (!(isalnum(c) || strchr("-._~/!$&'()*+,;=%", c))) return 0; }   // 路径：不含 ? # @ :（B024） | path: no ? # @ : (B024)
    return 1;
}
