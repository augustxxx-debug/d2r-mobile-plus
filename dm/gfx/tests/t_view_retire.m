// 贴图号视图缓存即时作废（RC1 资源审查：17 Pro 光追关 30 分钟局「已退役未销毁 贴图」0 → 514 个 34 MB，Mac 五幕轮换 0 → 261 个 18 MB）。
// 视图缓存（g_treg[].view）原先是惰性作废的：基贴图被重传换新、旧的退役后，要等下次查这个贴图号才发现并放掉旧视图；离开关卡后不再被查的号就一直攥着旧基贴图。
// 这里：两个贴图号各登记一张基贴图（一个带视图、一个不带视图 = 缓存里存的就是基贴图本身），各查一次建起缓存；用 serial_put 换掉基贴图、之后不再查这两个号，
// 跑三次提交，要求旧基贴图在外层池弹出之前就销毁；另一个没被换的号缓存的视图指针不变（没有误伤）；再查被换的号拿到的是新基贴图（或它的视图）。
// Immediate invalidation of the texture-ID view cache (RC1 resource review: 17 Pro RT-off 30-minute run "retired but not destroyed" textures 0 → 514, 34 MB; Mac five-act rotation 0 → 261, 18 MB).
// The view cache (g_treg[].view) used to be invalidated lazily: after a base texture was re-uploaded and the old one retired, the old view was only found and dropped on the next lookup of that texture ID; IDs no longer looked up after leaving a level kept holding the old base.
// Here: two texture IDs each register a base texture (one with a view, one without = the cache holds the base texture itself), each is looked up once to build the cache; serial_put replaces the bases and the IDs are not looked up again;
// three submissions run, and the old bases must be destroyed before the outer pool pops; a third ID whose base was not replaced keeps the same cached pointer (no collateral invalidation); looking the replaced ID up again yields the new base (or its view).
#include "../nvn/nvn_mtl.m"
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
static int fail, count;
#define EXPECT(name, want, got) do { long long w_=(long long)(want), g_=(long long)(got); count++; printf("%s 期望 %lld → 真结果 %lld %s\n", name, w_, g_, w_==g_?"✓":"✗"); fail += w_ != g_; } while (0)
static int alive(__weak id *w) { int a; @autoreleasepool { a = *w != nil; } return a; }   // 读弱引用本身也会自动释放：在自己的池里读 | reading a weak reference autoreleases too: read it inside its own pool
static tex_t mk(uint64_t serial) { tex_t t = {.serial = serial}; t.d.width = 8; t.d.height = 8; t.d.depth = 1; t.d.levels = 2; t.d.target = NVN_TEXTURE_TARGET_2D; t.d.format = NVN_FORMAT_RGBA8; return t; }
static id<MTLTexture> newtex(const tex_t *t) { MTLTextureDescriptor *d = tex_desc(t, 0); id<MTLTexture> x = [g_dev newTextureWithDescriptor:d]; res_add(x); return x; }
static void *lookup(int tid) { pthread_mutex_lock(&g_rmu); void *p = (__bridge void *)tex_by_id(tid); pthread_mutex_unlock(&g_rmu); return p; }
static void submit(void) { mtl_sub_begin(); uint64_t f = mtl_sub_commit(); mtl_wait(f, 10000000000ull); }
// 延迟释放有一部分在 GPU 完成回调线程上做，和 mtl_wait 返回有先后竞争：最多等 200 ms 再判（旧代码下旧基贴图永远不死，照样分得出） | part of deferred release runs on the GPU completion-handler thread and races mtl_wait's return: wait up to 200 ms before judging (with the old code the old base never dies, so it still tells them apart)
static int alive_after(__weak id *w) { for (int i = 0; i < 20 && alive(w); i++) usleep(10000); return alive(w); }
int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0); mtl_init();
    void *guest = objc_autoreleasePoolPush();   // 客体线程那个整局不排空的外层池 | the guest thread's outer pool that never drains during a game
    tex_t ta = mk(91001), tb = mk(91002), tc = mk(91003);
    __weak id wa = nil, wb = nil; void *keep_c = NULL;
    @autoreleasepool {
        id<MTLTexture> a1 = newtex(&ta), b1 = newtex(&tb), c1 = newtex(&tc); wa = a1; wb = b1;
        serial_put(ta.serial, a1); serial_put(tb.serial, b1); serial_put(tc.serial, c1);
        view_t v = {.set_levels = 1, .base_level = 1, .nlevels = 1};
        mtl_reg_tex(7001, &ta, &v);    // 带视图：缓存里是 a1 的视图（父贴图 a1） | with a view: the cache holds a view whose parent is a1
        mtl_reg_tex(7002, &tb, NULL);  // 不带视图：缓存里就是 b1 本身 | without a view: the cache holds b1 itself
        mtl_reg_tex(7003, &tc, &v);
        (void)lookup(7001); (void)lookup(7002); keep_c = lookup(7003);
        EXPECT("建缓存后 7001 的视图父贴图是 a1", 1, ((__bridge id<MTLTexture>)g_treg[7001].view).parentTexture == a1);
        EXPECT("建缓存后 7002 缓存的就是 b1", 1, (__bridge id)g_treg[7002].view == b1);
        id<MTLTexture> a2 = newtex(&ta), b2 = newtex(&tb);
        serial_put(ta.serial, a2); serial_put(tb.serial, b2);   // 重传换新：a1 / b1 退役（defer_obj_release） | re-upload: a1 / b1 retired
    }
    submit(); submit(); submit();   // 退役对象随提交排空；视图缓存应在这里作废 | retired objects drain with submissions; the view cache must be invalidated here
    EXPECT("换新后不再查 7001：旧基贴图 a1 已销毁（外层池弹出之前）", 0, alive_after(&wa));
    EXPECT("换新后不再查 7002：旧基贴图 b1 已销毁（外层池弹出之前）", 0, alive_after(&wb));
    EXPECT("没被换的 7003：缓存指针不变（没有误伤）", 1, g_treg[7003].view == keep_c);
    @autoreleasepool {
        id<MTLTexture> cur = serial_get(ta.serial); id<MTLTexture> got = (__bridge id<MTLTexture>)lookup(7001);
        EXPECT("再查 7001：拿到新基贴图的视图", 1, got.parentTexture == cur);
        EXPECT("再查 7002：拿到新基贴图", 1, (__bridge id)lookup(7002) == serial_get(tb.serial));
    }
    objc_autoreleasePoolPop(guest);
    printf("== %d 项，失败 %d ==\n", count, fail);
    return fail != 0;
}
