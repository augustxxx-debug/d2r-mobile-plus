// 安卓真机探针：宿主方案在这台机器上成不成立，一次跑完、逐项报 OK / FAIL（接口见 dm_probe.h）。
// 用法：命令行 dmprobe [可写目录]（NDK 编，adb push 到 /data/local/tmp，shell 身份）；探针 App（dm/android/probe/app）在应用身份下跑同一套。
//   ① 身份：页大小、CPU 特性、内核 / 系统 / SoC 属性、SELinux、seccomp、内存与资源上限（支持记录的单位：型号 + RAM + 系统 build + GPU + 页大小 + 包哈希）；
//   ② bionic 键表（改写器 DM_TARGET=android 的线程槽取法）：多键、多线程并发、线程反复创建退出，都用一段运行时生成的补丁同款代码实读；
//   ③ 匿名页写入后改只读+执行能否执行（装载器 map_image 的做法）、反复改回去再改回来、RWX、文件映射执行（应用数据目录，预期被拒）；
//   ④ 门调用形状：生成代码经字面量指针调宿主 C 函数、发系统调用；计数器寄存器用户态可读否；
//   ⑤ x18：宿主线程里是什么、跨调度会不会被改；⑥ 新线程有没有备用信号栈；
//   ⑦ 信号与脏页：SIGSEGV 处理里 ESR（写位）在不在、写保护页 → 缺页 → 解保护 → 重执行（nvn_dirty.c 的路径），主线程与工作线程各一遍，非本模块的野地址能区分；
//   ⑧ 页语义：4 KB 子范围 mprotect 在 16 KB 系统上的表现、MADV_FREE + PROT_NONE 回收；
//   ⑨ 客体地址空间：照 hle_svc.c 预留 5120 MiB 逐档降级、开堆 3285 MiB、触页、回收，看 RSS；更大的 NORESERVE 保留只记录；
//   ⑩ 映像布局：代码 RX / 键页 RO / 数据 RW / 补丁区 RX 四段一次性照装载器切出来、执行与读写。
// Android device probe: whether the host's approach holds on this machine, run all at once and report OK / FAIL item by item (interface: dm_probe.h).
// Usage: command line dmprobe [writable dir] (built with the NDK, adb push to /data/local/tmp, shell identity); the probe App (dm/android/probe/app) runs the same set under the app identity.
//   (1) identity: page size, CPU features, kernel / system / SoC properties, SELinux, seccomp, memory and resource limits (the unit of a support record: model + RAM + system build + GPU + page size + package hash);
//   (2) the bionic key table (the thread slot access of rewriter DM_TARGET=android): many keys, concurrent threads, repeated thread creation and exit, all read through run-time generated code identical to the patch code;
//   (3) whether anonymous pages execute after being written and switched to read-only+execute (what the loader's map_image does), flipping back and forth, RWX, file-backed execution (app data dir, expected refused);
//   (4) gate call shape: generated code calling a host C function through a literal pointer, issuing a system call; whether the counter registers are readable from user space;
//   (5) x18: what it holds in host threads and whether scheduling changes it; (6) whether new threads have an alternate signal stack;
//   (7) signals and dirty pages: whether ESR (the write bit) is present in the SIGSEGV context, write-protected page -> fault -> unprotect -> re-execute (nvn_dirty.c's path) on the main and a worker thread, wild addresses outside our range are told apart;
//   (8) page semantics: a 4 KB sub-range mprotect on a 16 KB system, MADV_FREE + PROT_NONE reclaim;
//   (9) guest address space: reserve 5120 MiB stepping down as hle_svc.c does, open a 3285 MiB heap, touch pages, reclaim, watch RSS; larger NORESERVE reservations are recorded only;
//   (10) image layout: text RX / key page RO / data RW / patch RX cut out at once as the loader does, executed and read/written.
#include "dm_probe.h"
#include "../../host/dm_uctx.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <setjmp.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sched.h>
#include <sys/auxv.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/system_properties.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

static FILE *g_out; static int g_fail;
#define OK(...) do { fprintf(g_out, "OK    " __VA_ARGS__); fputc('\n', g_out); } while (0)
#define FAIL(...) do { fprintf(g_out, "FAIL  " __VA_ARGS__); fputc('\n', g_out); g_fail++; } while (0)
#define INFO(...) do { fprintf(g_out, "INFO  " __VA_ARGS__); fputc('\n', g_out); } while (0)
#define KV(k, ...) do { fprintf(g_out, "KV    %s=", k); fprintf(g_out, __VA_ARGS__); fputc('\n', g_out); } while (0)

static uint64_t now_ns(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec; }
static int read_file(const char *path, char *buf, size_t n) {   // 读小文件到 buf（去尾部换行），失败返回 0 | read a small file into buf (trailing newline stripped), 0 on failure
    int fd = open(path, O_RDONLY | O_CLOEXEC); if (fd < 0) return 0;
    ssize_t r = read(fd, buf, n - 1); close(fd); if (r < 0) return 0;
    buf[r] = 0; while (r > 0 && (buf[r - 1] == '\n' || buf[r - 1] == ' ')) buf[--r] = 0;
    return 1;
}
static long proc_kv(const char *path, const char *key) {   // /proc 里「key:  123 kB」形式的值 | a "key:   123 kB" value from /proc
    FILE *f = fopen(path, "r"); if (!f) return -1; char line[256]; long v = -1; size_t kl = strlen(key);
    while (fgets(line, sizeof line, f)) if (!strncmp(line, key, kl) && line[kl] == ':') { v = atol(line + kl + 1); break; }
    fclose(f); return v;
}
static long rss_kb(void) { return proc_kv("/proc/self/status", "VmRSS"); }

// ---- ① 身份 | identity ----
static void prop(const char *name) { char v[PROP_VALUE_MAX] = ""; __system_property_get(name, v); KV(name, "%s", v); }
static void probe_identity(void) {
    long pg = sysconf(_SC_PAGESIZE);
    KV("uid", "%d", (int)getuid()); KV("pid", "%d", (int)getpid()); KV("page_size", "%ld", pg); KV("auxv_pagesz", "%lu", getauxval(AT_PAGESZ));
    KV("cpu_count", "%ld", sysconf(_SC_NPROCESSORS_CONF));
    unsigned long hw = getauxval(AT_HWCAP), hw2 = getauxval(AT_HWCAP2);
    KV("hwcap", "%#lx", hw); KV("hwcap2", "%#lx", hw2);
    INFO("HWCAP：LSE 原子 %s，fp16 %s，crc32 %s，aes %s，sha2 %s；HWCAP2：bti %s，mte %s，sve2 %s", hw & (1ul << 8) ? "有" : "无", hw & (1ul << 10) ? "有" : "无",
         hw & (1ul << 7) ? "有" : "无", hw & (1ul << 3) ? "有" : "无", hw & (1ul << 6) ? "有" : "无", hw2 & (1ul << 17) ? "有" : "无", hw2 & (1ul << 18) ? "有" : "无", hw2 & (1ul << 1) ? "有" : "无");
    struct utsname u; if (!uname(&u)) { KV("kernel", "%s %s", u.release, u.machine); }
    char b[256];
    // 各核 MIDR（应用身份下 sysfs 可能不可读：记「不可读」） | MIDR per core (sysfs may be unreadable under the app identity: recorded as unreadable)
    { int n = (int)sysconf(_SC_NPROCESSORS_CONF); char last[64] = "", acc[512] = ""; int cnt = 0, unreadable = 0;
      for (int i = 0; i < n && i < 16; i++) { char p[96]; snprintf(p, sizeof p, "/sys/devices/system/cpu/cpu%d/regs/identification/midr_el1", i);
        if (!read_file(p, b, sizeof b)) { unreadable = 1; break; }
        if (strcmp(b, last)) { if (cnt) snprintf(acc + strlen(acc), sizeof acc - strlen(acc), "x%d ", cnt); snprintf(acc + strlen(acc), sizeof acc - strlen(acc), "%s", b); strncpy(last, b, sizeof last - 1); cnt = 0; }
        cnt++; }
      if (cnt) snprintf(acc + strlen(acc), sizeof acc - strlen(acc), "x%d", cnt);
      KV("midr_el1", "%s", unreadable ? "不可读" : acc[0] ? acc : "无"); }
    KV("mem_total_kb", "%ld", proc_kv("/proc/meminfo", "MemTotal")); KV("mem_available_kb", "%ld", proc_kv("/proc/meminfo", "MemAvailable"));
    KV("overcommit_memory", "%s", read_file("/proc/sys/vm/overcommit_memory", b, sizeof b) ? b : "不可读");
    KV("selinux_enforce", "%s", read_file("/sys/fs/selinux/enforce", b, sizeof b) ? b : "不可读");
    { FILE *f = fopen("/proc/self/attr/current", "r"); if (f) { if (fgets(b, sizeof b, f)) { b[strcspn(b, "\n")] = 0; KV("selinux_context", "%s", b); } fclose(f); } }
    KV("seccomp", "%ld", proc_kv("/proc/self/status", "Seccomp")); KV("threads_now", "%ld", proc_kv("/proc/self/status", "Threads"));
    struct rlimit rl; if (!getrlimit(RLIMIT_AS, &rl)) KV("rlimit_as", "%llu", (unsigned long long)rl.rlim_cur); if (!getrlimit(RLIMIT_DATA, &rl)) KV("rlimit_data", "%llu", (unsigned long long)rl.rlim_cur);
    if (!getrlimit(RLIMIT_NOFILE, &rl)) KV("rlimit_nofile", "%llu", (unsigned long long)rl.rlim_cur); if (!getrlimit(RLIMIT_STACK, &rl)) KV("rlimit_stack", "%llu", (unsigned long long)rl.rlim_cur);
    static const char *const props[] = { "ro.product.manufacturer", "ro.product.brand", "ro.product.model", "ro.product.device", "ro.product.name", "ro.build.fingerprint", "ro.build.version.release",
        "ro.build.version.sdk", "ro.build.version.security_patch", "ro.build.id", "ro.build.display.id", "ro.build.type", "ro.soc.manufacturer", "ro.soc.model", "ro.hardware", "ro.board.platform",
        "ro.product.cpu.abilist", "ro.product.cpu.pagesize.max", "ro.product.build.16k_page.enabled", "ro.hardware.vulkan", "ro.hardware.egl", "ro.gfx.driver.0", "ro.gfx.driver.1",
        "ro.boot.verifiedbootstate", "ro.debuggable", "ro.vendor.build.fingerprint", "ro.vendor.build.version.sdk", "ro.kernel.version", "ro.opengles.version" };
    for (size_t i = 0; i < sizeof props / sizeof *props; i++) prop(props[i]);
}

// ---- ② bionic 键表 | bionic key table ----
static uint8_t *tsd_base(void) { uint64_t tp; __asm__ volatile("mrs %0, tpidr_el0" : "=r"(tp)); return ((uint8_t **)tp)[-1]; }
static uint64_t g_tsd_off; static int g_tsd_ok;
static uint64_t kidx(pthread_key_t k) { return (uint32_t)k & 0x7fffffffu; }   // bionic 键号带第 31 位有效标记 | bionic key numbers carry a validity flag in bit 31
static pthread_key_t g_k1, g_k2;
static uint64_t slot_off(pthread_key_t k) { return g_tsd_off + kidx(k) * 16 + 8; }

static void probe_tsd(void) {
    void *const m1 = (void *)0x6d4d7d5d4d3d2d1dull, *const m2 = (void *)0x1d2d3d4d5d7d4d6dull;
    pthread_key_t pad[5]; for (int i = 0; i < 5; i++) pthread_key_create(&pad[i], NULL);   // 让被测键号不是 0 | so the tested key number is not 0
    if (pthread_key_create(&g_k1, NULL) || pthread_key_create(&g_k2, NULL)) { FAIL("pthread_key_create"); return; }
    pthread_setspecific(g_k1, m1);
    uint8_t *b = tsd_base(); uint64_t at = 0;
    for (uint64_t o = 8; o < 0x4000 && !at; o += 8) if (*(void **)(b + o) == m1) at = o;
    if (!at) { FAIL("bionic 键表：键 %d 的值不在 bionic_tls 前 16 KB", (int)g_k1); return; }
    g_tsd_off = at - kidx(g_k1) * 16 - 8;
    pthread_setspecific(g_k2, m2);
    if (*(void **)(b + slot_off(g_k2)) == m2) { g_tsd_ok = 1; OK("bionic 键表偏移 %#llx（键 %#x / %#x 都对得上）", (unsigned long long)g_tsd_off, (unsigned)g_k1, (unsigned)g_k2); KV("bionic_tsd_off", "%#llx", (unsigned long long)g_tsd_off); }
    else FAIL("bionic 键表：键 %d 推出的偏移 %#llx 对键 %d 不成立", (int)g_k1, (unsigned long long)g_tsd_off, (int)g_k2);
    // 高键号：再建 100 个键，每个按公式核一遍（bionic 键表有上限 BIONIC_PTHREAD_KEY_COUNT≈130，建满为止）
    // High key numbers: create 100 more keys and check each by the formula (bionic's table is capped at BIONIC_PTHREAD_KEY_COUNT ~ 130, stop when full)
    int made = 0, bad = 0; uint64_t maxidx = 0; pthread_key_t ks[100];
    for (int i = 0; i < 100; i++) { if (pthread_key_create(&ks[i], NULL)) break; made++; void *v = (void *)(0x7000000000ull + i); pthread_setspecific(ks[i], v);
        if (*(void **)(tsd_base() + slot_off(ks[i])) != v) bad++; if (kidx(ks[i]) > maxidx) maxidx = kidx(ks[i]); }
    if (!bad) OK("再建 %d 个键（最高键号 %llu）按公式全部读对", made, (unsigned long long)maxidx); else FAIL("高键号：%d / %d 个键按公式读错", bad, made);
    for (int i = 0; i < made; i++) pthread_key_delete(ks[i]);
}

// 运行时生成与 patch_nso.py（DM_TARGET=android）同款的取槽代码：mrs x0,tpidr_el0; ldur x0,[x0,#-8]; ldr x1,[keypage]; add x0,x0,x1; ldr x0,[x0]; ret
// Generate at run time slot-fetch code identical to patch_nso.py (DM_TARGET=android): mrs x0,tpidr_el0; ldur x0,[x0,#-8]; ldr x1,[keypage]; add x0,x0,x1; ldr x0,[x0]; ret
typedef uint64_t (*slotfn)(void);
static slotfn g_slotfn; static uint8_t *g_code;
static int emit_slot_code(uint8_t *p, uint64_t keyoff) {   // p：16 KB 代码页 + 16 KB 键页（与装载器的粒度一致）| p: 16 KB code + 16 KB key page (the loader's granularity)
    uint64_t *key = (uint64_t *)(p + 0x4000); key[0] = keyoff;
    uint32_t *c = (uint32_t *)p; int n = 0;
    c[n++] = 0xD53BD040 | 0;                        // mrs x0, tpidr_el0
    c[n++] = 0xF8400000 | ((-8 & 0x1FF) << 12) | 0; // ldur x0, [x0, #-8]
    { int32_t imm = (int32_t)(0x4000 - 4 * n) >> 2; c[n++] = 0x58000000 | ((imm & 0x7FFFF) << 5) | 1; }   // ldr x1, =key[0]
    c[n++] = 0x8B010000;                            // add x0, x0, x1
    c[n++] = 0xF9400000;                            // ldr x0, [x0]
    c[n++] = 0xD65F03C0;                            // ret
    __builtin___clear_cache((char *)p, (char *)(p + 4 * n));
    return n;
}
static void probe_exec(const char *scratch) {
    long pg = sysconf(_SC_PAGESIZE);
    size_t sz = 0x8000;
    uint8_t *p = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (p == MAP_FAILED) { FAIL("mmap 匿名页 errno %d", errno); return; }
    int n = emit_slot_code(p, g_tsd_ok ? slot_off(g_k1) : 8);
    if (mprotect(p, 0x4000, PROT_READ | PROT_EXEC)) { FAIL("mprotect 只读+执行被拒 errno %d（页大小 %ld）", errno, pg); KV("anon_rx", "refused"); return; }
    if (mprotect(p + 0x4000, 0x4000, PROT_READ)) { FAIL("mprotect 键页只读 errno %d", errno); return; }
    g_slotfn = (slotfn)p; g_code = p;
    uint64_t v = g_slotfn();
    if (g_tsd_ok && v == 0x6d4d7d5d4d3d2d1dull) { OK("匿名页改只读+执行后可执行，补丁同款取槽代码读到键 %d 的值", (int)g_k1); KV("anon_rx", "ok"); }
    else if (!g_tsd_ok) INFO("键表未定，取槽代码只验证了可执行（读到 %#llx）", (unsigned long long)v);
    else FAIL("补丁同款取槽代码读到 %#llx", (unsigned long long)v);
    // 反复 RX → RW（改一条指令）→ RX，100 轮：模拟装载器之外的运行时改码（hle_count 等可选桩） | RX -> RW (change one instruction) -> RX, 100 rounds: run-time code changes beyond the loader (optional stubs such as hle_count)
    int flips = 0, bad = 0; uint64_t t0 = now_ns();
    for (int i = 0; i < 100; i++) {
        if (mprotect(p, 0x4000, PROT_READ | PROT_WRITE)) { bad++; break; }
        ((uint32_t *)p)[n] = 0xD503201F;   // nop（追加在 ret 之后，不改行为） | nop appended after ret
        __builtin___clear_cache((char *)p, (char *)p + 4 * (n + 1));
        if (mprotect(p, 0x4000, PROT_READ | PROT_EXEC)) { bad++; break; }
        if (g_tsd_ok && g_slotfn() != 0x6d4d7d5d4d3d2d1dull) { bad++; break; }
        flips++;
    }
    if (!bad) OK("RX↔RW 来回改码 %d 轮都成（每轮约 %llu µs）", flips, (unsigned long long)((now_ns() - t0) / 1000 / (flips ? flips : 1))); else FAIL("RX↔RW 来回改码第 %d 轮失败 errno %d", flips + 1, errno);
    void *rwx = mmap(NULL, 0x4000, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON, -1, 0);
    INFO("RWX 匿名页：%s（宿主不依赖）", rwx == MAP_FAILED ? "被拒" : "允许"); KV("anon_rwx", "%s", rwx == MAP_FAILED ? "refused" : "allowed"); if (rwx != MAP_FAILED) munmap(rwx, 0x4000);
    // 文件映射执行：把同一段代码写进可写目录里的文件，mmap PROT_EXEC（应用数据目录按 SELinux 预期被拒 —— 装载器用的是匿名页，这里只记录）
    // File-backed execution: write the same code to a file in the writable dir and mmap PROT_EXEC (expected refused in the app data dir by SELinux -- the loader uses anonymous pages; recorded only)
    if (scratch) {
        char path[1024]; snprintf(path, sizeof path, "%s/dm_probe_exec.bin", scratch);
        int fd = open(path, O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
        if (fd < 0) INFO("文件映射执行：建不了 %s errno %d", path, errno);
        else {
            uint8_t *tmp = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0); emit_slot_code(tmp, g_tsd_ok ? slot_off(g_k1) : 8);
            if (write(fd, tmp, sz) != (ssize_t)sz) INFO("文件映射执行：写文件失败 errno %d", errno);
            void *fm = mmap(NULL, sz, PROT_READ | PROT_EXEC, MAP_PRIVATE, fd, 0); int e1 = errno;
            if (fm == MAP_FAILED) { INFO("文件映射执行：mmap PROT_EXEC 被拒 errno %d（预期：应用数据目录禁执行；宿主用匿名页，不受影响）", e1); KV("file_exec", "refused:%d", e1); }
            else { uint64_t fv = ((slotfn)fm)(); INFO("文件映射执行：允许（读到 %#llx）", (unsigned long long)fv); KV("file_exec", "allowed"); munmap(fm, sz); }
            void *fm2 = mmap(NULL, sz, PROT_READ, MAP_PRIVATE, fd, 0);
            if (fm2 != MAP_FAILED) { int r = mprotect(fm2, 0x4000, PROT_READ | PROT_EXEC); int e2 = errno; KV("file_mprotect_exec", "%s", r ? "refused" : "allowed"); INFO("文件映射后 mprotect 加执行：%s%s", r ? "被拒 errno " : "允许", r ? (snprintf(path, sizeof path, "%d", e2), path) : ""); munmap(fm2, sz); }
            munmap(tmp, sz); close(fd); snprintf(path, sizeof path, "%s/dm_probe_exec.bin", scratch); unlink(path);
        }
    }
}

// ---- ④ 门调用形状 | gate call shape ----
static uint64_t __attribute__((noinline)) host_gate_fn(uint64_t a, uint64_t b) { return a * 3 + b; }
static void probe_gate(void) {
    // 生成：stp x29,x30,[sp,#-16]!; ldr x16,=fn; blr x16; ldp x29,x30,[sp],#16; ret —— 补丁块调宿主门的形状（寄存器约定按 AAPCS64，x18 不碰）
    // Generate: stp x29,x30,[sp,#-16]!; ldr x16,=fn; blr x16; ldp x29,x30,[sp],#16; ret -- the shape of a patch block calling a host gate (AAPCS64, x18 untouched)
    uint8_t *p = mmap(NULL, 0x8000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0); if (p == MAP_FAILED) { FAIL("gate mmap"); return; }
    uint64_t *lit = (uint64_t *)(p + 0x4000); lit[0] = (uint64_t)(uintptr_t)host_gate_fn;
    uint32_t *c = (uint32_t *)p; int n = 0;
    c[n++] = 0xA9BF7BFD;   // stp x29, x30, [sp, #-16]!
    { int32_t imm = (int32_t)(0x4000 - 4 * n) >> 2; c[n++] = 0x58000000 | ((imm & 0x7FFFF) << 5) | 16; }   // ldr x16, =lit[0]
    c[n++] = 0xD63F0200;   // blr x16
    c[n++] = 0xA8C17BFD;   // ldp x29, x30, [sp], #16
    c[n++] = 0xD65F03C0;   // ret
    // 第二段：x8=172 getpid; svc #0; ret —— 生成代码发系统调用（seccomp 对匿名可执行页里的 svc 一视同仁） | second block: x8=172 getpid; svc #0; ret -- a system call from generated code
    uint32_t *s = (uint32_t *)(p + 0x100); int m = 0;
    s[m++] = 0xD2801588;   // mov x8, #172
    s[m++] = 0xD4000001;   // svc #0
    s[m++] = 0xD65F03C0;   // ret
    __builtin___clear_cache((char *)p, (char *)p + 0x200);
    if (mprotect(p, 0x4000, PROT_READ | PROT_EXEC) || mprotect(p + 0x4000, 0x4000, PROT_READ)) { FAIL("gate mprotect errno %d", errno); return; }
    uint64_t r = ((uint64_t (*)(uint64_t, uint64_t))p)(7, 5);
    if (r == 26) OK("生成代码经字面量指针 blr 调到宿主 C 函数，参数与返回值正确"); else FAIL("门调用返回 %llu（应为 26）", (unsigned long long)r);
    uint64_t pid = ((uint64_t (*)(void))(p + 0x100))();
    if (pid == (uint64_t)getpid()) OK("生成代码里的 svc #0（getpid）正常返回"); else FAIL("生成代码 svc 返回 %llu，getpid %d", (unsigned long long)pid, (int)getpid());
    munmap(p, 0x8000);
}
// 计数器寄存器：cntfrq_el0 / cntvct_el0 用户态读（改写器把客体的计数读改成门；这里记录原始指令在本机能否直接读，SIGILL 用 sigsetjmp 兜住）
// Counter registers: cntfrq_el0 / cntvct_el0 read from user space (the rewriter turns guest counter reads into gates; recorded whether the raw instruction works here, SIGILL caught with sigsetjmp)
static sigjmp_buf g_ill_jb; static volatile sig_atomic_t g_ill_hit;
static void on_ill(int sig) { (void)sig; g_ill_hit = 1; siglongjmp(g_ill_jb, 1); }
static void probe_counters(void) {
    struct sigaction sa = {.sa_handler = on_ill}, old; sigemptyset(&sa.sa_mask); sigaction(SIGILL, &sa, &old);
    uint64_t frq = 0, v1 = 0, v2 = 0;
    if (!sigsetjmp(g_ill_jb, 1)) { __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(frq)); __asm__ volatile("mrs %0, cntvct_el0" : "=r"(v1)); __asm__ volatile("isb; mrs %0, cntvct_el0" : "=r"(v2)); }
    sigaction(SIGILL, &old, NULL);
    if (g_ill_hit) { INFO("cntfrq_el0 / cntvct_el0 用户态读触发 SIGILL（计数走门，不影响）"); KV("cnt_el0", "sigill"); }
    else { INFO("cntfrq_el0 = %llu Hz，cntvct_el0 可读（%llu → %llu）", (unsigned long long)frq, (unsigned long long)v1, (unsigned long long)v2); KV("cntfrq_el0", "%llu", (unsigned long long)frq); }
    struct timespec res; clock_getres(CLOCK_MONOTONIC, &res); KV("clock_monotonic_res_ns", "%ld", res.tv_nsec);
}

// ---- ⑤⑥ 线程：x18、备用栈、多线程键表、创建退出 | threads: x18, alternate stack, keys across threads, create/exit churn ----
static uint64_t read_x18(void) { uint64_t v; __asm__ volatile("mov %0, x18" : "=r"(v)); return v; }
struct thr { uint64_t x18; uint64_t slot; int altstack; size_t altsize; int slot_ok; };
static void *thr_main(void *arg) {
    struct thr *t = arg;
    t->x18 = read_x18();
    stack_t ss; t->altstack = sigaltstack(NULL, &ss) == 0 && !(ss.ss_flags & SS_DISABLE); t->altsize = ss.ss_size;
    if (g_slotfn && g_tsd_ok) { pthread_setspecific(g_k1, (void *)0x5151); t->slot = g_slotfn(); t->slot_ok = t->slot == 0x5151; }
    return NULL;
}
static _Atomic int g_conc_bad, g_conc_go;
static void *thr_conc(void *arg) {   // 32 线程并发：各自设不同值、经生成代码读 10000 次，核「每线程各自一份」 | 32 threads at once: each sets its own value and reads it 10000 times through the generated code
    uint64_t me = 0x1000 + (uint64_t)(uintptr_t)arg; pthread_setspecific(g_k1, (void *)(uintptr_t)me);
    while (!atomic_load(&g_conc_go)) sched_yield();
    for (int i = 0; i < 10000; i++) { if (g_slotfn() != me) { atomic_fetch_add(&g_conc_bad, 1); break; } if ((i & 1023) == 0) sched_yield(); }
    return NULL;
}
static void *thr_churn(void *arg) { uint64_t me = (uint64_t)(uintptr_t)arg; pthread_setspecific(g_k1, (void *)(uintptr_t)me); return (void *)(uintptr_t)(g_slotfn() == me && tsd_base() + slot_off(g_k1) == (uint8_t *)tsd_base() + slot_off(g_k1)); }
static void probe_threads(void) {
    INFO("主线程 x18 = %#llx", (unsigned long long)read_x18());
    struct thr t = {0}; pthread_t th;
    if (pthread_create(&th, NULL, thr_main, &t)) { FAIL("pthread_create"); return; }
    pthread_join(th, NULL);
    INFO("新线程 x18 = %#llx（非 0 多半是 bionic 给的影子调用栈指针：宿主代码与系统库要用 x18，客体 x18 必须走线程槽，与苹果相同）", (unsigned long long)t.x18);
    if (g_slotfn && g_tsd_ok) { if (t.slot_ok) OK("新线程经生成代码取到本线程自己的槽值"); else FAIL("新线程取槽得 %#llx", (unsigned long long)t.slot); }
    if (t.altstack) OK("新线程有备用信号栈（%zu 字节）：SA_ONSTACK 的信号不压当前栈", t.altsize);
    else FAIL("新线程没有备用信号栈：客体线程要自己 sigaltstack，宿主信号处理一律 SA_ONSTACK");
    KV("thread_altstack", "%zu", t.altstack ? t.altsize : 0);
    uint64_t back = 0, changed = 0;
    for (int i = 0; i < 2000; i++) {
        __asm__ volatile("mov x9, x18\n" "movz x18, #0x5a5a\n" "mov x8, #124\n" "svc #0\n" "mov %0, x18\n" "mov x18, x9\n" : "=r"(back) : : "x0", "x8", "x9", "memory");
        if (back != 0x5a5a) changed++;
    }
    if (!changed) OK("x18 跨 2000 次系统调用 / 让出调度不被内核改写"); else FAIL("x18 跨系统调用被改 %llu / 2000 次", (unsigned long long)changed);
    if (!(g_slotfn && g_tsd_ok)) return;
    // 并发 | concurrent
    pthread_t ths[32]; atomic_store(&g_conc_go, 0); atomic_store(&g_conc_bad, 0); int made = 0;
    for (int i = 0; i < 32; i++) if (!pthread_create(&ths[i], NULL, thr_conc, (void *)(uintptr_t)i)) made++; else break;
    atomic_store(&g_conc_go, 1); for (int i = 0; i < made; i++) pthread_join(ths[i], NULL);
    if (made == 32 && !atomic_load(&g_conc_bad)) OK("32 线程并发各读自己的槽 10000 次无串线"); else FAIL("并发槽读：建了 %d 线程，%d 个读到别人的值", made, atomic_load(&g_conc_bad));
    // 反复创建退出 1000 轮（含大栈与小栈的线程）| create/exit 1000 rounds (including large- and small-stack threads)
    int bad = 0; uint64_t t0 = now_ns(); pthread_attr_t at; pthread_attr_init(&at);
    for (int i = 0; i < 1000; i++) { pthread_attr_setstacksize(&at, i % 3 == 0 ? 64 * 1024 : i % 3 == 1 ? 1 << 20 : 8 << 20); void *r = NULL;
        if (pthread_create(&th, &at, thr_churn, (void *)(uintptr_t)(0x9000 + i))) { bad++; continue; } pthread_join(th, &r); if (!r) bad++; }
    pthread_attr_destroy(&at);
    if (!bad) OK("线程创建退出 1000 轮，每轮经生成代码读到自己的槽（共 %llu ms）", (unsigned long long)((now_ns() - t0) / 1000000)); else FAIL("线程创建退出：%d / 1000 轮槽读错或建不了", bad);
    uint64_t *b2 = (uint64_t *)tsd_base(); (void)b2;
    if (*(void **)(tsd_base() + slot_off(g_k1)) == (void *)0x6d4d7d5d4d3d2d1dull) OK("1000 轮之后主线程键表偏移不变"); else FAIL("1000 轮之后主线程槽值变了");
}

// ---- ⑦ 信号与脏页 | signals and dirty pages ----
static uint8_t *g_sig_lo, *g_sig_hi; static _Atomic int g_sig_hits, g_sig_wnr, g_sig_esr_present, g_sig_foreign; static sigjmp_buf g_wild_jb; static volatile int g_wild_armed;
static void on_segv(int sig, siginfo_t *si, void *uc_) {
    ucontext_t *uc = uc_; uint8_t *a = si->si_addr; (void)sig;
    if (a >= g_sig_lo && a < g_sig_hi) {   // 我们的页：照 nvn_dirty.c 解保护后重执行 | our page: unprotect as nvn_dirty.c does and re-execute
        uint64_t esr = UC_ESR(uc); atomic_fetch_add(&g_sig_hits, 1); if (esr) atomic_store(&g_sig_esr_present, 1); if ((esr >> 6) & 1) atomic_fetch_add(&g_sig_wnr, 1);
        mprotect((void *)((uintptr_t)a & ~((uintptr_t)getpagesize() - 1)), (size_t)getpagesize(), PROT_READ | PROT_WRITE); return;
    }
    atomic_fetch_add(&g_sig_foreign, 1);
    if (g_wild_armed) siglongjmp(g_wild_jb, 1);   // 野地址：不是我们的，交回（这里用 longjmp 代替崩溃处理）| wild address: not ours, hand back (longjmp stands in for the crash handler)
    _exit(139);
}
static void *thr_dirty(void *arg) { volatile uint8_t *p = arg; p[100] = 7; return (void *)(uintptr_t)(p[100] == 7); }
static void probe_signals(void) {
    long pg = sysconf(_SC_PAGESIZE);
    struct sigaction sa = {.sa_sigaction = on_segv, .sa_flags = SA_SIGINFO | SA_ONSTACK}, old_segv, old_bus; sigemptyset(&sa.sa_mask);
    stack_t ss = {.ss_sp = malloc(65536), .ss_size = 65536}; stack_t old_ss; sigaltstack(&ss, &old_ss);
    if (sigaction(SIGSEGV, &sa, &old_segv) || sigaction(SIGBUS, &sa, &old_bus)) { FAIL("sigaction errno %d", errno); return; }
    struct sigaction chk; sigaction(SIGSEGV, NULL, &chk);
    INFO("登记后 SIGSEGV 处理器 = %s（ART 的 sigchain 会把应用登记的处理器排在它自己之后）", chk.sa_sigaction == on_segv ? "本探针" : "别的（被链住）"); KV("segv_handler_is_ours", "%d", chk.sa_sigaction == on_segv);
    uint8_t *p = mmap(NULL, (size_t)pg * 4, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0); if (p == MAP_FAILED) { FAIL("dirty mmap"); return; }
    memset(p, 1, (size_t)pg * 4); g_sig_lo = p; g_sig_hi = p + pg * 4;
    // 写保护 → 写 → 缺页 → 解保护 → 重执行（主线程）| write-protect -> write -> fault -> unprotect -> re-execute (main thread)
    atomic_store(&g_sig_hits, 0); atomic_store(&g_sig_wnr, 0); atomic_store(&g_sig_esr_present, 0);
    mprotect(p, (size_t)pg, PROT_READ); volatile uint8_t *vp = p; vp[10] = 42;
    int hits = atomic_load(&g_sig_hits);
    if (hits == 1 && vp[10] == 42) OK("主线程：写保护页上的写触发一次缺页，处理器解保护后重执行写成功"); else FAIL("主线程脏页：缺页 %d 次，值 %d", hits, vp[10]);
    if (atomic_load(&g_sig_esr_present)) { if (atomic_load(&g_sig_wnr) == 1) OK("信号现场带 ESR，写访问 WnR=1（crash.c 据此只把写缺页交给脏页跟踪）"); else FAIL("信号现场带 ESR 但写缺页 WnR=0"); }
    else FAIL("信号现场没有 ESR 记录（uc_mcontext.__reserved 无 ESR_MAGIC）：crash.c 会把所有缺页当读、脏页跟踪永远不接手 → 首次写被保护贴图页即崩");
    KV("esr_in_sigcontext", "%d", atomic_load(&g_sig_esr_present));
    // 读访问 WnR=0 | read access WnR=0
    atomic_store(&g_sig_hits, 0); atomic_store(&g_sig_wnr, 0); mprotect(p + pg, (size_t)pg, PROT_NONE); volatile uint8_t rv = p[pg + 5]; (void)rv;
    if (atomic_load(&g_sig_hits) == 1 && atomic_load(&g_sig_wnr) == 0) OK("读 PROT_NONE 页：缺页一次，WnR=0"); else INFO("读缺页：hits %d wnr %d", atomic_load(&g_sig_hits), atomic_load(&g_sig_wnr));
    // 工作线程 | worker thread
    atomic_store(&g_sig_hits, 0); mprotect(p + pg * 2, (size_t)pg, PROT_READ); pthread_t th; void *r = NULL;
    if (!pthread_create(&th, NULL, thr_dirty, p + pg * 2)) pthread_join(th, &r);
    if (r && atomic_load(&g_sig_hits) == 1) OK("工作线程：写保护页缺页一次、解保护后写成功（在它的备用栈上处理）"); else FAIL("工作线程脏页：r=%p hits %d", r, atomic_load(&g_sig_hits));
    // 并发：8 线程同时写同一保护页（处理器各自 mprotect，重复解保护无害）| concurrent: 8 threads write the same protected page at once
    atomic_store(&g_sig_hits, 0); mprotect(p + pg * 3, (size_t)pg, PROT_READ); pthread_t ths[8]; int okc = 0;
    for (int i = 0; i < 8; i++) pthread_create(&ths[i], NULL, thr_dirty, p + pg * 3);
    for (int i = 0; i < 8; i++) { void *rr = NULL; pthread_join(ths[i], &rr); okc += rr != NULL; }
    if (okc == 8) OK("8 线程同时写同一保护页：全部写成（缺页 %d 次）", atomic_load(&g_sig_hits)); else FAIL("并发写保护页：%d / 8 成功", okc);
    // 野地址：不是我们的页 → 处理器交回（longjmp），能区分 | wild address: not our page -> handler hands back (longjmp), told apart
    atomic_store(&g_sig_foreign, 0); g_wild_armed = 1;
    if (!sigsetjmp(g_wild_jb, 1)) { volatile uint8_t *wild = (uint8_t *)0x10; *wild = 1; }
    g_wild_armed = 0;
    if (atomic_load(&g_sig_foreign) == 1) OK("野地址写：处理器认出不是脏页范围，交回崩溃处理路径"); else FAIL("野地址写：foreign=%d", atomic_load(&g_sig_foreign));
    // 处理器进入时的信号掩码 / 栈：是否在备用栈上 | on the alternate stack?
    munmap(p, (size_t)pg * 4); g_sig_lo = g_sig_hi = NULL;
    sigaction(SIGSEGV, &old_segv, NULL); sigaction(SIGBUS, &old_bus, NULL); sigaltstack(&old_ss, NULL); free(ss.ss_sp);
}

// ---- ⑧ 页语义 | page semantics ----
static void probe_pages(void) {
    long pg = sysconf(_SC_PAGESIZE); KV("page_size", "%ld", pg);
    uint8_t *p = mmap(NULL, 0x10000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0); if (p == MAP_FAILED) { FAIL("pages mmap"); return; }
    int r = mprotect(p + 0x1000, 0x1000, PROT_READ); int e = errno;   // 4 KB 子范围 | a 4 KB sub-range
    if (pg == 4096) { if (!r) OK("4 KB 页系统：4 KB 粒度 mprotect 正常"); else FAIL("4 KB 页：mprotect 4 KB 子范围失败 errno %d", e); }
    else { if (r) { OK("16 KB 页系统：4 KB 子范围 mprotect 被拒 errno %d（EINVAL）——客体 4 KB 语义（QueryMemory / Map / 别名）必须按宿主页对齐，不能全局换常数", e); KV("mprotect_4k_on_16k", "einval"); }
           else { INFO("16 KB 页系统：4 KB 子范围 mprotect 竟成功（内核可能按页圆整）"); KV("mprotect_4k_on_16k", "accepted"); } }
    if (!r) mprotect(p + 0x1000, 0x1000, PROT_READ | PROT_WRITE);
    // MADV_FREE + PROT_NONE 回收（hle_svc UnmapPhysicalMemory 的做法）再开回来 | MADV_FREE + PROT_NONE reclaim (hle_svc UnmapPhysicalMemory) and reopen
    memset(p, 5, 0x10000); long r0 = rss_kb();
    if (madvise(p, 0x10000, MADV_FREE)) FAIL("MADV_FREE errno %d", errno);
    if (mprotect(p, 0x10000, PROT_NONE)) FAIL("PROT_NONE errno %d", errno);
    if (mprotect(p, 0x10000, PROT_READ | PROT_WRITE)) FAIL("重开 RW errno %d", errno);
    INFO("MADV_FREE + PROT_NONE + 重开 RW 成功（内容是否清零由内核决定：首字节 %d）；RSS %ld kB", p[0], r0);
    munmap(p, 0x10000);
    // mmap 返回地址的对齐 | alignment of mmap results
    void *q = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0); if (q != MAP_FAILED) { KV("mmap_align", "%lu", (unsigned long)((uintptr_t)q & 0xFFFF)); munmap(q, 0x1000); }
}

// ---- ⑨ 客体地址空间 | guest address space ----
static void probe_vm(void) {
    const size_t start = 5ull << 30, step = 128ull << 20, limit = 4864ull << 20;   // 同 hle_svc.c | as hle_svc.c
    uint8_t *as = MAP_FAILED; size_t got = 0; int tries = 0;
    for (size_t sz = start;; sz -= step) { tries++; as = mmap(NULL, sz, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0); if (as != MAP_FAILED) { got = sz; break; } INFO("预留 %zu MiB：mmap 失败 errno %d，降档", sz >> 20, errno); if (sz == limit) break; }
    if (as == MAP_FAILED) { FAIL("客体地址空间：%zu–%zu MiB 全部预留失败", limit >> 20, start >> 20); KV("guest_reserve_mib", "0"); return; }
    OK("客体地址空间预留 %zu MiB（第 %d 档）于 %p", got >> 20, tries, as); KV("guest_reserve_mib", "%zu", got >> 20);
    uint8_t *heap = as + 0x40000000; size_t heap_cap = got - 0x40000000, heap_sz = 3285ull << 20; if (heap_sz > heap_cap) heap_sz = heap_cap;
    long r0 = rss_kb(); uint64_t t0 = now_ns();
    if (mprotect(heap, heap_sz, PROT_READ | PROT_WRITE)) { FAIL("开堆 %zu MiB mprotect RW errno %d", heap_sz >> 20, errno); munmap(as, got); return; }
    OK("堆 %zu MiB 一次 mprotect 开成（%llu µs）", heap_sz >> 20, (unsigned long long)((now_ns() - t0) / 1000));
    size_t touch = 256ull << 20; long pg = sysconf(_SC_PAGESIZE); t0 = now_ns();
    for (size_t o = 0; o < touch; o += (size_t)pg) heap[o] = (uint8_t)o;
    long r1 = rss_kb(); uint64_t dt = now_ns() - t0;
    INFO("触碰堆前 %zu MiB：%llu ms，RSS %ld → %ld kB（+%ld）", touch >> 20, (unsigned long long)(dt / 1000000), r0, r1, r1 - r0); KV("touch_256mib_ms", "%llu", (unsigned long long)(dt / 1000000)); KV("rss_delta_touch_kb", "%ld", r1 - r0);
    if (r1 - r0 < (long)(touch >> 10) * 9 / 10) FAIL("触碰 256 MiB 后 RSS 只增 %ld kB（页没真正到位？）", r1 - r0);
    madvise(heap, touch, MADV_FREE); mprotect(heap, touch, PROT_NONE);
    long r2 = rss_kb(); INFO("MADV_FREE + PROT_NONE 后 RSS %ld kB（内核可能延迟回收）", r2); KV("rss_after_free_kb", "%ld", r2);
    // 栈区 / 别名区在预留块内各开一段 | open a stack and an alias range inside the reserved block
    if (mprotect(as + 0x30000000, 1 << 20, PROT_READ | PROT_WRITE) || mprotect(as + 0x10000000, 1 << 20, PROT_READ | PROT_WRITE)) FAIL("栈区 / 别名区 mprotect errno %d", errno); else OK("栈区与别名区各开 1 MiB 成功");
    munmap(as, got);
    for (int gb = 8; gb <= 64; gb *= 2) { size_t sz = (size_t)gb << 30; void *p = mmap(NULL, sz, PROT_NONE, MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1, 0);
        if (p == MAP_FAILED) { INFO("保留 %d GiB 地址（NORESERVE）：失败 errno %d", gb, errno); KV("noreserve_max_gib", "%d", gb / 2); break; } INFO("保留 %d GiB 地址（NORESERVE）：%p", gb, p); munmap(p, sz); if (gb == 64) KV("noreserve_max_gib", "64"); }
}

// ---- ⑩ 映像布局 | image layout ----
static void probe_image(void) {
    // 64 KiB：[0,16K) 代码 RX，[16K,32K) 键页 RO，[32K,48K) 数据 RW，[48K,64K) 补丁区 RX（装载器：text 到 16K 边界 RX、键页 RO、其余 RW、patch_off 起 RX）
    // 64 KiB: [0,16K) text RX, [16K,32K) key page RO, [32K,48K) data RW, [48K,64K) patch RX (the loader: text up to a 16K boundary RX, key page RO, the rest RW, from patch_off RX)
    uint8_t *img = mmap(NULL, 0x10000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0); if (img == MAP_FAILED) { FAIL("image mmap"); return; }
    uint64_t *data = (uint64_t *)(img + 0x8000); data[0] = 0x1122334455667788ull;
    // 补丁区代码：ldr x0, =data（字面量在补丁区末尾）; ldr x0,[x0]; ret | patch code: load data[0] through a literal at the end of the patch area
    uint32_t *c = (uint32_t *)(img + 0xC000); uint64_t *lit = (uint64_t *)(img + 0xFFF0); lit[0] = (uint64_t)(uintptr_t)data; int n = 0;
    { int32_t imm = (int32_t)(0x3FF0 - 4 * n) >> 2; c[n++] = 0x58000000 | ((imm & 0x7FFFF) << 5) | 0; } c[n++] = 0xF9400000; c[n++] = 0xD65F03C0;
    // 代码区：b 到补丁区（相对跳转 +0xC000） | text: branch to the patch area (+0xC000)
    uint32_t *t = (uint32_t *)img; t[0] = 0x14000000 | ((0xC000 >> 2) & 0x3FFFFFF);
    __builtin___clear_cache((char *)img, (char *)img + 0x10000);
    if (mprotect(img, 0x4000, PROT_READ | PROT_EXEC) || mprotect(img + 0x4000, 0x4000, PROT_READ) || mprotect(img + 0xC000, 0x4000, PROT_READ | PROT_EXEC)) { FAIL("映像四段 mprotect errno %d", errno); munmap(img, 0x10000); return; }
    uint64_t v = ((uint64_t (*)(void))img)();
    if (v == 0x1122334455667788ull) OK("映像布局：代码区跳补丁区、补丁区读数据区，四段权限照装载器切出后执行正确"); else FAIL("映像布局：读到 %#llx", (unsigned long long)v);
    data[1] = 9; if (data[1] == 9) OK("数据区在代码区 RX 后仍可写"); 
    munmap(img, 0x10000);
}

int dm_probe_run(FILE *out, const char *scratch) {
    g_out = out; g_fail = 0; setvbuf(out, NULL, _IONBF, 0);
    uint64_t t0 = now_ns();
    fprintf(out, "INFO  dm_probe v2（P1：CPU 与系统探针）\n");
    probe_identity();
    probe_tsd();
    probe_exec(scratch);
    probe_gate();
    probe_counters();
    probe_threads();
    probe_signals();
    probe_pages();
    probe_vm();
    probe_image();
    dm_probe_vulkan(out);
    KV("probe_ms", "%llu", (unsigned long long)((now_ns() - t0) / 1000000));
    fprintf(out, g_fail ? "RESULT FAIL %d\n" : "RESULT OK\n", g_fail);
    return g_fail;
}
