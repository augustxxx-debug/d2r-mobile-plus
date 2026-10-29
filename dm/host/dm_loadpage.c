// 加载页核心：见 dm_loadpage.h。不依赖平台、不读时钟、不起线程、不分配堆内存。
// Loading page core: see dm_loadpage.h. Platform independent, reads no clock, starts no thread, allocates no heap memory.
#include "dm_loadpage.h"
#include "dm_i18n.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#ifdef DM_LOADPAGE_STANDALONE   // 安卓导入前的小库：没有 dm.h 的 LOG，写安卓日志 | Android pre-import small library: no dm.h LOG, write to the Android log
#include <android/log.h>
#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "dm", __VA_ARGS__)
#else
#include "dm.h"
#endif

// 阶段种类表：文案键、缺省权重（毫秒，按实测：安卓拷 690 MB 导入包在奥丁上约 6 秒、之后管线现编合计约 2 秒（2026-10-29 实测，6e9e8e3）→ 导入 : 准备 ≈ 85 : 15；手机端生成在奥丁上 371 秒里着色器占大头；iOS 下载按 4 GB / 10 分钟）、缺省副文键
// Stage kind table: text key, default weight (ms, by measurement: Android copying the 690 MB import pack ≈ 6 s on the Thor, then ≈ 2 s of pipeline builds (measured 2026-10-29, 6e9e8e3) → import : prepare ≈ 85 : 15; on-phone generation 371 s on the Thor, mostly shaders; iOS download 4 GB / 10 min), default sub-line key
static const struct { const char *title, *hint; int weight_ms; } k_kind[DM_LP_KIND_N] = {
    [DM_LP_DOWNLOAD] = { "dmLoadStageDownload", "dmLoadHintStay", 600000 },
    [DM_LP_IMPORT]   = { "dmLoadStageImport",   "dmLoadHintImport", 8500 },
    [DM_LP_PATCH]    = { "dmLoadStagePatch",    "dmLoadHintStay", 20000 },
    [DM_LP_ASSETS]   = { "dmLoadStageAssets",   "dmLoadHintStay", 30000 },
    [DM_LP_OVERLAY]  = { "dmLoadStageOverlay",  "dmLoadHintStay", 20000 },
    [DM_LP_PACKS]    = { "dmLoadStagePacks",    "dmLoadHintShaders", 30000 },
    [DM_LP_SHADERS]  = { "dmLoadStageShaders",  "dmLoadHintShaders", 270000 },
    [DM_LP_INSTALL]  = { "dmLoadStageInstall",  "dmLoadHintStay", 15000 },
    [DM_LP_VERIFY]   = { "dmLoadStageVerify",   "dmLoadHintStay", 5000 },
    [DM_LP_PREPARE]  = { "dmLoadStagePrepare",  "dmLoadHintPrepare", 1500 },
};
static const char *const k_kind_zh[DM_LP_KIND_N] = { "下载", "导入", "改写程序", "整理资源", "界面覆盖层", "着色器打包", "着色器缓存", "安装", "校验", "准备" };   // 日志用 | for the log

typedef struct { int kind, weight_ms; double frac; uint64_t t0, t_last; int started, done; dm_lp_detail d; double rate_bps; uint64_t rate_t; int64_t rate_b; } lp_stage;
static struct {
    int state, n, cur;
    lp_stage s[DM_LP_MAX_STAGES];
    double overall;             // 只增不减 | never decreasing
    uint64_t t_begin, t_end;
    char note[320], fail[320];  // 平台给的正文 / 失败原因 | body override / failure reason from the platform
    const char *hint_key;
    int ui_group;               // -1 = dm_ui() | -1 = dm_ui()
    int muted;                  // 静音开关（缺省 0 = 有声音） | mute toggle (default 0 = sound on)
} g = { .cur = -1, .ui_group = -1, .muted = 0 };

const char *dm_loadpage_ui(const char *key) { return g.ui_group >= 0 ? dm_ui_group(key, g.ui_group) : dm_ui(key); }
void dm_loadpage_ui_set_group(int group) { g.ui_group = group; }

void dm_loadpage_begin(const int *kinds, int n, const int *weight_ms, uint64_t now_ms) {
    int grp = g.ui_group, mu = g.muted; memset(&g, 0, sizeof g); g.ui_group = grp; g.muted = mu; g.cur = -1;
    if (n > DM_LP_MAX_STAGES) n = DM_LP_MAX_STAGES;
    for (int i = 0; i < n; i++) {
        int k = kinds[i] >= 0 && kinds[i] < DM_LP_KIND_N ? kinds[i] : DM_LP_IMPORT;
        g.s[i].kind = k; g.s[i].weight_ms = weight_ms && weight_ms[i] > 0 ? weight_ms[i] : k_kind[k].weight_ms;
    }
    g.n = n; g.state = n > 0 ? DM_LP_RUNNING : DM_LP_IDLE; g.t_begin = now_ms;
    if (n > 0) { char w[256]; int o = 0; for (int i = 0; i < n && o < (int)sizeof w - 32; i++) o += snprintf(w + o, sizeof w - o, "%s%s %d 秒", i ? " → " : "", k_kind_zh[g.s[i].kind], g.s[i].weight_ms / 1000);
        LOG("加载页：开始，%d 个阶段（权重 %s）", n, w); }
}
static double total_weight(void) { double t = 0; for (int i = 0; i < g.n; i++) t += g.s[i].weight_ms; return t > 0 ? t : 1; }
static void recompute(void) {
    double done = 0;
    for (int i = 0; i < g.n; i++) done += g.s[i].weight_ms * (g.s[i].done ? 1.0 : (i == g.cur ? g.s[i].frac : 0.0));
    double v = done / total_weight(); if (v > 1) v = 1; if (v > g.overall) g.overall = v;
}
void dm_loadpage_set(int stage, double frac, const dm_lp_detail *d, uint64_t now_ms) {
    if (g.state != DM_LP_RUNNING || stage < 0 || stage >= g.n) return;
    if (frac < 0) frac = 0; if (frac > 1) frac = 1; if (frac != frac) frac = 0;
    if (stage < g.cur) { if (d) g.s[stage].d = *d; return; }   // 回报更早的阶段：记细节、不动进度（不倒退） | an earlier stage reporting: keep details, progress untouched (no regression)
    for (int i = 0; i < stage; i++) if (!g.s[i].done) { g.s[i].done = 1; g.s[i].frac = 1; if (!g.s[i].started) { g.s[i].started = 1; g.s[i].t0 = now_ms; } g.s[i].t_last = now_ms; }
    lp_stage *s = &g.s[stage];
    if (!s->started) { s->started = 1; s->t0 = now_ms; s->rate_t = now_ms; s->rate_b = d ? d->bytes_done : 0; if (g.cur != stage) LOG("加载页：阶段 %d/%d %s 开始", stage + 1, g.n, k_kind_zh[s->kind]); }
    if (g.cur != stage) g.cur = stage;
    if (s->done) { if (d) s->d = *d; return; }   // 已走完的阶段再来回报：只记细节，用时定格在走完那一刻 | a finished stage reporting again: details only, its duration stays fixed at the moment it finished
    if (frac > s->frac) s->frac = frac;   // 同一阶段内也不倒退 | no regression within a stage either
    if (d) {
        s->d = *d;
        if (d->bytes_done > s->rate_b && now_ms >= s->rate_t + 250) {   // 速度：至少隔 250 毫秒算一次，指数平滑 | speed: at least 250 ms apart, exponentially smoothed
            double inst = (double)(d->bytes_done - s->rate_b) * 1000.0 / (double)(now_ms - s->rate_t);
            s->rate_bps = s->rate_bps > 0 ? s->rate_bps * 0.7 + inst * 0.3 : inst; s->rate_t = now_ms; s->rate_b = d->bytes_done;
        }
    }
    s->t_last = now_ms;
    if (s->frac >= 1) { s->done = 1; LOG("加载页：阶段 %d/%d %s 走完，%.1f 秒", stage + 1, g.n, k_kind_zh[s->kind], (now_ms - s->t0) / 1000.0); }
    recompute();
}
void dm_loadpage_note(const char *text) { if (text) snprintf(g.note, sizeof g.note, "%s", text); else g.note[0] = 0; }
void dm_loadpage_hint(const char *key) { g.hint_key = key; }
// 最近一次完成 / 失败那一行（安卓：加载页在宿主库之前跑，日志只进 logcat；DmActivity 进游戏前把它交给宿主写进 boot.log，分享诊断日志才看得到）
// The latest finish / failure line (Android: the loading page runs before the host library, so its log reaches only logcat; DmActivity hands it to the host
// before entering the game, which writes it into boot.log where share diagnostics can see it)
static char g_last_line[512];
const char *dm_loadpage_last_line(void) { return g_last_line; }
void dm_loadpage_fail(const char *reason, uint64_t now_ms) {
    if (g.state == DM_LP_IDLE) { g.state = DM_LP_RUNNING; g.t_begin = now_ms; }
    g.state = DM_LP_FAILED; g.t_end = now_ms; snprintf(g.fail, sizeof g.fail, "%s", reason ? reason : "");
    if (g.cur >= 0) g.s[g.cur].t_last = now_ms;
    snprintf(g_last_line, sizeof g_last_line, "加载页：失败于阶段 %d/%d %s（%.0f%%）：%s", g.cur + 1, g.n, g.cur >= 0 ? k_kind_zh[g.s[g.cur].kind] : "-", g.overall * 100, g.fail);
    LOG("%s", g_last_line);
}
void dm_loadpage_finish(uint64_t now_ms) {
    if (g.state != DM_LP_RUNNING) return;
    for (int i = 0; i < g.n; i++) { if (!g.s[i].started) { g.s[i].started = 1; g.s[i].t0 = now_ms; } if (!g.s[i].done) { g.s[i].done = 1; g.s[i].frac = 1; g.s[i].t_last = now_ms; } }
    g.cur = g.n - 1; g.overall = 1; g.state = DM_LP_DONE; g.t_end = now_ms;
    char sum[384]; dm_loadpage_summary(sum, sizeof sum, now_ms);
    snprintf(g_last_line, sizeof g_last_line, "加载页：完成，%s", sum);
    LOG("%s", g_last_line);   // 分享诊断日志摘要取「加载页：」的最后一行 | the share-log summary takes the last 「加载页：」 line
}
int dm_loadpage_state(void) { return g.state; }
double dm_loadpage_overall(void) { return g.overall; }
int dm_loadpage_percent(void) { int p = (int)floor(g.overall * 100 + 1e-9); return p < 0 ? 0 : p > 100 ? 100 : p; }
int dm_loadpage_stage(void) { return g.cur; }
int dm_loadpage_stage_kind(int stage) { return stage >= 0 && stage < g.n ? g.s[stage].kind : -1; }
int dm_loadpage_stage_ms(int stage) { if (stage < 0 || stage >= g.n || !g.s[stage].started) return 0; return (int)(g.s[stage].t_last - g.s[stage].t0); }
int dm_loadpage_summary(char *buf, size_t n, uint64_t now_ms) {
    int o = 0; if (!buf || !n) return 0; buf[0] = 0;
    for (int i = 0; i < g.n && o < (int)n; i++) if (g.s[i].started) o += snprintf(buf + o, n - o, "%s%s %.1f 秒", o ? " · " : "", k_kind_zh[g.s[i].kind], dm_loadpage_stage_ms(i) / 1000.0);
    uint64_t end = g.state == DM_LP_RUNNING ? now_ms : g.t_end;
    if (o < (int)n) o += snprintf(buf + o, n - o, "%s合计 %.1f 秒", o ? " · " : "", (end > g.t_begin ? end - g.t_begin : 0) / 1000.0);
    return o < (int)n ? o : (int)n - 1;
}

// 剩余时间估计（毫秒）：当前阶段按已用时间线性外推（走了 3% 且过了 2 秒才信），不够就按权重；再加后面各阶段的权重。-1 = 还估不出
// Remaining time estimate (ms): the current stage extrapolated linearly from elapsed time (trusted once 3% and 2 s have passed), otherwise by weight; plus the weights of later stages. -1 = not yet
static double eta_ms(uint64_t now_ms) {
    if (g.cur < 0) return -1;
    const lp_stage *s = &g.s[g.cur]; double el = (double)(now_ms - s->t0), rem;
    if (s->done) rem = 0;
    else if (s->d.bytes_total > s->d.bytes_done && s->rate_bps > 0) rem = (double)(s->d.bytes_total - s->d.bytes_done) * 1000.0 / s->rate_bps;   // 知道字节与速度（下载 / 拷贝）：最准 | bytes and speed known (download / copy): most accurate
    else if (s->frac >= 0.03 && el >= 2000) rem = el * (1 - s->frac) / s->frac;
    else if (el < 2000) return -1;
    else rem = s->weight_ms * (1 - s->frac);
    for (int i = g.cur + 1; i < g.n; i++) rem += g.s[i].weight_ms;
    return rem;
}
static int cat(char *b, size_t n, int o, const char *sep, const char *piece) {
    if (o >= (int)n) return o;
    return o + snprintf(b + o, n - o, "%s%s", o && sep ? sep : "", piece);
}
static void community_text(dm_lp_text *t, uint64_t now_ms);
static void text_main(dm_lp_text *t, uint64_t now_ms);
void dm_loadpage_text(dm_lp_text *t, uint64_t now_ms) { memset(t, 0, sizeof *t); t->brand = "D2R Mobile Plus"; text_main(t, now_ms); community_text(t, now_ms); }
static void text_main(dm_lp_text *t, uint64_t now_ms) {
    snprintf(t->percent, sizeof t->percent, "%d%%", dm_loadpage_percent());
    if (g.state == DM_LP_FAILED) {
        snprintf(t->title, sizeof t->title, "%s", dm_loadpage_ui("dmLoadFailTitle"));
        snprintf(t->body, sizeof t->body, "%s", g.fail);
        snprintf(t->sub, sizeof t->sub, "%s", dm_loadpage_ui("dmLoadFailHint"));
        return;
    }
    if (g.state == DM_LP_DONE) { snprintf(t->title, sizeof t->title, "%s", dm_loadpage_ui("dmLoadDoneTitle")); snprintf(t->percent, sizeof t->percent, "100%%"); return; }
    if (g.cur < 0) {   // 还没有阶段回报：有阶段表就用第一个阶段的标题，没有（iOS 资源页等玩家 / 等下载）用「准备游戏内容」，百分数空着 | no stage reported yet: the first stage's title if there is a stage list, else "Preparing game content" (iOS asset screen waiting), percent blank
        snprintf(t->title, sizeof t->title, "%s", dm_loadpage_ui(g.n ? k_kind[g.s[0].kind].title : "dmLoadIdleTitle")); if (!g.n) t->percent[0] = 0;
        if (g.note[0]) snprintf(t->body, sizeof t->body, "%s", g.note); return; }
    const lp_stage *s = &g.s[g.cur];
    snprintf(t->title, sizeof t->title, "%s", dm_loadpage_ui(k_kind[s->kind].title));
    snprintf(t->sub, sizeof t->sub, "%s", dm_loadpage_ui(g.hint_key ? g.hint_key : k_kind[s->kind].hint));
    if (g.note[0]) { snprintf(t->body, sizeof t->body, "%s", g.note); return; }
    const char *sep = dm_loadpage_ui("dmLoadDetailSep"); char piece[160]; int o = 0;
    if (s->d.items_total > 0) { snprintf(piece, sizeof piece, dm_loadpage_ui("dmLoadDetailItems"), (long long)s->d.items_done, (long long)s->d.items_total); o = cat(t->body, sizeof t->body, o, sep, piece); }
    if (s->d.bytes_total > 0) {
        if (s->d.bytes_total >= 1000000000LL) snprintf(piece, sizeof piece, dm_loadpage_ui("dmLoadDetailBytesGb"), s->d.bytes_done / 1e9, s->d.bytes_total / 1e9);
        else snprintf(piece, sizeof piece, dm_loadpage_ui("dmLoadDetailBytesMb"), s->d.bytes_done / 1e6, s->d.bytes_total / 1e6);
        o = cat(t->body, sizeof t->body, o, sep, piece);
        if (s->rate_bps > 0) { snprintf(piece, sizeof piece, dm_loadpage_ui("dmLoadDetailSpeed"), s->rate_bps / 1e6); o = cat(t->body, sizeof t->body, o, sep, piece); }
    }
    double eta = eta_ms(now_ms);
    if (eta < 0) snprintf(piece, sizeof piece, "%s", dm_loadpage_ui("dmLoadDetailEtaCalc"));
    else if (eta >= 90000) snprintf(piece, sizeof piece, dm_loadpage_ui("dmLoadDetailEtaMin"), (int)ceil(eta / 60000.0));
    else snprintf(piece, sizeof piece, dm_loadpage_ui("dmLoadDetailEtaSec"), (int)(ceil(eta / 5000.0) * 5));   // 5 秒一档，免得数字每帧跳 | in steps of 5 s so the number does not jitter every frame
    cat(t->body, sizeof t->body, o, sep, piece);
}

// ---- 社区信息（接口说明见 dm_loadpage.h） | community info (see dm_loadpage.h) ----
static struct { char qq[16], gh[168]; int pad, copy_state; uint64_t copy_t; } cm;   // 不随 begin 清零 | not reset by begin
void dm_loadpage_community(const char *qq, const char *github) {
    snprintf(cm.qq, sizeof cm.qq, "%s", qq ? qq : ""); snprintf(cm.gh, sizeof cm.gh, "%s", github ? github : "");
    LOG("加载页：社区信息 群号%s、GitHub %s", cm.qq[0] ? "有" : "无", cm.gh[0] ? "有" : "无");
}
int dm_loadpage_community_shown(void) { return (cm.qq[0] || cm.gh[0]) && (g.state == DM_LP_RUNNING || g.state == DM_LP_FAILED); }
static int prefer_qq(void) { return cm.qq[0] && (!cm.gh[0] || !strcmp(dm_loadpage_ui("dmLoadCommPrefer"), "qq")); }
const char *dm_loadpage_copy_value(void) { return !dm_loadpage_community_shown() ? "" : prefer_qq() ? cm.qq : cm.gh; }
void dm_loadpage_copied(int ok, int pad, uint64_t now_ms) {
    if (!dm_loadpage_community_shown()) return;
    cm.copy_state = ok ? 1 : -1; cm.copy_t = now_ms;
    LOG("加载页：复制%s%s（%s）", prefer_qq() ? "群号" : "链接", ok ? "" : "失败", pad ? "手柄 A" : "触屏");
}
void dm_loadpage_pad(int on) { if ((on != 0) != cm.pad) { cm.pad = on != 0; LOG("加载页：输入换成%s", cm.pad ? "手柄" : "触屏"); } }
int dm_loadpage_pad_mode(void) { return cm.pad; }
int dm_loadpage_hit_copy(const dm_lp_layout *L, double x, double y) {
    const double pad = 8; return L->comm && x >= L->copy.x - pad && x <= L->copy.x + L->copy.w + pad && y >= L->copy.y - pad && y <= L->copy.y + L->copy.h + pad;
}
static void community_text(dm_lp_text *t, uint64_t now_ms) {
    if (!dm_loadpage_community_shown()) return;
    if (cm.qq[0]) snprintf(t->comm1, sizeof t->comm1, dm_loadpage_ui(cm.gh[0] ? "dmLoadCommQqGh" : "dmLoadCommQq"), cm.qq);
    else snprintf(t->comm1, sizeof t->comm1, "%s", dm_loadpage_ui("dmLoadCommGh"));
    snprintf(t->comm2, sizeof t->comm2, "%s", dm_loadpage_ui("dmLoadCommFree"));
    if (cm.copy_state && now_ms - cm.copy_t > 2000) cm.copy_state = 0;   // 反馈留 2 秒 | feedback stays for 2 s
    t->copy_state = cm.copy_state;
    snprintf(t->copy, sizeof t->copy, "%s", dm_loadpage_ui(cm.copy_state > 0 ? "dmLoadCommCopied" : cm.copy_state < 0 ? "dmLoadCommCopyFail" : prefer_qq() ? "dmLoadCommCopyQq" : "dmLoadCommCopyGh"));
}

// ---- 素材与布局 ----
// 两张背景：横 2048×1152、竖 1152×2048（dm/assets/loadpage/，自绘，生成脚本 dm/tools/gen_loadpage_art.py）。焦点 (0.5, 0.58)：火光在画面中下
// Two backgrounds: landscape 2048×1152, portrait 1152×2048 (dm/assets/loadpage/, self-drawn, generator dm/tools/gen_loadpage_art.py). Focus (0.5, 0.58): the fire glow sits centre-low
#define LP_BG_LAND "loadpage_bg_land.jpg"
#define LP_BG_PORT "loadpage_bg_port.jpg"
#define LP_FOCUS_X 0.5
#define LP_FOCUS_Y 0.58
const char *dm_loadpage_bg_name(double w, double h, int *bg_w, int *bg_h) {
    int land = w >= h; if (bg_w) *bg_w = land ? 2048 : 1152; if (bg_h) *bg_h = land ? 1152 : 2048;
    return land ? LP_BG_LAND : LP_BG_PORT;
}
const char *dm_loadpage_logo_name(int *w, int *h) { if (w) *w = 1600; if (h) *h = 600; return "loadpage_logo.png"; }   // dm/tools/gen_loadpage_logo.py | dm/tools/gen_loadpage_logo.py
const char *dm_loadpage_music_name(void) { return "loadpage_loop.m4a"; }
void dm_loadpage_set_muted(int muted) { g.muted = muted != 0; }
int dm_loadpage_muted(void) { return g.muted; }
int dm_loadpage_toggle_muted(void) { g.muted = !g.muted; LOG("加载页：玩家%s背景音乐", g.muted ? "关闭了" : "打开了"); return g.muted; }
int dm_loadpage_hit_mute(const dm_lp_layout *L, double x, double y) { const double pad = 12; return x >= L->mute.x - pad && x <= L->mute.x + L->mute.w + pad && y >= L->mute.y - pad && y <= L->mute.y + L->mute.h + pad; }
int dm_loadpage_music_wanted(void) { return (g.state == DM_LP_RUNNING || g.state == DM_LP_IDLE) && !g.muted; }   // 页面在就放（含等玩家拷资源的空闲态）；完成 / 失败停 | plays while the page is up (including idle, waiting for the player); stops on done / failure
void dm_loadpage_cover(double iw, double ih, double dw, double dh, double fx, double fy, dm_lp_rect *src) {
    if (iw <= 0 || ih <= 0 || dw <= 0 || dh <= 0) { *src = (dm_lp_rect){ 0, 0, iw > 0 ? iw : 1, ih > 0 ? ih : 1 }; return; }
    double scale = dw / iw > dh / ih ? dw / iw : dh / ih;   // 短边对齐（放大到两边都盖住） | fit the short side (scale until both are covered)
    double sw = dw / scale, sh = dh / scale;
    if (sw > iw) sw = iw; if (sh > ih) sh = ih;
    double sx = fx * iw - sw / 2, sy = fy * ih - sh / 2;
    if (sx < 0) sx = 0; if (sy < 0) sy = 0; if (sx + sw > iw) sx = iw - sw; if (sy + sh > ih) sy = ih - sh;
    *src = (dm_lp_rect){ sx, sy, sw, sh };
}
void dm_loadpage_layout(const dm_lp_screen *s, dm_lp_layout *o) {
    memset(o, 0, sizeof *o);
    double w = s->w > 0 ? s->w : 1, h = s->h > 0 ? s->h : 1;
    o->landscape = w >= h;
    o->bg_name = dm_loadpage_bg_name(w, h, &o->bg_w, &o->bg_h);
    dm_loadpage_cover(o->bg_w, o->bg_h, w, h, LP_FOCUS_X, LP_FOCUS_Y, &o->bg_src);
    o->bg_dst = (dm_lp_rect){ 0, 0, w, h };
    o->title_pt = 24; o->body_pt = 18; o->sub_pt = 14; o->brand_pt = 20; o->bar_radius = 3;
    double L = s->inset_l, R = s->inset_r, T = s->inset_t, B = s->inset_b, m = 28;   // 安全区之内再留 28 点边距 | 28 pt margin inside the safe area
    double cx0 = L + m, cw = w - L - R - 2 * m; if (cw < 120) cw = 120;
    double bw = o->landscape ? (cw * 0.62 > 320 ? cw * 0.62 : 320) : cw; if (bw > cw) bw = cw; if (bw > 900) bw = 900;   // 横屏条宽 62%（最宽 900）、竖屏铺满内容宽 | landscape bar 62% wide (900 max), portrait the full content width
    double bx = cx0 + (cw - bw) / 2, bh = 8;
    double sub_h = o->sub_pt * 1.4, body_h = o->body_pt * 1.4, title_h = o->title_pt * 1.35;
    double y = h - B - 44;   // 从底往上排：（社区两行 + 复制钮）/ 副文 / 正文 / 条 / 标题 + 百分数 | stacked from the bottom: (community lines + copy button) / sub / body / bar / title + percent
    if ((o->comm = dm_loadpage_community_shown())) {   // 社区两行做成页脚：贴底、占满内容宽（安全区 + 边距之内），字左对齐、复制钮靠右跨两行高；进度那一组整体上移
                                                        // community lines as a footer: at the bottom across the full content width (inside the safe area + margin), text left aligned, the copy button on the right spanning both lines; the progress group moves up
        double cb_w = 144, cb_h = 2 * sub_h + 2, cy = h - B - 20 - cb_h;
        o->copy = (dm_lp_rect){ cx0 + cw - cb_w, cy, cb_w, cb_h };
        o->comm1 = (dm_lp_rect){ cx0, cy, cw - cb_w - 12, sub_h };
        o->comm2 = (dm_lp_rect){ cx0, cy + sub_h + 2, cw - cb_w - 12, sub_h };
        y = cy - 18;
    }
    o->sub = (dm_lp_rect){ bx, y - sub_h, bw, sub_h }; y -= sub_h + 6;
    o->body = (dm_lp_rect){ bx, y - body_h, bw, body_h }; y -= body_h + 10;
    o->bar = (dm_lp_rect){ bx, y - bh, bw, bh }; y -= bh + 12;
    o->title = (dm_lp_rect){ bx, y - title_h, bw * 0.72, title_h };
    o->percent = (dm_lp_rect){ bx + bw * 0.72, y - title_h, bw * 0.28, title_h };
    double fill = g.overall < 0 ? 0 : g.overall > 1 ? 1 : g.overall;
    o->bar_fill = (dm_lp_rect){ o->bar.x, o->bar.y, o->bar.w * fill, o->bar.h };
    o->brand = (dm_lp_rect){ cx0, T + m, cw - 56, o->brand_pt * 1.4 };   // 右边让出静音钮 | leave room for the mute button on the right
    o->mute = (dm_lp_rect){ w - R - m - 40, T + m - 6, 40, 40 };
    // 字标图：宽取内容宽的 58%（横屏）/ 86%（竖屏），不超过 560 点、不矮于能放下的高度；中心在 0.36h（横）/ 0.30h（竖），夹在安全区上沿 + 边距与标题上方 24 点之间
    // Wordmark: width 58% of the content width (landscape) / 86% (portrait), at most 560 pt and never taller than the room available; centre at 0.36h (landscape) / 0.30h (portrait), clamped between the top safe edge + margin and 24 pt above the title
    o->logo_name = dm_loadpage_logo_name(&o->logo_w, &o->logo_h);
    { double lw = cw * (o->landscape ? 0.58 : 0.86); if (lw > 560) lw = 560; double lh = lw * o->logo_h / o->logo_w;
      double top_lim = T + m + 46, bot_lim = o->title.y - 24, room = bot_lim - top_lim;
      if (room > 0 && lh > room) { lh = room; lw = lh * o->logo_w / o->logo_h; }
      double cy = h * (o->landscape ? 0.36 : 0.30), ly = cy - lh / 2; if (ly + lh > bot_lim) ly = bot_lim - lh; if (ly < top_lim) ly = top_lim;
      o->logo = (dm_lp_rect){ cx0 + (cw - lw) / 2, ly, lw > 0 ? lw : 0, lh > 0 ? lh : 0 }; }
}
