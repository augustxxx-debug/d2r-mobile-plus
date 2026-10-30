// 远端更新清单的严格解析，接口与规则见 dm_manifest.h。JSON 语法检查沿用 RC1 社区入口（dm_community.c，十一审 Q11-01）那套最小解析器的写法。
// Strict parsing of the remote update manifest; interface and rules in dm_manifest.h. The JSON syntax check follows the minimal parser of the RC1 community entry (dm_community.c, Q11-01).
#include "dm_manifest.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const char *p, *e;
    dm_manifest m;
    int n_build, n_version, n_note, n_urgent, n_render, n_comm, n_qq, n_gh, n_site;   // 出现次数（查重复） | occurrences (to catch duplicates)
    const char *bad;                                                         // 第一条语义错（类型 / 取值），NULL = 没有 | the first semantic error (type / value), NULL = none
} mp_t;

static void bad(mp_t *j, const char *why) { if (!j->bad) j->bad = why; }
static void ws(mp_t *j) { while (j->p < j->e && (*j->p == ' ' || *j->p == '\t' || *j->p == '\n' || *j->p == '\r')) j->p++; }
static int hex(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
// 字符串：验语法（转义只许 JSON 那几种，不许控制字符）；out 非空时原样抄字节（遇到转义记 *esc，抄不下记 *over）
// string: validate (only the JSON escapes, no control characters); with out, copy the raw bytes (an escape sets *esc, overflow sets *over)
static int str(mp_t *j, char *out, size_t n, int *esc, int *over) {
    if (j->p >= j->e || *j->p != '"') return 0; j->p++; size_t o = 0;
    while (j->p < j->e) { unsigned char c = (unsigned char)*j->p;
        if (c == '"') { j->p++; if (out) out[o] = 0; return 1; }
        if (c < 0x20) return 0;
        if (c == '\\') { if (esc) *esc = 1; if (j->p + 1 >= j->e) return 0; char x = j->p[1]; size_t k = x == 'u' ? 6 : 2;
            if (x == 'u') { if (j->p + 5 >= j->e || !hex(j->p[2]) || !hex(j->p[3]) || !hex(j->p[4]) || !hex(j->p[5])) return 0; }
            else if (!strchr("\"\\/bfnrt", x)) return 0;
            if (out) for (size_t i = 0; i < k; i++) { if (o + 1 < n) out[o++] = j->p[i]; else if (over) *over = 1; }
            j->p += k; continue; }
        if (out) { if (o + 1 < n) out[o++] = (char)c; else if (over) *over = 1; }
        j->p++; }
    return 0;
}
// 数字：验语法；*integer = 是不是纯整数写法（没有小数点 / 指数），*val = 整数值（超过 2e9 封顶，只用来判范围）
// number: validate; *integer = whether it is written as a plain integer (no point / exponent), *val = the integer value (capped above 2e9, only for the range check)
static int num(mp_t *j, int *integer, double *val) {
    const char *p = j->p, *e = j->e; int neg = 0, in = 1; double v = 0;
    if (p < e && *p == '-') { neg = 1; p++; }
    if (p >= e) return 0;
    if (*p == '0') p++; else if (*p >= '1' && *p <= '9') { while (p < e && *p >= '0' && *p <= '9') { if (v < 3e9) v = v * 10 + (*p - '0'); p++; } } else return 0;
    if (p < e && *p == '.') { in = 0; p++; if (p >= e || *p < '0' || *p > '9') return 0; while (p < e && *p >= '0' && *p <= '9') p++; }
    if (p < e && (*p == 'e' || *p == 'E')) { in = 0; p++; if (p < e && (*p == '+' || *p == '-')) p++; if (p >= e || *p < '0' || *p > '9') return 0; while (p < e && *p >= '0' && *p <= '9') p++; }
    j->p = p; if (integer) *integer = in; if (val) *val = neg ? -v : v; return 1;
}
static int lit(mp_t *j, const char *w) { size_t n = strlen(w); if ((size_t)(j->e - j->p) < n || memcmp(j->p, w, n)) return 0; j->p += n; return 1; }
static int value(mp_t *j, int depth, int ctx);
// 布尔字段：true / false 之外的类型记语义错（语法仍要对） | boolean field: any other type is a semantic error (syntax must still hold)
static int boolean(mp_t *j, int depth, int *out, const char *why) {
    if (lit(j, "true")) { *out = 1; return 1; } if (lit(j, "false")) { *out = 0; return 1; }
    bad(j, why); return value(j, depth, 0);
}
// 不许转义的短字符串字段（version / qq_group / github） | short string fields that may not contain escapes (version / qq_group / github)
static int plain(mp_t *j, int depth, char *out, size_t n, const char *why_type, const char *why_long) {
    if (j->p >= j->e || *j->p != '"') { bad(j, why_type); return value(j, depth, 0); }
    int esc = 0, over = 0; if (!str(j, out, n, &esc, &over)) return 0;
    if (esc) { out[0] = 0; bad(j, why_type); } else if (over) { out[0] = 0; bad(j, why_long); }
    return 1;
}
static int object(mp_t *j, int depth, int ctx) {   // ctx：0 别处 / 1 根对象 / 2 community 对象 | ctx: 0 elsewhere / 1 root object / 2 the community object
    if (depth > 16 || j->p >= j->e || *j->p != '{') return 0; j->p++; ws(j);
    if (j->p < j->e && *j->p == '}') { j->p++; return 1; }
    for (;;) { char key[24]; int kesc = 0, kover = 0;
        if (!str(j, key, sizeof key, &kesc, &kover)) return 0; if (kesc || kover) key[0] = 0;   // 带转义 / 超长的键不会是我们认的 | an escaped / long key is never one of ours
        ws(j); if (j->p >= j->e || *j->p != ':') return 0; j->p++; ws(j);
        int ok;
        if (ctx == 1 && !strcmp(key, "build")) { int in = 0; double v = 0; j->n_build++;
            if (j->p < j->e && (*j->p == '-' || (*j->p >= '0' && *j->p <= '9'))) { ok = num(j, &in, &v); if (ok && (!in || v < 1 || v >= 2e9)) bad(j, "build 不是 1 到 1999999999 之间的整数"); else if (ok) j->m.build = (int)v; }
            else { bad(j, "build 不是数字"); ok = value(j, depth + 1, 0); } }
        else if (ctx == 1 && !strcmp(key, "note")) { j->n_note++;
            if (j->p < j->e && *j->p == '"') ok = str(j, j->m.note, sizeof j->m.note, NULL, NULL); else { bad(j, "note 不是字符串"); ok = value(j, depth + 1, 0); } }
        else if (ctx == 1 && !strcmp(key, "urgent")) { j->n_urgent++; ok = boolean(j, depth + 1, &j->m.urgent, "urgent 不是 true / false"); }
        else if (ctx == 1 && !strcmp(key, "render_safe")) { j->n_render++; ok = boolean(j, depth + 1, &j->m.render_safe, "render_safe 不是 true / false"); }
        else if (ctx == 1 && !strcmp(key, "version")) { j->n_version++; ok = plain(j, depth + 1, j->m.version, sizeof j->m.version, "version 不是不带转义的字符串", "version 超过 20 个字符"); }
        else if (ctx == 1 && !strcmp(key, "community")) { j->n_comm++;
            if (j->p < j->e && *j->p == '{') ok = object(j, depth + 1, 2); else { bad(j, "community 不是对象"); ok = value(j, depth + 1, 0); } }
        else if (ctx == 2 && !strcmp(key, "qq_group")) { j->n_qq++; ok = plain(j, depth + 1, j->m.qq, sizeof j->m.qq, "qq_group 不是不带转义的字符串", "qq_group 超过 12 位"); }
        else if (ctx == 2 && !strcmp(key, "github")) { j->n_gh++; ok = plain(j, depth + 1, j->m.github, sizeof j->m.github, "github 不是不带转义的字符串", "github 超过 160 个字符"); }
        else if (ctx == 2 && !strcmp(key, "site")) { j->n_site++; ok = plain(j, depth + 1, j->m.site, sizeof j->m.site, "site 不是不带转义的字符串", "site 超过 160 个字符"); }
        else ok = value(j, depth + 1, 0);
        if (!ok) return 0;
        ws(j); if (j->p >= j->e) return 0;
        if (*j->p == '}') { j->p++; return 1; }
        if (*j->p != ',') return 0; j->p++; ws(j); }
}
static int array(mp_t *j, int depth) {
    if (depth > 16 || j->p >= j->e || *j->p != '[') return 0; j->p++; ws(j);
    if (j->p < j->e && *j->p == ']') { j->p++; return 1; }
    for (;;) { if (!value(j, depth + 1, 0)) return 0; ws(j); if (j->p >= j->e) return 0;
        if (*j->p == ']') { j->p++; return 1; } if (*j->p != ',') return 0; j->p++; ws(j); }
}
static int value(mp_t *j, int depth, int ctx) {
    if (j->p >= j->e) return 0; char c = *j->p;
    if (c == '{') return object(j, depth, ctx); if (c == '[') return array(j, depth);
    if (c == '"') return str(j, NULL, 0, NULL, NULL); if (c == '-' || (c >= '0' && c <= '9')) return num(j, NULL, NULL);
    return lit(j, "true") || lit(j, "false") || lit(j, "null");
}
static int qq_ok(const char *s) { size_t n = strlen(s); if (n < 5 || n > DM_MANIFEST_QQ_MAX) return 0; for (size_t i = 0; i < n; i++) if (s[i] < '0' || s[i] > '9') return 0; return 1; }
static int gh_ok(const char *s) {   // 同 dm_community.c | same as dm_community.c
    static const char pre[] = "https://github.com/"; size_t n = strlen(s);
    if (n <= sizeof pre - 1 || n > DM_MANIFEST_GH_MAX || strncmp(s, pre, sizeof pre - 1)) return 0;
    for (size_t i = 0; i < n; i++) { unsigned char c = (unsigned char)s[i]; if (!(isalnum(c) || strchr("-._~:/?#[]@!$&'()*+,;=%", c))) return 0; }
    return 1;
}
static int site_ok(const char *s) {   // https:// 开头、后面至少一个字符、只含 URL 字符 | starts with https://, at least one more character, URL characters only
    static const char pre[] = "https://"; size_t n = strlen(s);
    if (n <= sizeof pre - 1 || n > DM_MANIFEST_GH_MAX || strncmp(s, pre, sizeof pre - 1)) return 0;
    for (size_t i = 0; i < n; i++) { unsigned char c = (unsigned char)s[i]; if (!(isalnum(c) || strchr("-._~:/?#[]@!$&'()*+,;=%", c))) return 0; }
    return 1;
}
static int ver_ok(const char *s) { if (!*s) return 0; for (; *s; s++) if (!isalnum((unsigned char)*s) && !strchr(".+-", *s)) return 0; return 1; }
static int fail(char *why, size_t n, const char *fmt, ...) { if (why && n) { va_list a; va_start(a, fmt); vsnprintf(why, n, fmt, a); va_end(a); } return 0; }

int dm_manifest_parse(const char *json, size_t len, dm_manifest *out, char *why, size_t why_n) {
    if (why && why_n) why[0] = 0;
    if (!json || !len) return fail(why, why_n, "空内容");
    if (len > DM_MANIFEST_MAX) return fail(why, why_n, "超过 %d 字节（%zu）", DM_MANIFEST_MAX, len);
    if (len >= 3 && !memcmp(json, "\xef\xbb\xbf", 3)) return fail(why, why_n, "开头带 BOM");
    mp_t j; memset(&j, 0, sizeof j); j.p = json; j.e = json + len; ws(&j);
    int ok = j.p < j.e && *j.p == '{' && object(&j, 1, 1); if (ok) { ws(&j); ok = j.p == j.e; }
    if (!ok) return fail(why, why_n, "JSON 语法错（第 %zu 字节附近），或根不是对象", (size_t)(j.p - json));
    if (j.n_build > 1 || j.n_version > 1 || j.n_note > 1 || j.n_urgent > 1 || j.n_render > 1 || j.n_comm > 1 || j.n_qq > 1 || j.n_gh > 1 || j.n_site > 1) return fail(why, why_n, "有重复的键");
    if (j.bad) return fail(why, why_n, "%s", j.bad);
    if (!j.n_build || !j.n_note || !j.n_urgent) return fail(why, why_n, "缺必填字段（build / note / urgent）");
    if (j.n_version && !ver_ok(j.m.version)) return fail(why, why_n, "version 只许数字、字母与 .+-，且不能为空");
    if (j.n_qq && !qq_ok(j.m.qq)) return fail(why, why_n, "qq_group 须是 5–12 位数字");
    if (j.n_gh && !gh_ok(j.m.github)) return fail(why, why_n, "github 须以 https://github.com/ 开头、只含 URL 字符");
    if (j.n_site && !site_ok(j.m.site)) return fail(why, why_n, "site 须以 https:// 开头、只含 URL 字符");
    if (out) *out = j.m;
    return 1;
}
