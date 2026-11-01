// 设备与使用统计的策略与组包（dm_telemetry.c）：没同意零请求；开关关闭清队列与身份；组包字段全在白名单内、脱敏（UDID / 设备名 / 用户名 / 路径 / 角色名都进不来）；总开关三态（暂停 / 缓存 / 恢复补发）；队列上限、丢弃顺序与计数；局内零请求；蜂窝只发小包；Retry-After 与退避；逐事件回执与无效回执；能力协商；409、403、实现错误（P-000）；seq 跨启动单调、关闭后从 1 起。
// 不碰网络与游戏：假外壳记录调用，设置与围栏由测试给，时钟由测试给（dm_telemetry_boot / tick 直接调）。
// Statistics policy and event building: no consent = zero requests; turning off clears queue and identity; event fields within whitelists and scrubbed (UDID / device name / user name / path / character name cannot get in); the server switch's three states (paused / cached / resume and catch up); queue limits, drop order and counts; zero requests in a game; small packets on metered networks; Retry-After and backoff; per-event and invalid receipts; capabilities; 409, 403, implementation errors (P-000); seq monotonic across launches and from 1 after turning off.
// No network and no game: a fake shell records calls; settings, fence and clock come from the test.
#include "../dm_telemetry.c"
#include <assert.h>
#include <sys/stat.h>
module_t g_mod[MAX_MOD]; int g_nmod;
static char g_dir[256];
static int f_stats = 1, f_notice = 1, f_cfg = 1, f_crash, f_prev, f_cause = 1, f_arg = 11, f_pad, f_paused, f_seen = 1, f_probes, f_sends, f_cancels, f_forgets, f_ingame;
static const char *f_test; static unsigned f_tok; static char f_body[64 * 1024]; static size_t f_blen;
static dm_tele_device_t f_dev;
int dm_custom_tel_stats(void) { return f_stats; }
int dm_custom_tel_notice(void) { return f_notice; }
int dm_custom_loaded(void) { return 1; }
int dm_custom_auto_crash(void) { return f_crash; }
int dm_custom_dir(char *p, size_t n) { snprintf(p, n, "%s", g_dir); return 1; }
int dm_custom_get(const char *k) { if (!strcmp(k, "eco_fps")) return 2; if (!strcmp(k, "q3d")) return 3; if (!strcmp(k, "upscale")) return 1; if (!strcmp(k, "rt_shadow")) return 1; if (!strcmp(k, "render_res")) return 2; return 1; }
int dm_fps_setting(void) { return 120; }
int dm_3d_profile_get(void) { return 2; }
int dm_rt_shadow_setting(void) { return 0; }
int dm_padmode_current(void) { return f_pad; }
const char *dm_padmode_pad_type(void) { return f_pad ? "xbox" : "none"; }
int dm_lang_group(void) { return 1; }
void dm_mem_boot_info(int *t, int *l, int *p, int *a, int *b) { *t = 2; *l = 3375; *p = 12000; *a = 0; *b = 0; }
int dm_ingame_now(void) { return f_ingame; }
const char *dm_crashfence_test_env(void) { return f_test; }
int dm_crashfence_prev(int *c, int *a, int *s, char *h, size_t n) { (void)s; (void)h; (void)n; if (c) *c = f_cause; if (a) *a = f_arg; return f_prev; }
int dm_crashreport_switch_paused(long long w) { (void)w; return f_paused; }
int dm_crashreport_switch_seen(void) { return f_seen; }
uint64_t dm_ui_top_real(uint64_t r, const char *n) { (void)r; (void)n; return 0; }
int dm_ui_top_chk_on(void) { return 0; }
void dm_ui_top_chk(uint64_t r, const char *n, uint64_t g) { (void)r; (void)n; (void)g; }
static int s_cfg(void) { return f_cfg; }
static void s_device(dm_tele_device_t *d) { *d = f_dev; }
static void s_probe(unsigned t) { f_probes++; f_tok = t; }
#define CH43 "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
#define CH_ALL "{\"challenge\":\"" CH43 "\",\"capabilities\":[\"telemetry-launch-v1\",\"telemetry-session-v1\",\"telemetry-perf-v1\"]}"
static const char *f_ch_reply = CH_ALL; static int f_ch_status = 200, f_chals, f_ch_manual;
static void s_challenge(unsigned t) { f_chals++; f_tok = t; if (!f_ch_manual) dm_telemetry_http(t, f_ch_status, f_ch_reply, strlen(f_ch_reply), 0); }
static char f_sent_ch[64];
static void s_send(unsigned t, const char *ch, const char *b, size_t n) { f_sends++; f_tok = t; snprintf(f_sent_ch, sizeof f_sent_ch, "%s", ch); assert(n < sizeof f_body); memcpy(f_body, b, n); f_body[n] = 0; f_blen = n; }
static void s_cancel(void) { f_cancels++; }
static void s_forget(void) { f_forgets++; }
static const dm_telemetry_shell_t shell = { s_cfg, s_device, s_probe, s_challenge, s_send, s_cancel, s_forget, 0 };
#define EID "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
#define E_(t) "{\"event_id\":\"" EID "\",\"t\":\"" t "\"}"
// 按这一批造回执：statuses 给每条的状态，缺省全 accepted | build a receipt for this batch
static void receipt(char *out, size_t n, const char *status) {
    size_t w = (size_t)snprintf(out, n, "{\"results\":[");
    for (int i = 0; i < S.batch_n; i++) { const qent *e = batch_ev(i); const char *q = e ? strstr(e->json, "\"event_id\":\"") : NULL; char id[33] = ""; if (q) snprintf(id, sizeof id, "%.32s", q + 12); w += (size_t)snprintf(out + w, n - w, "%s{\"event_index\":%d,\"event_id\":\"%s\",\"status\":\"%s\"}", i ? "," : "", i, id, status); }
    snprintf(out + w, n - w, "]}");
}
static void http(unsigned t, int status, const char *b, int after) { dm_telemetry_http(t, status, b, strlen(b), after); }
static void ok_reply(unsigned t) { static char b[8192]; receipt(b, sizeof b, "accepted"); http(t, 200, b, 0); }
static int checks, fails;
#define CHECK(x) do { checks++; if (!(x)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)
static int exists(const char *n) { char p[1100]; snprintf(p, sizeof p, "%s/%s", g_dir, n); return access(p, F_OK) == 0; }
static void zero_state(void) { q_clear(); memset(&S, 0, sizeof S); memset(&g_pend, 0, sizeof g_pend); }
static void wipe(void) { zero_state(); static const char *const n[] = { "dm_tele_queue.tsv", "dm_tele_state.txt" }; for (int i = 0; i < 2; i++) { char p[1100]; snprintf(p, sizeof p, "%s/%s", g_dir, n[i]); unlink(p); } f_probes = f_sends = f_cancels = f_forgets = f_chals = 0; }
static void reset_counts(void) { f_probes = f_sends = f_cancels = f_forgets = f_chals = 0; }
static void launch(double now, long long wall) { zero_state(); dm_telemetry_boot(now, wall); }
// 走到发出一批：过 5 秒闲时 → 探网络 → 发 | run until one batch goes out: 5 idle seconds → probe → send
static double go_send(double now, long long wall, int metered) {
    dm_telemetry_tick(now, wall, 1); dm_telemetry_tick(now + 6, wall + 6, 1);
    if (S.st == DM_TE_PROBE) { dm_telemetry_net(f_tok, metered); dm_telemetry_tick(now + 6.1, wall + 6, 1); }
    return now + 6.2;
}
static const char *ALLOWED_TOP[] = { DM_TF_SCHEMA, DM_TF_EVENT_ID, DM_TF_TYPE, DM_TF_CLIENT_TS, DM_TF_SESSION_ID, DM_TF_SEQ, DM_TF_MONO_S, DM_TF_PLATFORM, DM_TF_APP_VERSION, DM_TF_BUILD, DM_TF_SOURCE_REVISION, DM_TF_CHANNEL, DM_TF_PACK_SOURCE, DM_TF_MODEL, DM_TF_SOC, DM_TF_GPU_DRIVER, DM_TF_OS_VERSION, DM_TF_MEM_TIER, DM_TF_MEM_LIMIT_MIB, DM_TF_DEVICE_CODE, DM_TF_CONSENT_VERSION, DM_TF_LOCALE, DM_TF_PREV_SESSION_ID, DM_TF_PREV_EXIT, DM_TF_PREV_EXIT_ARG, DM_TF_AUTO_CRASH, DM_TF_QUEUE_DROPPED, DM_TF_SETTINGS };
static int has(const char *j, const char *k) { char q[64]; snprintf(q, sizeof q, "\"%s\":", k); return strstr(j, q) != NULL; }
int main(void) {
    snprintf(g_dir, sizeof g_dir, "/tmp/t_telemetry.%d", getpid()); mkdir(g_dir, 0700); unsetenv("DM_TEST_TELEMETRY"); unsetenv("DM_TEST_TELEMETRY_NOTICE");
    f_dev = (dm_tele_device_t){ .platform = "ios", .app_version = "1.0.0", .build = "28", .source_revision = "0123456789abcdef0123456789abcdef01234567", .channel = "ios_ipa", .model = "iPhone18,3", .os_version = "27.1.2", .locale = "zh_CN", .device_code = "01234567-89AB-CDEF-0123-456789ABCDEF", .font = "lxgw" };
    dm_telemetry_init(&shell); char j[4096]; int q = 0; size_t by = 0; int at = 0;

    // ① 没同意 / 关着：零请求、不写文件、不看面板；遗留的队列删掉、身份也删 | no consent / off: zero requests, no files; leftovers and the identity are removed
    f_stats = 0; launch(0, 1000); dm_telemetry_tick(100, 1100, 1); CHECK(dm_telemetry_state(0, 0, 0) == DM_TE_OFF && f_probes == 0 && f_sends == 0 && !exists("dm_tele_queue.tsv") && !exists("dm_tele_state.txt"));
    { char p[1100]; snprintf(p, sizeof p, "%s/dm_tele_state.txt", g_dir); FILE *f = fopen(p, "w"); fputs("seq=9\n", f); fclose(f); snprintf(p, sizeof p, "%s/dm_tele_queue.tsv", g_dir); f = fopen(p, "w"); fputs("0\t1\t0\t0\t0\t{}\n", f); fclose(f); }
    launch(0, 1000); CHECK(!exists("dm_tele_state.txt") && !exists("dm_tele_queue.tsv") && f_forgets == 1); reset_counts(); f_stats = 1;
    // 测试局缺省不动；没配地址不动 | test runs and builds without an origin stay out
    f_test = "DM_KEYS"; launch(0, 1000); dm_telemetry_tick(100, 1100, 1); CHECK(dm_telemetry_state(0, 0, 0) == DM_TE_OFF && f_probes == 0); f_test = NULL;
    f_cfg = 0; launch(0, 1000); dm_telemetry_tick(100, 1100, 1); CHECK(dm_telemetry_state(0, 0, 0) == DM_TE_OFF && f_probes == 0); f_cfg = 1;

    // ② 组包：字段齐全、白名单内 | event building: complete fields, within whitelists
    wipe(); launch(0, 1000); rand_hex32(S.session); CHECK(dm_telemetry_build_launch(12, 1000, j, sizeof j));
    for (size_t i = 0; i < sizeof ALLOWED_TOP / sizeof *ALLOWED_TOP; i++) if (strcmp(ALLOWED_TOP[i], DM_TF_PACK_SOURCE) && strcmp(ALLOWED_TOP[i], DM_TF_SOC) && strcmp(ALLOWED_TOP[i], DM_TF_GPU_DRIVER) && strcmp(ALLOWED_TOP[i], DM_TF_PREV_SESSION_ID) && strcmp(ALLOWED_TOP[i], DM_TF_PREV_EXIT_ARG)) CHECK(has(j, ALLOWED_TOP[i]));
    CHECK(strstr(j, "\"seq\":1,") && strstr(j, "\"mono_s\":12,") && strstr(j, "\"client_ts\":1000,") && strstr(j, "\"consent_version\":1,") && strstr(j, "\"channel\":\"ios_ipa\"") && strstr(j, "\"mem_tier\":2,") && strstr(j, "\"mem_limit_mib\":3392,"));
    CHECK(strstr(j, "\"prev_exit\":\"none\"") && !has(j, DM_TF_PREV_SESSION_ID) && !has(j, DM_TF_PREV_EXIT_ARG));
    CHECK(strstr(j, "\"queue_dropped\":{\"launch\":0,\"session_end\":0,\"perf_window\":0}"));
    // 设置快照 31 键 | the settings snapshot's 31 keys
    { static const char *const ks[] = { DM_TF_SETTING_FLAGS, DM_TF_SETTING_LEVELS, DM_TF_S_FPS_SELECTED, DM_TF_S_FPS_EFFECTIVE, DM_TF_S_UPSCALE_SELECTED, DM_TF_S_UPSCALE_EFFECTIVE, DM_TF_S_RT_SHADOW_SET, DM_TF_S_RT_SHADOW_EFF, DM_TF_S_RT_AO, DM_TF_S_FRAMEGEN, DM_TF_S_INPUT_MODE, DM_TF_S_PAD_TYPE, DM_TF_S_LANG, DM_TF_S_FONT, DM_TF_S_DIFFICULTY };
      CHECK(sizeof ks / sizeof *ks == 31);
      const char *st = strstr(j, "\"settings\":{"); CHECK(st != NULL); for (size_t i = 0; st && i < sizeof ks / sizeof *ks; i++) CHECK(has(st, ks[i])); }
    CHECK(strstr(j, "\"fps_selected\":120") && strstr(j, "\"fps_effective\":120") && strstr(j, "\"upscale_selected\":\"quality\"") && strstr(j, "\"upscale_effective\":\"balanced\"") && strstr(j, "\"rt_shadow_set\":\"on\"") && strstr(j, "\"rt_shadow_eff\":\"unsupported\"") && strstr(j, "\"framegen\":null") && strstr(j, "\"input_mode\":\"touch\"") && strstr(j, "\"pad_type\":\"none\"") && strstr(j, "\"lang\":1") && strstr(j, "\"font\":\"lxgw\""));
    // 脱敏：外壳给什么脏值都进不来；设备码不合格就不生成 | scrubbing: no dirty value from the shell gets in; a bad device code generates nothing
    { dm_tele_device_t keep = f_dev;
      f_dev.model = "/Users/someone/Augustus' iPhone"; f_dev.locale = "zh_CN/../../Users/someone"; f_dev.app_version = "1.0 (Bob's)"; f_dev.build = "abc"; f_dev.source_revision = "Paladin"; f_dev.os_version = "iPhone OS 27"; f_dev.channel = "http://x"; f_dev.soc = "a\"b"; f_dev.gpu_driver = "/var/mobile/x"; f_dev.pack_source = "somewhere";
      CHECK(dm_telemetry_build_launch(12, 1000, j, sizeof j));
      CHECK(!strstr(j, "Users") && !strstr(j, "someone") && !strstr(j, "Augustus") && !strstr(j, "Bob") && !strstr(j, "Paladin") && !strstr(j, "http") && !strstr(j, "/") && !strstr(j, "var"));
      CHECK(strstr(j, "\"model\":\"unknown\"") && strstr(j, "\"locale\":\"und\"") && strstr(j, "\"app_version\":\"0\"") && strstr(j, "\"build\":\"0\"") && strstr(j, "\"source_revision\":\"unknown\"") && strstr(j, "\"os_version\":\"0\"") && strstr(j, "\"channel\":\"unknown\"") && !has(j, DM_TF_SOC) && !has(j, DM_TF_GPU_DRIVER) && !has(j, DM_TF_PACK_SOURCE));
      f_dev = keep; f_dev.device_code = "00008110-000A1B2C3D4E5F60"; CHECK(!dm_telemetry_build_launch(12, 1000, j, sizeof j));   // UDID 形状：拒 | a UDID shape: refused
      f_dev.device_code = "Augustus iPhone"; CHECK(!dm_telemetry_build_launch(12, 1000, j, sizeof j)); f_dev.device_code = "0123456789abcdef"; CHECK(dm_telemetry_build_launch(12, 1000, j, sizeof j)); CHECK(strstr(j, "\"device_code\":\"0123456789abcdef\"")); f_dev = keep; }
    // 上一局退出原因 | the previous exit
    f_prev = 1; f_cause = 3; f_arg = 9; CHECK(dm_telemetry_build_launch(1, 1000, j, sizeof j) && strstr(j, "\"prev_exit\":\"mem\"") && strstr(j, "\"prev_exit_arg\":9")); f_cause = 1; f_arg = 11; CHECK(dm_telemetry_build_launch(1, 1000, j, sizeof j) && strstr(j, "\"prev_exit\":\"crash\"")); f_prev = 0;

    // ③ 正常一轮：菜单上才生成、5 秒闲时才发；seq 跨启动单调 | one normal round: built on the menu, sent after 5 idle seconds; seq monotonic across launches
    wipe(); launch(0, 1000); dm_telemetry_tick(1, 1001, 0); CHECK(!exists("dm_tele_queue.tsv"));   // 没到菜单不生成 | nothing before the menu
    dm_telemetry_tick(2, 1002, 1); CHECK(exists("dm_tele_queue.tsv") && dm_telemetry_state(&q, 0, 0) == DM_TE_WAIT && q == 1 && f_probes == 0);
    dm_telemetry_tick(4, 1004, 1); CHECK(f_probes == 0);
    dm_telemetry_tick(8, 1008, 1); CHECK(f_probes == 1 && dm_telemetry_state(0, 0, 0) == DM_TE_PROBE);
    dm_telemetry_net(f_tok, 0); dm_telemetry_tick(8.1, 1008, 1); CHECK(f_sends == 1 && dm_telemetry_state(0, 0, &at) == DM_TE_SEND && at == 1);
    CHECK(strstr(f_body, "{\"schema\":1,\"events\":[{") == f_body && strstr(f_body, "\"type\":\"launch\""));
    ok_reply(f_tok); dm_telemetry_tick(9, 1009, 1); CHECK(dm_telemetry_state(&q, 0, 0) == DM_TE_IDLE && q == 0);
    launch(0, 2000); dm_telemetry_tick(1, 2001, 1); CHECK(S.seq == 2 && S.prev_session[0] && strstr(S.q[0].json, "\"seq\":2,") && strstr(S.q[0].json, "\"prev_exit\":\"normal\"") && strstr(S.q[0].json, "\"prev_session_id\":"));

    // 这次启动里才打开：mono_s 从启动算 | turned on during this launch: mono_s counts from the launch
    wipe(); f_stats = 0; launch(100, 1000); f_stats = 1; dm_telemetry_setting_changed(1); dm_telemetry_tick(130, 1030, 1); CHECK(S.qn == 1 && strstr(S.q[0].json, "\"mono_s\":30,"));
    // ④ 关闭清队列、状态、身份；重开 seq 从 1 起；此后零请求 | off clears queue, state and identity; seq restarts from 1; zero requests afterwards
    reset_counts(); dm_telemetry_setting_changed(0); CHECK(dm_telemetry_state(&q, 0, 0) == DM_TE_OFF && q == 0 && f_forgets == 1 && !exists("dm_tele_queue.tsv") && !exists("dm_tele_state.txt"));
    dm_telemetry_tick(100, 3000, 1); CHECK(f_probes == 0 && f_sends == 0);
    dm_telemetry_setting_changed(1); dm_telemetry_tick(101, 3001, 1); CHECK(S.seq == 1 && strstr(S.q[0].json, "\"seq\":1,") && strstr(S.q[0].json, "\"prev_exit\":\"none\""));
    // 飞着的时候关：取消、迟到回调丢弃 | turned off in flight: cancelled, a late callback is dropped
    wipe(); launch(0, 1000); go_send(1, 1001, 0); CHECK(f_sends == 1); unsigned stale = f_tok; dm_telemetry_setting_changed(0); CHECK(f_cancels == 1); http(stale, 200, "{}", 0); dm_telemetry_tick(20, 1020, 1); CHECK(dm_telemetry_state(0, 0, 0) == DM_TE_OFF);

    // ⑤ 总开关：暂停 = 事件照入队、零探测零请求；清单没到先等（最多 60 秒）；恢复后补发 | server switch: paused = events queue, no probe, no request; wait for the manifest (at most 60 s); resumes and catches up
    wipe(); f_paused = 1; launch(0, 1000); dm_telemetry_tick(1, 1001, 1); dm_telemetry_tick(50, 1050, 1); dm_telemetry_tick(90, 1090, 1); CHECK(dm_telemetry_state(&q, 0, 0) == DM_TE_WAIT && q == 1 && f_probes == 0 && f_sends == 0);
    launch(0, 2000); dm_telemetry_tick(1, 2001, 1); dm_telemetry_tick(90, 2090, 1); CHECK(dm_telemetry_state(&q, 0, 0) == DM_TE_WAIT && q == 2 && f_probes == 0);   // 缓存 false：下次启动继续停，队列保留 | cached false: the next launch stays stopped, the queue is kept
    f_paused = 0; dm_telemetry_tick(91, 2091, 1); CHECK(f_probes == 1); dm_telemetry_net(f_tok, 0); dm_telemetry_tick(92, 2092, 1); CHECK(f_sends == 1 && strstr(f_body, "\"seq\":1,") && strstr(f_body, "\"seq\":2,"));   // 恢复：补发两条 | resumed: both go out
    wipe(); f_seen = 0; launch(0, 1000); dm_telemetry_tick(1, 1001, 1); dm_telemetry_tick(30, 1030, 1); CHECK(f_probes == 0); dm_telemetry_tick(61, 1061, 1); CHECK(f_probes == 1); f_seen = 1;   // 清单一直没到：60 秒后按缓存走 | the manifest never came: the cache rules after 60 s
    // 探网络期间（安卓现拉清单）发现暂停：不发 | paused found during the probe (Android fetches the manifest there): nothing sent
    wipe(); launch(0, 1000); dm_telemetry_tick(1, 1001, 1); dm_telemetry_tick(8, 1008, 1); CHECK(f_probes == 1); f_paused = 1; dm_telemetry_net(f_tok, 0); dm_telemetry_tick(9, 1009, 1); CHECK(f_sends == 0 && dm_telemetry_state(&q, 0, 0) == DM_TE_WAIT && q == 1); f_paused = 0;

    // ⑥ 队列上限：64 条 / 128 KiB / 7 天；丢弃顺序 性能 → 摘要 → 启动；计数进下一条启动事件 | queue limits; drop order; counts in the next launch event
    wipe(); launch(0, 1000); S.launch_done = 1; char big[1024]; memset(big, 'x', sizeof big); big[0] = '{'; big[sizeof big - 2] = '}'; big[sizeof big - 1] = 0;
    for (int i = 0; i < 3; i++) CHECK(dm_telemetry_enqueue(DM_TELE_PERF_WINDOW, 1000, E_("p")));
    for (int i = 0; i < 10; i++) CHECK(dm_telemetry_enqueue(DM_TELE_LAUNCH, 1000, E_("l")));
    for (int i = 0; i < 51; i++) CHECK(dm_telemetry_enqueue(DM_TELE_SESSION_END, 1000, E_("s")));
    dm_telemetry_state(&q, 0, 0); CHECK(q == 64 && dm_telemetry_dropped(DM_TELE_PERF_WINDOW) == 0);
    CHECK(dm_telemetry_enqueue(DM_TELE_SESSION_END, 1000, E_("s"))); dm_telemetry_state(&q, 0, 0); CHECK(q == 64 && dm_telemetry_dropped(DM_TELE_PERF_WINDOW) == 1);
    CHECK(dm_telemetry_enqueue(DM_TELE_SESSION_END, 1000, E_("s")) && dm_telemetry_enqueue(DM_TELE_SESSION_END, 1000, E_("s")) && dm_telemetry_dropped(DM_TELE_PERF_WINDOW) == 3);
    CHECK(dm_telemetry_enqueue(DM_TELE_LAUNCH, 1000, E_("l")) && dm_telemetry_dropped(DM_TELE_SESSION_END) == 1 && dm_telemetry_dropped(DM_TELE_LAUNCH) == 0);
    // 超字节 | over the byte limit
    wipe(); launch(0, 1000); S.launch_done = 1; { static char huge[8000]; memset(huge, 'x', sizeof huge); huge[0] = '{'; huge[sizeof huge - 2] = '}'; huge[sizeof huge - 1] = 0; for (int i = 0; i < 20; i++) dm_telemetry_enqueue(DM_TELE_SESSION_END, 1000, huge); }
    dm_telemetry_state(&q, &by, 0); CHECK(by <= Q_MAX_BYTES && q == 16 && dm_telemetry_dropped(DM_TELE_SESSION_END) == 4);
    // 过期 7 天；落盘后下次启动读回；计数进启动事件；发送成功后清零 | expired after 7 days; read back from disk; counts travel with the launch event
    wipe(); launch(0, 1000); S.launch_done = 1; dm_telemetry_enqueue(DM_TELE_SESSION_END, 1000, E_("old")); dm_telemetry_enqueue(DM_TELE_SESSION_END, 1000 + Q_MAX_AGE_S, E_("new"));
    launch(0, 1000 + Q_MAX_AGE_S + 5); dm_telemetry_state(&q, 0, 0); CHECK(q == 1 && dm_telemetry_dropped(DM_TELE_SESSION_END) == 1);
    dm_telemetry_tick(1, 1000 + Q_MAX_AGE_S + 6, 1); dm_telemetry_state(&q, 0, 0); CHECK(q == 2 && strstr(S.q[1].json, "\"queue_dropped\":{\"launch\":0,\"session_end\":1,\"perf_window\":0}") && dm_telemetry_dropped(DM_TELE_SESSION_END) == 0);
    // 带着计数的启动事件被丢：计数还回来，再加它自己 | a launch event carrying counts is dropped: the counts come back, plus itself
    q_drop(1); CHECK(dm_telemetry_dropped(DM_TELE_SESSION_END) == 1 && dm_telemetry_dropped(DM_TELE_LAUNCH) == 1);
    // 坏行不崩、丢掉 | bad lines are dropped without harm
    wipe(); { char p[1100]; snprintf(p, sizeof p, "%s/dm_tele_queue.tsv", g_dir); FILE *f = fopen(p, "w"); fputs("garbage\n9\t1\t0\t0\t0\t{}\n0\t1000\t0\t0\t0\t{\"ok\":1}\n", f); fclose(f); } launch(0, 1000); dm_telemetry_state(&q, 0, 0); CHECK(q == 1);

    // ⑦ 局内零请求：进局取消飞着的，不占次数，回菜单重新等 5 秒 | zero requests in a game: what flies is cancelled, no budget spent, 5 s again on return
    wipe(); launch(0, 1000); double t = go_send(1, 1001, 0); CHECK(f_sends == 1 && S.attempts == 1); unsigned tk = f_tok; dm_telemetry_tick(t, 1010, 0); CHECK(f_cancels == 1 && S.attempts == 0 && dm_telemetry_state(0, 0, 0) == DM_TE_WAIT);
    for (int i = 0; i < 600; i++) dm_telemetry_tick(t + i, 1010 + i, 0); CHECK(f_sends == 1 && f_probes == 1);   // 局内十分钟：一个请求都没有 | ten minutes in a game: not one request
    http(tk, 200, "{}", 0); dm_telemetry_tick(t + 601, 1611, 1); dm_telemetry_state(&q, 0, 0); CHECK(q == 1);   // 迟到回调不算 | a late callback does not count
    dm_telemetry_tick(t + 603, 1613, 1); CHECK(f_probes == 1); dm_telemetry_tick(t + 607, 1617, 1); CHECK(f_probes == 2);

    // ⑧ 蜂窝只发小包：性能窗口留到 Wi-Fi；当日 16 KiB；一批 ≤ 8 KiB、≤ 20 条 | metered: small packets only; perf windows wait for Wi-Fi; 16 KiB a day; a batch ≤ 8 KiB, ≤ 20 events
    wipe(); launch(0, 1000); S.launch_done = 1; dm_telemetry_enqueue(DM_TELE_PERF_WINDOW, 1000, E_("p")); dm_telemetry_enqueue(DM_TELE_SESSION_END, 1000, E_("s")); go_send(1, 1001, 1);
    CHECK(f_sends == 1 && strstr(f_body, "\"s\"") && !strstr(f_body, "\"p\"")); ok_reply(f_tok); dm_telemetry_tick(10, 1010, 1); dm_telemetry_state(&q, 0, 0); CHECK(q == 1 && S.q[0].kind == DM_TELE_PERF_WINDOW);
    dm_telemetry_tick(45, 1045, 1); CHECK(f_probes == 2); dm_telemetry_net(f_tok, 1); dm_telemetry_tick(46, 1046, 1); CHECK(f_sends == 1 && dm_telemetry_state(0, 0, 0) == DM_TE_IDLE);   // 蜂窝下没有可发的：不发 | nothing eligible on metered
    launch(0, 1100); S.launch_done = 1; go_send(1, 1101, 0); CHECK(f_sends == 2 && strstr(f_body, "\"p\""));   // Wi-Fi 下发 | goes on Wi-Fi
    wipe(); launch(0, 1000); S.launch_done = 1; for (int i = 0; i < 30; i++) dm_telemetry_enqueue(DM_TELE_SESSION_END, 1000, E_("s")); go_send(1, 1001, 0); { int n = 0; for (const char *c = f_body; (c = strstr(c, "\"s\"")); c++) n++; CHECK(n == BATCH_MAX_N && f_blen <= BATCH_MAX_BYTES); }
    wipe(); launch(0, 1000); S.launch_done = 1; { static char h2[3000]; memset(h2, 'x', sizeof h2); h2[0] = '{'; h2[sizeof h2 - 2] = '}'; h2[sizeof h2 - 1] = 0; for (int i = 0; i < 12; i++) dm_telemetry_enqueue(DM_TELE_SESSION_END, 1000, h2); } go_send(1, 1001, 0); CHECK(f_blen <= BATCH_MAX_BYTES && S.batch_n == 2);
    // 当日额度：8 次请求 = 4 批；蜂窝 16 KiB；过了 UTC+8 零点清零 | daily budget: 8 requests = 4 batches; metered 16 KiB; reset at UTC+8 midnight
    wipe(); launch(0, 1000); S.launch_done = 1; for (int i = 0; i < 6; i++) dm_telemetry_enqueue(DM_TELE_SESSION_END, 1000, E_("s")); S.day = (1000 + 8 * 3600) / 86400; S.day_reqs = 8; dm_telemetry_tick(1, 1001, 1); dm_telemetry_tick(8, 1008, 1); CHECK(f_probes == 0 && dm_telemetry_state(0, 0, 0) == DM_TE_IDLE);
    launch(0, 1000 + 86400); S.launch_done = 1; dm_telemetry_tick(1, 1001 + 86400, 1); dm_telemetry_tick(8, 1008 + 86400, 1); CHECK(f_probes == 1 && S.day_reqs == 0);
    wipe(); launch(0, 1000); S.launch_done = 1; dm_telemetry_enqueue(DM_TELE_SESSION_END, 1000, E_("s")); S.day = (1000 + 8 * 3600) / 86400; S.day_metered = DAY_MAX_METERED; go_send(1, 1001, 1); CHECK(f_sends == 0 && dm_telemetry_state(&q, 0, 0) == DM_TE_IDLE && q == 1);

    // ⑨ 失败与退避（PRD §2.6 ⑤）| failures and backoff
    wipe(); launch(0, 1000); t = go_send(1, 1001, 0); http(f_tok, 429, "{\"error\":\"rate_limited\"}", 30); dm_telemetry_tick(t, 1010, 1); CHECK(dm_telemetry_state(&q, 0, &at) == DM_TE_WAIT && q == 1 && at == 0 && S.next_wall == 1040);   // 429 / 30 秒：照等 | wait as told
    dm_telemetry_tick(t + 10, 1020, 1); CHECK(f_probes == 1); S.menu_since = 0; dm_telemetry_tick(t + 40, 1040, 1); dm_telemetry_tick(t + 46, 1046, 1); CHECK(f_probes == 2);
    dm_telemetry_net(f_tok, 0); dm_telemetry_tick(t + 46.1, 1046, 1); http(f_tok, 429, "{\"error\":\"rate_limited\"}", 900); dm_telemetry_tick(t + 47, 1047, 1); CHECK(dm_telemetry_state(0, 0, 0) == DM_TE_IDLE && S.next_wall == 1947);   // 429 / 900 秒：等下次启动 | wait for the next launch
    launch(0, 1100); dm_telemetry_tick(1, 1101, 1); dm_telemetry_tick(8, 1108, 1); CHECK(f_probes == 2);   // 下次启动还没到点：不发 | the next launch before the time: nothing
    launch(0, 1950); dm_telemetry_tick(1, 1951, 1); dm_telemetry_tick(8, 1958, 1); CHECK(f_probes == 3);
    // 503 + 暂停 Retry-After：一天 | 503 with a pause Retry-After: one day
    wipe(); launch(0, 1000); t = go_send(1, 1001, 0); http(f_tok, 503, "{\"error\":\"uploads_paused\"}", 86400); dm_telemetry_tick(t, 1010, 1); CHECK(dm_telemetry_state(&q, 0, 0) == DM_TE_IDLE && q == 1 && S.next_wall == 1010 + 86400);
    // 超过 7 天的 Retry-After 按 7 天 | a Retry-After beyond 7 days counts as 7 days
    wipe(); launch(0, 1000); t = go_send(1, 1001, 0); http(f_tok, 503, "{\"error\":\"x\"}", 30 * 86400); dm_telemetry_tick(t, 1010, 1); CHECK(S.next_wall == 1010 + MAX_RETRY_AFTER_S);
    // 网络错误：指数退避，±20%，超过 10 分钟留到下次启动；6 次后这次启动不再试 | network errors: exponential backoff, ±20%, beyond 10 minutes the next launch; after 6 no more this launch
    wipe(); launch(0, 1000); t = go_send(1, 1001, 0); dm_telemetry_result(f_tok, DM_DIAGUP_UNAVAILABLE, "offline", 0); dm_telemetry_tick(t, 1010, 1); CHECK(dm_telemetry_state(0, 0, &at) == DM_TE_WAIT && at == 1 && S.next_wall >= 1010 + 48 && S.next_wall <= 1010 + 72);
    S.attempts = 3; S.st = DM_TE_SEND; S.batch_n = 1; S.batch[0] = S.q[0].id; dm_telemetry_result(f_tok, DM_DIAGUP_UNCONFIRMED, "response_lost", 0); dm_telemetry_tick(t + 1, 1011, 1); CHECK(dm_telemetry_state(&q, 0, 0) == DM_TE_IDLE && q == 1 && S.next_wall >= 1011 + 1440 && S.next_wall <= 1011 + 2160);
    S.attempts = MAX_ATTEMPTS; S.st = DM_TE_SEND; dm_telemetry_result(f_tok, DM_DIAGUP_UNAVAILABLE, "x", 0); dm_telemetry_tick(t + 2, 1012, 1); CHECK(dm_telemetry_state(&q, 0, &at) == DM_TE_IDLE && q == 1 && at == 0 && S.next_wall == 0);
    // ⑪ P-000 回话 | P-000 answers
    {
      // C2：2xx 逐事件；按 event_index 不按位置；隔离、重复都出队并分别计数 | per event, by event_index, quarantined and duplicate leave the queue and are counted apart
      wipe(); launch(0, 1000); S.launch_done = 1; dm_telemetry_enqueue(DM_TELE_SESSION_END, 1000, "{\"event_id\":\"11111111111111111111111111111111\",\"t\":\"a\"}"); dm_telemetry_enqueue(DM_TELE_SESSION_END, 1000, "{\"event_id\":\"22222222222222222222222222222222\",\"t\":\"b\"}"); dm_telemetry_enqueue(DM_TELE_SESSION_END, 1000, "{\"event_id\":\"33333333333333333333333333333333\",\"t\":\"c\"}"); go_send(1, 1001, 0); CHECK(f_sends == 1 && S.batch_n == 3);
      http(f_tok, 200, "{\"accepted\":1,\"results\":[{\"event_index\":2,\"event_id\":\"33333333333333333333333333333333\",\"status\":\"duplicate\",\"duplicate\":true},{\"event_index\":0,\"event_id\":\"11111111111111111111111111111111\",\"status\":\"accepted\"},{\"event_index\":1,\"event_id\":\"22222222222222222222222222222222\",\"status\":\"quarantined\"}]}", 0); dm_telemetry_tick(10, 1010, 1); dm_telemetry_state(&q, 0, &at); CHECK(q == 0 && at == 0 && S.acked[0] == 1 && S.acked[1] == 1 && S.acked[2] == 1 && S.acked[3] == 0);
      // C2：回执缺失 / 越界 / ID 不符 / 状态未知 / 重复序号 / 少一项 / 坏 JSON：整批留队，按网络失败退避 | missing, out of range, wrong ID, unknown status, repeated index, one short, bad JSON: the batch stays, network backoff
      static const char *const bad[] = { "{}", "{\"accepted\":1}", "{\"duplicate\":true}", "{\"results\":[]}", "{\"results\":[{\"event_index\":1,\"event_id\":\"" EID "\",\"status\":\"accepted\"}]}", "{\"results\":[{\"event_index\":0,\"event_id\":\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\",\"status\":\"accepted\"}]}", "{\"results\":[{\"event_index\":0,\"event_id\":\"" EID "\",\"status\":\"ok\"}]}", "{\"results\":[{\"event_index\":0,\"event_id\":\"" EID "\",\"status\":\"accepted\"},{\"event_index\":0,\"event_id\":\"" EID "\",\"status\":\"accepted\"}]}", "{\"results\":[{\"event_id\":\"" EID "\",\"status\":\"accepted\"}]}", "{\"results\":[{\"event_index\":null,\"event_id\":\"" EID "\",\"status\":\"accepted\"}]}", "{\"results\":[", "not json", "" };
      for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) { wipe(); launch(0, 1000); S.launch_done = 1; dm_telemetry_enqueue(DM_TELE_SESSION_END, 1000, E_("s")); t = go_send(1, 1001, 0); http(f_tok, 200, bad[i], 0); dm_telemetry_tick(t, 1010, 1); dm_telemetry_state(&q, 0, &at); CHECK(q == 1 && at == 1 && S.next_wall >= 1010 + 48 && S.next_wall <= 1010 + 72 && dm_telemetry_dropped(DM_TELE_SESSION_END) == 0); }
      // C3：带完整 results 的 400：只丢 rejected，计拒收，不进 queue_dropped；其余有效 | a 400 with complete results: only rejected go, counted as refused, not in queue_dropped
      wipe(); launch(0, 1000); S.launch_done = 1; dm_telemetry_enqueue(DM_TELE_SESSION_END, 1000, "{\"event_id\":\"11111111111111111111111111111111\",\"t\":\"a\"}"); dm_telemetry_enqueue(DM_TELE_SESSION_END, 1000, "{\"event_id\":\"22222222222222222222222222222222\",\"t\":\"b\"}"); go_send(1, 1001, 0);
      http(f_tok, 400, "{\"results\":[{\"event_index\":0,\"event_id\":\"11111111111111111111111111111111\",\"status\":\"accepted\"},{\"event_index\":1,\"event_id\":\"22222222222222222222222222222222\",\"status\":\"rejected\",\"error\":\"invalid_field\",\"field\":\"settings.x\"}]}", 0); dm_telemetry_tick(10, 1010, 1); dm_telemetry_state(&q, 0, 0); CHECK(q == 0 && S.acked[0] == 1 && S.acked[3] == 1 && dm_telemetry_dropped(DM_TELE_SESSION_END) == 0 && !S.halt);
      // C3 / C4：没有可验证 results 的 400，以及 411 / 413 / 415 / 其他 4xx：留队，这次启动不再发 | a 400 without verifiable results, and 411 / 413 / 415 / other 4xx: stay queued, no more this launch
      static const int st4[] = { 400, 411, 413, 415, 404 };
      for (size_t i = 0; i < sizeof st4 / sizeof *st4; i++) { wipe(); launch(0, 1000); go_send(1, 1001, 0); http(f_tok, st4[i], "{\"error\":\"invalid_request\",\"event_index\":null,\"field\":\"payload\"}", 0); dm_telemetry_tick(10, 1010, 1); dm_telemetry_state(&q, 0, &at); CHECK(dm_telemetry_state(0, 0, 0) == DM_TE_IDLE && q == 1 && at == 0 && S.halt && S.err_streak == 1 && dm_telemetry_dropped(DM_TELE_LAUNCH) == 0); reset_counts(); dm_telemetry_enqueue(DM_TELE_SESSION_END, 1020, E_("s")); dm_telemetry_tick(100, 1100, 1); dm_telemetry_tick(110, 1110, 1); CHECK(f_probes == 0 && f_sends == 0); }
      // C4：下次启动再试一次；同类第二次就停，新事件照入队不发；换构建重开；成功清零 | one more try next launch; the second time stops, new events queue unsent; a new build reopens; success clears
      wipe(); launch(0, 1000); go_send(1, 1001, 0); http(f_tok, 400, "{}", 0); dm_telemetry_tick(10, 1010, 1); launch(0, 2000); CHECK(!S.halt && S.err_streak == 1); reset_counts(); S.launch_done = 1; go_send(1, 2001, 0); CHECK(f_sends == 1); http(f_tok, 400, "{}", 0); dm_telemetry_tick(10, 2010, 1); CHECK(S.err_streak == 2);
      launch(0, 3000); CHECK(S.halt); reset_counts(); dm_telemetry_tick(1, 3001, 1); dm_telemetry_state(&q, 0, 0); CHECK(q == 2); dm_telemetry_tick(8, 3008, 1); dm_telemetry_tick(16, 3016, 1); CHECK(f_probes == 0 && f_sends == 0);
      launch(0, 3100 + Q_MAX_AGE_S); dm_telemetry_state(&q, 0, 0); CHECK(q == 0 && S.halt);   // 7 天过期 | 7 days expire
      wipe(); launch(0, 1000); go_send(1, 1001, 0); http(f_tok, 400, "{}", 0); dm_telemetry_tick(10, 1010, 1); launch(0, 2000); S.launch_done = 1; go_send(1, 2001, 0); http(f_tok, 400, "{}", 0); dm_telemetry_tick(10, 2010, 1); { const char *keep = f_dev.build; f_dev.build = "29"; launch(0, 3000); CHECK(!S.halt && S.err_streak == 0); reset_counts(); S.launch_done = 1; go_send(1, 3001, 0); CHECK(f_sends == 1); ok_reply(f_tok); dm_telemetry_tick(10, 3010, 1); CHECK(S.qn == 0); f_dev.build = keep; }
      wipe(); launch(0, 1000); go_send(1, 1001, 0); http(f_tok, 400, "{}", 0); dm_telemetry_tick(10, 1010, 1); launch(0, 2000); S.launch_done = 1; go_send(1, 2001, 0); ok_reply(f_tok); dm_telemetry_tick(10, 2010, 1); CHECK(S.err_streak == 0 && S.err_status == 0);
      // C5：403（任何错误码）：留队，按网络失败退避，不原地重取挑战；下一次重新取 | 403: stays, network backoff, no fresh challenge in place; the next try fetches one
      static const char *const e403[] = { "{\"error\":\"challenge_rejected\"}", "{\"error\":\"proof_rejected\"}", "{}" };
      for (size_t i = 0; i < 3; i++) { wipe(); launch(0, 1000); t = go_send(1, 1001, 0); reset_counts(); http(f_tok, 403, e403[i], 0); dm_telemetry_tick(t, 1010, 1); dm_telemetry_state(&q, 0, &at); CHECK(q == 1 && at == 1 && f_chals == 0 && f_sends == 0 && dm_telemetry_state(0, 0, 0) == DM_TE_WAIT && S.next_wall >= 1058 && S.next_wall <= 1082 && !S.halt); dm_telemetry_tick(t + 100, 1200, 1); dm_telemetry_tick(t + 106, 1206, 1); CHECK(f_probes == 1); dm_telemetry_net(f_tok, 0); dm_telemetry_tick(t + 107, 1207, 1); CHECK(f_chals == 1 && f_sends == 1 && at == 1); }
      // 取挑战就 403 / 503：不提交 | 403 / 503 at the challenge: nothing submitted
      wipe(); launch(0, 1000); f_ch_status = 403; f_ch_reply = "{\"error\":\"x\"}"; t = go_send(1, 1001, 0); dm_telemetry_state(&q, 0, &at); CHECK(f_chals == 1 && f_sends == 0 && q == 1 && at == 1 && S.next_wall >= 1007 + 48); f_ch_reply = CH_ALL;
      wipe(); launch(0, 1000); f_ch_status = 503; f_ch_manual = 1; t = go_send(1, 1001, 0); http(f_tok, 503, "{\"error\":\"uploads_paused\",\"paused\":true}", 3600); dm_telemetry_tick(t, 1010, 1); dm_telemetry_state(&q, 0, &at); CHECK(f_sends == 0 && q == 1 && at == 0 && S.next_wall == 4610 && dm_telemetry_state(0, 0, 0) == DM_TE_IDLE); f_ch_manual = 0; f_ch_status = 200;
      // C6：409：整批丢弃、记冲突、不进 queue_dropped，不重试 | 409: the batch goes, conflict counted, not in queue_dropped, no retry
      wipe(); launch(0, 1000); go_send(1, 1001, 0); reset_counts(); http(f_tok, 409, "{\"error\":\"event_conflict\",\"event_index\":0,\"field\":\"event_id\"}", 0); dm_telemetry_tick(10, 1010, 1); dm_telemetry_state(&q, 0, 0); CHECK(dm_telemetry_state(0, 0, 0) == DM_TE_IDLE && q == 0 && S.conflicts == 1 && dm_telemetry_dropped(DM_TELE_LAUNCH) == 0); dm_telemetry_tick(100, 1100, 1); CHECK(f_probes == 0);
      // 带着计数的启动事件被 409：计数还回来 | a launch event carrying counts hits 409: the counts come back
      wipe(); S.dropped[DM_TELE_SESSION_END] = 0; launch(0, 1000); dm_telemetry_enqueue(DM_TELE_SESSION_END, 1000, E_("s")); S.dropped[DM_TELE_PERF_WINDOW] = 2; dm_telemetry_tick(1, 1001, 1); CHECK(S.q[S.qn - 1].kind == DM_TELE_LAUNCH && S.dropped[DM_TELE_PERF_WINDOW] == 0); dm_telemetry_tick(8, 1008, 1); dm_telemetry_net(f_tok, 0); dm_telemetry_tick(8.1, 1008, 1); http(f_tok, 409, "{}", 0); dm_telemetry_tick(9, 1009, 1); CHECK(S.dropped[DM_TELE_PERF_WINDOW] == 2);
      // C7：缺 capabilities = 只有 launch；空数组 = 不发；未知忽略；坏类型 = 无效回话 | no capabilities = launch only; empty = nothing; unknown ignored; a bad type is an invalid answer
      wipe(); launch(0, 1000); S.launch_done = 1; dm_telemetry_enqueue(DM_TELE_SESSION_END, 1000, E_("s")); dm_telemetry_enqueue(DM_TELE_LAUNCH, 1000, E_("l")); f_ch_reply = "{\"challenge\":\"" CH43 "\",\"expires_in\":120}"; go_send(1, 1001, 0); CHECK(f_sends == 1 && strstr(f_body, "\"l\"") && !strstr(f_body, "\"s\"") && !strcmp(f_sent_ch, CH43)); ok_reply(f_tok); dm_telemetry_tick(10, 1010, 1); dm_telemetry_state(&q, 0, 0); CHECK(q == 1 && S.q[0].kind == DM_TELE_SESSION_END);   // 没宣告的留队 | unannounced stay queued
      wipe(); launch(0, 1000); f_ch_reply = "{\"challenge\":\"" CH43 "\",\"capabilities\":[]}"; go_send(1, 1001, 0); dm_telemetry_state(&q, 0, &at); CHECK(f_chals == 1 && f_sends == 0 && q == 1 && at == 0 && dm_telemetry_state(0, 0, 0) == DM_TE_IDLE);
      wipe(); launch(0, 1000); f_ch_reply = "{\"challenge\":\"" CH43 "\",\"capabilities\":[\"telemetry-future-v9\",\"telemetry-launch-v1\"]}"; go_send(1, 1001, 0); CHECK(f_sends == 1);
      wipe(); launch(0, 1000); S.launch_done = 1; dm_telemetry_enqueue(DM_TELE_SESSION_END, 1000, E_("s")); f_ch_reply = "{\"challenge\":\"" CH43 "\",\"capabilities\":[\"telemetry-launch-v1\"]}"; go_send(1, 1001, 0); dm_telemetry_state(&q, 0, &at); CHECK(f_sends == 0 && q == 1 && at == 0 && dm_telemetry_state(0, 0, 0) == DM_TE_IDLE);
      static const char *const badch[] = { "{\"challenge\":\"" CH43 "\",\"capabilities\":\"x\"}", "{\"challenge\":\"short\"}", "{\"challenge\":\"" "!AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA" "\"}", "{\"capabilities\":[]}", "{" };
      for (size_t i = 0; i < sizeof badch / sizeof *badch; i++) { wipe(); launch(0, 1000); f_ch_reply = badch[i]; go_send(1, 1001, 0); dm_telemetry_state(&q, 0, &at); CHECK(f_sends == 0 && q == 1 && at == 1 && S.next_wall >= 1007 + 48); }
      f_ch_reply = CH_ALL;
      // 额度：挑战 + 提交各算一次 | daily count: the challenge and the submit are one each
      wipe(); launch(0, 1000); go_send(1, 1001, 0); CHECK(S.day_reqs == 2);
      // C1：RC 版本原样；21 字符才退回 0 | an RC version goes as it is; 21 characters fall back to 0
      wipe(); launch(0, 1000); rand_hex32(S.session); { dm_tele_device_t keep = f_dev; f_dev.app_version = "1.0.0-rc1.2"; CHECK(dm_telemetry_build_launch(1, 1000, j, sizeof j) && strstr(j, "\"app_version\":\"1.0.0-rc1.2\"")); f_dev.app_version = "1.0.0+build.7"; CHECK(dm_telemetry_build_launch(1, 1000, j, sizeof j) && strstr(j, "\"app_version\":\"1.0.0+build.7\"")); f_dev.app_version = "12345678901234567890"; CHECK(dm_telemetry_build_launch(1, 1000, j, sizeof j) && strstr(j, "\"app_version\":\"12345678901234567890\"")); f_dev.app_version = "123456789012345678901"; CHECK(dm_telemetry_build_launch(1, 1000, j, sizeof j) && strstr(j, "\"app_version\":\"0\""));
      // C8：短 ANDROID_ID 左补零转小写；取不到才不生成；iOS 的 UUID 不动 | a short ANDROID_ID is padded and lower-cased; only none generates nothing; iOS UUIDs untouched
      f_dev = keep; f_dev.platform = "android"; f_dev.channel = "android_apk"; f_dev.device_code = "ABC123"; CHECK(dm_telemetry_build_launch(1, 1000, j, sizeof j) && strstr(j, "\"device_code\":\"0000000000abc123\""));
      f_dev.device_code = "0123456789ABCDEF"; CHECK(dm_telemetry_build_launch(1, 1000, j, sizeof j) && strstr(j, "\"device_code\":\"0123456789abcdef\"")); f_dev.device_code = ""; CHECK(!dm_telemetry_build_launch(1, 1000, j, sizeof j)); f_dev.device_code = "not hex"; CHECK(!dm_telemetry_build_launch(1, 1000, j, sizeof j)); f_dev.device_code = "0123456789abcdef";
      // C9：安卓机型逗号与别的字符换空格，裁到 40；什么都不剩才 unknown；iOS 逗号不动 | Android model: comma and others become spaces, cut to 40; only nothing left is unknown; iOS keeps its comma
      f_dev.model = "AYN Thor,Pro/2"; CHECK(dm_telemetry_build_launch(1, 1000, j, sizeof j) && strstr(j, "\"model\":\"AYN Thor Pro 2\""));
      f_dev.model = "1234567890123456789012345678901234567890XYZ"; CHECK(dm_telemetry_build_launch(1, 1000, j, sizeof j) && strstr(j, "\"model\":\"1234567890123456789012345678901234567890\""));
      f_dev.model = "\xe5\x8d\x8e\xe4\xb8\xba,,,"; CHECK(dm_telemetry_build_launch(1, 1000, j, sizeof j) && strstr(j, "\"model\":\"unknown\"")); f_dev.model = NULL; CHECK(dm_telemetry_build_launch(1, 1000, j, sizeof j) && strstr(j, "\"model\":\"unknown\""));
      // C10：安卓超分与 iCloud 固定无能力值；iOS 不变 | Android upscaling and iCloud are fixed no-capability values; iOS unchanged
      f_dev.model = "AYN Thor"; CHECK(dm_telemetry_build_launch(1, 1000, j, sizeof j) && strstr(j, "\"upscale_selected\":\"off\"") && strstr(j, "\"upscale_effective\":\"off\"") && strstr(j, "\"icloud\":0"));
      f_dev = keep; CHECK(dm_telemetry_build_launch(1, 1000, j, sizeof j) && strstr(j, "\"upscale_selected\":\"quality\"") && strstr(j, "\"icloud\":1") && strstr(j, "\"model\":\"iPhone18,3\"")); }
      // 回执太长截断 = 坏 JSON = 无效 | a truncated answer is bad JSON, invalid
      wipe(); launch(0, 1000); t = go_send(1, 1001, 0); { static char longr[REPLY_MAX + 100]; memset(longr, ' ', sizeof longr); longr[0] = '{'; longr[sizeof longr - 2] = '}'; longr[sizeof longr - 1] = 0; http(f_tok, 200, longr, 0); } dm_telemetry_tick(t, 1010, 1); dm_telemetry_state(&q, 0, &at); CHECK(q == 1 && at == 1); }
    // 外壳一直没回话：看门狗 | the shell never answers: watchdog
    wipe(); launch(0, 1000); go_send(1, 1001, 0); dm_telemetry_tick(200, 1200, 1); CHECK(f_cancels == 1 && dm_telemetry_state(0, 0, 0) == DM_TE_WAIT);
    // 还有存货：隔 30 秒发下一批，不集中爆发 | more queued: the next batch after 30 s, no burst
    wipe(); launch(0, 1000); S.launch_done = 1; for (int i = 0; i < 25; i++) dm_telemetry_enqueue(DM_TELE_SESSION_END, 1000, E_("s")); t = go_send(1, 1001, 0); ok_reply(f_tok); dm_telemetry_tick(t, 1010, 1); dm_telemetry_state(&q, 0, 0); CHECK(q == 5 && S.next_mono == t + NEXT_BATCH_S); dm_telemetry_tick(t + 10, 1020, 1); CHECK(f_probes == 1); dm_telemetry_tick(t + 31, 1041, 1); CHECK(f_probes == 2);

    // ⑩ 告知框：没答过 + 有地址 + 非测试局才到期 | the notice: due only when unanswered, with an origin, not a test run
    f_notice = 0; CHECK(dm_telemetry_notice_due()); f_cfg = 0; CHECK(!dm_telemetry_notice_due()); f_cfg = 1; f_test = "DM_KEYS"; CHECK(!dm_telemetry_notice_due()); setenv("DM_TEST_TELEMETRY_NOTICE", "1", 1); CHECK(dm_telemetry_notice_due()); unsetenv("DM_TEST_TELEMETRY_NOTICE"); f_test = NULL; f_notice = 1; CHECK(!dm_telemetry_notice_due());
    wipe(); char cmd[300]; snprintf(cmd, sizeof cmd, "rm -rf %s", g_dir); (void)!system(cmd);
    printf("t_telemetry: %d checks, %d failed\n", checks, fails); return fails ? 1 : 0;
}
