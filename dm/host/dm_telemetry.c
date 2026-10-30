// 设备与使用统计的公共部分，接口与做法见 dm_telemetry.h；线上字段名只在 dm_telemetry_fields.h。
// 每个字段按白名单过一遍再进 JSON：不合格的换成缺省值或整项不发，所以路径、设备名、角色名之类进不来，字符串里也不会出现要转义的字符。
// The shared part of the device and usage statistics; interface and approach in dm_telemetry.h, wire names only in dm_telemetry_fields.h.
// Every field passes a whitelist before entering the JSON: anything else becomes the default or is left out, so paths, device names or character names cannot get in, and strings never need escaping.
#include "dm_telemetry.h"
#include "dm_telemetry_fields.h"
#include "dm_crashfence.h"
#include "dm_crashreport.h"
#include "dm_custom.h"
#include "dm_diagup.h"
#include "dm_i18n.h"
#include "dm_widget.h"
#include "hle.h"
#include <ctype.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
int dm_ingame_now(void);
int dm_padmode_current(void);
const char *dm_padmode_pad_type(void);
int dm_rt_shadow_setting(void);

#define Q_MAX_N 64
#define Q_MAX_BYTES (128 * 1024)
#define Q_MAX_AGE_S (7 * 86400)
#define Q_LINE_MAX (16 * 1024)
#define BATCH_MAX_N 20
#define BATCH_MAX_BYTES (8 * 1024)    // 前台闲时的即时小包（PRD §2.6 ③） | the foreground idle packet
#define DAY_MAX_REQS 8
#define DAY_MAX_BYTES (64 * 1024)
#define DAY_MAX_METERED (16 * 1024)
#define REQS_PER_BATCH 2              // 挑战 + 提交 | challenge + submit
#define MENU_IDLE_S 5
#define MANIFEST_WAIT_S 60            // 第一批前最多等这么久看总开关（iOS 清单在选角页出现时才拉） | wait this long for the switch before the first batch (iOS fetches the manifest when the select page appears)
#define NEXT_BATCH_S 30               // 还有存货：隔一会儿再发一批，不集中爆发 | more queued: the next batch a little later, no burst
#define LONG_WAIT_S 600               // 服务端要求等得比这久：这次启动不再试 | the server asks for longer: no more attempts this launch
#define MAX_RETRY_AFTER_S (7 * 86400)
#define WATCHDOG_S 90                 // 外壳每个请求有自己的超时；过了这么久没回话按网络失败 | the shell has its own timeouts; past this it counts as a network failure
#define MAX_ATTEMPTS 6

static const dm_telemetry_shell_t *g_shell;
typedef struct { int kind; long long wall; int d[DM_TELE_KINDS]; unsigned id; char *json; size_t len; } qent;
static struct {   // 只在游戏主线程读写 | game main thread only
    int st, booted, launch_done, attempts, pause_logged, wifi_logged, menu_was;
    unsigned token, next_id, batch[BATCH_MAX_N]; int batch_n;
    double next_mono, boot_mono, menu_since, t_start;
    long long next_wall;
    int dropped[DM_TELE_KINDS], seq;
    long long day; int day_reqs, day_bytes, day_metered, sending_metered;
    char session[33], prev_session[33];
    qent q[Q_MAX_N]; int qn; size_t qbytes;
} S;
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;   // 护 g_pend（结果从外壳的后台线程来） | guards g_pend (results come from a shell background thread)
static struct { int have_net, metered; unsigned net_token; int have_res, outcome, retry_after; unsigned res_token; char detail[80]; } g_pend;

void dm_telemetry_init(const dm_telemetry_shell_t *shell) { g_shell = shell; }
int dm_telemetry_configured(void) { return g_shell && g_shell->configured && g_shell->configured(); }
int dm_telemetry_state(int *queued, size_t *bytes, int *attempts) { if (queued) *queued = S.qn; if (bytes) *bytes = S.qbytes; if (attempts) *attempts = S.attempts; return S.st; }
int dm_telemetry_dropped(dm_tele_kind k) { return S.dropped[k]; }
static double now_s(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }
static const char *test_block(void) { const char *te = dm_crashfence_test_env(); return te && !getenv("DM_TEST_TELEMETRY") ? te : NULL; }   // 测试局缺省不动，带 DM_TEST_TELEMETRY 才动 | test runs stay out unless DM_TEST_TELEMETRY is set
static int path(char *p, size_t n, const char *name) { char d[1024]; if (!dm_custom_dir(d, sizeof d)) return 0; return snprintf(p, n, "%s/%s", d, name) < (int)n; }

// ---- 状态文件（seq、丢弃计数、当日用量、下次可发时间） | the state file ----
static void state_write(void) {
    char p[1100], t[1120]; if (!path(p, sizeof p, "dm_tele_state.txt")) return; snprintf(t, sizeof t, "%s.tmp", p); FILE *f = fopen(t, "w"); if (!f) { LOG("【红】统计上报：状态文件写不进"); return; }
    int ok = fprintf(f, "seq=%d\nd0=%d\nd1=%d\nd2=%d\nnext=%lld\nattempts=%d\nday=%lld\nreqs=%d\nbytes=%d\nmetered=%d\nsession=%s\n", S.seq, S.dropped[0], S.dropped[1], S.dropped[2], S.next_wall, S.attempts, S.day, S.day_reqs, S.day_bytes, S.day_metered, S.session) > 0;
    ok = fclose(f) == 0 && ok; if (ok) rename(t, p); else unlink(t);
}
static int hex32(const char *s) { if (strlen(s) != 32) return 0; for (; *s; s++) if (!isxdigit((unsigned char)*s)) return 0; return 1; }
static void state_read(void) {
    char p[1100], ln[128]; if (!path(p, sizeof p, "dm_tele_state.txt")) return; FILE *f = fopen(p, "r"); if (!f) return;
    while (fgets(ln, sizeof ln, f)) { ln[strcspn(ln, "\r\n")] = 0; char *eq = strchr(ln, '='); if (!eq) continue; *eq = 0; const char *v = eq + 1;
        if (!strcmp(ln, "seq")) S.seq = atoi(v); else if (!strcmp(ln, "d0")) S.dropped[0] = atoi(v); else if (!strcmp(ln, "d1")) S.dropped[1] = atoi(v); else if (!strcmp(ln, "d2")) S.dropped[2] = atoi(v);
        else if (!strcmp(ln, "next")) S.next_wall = atoll(v); else if (!strcmp(ln, "attempts")) S.attempts = atoi(v); else if (!strcmp(ln, "day")) S.day = atoll(v); else if (!strcmp(ln, "reqs")) S.day_reqs = atoi(v);
        else if (!strcmp(ln, "bytes")) S.day_bytes = atoi(v); else if (!strcmp(ln, "metered")) S.day_metered = atoi(v); else if (!strcmp(ln, "session") && hex32(v)) snprintf(S.prev_session, sizeof S.prev_session, "%s", v); }
    fclose(f); if (S.seq < 0) S.seq = 0;
    for (int k = 0; k < DM_TELE_KINDS; k++) if (S.dropped[k] < 0) S.dropped[k] = 0;
}

// ---- 队列 | the queue ----
static void q_free(int i) { S.qbytes -= S.q[i].len; free(S.q[i].json); memmove(&S.q[i], &S.q[i + 1], (size_t)(S.qn - i - 1) * sizeof S.q[0]); S.qn--; }
static void q_clear(void) { while (S.qn) q_free(S.qn - 1); }
static void q_drop(int i) {   // 丢一条：计数，带着的旧计数还回去 | drop one: count it, give back the counts it carried
    S.dropped[S.q[i].kind]++; for (int k = 0; k < DM_TELE_KINDS; k++) S.dropped[k] += S.q[i].d[k];
    q_free(i);
}
static void q_write(void) {
    char p[1100], t[1120]; if (!path(p, sizeof p, "dm_tele_queue.tsv")) return; snprintf(t, sizeof t, "%s.tmp", p); FILE *f = fopen(t, "w"); if (!f) { LOG("【红】统计上报：队列文件写不进"); return; }
    int ok = 1; for (int i = 0; ok && i < S.qn; i++) ok = fprintf(f, "%d\t%lld\t%d\t%d\t%d\t%s\n", S.q[i].kind, S.q[i].wall, S.q[i].d[0], S.q[i].d[1], S.q[i].d[2], S.q[i].json) > 0;
    ok = fclose(f) == 0 && ok; if (ok) rename(t, p); else unlink(t);
}
// 超限：先丢性能窗口，再丢会话摘要，最后丢启动事件；同类丢最旧的 | over the limit: perf windows first, then session ends, then launches; the oldest of a kind first
static int q_drop_one(void) {
    static const int order[DM_TELE_KINDS] = { DM_TELE_PERF_WINDOW, DM_TELE_SESSION_END, DM_TELE_LAUNCH };
    for (int o = 0; o < DM_TELE_KINDS; o++) for (int i = 0; i < S.qn; i++) if (S.q[i].kind == order[o]) { q_drop(i); return 1; }
    return 0;
}
static void q_enforce(long long wall) {
    int n0 = S.qn, ex = 0;
    for (int i = S.qn - 1; i >= 0; i--) if (wall - S.q[i].wall > Q_MAX_AGE_S) { q_drop(i); ex++; }
    while (S.qn > Q_MAX_N || S.qbytes > Q_MAX_BYTES) if (!q_drop_one()) break;
    if (S.qn != n0) LOG("统计上报：队列丢弃 %d 条（过期 %d），累计 启动 %d / 摘要 %d / 性能 %d", n0 - S.qn, ex, S.dropped[0], S.dropped[1], S.dropped[2]);
}
static int q_push(int kind, long long wall, const int d[DM_TELE_KINDS], const char *json, size_t len) {
    if (S.qn >= Q_MAX_N && !q_drop_one()) return 0;
    char *c = malloc(len + 1); if (!c) return 0; memcpy(c, json, len + 1);
    qent *e = &S.q[S.qn++]; memset(e, 0, sizeof *e); e->kind = kind; e->wall = wall; e->json = c; e->len = len; e->id = ++S.next_id;
    if (d) memcpy(e->d, d, sizeof e->d);
    S.qbytes += len; return 1;
}
static void q_read(void) {
    char p[1100]; if (!path(p, sizeof p, "dm_tele_queue.tsv")) return; FILE *f = fopen(p, "r"); if (!f) return;
    char *ln = malloc(Q_LINE_MAX + 64); if (!ln) { fclose(f); return; }
    int bad = 0;
    while (fgets(ln, Q_LINE_MAX + 64, f)) { ln[strcspn(ln, "\r\n")] = 0; int kind, d[DM_TELE_KINDS], at = 0; long long wall;
        if (sscanf(ln, "%d\t%lld\t%d\t%d\t%d\t%n", &kind, &wall, &d[0], &d[1], &d[2], &at) != 5 || at <= 0 || kind < 0 || kind >= DM_TELE_KINDS || ln[at] != '{') { bad++; continue; }
        q_push(kind, wall, d, ln + at, strlen(ln + at)); }
    free(ln); fclose(f); if (bad) LOG("统计上报：队列文件有 %d 行认不出，丢掉", bad);
}
static void files_drop(void) { static const char *const nm[] = { "dm_tele_queue.tsv", "dm_tele_state.txt" }; for (int i = 0; i < 2; i++) { char p[1100]; if (path(p, sizeof p, nm[i])) unlink(p); } }
int dm_telemetry_enqueue(dm_tele_kind kind, long long wall, const char *json) {
    if (!S.booted || S.st == DM_TE_OFF || !json || kind < 0 || kind >= DM_TELE_KINDS) return 0;
    size_t len = strlen(json); if (len == 0 || len >= Q_LINE_MAX || strchr(json, '\n') || strchr(json, '\t')) return 0;
    if (!q_push(kind, wall, NULL, json, len)) return 0;
    q_enforce(wall); q_write(); state_write(); if (S.st == DM_TE_IDLE) S.st = DM_TE_WAIT;
    return 1;
}

// ---- 组包：白名单 | event building: whitelists ----
static int all_in(const char *s, const char *set, size_t maxlen) { size_t n = strlen(s); if (!n || n > maxlen) return 0; for (; *s; s++) if (!(isalnum((unsigned char)*s) || strchr(set, *s))) return 0; return 1; }
static const char *v_str(const char *s, const char *set, size_t maxlen, const char *dflt) { return s && all_in(s, set, maxlen) ? s : dflt; }
static int v_digits(const char *s, size_t maxlen) { size_t n = s ? strlen(s) : 0; if (!n || n > maxlen) return 0; for (; *s; s++) if (!isdigit((unsigned char)*s)) return 0; return 1; }
static int v_revision(const char *s) { if (!s || strlen(s) != 40) return 0; for (; *s; s++) if (!(isdigit((unsigned char)*s) || (*s >= 'a' && *s <= 'f'))) return 0; return 1; }
static int v_os(const char *s) {   // \d{1,3}(\.\d{1,3}){0,3}
    if (!s || !*s) return 0; int parts = 0;
    while (*s) { int n = 0; while (isdigit((unsigned char)*s) && n < 4) { s++; n++; } if (n < 1 || n > 3 || ++parts > 4) return 0; if (*s == '.') { s++; if (!*s) return 0; } else if (*s) return 0; }
    return 1;
}
// 设备码只认 UUID（IDFV）或 16 位十六进制（ANDROID_ID）；别的一律不发 | device code: only a UUID (IDFV) or 16 hex digits (ANDROID_ID); nothing else is sent
static int v_device_code(const char *s) {
    if (!s) return 0; size_t n = strlen(s);
    if (n == 16) { for (; *s; s++) if (!isxdigit((unsigned char)*s)) return 0; return 1; }
    if (n != 36) return 0;
    for (size_t i = 0; i < 36; i++) { int dash = i == 8 || i == 13 || i == 18 || i == 23; if (dash ? s[i] != '-' : !isxdigit((unsigned char)s[i])) return 0; }
    return 1;
}
static const char *v_enum(const char *s, const char *const *set, int n) { for (int i = 0; s && i < n; i++) if (!strcmp(s, set[i])) return set[i]; return NULL; }
static void rand_hex32(char out[33]) { unsigned char r[16]; arc4random_buf(r, sizeof r); for (int i = 0; i < 16; i++) snprintf(out + i * 2, 3, "%02x", r[i]); }
typedef struct { char *p; size_t n, at; int over; } jb;
static void jf(jb *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
#include <stdarg.h>
static void jf(jb *b, const char *fmt, ...) {
    if (b->over) return; va_list a; va_start(a, fmt); int w = vsnprintf(b->p + b->at, b->n - b->at, fmt, a); va_end(a);
    if (w < 0 || (size_t)w >= b->n - b->at) { b->over = 1; return; } b->at += (size_t)w;
}
#define J_STR(b, k, v) jf(b, ",\"%s\":\"%s\"", k, v)
#define J_INT(b, k, v) jf(b, ",\"%s\":%d", k, (int)(v))
static void settings_json(jb *b) {
    static const char *const flags[] = { DM_TF_SETTING_FLAGS }, *const levels[] = { DM_TF_SETTING_LEVELS }, *const up[5] = DM_TF_UPSCALE_NAMES;
    jf(b, ",\"%s\":{", DM_TF_SETTINGS); int first = 1;
    for (size_t i = 0; i < sizeof flags / sizeof *flags; i++) { jf(b, "%s\"%s\":%d", first ? "" : ",", flags[i], dm_custom_get(flags[i]) != 0); first = 0; }
    for (size_t i = 0; i < sizeof levels / sizeof *levels; i++) jf(b, ",\"%s\":%d", levels[i], dm_custom_get(levels[i]));
    int eco = dm_custom_get("eco_fps"), up_on = dm_custom_get("upscale") != 0, q = dm_custom_get("q3d"), eff = dm_3d_profile_get();
    J_INT(b, DM_TF_S_FPS_SELECTED, eco == 1 ? 30 : eco == 2 ? 120 : 60); J_INT(b, DM_TF_S_FPS_EFFECTIVE, dm_fps_setting());
    J_STR(b, DM_TF_S_UPSCALE_SELECTED, !up_on ? up[4] : q >= 0 && q < 5 ? up[q] : up[0]); J_STR(b, DM_TF_S_UPSCALE_EFFECTIVE, eff >= 0 && eff < 5 ? up[eff] : up[4]);
    int rt_set = dm_custom_get("rt_shadow") != 0, rt_on = dm_rt_shadow_setting(); const char *ao = getenv("DM_RT_AO");
    J_STR(b, DM_TF_S_RT_SHADOW_SET, rt_set ? "on" : "off"); J_STR(b, DM_TF_S_RT_SHADOW_EFF, rt_on ? "on" : rt_set ? "unsupported" : "off"); J_INT(b, DM_TF_S_RT_AO, ao && *ao == '1');
    jf(b, ",\"%s\":null", DM_TF_S_FRAMEGEN);   // 帧生成还没并进来 | frame generation is not merged yet
    J_STR(b, DM_TF_S_INPUT_MODE, dm_padmode_current() ? "pad" : "touch"); J_STR(b, DM_TF_S_PAD_TYPE, dm_padmode_pad_type());
    J_INT(b, DM_TF_S_LANG, dm_lang_group());
    dm_tele_device_t d; memset(&d, 0, sizeof d); if (g_shell && g_shell->device) g_shell->device(&d);
    J_STR(b, DM_TF_S_FONT, d.font && !strcmp(d.font, "lxgw") ? "lxgw" : "other");
    jf(b, ",\"%s\":null}", DM_TF_S_DIFFICULTY);   // 档位只有局内读得到，主菜单没有 | the level is only readable in a game, not on the menu
}
int dm_telemetry_build_launch(double now, long long wall, char *out, size_t n) {
    dm_tele_device_t d; memset(&d, 0, sizeof d); if (g_shell && g_shell->device) g_shell->device(&d);
    static const char *const plats[] = { "ios", "android", "mac" }, *const chans[] = { "android_apk", "ios_ipa", "mac", "unknown" }, *const packs[] = { "onphone", "imported", "none", "unknown" };
    const char *plat = v_enum(d.platform, plats, 3);
    if (!plat || !v_device_code(d.device_code)) { LOG("统计上报：平台或设备码读不到，这次不生成启动事件"); return 0; }
    const char *chan = v_enum(d.channel, chans, 4); const char *pack = v_enum(d.pack_source, packs, 4);
    char ev[33]; rand_hex32(ev);
    int mono = (int)(now - S.boot_mono); if (mono < 0) mono = 0; if (mono > 86400) mono = 86400;
    int tier = 2, lim = -1, phys = -1, r0 = -1, q0 = -1; dm_mem_boot_info(&tier, &lim, &phys, &r0, &q0); if (tier < 0 || tier > 2) tier = 2;
    int cause = 0, arg = 0, abn = dm_crashfence_prev(&cause, &arg, NULL, NULL, 0);
    static const char *const causes[4] = { DM_TF_EXIT_UNKNOWN, DM_TF_EXIT_CRASH, DM_TF_EXIT_HANG, DM_TF_EXIT_MEM };
    const char *prev = abn ? causes[cause >= 0 && cause < 4 ? cause : 0] : S.prev_session[0] ? DM_TF_EXIT_NORMAL : DM_TF_EXIT_NONE;
    jb b = { out, n, 0, 0 };
    jf(&b, "{\"%s\":%d", DM_TF_SCHEMA, DM_TELEMETRY_SCHEMA); J_STR(&b, DM_TF_EVENT_ID, ev); J_STR(&b, DM_TF_TYPE, DM_TF_TYPE_LAUNCH);
    jf(&b, ",\"%s\":%lld", DM_TF_CLIENT_TS, wall); J_STR(&b, DM_TF_SESSION_ID, S.session); J_INT(&b, DM_TF_SEQ, S.seq + 1); J_INT(&b, DM_TF_MONO_S, mono);
    J_STR(&b, DM_TF_PLATFORM, plat);
    J_STR(&b, DM_TF_APP_VERSION, v_str(d.app_version, ".", 16, "0")); J_STR(&b, DM_TF_BUILD, v_digits(d.build, 12) ? d.build : "0"); J_STR(&b, DM_TF_SOURCE_REVISION, v_revision(d.source_revision) ? d.source_revision : "unknown");
    J_STR(&b, DM_TF_CHANNEL, chan ? chan : "unknown"); if (pack) J_STR(&b, DM_TF_PACK_SOURCE, pack);
    J_STR(&b, DM_TF_MODEL, v_str(d.model, " ._+,-", 40, "unknown"));
    if (d.soc && all_in(d.soc, " ._+-", 32)) J_STR(&b, DM_TF_SOC, d.soc);
    if (d.gpu_driver && all_in(d.gpu_driver, " ._+-", 48)) J_STR(&b, DM_TF_GPU_DRIVER, d.gpu_driver);
    J_STR(&b, DM_TF_OS_VERSION, v_os(d.os_version) ? d.os_version : "0"); J_INT(&b, DM_TF_MEM_TIER, tier);
    if (lim >= 512 && lim <= 65536) J_INT(&b, DM_TF_MEM_LIMIT_MIB, (lim + 32) / 64 * 64);
    J_STR(&b, DM_TF_DEVICE_CODE, d.device_code); J_INT(&b, DM_TF_CONSENT_VERSION, DM_TELEMETRY_CONSENT_VERSION);
    J_STR(&b, DM_TF_LOCALE, v_str(d.locale, "_-", 24, "und"));
    if (S.prev_session[0]) J_STR(&b, DM_TF_PREV_SESSION_ID, S.prev_session);
    J_STR(&b, DM_TF_PREV_EXIT, prev); if (abn && arg >= 0 && arg <= 255) J_INT(&b, DM_TF_PREV_EXIT_ARG, arg);
    jf(&b, ",\"%s\":%s", DM_TF_AUTO_CRASH, dm_custom_auto_crash() ? "true" : "false");
    { static const char *const qk[DM_TELE_KINDS] = { DM_TF_TYPE_LAUNCH, DM_TF_TYPE_SESSION_END, DM_TF_TYPE_PERF_WINDOW };
      jf(&b, ",\"%s\":{", DM_TF_QUEUE_DROPPED); for (int k = 0; k < DM_TELE_KINDS; k++) jf(&b, "%s\"%s\":%d", k ? "," : "", qk[k], S.dropped[k] > 999 ? 999 : S.dropped[k]); jf(&b, "}"); }
    settings_json(&b); jf(&b, "}");
    return b.over ? 0 : 1;
}
static void make_launch(double now, long long wall) {
    S.launch_done = 1; if (!S.session[0]) rand_hex32(S.session);
    char j[4096]; if (!dm_telemetry_build_launch(now, wall, j, sizeof j)) { LOG("【红】统计上报：启动事件没组成"); return; }
    S.seq++; int d[DM_TELE_KINDS]; memcpy(d, S.dropped, sizeof d); memset(S.dropped, 0, sizeof S.dropped);   // 计数随这条走；这条被丢时再还回来 | the counts travel with this event and come back if it is dropped
    q_push(DM_TELE_LAUNCH, wall, d, j, strlen(j)); q_enforce(wall); q_write(); state_write();
    LOG("统计上报：启动事件已入队（seq %d，%zu 字节，队列 %d 条）", S.seq, strlen(j), S.qn);
    if (S.st == DM_TE_IDLE) S.st = DM_TE_WAIT;
}

// ---- 发送 | sending ----
static void day_roll(long long wall) { long long d = (wall + 8 * 3600) / 86400; if (d != S.day) { S.day = d; S.day_reqs = S.day_bytes = S.day_metered = 0; } }
// 组一批：按队列顺序，蜂窝不带性能窗口；受单包与当日上限约束。返回 malloc 的正文，没东西可发返回 NULL | compose a batch in queue order, no perf windows on a metered network, within the packet and daily limits; returns a malloc'd body, NULL when nothing can go
static char *batch_compose(int metered, size_t *len) {
    size_t head = 0; char pre[64]; head = (size_t)snprintf(pre, sizeof pre, "{\"%s\":%d,\"%s\":[", DM_TF_BATCH_SCHEMA, DM_TELEMETRY_SCHEMA, DM_TF_BATCH_EVENTS);
    size_t cap = BATCH_MAX_BYTES, left = (size_t)(DAY_MAX_BYTES - S.day_bytes); if (left < cap) cap = left;
    if (metered) { size_t m = (size_t)(DAY_MAX_METERED - S.day_metered); if (m < cap) cap = m; }
    char *body = malloc(cap + 8); if (!body || cap <= head + 2) { free(body); return NULL; }
    memcpy(body, pre, head); size_t at = head; S.batch_n = 0;
    for (int i = 0; i < S.qn && S.batch_n < BATCH_MAX_N; i++) {
        if (metered && S.q[i].kind == DM_TELE_PERF_WINDOW) continue;
        if (at + S.q[i].len + 3 > cap) break;
        if (S.batch_n) body[at++] = ',';
        memcpy(body + at, S.q[i].json, S.q[i].len); at += S.q[i].len; S.batch[S.batch_n++] = S.q[i].id; }
    if (!S.batch_n) { free(body); return NULL; }
    body[at++] = ']'; body[at++] = '}'; body[at] = 0; *len = at; return body;
}
static void batch_remove(void) { for (int b = 0; b < S.batch_n; b++) for (int i = 0; i < S.qn; i++) if (S.q[i].id == S.batch[b]) { q_free(i); break; } S.batch_n = 0; }
static void batch_drop(void) { for (int b = 0; b < S.batch_n; b++) for (int i = 0; i < S.qn; i++) if (S.q[i].id == S.batch[b]) { q_drop(i); break; } S.batch_n = 0; }
static void cancel_running(const char *why) {
    if ((S.st == DM_TE_PROBE || S.st == DM_TE_SEND) && g_shell && g_shell->cancel) g_shell->cancel();
    S.token++; S.batch_n = 0; LOG("统计上报：%s，取消进行中的请求", why);
}
static int backoff_gap(int attempts) {
    static const int gaps[5] = { 60, 300, 1800, 7200, 43200 }; int g = gaps[attempts >= 1 && attempts <= 5 ? attempts - 1 : 4];
    return g * 8 / 10 + (int)arc4random_uniform((uint32_t)(g * 4 / 10 + 1));   // ±20%
}
static void after_retry(double now, long long wall, int retry_after, const char *what) {
    if (retry_after > 0) {   // 429 / 503：照服务端说的等，不占退避表 | 429 / 503: wait as told, not the backoff table
        if (retry_after > MAX_RETRY_AFTER_S) retry_after = MAX_RETRY_AFTER_S;
        S.next_wall = wall + retry_after; state_write();
        if (retry_after > LONG_WAIT_S) { S.st = DM_TE_IDLE; LOG("统计上报：%s，服务端要求等 %d 秒，这次启动不再发", what, retry_after); }
        else { S.st = DM_TE_WAIT; S.next_mono = now + retry_after; LOG("统计上报：%s，%d 秒后再试", what, retry_after); }
        return; }
    if (S.attempts >= MAX_ATTEMPTS) { S.st = DM_TE_IDLE; S.next_wall = 0; S.attempts = 0; state_write(); LOG("统计上报：%s，重试到上限，留到下次启动", what); return; }
    int gap = backoff_gap(S.attempts); S.next_wall = wall + gap; state_write();
    S.st = gap > LONG_WAIT_S ? DM_TE_IDLE : DM_TE_WAIT; S.next_mono = now + gap;
    LOG("统计上报：%s（第 %d 次），%d 秒后%s", what, S.attempts, gap, gap > LONG_WAIT_S ? "（这次启动不再发）" : "再试");
}
static void on_result(double now, long long wall, int o, const char *detail, int retry_after) {
    if (o == DM_DIAGUP_OK || o == DM_DIAGUP_CONFLICT) {   // 409：同一批服务端已有，当已送达 | 409: the server already has this batch, delivered
        int n = S.batch_n; batch_remove(); S.attempts = 0; S.next_wall = 0; q_write(); state_write();
        LOG("统计上报：已送达 %d 条%s，队列还有 %d 条", n, o == DM_DIAGUP_CONFLICT ? "（服务端已有）" : "", S.qn);
        if (S.qn) { S.st = DM_TE_WAIT; S.next_mono = now + NEXT_BATCH_S; } else S.st = DM_TE_IDLE;
        return; }
    if (o == DM_DIAGUP_MAINTENANCE || o == DM_DIAGUP_RATE_LIMITED) { S.batch_n = 0; S.attempts--; after_retry(now, wall, retry_after > 0 ? retry_after : 60, detail[0] ? detail : "服务端暂停"); return; }
    if (o == DM_DIAGUP_UNAVAILABLE || o == DM_DIAGUP_UNCONFIRMED || o == DM_DIAGUP_PREPARE) { S.batch_n = 0; after_retry(now, wall, 0, detail[0] ? detail : "网络失败"); return; }
    if (o == DM_DIAGUP_CANCELLED) { S.batch_n = 0; S.st = DM_TE_WAIT; return; }
    int n = S.batch_n; batch_drop(); q_write(); S.attempts = 0; state_write();   // 其他 4xx（校验失败）、过大：整批丢弃并计数，不重试 | other 4xx (validation), too large: the batch is dropped and counted, no retry
    LOG("【红】统计上报：服务端拒收（%d，%s），丢弃这一批 %d 条，累计 启动 %d / 摘要 %d / 性能 %d", o, detail, n, S.dropped[0], S.dropped[1], S.dropped[2]);
    if (S.qn) { S.st = DM_TE_WAIT; S.next_mono = now + NEXT_BATCH_S; } else S.st = DM_TE_IDLE;
}
void dm_telemetry_boot(double now, long long wall) {
    S.booted = 1; S.boot_mono = now; S.st = DM_TE_OFF;
    if (!dm_custom_tel_stats()) {
        char p[1100]; if (path(p, sizeof p, "dm_tele_state.txt") && access(p, F_OK) == 0) { files_drop(); if (g_shell && g_shell->forget_identity) g_shell->forget_identity(); LOG("统计上报：关，删掉遗留的队列与状态"); }
        else LOG("统计上报：关（不收集、不联网）"); return; }
    const char *te = test_block(); if (te) { LOG("统计上报：开，但本局是测试局（%s），不收集、不联网", te); return; }
    if (!dm_telemetry_configured()) { LOG("统计上报：开，但这个构建没配服务地址，不收集、不联网"); return; }
    state_read(); q_read(); q_enforce(wall); S.st = DM_TE_WAIT; S.next_mono = now; S.attempts = 0;
    LOG("统计上报：开，队列里有 %d 条（%zu 字节），seq %d", S.qn, S.qbytes, S.seq);
}
void dm_telemetry_setting_changed(int on) {
    if (!S.booted) return;
    if (!on) { if (S.st == DM_TE_OFF) return; cancel_running("玩家关了统计"); q_clear(); files_drop(); S.seq = 0; S.next_wall = 0; S.attempts = 0; S.launch_done = 0; S.session[0] = S.prev_session[0] = 0; memset(S.dropped, 0, sizeof S.dropped); S.day_reqs = S.day_bytes = S.day_metered = 0; S.st = DM_TE_OFF;
        if (g_shell && g_shell->forget_identity) g_shell->forget_identity(); LOG("统计上报：已关，本机队列、状态与上报身份已清"); return; }
    if (S.st != DM_TE_OFF) return;
    dm_telemetry_boot(now_s(), (long long)time(NULL));   // 这次启动里打开：进主菜单就记这一条启动 | turned on during this launch: this launch is recorded on the menu
}
void dm_telemetry_net(unsigned token, int metered) { pthread_mutex_lock(&g_mu); g_pend.have_net = 1; g_pend.net_token = token; g_pend.metered = metered; pthread_mutex_unlock(&g_mu); }
void dm_telemetry_result(unsigned token, int outcome, const char *detail, int retry_after) {
    pthread_mutex_lock(&g_mu); g_pend.have_res = 1; g_pend.res_token = token; g_pend.outcome = outcome; g_pend.retry_after = retry_after; snprintf(g_pend.detail, sizeof g_pend.detail, "%s", detail ? detail : ""); pthread_mutex_unlock(&g_mu);
}
void dm_telemetry_tick(double now, long long wall, int on_menu) {
    if (S.st == DM_TE_OFF) return;
    if (!on_menu) {   // 局内 / 读取 / 选项页：零请求；飞着的取消 | in a game / loading / options: zero requests; what is in flight is cancelled
        if (S.st == DM_TE_PROBE || S.st == DM_TE_SEND) { if (S.st == DM_TE_SEND && S.attempts > 0) S.attempts--; cancel_running("离开菜单"); S.st = DM_TE_WAIT; }   // 这次不占重试次数 | this one uses no retry budget
        S.menu_since = 0; return; }
    if (!S.launch_done) make_launch(now, wall);
    pthread_mutex_lock(&g_mu); typeof(g_pend) p = g_pend; g_pend.have_net = g_pend.have_res = 0; pthread_mutex_unlock(&g_mu);
    if (S.st == DM_TE_PROBE && p.have_net && p.net_token == S.token) {
        if (dm_crashreport_switch_paused(wall)) { S.st = DM_TE_WAIT; S.next_mono = now + LONG_WAIT_S; if (!S.pause_logged) { S.pause_logged = 1; LOG("统计上报：服务端总开关暂停，本次不上传，事件留在本机"); } return; }
        day_roll(wall); size_t n = 0; char *body = S.day_reqs + REQS_PER_BATCH <= DAY_MAX_REQS ? batch_compose(p.metered, &n) : NULL;
        if (!body) { S.st = DM_TE_IDLE; if (!S.wifi_logged) { S.wifi_logged = 1; LOG("统计上报：%s，这次不发（今日 %d 次请求 / %d 字节，计量 %d 字节）", p.metered ? "计量网络下没有可发的，等 Wi-Fi 或明天" : "今日额度用完或没有可发的", S.day_reqs, S.day_bytes, S.day_metered); } return; }
        S.day_reqs += REQS_PER_BATCH; S.day_bytes += (int)n; if (p.metered) S.day_metered += (int)n; S.attempts++; state_write();
        S.st = DM_TE_SEND; S.t_start = now; LOG("统计上报：发一批 %d 条（%zu 字节，%s）", S.batch_n, n, p.metered ? "计量网络" : "非计量网络");
        g_shell->send(S.token, body, n); free(body); return; }
    if (S.st == DM_TE_SEND && p.have_res && p.res_token == S.token) { on_result(now, wall, p.outcome, p.detail, p.retry_after); return; }
    if ((S.st == DM_TE_PROBE || S.st == DM_TE_SEND) && now - S.t_start > WATCHDOG_S) { cancel_running("等结果超时"); S.st = DM_TE_WAIT; after_retry(now, wall, 0, "等结果超时"); return; }
    if (S.st != DM_TE_WAIT || now < S.next_mono || wall < S.next_wall) return;
    if (!S.menu_since) S.menu_since = now;
    if (now < S.menu_since + MENU_IDLE_S) return;
    q_enforce(wall);
    if (!S.qn) { S.st = DM_TE_IDLE; return; }
    if (!g_shell || !g_shell->net_probe || !g_shell->send) { S.st = DM_TE_IDLE; return; }
    if (!g_shell->fetches_manifest) {
        if (!dm_crashreport_switch_seen() && now < S.boot_mono + MANIFEST_WAIT_S) return;   // 先等这次的清单，别抢在总开关前面发 | wait for this launch's manifest first
        if (dm_crashreport_switch_paused(wall)) { if (!S.pause_logged) { S.pause_logged = 1; LOG("统计上报：服务端总开关暂停，本次不上传，事件留在本机"); } return; }
        if (S.pause_logged) { S.pause_logged = 0; LOG("统计上报：服务端总开关恢复，接着发"); } }
    day_roll(wall);
    if (S.day_reqs + REQS_PER_BATCH > DAY_MAX_REQS) { S.st = DM_TE_IDLE; LOG("统计上报：今日请求额度用完（%d 次），明天再发", S.day_reqs); return; }
    S.token++; S.st = DM_TE_PROBE; S.t_start = now; g_shell->net_probe(S.token);
}
// 在主菜单 / 选人页（面板显示着）才算闲时；进局读取时也不发 | idle only on the main menu / character select (the panel is showing); not while a game loads
static int on_menu_now(void) {
    if (dm_ingame_now()) return 0;
    uint8_t *b = NULL; for (int i = 0; i < g_nmod; i++) if (!strcmp(g_mod[i].name, "main")) b = (uint8_t *)g_mod[i].base; if (!b) return 0;
    uint64_t root = *(uint64_t *)(b + 0x2f05458); if (!root) return 0;
    static const char *const hosts[] = { "MainMenuPanel", "MainMenuButtonRibbon", "CharacterSelectPanel" };
    for (int i = 0; i < 3; i++) { uint64_t p = dm_ui_top(root, hosts[i]); if (p && *(uint8_t *)(p + 0x50)) return 1; }
    return 0;
}
int dm_telemetry_notice_due(void) { return dm_custom_loaded() && !dm_custom_tel_notice() && dm_telemetry_configured() && (!dm_crashfence_test_env() || getenv("DM_TEST_TELEMETRY_NOTICE")); }
void dm_telemetry_pump(void) {
    double now = now_s(); long long wall = (long long)time(NULL);
    if (!S.booted) { (void)dm_custom_tel_stats(); if (!dm_custom_loaded()) return; dm_telemetry_boot(now, wall); }
    if (S.st == DM_TE_OFF) return;   // 没同意：连面板都不看 | no consent: not even a look at the panels
    dm_telemetry_tick(now, wall, on_menu_now());
}
