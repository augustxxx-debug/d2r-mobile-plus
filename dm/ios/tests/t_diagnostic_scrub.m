#import <Foundation/Foundation.h>
#import "../Sources/dm_diagnostic_scrub.h"
#import "../Sources/dm_diagnostic_read.h"
#import "../Sources/dm_crashlog.h"
int main(void) { @autoreleasepool {
    NSString *source = @"MetalFX enabled=1 selected=Quality active=1 FPS target=60 renderScale=0.85 2424x1705 iPhone19,4\n/Users/SamplePerson/build org.example.privategame person@example.invalid\n/var/mobile/Containers/Data/Application/11111111-2222-3333-4444-555555555555/Documents/save.work/测试角色/Test.d2s\n角色 测试角色 UDID=12345678-0123456789abcdef TeamIdentifier=ABCDEFGHIJ\n";
    NSString *result = [[NSString alloc] initWithData:dm_diagnostic_scrub([source dataUsingEncoding:NSUTF8StringEncoding],@"/var/mobile",@"org.example.privategame") encoding:NSUTF8StringEncoding];
    for (NSString *private in @[@"SamplePerson",@"org.example.privategame",@"person@example.invalid",@"11111111-2222-3333-4444-555555555555",@"测试角色",@"12345678-0123456789abcdef",@"ABCDEFGHIJ"]) if ([result containsString:private]) return 1;
    for (NSString *metric in @[@"selected=Quality",@"active=1",@"target=60",@"renderScale=0.85",@"2424x1705",@"iPhone19,4",@"Test.d2s"]) if (![result containsString:metric]) return 2;
    if (dm_diagnostic_scrub(nil,@"",@"") != nil) return 3;
    const unsigned char invalid[] = {0xff,'p','e','r','s','o','n','@','e','x','a','m','p','l','e','.','c','o','m'};
    NSData *raw=[NSData dataWithBytes:invalid length:sizeof invalid];
    result=[[NSString alloc] initWithData:dm_diagnostic_scrub(raw,@"",@"") encoding:NSUTF8StringEncoding];
    if (!result || [result containsString:@"person@example.com"]) return 4;
    result=[[NSString alloc] initWithData:dm_diagnostic_scrub_names([@"角色 TestPerson MetalFX active=1" dataUsingEncoding:NSUTF8StringEncoding],@"",@"",@[@"TestPerson"]) encoding:NSUTF8StringEncoding];
    if ([result containsString:@"TestPerson"] || ![result containsString:@"active=1"]) return 10;
    // 一审 A1：中文 / 日文 / 韩文名紧挨汉字也替换；拉丁名挨汉字算词边界；同文种字母相连不算；名字互为子串先换长的 | first audit A1: CJK names next to Han text, Latin names next to Han, same-script joins kept, longer names first
    result=[[NSString alloc] initWithData:dm_diagnostic_scrub_names([@"角色张三进入营地\n张三abc 与 abc张三\n「张三」，张三：\n张三丰来了，张三走了\n角色Galkamir进入营地\n갈카미르가 마을에 / Anna stays, Ann goes / Иванов stays, Иван goes / ガルカミール" dataUsingEncoding:NSUTF8StringEncoding],@"",@"",@[@"张三",@"张三丰",@"Galkamir",@"갈카미르",@"Ann",@"Иван",@"ガルカ"]) encoding:NSUTF8StringEncoding];
    for (NSString *want in @[@"角色<character>进入营地",@"<character>abc 与 abc<character>",@"「<character>」，<character>：",@"<character>来了，<character>走了",@"角色<character>进入营地",@"<character>가 마을에",@"Anna stays, <character> goes",@"Иванов stays, <character> goes",@"<character>ミール"]) if (![result containsString:want]) { printf("A1 missing: %s\n", want.UTF8String); return 12; }
    for (NSString *leak in @[@"张三",@"丰",@"Galkamir"]) if ([result containsString:leak]) { printf("A1 leak: %s\n", leak.UTF8String); return 13; }
    result=[[NSString alloc] initWithData:dm_diagnostic_scrub([@"1 屏幕：Sample Monitor G8，2560x1440\n出图节拍（1 屏幕 Sample Monitor G8，刷新率 120）\n屏幕 2622x1206：签名工具开了，" dataUsingEncoding:NSUTF8StringEncoding],@"",@"") encoding:NSUTF8StringEncoding];
    if ([result containsString:@"Sample Monitor"] || ![result containsString:@"屏幕：<display>，2560x1440"] || ![result containsString:@"刷新率 120"] || ![result containsString:@"屏幕 2622x1206：签名工具开了，"]) return 11;
    // 社区入口的日志行：群号（10 位数字）与仓库地址都是给玩家看的公开值，脱敏不得抹掉；同一行里的邮箱 / 主目录照抹 | community entry log line: the group number (10 digits) and the repository address are public values shown to players and must survive redaction; an email / home directory on the same line is still redacted
    result=[[NSString alloc] initWithData:dm_diagnostic_scrub([@"[dm] 社区入口：清单有字段 群号=1129913678 链接=https://github.com/example-owner/d2r-mobile-plus/discussions\n[dm] 社区入口：复制 群号 1129913678（/Users/SamplePerson person@example.invalid）" dataUsingEncoding:NSUTF8StringEncoding],@"/Users/SamplePerson",@"") encoding:NSUTF8StringEncoding];
    if (![result containsString:@"社区入口：清单有字段 群号=1129913678 链接=https://github.com/example-owner/d2r-mobile-plus/discussions"] || ![result containsString:@"社区入口：复制 群号 1129913678"] || [result containsString:@"SamplePerson"] || [result containsString:@"person@example.invalid"]) return 12;
    // 鸣谢里的「Augustus」是公开署名（只在游戏画面里画，不写日志）；设备 / 显示器名「Augustus' iPhone」照擦（用户 2026-10-29：脱敏规则不变） | the credits name is public and never logged; a device / display name is still erased
    result=[[NSString alloc] initWithData:dm_diagnostic_scrub([@"[dm] 屏幕：Augustus' iPhone，120 Hz\n" dataUsingEncoding:NSUTF8StringEncoding],@"/Users/SamplePerson",@"org.example.privategame") encoding:NSUTF8StringEncoding];
    if ([result containsString:@"Augustus"] || ![result containsString:@"屏幕：<display>，"]) return 13;
    NSString *dir=[NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
    [NSFileManager.defaultManager createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
    NSString *path=[dir stringByAppendingPathComponent:@"log.txt"];
    NSMutableData *large=[NSMutableData dataWithLength:5*1024*1024];
    memset(large.mutableBytes,'a',large.length);
    NSData *tail=[@"\n温度正常 MetalFX enabled=1\n" dataUsingEncoding:NSUTF8StringEncoding];
    [large appendData:tail]; [large writeToFile:path atomically:YES];
    unsigned long long omitted=0;
    NSData *read=dm_diagnostic_read_tail(path,1024,&omitted);
    if (read.length>1024 || ![read isEqual: [tail subdataWithRange:NSMakeRange(1,tail.length-1)]] || omitted!=large.length-read.length) return 5;
    NSData *original=[NSData dataWithContentsOfFile:path]; if (![large isEqual:original]) return 6;
    [tail writeToFile:path atomically:YES]; read=dm_diagnostic_read_tail(path,1024,&omitted);
    if (![read isEqual:tail] || omitted) return 7;
    if (dm_diagnostic_read_tail([dir stringByAppendingPathComponent:@"missing"],1024,NULL)) return 8;
    if (dm_diagnostic_read_tail(path,0,NULL).length) return 9;
    [NSFileManager.defaultManager removeItemAtPath:dir error:nil];
    // 上传专用的再一道：网址、IPv4 / IPv6、令牌 / 票据 / 口令 / 私钥行抹掉；时间戳、分辨率、版本号、社区群号不动 | upload-only pass: URLs, IPs and secret-bearing lines go; timestamps, resolutions, versions and the group number stay
    result=[[NSString alloc] initWithData:dm_diagnostic_scrub_upload([@"12:34:56 大厅 https://lobby.example.invalid:8443/v1 连接 203.0.113.7 与 2001:db8:0:1::5 和 fe80:0:0:0:1:2:3:4 ::1 std::vector\n[coop] device_token=abc123\nhost token: xyz\nticket=TKT1\n-----BEGIN PRIVATE KEY-----\n版本 1.0.0+28 2424x1705 群号 1129913678 光追 AO：r=1.5\n" dataUsingEncoding:NSUTF8StringEncoding]) encoding:NSUTF8StringEncoding];
    for (NSString *secret in @[@"lobby.example.invalid",@"203.0.113.7",@"fe80:0:0:0:1:2:3:4",@"2001:db8",@"::5",@"abc123",@"xyz",@"TKT1",@"BEGIN PRIVATE"]) if ([result containsString:secret]) { printf("upload scrub kept %s\n", secret.UTF8String); return 14; }
    for (NSString *keep in @[@"12:34:56 大厅 <url> 连接 <ip> 与 <ip> 和 <ip> <ip> std::vector",@"版本 1.0.0+28 2424x1705 群号 1129913678 光追 AO：r=1.5"]) if (![result containsString:keep]) { printf("upload scrub lost %s\n", keep.UTF8String); return 15; }
    // 自动上报崩溃的整份：报告头 + MetricKit 诊断（dm_crashlog_section 先抹崩溃字段）+ 日志，走与外壳相同的三道；设备名、UDID、容器 UUID、用户名、路径、邮箱、角色名都不得留 | the whole automatic crash report through the same passes as the shells
    { NSString *json = @"{\"crashDiagnostics\":[{\"diagnosticMetaData\":{\"deviceType\":\"iPhone19,4\",\"osVersion\":\"iPhone OS 27.2\",\"appBuildVersion\":\"999\",\"signal\":11,\"exceptionType\":1},\"deviceName\":\"Sample's iPhone\",\"crashReporterKey\":\"0123456789abcdef0123456789abcdef01234567\",\"codeSigningTeamID\":\"ABCDEFGHIJ\",\"userID\":501,\"callStackTree\":{\"binaryName\":\"DmIOS\",\"path\":\"/var/mobile/Containers/Bundle/Application/11111111-2222-3333-4444-555555555555/DmIOS.app/DmIOS\"}}]}";
      NSString *jp = [NSTemporaryDirectory() stringByAppendingPathComponent:[NSString stringWithFormat:@"t_scrub_%d.json", getpid()]]; [json writeToFile:jp atomically:YES encoding:NSUTF8StringEncoding error:nil];
      NSString *sum = nil; NSData *crash = dm_crashlog_section(jp, 64 * 1024, &sum); [NSFileManager.defaultManager removeItemAtPath:jp error:nil];
      NSMutableData *all = [[@"D2R Mobile Plus crash report (automatic)\nfence cause=crash arg=11 streak=2 hist=11\ngpu Apple A20 Pro GPU family_apple9=1\n设备 Sample's iPhone 启动\n/Users/SamplePerson/Library/x\n/var/mobile/Containers/Data/Application/AAAAAAAA-2222-3333-4444-555555555555/Documents/save/张三/张三.d2s\n角色 张三 进入 UDID 00008110-000A1B2C3D4E5F60 person@example.invalid https://diag.example.invalid/x 198.51.100.9\n帧率档：生效 60 [perf] 机型 iPhone19,4\n" dataUsingEncoding:NSUTF8StringEncoding] mutableCopy];
      [all appendData:crash];
      NSData *out = dm_diagnostic_scrub_upload(dm_diagnostic_scrub_literals(dm_diagnostic_scrub_names(all, @"/var/mobile", @"org.example.privategame", @[@"张三"]), @[@"Sample's iPhone"]));
      NSString *t = [[NSString alloc] initWithData:out encoding:NSUTF8StringEncoding];
      for (NSString *secret in @[@"Sample's iPhone",@"SamplePerson",@"00008110-000A1B2C3D4E5F60",@"AAAAAAAA-2222",@"11111111-2222",@"张三",@"person@example.invalid",@"diag.example.invalid",@"198.51.100.9",@"0123456789abcdef0123456789abcdef01234567",@"ABCDEFGHIJ",@"\"userID\":501"]) if ([t containsString:secret]) { printf("crash report kept %s\n", secret.UTF8String); return 16; }
      for (NSString *keep in @[@"fence cause=crash arg=11 streak=2 hist=11",@"Apple A20 Pro GPU",@"iPhone19,4",@"\"signal\":11",@"DmIOS",@"帧率档：生效 60"]) if (![t containsString:keep]) { printf("crash report lost %s\n", keep.UTF8String); return 17; }
      NSData *none = dm_diagnostic_scrub_literals([@"iPhoneX iPhone 2" dataUsingEncoding:NSUTF8StringEncoding], @[@"iPhone"]);
      if (![[[NSString alloc] initWithData:none encoding:NSUTF8StringEncoding] isEqualToString:@"iPhoneX <device> 2"]) return 18; }
    puts("PASS diagnostics redaction preserves graphics metrics and removes identifiers/character names, including invalid UTF-8 input");
} return 0; }
