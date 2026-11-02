// 设备与使用统计（RC2 M0，看板 PRD §2.0–§2.7，缺省关）：玩家在告知框里同意（dm_telebox.c）或之后在「选项 → 自定义」打开后才记录；不同意 / 关闭 = 不收集、零请求。
// 这里是公共部分：本机队列、组包、发送时机与退避、服务端总开关，只在游戏主线程跑（取手柄处每帧 dm_telemetry_pump）。签名与网络在外壳后台线程（iOS / Mac dm_diag_upload.swift，安卓 TelemetryClient），与崩溃上报同一套传输层，身份分开。
//   队列：<设置目录>/dm_tele_queue.tsv，64 条 / 128 KiB / 7 天，超限按 性能窗口 → 会话摘要 → 启动事件 丢最旧，丢的条数进下一条启动事件的 queue_dropped。崩溃报告走自己的单份队列，不占这里。
//   时机：局内零请求、零压缩；只在主菜单 / 选人页连续 5 秒后发；请求飞着时进局就取消。M0 只有前台闲时通道（一批 ≤ 8 KiB），后台通道留后续。
//   网络：蜂窝 / 低数据模式 / 计量只发启动事件与会话摘要，当日 ≤ 16 KiB；性能窗口等 Wi-Fi。每安装每日 ≤ 8 次请求 / 64 KiB（UTC+8 日界）。
//   回话（P-000，docs/TELEMETRY_PROTOCOL.md）：先取挑战看 capabilities 再组批；2xx 与带完整 results 的 400 逐事件出队（rejected 丢弃计数），回执无效按网络失败；409 整批丢弃计数。
//   失败：网络、408、403、5xx 按 1 分 / 5 分 / 30 分 / 2 时 / 12 时退避（±20%），一次启动内最多 6 次；429 / 503 照 Retry-After（> 10 分钟等下次启动）；其余 4xx 留队、这次启动不再发，下次启动再试一次，连续两次同类就停发整条通道，满 7 天或换构建再试（到期口径与队列过期同一个 >，恰好七天仍停；解除时队列里的事件直接接着发）。
//   总开关：与崩溃上报共用（dm_crashreport_switch_*，清单 telemetry 字段 + 缓存）；暂停时事件照常入队、零上传、不提示，恢复后补发。只能少发不能多发。
//   关闭：清队列、状态与本机上报身份，seq 重开从 1 起。日志前缀「统计上报：」，不写地址、设备码与签名材料。
// Device and usage statistics (RC2 M0, dashboard PRD 2.0-2.7, off by default): recorded only after the player agrees in the notice (dm_telebox.c) or turns it on later under Options → Custom; declined / off = nothing collected, zero requests.
// This is the shared part: local queue, event building, send timing and backoff, the server switch; game main thread only (dm_telemetry_pump at the controller poll). Signing and network live on the shells' background threads (iOS / Mac dm_diag_upload.swift, Android TelemetryClient): the same transport as crash reports, a separate identity.
//   Queue: <settings dir>/dm_tele_queue.tsv, 64 entries / 128 KiB / 7 days; over the limit the oldest go first in the order perf window → session end → launch, counted in the next launch event's queue_dropped. Crash reports have their own single slot and do not use this quota.
//   Timing: zero requests and no compression in a game; only after 5 s on the main menu / character select; a request in flight is cancelled on entering a game. M0 has only the foreground idle channel (a batch ≤ 8 KiB); the background channel comes later.
//   Network: cellular / Low Data Mode / metered only sends launch and session-end events, ≤ 16 KiB a day; perf windows wait for Wi-Fi. Per install ≤ 8 requests / 64 KiB a day (UTC+8 day boundary).
//   Answers (P-000, docs/TELEMETRY_PROTOCOL.md): challenge first, capabilities decide the batch; 2xx and a 400 with complete results leave the queue event by event (rejected are dropped and counted), an invalid receipt counts as a network failure; 409 drops the batch and counts it.
//   Failures: network, 408, 403, 5xx back off 1 min / 5 min / 30 min / 2 h / 12 h (±20%), at most 6 per launch; 429 / 503 follow Retry-After (> 10 min waits for the next launch); other 4xx stay queued, no more this launch, one more try next launch, twice the same stops the whole channel for 7 days or until a new build (expiry uses the same > as the queue, so exactly seven days still counts as stopped; when it lifts, what is queued goes out by itself).
//   Server switch: shared with crash reports (dm_crashreport_switch_*, manifest telemetry field + cache); while paused events still queue, nothing is uploaded, no notice; resumes afterwards. It can only reduce sending.
//   Off: clears the queue, the state and the local reporting identity; seq restarts from 1. Log prefix "统计上报："; never the origin, the device code or signing material.
#pragma once
#include <stddef.h>

typedef enum { DM_TELE_LAUNCH, DM_TELE_SESSION_END, DM_TELE_PERF_WINDOW, DM_TELE_KINDS } dm_tele_kind;

// 外壳给的设备信息（都是出厂 / 构建信息，不含设备名与用户名）；空串 = 没有。这里还会按白名单再验一遍 | device facts from the shell (factory / build facts, no device or user name); empty = none; validated against whitelists again here
typedef struct {
    const char *platform;         // ios / android / mac
    const char *app_version, *build, *source_revision, *channel, *pack_source;
    const char *model, *soc, *gpu_driver, *os_version, *locale;
    const char *device_code;      // IDFV / ANDROID_ID（不足 16 位这里补零）；Mac 宿主是本机随机标识 | IDFV / ANDROID_ID (zero-padded here when short); a random local id on the Mac host
    const char *font;             // lxgw / other
} dm_tele_device_t;

typedef struct {
    int (*configured)(void);                             // 这个构建配了服务地址 | the build has a service origin
    void (*device)(dm_tele_device_t *d);                 // 游戏主线程，立即返回；字符串活到下一次调用 | game main thread, returns at once; strings live until the next call
    void (*net_probe)(unsigned token);                   // 后台看网络类型（需要时先现拉清单）；结束调 dm_telemetry_net | look at the network type in the background (fetching the manifest first when needed); ends with dm_telemetry_net
    void (*challenge)(unsigned token);                   // 后台取挑战；有 HTTP 回话调 dm_telemetry_http，没有调 dm_telemetry_result | fetch a challenge in the background; an HTTP answer ends with dm_telemetry_http, none with dm_telemetry_result
    void (*send)(unsigned token, const char *challenge, const char *body, size_t n);   // 后台签名提交一批（字符串外壳自己拷，每次新 upload_id）；结束同上 | sign and submit one batch in the background (the shell copies the strings, a new upload_id each time); ends as above
    void (*cancel)(void);                                // 取消进行中的请求；可为空 | cancel what is running; may be NULL
    void (*forget_identity)(void);                       // 删掉本机上报身份（玩家关了开关）；可为空 | delete the local reporting identity (the player turned it off); may be NULL
    int fetches_manifest;                                // 1 = net_probe 里现拉清单（安卓普通启动不拉）；0 = 等启动时已有的清单流程 | 1 = net_probe fetches the manifest itself (Android fetches none at a normal start); 0 = wait for the manifest flow that already runs at launch
} dm_telemetry_shell_t;

void dm_telemetry_init(const dm_telemetry_shell_t *shell);   // 外壳启动时登记一次 | registered once by the shell at launch
int dm_telemetry_configured(void);
void dm_telemetry_pump(void);                                // 取手柄处每帧调（hle_hid.c） | every frame at the controller poll
void dm_telemetry_net(unsigned token, int metered);          // 外壳报网络类型（任意线程） | the shell reports the network type (any thread)
void dm_telemetry_http(unsigned token, int status, const char *body, size_t n, int retry_after);   // 外壳报 HTTP 回话原样（任意线程），归类都在这里 | the shell reports an HTTP answer as it is (any thread); all classification is here
void dm_telemetry_result(unsigned token, int outcome, const char *detail, int retry_after);   // 外壳报没有 HTTP 回话的结果（任意线程）：取消 / 网络 / 结果不明 / 准备失败，outcome 同 DM_DIAGUP_* | the shell reports an end without an HTTP answer (any thread): cancelled / network / unconfirmed / preparation; outcome as DM_DIAGUP_*
void dm_telemetry_setting_changed(int on);                   // 开关变了（告知框答复或选项页「应用」，游戏主线程）：关 = 取消、清队列与身份 | the switch changed (notice answer or Apply, game main thread): off = cancel, clear queue and identity
int dm_telemetry_notice_due(void);                           // 该弹告知框：还没答过、构建配了地址、不是测试局 | the notice is due: not answered yet, the build has an origin, not a test run
// 给 M1 / M2 预留的入队接口（只在闲时调：切后台 / 退出 / 回主菜单）；json 是一条完整事件。M0 只有启动事件走它 | the enqueue entry for M1 / M2 (idle moments only: background / exit / back on the menu); json is one complete event. In M0 only launch events use it
int dm_telemetry_enqueue(dm_tele_kind kind, long long wall, const char *json);

// ---- 单测用（t_telemetry.c） | for unit tests ----
enum { DM_TE_OFF, DM_TE_IDLE, DM_TE_WAIT, DM_TE_PROBE, DM_TE_CHAL, DM_TE_SEND };
void dm_telemetry_boot(double now, long long wall);
void dm_telemetry_tick(double now, long long wall, int on_menu);
int dm_telemetry_state(int *queued, size_t *bytes, int *attempts);
int dm_telemetry_dropped(dm_tele_kind kind);
int dm_telemetry_build_launch(double now, long long wall, char *out, size_t n);
