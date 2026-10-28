// 崩溃围栏：系统崩溃日志附进分享诊断日志（dm_crashlog.h）。造一份带隐私字段的假 .ips / MetricKit JSON，过 dm_crashlog_section + dm_diagnostic_scrub_names
// （与 dm_sharelog.m / front_mac.m 的导出同一顺序），核：崩溃段在、摘要行对、隐私扫描 0 命中；没有崩溃日志时写「无崩溃日志」；超长只取前 cap 字节；选最新一份。
// 测试数据全是编的（用户名、Team ID、邮箱、角色名），不含本机信息
// Crash fence: the system crash log attached to share diagnostics (dm_crashlog.h). Build a fake .ips / MetricKit JSON with private fields and run it through dm_crashlog_section + dm_diagnostic_scrub_names
// (same order as the dm_sharelog.m / front_mac.m exports); check: the crash section is there, the summary line is right, the privacy scan has 0 hits; "no crash log" when there is none; over-long input keeps only the first cap bytes; the newest file is picked.
// All test data is made up (user name, Team ID, e-mail, character name), nothing from this machine
#import <Foundation/Foundation.h>
#import "../Sources/dm_crashlog.h"
#import "../Sources/dm_diagnostic_scrub.h"
static int checks, fails;
#define CHECK(x) do { checks++; if (!(x)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)
int main(void) { @autoreleasepool {
    NSFileManager *fm = NSFileManager.defaultManager;
    NSString *dir = [NSTemporaryDirectory() stringByAppendingPathComponent:[NSString stringWithFormat:@"dm-crashlog-%d", getpid()]];
    [fm removeItemAtPath:dir error:nil]; [fm createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
    NSString *home = @"/Users/alice", *bundle = @"org.example.d2rtest";
    NSString *ips = @"{\"app_name\":\"dmhost\",\"bug_type\":\"309\",\"incident_id\":\"6F1C2B3A-1111-2222-3333-444455556666\",\"timestamp\":\"2026-10-28 10:00:00.00 +0800\"}\n"
        "{\"procPath\":\"/Users/alice/Library/Containers/CAFEBABE-0000-4000-8000-00000000C0DE/Data/dmhost\",\"crashReporterKey\":\"0123456789abcdef0123456789abcdef01234567\","
        "\"codeSigningTeamID\":\"ABCDE12345\",\"codeSigningID\":\"org.example.d2rtest\",\"userID\":501,\"sleepWakeUUID\":\"AAAAAAAA-BBBB-CCCC-DDDD-EEEEEEEEEEEE\","
        "\"deviceName\":\"Alice的 iPhone\",\"exception\":{\"type\":\"EXC_BAD_ACCESS\",\"signal\":\"SIGSEGV\"},\"note\":\"contact alice@example.com\","
        "\"save\":\"/var/mobile/Containers/Data/Application/11111111-2222-3333-4444-555555555555/Documents/save/Hero/Hero.d2s\",\"faultingThread\":0}\n"
        "Crash Reporter Key:   89abcdef0123456789abcdef0123456789abcdef\nAnonymous UUID:       12345678-90AB-CDEF-1234-567890ABCDEF\n";
    NSString *p1 = [dir stringByAppendingPathComponent:@"dmhost-2026-10-27-090000.ips"], *p2 = [dir stringByAppendingPathComponent:@"dmhost-2026-10-28-100000.ips"];
    [@"old" writeToFile:p1 atomically:YES encoding:NSUTF8StringEncoding error:nil];
    [ips writeToFile:p2 atomically:YES encoding:NSUTF8StringEncoding error:nil];
    [fm setAttributes:@{NSFileModificationDate: [NSDate dateWithTimeIntervalSinceNow:-86400]} ofItemAtPath:p1 error:nil];
    [@"x" writeToFile:[dir stringByAppendingPathComponent:@"Other-2026-10-29.ips"] atomically:YES encoding:NSUTF8StringEncoding error:nil];   // 别的进程的 | another process's

    // 1. 选最新一份、只认本进程名开头 | 1. The newest one, only names starting with this process
    NSString *pick = dm_crashlog_newest(dir, @"dmhost", @[@".ips", @".crash"]);
    CHECK([pick isEqualToString:p2]);
    CHECK(dm_crashlog_newest(dir, @"nothing", @[@".ips"]) == nil);
    // 2. 崩溃段在末尾、摘要行带文件名，整份导出过脱敏后隐私扫描 0 命中 | 2. The crash section at the end, the summary names the file, and after the export is scrubbed the privacy scan has 0 hits
    NSString *sum = nil; NSData *sec = dm_crashlog_section(pick, 256 * 1024, &sum);
    CHECK([sum hasPrefix:@"崩溃日志：dmhost-2026-10-28-100000.ips（"]);
    NSMutableData *out = [[@"头部\n本局日志……\n" dataUsingEncoding:NSUTF8StringEncoding] mutableCopy]; [out appendData:sec];
    NSString *fin = [[NSString alloc] initWithData:dm_diagnostic_scrub_names(out, home, bundle, @[@"Hero"]) encoding:NSUTF8StringEncoding];
    CHECK([fin containsString:@"===== 系统崩溃日志 dmhost-2026-10-28-100000.ips"]);
    CHECK([fin containsString:@"EXC_BAD_ACCESS"]);   // 诊断内容留着 | the diagnostic content stays
    NSArray<NSString *> *leaks = @[ @"alice", @"Alice", @"ABCDE12345", @"0123456789abcdef0123456789abcdef01234567", @"89abcdef0123456789abcdef0123456789abcdef", @"CAFEBABE",
        @"org.example.d2rtest", @"6F1C2B3A", @"AAAAAAAA-BBBB", @"11111111-2222", @"12345678-90AB", @"Hero", @"\"userID\":501" ];
    int hits = 0; for (NSString *l in leaks) if ([fin rangeOfString:l].location != NSNotFound) { hits++; printf("隐私命中：%s\n", l.UTF8String); }
    CHECK(hits == 0);
    // 3. 没有崩溃日志：摘要写「无崩溃日志」，段落照样有标题 | 3. No crash log: the summary says "no crash log", the section still has its title
    NSString *none = nil; NSData *ns = dm_crashlog_section(nil, 1024, &none);
    CHECK([none isEqualToString:@"崩溃日志：无崩溃日志"]);
    CHECK([[[NSString alloc] initWithData:ns encoding:NSUTF8StringEncoding] containsString:@"系统崩溃日志（无崩溃日志）"]);
    CHECK(dm_crashlog_section([dir stringByAppendingPathComponent:@"missing.ips"], 1024, &none) != nil && [none isEqualToString:@"崩溃日志：无崩溃日志"]);
    // 4. 超长只取前 cap 字节、标题写明 | 4. Over-long input keeps only the first cap bytes and the title says so
    NSString *big = [dir stringByAppendingPathComponent:@"dmhost-big.ips"]; NSMutableString *b = [NSMutableString string]; for (int i = 0; i < 5000; i++) [b appendString:@"线程栈帧 0x1234\n"];
    [b writeToFile:big atomically:YES encoding:NSUTF8StringEncoding error:nil];
    NSData *bs = dm_crashlog_section(big, 1000, NULL); CHECK(bs.length < 1400);
    CHECK([[[NSString alloc] initWithData:bs encoding:NSUTF8StringEncoding] ?: @"" containsString:@"只取前 1000 字节"]);
    [fm removeItemAtPath:dir error:nil];
    printf("t_crashlog: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}}
