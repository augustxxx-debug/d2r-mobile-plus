// 社区入口，接口见 dm_community.h。
// 清单字段（版本文件里与 build / note / urgent 并列）：  "community": { "qq_group": "1129913678", "github": "https://github.com/<fork>/d2r-mobile-plus" }
//   qq_group：5–12 位数字；github：以 https://github.com/ 开头、≤ 160 个 URL 字符。两项都可省；一项不合规就当这一项没有并记红；两项都没有 = 当无字段。
//   解析在这里用 C 做（不依赖外壳的 JSON 库），外壳只把原文交过来；单测 t_community.c 覆盖有 / 无 / 坏 / 缓存回退。
// 缓存 community.txt（与 sw_custom.txt 同目录）：一行一个「键=值」，先写临时文件再换名；清单明确没有字段时删掉（旧值不留）。
// 画面（用户：「不要用原生的弹窗，用游戏内弹窗」）：边框 15 片 TouchCommTile（我们通知框同一套美术、自己一套切片 Ztouch_comm）+ 标题 + 三行正文（值 / 提示 / 「已复制」）+ 两颗原版前端按钮（复制群号 或 复制链接 二选一显示、确定），
//   布局 touch_layout.py _community，挂在暂停菜单面板（两份）和自定义页面板末尾，平时在屏外、字空着；这里每帧在游戏主线程写字、按面板矩形与缩放算出居中位置。
//   触屏点按钮；手柄接管 A = 复制、B = 收起；Mac 鼠标点按钮或键盘同手柄。复制走外壳登记的剪贴板回调，成功后第三行写「已复制」（游戏内文字，不用系统弹窗）。
// 日志固定前缀（分享诊断日志摘要取最后一行）：「社区入口：清单有字段 / 清单无字段 / 清单拉取失败」「社区入口：已显示」「社区入口：复制 群号 / 链接」。
// Community entry; the interface is in dm_community.h.
// Manifest field (next to build / note / urgent in the version file):  "community": { "qq_group": "1129913678", "github": "https://github.com/<fork>/d2r-mobile-plus" }
//   qq_group: 5-12 digits; github: starts with https://github.com/, at most 160 URL characters. Both are optional; an invalid one counts as absent and is logged red; both absent = no field.
//   Parsing is done here in C (no dependence on the shells' JSON libraries); the shells only hand over the raw text; the unit test t_community.c covers present / absent / malformed / cache fallback.
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
static _Atomic int g_from;   // 从哪里点开（只进日志）：1 暂停菜单 / 2 选项页 | which entry opened it (log only): 1 pause menu / 2 options page

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

// ---- 最小 JSON 读取：只认「"键" : 值」这一种形状；字符串里带转义（反斜杠）一律当不合规 | minimal JSON reading: only the shape "key" : value; a string with an escape (backslash) counts as invalid ----
static const char *ws(const char *p, const char *e) { while (p < e && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++; return p; }
static const char *str_end(const char *p, const char *e) {   // p 指向开头的引号；返回收尾引号的下一位（NULL = 没收尾） | p at the opening quote; returns one past the closing quote (NULL = unterminated)
    for (p++; p < e; p++) { if (*p == '\\') { p++; continue; } if (*p == '"') return p + 1; }
    return NULL;
}
static int json_sane(const char *p, const char *e) {   // 第一个非空白是 {、花括号配平、最后一个非空白是 } | first non-blank is {, braces balance, last non-blank is }
    p = ws(p, e); if (p >= e || *p != '{') return 0; int depth = 0; const char *last = NULL;
    while (p < e) { if (*p == '"') { p = str_end(p, e); if (!p) return 0; continue; } if (*p == '{') depth++; else if (*p == '}') { if (--depth < 0) return 0; last = p; } p++; }
    if (depth || !last) return 0; return ws(last + 1, e) == e;
}
static const char *find_key(const char *p, const char *e, const char *key) {   // 本层或更深：找到「"key" :」返回值的起点 | at this level or deeper: find "key" : and return where the value starts
    size_t kn = strlen(key);
    while (p < e) {
        if (*p != '"') { p++; continue; }
        const char *q = str_end(p, e); if (!q) return NULL;
        if ((size_t)(q - p) == kn + 2 && !memcmp(p + 1, key, kn)) { const char *v = ws(q, e); if (v < e && *v == ':') return ws(v + 1, e); }
        p = q;
    }
    return NULL;
}
static const char *obj_end(const char *p, const char *e) {   // p 指向 {；返回配对的 } | p at {; returns the matching }
    int depth = 0; while (p < e) { if (*p == '"') { p = str_end(p, e); if (!p) return NULL; continue; } if (*p == '{') depth++; else if (*p == '}' && --depth == 0) return p; p++; }
    return NULL;
}
static int read_str(const char *p, const char *e, char *out, size_t n) {   // 值必须是不带转义、长度 < n 的字符串 | the value must be a string without escapes and shorter than n
    if (p >= e || *p != '"') return 0; const char *q = str_end(p, e); if (!q) return 0;
    size_t len = (size_t)(q - p) - 2; if (len + 1 > n) return 0;
    for (size_t i = 0; i < len; i++) if (p[1 + i] == '\\' || (unsigned char)p[1 + i] < 0x20) return 0;
    memcpy(out, p + 1, len); out[len] = 0; return 1;
}

int dm_community_manifest(const char *json, size_t len) {
    char qq[QQ_MAX + 1] = "", gh[GH_MAX + 1] = ""; get_values(qq, gh);
    if (!json) { LOG("社区入口：清单拉取失败，沿用缓存（群号 %s，链接 %s）", qq[0] ? "有" : "无", gh[0] ? "有" : "无"); return -1; }
    if (len > MANIFEST_MAX) { LOG("社区入口：清单超过 %d 字节，沿用缓存", MANIFEST_MAX); return -1; }
    const char *e = json + len;
    if (!json_sane(json, e)) { LOG("社区入口：清单不是完整的 JSON 对象，沿用缓存（群号 %s，链接 %s）", qq[0] ? "有" : "无", gh[0] ? "有" : "无"); return -1; }
    const char *v = find_key(json, e, "community");
    if (!v) { set_values("", ""); cache_delete(); LOG("社区入口：清单无字段，入口不显示（缓存已清）"); return 0; }
    const char *oe = v < e && *v == '{' ? obj_end(v, e) : NULL;
    if (!oe) { set_values("", ""); cache_delete(); LOG("【红】社区入口：清单 community 字段不是对象，入口不显示"); return 0; }
    char nq[QQ_MAX + 1] = "", ng[GH_MAX + 1] = ""; int bad = 0;
    const char *q = find_key(v + 1, oe, "qq_group"); if (q && (!read_str(q, oe, nq, sizeof nq) || !qq_ok(nq))) { bad++; nq[0] = 0; }
    const char *g = find_key(v + 1, oe, "github"); if (g && (!read_str(g, oe, ng, sizeof ng) || !gh_ok(ng))) { bad++; ng[0] = 0; }
    if (!nq[0] && !ng[0]) { set_values("", ""); cache_delete(); LOG("【红】社区入口：清单 community 字段格式坏（%d 项不合规：群号须 5–12 位数字，链接须 https://github.com/ 开头、不超过 %d 字符），入口不显示", bad, GH_MAX); return 0; }
    set_values(nq, ng); int wr = cache_write(nq, ng);
    LOG("社区入口：清单有字段 群号=%s 链接=%s%s%s", nq[0] ? nq : "无", ng[0] ? ng : "无", bad ? "（另一项格式坏，已忽略）" : "", wr ? "" : "【红】缓存写不进设置目录");
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
void dm_community_open(int from) {
    if (!dm_community_available()) { LOG("【红】社区入口：点了入口（%s）但当前语言没有可显示的值", from == 1 ? "暂停菜单按钮" : from == 2 ? "选项页按钮" : "选项页手柄 A"); return; }
    atomic_store(&g_copied, 0); atomic_store(&g_from, from >= 2 ? 2 : 1); atomic_store(&g_show, 1); LOG("社区入口：点了入口（%s）", from == 1 ? "暂停菜单按钮" : from == 2 ? "选项页按钮" : "选项页手柄 A");
}
void dm_community_copy_init(int (*cb)(const char *)) { g_copy = cb; }
unsigned dm_community_copy_count(void) { return atomic_load(&g_copies); }
static void do_copy(int by) {   // by：1 点按钮 / 2 手柄 A | by: 1 button / 2 controller A
    char v[GH_MAX + 1]; int k = dm_community_value(dm_lang_group(), v, sizeof v);
    if (!k) { atomic_store(&g_copied, -1); LOG("【红】社区入口：复制失败（没有可复制的值）"); return; }
    int ok = g_copy ? g_copy(v) : 0;
    if (ok) { atomic_fetch_add(&g_copies, 1); atomic_store(&g_copied, 1); LOG("社区入口：复制 %s %s（%s）", k == 1 ? "群号" : "链接", v, by == 2 ? "手柄 A" : "点按钮"); }
    else { atomic_store(&g_copied, -1); LOG("【红】社区入口：复制失败（%s；%s）", g_copy ? "剪贴板回调返回失败" : "外壳没登记剪贴板回调", by == 2 ? "手柄 A" : "点按钮"); }
}
void dm_community_on_uimsg(const char *cat, const char *act, const char *arg) {   // 界面消息回调（diag.c 转来，游戏主线程） | UI message callback (forwarded from diag.c, game main thread)
    if (!cat || !act || !arg || strcmp(cat, "PanelManager") || strcmp(act, "ClosePanel")) return;
    if (!strcmp(arg, "TouchCommunity")) dm_community_open(1);
    else if (!strcmp(arg, "TouchCommunityOpt")) dm_community_open(2);
    else if (!strcmp(arg, "TouchCommCopy") && atomic_load(&g_show) == 2) do_copy(1);
    else if (!strcmp(arg, "TouchCommOk") && atomic_load(&g_show) == 2) atomic_store(&g_ack, 1);
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
// 提示框挂在哪个面板下：暂停菜单（PauseLayout / PauseLayoutGarden，局内）或自定义页（VideoOptionsPanel，前端与局内都有）——哪个开着、带我们的控件，就用哪个
// Which panel holds the box: the pause menu (PauseLayout / PauseLayoutGarden, in a run) or the Custom page (VideoOptionsPanel, front end and in a run) - whichever is open and carries our widgets
static const char *const k_hosts[3] = { "PauseLayout", "PauseLayoutGarden", "VideoOptionsPanel" };
static uint64_t co_host(uint64_t root, int *which) {
    for (int i = 0; i < 3; i++) { uint64_t p = dm_ui_top(root, k_hosts[i]); if (p && *(uint8_t *)(p + 0x50) && co_find(p, "TouchCommTitle", 0)) { if (which) *which = i; return p; } }
    return 0;
}
// 面板原点与缩放（面板级字段，所有控件共有：横向锚点 +0x48、纵向锚点 +0x4c、矩形 +0x70.. 、缩放 +0x80，dm_cursor.c 同口径）：子控件不带锚点时，屏幕坐标 = 原点 + 子坐标 × 缩放。
// 暂停菜单面板矩形 (-1039, 243) 缩放 1.16、自定义页 (-1007, 390, 1730×1284) 缩放 1.16（主机档 _profileConsoleHD），都按屏宽的一半锚定；算出来的值进日志，Mac 实测核过再信
// Panel origin and scale (panel-level fields shared by all widgets: horizontal anchor +0x48, vertical anchor +0x4c, rect +0x70.., scale +0x80, as in dm_cursor.c): for an unanchored child, screen = origin + child × scale.
// Pause panel rect (-1039, 243) scale 1.16, Custom page (-1007, 390, 1730×1284) scale 1.16 (console profile _profileConsoleHD), both anchored at half the screen width; the computed values go to the log and are trusted only after a Mac check
static void co_origin(uint64_t p, int xr, int *ox, int *oy, float *s) {
    float ax = *(float *)(p + 0x48), ay = *(float *)(p + 0x4c), sc = *(float *)(p + 0x80); int32_t *r = (int32_t *)(p + 0x70);
    if (!(ax >= 0 && ax <= 1)) ax = 0; if (!(ay >= 0 && ay <= 1)) ay = 0; if (!(sc > 0.05f && sc < 10)) sc = 1;
    *ox = (int)(ax * xr) + r[0]; *oy = (int)(ay * 2160) + r[1]; *s = sc;
}
static void co_hide_all(uint8_t *b, uint64_t p) {
    for (int k = 0; k < 15; k++) { char nm[24]; snprintf(nm, sizeof nm, "TouchCommTile%d%d", k / 3, k % 3); uint64_t t = co_find(p, nm, 0); if (t) { int32_t *r = (int32_t *)(t + 0x70); r[0] = r[1] = 99999; } }   // 边框片挪出屏幕（尺寸不动） | tiles off screen (size unchanged)
    static const char *const tx[] = { "TouchCommTitle", "TouchCommDesc0", "TouchCommDesc1", "TouchCommDesc2" };
    for (int k = 0; k < 4; k++) { uint64_t w = co_find(p, tx[k], 0); if (w) { co_place(w, 99999, 99999); co_text(b, w, ""); } }
    static const char *const bt[] = { "TouchCommCopyQq", "TouchCommCopyLink", "TouchCommOk" };
    for (int k = 0; k < 3; k++) co_place(co_find(p, bt[k], 0), 99999, 99999);
}
void dm_community_pump(void) {   // 取手柄处每帧调（游戏主线程，hle_hid.c） | called every frame at the controller poll (game main thread, hle_hid.c)
    uint8_t *b = co_base(); if (!b) return;
    uint64_t root = *(uint64_t *)(b + 0x2f05458); if (!root) return;
    int avail = dm_community_available();
    // 入口显隐：暂停菜单那一行、自定义页的「关于」组——没有可显示的值就挪出屏幕（行在滚动区里，只挪 x） | entry show / hide: the pause-menu row and the Custom page's About group - without a value they move off screen (the row sits in a scroll area, so only x moves)
    for (int i = 0; i < 3; i++) { uint64_t p = dm_ui_top(root, k_hosts[i]); if (!p || !*(uint8_t *)(p + 0x50)) continue;
        if (i < 2) { uint64_t w = co_find(p, "TouchCommunity", 0); if (w) { int32_t *r = (int32_t *)(w + 0x70); if (avail ? r[0] == 99999 : r[0] != 99999) co_place(w, avail ? 564 : 99999, r[1]); } }
        else { uint64_t w = co_find(p, "TouchAboutRow", 0); if (w) { int32_t *r = (int32_t *)(w + 0x70); if (avail ? r[0] == 99999 : r[0] != 99999) co_place(w, avail ? 0 : 99999, r[1]); } } }
    int st = atomic_load(&g_show); if (!st) return;
    int dm_padmode_current(void); uint64_t dm_padmode_page_keys(uint64_t); int pad = dm_padmode_current();
    static int wait; int which = 0; uint64_t p = co_host(root, &which);
    if (!p) { if (st == 1 && ++wait == 600) { wait = 0; atomic_store(&g_show, 0); LOG("【红】社区入口：开着的面板里没有提示框控件（原版布局？），只记日志不显示"); }
              else if (st == 2) { atomic_store(&g_show, 0); LOG("社区入口：所在面板已关，提示框作废"); }
              return; }
    wait = 0;
    uint64_t ti = co_find(p, "TouchCommTitle", 0), ok = co_find(p, "TouchCommOk", 0), cq = co_find(p, "TouchCommCopyQq", 0), cl = co_find(p, "TouchCommCopyLink", 0), de[3], tl[15]; int tiles = 1;
    for (int k = 0; k < 15; k++) { char nm[24]; snprintf(nm, sizeof nm, "TouchCommTile%d%d", k / 3, k % 3); tl[k] = co_find(p, nm, 0); tiles &= tl[k] != 0; }
    for (int k = 0; k < 3; k++) { char nm[24]; snprintf(nm, sizeof nm, "TouchCommDesc%d", k); de[k] = co_find(p, nm, 0); }
    if (!tiles || !ti || !de[0] || !de[1] || !de[2] || !ok || !cq || !cl) { atomic_store(&g_show, 0); LOG("【红】社区入口：%s 里的提示框控件不全，不显示", k_hosts[which]); return; }
    char v[GH_MAX + 1]; int kind = dm_community_value(dm_lang_group(), v, sizeof v);
    if (!kind) { co_hide_all(b, p); atomic_store(&g_show, 0); LOG("社区入口：没有可显示的值了，提示框收起"); return; }
    if (pad && st == 2) { uint64_t ev = dm_padmode_page_keys(DM_BTN_A | DM_BTN_B); if (ev & DM_BTN_A) do_copy(2); if (ev & DM_BTN_B) atomic_store(&g_ack, 1); }   // 手柄：显示着时接管 A 复制、B 收起 | controller: while shown, A copies and B dismisses
    if (atomic_exchange(&g_ack, 0)) { co_hide_all(b, p); atomic_store(&g_show, 0); LOG("社区入口：收起（%s）", pad ? "手柄 B" : "点确定"); return; }
    int vw = *(int32_t *)(b + 0x211da38), vh = *(int32_t *)(b + 0x211da3c), xr = vh > 0 ? (int)((long)vw * 2160 / vh) : 3840;
    int ox, oy; float s; co_origin(p, xr, &ox, &oy, &s);
    int cx = (int)((xr / 2 - ox) / s), y0 = (int)((1080 - oy) / s) - 310;   // 整屏居中：框 1800×620（面板单位） | centered on the screen: box 1800×620 (panel units)
    char line[GH_MAX + 64]; snprintf(line, sizeof line, dm_str(b, kind == 1 ? "dmCommunityQq" : "dmCommunityLink"), v);
    int cp = atomic_load(&g_copied);
    co_text(b, ti, dm_str(b, "dmCommunityTitle")); co_text(b, de[0], line); co_text(b, de[1], dm_str(b, kind == 1 ? "dmCommunityQqHint" : "dmCommunityLinkHint"));
    co_text(b, de[2], cp > 0 ? dm_str(b, "dmCommunityCopied") : cp < 0 ? dm_str(b, "dmCommunityCopyFail") : "");
    { int g[15][4], h = dm_notice_geometry(1800 - 128, 620 - 24, 1800, g), top = (244 - h) / 2;   // 边框几何同崩溃围栏提示框（dm_crashfence.c） | frame geometry as the crash-fence box
      for (int k = 0; h && k < 15; k++) { int r[4] = { cx - 900 + g[k][0], y0 + g[k][1] - top, g[k][2], g[k][3] + (k / 3 < 4 ? 2 : 0) }; co_tile(b, tl[k], r); } }
    co_place(ti, cx - 850, y0 + 40);
    for (int k = 0; k < 3; k++) co_place(de[k], cx - 850, y0 + 150 + 66 * k);
    co_place(kind == 1 ? cq : cl, cx - 490, y0 + 460); co_place(kind == 1 ? cl : cq, 99999, 99999); co_place(ok, cx + 20, y0 + 460);   // 两颗按钮各 470 宽，左复制、右确定 | two 470-wide buttons: Copy left, OK right
    if (st == 1) { atomic_store(&g_show, 2); int32_t *pr = (int32_t *)(p + 0x70);
        LOG("社区入口：已显示（%s，%s，%s；面板 %s 锚 %.2f/%.2f 矩形 %d,%d %dx%d 缩放 %.2f → 框中心 %d 顶 %d）", atomic_load(&g_from) == 2 ? "选项页" : "暂停菜单", pad ? "手柄模式，A 复制 B 收起" : "触屏 / 鼠标点按钮", kind == 1 ? "群号" : "链接",
            k_hosts[which], *(float *)(p + 0x48), *(float *)(p + 0x4c), pr[0], pr[1], pr[2], pr[3], s, cx, y0); }
}
