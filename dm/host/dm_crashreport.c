// 自动上报崩溃的公共部分，接口与做法见 dm_crashreport.h。
// 日志固定前缀「自动上报：」（分享诊断日志摘要取最后一行）。不写上传地址、不写签名材料；报告编号是服务端给的公开编号，可以写。
// The shared part of automatic crash reports; interface and approach in dm_crashreport.h.
// Log prefix "自动上报：" (the share summary takes the last line). The origin and signing material are never logged; the report number is the server's public id and may be.
#include "dm_crashreport.h"
#include "dm_crashfence.h"
#include "dm_custom.h"
#include "dm_diagup.h"
#include "hle.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define FIRST_DELAY_S 20        // 启动后等这么久再收集：让玩家先进主菜单，也给 MetricKit 送诊断留时间 | wait after launch: let the player reach the menu and give MetricKit time to deliver
#define MAX_PER_LAUNCH 3
#define MAX_LAUNCHES 3
#define MAX_ATTEMPTS 6
#define MAX_AGE_S (7 * 86400)
#define LONG_WAIT_S 600         // 服务端要求等得比这久：这次启动不再试 | the server asks for longer: no more attempts this launch
#define WATCHDOG_S 150          // 同 dm_diagup.c：外壳每次尝试有自己的超时 | as dm_diagup.c: the shell has its own timeouts
#define NOTE_SHOW_S 20

static const dm_crashreport_shell_t *g_shell;
static struct {   // 只在游戏主线程读写 | game main thread only
    int st, booted, attempts, launches, this_launch; unsigned token;
    long long created, next_wall; double next_mono, t_start; char fence[160];
} S;
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;   // 护 g_pend（结果从外壳的后台线程来） | guards g_pend (results come from a shell background thread)
static struct { int valid; unsigned token; int outcome, flags, retry_after; char detail[80]; } g_pend;
static char g_note_key[32], g_note_arg[16]; static double g_note_until; static _Atomic int g_note_dirty;

void dm_crashreport_init(const dm_crashreport_shell_t *shell) { g_shell = shell; }
int dm_crashreport_configured(void) { return g_shell && g_shell->configured && g_shell->configured(); }
int dm_crashreport_state(int *attempts, int *launches) { if (attempts) *attempts = S.attempts; if (launches) *launches = S.launches; return S.st; }
static double now_s(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }

static int path(char *p, size_t n, const char *ext) { char d[1024]; if (!dm_custom_dir(d, sizeof d)) return 0; return snprintf(p, n, "%s/dm_crash_report.%s", d, ext) < (int)n; }
static void drop_files(void) { static const char *const ex[] = { "payload", "id", "state" }; for (int i = 0; i < 3; i++) { char p[1100]; if (path(p, sizeof p, ex[i])) unlink(p); } }
static void state_write(void) {   // 先写临时文件再换名 | temp file, then rename
    char p[1100], t[1120]; if (!path(p, sizeof p, "state")) return; snprintf(t, sizeof t, "%s.tmp", p); FILE *f = fopen(t, "w"); if (!f) { LOG("【红】自动上报：状态文件写不进"); return; }
    int ok = fprintf(f, "created=%lld\nattempts=%d\nlaunches=%d\nnext=%lld\nfence=%s\n", S.created, S.attempts, S.launches, S.next_wall, S.fence) > 0; ok = fclose(f) == 0 && ok;
    if (ok) rename(t, p); else unlink(t);
}
static int state_read(void) {
    char p[1100], ln[256]; if (!path(p, sizeof p, "state")) return 0; FILE *f = fopen(p, "r"); if (!f) return 0;
    while (fgets(ln, sizeof ln, f)) { ln[strcspn(ln, "\r\n")] = 0;
        if (!strncmp(ln, "created=", 8)) S.created = atoll(ln + 8); else if (!strncmp(ln, "attempts=", 9)) S.attempts = atoi(ln + 9);
        else if (!strncmp(ln, "launches=", 9)) S.launches = atoi(ln + 9); else if (!strncmp(ln, "next=", 5)) S.next_wall = atoll(ln + 5);
        else if (!strncmp(ln, "fence=", 6)) snprintf(S.fence, sizeof S.fence, "%s", ln + 6); }
    fclose(f); if (S.attempts < 0 || S.launches < 0) S.attempts = S.launches = MAX_ATTEMPTS;   // 坏内容：按用完算 | bad contents: count as used up
    return 1;
}
static void note_set(const char *key, const char *arg) {
    snprintf(g_note_key, sizeof g_note_key, "%s", key); snprintf(g_note_arg, sizeof g_note_arg, "%s", arg ? arg : ""); g_note_until = 0; atomic_store(&g_note_dirty, 1);
    char p[1100]; if (path(p, sizeof p, "note")) { FILE *f = fopen(p, "w"); if (f) { fprintf(f, "%s %s\n", g_note_key, g_note_arg); fclose(f); } }
}
static void note_load(void) {
    char p[1100], k[32] = "", a[16] = ""; if (!path(p, sizeof p, "note")) return; FILE *f = fopen(p, "r"); if (!f) return;
    int n = fscanf(f, "%31s %15s", k, a); fclose(f);
    if (n >= 1 && (!strcmp(k, "dmCrashReportSent") || !strcmp(k, "dmCrashReportFailed"))) { snprintf(g_note_key, sizeof g_note_key, "%s", k); snprintf(g_note_arg, sizeof g_note_arg, "%s", a); atomic_store(&g_note_dirty, 1); }
    else unlink(p);
}
const char *dm_crashreport_note_arg(void) { return g_note_arg; }
const char *dm_crashreport_note(void) {
    if (!g_note_key[0]) return NULL;
    if (!g_note_until) { g_note_until = now_s() + NOTE_SHOW_S; char p[1100]; if (path(p, sizeof p, "note")) unlink(p); LOG("自动上报：选角页显示结果提示（%s）", g_note_key); }   // 到了玩家眼前才算看过 | counts as seen only once on screen
    return g_note_key;
}
static void finish(const char *why, const char *key, const char *arg) {
    drop_files(); S.st = DM_CR_OFF;
    LOG("自动上报：%s（共试 %d 次，跨 %d 次启动），待传报告已删除", why, S.attempts, S.launches); if (key) note_set(key, arg);
}

// 围栏状态一行：英文键值，只有原因、信号、连续次数 | the fence state on one line: English keys, only cause, signal and streak
static void fence_line(char *o, size_t n) {
    static const char *const names[4] = { "unknown", "crash", "hang", "memory" }; int cause = 0, arg = 0, streak = 0; char hist[12] = "";
    dm_crashfence_prev(&cause, &arg, &streak, hist, sizeof hist);
    snprintf(o, n, "fence cause=%s arg=%d streak=%d hist=%s", names[cause >= 0 && cause < 4 ? cause : 0], arg, streak, hist);
}
void dm_crashreport_boot(double now, long long wall) {
    S.booted = 1; S.st = DM_CR_OFF; note_load();
    int on = dm_custom_auto_crash(), have = 0; const char *te = dm_crashfence_test_env();
    if (!on) { char p[1100]; if (path(p, sizeof p, "state") && access(p, F_OK) == 0) { drop_files(); LOG("自动上报：关，删掉遗留的待传报告"); } else LOG("自动上报：关（不收集、不联网）"); return; }
    if (te && !getenv("DM_TEST_CRASHREPORT")) { LOG("自动上报：开，但本局是测试局（%s），不收集、不联网", te); return; }
    if (!dm_crashreport_configured()) { LOG("自动上报：开，但这个构建没配上报地址，不收集、不联网"); return; }
    have = state_read();
    if (have && S.fence[0]) {
        S.launches++;
        if (S.launches > MAX_LAUNCHES || S.attempts >= MAX_ATTEMPTS || (S.created > 0 && wall - S.created > MAX_AGE_S)) { S.launches--; finish("放弃上一份报告（到了重试上限或存放期限）", "dmCrashReportFailed", NULL); if (!dm_crashfence_prev(NULL, NULL, NULL, NULL, 0)) return; have = 0; S.attempts = S.launches = 0; S.next_wall = 0; S.created = 0; S.fence[0] = 0; }
    } else have = 0;
    // 待传的旧报告优先：一次只留一份；旧的传完前新的崩溃只进日志（下一次异常退出还会触发） | the pending older report goes first: only one is kept; a newer crash stays in the log until then
    if (!have) {
        if (!dm_crashfence_prev(NULL, NULL, NULL, NULL, 0)) { LOG("自动上报：开，上一局正常结束，没有要报的"); return; }
        drop_files(); S.created = wall; S.attempts = 0; S.launches = 1; S.next_wall = 0; fence_line(S.fence, sizeof S.fence);
    }
    state_write();
    S.st = DM_CR_WAIT; S.this_launch = 0; S.next_mono = now + FIRST_DELAY_S;
    LOG("自动上报：开，%s（%s），%d 秒后在后台开始", have ? "接着传上次没传完的报告" : "上一局异常退出", S.fence, FIRST_DELAY_S);
}
void dm_crashreport_setting_changed(int on) {
    if (on) return;
    if (S.st == DM_CR_RUNNING && g_shell && g_shell->cancel) g_shell->cancel();
    S.token++; S.st = DM_CR_OFF; drop_files(); LOG("自动上报：玩家关了开关，取消并删掉待传报告");
}
static int retryable(int o) { return o == DM_DIAGUP_UNCONFIRMED || o == DM_DIAGUP_UNAVAILABLE || o == DM_DIAGUP_MAINTENANCE || o == DM_DIAGUP_RATE_LIMITED; }
static void after_retryable(double now, long long wall, int retry_after) {
    static const int k_gap[MAX_PER_LAUNCH] = { 60, 300, 0 };
    int gap = S.this_launch >= 1 && S.this_launch <= MAX_PER_LAUNCH ? k_gap[S.this_launch - 1] : 0;
    if (retry_after > gap) gap = retry_after;
    S.next_wall = retry_after > 0 ? wall + retry_after : 0;
    if (S.attempts >= MAX_ATTEMPTS || (S.this_launch >= MAX_PER_LAUNCH && S.launches >= MAX_LAUNCHES)) { finish("重试到上限，放弃", "dmCrashReportFailed", NULL); return; }
    state_write();
    if (S.this_launch >= MAX_PER_LAUNCH || gap > LONG_WAIT_S) { S.st = DM_CR_IDLE; LOG("自动上报：这次启动不再试（第 %d 次，要等 %d 秒），报告留到下次启动", S.attempts, gap); return; }
    S.st = DM_CR_WAIT; S.next_mono = now + gap; LOG("自动上报：第 %d 次没成，%d 秒后静默重试", S.attempts, gap);
}
void dm_crashreport_tick(double now, long long wall) {
    if (S.st == DM_CR_WAIT && now >= S.next_mono && wall >= S.next_wall) {
        if (!g_shell || !g_shell->start) { S.st = DM_CR_IDLE; return; }
        S.attempts++; S.this_launch++; S.token++; state_write();   // 先记次数再开始：万一收集或上传本身把进程弄崩，次数上限照样管用 | count first: should collection or upload itself bring the process down, the limits still hold
        S.st = DM_CR_RUNNING; S.t_start = now; LOG("自动上报：开始第 %d 次尝试（后台收集、脱敏、签名、上传）", S.attempts);
        { char d[1024]; if (!dm_custom_dir(d, sizeof d)) d[0] = 0; g_shell->start(S.token, S.fence, d); } return;
    }
    pthread_mutex_lock(&g_mu); typeof(g_pend) p = g_pend; g_pend.valid = 0; pthread_mutex_unlock(&g_mu);
    if (p.valid) {
        if (p.token != S.token || S.st != DM_CR_RUNNING) LOG("自动上报：丢弃过期回调（第 %u 号尝试）", p.token);
        else if (p.outcome == DM_DIAGUP_OK) { char id[9]; snprintf(id, sizeof id, "%s", p.detail); LOG("自动上报：已上传，报告编号 %s%s（第 %d 次尝试，用时 %.1f 秒）", p.detail, p.flags & DM_DIAGUP_F_DUPLICATE ? "（重试命中已存的那份）" : "", S.attempts, now - S.t_start); finish("上传成功", "dmCrashReportSent", id); }
        else if (retryable(p.outcome)) { LOG("自动上报：第 %d 次结果 %d（%s%s）", S.attempts, p.outcome, p.detail, p.flags & DM_DIAGUP_F_MAYBE ? "，此前的请求可能已送达" : ""); after_retryable(now, wall, p.retry_after); }
        else if (p.outcome == DM_DIAGUP_CANCELLED) S.st = DM_CR_IDLE;
        else { LOG("%s自动上报：服务端或本机明确不能继续，结果 %d（%s）", p.outcome == DM_DIAGUP_CONFLICT ? "【红】" : "", p.outcome, p.detail); finish("不再重试", "dmCrashReportFailed", NULL); }
    }
    if (S.st == DM_CR_RUNNING && now - S.t_start > WATCHDOG_S) {
        if (g_shell && g_shell->cancel) g_shell->cancel();
        S.token++; LOG("【红】自动上报：%d 秒没等到结果，按结果未确认处理", WATCHDOG_S); after_retryable(now, wall, 0);
    }
}
void dm_crashreport_result(unsigned token, int outcome, const char *detail, int flags, int retry_after) {
    pthread_mutex_lock(&g_mu);
    g_pend.valid = 1; g_pend.token = token; g_pend.outcome = outcome; g_pend.flags = flags; g_pend.retry_after = retry_after; snprintf(g_pend.detail, sizeof g_pend.detail, "%s", detail ? detail : "");
    pthread_mutex_unlock(&g_mu);
}
void dm_update_redraw(void);
void dm_crashreport_pump(void) {
    double now = now_s(); long long wall = (long long)time(NULL);
    // 设置文件读到之后才判定：没读到就当「关」会把待传报告删掉 | decide only once the settings file is read: treating "not read yet" as off would delete a pending report
    if (!S.booted) { (void)dm_custom_auto_crash(); if (!dm_custom_loaded()) return; dm_crashreport_boot(now, wall); }
    dm_crashreport_tick(now, wall);
    if (atomic_exchange(&g_note_dirty, 0)) dm_update_redraw();
    if (g_note_until && now > g_note_until) { g_note_key[0] = 0; g_note_until = 0; dm_update_redraw(); }
}
