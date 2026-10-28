// 社区入口，接口见 dm_community.h。
// 清单字段（版本文件里与 build / note / urgent 并列）：  "community": { "qq_group": "1129913678", "github": "https://github.com/<fork>/d2r-mobile-plus" }
//   qq_group：5–12 位数字；github：以 https://github.com/ 开头、≤ 160 个 URL 字符。两项都可省；一项不合规就当这一项没有并记红；两项都没有 = 当无字段。
//   解析在这里用 C 做（严格的最小 JSON 解析器，十一审 Q11-01：整段必须是合法根对象，只认根对象直接子级 community 及其直接子级；语法错 / 重复 / 嵌套同名 / 转义都当格式错），外壳只把原文交过来；单测 t_community.c 覆盖有 / 无 / 坏 / 缓存回退与这些坏形状。
// 缓存 community.txt（与 sw_custom.txt 同目录）：一行一个「键=值」，先写临时文件再换名；清单明确没有字段时删掉（旧值不留）。
// 画面（用户：「不要用原生的弹窗，用游戏内弹窗」）：边框 15 片 TouchCommTile（我们通知框同一套美术、自己一套切片 Ztouch_comm）+ 标题 + 三行正文（值 / 提示 / 「已复制」）+ 两颗原版前端按钮（复制群号 或 复制链接 二选一显示、确定），
//   布局 touch_layout.py _community，挂在暂停菜单面板（两份）和自定义页面板末尾，平时在屏外、字空着；这里每帧在游戏主线程写字、按面板矩形与缩放算出居中位置。
//   触屏点按钮；手柄接管 A = 复制、B = 收起；Mac 鼠标点按钮或键盘同手柄。复制走外壳登记的剪贴板回调，成功后第三行写「已复制」（游戏内文字，不用系统弹窗）。
// 日志固定前缀（分享诊断日志摘要取最后一行）：「社区入口：清单有字段 / 清单无字段 / 清单拉取失败」「社区入口：已显示」「社区入口：复制 群号 / 链接」。
// Community entry; the interface is in dm_community.h.
// Manifest field (next to build / note / urgent in the version file):  "community": { "qq_group": "1129913678", "github": "https://github.com/<fork>/d2r-mobile-plus" }
//   qq_group: 5-12 digits; github: starts with https://github.com/, at most 160 URL characters. Both are optional; an invalid one counts as absent and is logged red; both absent = no field.
//   Parsing is done here in C (a strict minimal JSON parser, Q11-01: the whole text must be a valid root object; only the root object's direct child community and its direct children count; syntax errors / duplicates / nested same-name keys / escapes are format errors); the shells only hand over the raw text; t_community.c covers present / absent / malformed / cache fallback and these bad shapes.
// Cache community.txt (next to sw_custom.txt): one "key=value" per line, written to a temporary file then renamed; deleted when the manifest explicitly has no field (no stale value is kept).
// Picture (user: "do not use a native popup, use an in-game one"): 15 frame tiles TouchCommTile (same art as our notification box, its own slices Ztouch_comm) + title + three body lines (value / hint / "Copied") + two original front-end buttons (Copy number or Copy link, one shown; OK),
//   layout touch_layout.py _community, hung at the end of the pause menu panel (both) and the Custom page panel, off screen with empty text; here, every frame on the game main thread, the text is written and the centered position computed from the panel's rect and scale.
//   Touch taps the buttons; the controller takes over A = copy, B = dismiss; the Mac clicks the buttons or uses the controller keys. Copy goes through the shell's clipboard callback; on success the third line says "Copied" (in-game text, no system popup).
// Fixed log prefixes (the share-log summary takes the last line): "社区入口：清单有字段 / 清单无字段 / 清单拉取失败", "社区入口：已显示", "社区入口：复制 群号 / 链接".
#include "dm_community.h"
#include "dm_custom.h"
#include "dm_crashfence.h"
#include "dm_i18n.h"
#include "dm_input.h"
#include "dm_widget.h"
#include "dm_notice_geometry.h"
#include "dm_cursor.h"
#include "dm_sel_frame.h"
#include "hle.h"
#include <ctype.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define QQ_MAX 12
#define GH_MAX 160
#define MANIFEST_MAX 4096   // 外壳也按 4096 字节封顶（dm_update.m） | the shells cap at 4096 bytes too (dm_update.m)
static pthread_mutex_t g_cm_mu = PTHREAD_MUTEX_INITIALIZER;   // 护 g_qq / g_gh（清单从网络线程来，泵在游戏主线程读） | guards g_qq / g_gh (the manifest arrives on a network thread; the pump reads on the game main thread)
static char g_qq[QQ_MAX + 1], g_gh[GH_MAX + 1];
static _Atomic int g_loaded_cm;
static _Atomic int g_show, g_ack, g_copied;   // g_show：0 不显示 / 1 要显示 / 2 显示着；g_copied：0 / 1 已复制 / -1 复制失败 | g_show: 0 none / 1 pending / 2 shown; g_copied: 0 / 1 copied / -1 failed
static int (*g_copy)(const char *);
static _Atomic unsigned g_copies;
static _Atomic int g_from;   // 从哪里点开（只进日志）：1 暂停菜单 / 2 主菜单 / 3 自定义页 | which entry opened it (log only): 1 pause menu / 2 main menu / 3 Custom page
static _Atomic int g_mode;   // 1 社区框（社区段 + 「关于」段）/ 2 纯「关于」框（版本 + 鸣谢，主菜单「关于」行） | 1 community box (community + About section) / 2 About-only box (version + credits, the main menu's About row)

static int cache_path(char *p, size_t n) { char d[1024]; if (!dm_custom_dir(d, sizeof d)) return 0; return snprintf(p, n, "%s/community.txt", d) < (int)n; }
static int qq_ok(const char *s) { size_t n = strlen(s); if (n < 5 || n > QQ_MAX) return 0; for (size_t i = 0; i < n; i++) if (s[i] < '0' || s[i] > '9') return 0; return 1; }
static int gh_ok(const char *s) {
    static const char pre[] = "https://github.com/"; size_t n = strlen(s);
    if (n <= sizeof pre - 1 || n > GH_MAX || strncmp(s, pre, sizeof pre - 1)) return 0;
    for (size_t i = 0; i < n; i++) { unsigned char c = (unsigned char)s[i]; if (!(isalnum(c) || strchr("-._~:/?#[]@!$&'()*+,;=%", c))) return 0; }
    return 1;
}
static void set_values(const char *qq, const char *gh) { pthread_mutex_lock(&g_cm_mu); snprintf(g_qq, sizeof g_qq, "%s", qq ? qq : ""); snprintf(g_gh, sizeof g_gh, "%s", gh ? gh : ""); pthread_mutex_unlock(&g_cm_mu); }
static void get_values(char *qq, char *gh) { pthread_mutex_lock(&g_cm_mu); memcpy(qq, g_qq, sizeof g_qq); memcpy(gh, g_gh, sizeof g_gh); pthread_mutex_unlock(&g_cm_mu); }
static int cache_write(const char *qq, const char *gh) {
    char p[1100], t[1120]; if (!cache_path(p, sizeof p)) return 0;
    snprintf(t, sizeof t, "%s.tmp", p); FILE *f = fopen(t, "w"); if (!f) return 0;
    int ok = 1; if (qq[0]) ok &= fprintf(f, "qq_group=%s\n", qq) > 0; if (gh[0]) ok &= fprintf(f, "github=%s\n", gh) > 0;
    ok = fclose(f) == 0 && ok; return ok && rename(t, p) == 0;
}
static void cache_delete(void) { char p[1100]; if (cache_path(p, sizeof p)) unlink(p); }

// ---- 严格的最小 JSON 解析（十一审 Q11-01）：整段输入必须是一个合法的 JSON 根对象（对象 / 数组 / 字符串 / 数字 / true / false / null 全按语法验），
// 只认根对象的直接子级 "community"（必须是对象）和它的直接子级 "qq_group" / "github"（必须是不带转义的字符串）；嵌套更深的同名键一律忽略；
// 重复的 community / qq_group / github 当格式错。语法错、尾逗号、非对象根、根后有多余内容 = 格式错。深度上限 16 层，不递归到栈爆
// Strict minimal JSON parsing (Q11-01): the whole input must be one valid JSON root object (object / array / string / number / true / false / null all validated);
// only the root object's direct child "community" (must be an object) and its direct children "qq_group" / "github" (must be strings without escapes) are honored; deeper same-name keys are ignored;
// a duplicate community / qq_group / github is a format error. Syntax error, trailing comma, non-object root, trailing content = format error. Depth capped at 16
#define CR_NAME_MAX 32   // 鸣谢名字上限（字节） | credit name limit (bytes)
#define CR_MAX 24        // 两份名单合计上限 | both lists together
typedef struct { const char *p, *e; char qq[QQ_MAX + 1], gh[GH_MAX + 1]; int n_comm, n_qq, n_gh, bad_type, too_long;
    char cr[CR_MAX][CR_NAME_MAX + 1]; int n_cr, n_special, n_thanks, n_credits, bad_credits; } jp_t;   // credits：RC1 只解析、去重、计数进日志，不渲染不缓存（RC2 覆盖静态缺省） | credits: RC1 only parses, dedupes and logs counts; not rendered or cached (RC2 overrides the static default)
static void js_ws(jp_t *j) { while (j->p < j->e && (*j->p == ' ' || *j->p == '\t' || *j->p == '\n' || *j->p == '\r')) j->p++; }
static int js_hex(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
// 字符串：验语法（转义只许 \" \\ \/ \b \f \n \r \t \uXXXX，不许控制字符）；out 非空时原样抄（遇到转义记 *esc，抄不下记 *over） | string: validate (escapes only the JSON set, no control characters); with out, copy raw bytes (an escape sets *esc, overflow sets *over)
static int js_string(jp_t *j, char *out, size_t n, int *esc, int *over) {
    if (j->p >= j->e || *j->p != '"') return 0; j->p++; size_t o = 0;
    while (j->p < j->e) { unsigned char c = (unsigned char)*j->p;
        if (c == '"') { j->p++; if (out) out[o] = 0; return 1; }
        if (c < 0x20) return 0;
        if (c == '\\') { if (esc) *esc = 1; if (j->p + 1 >= j->e) return 0; char x = j->p[1];
            if (x == 'u') { if (j->p + 5 >= j->e || !js_hex(j->p[2]) || !js_hex(j->p[3]) || !js_hex(j->p[4]) || !js_hex(j->p[5])) return 0; j->p += 6; continue; }
            if (!strchr("\"\\/bfnrt", x)) return 0; j->p += 2; continue; }
        if (out) { if (o + 1 < n) out[o++] = (char)c; else if (over) *over = 1; }
        j->p++; }
    return 0;
}
static int js_number(jp_t *j) {
    const char *p = j->p, *e = j->e; if (p < e && *p == '-') p++;
    if (p >= e) return 0; if (*p == '0') p++; else if (*p >= '1' && *p <= '9') { while (p < e && *p >= '0' && *p <= '9') p++; } else return 0;
    if (p < e && *p == '.') { p++; if (p >= e || *p < '0' || *p > '9') return 0; while (p < e && *p >= '0' && *p <= '9') p++; }
    if (p < e && (*p == 'e' || *p == 'E')) { p++; if (p < e && (*p == '+' || *p == '-')) p++; if (p >= e || *p < '0' || *p > '9') return 0; while (p < e && *p >= '0' && *p <= '9') p++; }
    j->p = p; return 1;
}
static int js_lit(jp_t *j, const char *w) { size_t n = strlen(w); if ((size_t)(j->e - j->p) < n || memcmp(j->p, w, n)) return 0; j->p += n; return 1; }
static int js_value(jp_t *j, int depth, int ctx);   // ctx：0 别处 / 1 根对象 / 2 community 对象 / 3 credits 对象 | ctx: 0 elsewhere / 1 root object / 2 the community object / 3 the credits object
// credits 的名单数组：只收不带转义、≤ 32 字节的字符串，去重，合计 ≤ 24；别的元素 / 超限记 bad_credits（整个 credits 忽略），语法仍要对 | a credits name array: only escape-free strings ≤ 32 bytes, deduped, ≤ 24 in all; anything else / over the cap sets bad_credits (credits ignored as a whole); syntax must still hold
static int js_names(jp_t *j, int depth, int which) {
    if (depth > 16 || j->p >= j->e || *j->p != '[') { j->bad_credits = 1; return js_value(j, depth, 0); }
    j->p++; js_ws(j); if (j->p < j->e && *j->p == ']') { j->p++; return 1; }
    for (;;) { if (j->p < j->e && *j->p == '"') { char nm[CR_NAME_MAX + 1]; int esc = 0, over = 0; if (!js_string(j, nm, sizeof nm, &esc, &over)) return 0;
            if (esc || over || !nm[0]) j->bad_credits = 1;
            else { int dup = 0; for (int k = 0; k < j->n_cr; k++) dup |= !strcmp(j->cr[k], nm);
                if (!dup) { if (j->n_cr < CR_MAX) { snprintf(j->cr[j->n_cr++], CR_NAME_MAX + 1, "%s", nm); if (which) j->n_thanks++; else j->n_special++; } else j->bad_credits = 1; } } }
        else { j->bad_credits = 1; if (!js_value(j, depth + 1, 0)) return 0; }
        js_ws(j); if (j->p >= j->e) return 0; if (*j->p == ']') { j->p++; return 1; } if (*j->p != ',') return 0; j->p++; js_ws(j); }
}
static int js_object(jp_t *j, int depth, int ctx) {
    if (depth > 16 || j->p >= j->e || *j->p != '{') return 0; j->p++; js_ws(j);
    if (j->p < j->e && *j->p == '}') { j->p++; return 1; }
    for (;;) { char key[24]; int kesc = 0, kover = 0;
        if (!js_string(j, key, sizeof key, &kesc, &kover)) return 0; if (kesc || kover) key[0] = 0;   // 带转义 / 超长的键不会是我们认的那三个 | an escaped / long key is never one of ours
        js_ws(j); if (j->p >= j->e || *j->p != ':') return 0; j->p++; js_ws(j);
        if (ctx == 1 && !strcmp(key, "community")) { j->n_comm++;
            if (j->p < j->e && *j->p == '{') { if (!js_object(j, depth + 1, 2)) return 0; } else { j->bad_type = 1; if (!js_value(j, depth + 1, 0)) return 0; } }
        else if (ctx == 2 && (!strcmp(key, "qq_group") || !strcmp(key, "github"))) { int gh = key[0] == 'g'; if (gh) j->n_gh++; else j->n_qq++;
            if (j->p < j->e && *j->p == '"') { int esc = 0, over = 0; char *out = gh ? j->gh : j->qq; size_t n = gh ? sizeof j->gh : sizeof j->qq;
                if (!js_string(j, out, n, &esc, &over)) return 0; if (esc || over) { out[0] = 0; j->too_long |= over; j->bad_type |= esc; } }
            else { j->bad_type = 1; if (!js_value(j, depth + 1, 0)) return 0; } }
        else if (ctx == 1 && !strcmp(key, "credits")) { j->n_credits++;
            if (j->p < j->e && *j->p == '{') { if (!js_object(j, depth + 1, 3)) return 0; } else { j->bad_credits = 1; if (!js_value(j, depth + 1, 0)) return 0; } }
        else if (ctx == 3 && (!strcmp(key, "special") || !strcmp(key, "thanks"))) { if (!js_names(j, depth + 1, key[0] == 't')) return 0; }
        else if (!js_value(j, depth + 1, 0)) return 0;
        js_ws(j); if (j->p >= j->e) return 0;
        if (*j->p == '}') { j->p++; return 1; }
        if (*j->p != ',') return 0; j->p++; js_ws(j); }
}
static int js_array(jp_t *j, int depth) {
    if (depth > 16 || j->p >= j->e || *j->p != '[') return 0; j->p++; js_ws(j);
    if (j->p < j->e && *j->p == ']') { j->p++; return 1; }
    for (;;) { if (!js_value(j, depth + 1, 0)) return 0; js_ws(j); if (j->p >= j->e) return 0;
        if (*j->p == ']') { j->p++; return 1; } if (*j->p != ',') return 0; j->p++; js_ws(j); }
}
static int js_value(jp_t *j, int depth, int ctx) {
    if (j->p >= j->e) return 0; char c = *j->p;
    if (c == '{') return js_object(j, depth, ctx); if (c == '[') return js_array(j, depth);
    if (c == '"') return js_string(j, NULL, 0, NULL, NULL); if (c == '-' || (c >= '0' && c <= '9')) return js_number(j);
    return js_lit(j, "true") || js_lit(j, "false") || js_lit(j, "null");
}
// 解析整份清单：1 合法根对象（结果在 *j）/ 0 语法错（*at = 出错位置） | parse the whole manifest: 1 valid root object (result in *j) / 0 syntax error (*at = where)
static int js_parse(jp_t *j, const char *json, size_t len, size_t *at) {
    memset(j, 0, sizeof *j); j->p = json; j->e = json + len; js_ws(j);
    int ok = j->p < j->e && *j->p == '{' && js_object(j, 1, 1); if (ok) { js_ws(j); ok = j->p == j->e; }
    *at = (size_t)(j->p - json); return ok;
}
static void drop_values(void) { set_values("", ""); cache_delete(); }   // 格式错 / 无字段：清内存与缓存 | malformed / absent: clear memory and cache

int dm_community_manifest(const char *json, size_t len) {
    char qq[QQ_MAX + 1] = "", gh[GH_MAX + 1] = ""; get_values(qq, gh);
    if (!json) { LOG("社区入口：清单拉取失败，沿用缓存（群号 %s，链接 %s）", qq[0] ? "有" : "无", gh[0] ? "有" : "无"); return -1; }
    if (len > MANIFEST_MAX) { drop_values(); LOG("【红】社区入口：清单格式错（超过 %d 字节），入口不显示（缓存已清）", MANIFEST_MAX); return 0; }
    jp_t j; size_t at; if (!js_parse(&j, json, len, &at)) { drop_values(); LOG("【红】社区入口：清单格式错（JSON 语法：第 %zu 字节附近），入口不显示（缓存已清）", at); return 0; }
    if (j.n_comm == 0) { drop_values(); LOG("社区入口：清单无字段，入口不显示（缓存已清）"); return 0; }
    if (j.n_comm > 1 || j.n_qq > 1 || j.n_gh > 1) { drop_values(); LOG("【红】社区入口：清单格式错（community / qq_group / github 重复），入口不显示（缓存已清）"); return 0; }
    if (j.bad_type && !j.qq[0] && !j.gh[0]) { drop_values(); LOG("【红】社区入口：清单格式错（community 不是对象，或值不是字符串 / 带转义），入口不显示（缓存已清）"); return 0; }
    int bad = 0; if (j.qq[0] && !qq_ok(j.qq)) { bad++; j.qq[0] = 0; } if (j.gh[0] && !gh_ok(j.gh)) { bad++; j.gh[0] = 0; }
    bad += j.bad_type + j.too_long;
    if (!j.qq[0] && !j.gh[0]) { drop_values(); LOG("【红】社区入口：清单格式错（%d 项不合规：群号须 5–12 位数字，链接须 https://github.com/ 开头、不超过 %d 字符、不带转义），入口不显示（缓存已清）", bad, GH_MAX); return 0; }
    set_values(j.qq, j.gh); int wr = cache_write(j.qq, j.gh);
    if (j.n_credits > 1 || j.bad_credits) LOG("【红】社区入口：清单 credits 字段格式坏（%s），忽略（RC1 只计数不渲染）", j.n_credits > 1 ? "重复" : "须是 { special: [名字…], thanks: [名字…] }，名字 ≤ 32 字节、不带转义、合计 ≤ 24");
    LOG("社区入口：清单有字段 群号=%s 链接=%s%s%s credits=特别 %d / 鸣谢 %d", j.qq[0] ? j.qq : "无", j.gh[0] ? j.gh : "无", bad ? "（另一项格式坏，已忽略）" : "", wr ? "" : "【红】缓存写不进设置目录", j.bad_credits || j.n_credits > 1 ? 0 : j.n_special, j.bad_credits || j.n_credits > 1 ? 0 : j.n_thanks);
    return 1;
}
static void cache_read(void) {
    char p[1100]; if (!cache_path(p, sizeof p)) { LOG("社区入口：没有设置目录，不读缓存"); return; }
    FILE *f = fopen(p, "r"); if (!f) { LOG("社区入口：没有缓存，入口不显示（等清单）"); return; }
    char ln[256], qq[QQ_MAX + 1] = "", gh[GH_MAX + 1] = "";
    while (fgets(ln, sizeof ln, f)) { size_t n = strcspn(ln, "\r\n"); ln[n] = 0;
        if (!strncmp(ln, "qq_group=", 9) && qq_ok(ln + 9)) snprintf(qq, sizeof qq, "%s", ln + 9);
        else if (!strncmp(ln, "github=", 7) && gh_ok(ln + 7)) snprintf(gh, sizeof gh, "%s", ln + 7); }
    fclose(f); set_values(qq, gh);
    LOG("社区入口：缓存 群号=%s 链接=%s", qq[0] ? qq : "无", gh[0] ? gh : "无");
}
void dm_community_load(void) {
    if (atomic_exchange(&g_loaded_cm, 1)) return;
    const char *tf = getenv("DM_TEST_COMMUNITY");   // 测试开关（只在测试局读，产品局连环境变量都不看）：从文件读一份清单当这次拉到的 | test switch (read only in test runs): read a manifest from a file as if fetched now
    if (tf && *tf && dm_crashfence_test_env()) {
        FILE *f = fopen(tf, "r"); char buf[MANIFEST_MAX + 1]; size_t n = f ? fread(buf, 1, sizeof buf - 1, f) : 0; if (f) fclose(f); buf[n] = 0;
        LOG("社区入口：测试开关 DM_TEST_COMMUNITY 读清单（%zu 字节）", n); dm_community_manifest(f ? buf : NULL, n); return; }
    cache_read();
}
int dm_community_value(int lang_group, char *out, size_t n) {
    char qq[QQ_MAX + 1], gh[GH_MAX + 1]; get_values(qq, gh); int k = 0; const char *v = "";
    if (lang_group == DM_LG_ZH && qq[0]) { k = 1; v = qq; } else if (gh[0]) { k = 2; v = gh; }
    if (out && n) snprintf(out, n, "%s", v);
    return k;
}
int dm_community_available(void) { return dm_community_value(dm_lang_group(), NULL, 0) != 0; }
void dm_community_open(int from) {   // from：1 暂停菜单 / 2 主菜单 / 3 自定义页按钮 / 4 自定义页手柄 A | from: 1 pause menu / 2 main menu / 3 Custom page button / 4 Custom page controller A
    if (!dm_community_available()) { LOG("【红】社区入口：点了「加入社区讨论」（%s）但当前语言没有可显示的值", from == 1 ? "暂停菜单" : from == 2 ? "主菜单" : "自定义页"); return; }
    atomic_store(&g_copied, 0); atomic_store(&g_from, from >= 3 ? 3 : from); atomic_store(&g_mode, 1); atomic_store(&g_show, 1); LOG("社区入口：点了「加入社区讨论」（%s%s）", from == 1 ? "暂停菜单" : from == 2 ? "主菜单" : "自定义页", from == 4 ? "手柄 A" : "");
}
void dm_community_open_about(void) {   // 主菜单「关于」行：只有版本 + 鸣谢的框，没有社区值也开 | the main menu's About row: the version + credits box, opens without a community value too
    atomic_store(&g_copied, 0); atomic_store(&g_from, 2); atomic_store(&g_mode, 2); atomic_store(&g_show, 1); LOG("社区入口：点了「关于」（主菜单）");
}
void dm_community_copy_init(int (*cb)(const char *)) { g_copy = cb; }
unsigned dm_community_copy_count(void) { return atomic_load(&g_copies); }
int dm_community_shown(void) { return atomic_load(&g_show) == 2; }
static void do_copy(int by) {   // by：1 点按钮 / 2 手柄 A | by: 1 button / 2 controller A
    char v[GH_MAX + 1]; int k = dm_community_value(dm_lang_group(), v, sizeof v);
    if (!k) { atomic_store(&g_copied, -1); LOG("【红】社区入口：复制失败（没有可复制的值）"); return; }
    int ok = g_copy ? g_copy(v) : 0;
    if (ok) { atomic_fetch_add(&g_copies, 1); atomic_store(&g_copied, 1); LOG("社区入口：复制 %s %s（%s）", k == 1 ? "群号" : "链接", v, by == 2 ? "手柄 A" : "点按钮"); }
    else { atomic_store(&g_copied, -1); LOG("【红】社区入口：复制失败（%s；%s）", g_copy ? "剪贴板回调返回失败" : "外壳没登记剪贴板回调", by == 2 ? "手柄 A" : "点按钮"); }
}
void dm_community_on_uimsg(const char *cat, const char *act, const char *arg) {   // 界面消息回调（diag.c 转来，游戏主线程） | UI message callback (forwarded from diag.c, game main thread)
    if (!cat || !act || !arg || strcmp(cat, "PanelManager") || strcmp(act, "ClosePanel")) return;
    if (!strcmp(arg, "TouchCommunity")) dm_community_open(1);          // 暂停菜单行 | pause-menu row
    else if (!strcmp(arg, "TouchCommunityFe")) dm_community_open(2);    // 主菜单行 | main-menu row
    else if (!strcmp(arg, "TouchCommunityOpt")) dm_community_open(3);   // 自定义页「查看」 | Custom page View
    else if (!strcmp(arg, "TouchAbout")) dm_community_open_about();      // 主菜单「关于」 | main-menu About
    else if (!strcmp(arg, "TouchCommCopy") && atomic_load(&g_show) == 2) do_copy(1);
    else if (!strcmp(arg, "TouchCommOk") && atomic_load(&g_show) == 2) atomic_store(&g_ack, 1);   // 1 点按钮 / 2 手柄 B（只进日志） | 1 button / 2 controller B (log only)
}

// ---- 鸣谢（自定义页「关于」组的子块，用户 2026-10-28）：名字放代码缺省常量（RC2 由清单 credits 覆盖），宿主按语言拼行：每行 ≤ 4 名且估算宽不超名单框（36 号 × 0.7 ≈ 25 单位 / 字，框 1110 → 44 单位），
// 「特别鸣谢」名单末尾接括号说明（六套文案），「鸣谢」为空整行不显示；封顶 6 行，超出末行加「…」并记一条日志；续行标签留空；中文用「、」、其他语言用「, 」分隔
// Credits (sub-block of the About group, user 2026-10-28): names are a default constant in code (RC2: the manifest credits field overrides); the host assembles lines per language: ≤ 4 names per line and estimated width within the name box (36 × 0.7 ≈ 25 units per char, box 1110 → 44 units),
// the special list ends with a parenthetical note (six texts), an empty thanks list shows no line; capped at 6 lines with "…" on the last and one log line; continuation lines have no label; "、" in Chinese, ", " elsewhere
static const char *const k_special_default[] = { "jhaizhou-ops" };   // 上游 d2r-mobile 原作者的公开 GitHub 账号名（README 引用的 https://github.com/jhaizhou-ops/d2r-mobile）；不写真名、不写别的信息 | the public GitHub handle of the upstream d2r-mobile author; no real name, nothing else
#define CR_LINES 6
#define CR_PER_LINE 4
#define CR_LINE_UNITS 44
typedef struct { int kind; char text[192]; } cr_line_t;   // kind：1 特别鸣谢首行 / 2 鸣谢首行 / 0 续行 | kind: 1 first special line / 2 first thanks line / 0 continuation
static int cr_units(const char *s) { int u = 0; for (; *s; s++) u += ((unsigned char)*s & 0xC0) == 0x80 ? 0 : ((unsigned char)*s >= 0x80 ? 2 : 1); return u; }   // 估算宽：ASCII 1、多字节字符 2 | estimated width: ASCII 1, multibyte 2
static int cr_list(const char *const *names, int n, int kind, const char *sep, const char *note, cr_line_t *out, int o, int max) {   // sep = 名字间分隔（表 dmCreditsSep：中文「、」、其他「, 」）；返回新的行数 | sep = separator between names (table dmCreditsSep); returns the new line count
    char cur[192] = ""; int cnt = 0, first = 1;
    for (int i = 0; i < n; i++) { char cand[192]; snprintf(cand, sizeof cand, "%s%s%s", cur, cur[0] ? sep : "", names[i]);
        if (cnt > 0 && (cnt >= CR_PER_LINE || cr_units(cand) > CR_LINE_UNITS)) { if (o < max) { out[o].kind = first ? kind : 0; snprintf(out[o].text, sizeof out[o].text, "%s", cur); } o++; first = 0; snprintf(cur, sizeof cur, "%s", names[i]); cnt = 1; }
        else { snprintf(cur, sizeof cur, "%s", cand); cnt++; } }
    if (note && *note) { char cand[192]; snprintf(cand, sizeof cand, "%s%s", cur, note);
        if (cur[0] && cr_units(cand) > CR_LINE_UNITS) { if (o < max) { out[o].kind = first ? kind : 0; snprintf(out[o].text, sizeof out[o].text, "%s", cur); } o++; first = 0; snprintf(cur, sizeof cur, "%s", note); }
        else snprintf(cur, sizeof cur, "%s", cand); }
    if (cur[0]) { if (o < max) { out[o].kind = first ? kind : 0; snprintf(out[o].text, sizeof out[o].text, "%s", cur); } o++; }
    return o;
}
// 两份名单 → 行；*truncated = 超过 max 行（末行已加「…」） | both lists → lines; *truncated = more than max lines (the last line got "…")
static int credits_lines(const char *const *sp, int nsp, const char *const *th, int nth, const char *sep, const char *note, cr_line_t *out, int max, int *truncated) {
    int o = cr_list(sp, nsp, 1, sep, note, out, 0, max); o = cr_list(th, nth, 2, sep, NULL, out, o, max); *truncated = o > max;
    if (o > max) { o = max; size_t L = strlen(out[o - 1].text); if (L + 4 < sizeof out[o - 1].text) strcpy(out[o - 1].text + L, "…"); }
    return o;
}
static void co_text(uint8_t *b, uint64_t w, const char *s);
static void co_place(uint64_t w, int x, int y);
static uint64_t co_find(uint64_t w, const char *name, int dep);
static void credits_render(uint8_t *b, uint64_t sp) {   // 自定义页开着时每帧（游戏主线程）：按当前语言拼一次并缓存，摆标签 / 名单行、改「关于」容器高度 | every frame while the Custom page shows: assemble once per language and cache; place labels / lines, set the About container's height
    static int grp = -1, n, trunc; static cr_line_t lines[CR_LINES]; int g = dm_lang_group();
    if (g != grp) { grp = g; n = credits_lines(k_special_default, (int)(sizeof k_special_default / sizeof *k_special_default), NULL, 0, dm_str(b, "dmCreditsSep"), dm_str(b, "dmCreditsUpstreamNote"), lines, CR_LINES, &trunc);
        LOG("社区入口：鸣谢 %d 行（特别鸣谢 %d 名，鸣谢 0 名）%s", n, (int)(sizeof k_special_default / sizeof *k_special_default), trunc ? "，超过上限已在末尾加「…」" : ""); }
    uint64_t about = co_find(sp, "TouchAboutRow", 0); if (!about) return;
    int y0 = 3 * 180, ys = -1, yt = -1;
    for (int k = 0; k < CR_LINES; k++) { char nm[24]; snprintf(nm, sizeof nm, "TouchCreditLine%d", k); uint64_t w = co_find(about, nm, 0); if (!w) continue;
        if (k < n) { co_text(b, w, lines[k].text); co_place(w, 620, y0 + 90 * k); if (lines[k].kind == 1) ys = y0 + 90 * k; if (lines[k].kind == 2) yt = y0 + 90 * k; }
        else { co_text(b, w, ""); co_place(w, 99999, y0); } }
    uint64_t l0 = co_find(about, "TouchCreditLabel0", 0), l1 = co_find(about, "TouchCreditLabel1", 0);
    // 标签横坐标 = 行文字缩进后的位置（dm_sel_frame.h DM_ROW_INDENT）：放 0 会被 dm_custom.c indent_rows 当成「载入 x = 0 的叶子」每帧挪到 40、这里再挪回 0，来回拉扯（十二审 Q12-01 顺带）
    // label x = the indented row-text position (DM_ROW_INDENT): at 0, indent_rows would take it for a leaf loaded at x = 0 and move it to 40 every frame while this moved it back (found with Q12-01)
    if (l0) { co_text(b, l0, ys >= 0 ? dm_str(b, "dmCreditsSpecial") : ""); co_place(l0, ys >= 0 ? DM_ROW_INDENT : 99999, ys >= 0 ? ys : y0); }
    if (l1) { co_text(b, l1, yt >= 0 ? dm_str(b, "dmCreditsThanks") : ""); co_place(l1, yt >= 0 ? DM_ROW_INDENT : 99999, yt >= 0 ? yt : y0); }
    int32_t *r = (int32_t *)(about + 0x70), h = y0 + 90 * (n > 0 ? n : 1);   // 容器高度 = 子标题下沿 + 行数 × 行距（滚动区按它算内容高度） | container height = below the sub-title + lines × pitch (the scroll area sizes by it)
    if (r[3] != h) { r[3] = h; ((void (*)(uint64_t))*(uint64_t *)(*(uint64_t *)about + 0x10))(about); }
}
// ---- 画面 | picture ----
static uint8_t *co_base(void) { static uint8_t *b; if (!b) for (int i = 0; i < g_nmod; i++) if (!strcmp(g_mod[i].name, "main")) b = (uint8_t *)g_mod[i].base; return b; }
static uint64_t co_find(uint64_t w, const char *name, int dep) {   // 按名字找控件：名字 +8、子控件数组 +0x58、个数 +0x60（同 dm_crashfence.c） | find a widget by name (same as dm_crashfence.c)
    if (!w || dep > 6) return 0; const char *nm = *(const char **)(w + 8); if (nm && !strcmp(nm, name)) return w;
    uint64_t ch = *(uint64_t *)(w + 0x58), n = *(uint64_t *)(w + 0x60); if (!ch || n > 256) return 0;
    for (uint64_t i = 0; i < n; i++) { uint64_t r = co_find(*(uint64_t *)(ch + 8 * i), name, dep + 1); if (r) return r; }
    return 0;
}
static void co_place(uint64_t w, int x, int y) {   // 只挪自己的控件（名字 Touch 开头）：矩形 +0x70 / +0x74，改完调虚表 +0x10 重算（同 dm_crashfence.c cf_place） | only our own widgets (names starting with Touch): rect +0x70 / +0x74, then vtable +0x10 (same as cf_place)
    const char *nm = w ? *(const char **)(w + 8) : NULL; if (!nm || strncmp(nm, "Touch", 5)) return;
    int32_t *r = (int32_t *)(w + 0x70); if (r[0] == x && r[1] == y) return; r[0] = x; r[1] = y; ((void (*)(uint64_t))*(uint64_t *)(*(uint64_t *)w + 0x10))(w);
}
static void co_text(uint8_t *b, uint64_t w, const char *s) {   // 文字框设文本（同 dm_crashfence.c cf_text；不是文字框不动） | set a text box's text (same as cf_text)
    if (!w || *(uint64_t *)w - (uint64_t)(uintptr_t)b != DM_VT_TEXT) return;
    size_t n = strlen(s); if (*(uint64_t *)(w + 0x90) == n && !memcmp(*(const char **)(w + 0x88), s, n)) return;
    ((void *(*)(uint64_t, const char *, uint64_t))(b + 0x136e5c))(w + 0x88, s, n); ((void (*)(uint64_t))(b + 0xadc434))(w);
}
static void co_tile(uint8_t *b, uint64_t w, const int want[4]) {   // 边框片（同 dm_crashfence.c cf_tile）：只改自己的单帧切片 Ztouch_comm/tileRC 的绘制尺寸，名字、文件名、帧数都对上才写 | frame tile (same as cf_tile): only our own slice Ztouch_comm/tileRC, written only when name, file and frame count match
    const char *name = w ? *(const char **)(w + 8) : NULL, *pre = "TouchCommTile"; size_t n = strlen(pre);
    if (!name || strncmp(name, pre, n) || name[n] < '0' || name[n] > '4' || name[n + 1] < '0' || name[n + 1] > '2' || name[n + 2]) return;
    if (want[2] < 1 || want[2] > 4000 || want[3] < 1 || want[3] > 800 || !dm_widget_is(b, w, DM_VT_IMAGE)) return;
    char expect[40]; snprintf(expect, sizeof expect, "Ztouch_comm/tile%c%c", name[n], name[n + 1]);
    const char *file = *(const char **)(w + 0x108); uint64_t len = *(uint64_t *)(w + 0x110);
    if (!file || len != strlen(expect) || memcmp(file, expect, len) || *(int32_t *)(w + 0x90) != 0) return;
    uint64_t ref = *(uint64_t *)(w + 0x88), sp = ref ? *(uint64_t *)ref : 0; if (!sp) return;
    uint32_t *atlas = (uint32_t *)(sp + 0xbc), *height = (uint32_t *)(sp + 0xc0), *frames = (uint32_t *)(sp + 0xcc), *width = (uint32_t *)(sp + 0xd0);
    if (*frames != 1 || *atlas != *width || !*width || *width > 4000 || !*height || *height > 800) return;
    *atlas = *width = (uint32_t)want[2]; *height = (uint32_t)want[3];
    int32_t *r = (int32_t *)(w + 0x70); if (memcmp(r, want, 4 * sizeof *r)) { r[0] = want[0]; r[1] = want[1]; r[2] = want[2]; r[3] = want[3]; }
}
// 提示框挂在哪个面板下：自定义页（VideoOptionsPanel，前端与局内都有；在前——局内从暂停菜单开选项时两者同时开着、选项页画在上面）、暂停菜单（PauseLayout / PauseLayoutGarden）、
// 前端主菜单（手柄：左侧按钮栏 MainMenuButtonRibbon，画在主菜单之上；触屏：键鼠版 MainMenuPanel）——哪个开着、带我们的控件，就用哪个
// Which panel holds the box: the Custom page (VideoOptionsPanel, front end and in a run; first because in a run both it and the pause menu are open and the options page draws above), the pause menu (PauseLayout / PauseLayoutGarden),
// the front-end main menu (controller: the button column MainMenuButtonRibbon, drawn above the main menu; touch: the keyboard-mouse MainMenuPanel) - whichever is open and carries our widgets
#define N_HOSTS 5
static const char *const k_hosts[N_HOSTS] = { "VideoOptionsPanel", "PauseLayout", "PauseLayoutGarden", "MainMenuButtonRibbon", "MainMenuPanel" };
static uint64_t co_host(uint64_t root, int *which) {
    for (int i = 0; i < N_HOSTS; i++) { uint64_t p = dm_ui_top(root, k_hosts[i]); if (p && *(uint8_t *)(p + 0x50) && co_find(p, "TouchCommTitle", 0)) { if (which) *which = i; return p; } }
    return 0;
}
// 面板原点与缩放（面板级字段，所有控件共有：横向锚点 +0x48、纵向锚点 +0x4c、矩形 +0x70.. 、缩放 +0x80，dm_cursor.c 同口径）：子控件不带锚点时，屏幕坐标 = 原点 + 子坐标 × 缩放。
// Mac 实测：暂停菜单面板矩形 (-1039, 243) 缩放 1.16、自定义页 (-1007, 390, 1730×1284) 缩放 1.16（主机档 _profileConsoleHD），都按屏宽的一半锚定，算出的框整屏居中；算出来的值进日志
// Panel origin and scale (panel-level fields shared by all widgets: horizontal anchor +0x48, vertical anchor +0x4c, rect +0x70.., scale +0x80, as in dm_cursor.c): for an unanchored child, screen = origin + child × scale.
// Mac: pause panel rect (-1039, 243) scale 1.16, Custom page (-1007, 390, 1730×1284) scale 1.16 (console profile), both anchored at half the screen width; the computed box was screen-centered; the values go to the log
static void co_origin(uint64_t p, int xr, int *ox, int *oy, float *s) {
    float ax = *(float *)(p + 0x48), ay = *(float *)(p + 0x4c), sc = *(float *)(p + 0x80); int32_t *r = (int32_t *)(p + 0x70);
    if (!(ax >= 0 && ax <= 1)) ax = 0; if (!(ay >= 0 && ay <= 1)) ay = 0; if (!(sc > 0.05f && sc < 10)) sc = 1;
    *ox = (int)(ax * xr) + r[0]; *oy = (int)(ay * 2160) + r[1]; *s = sc;
}
#define CO_DESC 6   // 正文行：社区框 0 群号或链接 / 1 提示或「已复制」/ 2 空（分隔线占这一行）/ 3 版本 / 4–5 鸣谢；纯「关于」框 0 版本 / 1–2 鸣谢，其余空 | body lines: community box 0 number or link / 1 hint or Copied / 2 empty (the divider takes this line) / 3 version / 4-5 credits; About-only box 0 version / 1-2 credits, the rest blank
static void co_hide_all(uint8_t *b, uint64_t p) {
    for (int k = 0; k < 15; k++) { char nm[24]; snprintf(nm, sizeof nm, "TouchCommTile%d%d", k / 3, k % 3); uint64_t t = co_find(p, nm, 0); if (t) { int32_t *r = (int32_t *)(t + 0x70); r[0] = r[1] = 99999; } }   // 边框片挪出屏幕（尺寸不动） | tiles off screen (size unchanged)
    uint64_t w = co_find(p, "TouchCommTitle", 0); if (w) { co_place(w, 99999, 99999); co_text(b, w, ""); }
    for (int k = 0; k < CO_DESC; k++) { char nm[24]; snprintf(nm, sizeof nm, "TouchCommDesc%d", k); w = co_find(p, nm, 0); if (w) { co_place(w, 99999, 99999); co_text(b, w, ""); } }
    static const char *const bt[] = { "TouchCommCopyQq", "TouchCommCopyLink", "TouchCommOk", "TouchCommDivider" };
    for (int k = 0; k < 4; k++) co_place(co_find(p, bt[k], 0), 99999, 99999);
}
// 社区段与「关于」段之间的分隔线（用户 2026-10-29：「关于」子标题换成分隔线）：原版设置页分节同一张图 PauseMenu\Divider，共享精灵不改尺寸——
// 只读它的图宽（精灵 +0xd0，同 co_tile 的偏移）按框中线居中；读不到就按 1500 估。核过是图片控件、名字对才摆
// Divider between the community and About sections (user, 2026-10-29: replaces the About sub-title): the original settings-section image PauseMenu\Divider, a shared sprite never resized --
// only its width is read (sprite +0xd0, same offset as co_tile) to centre it on the box; 1500 assumed if unreadable. Placed only after checking it is an image widget with our name
static void co_divider(uint8_t *b, uint64_t w, int cx, int y) {
    if (!w || !dm_widget_is(b, w, DM_VT_IMAGE)) return;
    uint64_t ref = *(uint64_t *)(w + 0x88), sp = ref ? *(uint64_t *)ref : 0; uint32_t wd = sp ? *(uint32_t *)(sp + 0xd0) : 0;
    co_place(w, cx - (int)(wd > 0 && wd < 4000 ? wd : 1500) / 2, y);
}
const char *dm_update_label(void);   // dm_update.c：显示版本「1.0.0+N · 哈希」（没经外壳初始化时为空） | display version (empty when the shell has not initialized it)
void dm_community_pump(void) {   // 取手柄处每帧调（游戏主线程，hle_hid.c） | called every frame at the controller poll (game main thread, hle_hid.c)
    uint8_t *b = co_base(); if (!b) return;
    uint64_t root = *(uint64_t *)(b + 0x2f05458); if (!root) return;
    int avail = dm_community_available();
    // 入口显隐：暂停菜单行、主菜单第二行、自定义页「关于」组的社区行——没有可显示的值就挪出屏幕（行在滚动区里，只挪 x）；主菜单「关于」行总在。自定义页同时写鸣谢块
    // entry show / hide: the pause-menu row, the main menu's second row, the Custom page's community row - without a value they move off screen (the row sits in a scroll area, so only x moves); the main menu's About row stays. The Custom page also gets its credits block written
    for (int i = 0; i < N_HOSTS; i++) { uint64_t p = dm_ui_top(root, k_hosts[i]); if (!p || !*(uint8_t *)(p + 0x50)) continue;
        if (i == 0) { uint64_t w = co_find(p, "TouchAboutCommunityRow", 0); if (w) { int32_t *r = (int32_t *)(w + 0x70); if (avail ? r[0] == 99999 : r[0] != 99999) co_place(w, avail ? 0 : 99999, r[1]); } credits_render(b, p); }
        else if (i < 3) { uint64_t w = co_find(p, "TouchCommunity", 0); if (w) { int32_t *r = (int32_t *)(w + 0x70); if (avail ? r[0] == 99999 : r[0] != 99999) co_place(w, avail ? 564 : 99999, r[1]); } }
        else { uint64_t w = co_find(p, "TouchCommunityFe", 0); if (w) { int32_t *r = (int32_t *)(w + 0x70); int home = i == 3 ? 164 : 20; if (avail ? r[0] == 99999 : r[0] != 99999) co_place(w, avail ? home : 99999, r[1]); } } }
    int st = atomic_load(&g_show); if (!st) return;
    int dm_padmode_current(void); uint64_t dm_padmode_page_keys(uint64_t); int pad = dm_padmode_current(), mode = atomic_load(&g_mode);
    static int wait; int which = 0; uint64_t p = co_host(root, &which);
    if (!p) { if (st == 1 && ++wait == 600) { wait = 0; atomic_store(&g_show, 0); LOG("【红】社区入口：开着的面板里没有提示框控件（原版布局？），只记日志不显示"); }
              else if (st == 2) { atomic_store(&g_show, 0); LOG("社区入口：所在面板已关，提示框作废"); }
              return; }
    wait = 0;
    uint64_t ti = co_find(p, "TouchCommTitle", 0), ok = co_find(p, "TouchCommOk", 0), cq = co_find(p, "TouchCommCopyQq", 0), cl = co_find(p, "TouchCommCopyLink", 0), de[CO_DESC], tl[15]; int all = ti && ok && cq && cl;
    for (int k = 0; k < 15; k++) { char nm[24]; snprintf(nm, sizeof nm, "TouchCommTile%d%d", k / 3, k % 3); tl[k] = co_find(p, nm, 0); all &= tl[k] != 0; }
    for (int k = 0; k < CO_DESC; k++) { char nm[24]; snprintf(nm, sizeof nm, "TouchCommDesc%d", k); de[k] = co_find(p, nm, 0); all &= de[k] != 0; }
    if (!all) { atomic_store(&g_show, 0); LOG("【红】社区入口：%s 里的提示框控件不全，不显示", k_hosts[which]); return; }
    char v[GH_MAX + 1]; int kind = mode == 1 ? dm_community_value(dm_lang_group(), v, sizeof v) : 0;
    if (mode == 1 && !kind) { co_hide_all(b, p); atomic_store(&g_show, 0); LOG("社区入口：没有可显示的值了，提示框收起"); return; }
    if (pad && st == 2) { uint64_t ev = dm_padmode_page_keys(DM_BTN_A | DM_BTN_B); if ((ev & DM_BTN_A) && kind) do_copy(2); if (ev & DM_BTN_B) atomic_store(&g_ack, 2); }   // 手柄：显示着时接管 A 复制（社区框）、B 收起 | controller: while shown, A copies (community box) and B dismisses
    { int ack = atomic_exchange(&g_ack, 0); if (ack) { co_hide_all(b, p); atomic_store(&g_show, 0); LOG("社区入口：收起（%s）", ack == 2 ? "手柄 B" : "点确定"); return; } }
    int vw = *(int32_t *)(b + 0x211da38), vh = *(int32_t *)(b + 0x211da3c), xr = vh > 0 ? (int)((long)vw * 2160 / vh) : 3840;
    int ox, oy; float s; co_origin(p, xr, &ox, &oy, &s);
    int H = mode == 1 ? 760 : 520;   // 社区框：标题 + 社区两行 + 「关于」段四行 + 按钮；纯「关于」框：标题 + 三行 + 确定 | community box: title + two community lines + the four-line About section + buttons; About-only box: title + three lines + OK
    int cx = (int)((xr / 2 - ox) / s), y0 = (int)((1080 - oy) / s) - H / 2;   // 整屏居中：框 1800×H（面板单位） | centered on the screen: box 1800×H (panel units)
    // 文字：版本（外壳给的显示版本，没有就空行）、鸣谢（代码缺省名单，最多两行，带标签）、社区值 + 提示 / 「已复制」
    // Text: version (the shell's display version, blank line if none), credits (default list in code, at most two lines, labelled), community value + hint / Copied
    char line[GH_MAX + 64], ver[96]; const char *label = dm_update_label(); int cp = atomic_load(&g_copied);
    if (label && *label) snprintf(ver, sizeof ver, dm_str(b, "dmAboutVersion"), label); else ver[0] = 0;
    static int grp = -1, ncr; static cr_line_t cr[2]; int g = dm_lang_group();
    if (g != grp) { int trunc; grp = g; ncr = credits_lines(k_special_default, (int)(sizeof k_special_default / sizeof *k_special_default), NULL, 0, dm_str(b, "dmCreditsSep"), dm_str(b, "dmCreditsUpstreamNote"), cr, 2, &trunc);
        LOG("社区入口：提示框鸣谢 %d 行（特别鸣谢 %d 名，鸣谢 0 名）%s", ncr, (int)(sizeof k_special_default / sizeof *k_special_default), trunc ? "，超过两行已在末尾加「…」" : ""); }
    char crt[2][224]; for (int k = 0; k < 2; k++) { if (k < ncr) snprintf(crt[k], sizeof crt[k], "%s%s%s", cr[k].kind == 1 ? dm_str(b, "dmCreditsSpecial") : cr[k].kind == 2 ? dm_str(b, "dmCreditsThanks") : "", cr[k].kind ? " " : "", cr[k].text); else crt[k][0] = 0; }
    static const int dy_c[CO_DESC] = { 130, 192, 272, 340, 388, 436 }, dy_a[CO_DESC] = { 130, 200, 248, 0, 0, 0 };   // 社区框 / 纯「关于」框各行的 y（§2.6：36 / 28 / 36 子标题 / 28 × 3；36 / 28 / 28） | line y per box
    const int *dy = mode == 1 ? dy_c : dy_a; int btn_y = mode == 1 ? 600 : 380;
    if (mode == 1) {
        snprintf(line, sizeof line, dm_str(b, kind == 1 ? "dmCommunityQq" : "dmCommunityLink"), v);
        co_text(b, ti, dm_str(b, "dmCommunityTitle")); co_text(b, de[0], line);
        co_text(b, de[1], cp > 0 ? dm_str(b, "dmCommunityCopied") : cp < 0 ? dm_str(b, "dmCommunityCopyFail") : dm_str(b, kind == 1 ? "dmCommunityQqHint" : "dmCommunityLinkHint"));
        co_text(b, de[2], ""); co_text(b, de[3], ver); co_text(b, de[4], crt[0]); co_text(b, de[5], crt[1]); }   // 第 2 行空：「关于」子标题换成分隔线 | line 2 empty: the About sub-title became the divider
    else { co_text(b, ti, dm_str(b, "dmCommunityAbout")); co_text(b, de[0], ver); co_text(b, de[1], crt[0]); co_text(b, de[2], crt[1]); for (int k = 3; k < CO_DESC; k++) co_text(b, de[k], ""); }
    { int gm[15][4], h = dm_notice_geometry(1800 - 128, H - 24, 1800, gm), top = (244 - h) / 2;   // 边框几何同崩溃围栏提示框（dm_crashfence.c） | frame geometry as the crash-fence box
      for (int k = 0; h && k < 15; k++) { int r[4] = { cx - 900 + gm[k][0], y0 + gm[k][1] - top, gm[k][2], gm[k][3] + (k / 3 < 4 ? 2 : 0) }; co_tile(b, tl[k], r); } }
    co_place(ti, cx - 850, y0 + 40);
    for (int k = 0; k < CO_DESC; k++) { if (dy[k]) co_place(de[k], cx - 850, y0 + dy[k]); else co_place(de[k], 99999, 99999); }
    { uint64_t dv = co_find(p, "TouchCommDivider", 0); if (mode == 1) co_divider(b, dv, cx, y0 + 296); else co_place(dv, 99999, 99999); }   // 原「关于」行（y 272，高 60）的中线 | the middle of the former About line
    if (mode == 1) { co_place(kind == 1 ? cq : cl, cx - 490, y0 + btn_y); co_place(kind == 1 ? cl : cq, 99999, 99999); co_place(ok, cx + 20, y0 + btn_y); }   // 两颗按钮各 470 宽，左复制、右确定 | two 470-wide buttons: Copy left, OK right
    else { co_place(cq, 99999, 99999); co_place(cl, 99999, 99999); co_place(ok, cx - 235, y0 + btn_y); }   // 纯「关于」框：只有「确定」，居中 | About-only box: OK only, centered
    if (st == 1) { atomic_store(&g_show, 2); int32_t *pr = (int32_t *)(p + 0x70);
        LOG("社区入口：已显示（%s，%s，%s，版本%s，鸣谢 %d 行；面板 %s 锚 %.2f/%.2f 矩形 %d,%d %dx%d 缩放 %.2f → 框中心 %d 顶 %d）", atomic_load(&g_from) == 1 ? "暂停菜单" : atomic_load(&g_from) == 2 ? "主菜单" : "自定义页", mode == 2 ? "关于框" : pad ? "手柄模式，A 复制 B 收起" : "触屏 / 鼠标点按钮", mode == 2 ? "版本 + 鸣谢" : kind == 1 ? "群号" : "链接",
            ver[0] ? "有" : "无", ncr, k_hosts[which], *(float *)(p + 0x48), *(float *)(p + 0x4c), pr[0], pr[1], pr[2], pr[3], s, cx, y0); }
}
