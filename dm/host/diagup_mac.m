// Mac 原生宿主的「上传诊断报告」外壳（只链进 make dmhost_diag，普通 dmhost 不带）：给公共状态机 dm_diagup.c 登记收集 / 上传 / 复制 / 导出回调，网络与签名走 dm_diag_upload.dylib（iOS 同一份 Swift 源码）。
// 用途：在 Mac 上验暂停菜单「分享诊断日志」的真实入口（不用起 iOS 包、不弹签名框）。地址只来自环境变量 DM_DIAG_ORIGIN（开发 / 联调用，源码与日志里都没有）；没设 = 不开上传，行为同普通宿主。
// 收集：本次日志 = 宿主 stderr 所指的文件（命令行局重定向的日志 / App 形态的 host.log），上一次 = 同目录 host.prev.log（有才放），崩溃 = DiagnosticReports 里本进程名最新一份；
//   头部 = 版本与机器读数 + 设置摘要（各关键前缀最后一行，同 iOS perf_block）+ 省略字节数；整份过 dm_diagnostic_scrub_names（存档目录里的角色名一并抹）再过上传专用的 dm_diagnostic_scrub_upload。
// 私钥：0600 文件 DM_DIAG_KEY_FILE（缺省 ~/Library/Application Support/D2R Mobile/diag_identity.key）；未签名的命令行宿主读钥匙串会弹授权框，所以不用钥匙串。
// "Upload diagnostic report" shell for the native Mac host (linked only into make dmhost_diag; plain dmhost does not carry it): registers collect / upload / copy / export callbacks with the shared state machine dm_diagup.c; network and signing via dm_diag_upload.dylib (the same Swift source as iOS).
// Purpose: verify the real pause-menu Share Diagnostics entry on the Mac (no iOS package, no signing prompt). The origin comes only from DM_DIAG_ORIGIN (development / integration; never in source or logs); unset = no upload, same as the plain host.
// Collection: this run = the file stderr points to (a redirected command-line log / host.log in App form), the previous run = host.prev.log next to it (if present), crash = the newest DiagnosticReports entry for this process;
//   header = version and machine readings + settings summary (last line of each key prefix, as iOS perf_block) + omitted byte counts; the whole text goes through dm_diagnostic_scrub_names (character names from the save directory too), then the upload-only dm_diagnostic_scrub_upload.
// Private key: a 0600 file DM_DIAG_KEY_FILE (default ~/Library/Application Support/D2R Mobile/diag_identity.key); an unsigned command-line host would get a Keychain prompt, so no Keychain here.
#import <Cocoa/Cocoa.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include "dm.h"
#include "dm_diagup.h"
#include "dm_i18n.h"
#import "../ios/Sources/dm_diagnostic_scrub.h"
#import "../ios/Sources/dm_diagnostic_read.h"
#import "../ios/Sources/dm_crashlog.h"

void dm_diagup_net_start(unsigned token, unsigned job, const char *origin, const char *meta, const char *key_path, char *(*collect)(long, void *), void *ctx,
                         void (*done)(unsigned, int, const char *, long long, int, int, int));   // dm_diag_upload.swift
void dm_diagup_net_cancel(void);
int dm_diagup_net_origin_ok(const char *origin);

static const char *mac_origin(void) { const char *o = getenv("DM_DIAG_ORIGIN"); return o && *o && dm_diagup_net_origin_ok(o) ? o : NULL; }
static int mac_configured(void) { return mac_origin() != NULL; }
static NSString *mac_log_path(void) { char p[PATH_MAX]; struct stat st; return fstat(2, &st) == 0 && S_ISREG(st.st_mode) && fcntl(2, F_GETPATH, p) == 0 ? @(p) : nil; }
static NSString *last_line(NSData *d, NSString *key) {   // 同 dm_sharelog.m last_line | as last_line in dm_sharelog.m
    NSData *k = [key dataUsingEncoding:NSUTF8StringEncoding]; if (!d.length) return nil;
    NSRange r = [d rangeOfData:k options:NSDataSearchBackwards range:NSMakeRange(0, d.length)]; if (r.location == NSNotFound) return nil;
    const char *b = d.bytes; NSUInteger s = r.location, e = r.location; while (s > 0 && b[s - 1] != '\n') s--; while (e < d.length && b[e] != '\n') e++;
    return [[NSString alloc] initWithBytes:b + s length:e - s encoding:NSUTF8StringEncoding] ?: @"（这一行不是合法 UTF-8）";
}
static NSData *mac_build(NSUInteger limit) {
    NSFileManager *fm = NSFileManager.defaultManager; NSString *home = NSHomeDirectory(), *cur_p = mac_log_path();
    NSString *prev_p = cur_p ? [[cur_p stringByDeletingLastPathComponent] stringByAppendingPathComponent:@"host.prev.log"] : nil;
    fflush(stderr);
    unsigned long long co = 0, po = 0; NSData *cur = cur_p ? dm_diagnostic_read_tail(cur_p, limit, &co) : nil, *prev = prev_p && [fm fileExistsAtPath:prev_p] ? dm_diagnostic_read_tail(prev_p, limit, &po) : nil;
    NSString *crash_sum = nil; NSData *crash = dm_crashlog_section(dm_crashlog_newest([home stringByAppendingPathComponent:@"Library/Logs/DiagnosticReports"], @(getprogname()), @[@".ips", @".crash"]), MIN((NSUInteger)256 * 1024, limit / 2), &crash_sum);
    char hw[64] = "?"; size_t hn = sizeof hw; sysctlbyname("hw.model", hw, &hn, NULL, 0); NSProcessInfo *pi = NSProcessInfo.processInfo;
    NSMutableString *h = [NSMutableString stringWithFormat:@"D2R Mobile Diagnostics (macOS host)\n机型 %s 系统 %@ 物理内存 %.0f GB\n%@\n", hw, pi.operatingSystemVersionString, pi.physicalMemory / 1073741824.0, crash_sum];
    [h appendFormat:@"Log upload: at most %lu bytes per log; omitted prefix bytes current=%llu previous=%llu. Original log files are unchanged.\n\n—— 设置摘要（本次日志各取最后一条）——\n", (unsigned long)limit, co, po];
    for (NSString *k in @[@"崩溃围栏：", @"自定义设置：读盘", @"图形设置：", @"帧率档：生效", @"超分档位：", @"内存档位：", @"光追阴影：生效", @"输入模式：", @"社区入口：清单", @"游戏难度：", @"线程同步：", @"[perf]", @"诊断上传：", @"自动上报："])
        [h appendFormat:@"%@\n", last_line(cur, k) ?: [NSString stringWithFormat:@"（没有「%@」行）", k]];
    NSMutableData *out = [[h dataUsingEncoding:NSUTF8StringEncoding] mutableCopy];
    void (^part)(NSString *, NSData *) = ^(NSString *t, NSData *d) {
        [out appendData:[[NSString stringWithFormat:@"\n\n===== %@（%@）=====\n", t, d ? [NSString stringWithFormat:@"%lu 字节", (unsigned long)d.length] : @"没有这份日志"] dataUsingEncoding:NSUTF8StringEncoding]];
        if (d) [out appendData:d]; };
    part(@"本次日志", cur); part(@"上一次日志 host.prev.log", prev); [out appendData:crash];
    // 存档目录里的角色名（只读目录名，不读存档内容）：DM_SAVE 下 <名>/<名>.d2s 或 <名>.d2s | character names from the save directory (names only, never contents)
    NSMutableArray<NSString *> *names = [NSMutableArray array]; const char *sv = getenv("DM_SAVE");
    for (NSString *n in sv ? [fm contentsOfDirectoryAtPath:@(sv) error:nil] : @[]) {
        NSString *base = n.stringByDeletingPathExtension;
        if ([n.pathExtension isEqualToString:@"d2s"] || [fm fileExistsAtPath:[[@(sv) stringByAppendingPathComponent:n] stringByAppendingPathComponent:[n stringByAppendingPathExtension:@"d2s"]]]) [names addObject:base];
    }
    return dm_diagnostic_scrub_upload(dm_diagnostic_scrub_names(out, home, NSBundle.mainBundle.bundleIdentifier ?: @"", names));
}
static char *mac_collect(long limit, void *ctx) { (void)ctx;
    @autoreleasepool {
        NSData *d = mac_build(limit > 0 ? (NSUInteger)limit : 0); if (!d) return NULL;
        char *p = malloc(d.length + 1); if (!p) return NULL; memcpy(p, d.bytes, d.length); p[d.length] = 0;
        for (NSUInteger i = 0; i < d.length; i++) if (!p[i]) p[i] = '?';
        LOG("诊断上传：已收集并脱敏 %lu 字节（每份日志上限 %ld）", (unsigned long)d.length, limit);
        return p;
    }
}
static void mac_done(unsigned token, int outcome, const char *detail, long long expires_at, int flags, int retry_after, int retention_days) { dm_diagup_result(token, outcome, detail, expires_at, flags, retry_after, retention_days); }
static void mac_start(unsigned token, unsigned job) {
    NSOperatingSystemVersion os = NSProcessInfo.processInfo.operatingSystemVersion; char hw[64] = "unknown"; size_t hn = sizeof hw; sysctlbyname("hw.model", hw, &hn, NULL, 0);
    static const char *const lg[] = { "en", "zh-Hans", "es-ES", "es-MX", "ru", "ko" }; int g = dm_lang_group();
    const char *rev = getenv("DM_DIAG_SOURCE_REVISION");   // 开发宿主没有 Info.plist：提交号由起局脚本给（缺省 unknown） | the dev host has no Info.plist: the commit comes from the run script (default unknown)
    NSString *meta = [NSString stringWithFormat:@"app_version=dev\nbuild=0\nsource_revision=%s\nmodel=%s\nos_version=%ld.%ld.%ld\nlocale=%s", rev ? rev : "unknown", hw,
                      (long)os.majorVersion, (long)os.minorVersion, (long)os.patchVersion, g >= 0 && g < 6 ? lg[g] : "en"];
    const char *o = mac_origin(); if (!o) { mac_done(token, DM_DIAGUP_CONFIG, "origin", 0, 0, 0, 0); return; }
    const char *kf = getenv("DM_DIAG_KEY_FILE"); NSString *def = [[NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES).firstObject stringByAppendingPathComponent:@"D2R Mobile"] stringByAppendingPathComponent:@"diag_identity.key"];
    dm_diagup_net_start(token, job, o, meta.UTF8String, kf && *kf ? kf : def.fileSystemRepresentation, mac_collect, NULL, mac_done);
}
static int mac_copy(const char *s) {   // 同 app_mac.m mac_copy | as mac_copy in app_mac.m
    NSString *t = @(s ?: ""); __block BOOL ok = NO;
    void (^w)(void) = ^{ NSPasteboard *pb = NSPasteboard.generalPasteboard; [pb clearContents]; ok = [pb setString:t forType:NSPasteboardTypeString]; };
    if (NSThread.isMainThread) w(); else dispatch_sync(dispatch_get_main_queue(), w);
    return ok ? 1 : 0;
}
// 「导出到本地」：Mac 宿主没有系统分享面板；DM_DIAG_EXPORT_TO=<文件> 时把同一份（导出口径：每份 2 MiB，不过上传专用那一道）写到那里，供回归核对 | Export locally: the Mac host has no share sheet; with DM_DIAG_EXPORT_TO=<file> the export-scope text is written there for regression checks
static void mac_export(void) {
    const char *to = getenv("DM_DIAG_EXPORT_TO"); if (!to || !*to) { LOG("诊断上传：Mac 宿主没有分享面板，导出到本地不做事（菜单「帮助 → 分享日志」可用）"); return; }
    NSString *dst = @(to);
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{ @autoreleasepool {
        NSFileManager *fm = NSFileManager.defaultManager; NSString *home = NSHomeDirectory(), *cur_p = mac_log_path(); fflush(stderr);
        NSData *cur = cur_p ? dm_diagnostic_read_tail(cur_p, 2 * 1024 * 1024, NULL) : nil;
        NSData *d = dm_diagnostic_scrub_names(cur ?: [NSData data], home, NSBundle.mainBundle.bundleIdentifier ?: @"", nil);
        BOOL ok = [d writeToFile:dst atomically:YES]; (void)fm;
        LOG("%s诊断日志：导出到本地%s（%lu 字节）", ok ? "" : "【红】", ok ? "已写好" : "写不进", (unsigned long)d.length); } });
}
// 自动上报崩溃的 Mac 外壳（只在 dmhost_diag 里；地址同 DM_DIAG_ORIGIN）。崩溃段优先用 DM_TEST_CRASH_JSON 指的样例文件（回归用），否则同 mac_build 的 DiagnosticReports | the automatic crash report's Mac shell (dmhost_diag only; same DM_DIAG_ORIGIN)
#include "dm_crashreport.h"
void dm_crashreport_net_start(unsigned token, const char *origin, const char *meta, const char *key_path, const char *dir, char *(*collect)(long, void *), void *ctx,
                              void (*done)(unsigned, int, const char *, long long, int, int, int));
void dm_crashreport_net_cancel(void);
void dm_crashreport_net_prepare(const char *meta, const char *dir, char *(*collect)(long, void *), void *ctx);
static char *mac_crash_collect(long limit, void *ctx) {
    @autoreleasepool {
        NSUInteger lim = limit > 0 ? MIN((NSUInteger)limit, (NSUInteger)128 * 1024) : 0; 
        NSData *body = mac_build(lim); NSMutableData *all = [[[NSString stringWithFormat:@"D2R Mobile Plus crash report (automatic)\n%s\n\n", (const char *)ctx] dataUsingEncoding:NSUTF8StringEncoding] mutableCopy];
        if (body) [all appendData:body];
        const char *cj = getenv("DM_TEST_CRASH_JSON"); if (cj) { NSString *sum = nil; [all appendData:dm_crashlog_section(@(cj), lim, &sum)]; }
        NSData *d = dm_diagnostic_scrub_upload(dm_diagnostic_scrub_literals(dm_diagnostic_scrub_names(all, NSHomeDirectory(), NSBundle.mainBundle.bundleIdentifier ?: @"", nil), @[NSHost.currentHost.localizedName ?: @"", NSUserName(), NSFullUserName()])); if (!d) return NULL;
        char *p = malloc(d.length + 1); if (!p) return NULL; memcpy(p, d.bytes, d.length); p[d.length] = 0;
        for (NSUInteger i = 0; i < d.length; i++) if (!p[i]) p[i] = '?';
        LOG("自动上报：已收集并脱敏 %lu 字节（每份日志上限 %lu）", (unsigned long)d.length, (unsigned long)lim);
        return p;
    }
}
static void mac_crash_done(unsigned token, int outcome, const char *detail, long long expires_at, int flags, int retry_after, int retention_days) { (void)expires_at; (void)retention_days; dm_crashreport_result(token, outcome, detail, flags, retry_after); }
static void mac_crash_start(unsigned token, const char *fence, const char *dir) {
    NSOperatingSystemVersion os = NSProcessInfo.processInfo.operatingSystemVersion; char hw[64] = "unknown"; size_t hn = sizeof hw; sysctlbyname("hw.model", hw, &hn, NULL, 0);
    const char *rev = getenv("DM_DIAG_SOURCE_REVISION");
    NSString *meta = [NSString stringWithFormat:@"app_version=dev\nbuild=0\nsource_revision=%s\nmodel=%s\nos_version=%ld.%ld.%ld\nlocale=en", rev ? rev : "unknown", hw, (long)os.majorVersion, (long)os.minorVersion, (long)os.patchVersion];
    const char *o = mac_origin(); if (!o) { mac_crash_done(token, DM_DIAGUP_CONFIG, "origin", 0, 0, 0, 0); return; }
    const char *kf = getenv("DM_DIAG_KEY_FILE"); NSString *def = [[NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES).firstObject stringByAppendingPathComponent:@"D2R Mobile"] stringByAppendingPathComponent:@"diag_identity.key"];
    static char fence_copy[160]; snprintf(fence_copy, sizeof fence_copy, "%s", fence ? fence : "");
    dm_crashreport_net_start(token, o, meta.UTF8String, kf && *kf ? kf : def.fileSystemRepresentation, dir, mac_crash_collect, fence_copy, mac_crash_done);
}
static void mac_crash_prepare(const char *fence, const char *dir) {
    static char fence_copy[160]; snprintf(fence_copy, sizeof fence_copy, "%s", fence ? fence : "");
    dm_crashreport_net_prepare("app_version=dev\nbuild=0\nsource_revision=unknown\nmodel=unknown\nos_version=0.0\nlocale=en", dir, mac_crash_collect, fence_copy);
}
static const dm_crashreport_shell_t mac_crash_shell = { mac_configured, mac_crash_start, dm_crashreport_net_cancel, mac_crash_prepare, 0 };
static const dm_diagup_shell_t mac_shell = { mac_configured, mac_start, dm_diagup_net_cancel, mac_export, mac_copy };
__attribute__((constructor)) static void diagup_mac_register(void) { dm_diagup_init(&mac_shell); dm_crashreport_init(&mac_crash_shell); }   // 只在 dmhost_diag 里链进来：一启动就登记 | linked only into dmhost_diag: registered at launch
