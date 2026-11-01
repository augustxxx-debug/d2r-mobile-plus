// Export-only redaction. Never modifies game files or the original local logs.
#import <Foundation/Foundation.h>
static NSData *dm_diagnostic_scrub_names(NSData *data, NSString *home, NSString *bundle, NSArray<NSString *> *known_names) {
    if (!data) return nil;
    NSString *text = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
    if (!text) text = [[NSString alloc] initWithData:data encoding:NSISOLatin1StringEncoding];
    if (!text) return [NSData data];
    if (home.length > 1) text = [text stringByReplacingOccurrencesOfString:home withString:@"~"];
    if (bundle.length) text = [text stringByReplacingOccurrencesOfString:bundle withString:@"<app-id>"];
    // Characters may be named after people. Discover names from save paths,
    // then redact whole occurrences without removing settings/file extensions.
    NSRegularExpression *names = [NSRegularExpression regularExpressionWithPattern:@"/save(?:\\.test)?(?:\\.work)?/([^/\\s]+)/" options:0 error:nil];
    NSMutableSet<NSString *> *characters = [NSMutableSet setWithArray:known_names ?: @[]];
    for (NSTextCheckingResult *match in [names matchesInString:text options:0 range:NSMakeRange(0,text.length)]) {
        NSString *name = [text substringWithRange:[match rangeAtIndex:1]];
        if (![name isEqualToString:@"Settings"] && ![name hasPrefix:@"ModernSharedStash"]) [characters addObject:name];
    }
    // 边界：名字这一端是汉字 / 假名 / 注音 / 谚文（不靠空格分词）时按字面子串换；否则只有同为「空格分词的字母 / 数字 / 下划线」的邻字才挡（拉丁名挨着汉字算换文种）。
    // 长名先换（「张三」先换会把「张三丰」变成「<character>丰」）。规则与安卓 sharelog_android.c 的 side_blocks 一致。一审 A1：原先汉字也算字母，「角色张三进入营地」不替换
    // Boundaries: when the name's end is Han / kana / Bopomofo / Hangul (not space-delimited) the name is replaced as a literal substring; otherwise only a neighbouring space-delimited
    // letter / digit / underscore blocks (a Latin name next to Han is a script change). Longer names go first (replacing 张三 first would turn 张三丰 into "<character>丰").
    // Same rule as side_blocks in Android's sharelog_android.c. First audit A1: Han used to count as a letter, so 「角色张三进入营地」 was left as is
    NSString *unspaced = @"\\p{Han}\\p{Hiragana}\\p{Katakana}\\p{Bopomofo}\\p{Hangul}";
    NSString *word = [NSString stringWithFormat:@"[[\\p{L}\\p{N}_]--[%@]]", unspaced];
    NSRegularExpression *cjk = [NSRegularExpression regularExpressionWithPattern:[NSString stringWithFormat:@"^[%@]$", unspaced] options:0 error:nil];
    NSArray<NSString *> *ordered = [characters.allObjects sortedArrayUsingComparator:^NSComparisonResult(NSString *a, NSString *b) {
        if (a.length != b.length) return a.length > b.length ? NSOrderedAscending : NSOrderedDescending;
        return [a compare:b];
    }];
    for (NSString *name in ordered) {
        if (!name.length) continue;
        NSString *head = [name substringWithRange:[name rangeOfComposedCharacterSequenceAtIndex:0]];
        NSString *tail = [name substringWithRange:[name rangeOfComposedCharacterSequenceAtIndex:name.length - 1]];
        BOOL head_cjk = [cjk numberOfMatchesInString:head options:0 range:NSMakeRange(0, head.length)] > 0;
        BOOL tail_cjk = [cjk numberOfMatchesInString:tail options:0 range:NSMakeRange(0, tail.length)] > 0;
        NSString *pattern = [NSString stringWithFormat:@"%@%@%@", head_cjk ? @"" : [NSString stringWithFormat:@"(?<!%@)", word],
                             [NSRegularExpression escapedPatternForString:name], tail_cjk ? @"" : [NSString stringWithFormat:@"(?!%@)", word]];
        NSRegularExpression *r = [NSRegularExpression regularExpressionWithPattern:pattern options:0 error:nil];
        // 正则编不出来时宁可多换，不漏名字 | if the pattern fails to compile, over-redact rather than leak the name
        text = r && cjk ? [r stringByReplacingMatchesInString:text options:0 range:NSMakeRange(0,text.length) withTemplate:@"<character>"]
                        : [text stringByReplacingOccurrencesOfString:name withString:@"<character>"];
    }
    NSArray<NSString *> *patterns = @[
        @"/Users/[^/\\s]+", @"[A-Z0-9._%+-]+@[A-Z0-9.-]+\\.[A-Z]{2,}",
        @"[0-9A-F]{8}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{12}",
        @"(?<![0-9A-F])[0-9A-F]{8}-[0-9A-F]{16}(?![0-9A-F])",
        @"(?:DEVELOPMENT_TEAM|TeamIdentifier|Team ID)\\s*[:=]\\s*[A-Z0-9]{10}"
    ];
    for (NSString *pattern in patterns) {
        NSRegularExpression *r = [NSRegularExpression regularExpressionWithPattern:pattern options:NSRegularExpressionCaseInsensitive error:nil];
        text = [r stringByReplacingMatchesInString:text options:0 range:NSMakeRange(0,text.length) withTemplate:@"<redacted>"];
    }
    // 外接显示器名（宿主日志「屏幕：<名字>，」「屏幕 <名字>，」）可能带型号或用户自起的名字；「屏幕 2622x1206」这类分辨率不动 | external display names in host logs may carry a model or a user-chosen name; resolutions such as 「屏幕 2622x1206」 are kept
    NSRegularExpression *disp = [NSRegularExpression regularExpressionWithPattern:@"(屏幕(?:[：:]\\s*|\\s+))(?!\\d+x\\d)[^，,\\n]+([，,])" options:0 error:nil];
    text = [disp stringByReplacingMatchesInString:text options:0 range:NSMakeRange(0,text.length) withTemplate:@"$1<display>$2"];
    return [text dataUsingEncoding:NSUTF8StringEncoding];
}

static inline NSData *dm_diagnostic_scrub(NSData *data, NSString *home, NSString *bundle) {
    return dm_diagnostic_scrub_names(data,home,bundle,nil);
}

// 自动 / 手动上传再抹一遍本机专有的字面值（设备名、Mac 用户名）：只抹整词；iOS 上没权益时系统给的设备名就是「iPhone」，这种通用词外壳不传进来 | blank machine-specific literals (device name, Mac user name) for uploads: whole words only; the shells do not pass generic names such as a bare "iPhone"
static NSData *dm_diagnostic_scrub_literals(NSData *data, NSArray<NSString *> *literals) {
    NSString *text = data ? [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding] : nil; if (!text) return data;
    for (NSString *lit in literals) { if (lit.length < 2) continue;
        NSRegularExpression *r = [NSRegularExpression regularExpressionWithPattern:[NSString stringWithFormat:@"(?<![\\p{L}\\p{N}_])%@(?![\\p{L}\\p{N}_])", [NSRegularExpression escapedPatternForString:lit]] options:0 error:nil];
        text = [r stringByReplacingMatchesInString:text options:0 range:NSMakeRange(0, text.length) withTemplate:@"<device>"]; }
    return [text dataUsingEncoding:NSUTF8StringEncoding];
}
// 上传专用的再一道（上传诊断报告，dm_sharelog.m / host/diagup_mac.m）：在上面整份脱敏之后，再抹网址、IP 地址、带令牌 / 票据 / 口令 / 私钥字样的整行。
// 本地导出不走这一道（玩家自己发给谁由他定，原导出内容不变）；上传不靠服务端再脱敏。
// Upload-only extra pass (diagnostic upload, dm_sharelog.m / host/diagup_mac.m): after the full scrub above, also blank URLs, IP addresses and whole lines mentioning tokens / tickets / passwords / private keys.
// The local export does not take this pass (the player decides where it goes; its content is unchanged); the upload does not rely on the server scrubbing again.
static NSData *dm_diagnostic_scrub_upload(NSData *data) {
    NSString *text = data ? [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding] : nil; if (!text) return data;
    NSRegularExpression *line = [NSRegularExpression regularExpressionWithPattern:@"^.*(?:authorization|bearer\\s|cookie|private[ _-]?key|passw(?:or)?d|secret|(?:device|host|reader|operator|access|refresh)[ _-]?token|ticket\\s*[:=]|-----BEGIN ).*$"
                                                                        options:NSRegularExpressionCaseInsensitive | NSRegularExpressionAnchorsMatchLines error:nil];
    text = [line stringByReplacingMatchesInString:text options:0 range:NSMakeRange(0, text.length) withTemplate:@"<sensitive-line-redacted>"];
    NSArray<NSArray<NSString *> *> *rules = @[ @[ @"[a-z][a-z0-9+.-]*://[^\\s<>\"']+", @"<url>" ], @[ @"(?<![0-9.])(?:[0-9]{1,3}\\.){3}[0-9]{1,3}(?![0-9.])", @"<ip>" ],
                                              @[ @"(?<![0-9A-Za-z:])(?:(?:[0-9a-f]{1,4}:){3,7}[0-9a-f]{1,4}|(?:[0-9a-f]{1,4}(?::[0-9a-f]{1,4})*)?::[0-9a-f]{1,4}(?::[0-9a-f]{1,4})*|[0-9a-f]{1,4}(?::[0-9a-f]{1,4})*::)(?![0-9A-Za-z:])", @"<ip>" ] ];   // IPv6：完整至少四组，或带 :: 的缩写；两个冒号的时间戳不算 | IPv6: four or more full groups, or a :: form; two-colon timestamps are not
    for (NSArray<NSString *> *r in rules) {
        NSRegularExpression *re = [NSRegularExpression regularExpressionWithPattern:r[0] options:NSRegularExpressionCaseInsensitive error:nil];
        text = [re stringByReplacingMatchesInString:text options:0 range:NSMakeRange(0, text.length) withTemplate:r[1]];
    }
    return [text dataUsingEncoding:NSUTF8StringEncoding];
}
