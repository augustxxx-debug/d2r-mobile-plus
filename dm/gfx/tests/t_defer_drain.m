// 延迟释放真正销毁（RC1 光追关真机长测：「已退役未销毁」缓冲 0 → 17 个 416 MB，显卡已分配 +423 MB 里 416 MB 在这里）。
// 客体线程的最外层自动释放池整局不排空：在它上面跑的代码若把退役对象放进当前池，对象移出了驻留集却永远销毁不了。
// 这里用一个一直不弹的外层池代表客体线程，三条路径各退役一块缓冲，要求「在外层池弹出之前」对象已经没了：
//   1. defer_drain 直接调（GPU 已做完那次提交）：查弱引用驻留表会自动释放对象——修后 defer_drain 自带池；
//   2. mtl_sub_begin 开头那次 defer_drain（对象在上一次完成回调之后才可放）：原先在压入提交池之前；
//   3. 上传诊断 up_diag：g_up_old.allObjects 是自动释放的强引用数组（里面是换下的 32 MB 暂存块）。
// Deferred release really destroys (RC1 RT-off device long test: "retired but not destroyed" buffers 0 → 17, 416 MB; 416 of the +423 MB of GPU allocated were here).
// The guest thread's outermost autorelease pool never drains during a game: code running on it that puts a retired object into the current pool leaves it out of the residency set but never destroyed.
// An outer pool that is never popped stands for the guest thread; each of three paths retires one buffer, and the object must be gone *before* the outer pool is popped:
//   1. defer_drain called directly (the GPU finished that submission): checking the weak residency table autoreleases the object - after the fix defer_drain has its own pool;
//   2. the defer_drain at the start of mtl_sub_begin (the object becomes releasable only after the last completion handler): it used to run before the submission pool was pushed;
//   3. the upload diagnostic up_diag: g_up_old.allObjects is an autoreleased array of strong references (the switched-out 32 MB staging blocks).
#include "../nvn/nvn_mtl.m"
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
static int fail, count;
#define EXPECT(name, want, got) do { long long w_=(long long)(want), g_=(long long)(got); count++; printf("%s 期望 %lld → 真结果 %lld %s\n", name, w_, g_, w_==g_?"✓":"✗"); fail += w_ != g_; } while (0)
static int alive(__weak id *w) { int a; @autoreleasepool { a = *w != nil; } return a; }   // 读弱引用本身也会自动释放：在自己的池里读 | reading a weak reference autoreleases too: read it inside its own pool
static id<MTLBuffer> retire_one(__weak id *w) {   // 建一块登记进驻留集的缓冲并退役（defer_obj_release），返回 nil；弱引用留给检查 | create a buffer registered in the residency set and retire it; the weak reference is kept for the check
    id<MTLBuffer> b = [g_dev newBufferWithLength:1 << 20 options:MTLResourceStorageModePrivate]; res_add(b); *w = b; defer_obj_release(b); return nil;
}
int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0); mtl_init();
    void *guest = objc_autoreleasePoolPush();   // 客体线程那个整局不排空的外层池 | the guest thread's outer pool that never drains during a game
    {   // 1. defer_drain 直接调 | defer_drain called directly
        __weak id w = nil; retire_one(&w);
        pthread_mutex_lock(&g_mu); g_done = g_fence + 1; pthread_mutex_unlock(&g_mu);   // 那次提交 GPU 已做完 | the GPU finished that submission
        defer_drain();
        EXPECT("1 defer_drain 之后、外层池弹出之前：退役缓冲已销毁", 0, alive(&w));
    }
    {   // 2. mtl_sub_begin 开头那次 defer_drain | the defer_drain at the start of mtl_sub_begin
        __weak id w = nil; retire_one(&w);
        pthread_mutex_lock(&g_mu); g_done = g_fence + 1; pthread_mutex_unlock(&g_mu);   // 完成回调已经跑过，对象只能在下一次提交开头放 | the completion handler already ran; the object can only be released at the next submission's start
        mtl_set_frame(2); mtl_sub_begin(); uint64_t f = mtl_sub_commit(); mtl_wait(f, 10000000000ull);
        EXPECT("2 mtl_sub_begin 之后、外层池弹出之前：退役缓冲已销毁", 0, alive(&w));
    }
    {   // 3. 上传诊断 up_diag | the upload diagnostic up_diag
        __weak id w = nil; id<MTLBuffer> b = [g_dev newBufferWithLength:1 << 20 options:MTLResourceStorageModePrivate]; w = b;
        if (!g_up_old) g_up_old = [NSHashTable weakObjectsHashTable];
        [g_up_old addObject:b]; g_up_swaps++;   // 换块次数变了：up_diag 打一行，数旧块还活着几块 | the switch count changed: up_diag logs a line counting how many old blocks are alive
        up_diag(); b = nil;
        EXPECT("3 up_diag 之后放掉最后一个引用、外层池弹出之前：旧暂存块已销毁", 0, alive(&w));
    }
    objc_autoreleasePoolPop(guest);
    printf("== %d 项，失败 %d ==\n", count, fail);
    return fail != 0;
}
