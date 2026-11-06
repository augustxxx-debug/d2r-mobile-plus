// B211：更新提示的渠道——iOS 26 包读版本文件顶层；旧接口版（DM_NVN_M3=1）只读 ios18 子对象，缺了 = 不提示（不算失败）。
// 本文件编两遍（-DDM_NVN_M3=0 / 1），以 Mac Catalyst 跑，直接调 dm_update.m 里的 upd_parse。
// B211: update channel -- the iOS 26 build reads the top level of the version file; the older-API build (DM_NVN_M3=1) reads only the ios18 sub-object, and
// without it shows no notice (not a failure). Built twice (-DDM_NVN_M3=0 / 1), run as Mac Catalyst, calling upd_parse in dm_update.m directly.
#include "../Sources/dm_update.m"
int g_dm_rt_ab;
void plog(const char *f, ...) { (void)f; }
void dm_update_offer(int b, const char *v, const char *n, int u) { (void)b; (void)v; (void)n; (void)u; }
void dm_update_fail(int k) { (void)k; }
int dm_community_manifest(const char *d, size_t n) { (void)d; (void)n; return 0; }
void dm_update_init(int b, const char *l, void (*f)(void)) { (void)b; (void)l; (void)f; }
void dm_update_safe_left(int u) { (void)u; }
void dm_update_site(const char *h) { (void)h; }
void dm_community_copy_init(int (*cb)(const char *)) { (void)cb; }
static int checks, fails;
#define CHECK(x) do { checks++; if (!(x)) { fails++; fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); } } while (0)
static NSString *parse(const char *js, int *b) { NSString *v = nil, *n = nil; int u = 0; *b = 0; return upd_parse([NSData dataWithBytes:js length:strlen(js)], b, &v, &n, &u); }
int main(void) { @autoreleasepool {
    int b; NSString *w;
    const char *top = "{\"build\":1793900000,\"note\":\"新版\",\"urgent\":false}";
    const char *both = "{\"build\":1793900000,\"note\":\"新版\",\"urgent\":false,\"ios18\":{\"build\":1793800000,\"note\":\"旧系统新版\",\"urgent\":true}}";
    const char *bad = "{\"build\":1793900000,\"note\":\"新版\",\"urgent\":false,\"ios18\":[1]}";
#if DM_NVN_M3
    w = parse(top, &b); CHECK(w == k_upd_no_channel && b == 0);        // 只有顶层：旧接口版不提示 | top level only: no notice
    w = parse(both, &b); CHECK(w == nil && b == 1793800000);          // 读子对象的构建号 | the sub-object's build
    w = parse(bad, &b); CHECK(w != nil && w != k_upd_no_channel);     // 子对象类型不对 = 格式错 | wrong type = bad format
#else
    w = parse(top, &b); CHECK(w == nil && b == 1793900000);           // iOS 26 包照旧读顶层 | the iOS 26 build reads the top level as before
    w = parse(both, &b); CHECK(w == nil && b == 1793900000);          // 有 ios18 也不看 | ignores ios18
    w = parse(bad, &b); CHECK(w == nil && b == 1793900000);
#endif
    printf("t_update_channel%s: %d 项检查，%d 项失败\n", DM_NVN_M3 ? "（旧接口版）" : "", checks, fails);
    return fails != 0; } }
