// 暂停菜单「分享诊断日志」的 iOS 侧。
// 按钮是游戏自己的控件（布局 tools/touch_layout.py 的 _pause_no_help：借原版「消息记录」按钮，换字、改发 PanelManager:ClosePanel:TouchShareLog），
// 宿主收到这条空操作消息后调这里登记的回调（dm/host/diag.c）。这里做两件：
//   ① 打一个文件：头部（构建号、机型、系统版本、温控 / 电量 / 内存 / 存储、本局与上一局的关键性能读数）+ 本局 boot.txt + 上一局 boot.previous.txt（main.m 每次启动前改名留下）；
//   ② 弹系统分享面板（UIActivityViewController），玩家自己选发到哪；iPad / Mac 上分享面板是弹出框，必须给锚点，否则一弹就崩。
// 隐私：只放这两份日志（游戏与宿主自己写的）和上面那几项机器读数；不取设备名、不取任何账号 / 位置 / 通讯录类信息。
// 测试开关 DM_TEST_SHARELOG_CLOSE=<秒>：分享面板照常弹出，过这么多秒自动收起；导出的文件另拷一份到 Documents/sharelog_test.txt 供回归核内容。
// iOS side of the pause menu's 「分享诊断日志」(Share Diagnostics).
// The button is the game's own widget (layout: _pause_no_help in tools/touch_layout.py: borrows the original game's 「消息记录」(Message Log) button, changes the text, and makes it send PanelManager:ClosePanel:TouchShareLog instead),
// and on receiving this no-op message the host calls the callback registered here (dm/host/diag.c). Two things are done here:
//   ① Build one file: header (build number, device model, OS version, thermal / battery / memory / storage, key performance readings of this run and the previous run) + this run's boot.txt + the previous run's boot.previous.txt (left by main.m's rename before each launch);
//   ② Present the system share sheet (UIActivityViewController), and the player chooses where to send it; on iPad / Mac the share sheet is a popover and must be given an anchor, otherwise it crashes as soon as it is presented.
// Privacy: export redacts home paths, application IDs, emails, container/device identifiers and save character names. Only these two logs (written by the game and the host themselves) and the machine readings listed above are included; no device name, no account / location / contacts information of any kind.
// Test switch DM_TEST_SHARELOG_CLOSE=<seconds>: the share sheet is presented as usual and dismissed automatically after that many seconds; the exported file is also copied to Documents/sharelog_test.txt so regression can check its content.
#import <UIKit/UIKit.h>
#include <stdatomic.h>
#include <sys/sysctl.h>
#include "../../host/dm_i18n.h"
#include "../../host/dm_custom.h"
#include "../../host/dm_diagup.h"
#include "../../host/dm_crashreport.h"
#import <Metal/Metal.h>
#import "dm_diagnostic_scrub.h"
#import "dm_diagnostic_read.h"
#import "dm_crashlog.h"
#include "../../gfx/nvn/nvn_mtl.h"
float dm_3d_scale_cur(void);
void plog(const char *fmt, ...);
void dm_sharelog_init(void (*cb)(void));   // dm/host/diag.c

// 日志里最后一条含 key 的行（不含行尾换行）；没有返回 nil
// The last line in the log containing key (without the trailing newline); returns nil if none
static NSString *last_line(NSData *d, const char *key) {
    if (!d.length) return nil;
    NSData *k = [NSData dataWithBytes:key length:strlen(key)];
    NSRange r = [d rangeOfData:k options:NSDataSearchBackwards range:NSMakeRange(0, d.length)]; if (r.location == NSNotFound) return nil;
    const char *b = d.bytes; NSUInteger s = r.location, e = r.location;
    while (s > 0 && b[s - 1] != '\n') s--;
    while (e < d.length && b[e] != '\n') e++;
    return [[NSString alloc] initWithBytes:b + s length:e - s encoding:NSUTF8StringEncoding] ?: @"（这一行不是合法 UTF-8）";
}
static void perf_block(NSMutableString *h, NSString *title, NSData *d) {
    [h appendFormat:@"\n—— %@ ——\n", title];
    if (!d) { [h appendString:@"（没有这份日志）\n"]; return; }
    static const char *keys[] = { "boot: 版本", "崩溃围栏：", "存档管理：", "诊断上传：", "自动上报：", "[诊断]", "[perf]", "[nvn] 上屏", "温控档位", "紫闪探针 至出图", "MetalFX spatial support", "MetalFX spatial ready", "MetalFX initialization unavailable", "图形设置：", "帧率档：生效", "帧率档：出图", "超分档位：", "内存档位：", "内存档位预留：", "光追阴影：机器判定", "光追阴影：生效", "光追阴影：回退", "光追阴影：画面", "光追阴影：第", "光追点光：光源记录", "光追阴影：建好", "光追阴影：没进原因", "光追阴影：局部回退 至第", "光追阴影：主光", "光追阴影：阴影图 第", "光追镂空：第", "光追镂空：判定库", "光追降档：", "光追 AO：", "光追质量：", "光追降分辨率：", "光追阴影：GPU", "自定义设置：读盘", "输入模式：", "社区入口：清单", "社区入口：已显示", "社区入口：复制", "Metal 抓帧层：", "光追内存：预算", "光追内存：第", "光追内存：告警", "内存 至帧", "【内存告警】", "游戏难度：", "线程同步：", "加载页：" };
    for (size_t i = 0; i < sizeof keys / sizeof *keys; i++) { NSString *l = last_line(d, keys[i]); [h appendFormat:@"%@\n", l ?: [NSString stringWithFormat:@"（没有「%@」行）", @(keys[i])]]; }
}
// 日志里的路径会带系统用户主目录（Mac 上是 /Users/<用户名>，iPhone 上是 /var/mobile）：导出时一律换成 ~，不把用户名带出去
// Paths in the log carry the system user's home directory (/Users/<username> on Mac, /var/mobile on iPhone): on export always replace it with ~ so the username does not leak out
static NSDate *g_t0;   // 本次启动时刻（dm_sharelog_boot） | Time of this launch (dm_sharelog_boot)
// 打包整份（已脱敏）：导出用每份日志 2 MiB、崩溃日志 256 KiB；上传按服务端上限给更小的值（diag_collect）。原日志文件不动
// Build the whole text (scrubbed): the export uses 2 MiB per log and 256 KiB of crash log; the upload passes smaller values from the server limit (diag_collect). Original log files are untouched
static NSData *build_data(NSUInteger log_limit, NSUInteger crash_limit, NSUInteger *cur_len, NSData **prev_out) {
    NSFileManager *fm = NSFileManager.defaultManager;
    NSString *doc = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES)[0];
    unsigned long long cur_omitted = 0, prev_omitted = 0;
    NSData *cur = dm_diagnostic_read_tail([doc stringByAppendingPathComponent:@"boot.txt"],log_limit,&cur_omitted);
    NSData *prev = dm_diagnostic_read_tail([doc stringByAppendingPathComponent:@"boot.previous.txt"],log_limit,&prev_omitted);
    NSDictionary *inf = NSBundle.mainBundle.infoDictionary; NSString *build = [inf[@"CFBundleVersion"] description] ?: @"未知";
    NSDateFormatter *df = [NSDateFormatter new]; df.dateFormat = @"yyyy-MM-dd HH:mm:ss ZZZ";
    char hw[64] = "未知"; size_t hn = sizeof hw; sysctlbyname("hw.machine", hw, &hn, NULL, 0);
    __block float battery_level = -1; __block int bs = 0;
    void (^battery_snapshot)(void) = ^{
        UIDevice *dev = UIDevice.currentDevice; dev.batteryMonitoringEnabled = YES;
        battery_level = dev.batteryLevel; bs = (int)dev.batteryState;
    };
    if (NSThread.isMainThread) battery_snapshot();
    else dispatch_sync(dispatch_get_main_queue(),battery_snapshot);
    NSArray<NSString *> *bst = @[ @"未知", @"放电", @"充电中", @"已充满" ];
    NSDictionary *fsa = [fm attributesOfFileSystemForPath:doc error:nil]; NSProcessInfo *pi = NSProcessInfo.processInfo;
    NSMutableString *h = [NSMutableString string];
    [h appendFormat:@"D2R Mobile Diagnostics\n导出时间 %@\n版本 %@ 构建 %@\n显示版本 %@\n机型 %s 系统 %@\n", [df stringFromDate:NSDate.date], [inf[@"CFBundleShortVersionString"] description] ?: @"未知", build, [inf[@"DMVersionLabel"] description].length ? [inf[@"DMVersionLabel"] description] : @"未知", hw, pi.operatingSystemVersionString];
    [h appendFormat:@"温控档位 %ld 低电量模式 %@ 电量 %.0f%% %@\n", (long)pi.thermalState, pi.lowPowerModeEnabled ? @"开" : @"关", battery_level < 0 ? -1.0 : battery_level * 100, bs >= 0 && bs <= 3 ? bst[bs] : @"未知"];   // 中文一律走 %@：NSString 的 %s 不按 UTF-8 解 | Chinese always goes through %@: NSString's %s does not decode as UTF-8
    // 崩溃围栏：MetricKit 送来的最新一份系统崩溃诊断（main.m 存的 Documents/dm_crash_last.json），摘要一行、全文附在末尾 | crash fence: the newest system crash diagnostic delivered by MetricKit (Documents/dm_crash_last.json saved by main.m), one summary line, full text attached at the end
    NSString *crash_path = [doc stringByAppendingPathComponent:@"dm_crash_last.json"], *crash_sum = nil;
    NSData *crash = dm_crashlog_section([fm fileExistsAtPath:crash_path] ? crash_path : nil, crash_limit, &crash_sum);
    [h appendFormat:@"%@\n", crash_sum];
    [h appendFormat:@"物理内存 %.1f GB 可用存储 %.1f GB 本次已运行 %.0f 分钟\n", pi.physicalMemory / 1073741824.0, [fsa[NSFileSystemFreeSize] doubleValue] / 1073741824.0, -g_t0.timeIntervalSinceNow / 60];
    mtl_metalfx_status graphics = {0}; mtl_metalfx_snapshot(&graphics);
    static const char *const profiles[] = { "Auto", "Eco", "Balanced", "Quality", "Original" };
    static const char *const fallback[] = { "none-or-not-attempted", "unsupported-device", "initialization-failed", "encode-failed" };
    int selected = dm_custom_get("q3d"), effective = graphics.profile;
    [h appendFormat:@"\n—— Plus graphics (applied values; no pending UI changes) ——\nMetalFX enabled=%d selected=%s effective=%s lastEligiblePassActive=%d supported=%d (-1=not-probed) fallback=%s\n", dm_custom_get("upscale"), selected>=0 && selected<4 ? profiles[selected] : "unknown", effective>=0 && effective<5 ? profiles[effective] : "unknown", graphics.active, graphics.supported, graphics.fallback>=0 && graphics.fallback<4 ? fallback[graphics.fallback] : "unknown"];
    // 帧率档与超分档同一行看出联动：120 帧档不给高画质，设置是高画质时生效按均衡（dm_custom.c q3d_for_fps，用户 2026-10-29） | frame rate and upscale profile on one line: no Quality at 120 fps, a saved Quality runs as Balanced (dm_custom.c q3d_for_fps, user 2026-10-29)
    int fps_now = dm_fps_setting();
    [h appendFormat:@"FPS target=%d profileSelected=%s profileEffective=%s qualityCapped120=%d renderScale=%.3f lastUpscaleInput=%ux%u lastUpscaleOutput=%ux%u\n", fps_now, selected>=0 && selected<4 ? profiles[selected] : "unknown", effective>=0 && effective<5 ? profiles[effective] : "unknown", fps_now == 120 && selected == 3 && effective == 2, dm_3d_scale_cur(), graphics.input_width, graphics.input_height, graphics.output_width, graphics.output_height];
    // 内存档位：启动时生效的档（渲染分辨率 / 超分档在启动时定预留，局中调高要下次启动）与当前设置值并列（dm_custom.c dm_mem_boot_info，用户 2026-10-29）
    // Memory tier: the launch values (render resolution / profile fix the reserve at launch; raising mid-run needs a restart) next to the current settings (dm_custom.c dm_mem_boot_info, user 2026-10-29)
    { int tier = 2, lim = -1, phys = -1, r0 = -1, q0 = -1; dm_mem_boot_info(&tier, &lim, &phys, &r0, &q0); int up = dm_custom_get("upscale"), qn = dm_custom_get("q3d");
      [h appendFormat:@"Memory tier=%d limitMB=%d physMB=%d model=%s | atLaunch renderRes=%d profile=%s | now renderRes=%d profile=%s\n", tier, lim, phys, dm_mem_model(), r0,
          q0 >= 0 && q0 < 5 ? profiles[q0] : "unknown", dm_render_res_level(), up ? (qn >= 0 && qn < 4 ? profiles[qn] : "unknown") : profiles[4]]; }
    [h appendString:@"Enhanced preferences (applied):"];
    static const char *const custom[] = { "auto_mana", "merc_pot", "drop_light", "map_mark", "item_notes", "icloud", "pot_pick", "drop_haptic", "gem_pick", "store_btn", "loot_filter", "auto_buff", "touch_swap_above", "free_tp", "auto_crash" };
    for (unsigned i=0;i<sizeof custom/sizeof *custom;i++) [h appendFormat:@" %s=%d",custom[i],dm_custom_get(custom[i])];
    [h appendString:@"\n"];
    [h appendFormat:@"Log export: at most %lu bytes per log; omitted prefix bytes current=%llu previous=%llu. Original log files are unchanged.\n", (unsigned long)log_limit,cur_omitted,prev_omitted];
    perf_block(h, @"本局关键读数（各取最后一条）", cur);
    perf_block(h, @"上一局关键读数（各取最后一条）", prev);
    NSMutableData *out = [[h dataUsingEncoding:NSUTF8StringEncoding] mutableCopy];
    void (^part)(NSString *, NSData *) = ^(NSString *t, NSData *d) {
        [out appendData:[[NSString stringWithFormat:@"\n\n===== %@（%@）=====\n", t, d ? [NSString stringWithFormat:@"%lu 字节", (unsigned long)d.length] : @"没有这份日志"] dataUsingEncoding:NSUTF8StringEncoding]];
        if (d) [out appendData:d]; };
    part(@"本局日志 boot.txt", cur); part(@"上一局日志 boot.previous.txt", prev);
    [out appendData:crash];   // 末尾：系统崩溃日志（随整份一起脱敏） | at the end: the system crash log (scrubbed together with the whole file)
    // Tail truncation can remove the path that originally identified a name.
    // Read directory names only (never save contents), so recent standalone
    // character-name messages are still redacted from the exported copy.
    NSMutableArray<NSString *> *character_names = [NSMutableArray array];
    for (NSString *root in @[@"save",@"save.work",@"save.test",@"save.test.work"]) {
        NSString *directory = [doc stringByAppendingPathComponent:root];
        for (NSString *name in [fm contentsOfDirectoryAtPath:directory error:nil]) {
            NSString *save = [[directory stringByAppendingPathComponent:name]
                stringByAppendingPathComponent:[name stringByAppendingPathExtension:@"d2s"]];
            if ([fm fileExistsAtPath:save]) [character_names addObject:name];
        }
    }
    if (cur_len) *cur_len = cur.length;
    if (prev_out) *prev_out = prev;
    return dm_diagnostic_scrub_names(out,NSHomeDirectory(),NSBundle.mainBundle.bundleIdentifier,character_names);
}
static NSURL *build_file(void) {
    NSFileManager *fm = NSFileManager.defaultManager;
    NSString *doc = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES)[0];
    NSUInteger cur_len = 0; NSData *prev = nil;
    NSData *fin = build_data(2 * 1024 * 1024, 256 * 1024, &cur_len, &prev);
    NSString *build = [NSBundle.mainBundle.infoDictionary[@"CFBundleVersion"] description] ?: @"未知";
    NSDateFormatter *df = [NSDateFormatter new]; df.dateFormat = @"yyyyMMdd_HHmmss";
    NSString *dir = [NSTemporaryDirectory() stringByAppendingPathComponent:@"sharelog"];   // 只放这一份导出：每次重建这个目录（我们自己的，不碰别的） | Hold only this one export: rebuild this directory every time (it is our own; nothing else is touched)
    [fm removeItemAtPath:dir error:nil]; [fm createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
    NSString *p = [dir stringByAppendingPathComponent:[NSString stringWithFormat:@(dm_ui("dmShareLogFile")), build, [df stringFromDate:NSDate.date]]];
    if (![fin writeToFile:p atomically:YES]) { plog("诊断日志：【红】导出文件写不进 %s", p.UTF8String); return nil; }
    plog("诊断日志：已打包 %s（%lu 字节：本局 %lu、上一局 %s）", p.lastPathComponent.UTF8String, (unsigned long)fin.length, (unsigned long)cur_len,
         prev ? [NSString stringWithFormat:@"%lu", (unsigned long)prev.length].UTF8String : "没有");
    if (getenv("DM_TEST_SHARELOG_CLOSE")) [fin writeToFile:[doc stringByAppendingPathComponent:@"sharelog_test.txt"] atomically:YES];
    return [NSURL fileURLWithPath:p];
}
static UIViewController *top_vc(void) {   // 同 front_ios.m 系统输入框找宿主的顺序：主窗口 → 前台活跃场景的窗口 → 任一窗口，再顺着已弹出的往上找 | Same order as front_ios.m's system input box uses to find its host: main window → a window of the foreground active scene → any window, then walk up through what is already presented
    UIViewController *vc = nil, *alt = nil, *any = nil;
    for (UIScene *s in UIApplication.sharedApplication.connectedScenes)
        if ([s isKindOfClass:UIWindowScene.class]) for (UIWindow *w in ((UIWindowScene *)s).windows) {
            if (!w.rootViewController) continue;
            if (w.isKeyWindow) vc = w.rootViewController;
            else if (s.activationState == UISceneActivationStateForegroundActive && !alt) alt = w.rootViewController;
            else if (!any) any = w.rootViewController; }
    vc = vc ?: alt ?: any; while (vc.presentedViewController) vc = vc.presentedViewController;
    return vc;
}
static _Atomic int g_busy;   // 打包 / 分享面板开着时再点不叠第二个 | Tapping again while packaging / the share sheet is open does not stack a second one
static void on_click(void) {   // 宿主在游戏主线程调：玩家点了「分享诊断日志」 | Called by the host on the game main thread: the player tapped 「分享诊断日志」(Share Diagnostics)
    if (atomic_exchange(&g_busy, 1)) { plog("诊断日志：分享面板还开着，这次点击不再弹"); return; }
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        NSURL *u = build_file();
        dispatch_async(dispatch_get_main_queue(), ^{
            UIViewController *vc = u ? top_vc() : nil;
            if (!vc) { if (u) plog("诊断日志：【红】没找到可弹分享面板的界面"); atomic_store(&g_busy, 0); return; }
            UIActivityViewController *a = [[UIActivityViewController alloc] initWithActivityItems:@[u] applicationActivities:nil];
            a.completionWithItemsHandler = ^(UIActivityType t, BOOL done, NSArray *items, NSError *e) { (void)items;
                plog("诊断日志：分享面板收起（%s%s%s）", done ? "已发到 " : "没发", done ? (t ?: @"未知").UTF8String : "", e ? [NSString stringWithFormat:@"，错误 %@", e.localizedDescription].UTF8String : "");
                atomic_store(&g_busy, 0); };
            UIPopoverPresentationController *pp = a.popoverPresentationController;   // iPad / Mac 上是弹出框：锚在画面正中（暂停菜单所在处），不画箭头 | On iPad / Mac it is a popover: anchored at the center of the picture (where the pause menu is), no arrow drawn
            if (pp) { pp.sourceView = vc.view; pp.sourceRect = CGRectMake(CGRectGetMidX(vc.view.bounds), CGRectGetMidY(vc.view.bounds), 1, 1); pp.permittedArrowDirections = 0; }
            [vc presentViewController:a animated:YES completion:^{
                plog("诊断日志：分享面板已弹出（%s，%s）", pp ? "弹出框" : "底部面板", NSStringFromCGRect(a.view.frame).UTF8String);
                const char *tc = getenv("DM_TEST_SHARELOG_CLOSE"); if (!tc) return;
                dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(atof(tc) * 1e9)), dispatch_get_main_queue(), ^{
                    [a dismissViewControllerAnimated:NO completion:^{ plog("诊断日志：测试开关 DM_TEST_SHARELOG_CLOSE 到点收起分享面板"); atomic_store(&g_busy, 0); }]; });
            }];
        });
    });
}
// ---- 上传诊断报告的外壳（状态机与提示框 dm/host/dm_diagup.c，网络与签名 dm_diag_upload.swift） | shell of the diagnostic upload (state machine and box: dm/host/dm_diagup.c; network and signing: dm_diag_upload.swift) ----
// 地址只来自构建注入的 Info.plist DMDiagnosticUploadOrigin（出包时 DM_DIAGNOSTIC_ORIGIN，源码里没有）；空或不合格 = 不开上传，「分享诊断日志」照旧直接导出。地址不写日志
// The origin comes only from the build-injected Info.plist key DMDiagnosticUploadOrigin (DM_DIAGNOSTIC_ORIGIN at packaging, never in the source); empty or invalid = no upload, Share Diagnostics exports as before. The origin is never logged
void dm_diagup_net_start(unsigned token, unsigned job, const char *origin, const char *meta, const char *key_path, char *(*collect)(long, void *), void *ctx,
                         void (*done)(unsigned, int, const char *, long long, int, int, int));   // dm_diag_upload.swift
void dm_diagup_net_cancel(void);
int dm_diagup_net_origin_ok(const char *origin);
static NSString *diag_origin(void) { NSString *o = [NSBundle.mainBundle.infoDictionary[@"DMDiagnosticUploadOrigin"] description]; return o.length && dm_diagup_net_origin_ok(o.UTF8String) ? o : nil; }
static int diag_configured(void) { return diag_origin() != nil; }
// 外壳的 Swift 任务在后台线程调：按每份日志上限 limit 打一整份（崩溃日志给一半），过 dm_diagnostic_scrub_names 再过上传专用的 dm_diagnostic_scrub_upload；交回 malloc 的 C 串（NUL 换成 ?） | called on the Swift task's background thread
static char *diag_collect(long limit, void *ctx) { (void)ctx;
    @autoreleasepool {
        NSUInteger lim = limit > 0 ? (NSUInteger)limit : 0; NSData *d = dm_diagnostic_scrub_upload(build_data(lim, MIN((NSUInteger)256 * 1024, lim / 2), NULL, NULL)); if (!d) return NULL;
        char *p = malloc(d.length + 1); if (!p) return NULL; memcpy(p, d.bytes, d.length); p[d.length] = 0;
        for (NSUInteger i = 0; i < d.length; i++) if (!p[i]) p[i] = '?';
        plog("诊断上传：已收集并脱敏 %lu 字节（每份日志上限 %lu）", (unsigned long)d.length, (unsigned long)lim);
        return p;
    }
}
static void diag_done(unsigned token, int outcome, const char *detail, long long expires_at, int flags, int retry_after, int retention_days) {
    void dm_diagup_result(unsigned, int, const char *, long long, int, int, int); dm_diagup_result(token, outcome, detail, expires_at, flags, retry_after, retention_days);
}
static const char *diag_locale(void) { static const char *const g[] = { "en", "zh-Hans", "es-ES", "es-MX", "ru", "ko" }; int k = dm_lang_group(); return k >= 0 && k < 6 ? g[k] : "en"; }   // 游戏界面语言（DM_LG_*） | the game's UI language
static NSString *diag_meta(void) {
    NSDictionary *inf = NSBundle.mainBundle.infoDictionary; NSOperatingSystemVersion os = NSProcessInfo.processInfo.operatingSystemVersion;
    char hw[64] = "unknown"; size_t hn = sizeof hw; sysctlbyname("hw.machine", hw, &hn, NULL, 0);
#if TARGET_OS_SIMULATOR
    snprintf(hw, sizeof hw, "simulator");
#endif
    // 元数据只有这六项：版本、构建号、源码提交、硬件型号（不是设备名）、系统版本、游戏语言；不合格的值由 Swift 换成缺省 | exactly six metadata fields; invalid values are replaced by Swift
    return [NSString stringWithFormat:@"app_version=%@\nbuild=%@\nsource_revision=%@\nmodel=%s\nos_version=%ld.%ld.%ld\nlocale=%s",
        [inf[@"CFBundleShortVersionString"] description] ?: @"", [inf[@"CFBundleVersion"] description] ?: @"", [inf[@"DMSourceCommit"] description] ?: @"",
        hw, (long)os.majorVersion, (long)os.minorVersion, (long)os.patchVersion, diag_locale()];
}
static void diag_start(unsigned token, unsigned job) {   // 游戏主线程调，立即返回 | called on the game main thread, returns at once
    NSString *meta = diag_meta(); NSString *o = diag_origin();
    if (!o) { diag_done(token, 10, "origin", 0, 0, 0, 0); return; }
    dm_diagup_net_start(token, job, o.UTF8String, meta.UTF8String, NULL, diag_collect, NULL, diag_done);   // 私钥在钥匙串（key_path 空） | the private key is in the Keychain (key_path NULL)
}
static int diag_copy(const char *s) {   // 同 dm_update.m ios_copy：到主线程写 UIPasteboard | as ios_copy in dm_update.m
    NSString *t = @(s ?: ""); __block int ok = 0;
    void (^w)(void) = ^{ UIPasteboard.generalPasteboard.string = t; ok = [UIPasteboard.generalPasteboard.string isEqualToString:t]; };
    if (NSThread.isMainThread) w(); else dispatch_sync(dispatch_get_main_queue(), w);
    return ok;
}
// ---- 自动上报崩溃的外壳（策略与次数 dm/host/dm_crashreport.c，网络与盘上报告 dm_diag_upload.swift） | shell of the automatic crash report (policy and counts: dm_crashreport.c; network and the report on disk: dm_diag_upload.swift) ----
// 内容 = 报告头（自动上报标记 + 围栏状态一行 + GPU）+ 整份 build_data（版本 / 机型 / 设置摘要 / 日志尾部 / MetricKit 诊断），每份日志最多 128 KiB；整份过 dm_diagnostic_scrub_names 与 dm_diagnostic_scrub_upload
// Contents = report head (automatic marker + fence line + GPU) + the whole build_data (version / model / settings summary / log tails / MetricKit diagnostic), at most 128 KiB per log; the whole text passes dm_diagnostic_scrub_names and dm_diagnostic_scrub_upload
void dm_crashreport_net_start(unsigned token, const char *origin, const char *meta, const char *key_path, const char *dir, char *(*collect)(long, void *), void *ctx,
                              void (*done)(unsigned, int, const char *, long long, int, int, int));   // dm_diag_upload.swift
void dm_crashreport_net_cancel(void);
static char *crash_collect(long limit, void *ctx) {
    @autoreleasepool {
        NSUInteger lim = limit > 0 ? MIN((NSUInteger)limit, (NSUInteger)128 * 1024) : 0; const char *fence = ctx;
        id<MTLDevice> gpu = MTLCreateSystemDefaultDevice();
        NSString *head = [NSString stringWithFormat:@"D2R Mobile Plus crash report (automatic)\n%s\ngpu %@ family_apple9=%d\n\n", fence ? fence : "fence unknown", gpu.name ?: @"unknown", gpu ? [gpu supportsFamily:MTLGPUFamilyApple9] : 0];
        NSMutableData *all = [[head dataUsingEncoding:NSUTF8StringEncoding] mutableCopy];
        NSData *body = build_data(lim, MIN((NSUInteger)128 * 1024, lim), NULL, NULL); if (body) [all appendData:body];
        __block NSString *dev = nil; void (^name)(void) = ^{ dev = UIDevice.currentDevice.name; }; if (NSThread.isMainThread) name(); else dispatch_sync(dispatch_get_main_queue(), name);
        NSData *d = dm_diagnostic_scrub_upload(dm_diagnostic_scrub_literals(all, dev && ![@[@"iPhone", @"iPad", @"iPod touch"] containsObject:dev] ? @[dev] : @[])); if (!d) return NULL;
        char *p = malloc(d.length + 1); if (!p) return NULL; memcpy(p, d.bytes, d.length); p[d.length] = 0;
        for (NSUInteger i = 0; i < d.length; i++) if (!p[i]) p[i] = '?';
        plog("自动上报：已收集并脱敏 %lu 字节（每份日志上限 %lu）", (unsigned long)d.length, (unsigned long)lim);
        return p;
    }
}
static void crash_done(unsigned token, int outcome, const char *detail, long long expires_at, int flags, int retry_after, int retention_days) { (void)expires_at; (void)retention_days; dm_crashreport_result(token, outcome, detail, flags, retry_after); }
static void crash_start(unsigned token, const char *fence, const char *dir) {   // 游戏主线程调，立即返回 | called on the game main thread, returns at once
    NSString *o = diag_origin(); if (!o) { crash_done(token, 10, "origin", 0, 0, 0, 0); return; }
    static char fence_copy[160]; snprintf(fence_copy, sizeof fence_copy, "%s", fence ? fence : "");   // 一次只有一个尝试（dm_crashreport.c 保证），放静态区 | one attempt at a time
    dm_crashreport_net_start(token, o.UTF8String, diag_meta().UTF8String, NULL, dir, crash_collect, fence_copy, crash_done);
}
static const dm_crashreport_shell_t crash_shell = { diag_configured, crash_start, dm_crashreport_net_cancel };
static const dm_diagup_shell_t diag_shell = { diag_configured, diag_start, dm_diagup_net_cancel, on_click, diag_copy };   // 「导出到本地」= 原分享面板 | Export locally = the original share sheet
void dm_sharelog_boot(void) { g_t0 = NSDate.date; dm_sharelog_init(on_click); dm_diagup_init(&diag_shell); dm_crashreport_init(&crash_shell); plog("诊断上传：%s", diag_configured() ? "已配置上传地址（分享诊断日志先开上传提示框）" : "未配置上传地址（分享诊断日志直接导出）"); }   // main.m 起宿主前调一次 | main.m calls this once before starting the host
void dm_sharelog_share(void) { on_click(); }   // 游戏外的界面直接调（资源下载页的「分享诊断日志」按钮，main.m AssetVC） | called directly from screens outside the game (the "Share diagnostic log" button on the asset download page, main.m AssetVC)
