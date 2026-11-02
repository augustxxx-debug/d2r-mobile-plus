// 单测（B122，crash.c 的先后）：围栏钩子和 3 秒闹钟要先于回溯。每例在子进程里装真的崩溃处理，父进程看钩子调到没有、有没有退出
// Unit test (B122, ordering in crash.c): the fence hook and the 3-second alarm come before the backtrace. Each case installs the real handler in a child; the parent checks the hook ran and the child exited
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
void crash_install(void); void crash_set_dump_hook(void (*fn)(void));
extern void (*g_crash_note_hook)(int);
static int g_w, fails, checks;
static void note(int s) { char c = 'N'; (void)s; if (write(g_w, &c, 1) < 0) {} }
static void dump_ok(void) {}
static void *hold_stderr(void *a) { flockfile(stderr); *(volatile int *)a = 1; for (;;) pause(); return 0; }
static void dump_hang(void) { for (;;) pause(); }
static void crash_now(void) { *(volatile int *)8 = 1; }
#if defined(__aarch64__)
static void crash_badfp(void) { __asm__ volatile("mov x29,#0x10\n mov x9,#8\n str xzr,[x9]\n"); }
#else
static void crash_badfp(void) { crash_now(); }
#endif
static void run(const char *name, int mode) {
    int fd[2]; if (pipe(fd)) exit(2);
    pid_t p = fork();
    if (!p) { close(fd[0]); g_w = fd[1]; int nul = open("/dev/null", O_WRONLY); dup2(nul, 2);
        crash_install(); g_crash_note_hook = note; crash_set_dump_hook(dump_ok);
        if (mode == 1) { volatile int ok = 0; pthread_t t; pthread_create(&t, 0, hold_stderr, (void *)&ok); while (!ok) usleep(1000); crash_now(); }
        else if (mode == 2) crash_badfp();
        else if (mode == 3) { crash_set_dump_hook(dump_hang); raise(SIGALRM); }
        else crash_now();
        _exit(0); }
    close(fd[1]); int st = 0, done = 0; for (int i = 0; i < 80; i++) { if (waitpid(p, &st, WNOHANG) == p) { done = 1; break; } usleep(100000); }
    if (!done) { kill(p, 9); waitpid(p, &st, 0); }
    char c = 0; ssize_t k = read(fd[0], &c, 1); close(fd[0]); checks += 2;
    if (k != 1) { fails++; printf("FAIL %s：钩子没调到 | hook not called\n", name); }
    if (!done) { fails++; printf("FAIL %s：8 秒没退 | no exit in 8 s\n", name); }
}
int main(void) {
    run("普通崩溃 | plain crash", 0); run("别的线程持 stderr 锁 | stderr lock held", 1); run("帧指针坏 | bad frame pointer", 2); run("SIGALRM 进来且导出卡住 | SIGALRM with a stuck dump", 3);
    printf("t_crash_order: %d checks, %d failed\n", checks, fails); return fails != 0;
}
