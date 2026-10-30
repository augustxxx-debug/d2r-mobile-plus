// 暂停菜单「分享诊断日志」里的「上传诊断报告」：玩家点「分享诊断日志」→ 游戏内提示框（上传 / 导出到本地 / 取消），确认后才收集、脱敏、签名、上传（协议 docs/DIAGNOSTICS.zh-CN.md）。
// 这里是公共部分：状态机（游戏主线程）+ 提示框（布局 touch_layout.py _diagbox，挂在暂停菜单面板末尾）。收集、网络、签名在外壳（iOS dm_sharelog.m + dm_diag_upload.swift；Mac 宿主 diagup_mac.m），一律在后台线程。
//   只有收到并校验过服务端回执才显示「已上传」+ 报告编号（服务端给的 report_id）；超时 / 断线 / 响应丢失 = 「结果未确认」；维护、限流、过大、离线、服务端错误各有自己的状态，全都不自动重试，由玩家点「重试」。
//   重试沿用同一任务（外壳留着同一 upload_id、同一载荷，换新挑战重签）；新任务 = 新报告。每次尝试一个 token，结果按 token 认，旧尝试的回调不会改写新任务。
//   没配上传地址（构建没注入）或测试开关 DM_TEST_SHARELOG_CLOSE：点「分享诊断日志」照旧直接导出（原链路不变）。
// "Upload diagnostic report" inside the pause menu's Share Diagnostics: the player taps Share Diagnostics → in-game box (Upload / Export locally / Cancel); only after confirming are logs collected, scrubbed, signed and uploaded (protocol: docs/DIAGNOSTICS.md).
// This is the shared part: the state machine (game main thread) + the box (layout touch_layout.py _diagbox, appended to the pause menu panel). Collection, network and signing live in the shells (iOS dm_sharelog.m + dm_diag_upload.swift; Mac host diagup_mac.m), always off the game thread.
//   "Uploaded" + the report number (the server's report_id) only after a verified server receipt; timeout / disconnect / lost response = "result unconfirmed"; maintenance, rate limits, oversize, offline and server errors each have their own state, none retried automatically - the player presses Retry.
//   A retry stays in the same job (the shell keeps the same upload_id and payload and re-signs with a fresh challenge); a new job = a new report. Each attempt has a token and results are matched by token, so a stale attempt's callback never rewrites a newer job.
//   No upload origin (none injected by the build) or the test switch DM_TEST_SHARELOG_CLOSE: Share Diagnostics exports directly as before (original path unchanged).
#pragma once
#include <stddef.h>

enum {   // 与 dm_diag_upload.swift DmDiagOutcome 一致 | kept in step with DmDiagOutcome in dm_diag_upload.swift
    DM_DIAGUP_CANCELLED = 0, DM_DIAGUP_OK = 1, DM_DIAGUP_UNCONFIRMED = 2, DM_DIAGUP_UNAVAILABLE = 3, DM_DIAGUP_MAINTENANCE = 4, DM_DIAGUP_RATE_LIMITED = 5,
    DM_DIAGUP_TOO_LARGE = 6, DM_DIAGUP_REJECTED = 7, DM_DIAGUP_CONFLICT = 8, DM_DIAGUP_PREPARE = 9, DM_DIAGUP_CONFIG = 10,
};
#define DM_DIAGUP_F_DUPLICATE 1   // 回执 duplicate = true（重试命中服务端已存的那份） | receipt duplicate = true
#define DM_DIAGUP_F_MAYBE 2       // 这份报告此前有请求结果不明，可能已送达 | an earlier request of this report had no definite answer

typedef struct {
    int (*configured)(void);                      // 有合法上传地址 | a valid upload origin is configured
    void (*start)(unsigned token, unsigned job);  // 后台开一次尝试（立即返回）；job 与上次相同 = 重试。结束时调 dm_diagup_result（任意线程）一次 | start one attempt in the background (returns at once); the same job as before = retry
    void (*cancel)(void);                         // 取消进行中的尝试（看门狗用）；可为空 | cancel the running attempt (watchdog); may be NULL
    void (*export_local)(void);                   // 原「分享诊断日志」导出（iOS 系统分享面板 / Mac 存储面板） | the original Share Diagnostics export
    int (*copy)(const char *text);                // 剪贴板，返回 1 成功 | clipboard, returns 1 on success
} dm_diagup_shell_t;

void dm_diagup_init(const dm_diagup_shell_t *shell);   // 外壳启动时登记一次 | registered once by the shell at launch
int dm_diagup_open(int from);                          // 玩家点了「分享诊断日志」（游戏主线程）：1 = 提示框接手；0 = 没配上传，调用方照旧导出 | 1 = the box takes it; 0 = no upload configured, the caller exports as before
void dm_diagup_result(unsigned token, int outcome, const char *detail, long long expires_at, int flags, int retry_after, int retention_days);   // 外壳报结果（任意线程） | the shell reports a result (any thread)
void dm_diagup_on_uimsg(const char *cat, const char *act, const char *arg);   // 界面消息（diag.c）：提示框各按钮 | UI messages (diag.c): the box's buttons
void dm_diagup_pump(void);                             // 取手柄处每帧调（hle_hid.c） | every frame at the controller poll (hle_hid.c)
int dm_diagup_shown(void);                             // 提示框显示着 | the box is showing
// 弹窗吞输入（同社区入口 dm_community.c，用户 RC1.1 判过的阻塞项）：开着时触屏的抬起只给框里的按钮，其余吞掉；手柄键与两根摇杆全不交给游戏，关框后按着的东西松开前也不交 | modal input (as the community box): while up, a touch lift only reaches the box's buttons and everything else is swallowed; controller keys and both sticks never reach the game, not even until released after the box closes
int dm_diagup_modal(void);
int dm_diagup_hit_answer(void);
void dm_diagup_ui_tap(unsigned long long root, int x, int y);
void dm_diagup_input_gate(unsigned long long *bt, int *lx, int *ly, int *rx, int *ry);
void dm_diagup_input_leak(int leaked);
// 「自动上报崩溃」第一次打开时的确认框（选项 → 自定义，dm_custom.c）：同一个框的另一页。答复调 cb(1 同意 / 0 不开启)；构建没配地址时只说明、只有「关闭」，答 0 | the first-enable consent box for automatic crash reports: another page of the same box. Answers through cb(1 agree / 0 no); without an origin it only explains, Close only, answers 0
void dm_diagup_consent_open(void (*cb)(int yes));

// ---- 单测用（t_diagup.c） | for unit tests ----
enum { DM_DIAGUP_ST_IDLE, DM_DIAGUP_ST_CONFIRM, DM_DIAGUP_ST_RUNNING, DM_DIAGUP_ST_RESULT };
typedef struct {
    const char *title, *line0, *line1, *sub;   // 表键（ui_text.tsv）；line0 / line1 带一个 %s / %d 时 arg 填进去 | table keys; arg fills a %s / %d in line0 / line1
    char arg0[96], arg1[96];
    int upload, retry, exp, copy, cancel, close, enable;   // 哪几颗按钮露出来 | which buttons show
} dm_diagup_view_t;
void dm_diagup_tick(double now);                       // 收结果、看门狗、按键动作（pump 里调；单测直接调） | take results, watchdog, button actions
void dm_diagup_view(double now, int pad, dm_diagup_view_t *v);
int dm_diagup_state(unsigned *job, unsigned *token, int *outcome, char *detail, size_t n);
