// 崩溃围栏（发布前闸 1）：进程启动写「正在运行」标记、正常结束才清；下次启动看到残留 = 上一局异常退出，连续两次就关掉全部实验性功能并在主菜单提示一次。
// 标记文件 dm_running.txt 与设置文件 sw_custom.txt 同目录（iOS 是 Documents），内容一行「streak=<本局开始时已连续异常退出的次数>」。
// Crash fence (release gate 1): on process start write a "running" marker, cleared only on a normal end; a leftover marker at the next start = the previous run exited abnormally, and two in a row turn off every experimental feature with a one-time notice on the main menu.
// The marker file dm_running.txt sits in the same directory as the settings file sw_custom.txt (Documents on iOS); it holds one line "streak=<consecutive abnormal exits seen when this run started>".
#pragma once
int dm_crashfence_start(void);              // 宿主入口调一次：读残留、算连续次数、写标记；连续 ≥ 2 关实验性功能（dm_custom_safe_mode）。返回本局开始时的连续次数 | called once at host entry: read the leftover, count, write the marker; >= 2 in a row turns off experimental features. Returns the streak at this start
void dm_crashfence_clean(const char *why);  // 正常结束（优雅退出、iOS 进后台）：删标记，连续次数清零 | normal end (graceful exit, iOS going to background): delete the marker, the streak resets to zero
void dm_crashfence_background(void);      // iOS 进后台：画面还在动就清标记；已 5 秒没出图 = 卡死，标记留着 | iOS going to the background: clear the marker if the picture still moves; no present for 5 s = frozen, the marker stays
void dm_crashfence_note_memory(void);     // 系统内存告警：标记里记下（之后在前台没了算内存不足） | system memory warning: noted in the marker (vanishing in the foreground afterwards counts as out of memory)
void dm_crashfence_resume(void);            // iOS 回前台：重写标记（之后再异常退出照样记） | iOS back to the foreground: write the marker again (a later abnormal exit counts as usual)
int dm_crashfence_notice_take(void);        // 取一次：1 = 本次启动关过实验性功能，该提示玩家 | take once: 1 = this launch turned off experimental features and the player should be told
void dm_crashfence_main_menu(void);         // 主菜单出来时调（diag.c 的界面消息）：要提示就挂上游戏内提示框 | called when the main menu appears (UI message in diag.c): queue the in-game notice box if needed
void dm_crashfence_on_uimsg(const char *cat, const char *act, const char *arg);   // 界面消息（diag.c）：提示框「确定」 | UI messages (diag.c): the notice box's OK
const char *dm_crashfence_test_env(void);   // 本局是不是测试局：认出的那个变量名（只进日志），不是返回 NULL；测试开关据此只在测试局读 | is this a test run: the variable name that identified it (log only), NULL if not; test switches use it to be read only in test runs
void dm_crashfence_pump(void);              // 取手柄处每帧调（hle_hid.c）：写字、摆位置、接管手柄 A、收起 | every frame at the controller poll (hle_hid.c): text, placement, controller A, dismissal
