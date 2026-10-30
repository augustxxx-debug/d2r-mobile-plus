// 社区入口，接口见 dm_community.h。
// 清单字段（版本文件里与 build / note / urgent 并列）：  "community": { "qq_group": "1129913678", "github": "https://github.com/<fork>/d2r-mobile-plus" }
//   qq_group：5–12 位数字；github：以 https://github.com/ 开头、≤ 160 个 URL 字符；site 见下。三项都可省；一项不合规就当这一项没有并记红；三项都没有 = 当无字段。
//   site（项目官网，用户 2026-10-29；RC1.1 审计后定）：https:// + 主机名 + 可选 /路径、不带端口、≤ 60 字符（site_ok）；有它时渠道卡片多一张官网卡（中文排在群号后、外文排第一，见 dm_community_channels），链接一项（dm_community_value）取官网；没有官网退回 github、与 RC1 同值。旧客户端忽略它。
//   解析在这里用 C 做（严格的最小 JSON 解析器，十一审 Q11-01：整段必须是合法根对象，只认根对象直接子级 community 及其直接子级；语法错 / 重复 / 嵌套同名 / 转义都当格式错），外壳只把原文交过来；单测 t_community.c 覆盖有 / 无 / 坏 / 缓存回退与这些坏形状。
// 缓存 community.txt（与 sw_custom.txt 同目录）：一行一个「键=值」，先写临时文件再换名；清单明确没有字段时删掉（旧值不留）。
// 画面（用户：「不要用原生的弹窗，用游戏内弹窗」；RC1.1 重构，用户 2026-10-30）：边框 15 片 TouchCommTile + 标题 + 渠道卡片 0–2 行（标签 / 值 / 各自的小「复制」）+ 免费声明 + 分隔线 + 页脚两行（版本 + 制作；特别鸣谢）+ 一颗「确定」，
//   布局 touch_layout.py _community，挂在暂停菜单面板（两份）和自定义页面板末尾，平时在屏外、字空着；这里每帧在游戏主线程写字、按面板矩形与缩放算出居中位置。
//   触屏点按钮；手柄焦点链 复制₁ → 复制₂ → 确定（上下移、A 触发、B 收起）；Mac 鼠标点按钮或键盘同手柄。复制走外壳登记的剪贴板回调，按钮字变「已复制 / 复制失败」2 秒（游戏内文字，不用系统弹窗）。
// 日志固定前缀（分享诊断日志摘要取最后一行）：「社区入口：清单有字段 / 清单无字段 / 清单拉取失败」「社区入口：已显示」「社区入口：复制 群号 / 官网 / GitHub」。
// Community entry; the interface is in dm_community.h.
// Manifest field (next to build / note / urgent in the version file):  "community": { "qq_group": "1129913678", "github": "https://github.com/<fork>/d2r-mobile-plus" }
//   qq_group: 5-12 digits; github: starts with https://github.com/, at most 160 URL characters; site: see below. All three are optional; an invalid one counts as absent and is logged red; all absent = no field.
//   site (project website, user 2026-10-29; settled after the RC1.1 review): https:// + host + optional /path, no port, at most 60 characters (site_ok); when present it adds a website card (after the QQ card in Chinese, first in other languages, see dm_community_channels) and the link (dm_community_value) is the website; otherwise github, the same value as RC1. Older clients ignore it.
//   Parsing is done here in C (a strict minimal JSON parser, Q11-01: the whole text must be a valid root object; only the root object's direct child community and its direct children count; syntax errors / duplicates / nested same-name keys / escapes are format errors); the shells only hand over the raw text; t_community.c covers present / absent / malformed / cache fallback and these bad shapes.
// Cache community.txt (next to sw_custom.txt): one "key=value" per line, written to a temporary file then renamed; deleted when the manifest explicitly has no field (no stale value is kept).
// Picture (user: "do not use a native popup, use an in-game one"; RC1.1 redesign, user 2026-10-30): 15 frame tiles + title + 0-2 channel cards (label / value / own small Copy) + free notice + divider + two footer lines (version + maker; special thanks) + one OK,
//   layout touch_layout.py _community, hung at the end of the pause menu panel (both) and the Custom page panel, off screen with empty text; here, every frame on the game main thread, the text is written and the centered position computed from the panel's rect and scale.
//   Touch taps the buttons; controller focus chain Copy1 → Copy2 → OK (up / down, A activates, B dismisses); the Mac clicks or uses the controller keys. Copy goes through the shell's clipboard callback; the button reads Copied / Copy failed for 2 s (in-game text).
// Fixed log prefixes (the share-log summary takes the last line): "社区入口：清单有字段 / 清单无字段 / 清单拉取失败", "社区入口：已显示", "社区入口：复制 群号 / 官网 / GitHub".
#include "dm_community.h"
#include "dm_custom.h"
#include "dm_crashfence.h"
#include "dm_i18n.h"
#include "dm_input.h"
#include "dm_widget.h"
#include "dm_notice_geometry.h"
#include "dm_cursor.h"
#include "dm_stick_nav.h"
#include "hle.h"
#include <ctype.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define QQ_MAX 12
#define GH_MAX 160
#define MANIFEST_MAX 4096   // 外壳也按 4096 字节封顶（dm_update.m） | the shells cap at 4096 bytes too (dm_update.m)
static pthread_mutex_t g_cm_mu = PTHREAD_MUTEX_INITIALIZER;   // 护 g_qq / g_gh（清单从网络线程来，泵在游戏主线程读） | guards g_qq / g_gh (the manifest arrives on a network thread; the pump reads on the game main thread)
static char g_qq[QQ_MAX + 1], g_gh[GH_MAX + 1], g_site[GH_MAX + 1];   // g_site：项目官网（清单 community.site，用户 2026-10-29） | g_site: the project website (manifest community.site)
static _Atomic int g_loaded_cm;
static _Atomic int g_show, g_ack, g_copied;   // g_show：0 不显示 / 1 要显示 / 2 显示着；g_copied：0 / 1 已复制 / -1 复制失败 | g_show: 0 none / 1 pending / 2 shown; g_copied: 0 / 1 copied / -1 failed
static _Atomic int g_copied_row; static _Atomic uint64_t g_copied_at;   // 哪一行的「复制」刚按过、何时（按钮字变「已复制 / 复制失败」2 秒） | which row's Copy was just pressed and when (the button reads Copied / Copy failed for 2 s)
// 弹窗吞输入（RC1.1 阻塞，用户 Duo 真机 80c0882：触屏点弹窗下半部穿透到底下暂停菜单、「确定」点不到；手柄左摇杆还在动左边的菜单）：弹窗开着（g_show ≠ 0）时
//   触屏：dm_cursor.c 不再给游戏发鼠标消息，抬起那一下由 dm_community_ui_tap 用控件自己的命中判断只认两颗「复制」与「确定」，其余吞掉；拖动 / 滚动也不起；
//   手柄：hle_hid.c 取手柄处最后经 dm_community_input_gate，键与两根摇杆全清零，方向键 / 左右摇杆上下变成焦点移动、A / B 留给弹窗。各自计数，收起时记一行（交给底层的应为 0）
// Modal input (RC1.1 blocker, user on the Duo 80c0882: taps on the lower half of the box went through to the pause menu, OK could not be hit; the left stick still moved the menu on the left): while the box is up
//   touch: dm_cursor.c sends the game no mouse messages; on lift dm_community_ui_tap hit-tests only the two Copy buttons and OK with the widgets' own hit test, everything else is swallowed; no drag / scroll either;
//   controller: hle_hid.c runs dm_community_input_gate last, which zeroes every key and both sticks, turns D-pad / stick up-down into focus moves and keeps A / B for the box. Counted; one log line on dismiss (passed to the game should be 0)
static _Atomic uint64_t g_md_ev;   // 闸门攒给弹窗的按下沿（A / B / 上 / 下） | press edges collected for the box
static _Atomic unsigned g_sw_touch, g_sw_key, g_sw_stick, g_leak;
int dm_community_modal(void) { return atomic_load(&g_show) != 0; }
// 弹窗开着时宿主对触屏命中查询的答案（RC1.1 复核 P1-2；dm_cursor.c 的异步 / 同步两种查询都取这里）：0「面板内控件」——两端手势层对 0 走界面鼠标点击，
// 鼠标泵把抬起交给 dm_community_ui_tap；不能答 6「不点」（9943fd2：手势层不点也不发键，iOS 局内面板态弹窗里的按钮全点不动）。没开弹窗返回 -1（照常判类）
// The host's answer to a touch hit query while the box is up (RC1.1 recheck P1-2; both of dm_cursor.c's async / sync queries take it from here): 0 "control inside a panel" -- both ends'
// gesture layer turn 0 into a UI mouse click and the mouse pump hands the lift to dm_community_ui_tap; it must not be 6 "no click" (9943fd2: the gesture layer neither clicked nor sent a key,
// so on iOS in panel mode the box's buttons could not be tapped). -1 when no box is up (classify as usual)
int dm_community_hit_answer(void) { return atomic_load(&g_show) ? 0 : -1; }
// 关框后的压住窗口（RC1.1 终版审计 P2-1）：手柄 A / B 关框时，泵在取手柄处开头就把 g_show 置 0，同一拍末尾的闸门以为弹窗已关、把还按着的那一下交给了底层菜单。
// 现在弹窗开着时记下按着的键与摇杆；关掉后，这些键全松开、两根摇杆都回到 1/4 以内之前，整只手柄都不交给游戏（压住的拍数在清零之前计数），窗口结束记一行。
// 漏口计数（dm_community_input_leak）也覆盖这个窗口：原先只在 g_show 时计、又在闸门清零之后查，恒为 0、测不到这类漏口
// Post-close hold window (RC1.1 final audit P2-1): a controller A / B that closes the box is handled by the pump at the start of the controller read, which clears g_show, so the gate at the
// end of the same read thought the box was gone and handed the still-held press to the menu underneath. Now the gate remembers what was held while the box was up; after it closes, the whole
// controller stays withheld until those keys are released and both sticks are back inside a quarter (withheld samples are counted before zeroing), with one log line when the window ends.
// The leak counter (dm_community_input_leak) covers that window too: it used to count only while g_show was set and after the gate had zeroed everything, so it was always 0
static _Atomic int g_after; static _Atomic unsigned g_after_n;
static int stick_off(int32_t v) { return v > -8000 && v < 8000; }
void dm_community_input_gate(uint64_t *bt, int32_t *lx, int32_t *ly, int32_t *rx, int32_t *ry) {
    static uint64_t prev, pdir, held; static int stick; static dm_stick_nav nav;
    if (!atomic_load(&g_show)) {
        if (atomic_load(&g_after) && bt && lx && ly && rx && ry) {
            int centred = stick_off(*lx) && stick_off(*ly) && stick_off(*rx) && stick_off(*ry);
            held &= *bt; if (centred) stick = 0;
            if (held || stick) { atomic_fetch_add(&g_after_n, 1); *bt = 0; *lx = *ly = *rx = *ry = 0; }   // 先计数再清零 | counted, then zeroed
            else { atomic_store(&g_after, 0); LOG("社区入口：关框后压住按着的键 / 摇杆 %u 拍（松开前不交给底层），交给底层 %u", atomic_exchange(&g_after_n, 0), atomic_exchange(&g_leak, 0)); } }
        prev = bt ? *bt : 0; pdir = 0; dm_stick_nav_reset(&nav); return; }
    if (!bt || !lx || !ly || !rx || !ry) return;
    held = *bt; stick = !(stick_off(*lx) && stick_off(*ly) && stick_off(*rx) && stick_off(*ry)); atomic_store(&g_after, 1); atomic_store(&g_after_n, 0);   // 关掉时按着的东西 | what is held when it closes
    uint64_t down = *bt & ~prev; prev = *bt;
    int32_t vy = (*ly < 0 ? -*ly : *ly) >= (*ry < 0 ? -*ry : *ry) ? *ly : *ry;   // 两根摇杆取推得多的那根的上下 | up / down of whichever stick is pushed further
    uint64_t dir = dm_stick_nav_dirs(&nav, *lx, vy) & (DM_BTN_UP | DM_BTN_DOWN);   // 公共折算（dm_stick_nav.h：回滞、主轴）；左右在弹窗里无处可去 | shared conversion; left / right have nowhere to go in the box
    uint64_t ev = down & (DM_BTN_A | DM_BTN_B | DM_BTN_UP | DM_BTN_DOWN);
    if (dir & ~pdir) { ev |= dir; atomic_fetch_add(&g_sw_stick, 1); }
    pdir = dir;
    if (down) atomic_fetch_add(&g_sw_key, 1);
    if (ev) atomic_fetch_or(&g_md_ev, ev);
    *bt = 0; *lx = *ly = *rx = *ry = 0;
}
void dm_community_input_leak(int leaked) { if (leaked && (atomic_load(&g_show) || atomic_load(&g_after))) atomic_fetch_add(&g_leak, 1); }   // 闸门之后（弹窗开着或关框后压住窗口里）还有非零输入交给游戏 | a non-zero input reached the game after the gate while the box was up or in the post-close window
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
// 官网（用户 2026-10-29：「关于弹窗里网址改成新的官网」）：地址只从清单 / 缓存来，代码里不写死。RC1.1 增量审计 Q11.1-01 / 02 收紧：
//   https:// + 主机名（只许 [A-Za-z0-9.-]、至少一个点、不以点或横线开头结尾）+ 可选 /路径；主机名后面只能是串尾或 /，不收端口（B024：接口约定第 13 节没有端口，整串不许出现 :）；拒绝 @（userinfo，浏览器会打开 @ 后面的主机）、? 与 #；
//   总长 ≤ SITE_MAX = 60：值行 36 号按 §2.6 估宽（拉丁 0.7 × 字号 ≈ 25.2 单位 / 字）在框内可用 1560 里最多约 61 字。超了当这一项没有（退回 GitHub）
// The website: only from the manifest / cache, never hard-coded. Tightened in the RC1.1 review (Q11.1-01 / 02): https:// + host ([A-Za-z0-9.-], at least one dot, not starting or ending with . or -)
//   + optional /path; the host is followed only by end-of-string or / and no port is accepted (B024: the interface contract §13 has no port, no : anywhere); rejects @ (userinfo -- a browser opens the host after @), ? and #; at most SITE_MAX = 60 characters (the 36-pt value line fits about 61 in the box's 1560 by the §2.6 estimate). Longer = absent (GitHub fallback)
#define SITE_MAX 60
static int site_ok(const char *s) {
    static const char pre[] = "https://"; size_t n = strlen(s);
    if (n <= sizeof pre - 1 || n > SITE_MAX || strncmp(s, pre, sizeof pre - 1)) return 0;
    const char *h = s + sizeof pre - 1, *e = h; int dot = 0;
    while (*e && *e != '/') { unsigned char c = (unsigned char)*e; if (!(isalnum(c) || c == '.' || c == '-')) return 0; dot |= c == '.'; e++; }
    if (e == h || !dot || h[0] == '.' || h[0] == '-' || e[-1] == '.' || e[-1] == '-') return 0;
    for (; *e; e++) { unsigned char c = (unsigned char)*e; if (!(isalnum(c) || strchr("-._~/!$&'()*+,;=%", c))) return 0; }   // 路径：不含 ? # @ :（B024） | path: no ? # @ : (B024)
    return 1;
}
static void set_values(const char *qq, const char *gh, const char *site) { pthread_mutex_lock(&g_cm_mu); snprintf(g_qq, sizeof g_qq, "%s", qq ? qq : ""); snprintf(g_gh, sizeof g_gh, "%s", gh ? gh : ""); snprintf(g_site, sizeof g_site, "%s", site ? site : ""); pthread_mutex_unlock(&g_cm_mu); }
static void get_values(char *qq, char *gh, char *site) { pthread_mutex_lock(&g_cm_mu); memcpy(qq, g_qq, sizeof g_qq); memcpy(gh, g_gh, sizeof g_gh); if (site) memcpy(site, g_site, sizeof g_site); pthread_mutex_unlock(&g_cm_mu); }
static int cache_write(const char *qq, const char *gh, const char *site) {
    char p[1100], t[1120]; if (!cache_path(p, sizeof p)) return 0;
    snprintf(t, sizeof t, "%s.tmp", p); FILE *f = fopen(t, "w"); if (!f) return 0;
    int ok = 1; if (qq[0]) ok &= fprintf(f, "qq_group=%s\n", qq) > 0; if (gh[0]) ok &= fprintf(f, "github=%s\n", gh) > 0; if (site[0]) ok &= fprintf(f, "site=%s\n", site) > 0;
    ok = fclose(f) == 0 && ok; return ok && rename(t, p) == 0;
}
static void cache_delete(void) { char p[1100]; if (cache_path(p, sizeof p)) unlink(p); }

// ---- 严格的最小 JSON 解析（十一审 Q11-01）：整段输入必须是一个合法的 JSON 根对象（对象 / 数组 / 字符串 / 数字 / true / false / null 全按语法验），
// 只认根对象的直接子级 "community"（必须是对象）和它的直接子级 "qq_group" / "github" / "site"（必须是不带转义的字符串）；嵌套更深的同名键一律忽略；
// 重复的 community / qq_group / github / site 当格式错。语法错、尾逗号、非对象根、根后有多余内容 = 格式错。深度上限 16 层，不递归到栈爆
// Strict minimal JSON parsing (Q11-01): the whole input must be one valid JSON root object (object / array / string / number / true / false / null all validated);
// only the root object's direct child "community" (must be an object) and its direct children "qq_group" / "github" / "site" (must be strings without escapes) are honored; deeper same-name keys are ignored;
// a duplicate community / qq_group / github / site is a format error. Syntax error, trailing comma, non-object root, trailing content = format error. Depth capped at 16
#define CR_NAME_MAX 32   // 鸣谢名字上限（字节） | credit name limit (bytes)
#define CR_MAX 24        // 两份名单合计上限 | both lists together
typedef struct { const char *p, *e; char qq[QQ_MAX + 1], gh[GH_MAX + 1], site[GH_MAX + 1]; int n_comm, n_qq, n_gh, n_site, bad_type, too_long;
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
        if (!js_string(j, key, sizeof key, &kesc, &kover)) return 0; if (kesc || kover) key[0] = 0;   // 带转义 / 超长的键不会是我们认的任何一个（community / qq_group / github / site / credits / special / thanks） | an escaped / long key is never one of ours (community / qq_group / github / site / credits / special / thanks)
        js_ws(j); if (j->p >= j->e || *j->p != ':') return 0; j->p++; js_ws(j);
        if (ctx == 1 && !strcmp(key, "community")) { j->n_comm++;
            if (j->p < j->e && *j->p == '{') { if (!js_object(j, depth + 1, 2)) return 0; } else { j->bad_type = 1; if (!js_value(j, depth + 1, 0)) return 0; } }
        else if (ctx == 2 && (!strcmp(key, "qq_group") || !strcmp(key, "github") || !strcmp(key, "site"))) { int gh = key[0] == 'g', st = key[0] == 's'; if (gh) j->n_gh++; else if (st) j->n_site++; else j->n_qq++;
            if (j->p < j->e && *j->p == '"') { int esc = 0, over = 0; char *out = gh ? j->gh : st ? j->site : j->qq; size_t n = gh ? sizeof j->gh : st ? sizeof j->site : sizeof j->qq;
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
static void drop_values(void) { set_values("", "", ""); cache_delete(); }   // 格式错 / 无字段：清内存与缓存 | malformed / absent: clear memory and cache

int dm_community_manifest(const char *json, size_t len) {
    char qq[QQ_MAX + 1] = "", gh[GH_MAX + 1] = "", site[GH_MAX + 1] = ""; get_values(qq, gh, site);
    if (!json) { LOG("社区入口：清单拉取失败，沿用缓存（群号 %s，链接 %s，官网 %s）", qq[0] ? "有" : "无", gh[0] ? "有" : "无", site[0] ? "有" : "无"); return -1; }
    if (len > MANIFEST_MAX) { drop_values(); LOG("【红】社区入口：清单格式错（超过 %d 字节），入口不显示（缓存已清）", MANIFEST_MAX); return 0; }
    jp_t j; size_t at; if (!js_parse(&j, json, len, &at)) { drop_values(); LOG("【红】社区入口：清单格式错（JSON 语法：第 %zu 字节附近），入口不显示（缓存已清）", at); return 0; }
    if (j.n_comm == 0) { drop_values(); LOG("社区入口：清单无字段，入口不显示（缓存已清）"); return 0; }
    if (j.n_comm > 1 || j.n_qq > 1 || j.n_gh > 1 || j.n_site > 1) { drop_values(); LOG("【红】社区入口：清单格式错（community / qq_group / github / site 重复），入口不显示（缓存已清）"); return 0; }
    if (j.bad_type && !j.qq[0] && !j.gh[0] && !j.site[0]) { drop_values(); LOG("【红】社区入口：清单格式错（community 不是对象，或值不是字符串 / 带转义），入口不显示（缓存已清）"); return 0; }
    int bad = 0; if (j.qq[0] && !qq_ok(j.qq)) { bad++; j.qq[0] = 0; } if (j.gh[0] && !gh_ok(j.gh)) { bad++; j.gh[0] = 0; } if (j.site[0] && !site_ok(j.site)) { bad++; j.site[0] = 0; }
    bad += j.bad_type + j.too_long;
    if (!j.qq[0] && !j.gh[0] && !j.site[0]) { drop_values(); LOG("【红】社区入口：清单格式错（%d 项不合规：群号须 5–12 位数字，链接须 https://github.com/ 开头、官网须 https:// 开头，都不超过 %d 字符、不带转义），入口不显示（缓存已清）", bad, GH_MAX); return 0; }
    set_values(j.qq, j.gh, j.site); int wr = cache_write(j.qq, j.gh, j.site);
    if (j.n_credits > 1 || j.bad_credits) LOG("【红】社区入口：清单 credits 字段格式坏（%s），忽略（RC1 只计数不渲染）", j.n_credits > 1 ? "重复" : "须是 { special: [名字…], thanks: [名字…] }，名字 ≤ 32 字节、不带转义、合计 ≤ 24");
    LOG("社区入口：清单有字段 群号=%s 链接=%s 官网=%s%s%s credits=特别 %d / 鸣谢 %d", j.qq[0] ? j.qq : "无", j.gh[0] ? j.gh : "无", j.site[0] ? j.site : "无", bad ? "（另一项格式坏，已忽略）" : "", wr ? "" : "【红】缓存写不进设置目录", j.bad_credits || j.n_credits > 1 ? 0 : j.n_special, j.bad_credits || j.n_credits > 1 ? 0 : j.n_thanks);
    return 1;
}
static void cache_read(void) {
    char p[1100]; if (!cache_path(p, sizeof p)) { LOG("社区入口：没有设置目录，不读缓存"); return; }
    FILE *f = fopen(p, "r"); if (!f) { LOG("社区入口：没有缓存，入口不显示（等清单）"); return; }
    char ln[256], qq[QQ_MAX + 1] = "", gh[GH_MAX + 1] = "", site[GH_MAX + 1] = "";
    while (fgets(ln, sizeof ln, f)) { size_t n = strcspn(ln, "\r\n"); ln[n] = 0;
        if (!strncmp(ln, "qq_group=", 9) && qq_ok(ln + 9)) snprintf(qq, sizeof qq, "%s", ln + 9);
        else if (!strncmp(ln, "github=", 7) && gh_ok(ln + 7)) snprintf(gh, sizeof gh, "%s", ln + 7);
        else if (!strncmp(ln, "site=", 5) && site_ok(ln + 5)) snprintf(site, sizeof site, "%s", ln + 5); }
    fclose(f); set_values(qq, gh, site);
    LOG("社区入口：缓存 群号=%s 链接=%s 官网=%s", qq[0] ? qq : "无", gh[0] ? gh : "无", site[0] ? site : "无");
}
void dm_community_load(void) {
    if (atomic_exchange(&g_loaded_cm, 1)) return;
    const char *tf = getenv("DM_TEST_COMMUNITY");   // 测试开关（只在测试局读，产品局连环境变量都不看）：从文件读一份清单当这次拉到的 | test switch (read only in test runs): read a manifest from a file as if fetched now
    if (tf && *tf && dm_crashfence_test_env()) {
        FILE *f = fopen(tf, "r"); char buf[MANIFEST_MAX + 1]; size_t n = f ? fread(buf, 1, sizeof buf - 1, f) : 0; if (f) fclose(f); buf[n] = 0;
        LOG("社区入口：测试开关 DM_TEST_COMMUNITY 读清单（%zu 字节）", n); dm_community_manifest(f ? buf : NULL, n); return; }
    cache_read();
}
// 链接一项：有官网用官网（用户 2026-10-29），没有退回 GitHub（旧清单 / 旧缓存） | the link: the website when there is one (user, 2026-10-29), else GitHub (older manifests / caches)
int dm_community_value(int lang_group, char *out, size_t n) {
    char qq[QQ_MAX + 1], gh[GH_MAX + 1], site[GH_MAX + 1]; get_values(qq, gh, site); int k = 0; const char *v = "";
    if (lang_group == DM_LG_ZH && qq[0]) { k = 1; v = qq; } else if (site[0]) { k = 2; v = site; } else if (gh[0]) { k = 2; v = gh; }
    if (out && n) snprintf(out, n, "%s", v);
    return k;
}
// 渠道（RC1.1 弹窗重构，用户 2026-10-30「同意 很好」；补审 Q11.1-06：中文也要看得到官网）：框里最多两行渠道卡片——
//   中文按 [QQ 群, 官网, GitHub] 取前两个有值的，其他语言按 [官网, GitHub]（不显示群号）。显示值：群号原样、官网去掉 https:// 与末尾 /、GitHub 只显示仓库名；复制的永远是完整内容
// Channels (RC1.1 box redesign, user 2026-10-30; follow-up review Q11.1-06: Chinese must see the website too): at most two channel cards --
//   Chinese takes the first two present of [QQ group, website, GitHub], other languages [website, GitHub] (no QQ). Shown value: the number as is, the website without https:// and the
//   trailing /, GitHub as the repository name only; copy always takes the full value
int dm_community_channels(int lang_group, int out[2]) {
    char qq[QQ_MAX + 1], gh[GH_MAX + 1], site[GH_MAX + 1]; get_values(qq, gh, site); int n = 0;
    const int order_zh[3] = { DM_CH_QQ, DM_CH_SITE, DM_CH_GITHUB }, order_xx[2] = { DM_CH_SITE, DM_CH_GITHUB };
    const int *o = lang_group == DM_LG_ZH ? order_zh : order_xx; int no = lang_group == DM_LG_ZH ? 3 : 2;
    for (int i = 0; i < no && n < 2; i++) if ((o[i] == DM_CH_QQ && qq[0]) || (o[i] == DM_CH_SITE && site[0]) || (o[i] == DM_CH_GITHUB && gh[0])) out[n++] = o[i];
    return n;
}
int dm_community_channel_value(int ch, char *full, size_t nf, char *shown, size_t ns) {   // 完整值（复制用）与显示值；没有返回 0 | full value (for copy) and shown value; 0 when absent
    char qq[QQ_MAX + 1], gh[GH_MAX + 1], site[GH_MAX + 1]; get_values(qq, gh, site);
    const char *v = ch == DM_CH_QQ ? qq : ch == DM_CH_SITE ? site : ch == DM_CH_GITHUB ? gh : ""; if (!v[0]) { if (full && nf) full[0] = 0; if (shown && ns) shown[0] = 0; return 0; }
    if (full && nf) snprintf(full, nf, "%s", v);
    if (shown && ns) { const char *a = v; size_t L = strlen(v);
        if (ch != DM_CH_QQ) { a = v + strlen("https://"); L = strlen(a); if (L > 1 && a[L - 1] == '/') L--; }
        if (ch == DM_CH_GITHUB) {   // 仓库名 = github.com/<owner>/<repo>/… 的 <repo> 那一段（RC1.1 终版审计 P2-4：原先取最后一段，…/d2r-mobile-plus/discussions 显示成 discussions）；没有第三段才退回最后一段
                                    // repository name = the <repo> segment of github.com/<owner>/<repo>/... (final audit P2-4: the last segment used to be taken, so .../d2r-mobile-plus/discussions showed "discussions"); falls back to the last segment only without a third one
            const char *e = a + L, *s1 = memchr(a, '/', L), *s2 = s1 ? memchr(s1 + 1, '/', (size_t)(e - s1 - 1)) : NULL;
            if (s2 && s2 + 1 < e) { const char *r = s2 + 1, *re = memchr(r, '/', (size_t)(e - r)); a = r; L = (size_t)((re ? re : e) - r); }
            else { const char *q = e; while (q > a && q[-1] != '/') q--; if (q < e) { L = (size_t)(e - q); a = q; } } }
        snprintf(shown, ns, "%.*s", (int)L, a); }
    return 1;
}
// 估宽（§2.6：拉丁 0.7、其他 1.0 个字号）超过 maxw 就截断加「…」；只截显示，复制仍是完整值。RC1.1 补审 Q11.1-08：带目标容量 cap，只走合法 UTF-8 前缀
// （首字节定长度、续字节必须 10xxxxxx、不越过 cap − 1；遇坏字节就在那里截断），「…」只在放得下时加，保证 NUL 结尾；返回是否改了串
// Estimated width (§2.6) over maxw: cut and add "…"; display only, copy keeps the full value. RC1.1 follow-up review Q11.1-08: takes the buffer capacity cap and walks only a valid UTF-8 prefix
// (lead byte gives the length, continuation bytes must be 10xxxxxx, never past cap − 1; a bad byte truncates there); "…" only when it fits; always NUL-terminated; returns whether the string changed
int dm_community_fit(char *s, size_t cap, int pt, int maxw) {
    if (!s || !cap) return 0;
    size_t i = 0, cut = 0, lim = cap - 1; int w = 0, over = 0, bad = 0;
    while (i < lim && s[i]) { unsigned char c = (unsigned char)s[i]; size_t step = c < 0x80 ? 1 : (c & 0xe0) == 0xc0 ? 2 : (c & 0xf0) == 0xe0 ? 3 : (c & 0xf8) == 0xf0 ? 4 : 0;
        if (!step || (step > 1 && c < 0xc2) || c > 0xf4 || i + step > lim) { bad = 1; break; }   // 孤立续字节 / 非法首字节 / 过长编码 / 多字节越过容量 | stray continuation / invalid lead / overlong / past capacity
        for (size_t k = 1; k < step; k++) if (((unsigned char)s[i + k] & 0xc0) != 0x80) { bad = 1; break; }
        if (bad) break;
        int cw = c < 0x80 ? pt * 7 / 10 : pt;
        if (w + cw > maxw) { over = 1; break; }
        if (w + cw + pt <= maxw) cut = i + step;   // 「…」按一个字号宽算：记下最后一个还放得下「…」的字边界 | "…" counts as one point size: the last boundary that still fits it
        w += cw; i += step; }
    if (!over && !bad && i == lim && s[i]) over = 1;   // 串比容量长（没在 cap − 1 前结束）：同超宽，加「…」 | string longer than the capacity: treated like too wide, gets "…"
    if (!over) { int changed = bad || s[i]; s[i] = 0; return changed; }   // 放得下：只按合法前缀收尾 | fits: end at the valid prefix
    while (cut > 0 && cut + 4 > cap) { cut--; while (cut > 0 && ((unsigned char)s[cut] & 0xc0) == 0x80) cut--; }   // 「…」3 字节 + NUL 放不下就往前退到字边界 | step back to a character boundary until "…" + NUL fit
    if (cut + 4 <= cap) memcpy(s + cut, "\xe2\x80\xa6", 4); else s[cut] = 0;   // 「…」+ NUL | "…" + NUL
    return 1;
}
int dm_community_available(void) { return dm_community_value(dm_lang_group(), NULL, 0) != 0; }
void dm_community_open(int from) {   // from：1 暂停菜单 / 2 主菜单 / 3 自定义页按钮 / 4 自定义页手柄 A | from: 1 pause menu / 2 main menu / 3 Custom page button / 4 Custom page controller A
    if (!dm_community_available()) { LOG("【红】社区入口：点了「加入社区讨论」（%s）但当前语言没有可显示的值", from == 1 ? "暂停菜单" : from == 2 ? "主菜单" : "自定义页"); return; }
    atomic_store(&g_sw_touch, 0); atomic_store(&g_sw_key, 0); atomic_store(&g_sw_stick, 0); atomic_store(&g_leak, 0); atomic_store(&g_md_ev, 0);
    atomic_store(&g_copied, 0); atomic_store(&g_copied_at, 0); atomic_store(&g_from, from >= 3 ? 3 : from); atomic_store(&g_mode, 1); atomic_store(&g_show, 1); LOG("社区入口：点了「加入社区讨论」（%s%s）", from == 1 ? "暂停菜单" : from == 2 ? "主菜单" : "自定义页", from == 4 ? "手柄 A" : "");
}
void dm_community_open_about(void) {   // 主菜单「关于」行：只有版本 + 鸣谢的框，没有社区值也开 | the main menu's About row: the version + credits box, opens without a community value too
    atomic_store(&g_copied, 0); atomic_store(&g_copied_at, 0); atomic_store(&g_from, 2); atomic_store(&g_mode, 2); atomic_store(&g_show, 1); LOG("社区入口：点了「关于」（主菜单）");
}
void dm_community_copy_init(int (*cb)(const char *)) { g_copy = cb; }
unsigned dm_community_copy_count(void) { return atomic_load(&g_copies); }
int dm_community_shown(void) { return atomic_load(&g_show) == 2; }
static uint64_t co_now(void) { return clock_gettime_nsec_np(CLOCK_MONOTONIC); }
static void do_copy(int by, int row) {   // by：1 点按钮 / 2 手柄 A；row：第几行渠道 | by: 1 button / 2 controller A; row: channel row
    int ch[2], n = dm_community_channels(dm_lang_group(), ch); char v[GH_MAX + 1] = "";
    atomic_store(&g_copied_row, row); atomic_store(&g_copied_at, co_now());
    if (row < 0 || row >= n || !dm_community_channel_value(ch[row], v, sizeof v, NULL, 0)) { atomic_store(&g_copied, -1); LOG("【红】社区入口：复制失败（第 %d 行没有可复制的值）", row + 1); return; }
    int ok = g_copy ? g_copy(v) : 0;
    if (ok) { atomic_fetch_add(&g_copies, 1); atomic_store(&g_copied, 1); LOG("社区入口：复制 %s %s（%s）", ch[row] == DM_CH_QQ ? "群号" : ch[row] == DM_CH_SITE ? "官网" : "GitHub", v, by == 2 ? "手柄 A" : "点按钮"); }
    else { atomic_store(&g_copied, -1); LOG("【红】社区入口：复制失败（%s；%s）", g_copy ? "剪贴板回调返回失败" : "外壳没登记剪贴板回调", by == 2 ? "手柄 A" : "点按钮"); }
}
void dm_community_on_uimsg(const char *cat, const char *act, const char *arg) {   // 界面消息回调（diag.c 转来，游戏主线程） | UI message callback (forwarded from diag.c, game main thread)
    if (!cat || !act || !arg || strcmp(cat, "PanelManager") || strcmp(act, "ClosePanel")) return;
    if (!strcmp(arg, "TouchCommunity")) dm_community_open(1);          // 暂停菜单行 | pause-menu row
    else if (!strcmp(arg, "TouchCommunityFe")) dm_community_open(2);    // 主菜单行 | main-menu row
    else if (!strcmp(arg, "TouchCommunityOpt")) dm_community_open(3);   // 自定义页「查看」 | Custom page View
    else if (!strcmp(arg, "TouchAbout")) dm_community_open_about();      // 主菜单「关于」 | main-menu About
    else if (!strncmp(arg, "TouchCommCopy", 13) && (!arg[13] || ((arg[13] == '0' || arg[13] == '1') && !arg[14])) && atomic_load(&g_show) == 2) do_copy(1, arg[13] ? arg[13] - '0' : 0);   // 各行的小「复制」（不带行号 = 第一行） | each row's Copy (no index = row 1)
    else if (!strcmp(arg, "TouchCommOk") && atomic_load(&g_show) == 2) atomic_store(&g_ack, 1);   // 1 点按钮 / 2 手柄 B（只进日志） | 1 button / 2 controller B (log only)
}

// ---- 鸣谢（提示框页脚；RC1.1 B008 起自定义页不再另有一块，用户 2026-10-28 定拼行规则）：名字放代码缺省常量（RC2 由清单 credits 覆盖），宿主按语言拼行：每行 ≤ 4 名且估算宽 ≤ 44 单位（原名单框 36 号 × 0.7 ≈ 25 单位 / 字，框 1110），
// 「特别鸣谢」名单末尾接括号说明（六套文案），「鸣谢」为空整行不显示；封顶 6 行，超出末行加「…」并记一条日志；续行标签留空；中文用「、」、其他语言用「, 」分隔
// Credits (the box footer; from RC1.1 B008 the Custom page no longer has its own block; line rules from user 2026-10-28): names are a default constant in code (RC2: the manifest credits field overrides); the host assembles lines per language: ≤ 4 names per line and estimated width within the name box (36 × 0.7 ≈ 25 units per char, box 1110 → 44 units),
// the special list ends with a parenthetical note (six texts), an empty thanks list shows no line; capped at 6 lines with "…" on the last and one log line; continuation lines have no label; "、" in Chinese, ", " elsewhere
// 署名（用户 2026-10-29）：鸣谢第一行「制作：Augustus」——用户公开署名，只画在游戏画面的提示框页脚，不写日志、不进分享摘要；排在特别鸣谢前 | maker credit: the user's public byline, drawn only in the credits area, never logged; before Special thanks
static const char *const k_maker_default[] = { "Augustus" };
static const char *const k_special_default[] = { "jhaizhou-ops" };   // 上游 d2r-mobile 原作者的公开 GitHub 账号名（README 引用的 https://github.com/jhaizhou-ops/d2r-mobile）；不写真名、不写别的信息 | the public GitHub handle of the upstream d2r-mobile author; no real name, nothing else
#define CR_LINES 6
#define CR_PER_LINE 4
#define CR_LINE_UNITS 44
typedef struct { int kind; char text[192]; } cr_line_t;   // kind：1 特别鸣谢首行 / 2 鸣谢首行 / 3 制作首行 / 0 续行 | kind: 1 first special line / 2 first thanks line / 3 first maker line / 0 continuation
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
// 三份名单（制作 kind 3 → 特别鸣谢 kind 1 → 鸣谢 kind 2）→ 行；同一套拼行规则；*truncated = 超过 max 行（末行已加「…」） | three lists (maker → special → thanks) → lines; same rules; *truncated = over max (the last line got "…")
static int credits_all(const char *const *mk, int nmk, const char *const *sp, int nsp, const char *const *th, int nth, const char *sep, const char *note, cr_line_t *out, int max, int *truncated) {
    int o = cr_list(mk, nmk, 3, sep, NULL, out, 0, max); o = cr_list(sp, nsp, 1, sep, note, out, o, max); o = cr_list(th, nth, 2, sep, NULL, out, o, max); *truncated = o > max;
    if (o > max) { o = max; size_t L = strlen(out[o - 1].text); if (L + 4 < sizeof out[o - 1].text) strcpy(out[o - 1].text + L, "…"); }
    return o;
}
// 两份名单 → 行；*truncated = 超过 max 行（末行已加「…」） | both lists → lines; *truncated = more than max lines (the last line got "…")
static int credits_lines(const char *const *sp, int nsp, const char *const *th, int nth, const char *sep, const char *note, cr_line_t *out, int max, int *truncated) {
    int o = cr_list(sp, nsp, 1, sep, note, out, 0, max); o = cr_list(th, nth, 2, sep, NULL, out, o, max); *truncated = o > max;
    if (o > max) { o = max; size_t L = strlen(out[o - 1].text); if (L + 4 < sizeof out[o - 1].text) strcpy(out[o - 1].text + L, "…"); }
    return o;
}
// ---- 画面 | picture ----
static uint8_t *co_base(void) { static uint8_t *b; if (!b) for (int i = 0; i < g_nmod; i++) if (!strcmp(g_mod[i].name, "main")) b = (uint8_t *)g_mod[i].base; return b; }
static int co_has(uint64_t w, uint64_t target, int dep) {   // 按指针找：target 在不在 w 的子树里（深度 14、每层至多 256 个子控件，同 dm_cursor.c ui_has） | by pointer: is target in w's subtree
    if (!w || dep > 14) return 0; if (w == target) return 1;
    uint64_t ch = *(uint64_t *)(w + 0x58), n = *(uint64_t *)(w + 0x60); if (!ch || n > 256) return 0;
    for (uint64_t i = 0; i < n; i++) if (co_has(*(uint64_t *)(ch + 8 * i), target, dep + 1)) return 1;
    return 0;
}
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
// 社区 / 关于弹窗（RC1.1 重构，用户 2026-10-30「同意 很好」，设计说明 dm-work/run/xpline/design/）：自上而下 标题 48 → 渠道卡片 0–2 行（标签 28 / 值 36 / 各自的小「复制」）
// → 免费声明 28 → 分隔线 → 页脚两行 28（版本 + 制作；特别鸣谢）→ 一颗「确定」。纵向（框内面板单位，自框顶算）按渠道行数 n 排：卡片高 100、行距 120，各段间距照 §2.6 层级；
// 末行文字下沿到按钮顶 CO_GAP，框高 = 按钮顶 + 按钮高 115 + 下边距 45（n = 2 / 1 / 0 → 802 / 682 / 562）。纯「关于」框（主菜单「关于」行，现无入口）= 同一结构、0 行渠道，原先 Q11.1-04 的字号错位随之消失
// Community / About box (RC1.1 redesign): title 48 → 0-2 channel cards (label 28 / value 36 / own small Copy) → free notice 28 → divider → two footer lines 28 (version + maker; special thanks) → one OK.
// Vertical layout by channel count n: card 100 high, 120 pitch; last text bottom to button top CO_GAP; box height = button top + 115 + 45 (802 / 682 / 562). The About-only box (no entry now) is the same structure with 0 channel rows, which also removes the Q11.1-04 size mismatch
#define CO_GAP 60
#define CO_ROWS 2
#define CO_CARD_W 1480
#define CO_BTN_W 293   // 原版前端小按钮 366×92 × 0.8（0.7 时俄文「Скопировано」贴满按钮） | the original small front-end button 366×92 × 0.8 (at 0.7 the Russian "Copied" filled it edge to edge)
#define CO_BTN_H 74
#define CO_LABEL_W 260
#define CO_VALUE_X (-420)
#define CO_VALUE_W (CO_CARD_W / 2 - 30 - CO_BTN_W - 30 - CO_VALUE_X)   // 值列：起点 −420，到小按钮左边留 30 | value column: from −420 to 30 left of the small button
typedef struct { int n, h, btn_y, div_y, free_y, foot_y[2], card_y[CO_ROWS]; } co_geom_t;
static co_geom_t co_geom(int n) {
    co_geom_t g = { 0 }; if (n < 0) n = 0; if (n > CO_ROWS) n = CO_ROWS; g.n = n;
    for (int k = 0; k < n; k++) g.card_y[k] = 130 + 120 * k;
    g.free_y = 140 + 120 * n; g.div_y = g.free_y + 80; g.foot_y[0] = g.div_y + 30; g.foot_y[1] = g.foot_y[0] + 48;
    g.btn_y = g.foot_y[1] + 30 + 28 / 2 + CO_GAP; g.h = g.btn_y + 115 + 45;
    return g;
}
static const char *const k_co_names[] = { "TouchCommTitle", "TouchCommLabel0", "TouchCommLabel1", "TouchCommValue0", "TouchCommValue1", "TouchCommCopy0", "TouchCommCopy1",
    "TouchCommCopyText0", "TouchCommCopyText1", "TouchCommCard0T", "TouchCommCard0B", "TouchCommCard0L", "TouchCommCard0R", "TouchCommCard1T", "TouchCommCard1B", "TouchCommCard1L", "TouchCommCard1R",
    "TouchCommFree", "TouchCommDivider", "TouchCommFoot0", "TouchCommFoot0R", "TouchCommFoot1", "TouchCommOk", "TouchCommFocusT", "TouchCommFocusB", "TouchCommFocusL", "TouchCommFocusR" };
#define CO_NNAMES (int)(sizeof k_co_names / sizeof *k_co_names)
static void co_hide_all(uint8_t *b, uint64_t p) {
    for (int k = 0; k < 15; k++) { char nm[24]; snprintf(nm, sizeof nm, "TouchCommTile%d%d", k / 3, k % 3); uint64_t t = co_find(p, nm, 0); if (t) { int32_t *r = (int32_t *)(t + 0x70); r[0] = r[1] = 99999; } }   // 边框片挪出屏幕（尺寸不动） | tiles off screen (size unchanged)
    for (int k = 0; k < CO_NNAMES; k++) { uint64_t w = co_find(p, k_co_names[k], 0); if (!w) continue; int32_t *r = (int32_t *)(w + 0x70); if (r[0] != 99999) { r[0] = r[1] = 99999; } co_text(b, w, ""); }
}
// 线条片（卡片边框、手柄焦点框）：我们自己的单色单帧切片 Ztouch_comm/<file>，同 co_tile 只改精灵的绘制尺寸（同名切片的控件尺寸相同才共用：横线一组、竖线一组）
// Line slices (card border, controller focus frame): our own one-colour single-frame slices Ztouch_comm/<file>; like co_tile only the sprite's draw size changes (widgets sharing a slice have the same size)
static void co_line(uint8_t *b, uint64_t w, const char *file, int x, int y, int wd, int ht) {
    const char *name = w ? *(const char **)(w + 8) : NULL; if (!name || strncmp(name, "TouchComm", 9) || !dm_widget_is(b, w, DM_VT_IMAGE) || wd < 1 || ht < 1 || wd > 4000 || ht > 800) return;
    char expect[40]; snprintf(expect, sizeof expect, "Ztouch_comm/%s", file);
    const char *f = *(const char **)(w + 0x108); uint64_t len = *(uint64_t *)(w + 0x110);
    if (!f || len != strlen(expect) || memcmp(f, expect, len) || *(int32_t *)(w + 0x90) != 0) return;
    uint64_t ref = *(uint64_t *)(w + 0x88), sp = ref ? *(uint64_t *)ref : 0; if (!sp) return;
    uint32_t *atlas = (uint32_t *)(sp + 0xbc), *height = (uint32_t *)(sp + 0xc0), *frames = (uint32_t *)(sp + 0xcc), *width = (uint32_t *)(sp + 0xd0);
    if (*frames != 1 || *atlas != *width || !*width || *width > 4000 || !*height || *height > 800) return;
    *atlas = *width = (uint32_t)wd; *height = (uint32_t)ht;
    int32_t *r = (int32_t *)(w + 0x70); if (r[0] != x || r[1] != y || r[2] != wd || r[3] != ht) { r[0] = x; r[1] = y; r[2] = wd; r[3] = ht; }
}
static void co_rect_lines(uint8_t *b, uint64_t p, const char *pre, const char *fh, const char *fv, int x, int y, int wd, int ht, int t) {   // 四条线围一个矩形 | four lines around a rectangle
    char nm[32]; snprintf(nm, sizeof nm, "%sT", pre); co_line(b, co_find(p, nm, 0), fh, x, y, wd, t);
    snprintf(nm, sizeof nm, "%sB", pre); co_line(b, co_find(p, nm, 0), fh, x, y + ht - t, wd, t);
    snprintf(nm, sizeof nm, "%sL", pre); co_line(b, co_find(p, nm, 0), fv, x, y, t, ht);
    snprintf(nm, sizeof nm, "%sR", pre); co_line(b, co_find(p, nm, 0), fv, x + wd - t, y, t, ht);
}
static void co_rect_hide(uint64_t p, const char *pre) { static const char sfx[4] = { 'T', 'B', 'L', 'R' }; for (int k = 0; k < 4; k++) { char nm[32]; snprintf(nm, sizeof nm, "%s%c", pre, sfx[k]); uint64_t w = co_find(p, nm, 0); if (w) { int32_t *r = (int32_t *)(w + 0x70); r[0] = r[1] = 99999; } } }
// 社区段与「关于」段之间的分隔线（用户 2026-10-29：「关于」子标题换成分隔线）：原版设置页分节同一张图 PauseMenu\Divider，共享精灵不改尺寸——
// 只读它的图宽（精灵 +0xd0，同 co_tile 的偏移）按框中线居中；读不到就按 1500 估。核过是图片控件、名字对才摆
// Divider between the community and About sections (user, 2026-10-29: replaces the About sub-title): the original settings-section image PauseMenu\Divider, a shared sprite never resized --
// only its width is read (sprite +0xd0, same offset as co_tile) to centre it on the box; 1500 assumed if unreadable. Placed only after checking it is an image widget with our name
static void co_divider(uint8_t *b, uint64_t w, int cx, int y) {
    if (!w || !dm_widget_is(b, w, DM_VT_IMAGE)) return;
    uint64_t ref = *(uint64_t *)(w + 0x88), sp = ref ? *(uint64_t *)ref : 0; uint32_t wd = sp ? *(uint32_t *)(sp + 0xd0) : 0;
    co_place(w, cx - (int)(wd > 0 && wd < 4000 ? wd : 1500) / 2, y);
}
// 入口显隐：没有可显示的值就挪出屏幕（行在滚动区里，只挪 x），有值时放回**挪走前的 x**。RC1.1：先前放回写死的 x——触屏首页「加入社区讨论」布局里居中（x −235），
// 却被放回旧版并排时的 20，新安装首启（清单还没到、先藏后显）时往右歪约 255 单位（用户 Duo 真机 80c0882）。还没挪过（home = 99999）就不动
// Entry show / hide: without a value the entry moves off screen (x only, it may sit in a scroll area); with one it returns to **its x before the move**. RC1.1: it used to return to a hard-coded x --
// the touch front page's Join button is centred in the layout (x −235) but went back to 20 from the old side-by-side layout, about 255 units right, on a fresh install's first run (hidden until the manifest arrived)
static void co_entry_show(uint64_t w, int avail, int32_t *home) {
    if (!w) return; int32_t *r = (int32_t *)(w + 0x70);
    if (!avail && r[0] != 99999) { *home = r[0]; co_place(w, 99999, r[1]); }
    else if (avail && r[0] == 99999 && *home != 99999) co_place(w, *home, r[1]);
}
const char *dm_update_label(void);   // dm_update.c：显示版本「1.0.0+N · 哈希」（没经外壳初始化时为空） | display version (empty when the shell has not initialized it)
// 触屏点按的命中区（RC1.1 真机 B058，用户真手指在 17 Pro 上点「复制 / 确定」全不响应）：
//   ① 框挂在主菜单面板（MainMenuPanel，矩形 0×0）下时，原版控件自己的命中判断（虚表 +0x48）一个都不中——画出来照样在，但命中被父面板的范围裁掉；
//      自定义页的框比设置面板宽，靠右的「复制」落在面板外也不中，居中的「确定」中（09:30 那局在暂停菜单测、点的是控件正中，所以测得过）；
//   ② 真手指的落点普遍偏在小钮上方（日志：比第一张卡片上沿还高 3–34 单位）。
// 所以先照旧用控件自己的命中判断，没中再按画出来的几何判：每一行渠道占框的整宽、行与行之间不留缝，第一行往上多放 70；「确定」四周放宽 60 / 50。
// 几何由泵每帧按实际摆放记下（面板单位），这里把游戏窗口像素换回面板单位再比（Mac 宿主 2622×1206 截图核过：算出的钮心与画面一致）
// Hit areas for touch taps (RC1.1 on-device B058, the user's real finger on a 17 Pro got no response from Copy / OK):
//   1) with the box hosted by the main menu panel (MainMenuPanel, a 0x0 rect) none of the widgets' own hit tests (vtable +0x48) hit -- the box is still drawn, but hits are clipped to the
//      parent panel; on the Custom page the box is wider than the settings panel, so the right-hand Copy buttons fall outside it while the centred OK still hits (the 09:30 run tested the
//      pause menu and tapped widget centres, so it passed);
//   2) real fingers land above the small button (log: 3-34 units above the first card's top edge).
// So the widgets' own hit test still goes first; on a miss the drawn geometry decides: each channel row spans the box's width with no gap between rows, the first row reaches 70 further up,
// OK is widened by 60 / 50. The pump records the geometry it actually placed (panel units) every frame; the tap converts game-window pixels back to panel units (checked against a 2622x1206
// Mac host screenshot: the computed button centres match the picture)
typedef struct { int valid, vh, ox, oy, n, bx0, bx1, card_y[CO_ROWS], ok_x, ok_y, ok_w, ok_h; float s; } co_tapgeo_t;
static co_tapgeo_t g_tapgeo;
static int co_tap_zone(const co_tapgeo_t *g, int x, int y) {   // 返回 0 / 1 = 第几行「复制」，2 = 「确定」，-1 = 都不是 | 0 / 1 = that row's Copy, 2 = OK, -1 = none
    if (!g || !g->valid || g->vh <= 0 || !(g->s > 0.05f)) return -1;
    float u = (float)x * 2160 / g->vh, v = (float)y * 2160 / g->vh, lx = (u - g->ox) / g->s, ly = (v - g->oy) / g->s;
    if (lx >= g->ok_x - 60 && lx <= g->ok_x + g->ok_w + 60 && ly >= g->ok_y - 50 && ly <= g->ok_y + g->ok_h + 50) return 2;
    if (lx < g->bx0 || lx > g->bx1) return -1;
    for (int k = 0; k < g->n && k < CO_ROWS; k++) if (ly >= g->card_y[k] - (k ? 10 : 70) && ly <= g->card_y[k] + 110) return k;
    return -1;
}
// 触屏点在弹窗上（dm_cursor.c 在弹窗开着时不发鼠标消息，抬起那一下转到这里）：用控件自己的命中判断（虚表 +0x48，同 dm_cursor.c 删除角色弹窗的「是」）只认两颗「复制」与「确定」；
// x / y = 游戏窗口像素（与 dm_cursor.c 发鼠标消息同一坐标）。其余位置一律吞掉。游戏主线程
// A tap while the box is up (dm_cursor.c sends no mouse messages then and hands the lift here): the widgets' own hit test (vtable +0x48, as the delete-character Yes in dm_cursor.c) for the two Copy buttons and OK only;
// x / y = game window pixels (the coordinates dm_cursor.c sends mouse messages in). Anything else is swallowed. Game main thread
void dm_community_ui_tap(uint64_t root, int x, int y) {
    atomic_fetch_add(&g_sw_touch, 1);
    int which = 0; uint64_t p = root ? co_host(root, &which) : 0; if (!p || atomic_load(&g_show) != 2) return;
    uint64_t xy = (uint64_t)(uint32_t)x | (uint64_t)(uint32_t)y << 32;
    static const char *const nm[3] = { "TouchCommCopy0", "TouchCommCopy1", "TouchCommOk" };
    for (int k = 0; k < 3; k++) { uint64_t w = co_find(p, nm[k], 0); if (!w || ((int32_t *)(w + 0x70))[0] == 99999) continue;
        if (((int (*)(uint64_t, uint64_t))*(uint64_t *)(*(uint64_t *)w + 0x48))(w, xy) & 1) { LOG("社区入口：弹窗开着时点按 (%d,%d) → %s", x, y, k == 2 ? "确定" : k ? "第 2 行复制" : "第 1 行复制"); if (k < 2) do_copy(1, k); else atomic_store(&g_ack, 1); return; } }
    { int z = co_tap_zone(&g_tapgeo, x, y);   // 放宽的命中区（B058） | the widened hit areas (B058)
      if (z >= 0) { LOG("社区入口：弹窗开着时点按 (%d,%d) → %s（按卡片 / 按钮周围的放宽区）", x, y, z == 2 ? "确定" : z ? "第 2 行复制" : "第 1 行复制"); if (z < 2) do_copy(1, z); else atomic_store(&g_ack, 1); return; } }
    LOG("社区入口：弹窗开着时点按 (%d,%d) → 没点在按钮上，吞掉", x, y);
}
void dm_community_pump(void) {   // 取手柄处每帧调（游戏主线程，hle_hid.c） | called every frame at the controller poll (game main thread, hle_hid.c)
    uint8_t *b = co_base(); if (!b) return;
    uint64_t root = *(uint64_t *)(b + 0x2f05458); if (!root) return;
    int avail = dm_community_available();
    // 入口显隐：暂停菜单行、主菜单第二行、自定义页「关于」组——没有可显示的值就挪出屏幕（组在滚动区里，只挪 x）；主菜单「关于」行总在。
    // 自定义页挪整组（RC1.1 B008：组里删了鸣谢子块，只剩社区行，没值时只挪社区行会留下一个空的「关于」标题）
    // entry show / hide: the pause-menu row, the main menu's second row, the Custom page's About group - without a value they move off screen (the group sits in a scroll area, so only x moves); the main menu's About row stays.
    // On the Custom page the whole group moves (RC1.1 B008: with the credits sub-block gone only the community row is left, and moving just that row would leave an empty About title)
    static int32_t home[N_HOSTS] = { 99999, 99999, 99999, 99999, 99999 };   // 各入口挪走前的 x（还没挪过 = 99999） | each entry's x before it was moved away (99999 = never moved)
    for (int i = 0; i < N_HOSTS; i++) { uint64_t p = dm_ui_top(root, k_hosts[i]); if (!p || !*(uint8_t *)(p + 0x50)) continue;
        // 弹窗开着时把暂停菜单 / 主菜单上点开它的那一行挪开（它的悬停 / 焦点光圈露在弹窗外，像还能点；关掉后回到原位、焦点还在这一行）；自定义页那行在滚动区里，冻结不挪
        // while the box is up, the pause / main menu row that opened it moves away (its hover / focus glow showed outside the box as if still active; it comes back with focus on close); the Custom page row is frozen
        co_entry_show(co_find(p, i == 0 ? "TouchAboutRow" : i < 3 ? "TouchCommunity" : "TouchCommunityFe", 0), i == 0 ? avail : avail && !atomic_load(&g_show), &home[i]);
    }
    // 底下菜单的焦点金框（RC1.1 冒烟 PARENTHL，用户点名阻塞）：手柄模式开框时点开它的那一行被挪开，原版就把焦点（连同金框）挪到「进入游戏」等别的行，框外还亮着一个能按的样子
    // （同一下 A 漏下去时直接进了游戏，审计 P2-1）。开着时把焦点管理器的焦点控件（[界面根+0xd0]+0x190，B015 也这样摆焦点）清空、每帧保持，金框不画；
    // 开框那一刻的焦点控件记下，关掉后它还在界面树里就交还焦点（行已在上面挪回原位），玩家回到点开它的那一行
    // The focus frame of the menu underneath (RC1.1 smoke PARENTHL, a blocker): in controller mode the row that opened the box moves away, so the game moved the focus (and its gold frame) to
    // "Play" or another row, lit up outside the box as if usable (and the leaked A started the game, audit P2-1). While the box is up the focus manager's focused widget ([UI root+0xd0]+0x190,
    // as B015 sets focus) is cleared every frame, so no frame is drawn; the widget focused when the box opened is remembered and gets the focus back after it closes if still in the UI tree
    // (the row has been moved back above), returning the player to the row that opened it
    int dm_padmode_current(void); uint64_t dm_padmode_page_keys(uint64_t); int pad = dm_padmode_current(), mode = atomic_load(&g_mode);
    { static uint64_t saved; static char saved_nm[64]; static int held; uint64_t fm = *(uint64_t *)(root + 0xd0);
      if (atomic_load(&g_show) && fm && pad) {
          uint64_t F = *(uint64_t *)(fm + 0x190);
          if (!held) { held = 1; saved = F; const char *nm = F ? *(const char **)(F + 8) : NULL; snprintf(saved_nm, sizeof saved_nm, "%s", nm ? nm : ""); if (F) LOG("社区入口：开框时藏起底下菜单的焦点（原在 %s）", saved_nm); }
          if (F) *(uint64_t *)(fm + 0x190) = 0; }
      else if (held && !atomic_load(&g_show)) { held = 0;
          // 按指针确认原控件还在界面树里、名字也没变（复核 P2-5：原先按名字从根找第一个同名控件、深度 6 层——TouchCommunity / TouchCommunityFe 手柄版与触屏版布局都有，两份同在树里时找不回）
          // confirm by pointer that the original widget is still in the UI tree with the same name (recheck P2-5: the first same-named widget within 6 levels of the root used to be taken --
          // TouchCommunity / TouchCommunityFe exist in both the controller and the touch layouts, so with both in the tree it was not found)
          const char *nm = saved && co_has(root, saved, 0) ? *(const char **)(saved + 8) : NULL;
          if (fm && nm && !strcmp(nm, saved_nm)) { *(uint64_t *)(fm + 0x190) = saved; LOG("社区入口：关框后焦点交还 %s", saved_nm); }
          else if (saved) LOG("社区入口：关框后原焦点控件 %s 已不在界面树里，不交还", saved_nm);
          saved = 0; } }
    int st = atomic_load(&g_show); if (!st) return;
    static int wait; int which = 0; uint64_t p = co_host(root, &which);
    if (!p) { if (st == 1 && ++wait == 600) { wait = 0; atomic_store(&g_show, 0); LOG("【红】社区入口：开着的面板里没有提示框控件（原版布局？），只记日志不显示"); }
              else if (st == 2) { atomic_store(&g_show, 0); LOG("社区入口：所在面板已关，提示框作废"); }
              return; }
    wait = 0;
    int all = 1; for (int k = 0; k < CO_NNAMES; k++) all &= co_find(p, k_co_names[k], 0) != 0;
    uint64_t tl[15]; for (int k = 0; k < 15; k++) { char nm[24]; snprintf(nm, sizeof nm, "TouchCommTile%d%d", k / 3, k % 3); tl[k] = co_find(p, nm, 0); all &= tl[k] != 0; }
    if (!all) { atomic_store(&g_show, 0); LOG("【红】社区入口：%s 里的提示框控件不全，不显示", k_hosts[which]); return; }
#define W(nm) co_find(p, nm, 0)
    int ch[CO_ROWS], n = mode == 1 ? dm_community_channels(dm_lang_group(), ch) : 0;
    if (mode == 1 && !n) { co_hide_all(b, p); atomic_store(&g_show, 0); LOG("社区入口：没有可显示的值了，提示框收起"); return; }
    static int foc; if (st == 1) foc = 0;   // 缺省焦点：第一行「复制」（没有渠道行时是「确定」） | default focus: row 1's Copy (OK when there are no rows)
    if (foc > n) foc = n;
    if (st == 2) { uint64_t ev = atomic_exchange(&g_md_ev, 0); if (!pad) ev = 0;   // 键已在取手柄处闸门截下（dm_community_input_gate），这里只用；触屏模式不走焦点 | keys were taken at the controller gate; touch mode does not use focus   // 手柄：焦点链 复制₁ → 复制₂ → 确定（上下移、不循环），A 触发、B 收起 | controller focus chain, A activates, B dismisses
        if ((ev & DM_BTN_UP) && foc > 0) foc--; if ((ev & DM_BTN_DOWN) && foc < n) foc++;
        if (ev & DM_BTN_A) { if (foc < n) do_copy(2, foc); else atomic_store(&g_ack, 3); }
        if (ev & DM_BTN_B) atomic_store(&g_ack, 2); }
    { int ack = atomic_exchange(&g_ack, 0); if (ack) { co_hide_all(b, p); atomic_store(&g_show, 0); unsigned t0 = atomic_exchange(&g_sw_touch, 0), k0 = atomic_exchange(&g_sw_key, 0), s0 = atomic_exchange(&g_sw_stick, 0), l0 = atomic_exchange(&g_leak, 0); atomic_store(&g_md_ev, 0);
        LOG("社区入口：打开期间吞掉 触点 %u / 手柄键 %u / 摇杆 %u，交给底层 %u", t0, k0, s0, l0); LOG("社区入口：收起（%s）", ack == 2 ? "手柄 B" : ack == 3 ? "手柄 A 确定" : "点确定"); return; } }
    int vw = *(int32_t *)(b + 0x211da38), vh = *(int32_t *)(b + 0x211da3c), xr = vh > 0 ? (int)((long)vw * 2160 / vh) : 3840;
    int ox, oy; float s; co_origin(p, xr, &ox, &oy, &s);
    const co_geom_t gm_ = co_geom(n); int H = gm_.h;
    int cx = (int)((xr / 2 - ox) / s), y0 = (int)((1080 - oy) / s) - H / 2;   // 整屏居中：框 1800×H（面板单位） | centered on the screen: box 1800×H (panel units)
    // 页脚：版本（外壳给的显示版本，没有就空）、制作（右）、特别鸣谢（第二行）——名单是代码缺省常量，按语言拼两行 | footer: version, maker (right), special thanks (second line)
    char ver[96]; const char *label = dm_update_label(); if (label && *label) snprintf(ver, sizeof ver, dm_str(b, "dmAboutVersion"), label); else ver[0] = 0;
    static int grp = -1, ncr; static cr_line_t cr[2]; int g = dm_lang_group();
    if (g != grp) { int trunc; grp = g; ncr = credits_all(k_maker_default, (int)(sizeof k_maker_default / sizeof *k_maker_default), k_special_default, (int)(sizeof k_special_default / sizeof *k_special_default), NULL, 0, dm_str(b, "dmCreditsSep"), dm_str(b, "dmCreditsUpstreamNote"), cr, 2, &trunc);
        LOG("社区入口：提示框鸣谢 %d 行（制作 %d 名，特别鸣谢 %d 名，鸣谢 0 名）%s", ncr, (int)(sizeof k_maker_default / sizeof *k_maker_default), (int)(sizeof k_special_default / sizeof *k_special_default), trunc ? "，超过两行已在末尾加「…」" : ""); }
    char crt[2][224]; for (int k = 0; k < 2; k++) { if (k < ncr) snprintf(crt[k], sizeof crt[k], "%s%s%s", cr[k].kind == 3 ? dm_str(b, "dmCreditsMaker") : cr[k].kind == 1 ? dm_str(b, "dmCreditsSpecial") : cr[k].kind == 2 ? dm_str(b, "dmCreditsThanks") : "", cr[k].kind ? " " : "", cr[k].text); else crt[k][0] = 0; }
    dm_community_fit(ver, sizeof ver, 28, 880); dm_community_fit(crt[0], sizeof crt[0], 28, 560); dm_community_fit(crt[1], sizeof crt[1], 28, CO_CARD_W);
    { int gm[15][4], h = dm_notice_geometry(1800 - 128, H - 24, 1800, gm), top = (244 - h) / 2;   // 边框几何同崩溃围栏提示框（dm_crashfence.c） | frame geometry as the crash-fence box
      for (int k = 0; h && k < 15; k++) { int r[4] = { cx - 900 + gm[k][0], y0 + gm[k][1] - top, gm[k][2], gm[k][3] + (k / 3 < 4 ? 2 : 0) }; co_tile(b, tl[k], r); } }
    co_text(b, W("TouchCommTitle"), dm_str(b, mode == 1 ? "dmCommunityTitle" : "dmCommunityAbout")); co_place(W("TouchCommTitle"), cx - 850, y0 + 40);
    int cl = cx - CO_CARD_W / 2, bx = cl + CO_CARD_W - 30 - CO_BTN_W; uint64_t now = co_now(); int crow = atomic_load(&g_copied_row), cst = atomic_load(&g_copied);
    int recent = now - atomic_load(&g_copied_at) < 2000000000ull;   // 按钮字「已复制 / 复制失败」留 2 秒 | Copied / Copy failed stays 2 s
    for (int k = 0; k < CO_ROWS; k++) { char nm[32], full[GH_MAX + 1], shown[GH_MAX + 4];
        uint64_t lb, vl, cp, ct; snprintf(nm, sizeof nm, "TouchCommLabel%d", k); lb = W(nm); snprintf(nm, sizeof nm, "TouchCommValue%d", k); vl = W(nm);
        snprintf(nm, sizeof nm, "TouchCommCopy%d", k); cp = W(nm); snprintf(nm, sizeof nm, "TouchCommCopyText%d", k); ct = W(nm); snprintf(nm, sizeof nm, "TouchCommCard%d", k);
        if (k >= n || !dm_community_channel_value(ch[k], full, sizeof full, shown, sizeof shown)) { co_place(lb, 99999, 99999); co_place(vl, 99999, 99999); co_place(cp, 99999, 99999); co_place(ct, 99999, 99999); co_rect_hide(p, nm); continue; }
        int top = y0 + gm_.card_y[k]; dm_community_fit(shown, sizeof shown, 36, CO_VALUE_W);
        co_rect_lines(b, p, nm, "rule_h", "rule_v", cl, top, CO_CARD_W, 100, 2);
        co_text(b, lb, dm_str(b, ch[k] == DM_CH_QQ ? "dmCommunityLabelQq" : ch[k] == DM_CH_SITE ? "dmCommunityLabelSite" : "dmCommunityLabelGithub")); co_place(lb, cl + 40, top);
        co_text(b, vl, shown); co_place(vl, cx + CO_VALUE_X, top);
        co_place(cp, bx, top + (100 - CO_BTN_H) / 2); co_place(ct, bx, top + (100 - CO_BTN_H) / 2 + 2);
        co_text(b, ct, recent && crow == k ? dm_str(b, cst > 0 ? "dmCommunityCopied" : "dmCommunityCopyFailShort") : dm_str(b, "dmCommunityCopy")); }
    co_text(b, W("TouchCommFree"), dm_str(b, "dmLoadCommFree")); co_place(W("TouchCommFree"), cx - 850, y0 + gm_.free_y);
    co_divider(b, W("TouchCommDivider"), cx, y0 + gm_.div_y);
    co_text(b, W("TouchCommFoot0"), ver); co_place(W("TouchCommFoot0"), cl, y0 + gm_.foot_y[0]);
    co_text(b, W("TouchCommFoot0R"), crt[0]); co_place(W("TouchCommFoot0R"), cl + CO_CARD_W - 880, y0 + gm_.foot_y[0]);
    co_text(b, W("TouchCommFoot1"), crt[1]); co_place(W("TouchCommFoot1"), cl, y0 + gm_.foot_y[1]);
    co_place(W("TouchCommOk"), cx - 235, y0 + gm_.btn_y);   // 只留一颗「确定」，居中 | one OK only, centered
    { co_tapgeo_t g = { 1, vh, ox, oy, n, cx - 860, cx + 860, { 0 }, cx - 235, y0 + gm_.btn_y, 470, 115, s }; for (int k = 0; k < CO_ROWS; k++) g.card_y[k] = y0 + gm_.card_y[k]; g_tapgeo = g; }   // 触屏命中区的几何（B058） | geometry for the touch hit areas (B058)
    if (pad) { if (foc < n) co_rect_lines(b, p, "TouchCommFocus", "focus_h", "focus_v", bx - 8, y0 + gm_.card_y[foc] + (100 - CO_BTN_H) / 2 - 8, CO_BTN_W + 16, CO_BTN_H + 16, 4);
               else co_rect_lines(b, p, "TouchCommFocus", "focus_h", "focus_v", cx - 235 - 8, y0 + gm_.btn_y - 8, 470 + 16, 115 + 16, 4); }   // 手柄焦点框贴合按钮、不压字 | controller focus frame hugs the button
    else co_rect_hide(p, "TouchCommFocus");
#undef W
    if (st == 1) { atomic_store(&g_show, 2); int32_t *pr = (int32_t *)(p + 0x70);
        LOG("社区入口：已显示（%s，%s，%s，渠道 %d 行%s%s，版本%s，鸣谢 %d 行；面板 %s 锚 %.2f/%.2f 矩形 %d,%d %dx%d 缩放 %.2f → 框中心 %d 顶 %d 高 %d）", atomic_load(&g_from) == 1 ? "暂停菜单" : atomic_load(&g_from) == 2 ? "主菜单" : "自定义页", mode == 2 ? "关于框" : pad ? "手柄模式，焦点链 复制 → 确定" : "触屏 / 鼠标点按钮", mode == 2 ? "版本 + 鸣谢" : "社区", n, n < 1 ? "" : ch[0] == DM_CH_QQ ? "：群号" : ch[0] == DM_CH_SITE ? "：官网" : "：GitHub", n < 2 ? "" : ch[1] == DM_CH_SITE ? " + 官网" : " + GitHub", ver[0] ? "有" : "无", ncr,
            k_hosts[which], *(float *)(p + 0x48), *(float *)(p + 0x4c), pr[0], pr[1], pr[2], pr[3], s, cx, y0, H); }
}
