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
