// 系统崩溃日志附进「分享诊断日志」（崩溃围栏，发布前闸 1）。iOS（dm_sharelog.m）与 Mac（front_mac.m）共用。
// 哪来：iOS 沙盒读不到系统的 .ips，改由 MetricKit 在下次启动送来崩溃诊断，main.m 的订阅者把最新一份存成 Documents/dm_crash_last.json；
//       Mac 读 ~/Library/Logs/DiagnosticReports 里本进程名开头的最新一份 .ips / .crash。
// 脱敏：这里先抹掉崩溃报告特有、通用脱敏盖不住的字段（崩溃报告键是 40 位十六进制、Team ID 字段名不同、用户号），整份导出之后照旧过 dm_diagnostic_scrub（家目录、包名、UUID、邮箱、角色名）。
// System crash logs attached to "share diagnostics" (crash fence, release gate 1). Shared by iOS (dm_sharelog.m) and Mac (front_mac.m).
// Source: the iOS sandbox cannot read the system's .ips, so MetricKit delivers crash diagnostics at the next launch and the subscriber in main.m stores the newest one as Documents/dm_crash_last.json;
//         on Mac, the newest .ips / .crash in ~/Library/Logs/DiagnosticReports starting with this process name.
// Redaction: fields specific to crash reports that the generic scrub does not cover are blanked here first (the crash reporter key is 40 hex digits, the Team ID field has another name, the user id); the whole export then goes through dm_diagnostic_scrub as before (home, bundle id, UUIDs, e-mail, character names).
#pragma once
#import <Foundation/Foundation.h>

static NSString *dm_crashlog_redact(NSString *text) {
    NSArray<NSString *> *keys = @[ @"crashReporterKey", @"deviceIdentifierForVendor", @"codeSigningTeamID", @"teamID", @"userID", @"sleepWakeUUID", @"bootSessionUUID", @"incident", @"incident_id", @"deviceName" ];
    for (NSString *k in keys) {   // JSON 形式："键": "值" 或 "键": 数字 | JSON form: "key": "value" or "key": number
        NSString *pat = [NSString stringWithFormat:@"(\"%@\"\\s*:\\s*)(\"[^\"]*\"|-?[0-9]+)", k];
        NSRegularExpression *r = [NSRegularExpression regularExpressionWithPattern:pat options:0 error:nil];
        text = [r stringByReplacingMatchesInString:text options:0 range:NSMakeRange(0, text.length) withTemplate:@"$1\"<redacted>\""];
    }
    // 旧式文本报告：「Crash Reporter Key: …」「Anonymous UUID: …」「Sleep/Wake UUID: …」 | old text reports
    NSRegularExpression *t = [NSRegularExpression regularExpressionWithPattern:@"^((?:Crash Reporter Key|Anonymous UUID|Sleep/Wake UUID|Beta Identifier):[ \\t]*).*$" options:NSRegularExpressionAnchorsMatchLines error:nil];
    return [t stringByReplacingMatchesInString:text options:0 range:NSMakeRange(0, text.length) withTemplate:@"$1<redacted>"];
}
// dir 里名字以 prefix 开头、以 suffixes 之一结尾的最新一份（按修改时间）；没有返回 nil | the newest file in dir whose name starts with prefix and ends with one of suffixes (by modification time); nil if none
static NSString *dm_crashlog_newest(NSString *dir, NSString *prefix, NSArray<NSString *> *suffixes) {
    NSFileManager *fm = NSFileManager.defaultManager; NSString *best = nil; NSDate *bt = nil;
    for (NSString *n in [fm contentsOfDirectoryAtPath:dir error:nil]) {
        if (prefix.length && ![n hasPrefix:prefix]) continue;
        BOOL ok = NO; for (NSString *s in suffixes) if ([n hasSuffix:s]) ok = YES; if (!ok) continue;
        NSString *p = [dir stringByAppendingPathComponent:n]; NSDate *m = [fm attributesOfItemAtPath:p error:nil].fileModificationDate;
        if (m && (!bt || [m compare:bt] == NSOrderedDescending)) { best = p; bt = m; }
    }
    return best;
}
// 崩溃段：path 为 nil 或读不到 = 「无崩溃日志」。只取前 cap 字节（崩溃报告开头就是异常与崩溃线程）。*summary 给头部摘要一行 | Crash section: path nil or unreadable = "no crash log". Only the first cap bytes (a crash report starts with the exception and the crashed thread). *summary gets one line for the header summary
static NSData *dm_crashlog_section(NSString *path, NSUInteger cap, NSString **summary) {
    NSData *d = path ? [NSData dataWithContentsOfFile:path options:NSDataReadingMappedIfSafe error:nil] : nil;
    if (!d.length) {
        if (summary) *summary = @"崩溃日志：无崩溃日志";
        return [@"\n\n===== 系统崩溃日志（无崩溃日志）=====\n" dataUsingEncoding:NSUTF8StringEncoding];
    }
    NSUInteger total = d.length; if (total > cap) d = [d subdataWithRange:NSMakeRange(0, cap)];
    NSString *text = [[NSString alloc] initWithData:d encoding:NSUTF8StringEncoding];
    if (!text) text = [[NSString alloc] initWithData:d encoding:NSISOLatin1StringEncoding] ?: @"";   // 截断切在多字节字符中间时 UTF-8 解不开 | UTF-8 fails when the cut lands inside a multi-byte character
    NSDate *m = [NSFileManager.defaultManager attributesOfItemAtPath:path error:nil].fileModificationDate;
    NSDateFormatter *df = [NSDateFormatter new]; df.dateFormat = @"yyyy-MM-dd HH:mm:ss ZZZ";
    NSString *when = m ? [df stringFromDate:m] : @"时间不明";
    if (summary) *summary = [NSString stringWithFormat:@"崩溃日志：%@（%@，%lu 字节）", path.lastPathComponent, when, (unsigned long)total];
    NSString *head = [NSString stringWithFormat:@"\n\n===== 系统崩溃日志 %@（%@，%lu 字节%@）=====\n", path.lastPathComponent, when, (unsigned long)total,
                      total > cap ? [NSString stringWithFormat:@"，只取前 %lu 字节", (unsigned long)cap] : @""];
    return [[head stringByAppendingString:dm_crashlog_redact(text)] dataUsingEncoding:NSUTF8StringEncoding];
}
