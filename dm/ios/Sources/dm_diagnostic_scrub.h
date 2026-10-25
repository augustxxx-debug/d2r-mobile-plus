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
    for (NSString *name in characters) {
        NSString *pattern = [NSString stringWithFormat:@"(?<![\\p{L}\\p{N}_])%@(?![\\p{L}\\p{N}_])",[NSRegularExpression escapedPatternForString:name]];
        NSRegularExpression *r = [NSRegularExpression regularExpressionWithPattern:pattern options:0 error:nil];
        text = [r stringByReplacingMatchesInString:text options:0 range:NSMakeRange(0,text.length) withTemplate:@"<character>"];
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
    return [text dataUsingEncoding:NSUTF8StringEncoding];
}

static inline NSData *dm_diagnostic_scrub(NSData *data, NSString *home, NSString *bundle) {
    return dm_diagnostic_scrub_names(data,home,bundle,nil);
}
