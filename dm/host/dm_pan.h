// 社区清单 community.pans（网盘数组）的唯一规则：加载页（dm_manifest.c）与游戏内（dm_community.c）共用，安卓两者在不同的 .so 里，所以写成头文件内联。
// 纯规则，不碰游戏内存、没有全局状态。
//   name：1–12 个 UTF-8 字符（按字符数，不按字节），不带转义；超长 / 空 / 坏 UTF-8 → 该条作废（调用方判定）。
//   icon：内置键 quark / baidu / aliyun / 123pan / generic；认不出的键（含图片 URL）一律当 generic，不接受任何图片 URL。
//   url：只接受 https://，且必须是所属网盘的前缀：
//        quark  https://pan.quark.cn/s/          baidu  https://pan.baidu.com/s/
//        aliyun https://www.alipan.com/s/ 或 https://www.aliyundrive.com/s/
//        123pan https://www.123pan.com/s/
//        只含 URL 字符，长度 ≤ 160（同 github）。icon 是具体网盘时必须是它自己的前缀；generic 时必须在四家的并集里，不允许任意域名。
//   code：可选，4–8 位字母或数字；不合规只丢 code，条目保留。
// The one rule set of the manifest's community.pans (cloud-drive array): shared by the loading page (dm_manifest.c) and the game (dm_community.c), which live in
// different .so files on Android, hence inline in a header. Pure rules: no game memory, no global state.
//   name: 1-12 UTF-8 characters (characters, not bytes), no escapes; too long / empty / bad UTF-8 makes the entry invalid (decided by the caller).
//   icon: built-in key quark / baidu / aliyun / 123pan / generic; any unknown key (an image URL included) is generic, no image URL is ever accepted.
//   url: https:// only, with the provider's own prefix:
//        quark  https://pan.quark.cn/s/          baidu  https://pan.baidu.com/s/
//        aliyun https://www.alipan.com/s/ or https://www.aliyundrive.com/s/
//        123pan https://www.123pan.com/s/
//        URL characters only, at most 160 (same as github). A concrete icon must carry its own prefix; generic must match one of the four providers, never an arbitrary host.
//   code: optional, 4-8 letters or digits; an invalid code is dropped and the entry stays.
#pragma once
#include <ctype.h>
#include <stddef.h>
#include <string.h>

enum { DM_PAN_MAX = 4, DM_PAN_URL_MAX = 160, DM_PAN_NAME_MAX = 48, DM_PAN_CODE_MAX = 8 };   // name 12 个字符 × 最多 4 字节 | 12 characters × up to 4 bytes
typedef enum { DM_PAN_GENERIC = 0, DM_PAN_QUARK, DM_PAN_BAIDU, DM_PAN_ALIYUN, DM_PAN_123PAN } dm_pan_icon;
typedef struct {
    char name[DM_PAN_NAME_MAX + 1];   // 原样 UTF-8 | raw UTF-8
    dm_pan_icon icon;
    char url[DM_PAN_URL_MAX + 1];
    char code[DM_PAN_CODE_MAX + 1];   // 空 = 没给 | empty = not given
} dm_pan_entry;

static inline dm_pan_icon dm_pan_icon_of(const char *key) {   // 认不出的键（含 NULL / 图片 URL）→ generic | unknown keys (NULL / an image URL included) -> generic
    if (key) {
        if (!strcmp(key, "quark")) return DM_PAN_QUARK;
        if (!strcmp(key, "baidu")) return DM_PAN_BAIDU;
        if (!strcmp(key, "aliyun")) return DM_PAN_ALIYUN;
        if (!strcmp(key, "123pan")) return DM_PAN_123PAN;
    }
    return DM_PAN_GENERIC;
}
static inline const char *dm_pan_icon_key(dm_pan_icon icon) {   // 枚举 → 缓存里的键（写 pan<N>_icon= 用） | enum -> cache key
    switch (icon) {
    case DM_PAN_QUARK: return "quark";
    case DM_PAN_BAIDU: return "baidu";
    case DM_PAN_ALIYUN: return "aliyun";
    case DM_PAN_123PAN: return "123pan";
    default: return "generic";
    }
}
// 图标方块里的首字（社区框网盘行）：只放这一个字，六种语言都用这五个（用户 2026-11-04）。放在头文件里，上屏中文字面量闸门只扫 .c
// First character inside the icon square (a cloud-drive row in the community box): only this one character, the same five in every language (user, 2026-11-04)
static inline const char *dm_pan_icon_glyph(dm_pan_icon icon) {
    switch (icon) {
    case DM_PAN_QUARK: return "夸";
    case DM_PAN_BAIDU: return "百";
    case DM_PAN_ALIYUN: return "阿";
    case DM_PAN_123PAN: return "1";
    default: return "盘";
    }
}
static inline int dm_pan_prefixed(const char *s, const char *pre) { size_t n = strlen(s), p = strlen(pre); return n > p && n <= DM_PAN_URL_MAX && !strncmp(s, pre, p); }
static inline int dm_pan_url_ok(dm_pan_icon icon, const char *url) {
    if (!url) return 0;
    size_t n = strlen(url);
    if (n > DM_PAN_URL_MAX) return 0;
    for (size_t i = 0; i < n; i++) { unsigned char c = (unsigned char)url[i]; if (!(isalnum(c) || strchr("-._~:/?#[]@!$&'()*+,;=%", c))) return 0; }
    if (icon != DM_PAN_GENERIC) {   // 具体网盘：只认自己的前缀 | concrete provider: its own prefix only
        switch (icon) {
        case DM_PAN_QUARK: return dm_pan_prefixed(url, "https://pan.quark.cn/s/");
        case DM_PAN_BAIDU: return dm_pan_prefixed(url, "https://pan.baidu.com/s/");
        case DM_PAN_ALIYUN: return dm_pan_prefixed(url, "https://www.alipan.com/s/") || dm_pan_prefixed(url, "https://www.aliyundrive.com/s/");
        case DM_PAN_123PAN: return dm_pan_prefixed(url, "https://www.123pan.com/s/");
        default: return 0;
        }
    }
    return dm_pan_prefixed(url, "https://pan.quark.cn/s/") || dm_pan_prefixed(url, "https://pan.baidu.com/s/")
        || dm_pan_prefixed(url, "https://www.alipan.com/s/") || dm_pan_prefixed(url, "https://www.aliyundrive.com/s/")
        || dm_pan_prefixed(url, "https://www.123pan.com/s/");   // generic：四家并集，别的域名一律不认 | generic: the four providers only
}
static inline int dm_pan_code_ok(const char *code) {   // 4–8 位字母或数字；空 / 别的字符 / 超长 → 不合规 | 4-8 letters or digits
    if (!code) return 0; size_t n = strlen(code);
    if (n < 4 || n > DM_PAN_CODE_MAX) return 0;
    for (size_t i = 0; i < n; i++) if (!isalnum((unsigned char)code[i])) return 0;
    return 1;
}
static inline int dm_pan_name_ok(const char *name) {   // 1–12 个合法 UTF-8 字符；坏字节 / 控制字符 / 超长 → 不合规 | 1-12 valid UTF-8 characters
    if (!name) return 0; size_t i = 0; int n = 0;
    while (name[i]) {
        unsigned char c = (unsigned char)name[i]; size_t k;
        if (c < 0x20 || c == 0x7f) return 0;
        if (c < 0x80) k = 1;
        else if (c >= 0xc2 && c <= 0xdf) k = 2;
        else if (c >= 0xe0 && c <= 0xef) k = 3;
        else if (c >= 0xf0 && c <= 0xf4) k = 4;
        else return 0;
        for (size_t j = 1; j < k; j++) if (((unsigned char)name[i + j] & 0xc0) != 0x80) return 0;
        i += k; if (++n > 12) return 0;
    }
    return n > 0;
}
// 取域名（确认框用）：https:// 之后到 / : ? # 为止；返回写进 out 的长度（0 = 没有域名），out 总是 NUL 结尾，可为 NULL
// Host extraction (for the confirmation box): after https:// up to / : ? #; returns the copied length (0 = none); out is always NUL-terminated and may be NULL
static inline int dm_pan_host(const char *url, char *out, size_t n) {
    if (out && n) out[0] = 0;
    if (!url) return 0;
    const char *p = strstr(url, "://"); if (!p) return 0; p += 3;
    const char *e = p; while (*e && *e != '/' && *e != ':' && *e != '?' && *e != '#') e++;
    size_t L = (size_t)(e - p); if (!L) return 0;
    if (out && n) { if (L > n - 1) L = n - 1; memcpy(out, p, L); out[L] = 0; }
    return (int)L;
}
// 打开前的最终判定（dm_community_open_pan 打开前再走一遍，单测也用它）：方案必须是 https、URL 必须仍是所属网盘的前缀（dm_pan_url_ok）、主机名非空。
// 认不出的 icon 不放松：它不是四家之一 → dm_pan_url_ok 返回 0。只有这个判定通过，链接才会交给系统浏览器。
// Final check before opening (also run by dm_community_open_pan and covered by unit tests): the scheme must be https, the URL must still carry its provider's prefix
// (dm_pan_url_ok) and the host name must be non-empty. An unknown icon does not relax anything -- it is not one of the four, so dm_pan_url_ok returns 0.
// Only a link that passes this check is ever handed to the system browser.
static inline int dm_pan_open_allowed(const dm_pan_entry *e) {
    if (!e || strncmp(e->url, "https://", 8)) return 0;
    if (!dm_pan_url_ok(e->icon, e->url)) return 0;
    char host[64];
    return dm_pan_host(e->url, host, sizeof host) > 0;
}
