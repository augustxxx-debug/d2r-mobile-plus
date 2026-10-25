// Bounded tail reads for diagnostic exports. Original logs remain untouched.
#import <Foundation/Foundation.h>
static NSData *dm_diagnostic_read_tail(NSString *path, NSUInteger limit, unsigned long long *omitted) {
    if (omitted) *omitted = 0;
    if (!limit) return [NSData data];
    NSFileHandle *file = [NSFileHandle fileHandleForReadingAtPath:path];
    if (!file) return nil;
    NSData *data = nil;
    @try {
        unsigned long long size = [file seekToEndOfFile];
        unsigned long long offset = size > limit ? size - limit : 0;
        [file seekToFileOffset:offset];
        data = [file readDataOfLength:limit];
        // A tail may start inside a UTF-8 character or a private identifier.
        // Drop the first partial line so neither gets exported in fragments.
        if (offset && data.length) {
            const unsigned char *bytes = data.bytes; NSUInteger start = 0;
            while (start < data.length && bytes[start] != '\n') start++;
            if (start < data.length) start++;
            data = [data subdataWithRange:NSMakeRange(start,data.length-start)];
            offset += start;
        }
        if (omitted) *omitted = offset;
    } @catch (NSException *exception) { (void)exception; data = nil; }
    @finally { [file closeFile]; }
    return data;
}
