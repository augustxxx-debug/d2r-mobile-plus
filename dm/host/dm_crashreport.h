// 自动上报崩溃（RC2，缺省关）：玩家在「选项 → 自定义」开了「自动上报崩溃」并在游戏内确认框里同意后，下次启动若崩溃围栏说上一局异常退出，就把上一局的脱敏日志、系统崩溃诊断（iOS MetricKit / 安卓 ApplicationExitInfo）、围栏状态、版本、机型与 GPU 打成一份报告，按诊断上传协议（docs/DIAGNOSTICS.zh-CN.md）挑战签名上传。
// 这里是公共部分：是否上报、何时上报、失败重试到什么时候为止、结果提示；只在游戏主线程跑（取手柄处每帧 dm_crashreport_pump），收集 / 脱敏 / 签名 / 网络全在外壳的后台线程，绝不阻塞启动。
//   开关关着（或没同意、没配地址、测试局没带 DM_TEST_CRASHREPORT）：不收集、不联网，手上的待传报告一并删掉。
//   重试有上限：一次启动最多 3 次（启动 20 秒后第一次，再隔 60 秒、300 秒，服务端要求更久就按它，超过 10 分钟留到下次启动）、最多跨 3 次启动、总共 6 次、报告存放不超过 7 天；到了上限就放弃。服务端明确拒收（其他 4xx、upload_conflict、过大）不重试。
//   网络（看板 PRD §2.6，用户 2026-10-30）：蜂窝 / 低数据模式 / 计量网络只发摘要版（日志头与关键读数，≤ 16 KiB，第一行 (automatic, summary)），完整版（第一行 (automatic, full) summary_of=<摘要的 upload_id>）只在 Wi-Fi 发；判断在外壳。设置里没有另外的开关。
//   时机：只在主菜单 / 选人页、连续 5 秒没进局时发；局内零请求。到点时已在局内就先把报告打好存盘（上一局日志下次启动就被覆盖），回主菜单再发。后台通道留给后续 P-编号。
//   报告在盘上：<设置目录>/dm_crash_report.payload（已脱敏的载荷原文，玩家可以直接打开看）+ .id（upload_id）由外壳写；.state（次数与时间）这里写；重试沿用同一 upload_id 与同一载荷（服务端幂等）。结果提示写 .note，下次到选角页显示一行（dm_update.c TouchUpdNote），显示 20 秒。
// Automatic crash reports (RC2, off by default): once the player has turned on "Auto-report crashes" under Options → Custom and agreed in the in-game box, a launch where the crash fence says the previous run exited abnormally packs that run's scrubbed log, the system crash diagnostic (iOS MetricKit / Android ApplicationExitInfo), the fence state, version, model and GPU into one report and uploads it with the challenge-signed diagnostic protocol (docs/DIAGNOSTICS.md).
// This is the shared part: whether and when to report, when retries stop, the result notice; it runs only on the game main thread (dm_crashreport_pump at the controller poll); collection / scrubbing / signing / network all live on the shells' background threads, so startup is never blocked.
//   Switch off (or no consent, no origin, or a test run without DM_TEST_CRASHREPORT): nothing is collected or sent, and any pending report is deleted.
//   Bounded retries: at most 3 per launch (first 20 s after launch, then 60 s and 300 s later, longer if the server asks, beyond 10 minutes left to the next launch), at most 3 launches, 6 attempts overall, 7 days of storage; then it gives up. An explicit refusal (other 4xx, upload_conflict, too large) is not retried.
//   Network (dashboard PRD 2.6): metered networks (cellular, Low Data Mode) only get the summary (log head and key readings, at most 16 KiB, first line (automatic, summary)); the full report (first line (automatic, full) summary_of=<the summary's upload_id>) only goes on Wi-Fi; the shells decide. There is no further switch in Settings.
//   Timing: only on the main menu / character select after 5 s without entering a game; zero requests in a game. If already in a game when due, the report is packed to disk first (the previous log is overwritten at the next launch) and sent back on the menu. A background channel is left to a later P- number.
//   The report on disk: <settings dir>/dm_crash_report.payload (the scrubbed payload itself, the player can open it) + .id (upload_id), written by the shell; .state (counts and times) written here; a retry keeps the same upload_id and payload (idempotent on the server). The result goes to .note and shows as one line on the character-select page at the next visit (dm_update.c TouchUpdNote) for 20 s.
#pragma once
#include <stddef.h>

typedef struct {
    int (*configured)(void);   // 有合法上传地址 | a valid upload origin is configured
    // 后台开一次尝试（立即返回）。fence = 围栏状态一行（英文键值，没有用户内容）；dir = 报告所在目录。结束时调 dm_crashreport_result（任意线程）一次 | start one attempt in the background (returns at once); calls dm_crashreport_result once when done (any thread)
    void (*start)(unsigned token, const char *fence, const char *dir);
    void (*cancel)(void);      // 取消进行中的尝试（看门狗、玩家关开关）；可为空 | cancel the running attempt (watchdog, the player turning it off); may be NULL
    void (*prepare)(const char *fence, const char *dir);   // 总开关暂停时：只把报告打好存盘（上一局日志下次启动就被覆盖），不联网；可为空 | while the server switch pauses uploads: pack the report to disk only (the previous log is overwritten at the next launch), no network; may be NULL
    int checks_switch;         // 1 = 外壳每次尝试前自己现查总开关（安卓：普通启动不拉清单）；0 = 这里按清单缓存拦 | 1 = the shell checks the switch itself before each attempt (Android: a normal start fetches no manifest); 0 = held back here from the manifest cache
} dm_crashreport_shell_t;

void dm_crashreport_init(const dm_crashreport_shell_t *shell);   // 外壳启动时登记一次 | registered once by the shell at launch
int dm_crashreport_configured(void);                             // 这个构建能不能上报（确认框据此说明） | whether this build can report at all (the consent box says so)
void dm_crashreport_pump(void);                                  // 取手柄处每帧调（hle_hid.c），第一次调时做启动判定 | every frame at the controller poll; the first call makes the launch decision
void dm_crashreport_result(unsigned token, int outcome, const char *detail, int flags, int retry_after);   // 外壳报结果（任意线程）；outcome 同 DM_DIAGUP_* | the shell reports a result (any thread); outcome as DM_DIAGUP_*
void dm_crashreport_setting_changed(int on);                     // 玩家点了「应用」改了开关：关 = 取消并删掉待传报告；开 = 下次启动起作用 | Apply changed the switch: off = cancel and delete the pending report; on = effective from the next launch
// 服务端总开关（更新清单 telemetry.enabled，缺省 true，可带 pause_until / reason；用户 2026-10-30）：false 时本次零上传，报告留盘但受 7 天上限约束，不弹提示；恢复 true 后接着发。只能让客户端少发、不能多发，所以读未签名清单可以接受。玩家的开关仍是前提。
//   清单拉不到 = 沿用上次缓存（<设置目录>/dm_telemetry.txt）。上传接口回 503 + Retry-After 同样当暂停：不占重试次数，隔至少 300 秒再探，一次启动最多探 3 次。
// Server switch (manifest telemetry.enabled, default true, optional pause_until / reason): false = no uploads this launch, the report stays on disk within the 7-day limit, no notice; true again resumes. It can only make clients send less, never more, so reading it from an unsigned manifest is acceptable. The player's switch remains the precondition.
//   Manifest not fetched = the cached value is kept. A 503 + Retry-After from the upload endpoint counts as a pause too: no retry budget spent, probed again after at least 300 s, at most 3 probes per launch.
void dm_crashreport_switch(int present, int enabled, long long pause_until, const char *reason);   // 拉到合法清单（任意线程）：present = 0 没有 telemetry 字段（= 开） | a valid manifest arrived (any thread): present = 0 means no telemetry field (= on)
void dm_crashreport_switch_no_fetch(void);   // 这次拉不到（任意线程）：沿用缓存，不再等 | not fetched this time (any thread): keep the cache, stop waiting
int dm_crashreport_switch_paused(long long wall);   // 此刻是否被总开关暂停 | whether the server switch pauses uploads right now
int dm_crashreport_switch_seen(void);   // 这次启动已拉过清单（成败都算） | this launch's manifest fetch finished (either way)
const char *dm_crashreport_note(void);                           // 选角页那一行的表键（dmCrashReportSent / dmCrashReportFailed），没有返回 NULL；第一次取到开始计 20 秒 | table key for the select-page line, NULL if none; the first call starts the 20 s
const char *dm_crashreport_note_arg(void);                       // 那一行的参数（报告编号前 8 位） | its argument (first 8 characters of the report number)

// ---- 单测用（t_crashreport.c） | for unit tests ----
enum { DM_CR_OFF, DM_CR_WAIT, DM_CR_RUNNING, DM_CR_IDLE };   // OFF = 这次启动不动作；IDLE = 这次启动不再试、报告留到下次 | OFF = nothing this launch; IDLE = no more attempts this launch, the report waits for the next
void dm_crashreport_boot(double now, long long wall);
void dm_crashreport_tick(double now, long long wall);
int dm_crashreport_state(int *attempts, int *launches);
