// 系统内存告警累计不封顶（RC1 闸 2 判据 ②「后 10 分钟系统内存告警 = 0」：17 Pro 60 分钟局 iOS 壳的【内存告警】逐条日志正好 50 条——main.m 一局封顶 50 条，判不了后 10 分钟）。
// 这里：调 60 次 mtl_note_memory_warning，内存读数行（mtl_mem_stat）末尾要有「内存告警累计 60」；源码级再核 main.m 在封顶 return 之前就调了 mtl_note_memory_warning（否则累计也会停在 50）。
// Uncapped total of system memory warnings (RC1 gate 2 criterion ② "0 system memory warnings in the last 10 minutes": in the 17 Pro 60-minute run the iOS shell's per-warning lines were exactly 50 - main.m caps them at 50 per run, so the last 10 minutes could not be judged).
// Here: call mtl_note_memory_warning 60 times; the memory readout line (mtl_mem_stat) must end with "内存告警累计 60"; at source level, main.m must call mtl_note_memory_warning before the capping return (otherwise the total would stop at 50 too).
#include "../nvn/nvn_mtl.m"
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
static int fail, count;
#define EXPECT(name, want, got) do { long long w_=(long long)(want), g_=(long long)(got); count++; printf("%s 期望 %lld → 真结果 %lld %s\n", name, w_, g_, w_==g_?"✓":"✗"); fail += w_ != g_; } while (0)
static char *slurp(const char *p) { FILE *f = fopen(p, "rb"); if (!f) return NULL; fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f); char *b = calloc(1, n + 1); fread(b, 1, n, f); fclose(f); return b; }
int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    for (int i = 0; i < 60; i++) mtl_note_memory_warning();
    char path[] = "/tmp/t_memwarn_XXXXXX"; int fd = mkstemp(path); if (fd < 0) return 1;
    fflush(stderr); int saved = dup(2); dup2(fd, 2);
    mtl_mem_stat(300, 2700, 0, 0, 0, 0);
    fflush(stderr); dup2(saved, 2); close(saved); close(fd);
    char *log = slurp(path); unlink(path);
    EXPECT("内存读数行末尾有「内存告警累计 60」（不封顶）", 1, log && strstr(log, "内存告警累计 60") != NULL);
    char *src = slurp("../../ios/Sources/main.m");
    const char *h = src ? strstr(src, "- (void)applicationDidReceiveMemoryWarning:") : NULL, *note = h ? strstr(h, "mtl_note_memory_warning();") : NULL, *cap = h ? strstr(h, "if (++n > 50) return;") : NULL;
    EXPECT("main.m 告警处理在封顶 return 之前调 mtl_note_memory_warning", 1, note && cap && note < cap);
    printf("== %d 项，失败 %d ==\n", count, fail);
    return fail != 0;
}
