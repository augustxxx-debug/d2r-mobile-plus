#import <Foundation/Foundation.h>
#import "../Sources/dm_diagnostic_scrub.h"
#import "../Sources/dm_diagnostic_read.h"
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
    result=[[NSString alloc] initWithData:dm_diagnostic_scrub([@"1 屏幕：Sample Monitor G8，2560x1440\n出图节拍（1 屏幕 Sample Monitor G8，刷新率 120）\n屏幕 2622x1206：签名工具开了，" dataUsingEncoding:NSUTF8StringEncoding],@"",@"") encoding:NSUTF8StringEncoding];
    if ([result containsString:@"Sample Monitor"] || ![result containsString:@"屏幕：<display>，2560x1440"] || ![result containsString:@"刷新率 120"] || ![result containsString:@"屏幕 2622x1206：签名工具开了，"]) return 11;
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
    puts("PASS diagnostics redaction preserves graphics metrics and removes identifiers/character names, including invalid UTF-8 input");
} return 0; }
