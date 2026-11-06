// B202 宿主级行为用例：护身符页「进局读回 → 快照 → 提交写回」在同一进程里重进同一角色时件数不变。
// 直接编进 dm_bag.c（#include），调它自己的 charm_load / charm_snapshot / charm_pre_commit；游戏一侧用假的：
//   假背包（物品链、页 5、格子）、dm_item_make / dm_item_write 桩；原版函数地址（b + 偏移）处放两条指令的桩（MAP_JIT：放进背包返回 1，其余直接返回）。
// 修前（快照的上次签名是函数内 static，读回时不清）第二次提交写回 0 件 → 红；修后写回 K 件 → 绿。
// B202 host-level behaviour test: the charm page "load on entry -> snapshot -> write back on commit" keeps its count when the same character is re-entered in one process.
// dm_bag.c is compiled in (#include) and its own charm_load / charm_snapshot / charm_pre_commit are called; the game side is fake: a fake inventory (item chain, page 5, cells),
// dm_item_make / dm_item_write stubs, and two-instruction stubs at the original function addresses (b + offset; MAP_JIT: placing into the inventory returns 1, the rest just return).
#include "../dm_bag.c"
#include <libkern/OSCacheControl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("  ✗ %s:%d ", __func__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

enum { K = 6 };
static uint8_t g_unit[K][0x100], g_data[K][0x100], g_pos[K][0x40], g_inv[0x100], g_player[0x100], g_ppos[0x40], g_room[8], g_G[0x200];
static char g_wd[512];
static int g_make_next;
// 原版「造物品」：真游戏里新物品由放进背包那一步挂进物品链；假背包里 K 件本来就在链上，按次序交出 | the real game links the new item in when placing it; here the K items are already on the chain and are handed out in order
uint64_t dm_item_make(uint8_t *b, uint64_t G, uint64_t room, const uint8_t *buf, int len, int *socks_out) { (void)b; (void)G; (void)room; (void)buf; (void)len; (void)socks_out; return g_make_next < K ? (uint64_t)(uintptr_t)g_unit[g_make_next++] : 0; }
int dm_item_write(uint8_t *b, uint64_t it, uint8_t *buf) { (void)b; for (int i = 0; i < K; i++) if ((uint64_t)(uintptr_t)g_unit[i] == it) return snprintf((char *)buf, VB, "CHARMBITS-%02d|", i); return 0; }
void *dm_memfind(const uint8_t *h, size_t hn, const uint8_t *n, size_t nn) { return memmem(h, hn, n, nn); }
const char *dm_save_workdir(void) { return g_wd; }
void (*g_dm_pre_commit_hook[6])(const char *);

static uint8_t *fake_game(void) {   // 原版函数偏移处放桩 | stubs at the original function offsets
    static const uint32_t offs[] = { 0x560740, 0x642f70, 0x645820, 0x64b220, 0x5cadb0, 0x658be0 }; size_t sz = 0x1e00000;
    uint8_t *b = mmap(NULL, sz, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON | MAP_JIT, -1, 0); if (b == MAP_FAILED) { perror("mmap"); exit(2); }
    pthread_jit_write_protect_np(0);
    for (size_t i = 0; i < sizeof offs / sizeof *offs; i++) { uint32_t *p = (uint32_t *)(b + offs[i]); p[0] = offs[i] == 0x560740 ? 0x52800020u /* mov w0, #1 */ : 0xd503201fu /* nop */; p[1] = 0xd65f03c0u /* ret */; }
    pthread_jit_write_protect_np(1); sys_icache_invalidate(b, sz); return b;
}
static void fake_player(void) {   // 偏移同 dm_bag.c：玩家 +0x90 背包、+0x38 位置（+0x20 房间）、+0x10 名字；物品 +8 id、+0xc 容器、+0x10 数据（+0x55 页、+0xb0 下一件）、+0x38 位置（+0x10 x、+0x14 y）
    static const char name[] = "Tst";
    *(uint64_t *)(g_player + 0x90) = (uint64_t)(uintptr_t)g_inv; *(uint64_t *)(g_player + 0x38) = (uint64_t)(uintptr_t)g_ppos; *(uint64_t *)(g_ppos + 0x20) = (uint64_t)(uintptr_t)g_room; *(uint64_t *)(g_player + 0x10) = (uint64_t)(uintptr_t)name;
    *(uint32_t *)g_inv = 0x01020304; *(uint64_t *)(g_inv + 0x10) = (uint64_t)(uintptr_t)g_unit[0];
    for (int i = 0; i < K; i++) { *(uint32_t *)(g_unit[i] + 8) = 1000 + (uint32_t)i; *(uint32_t *)(g_unit[i] + 0xc) = 0; *(uint64_t *)(g_unit[i] + 0x10) = (uint64_t)(uintptr_t)g_data[i]; *(uint64_t *)(g_unit[i] + 0x38) = (uint64_t)(uintptr_t)g_pos[i];
        g_data[i][0x55] = CHARM_PAGE; *(uint64_t *)(g_data[i] + 0xb0) = i + 1 < K ? (uint64_t)(uintptr_t)g_unit[i + 1] : 0; *(int32_t *)(g_pos[i] + 0x10) = i % COLS; *(int32_t *)(g_pos[i] + 0x14) = i / COLS; }
}
static int charm_file_count(void) { char p[700]; snprintf(p, sizeof p, "%s/Tst/Tst.swcharm", g_wd); FILE *f = fopen(p, "rb"); if (!f) return -1; uint8_t h[10]; int n = fread(h, 1, 10, f) == 10 && !memcmp(h, "SWCH", 4) ? h[8] | h[9] << 8 : -1; fclose(f); return n; }
static void enter(uint8_t *b) { strlcpy(g_char, "Tst", sizeof g_char); g_make_next = 0; charm_load(b, (uint64_t)(uintptr_t)g_G, (uint64_t)(uintptr_t)g_player); g_loaded = 1; }   // 同 dm_bag_srv_tick 换玩家那一步 | as the player-change step of dm_bag_srv_tick

int main(void) {
    const char *td = getenv("TMPDIR"); snprintf(g_wd, sizeof g_wd, "%s/t_charm_reload_%d", td ? td : "/tmp", (int)getpid()); char d[700]; snprintf(d, sizeof d, "%s/Tst", g_wd); mkdir(g_wd, 0755); mkdir(d, 0755);
    { char p[700]; snprintf(p, sizeof p, "%s/Tst/Tst.d2s", g_wd); FILE *f = fopen(p, "wb"); fputs("D2SHEAD", f); for (int i = 0; i < K; i++) fprintf(f, "CHARMBITS-%02d|", i); fclose(f); }   // 角色档里 K 件都在 | all K items are in the character file
    uint8_t *b = fake_game(); fake_player(); uint64_t pl = (uint64_t)(uintptr_t)g_player;
    // ① 第一次进局（还没有护身符页文件 = 老档）→ 快照 → 提交：写回 K 件 | first entry (no charm file yet) -> snapshot -> commit: K written back
    enter(b); charm_snapshot(b, pl); charm_pre_commit(g_wd); CHECK(charm_file_count() == K, "第一次提交写回 %d 件，应为 %d", charm_file_count(), K);
    // ② 同一进程重进同一角色（一键重开 / 存档退出再进）：读回 K 件 → 快照 → 提交，件数不变 | re-enter the same character in one process: K read back -> snapshot -> commit, count unchanged
    enter(b); CHECK(g_nhist == 0, "读回时清单清空（%d）", g_nhist); charm_snapshot(b, pl); CHECK(g_nhist == K, "重进后快照重建清单 %d 件，应为 %d（B202）", g_nhist, K);
    charm_pre_commit(g_wd); CHECK(charm_file_count() == K, "重进后提交写回 %d 件，应为 %d（B202：修前写成 0 件）", charm_file_count(), K);
    // ③ 再来一轮：页 5 没变也不丢 | once more: nothing is lost while page 5 stays the same
    charm_snapshot(b, pl); charm_pre_commit(g_wd); CHECK(charm_file_count() == K, "再提交仍是 %d 件", charm_file_count());
    enter(b); charm_snapshot(b, pl); charm_pre_commit(g_wd); CHECK(charm_file_count() == K, "第三次进局后写回 %d 件", charm_file_count());
    char cmd[800]; snprintf(cmd, sizeof cmd, "rm -rf '%s'", g_wd); if (!fails) system(cmd);
    printf("t_charm_reload: %d 项检查，%d 项失败\n", checks, fails); return fails != 0;
}
