// 药水真堆叠（表见 dm/ios/tools/stack_potions.py）：宿主这边的部分，都在服务端，只调原版函数、只发原版包。
// 数量 = 物品属性 70（0x46，回城书、钥匙同一个）：读 main+0x5b9290(物品, 0x46, 0)，加减 main+0x5bc5a0(物品, 0x46, 差值, 0)，通知客户端 main+0x63cc80(客户端, 物品, 1, 0x46, 值, 0)（同原版 0x64f188–0x64f1bc）。
// 喝药：原版使用物品（命令 0x26，0x6d3xxx）摘下并释放整件，只改表喝一口整叠就没了。背包、腰带键、自动喝药、佣兵喂药都走这一个命令，所以在命令分派入口（diag.c c2s_obs）接住：
//   数量 ≥ 2 → 留一份位流 → 让原版处理这条包（能不能喝、药效、列内补位都是原版的）→ 物品没了就在原位建回数量少 1 的一叠。外层那条包换成原版会忽略的（目标类型非法，0x6d30a0 直接返回）。
//   没喝成（原版拒了）物品还在，什么都不做。
// 散瓶：商人和怪物给的是单瓶，旧存档读进来数量是 0。服务端拍里把背包同种散瓶并进一叠，封顶 99（DM_STACK_MAX）。
// 紫药：rvs / rvl 表里开着 AdvancedStashStackable（可进材料页），写档走 8 位那个字段（物品数据 +0x9c，dm_pouch.c），属性 70 不落盘。所以背包和腰带里的紫药把数量同步到 +0x9c，读档后再抄回属性。
// True potion stacking (tables in dm/ios/tools/stack_potions.py): the host side, all on the server, calling only original functions and sending only original packets.
// The count is item stat 70 (0x46, same as tomes and keys): read main+0x5b9290(item, 0x46, 0), adjust main+0x5bc5a0(item, 0x46, delta, 0), notify the client main+0x63cc80(client, item, 1, 0x46, value, 0) (same as the original 0x64f188–0x64f1bc).
// Drinking: the original use-item (command 0x26, 0x6d3xxx) detaches and frees the whole item, so with the tables alone one sip removes the stack. Inventory, belt keys, auto-drink and feeding the merc all go through this one command, so it is caught at the command dispatch entry (c2s_obs in diag.c):
//   count >= 2 -> keep a bitstream copy -> let the original handle this packet (whether it can be drunk, the effect and the column shift are all original) -> if the item is gone, rebuild a stack one smaller in place. The outer packet is replaced with one the original ignores (illegal target type, 0x6d30a0 returns at once).
//   If not drunk (rejected by the original) the item is still there and nothing is done.
// Loose bottles: vendors and monsters give single bottles and old saves load with count 0. In the server tick, loose bottles of a kind in the inventory merge into one stack, capped at 99 (DM_STACK_MAX).
// Rejuvenation: rvs / rvl have AdvancedStashStackable on in the table (they can go to the materials page), so saving uses that 8-bit field (item data +0x9c, dm_pouch.c) and stat 70 is not saved. So rejuvenation potions in the inventory and belt mirror the count to +0x9c, and it is copied back to the stat after loading.
// Test switch DM_TEST_STACK_LOG=<ticks>: every this many server ticks log the potions in the inventory and belt with their counts.
#include "hle.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DM_STACK_MAX 99
#define COMPACT 0x200000u   // 物品数据 +0x18 的简单物品位：写档看物品自己这一位，旧档读进来的药水要清掉，否则数量不落盘 | Simple-item bit in item data +0x18: saving looks at the item's own bit, so potions from old saves must clear it or the count is not saved
int dm_item_write(uint8_t *b, uint64_t it, uint8_t *buf); uint64_t dm_item_make(uint8_t *b, uint64_t G, uint64_t room, const uint8_t *buf, int len, int *socks_out); uint64_t dm_diag_srv_item(uint32_t id);
static uint8_t *B;
static int is_potion(uint64_t it) { int (*is)(uint64_t, int) = (int (*)(uint64_t, int))(B + 0x574bc0); return is(it, 77) || is(it, 78) || is(it, 79) || is(it, 80) || is(it, 81) || is(it, 82); }
static uint32_t code_of(uint64_t it) { return ((uint32_t (*)(uint64_t))(B + 0x570260))(it); }
static int is_rejuv(uint64_t it) { uint32_t c = code_of(it); const char *k = (const char *)&c; return k[0] == 'r' && k[1] == 'v' && (k[2] == 's' || k[2] == 'l'); }
static int qty(uint64_t it) { return ((int (*)(uint64_t, int, int))(B + 0x5b9290))(it, 0x46, 0); }
static void set_qty(uint64_t player, uint64_t it, int n) {
    ((void (*)(uint64_t, int, int, int))(B + 0x5bc5a0))(it, 0x46, n - qty(it), 0);   // 0x5bc5a0 是加减（原版 0x647558 传 -数量） | 0x5bc5a0 adds (the original at 0x647558 passes -count)
    uint64_t d = *(uint64_t *)(it + 0x10); if (d) { *(uint32_t *)(d + 0x18) &= ~COMPACT; if (is_rejuv(it)) *(uint8_t *)(d + 0x9c) = (uint8_t)(n >= 2 ? n : 0); }
    uint64_t pd = *(uint32_t *)player == 0 ? *(uint64_t *)(player + 0x10) : 0, cl = pd ? *(uint64_t *)(pd + 0xf0) : 0;
    if (cl) ((void (*)(uint64_t, uint64_t, int, int, int, int))(B + 0x63cc80))(cl, it, 1, 0x46, n, 0);
}
static uint64_t inv_first(uint64_t player) { uint64_t inv = *(uint64_t *)(player + 0x90); return inv && *(uint32_t *)inv == 0x01020304 ? *(uint64_t *)(inv + 0x10) : 0; }

// 放回：位流建物品，数量改成 n，放到 (mode, x, y)。腰带：列内已被原版补位，落点问原版腰带找位（同类那一列的最低空格）；背包：原格
// Put back: build the item from the bitstream, set the count to n, place at (mode, x, y). Belt: the column has been shifted by the original, so ask the original belt finder (lowest free cell of that kind's column); inventory: the same cell
static int put_back(uint64_t G, uint64_t player, const uint8_t *bits, int len, int n, uint32_t mode, int x, int y) {
    uint64_t pp = *(uint64_t *)(player + 0x38), room = pp ? *(uint64_t *)(pp + 0x20) : 0; if (!room) return 0;
    uint64_t nu = dm_item_make(B, G, room, bits, len, NULL); if (!nu) return 0;
    uint64_t nd = *(uint64_t *)(nu + 0x10), ip = *(uint64_t *)(nu + 0x38), inv = *(uint64_t *)(player + 0x90); if (!nd || !ip || !inv) { ((void (*)(uint64_t, uint64_t))(B + 0x658be0))(G, nu); return 0; }
    if (mode == 2) { uint32_t ns = 0; if (((int (*)(uint64_t, uint64_t, uint32_t *, int))(B + 0x561de0))(inv, nu, &ns, 1)) x = (int)ns; else { ((void (*)(uint64_t, uint64_t))(B + 0x658be0))(G, nu); return 0; } y = 0; }
    *(int32_t *)(ip + 0x10) = x; *(int32_t *)(ip + 0x14) = y;
    uint64_t one = nu, ox = 0; ((int (*)(uint64_t, uint64_t, uint64_t *, uint64_t *))(B + 0x6f1940))(G, player, &one, &ox);
    if (!*(uint64_t *)(nd + 0xa0)) { ((void (*)(uint64_t, uint64_t))(B + 0x658be0))(G, nu); return 0; }
    if (mode == 2) ((void (*)(uint64_t, uint64_t, uint64_t, int, int, int, int, int, uint64_t))(B + 0x642f70))(G, player, nu, 1, 0, 2, 0xff, 0, (uint64_t)(uint32_t)x);
    else ((void (*)(uint64_t, uint64_t, uint64_t, int, int, int, int, int, uint64_t))(B + 0x642f70))(G, player, nu, 1, 2, 0, 0, 0, (uint64_t)(x & 0xffff) | (uint64_t)y << 16);
    set_qty(player, nu, n); LOG("堆叠：放回 %.3s×%d（方式 %u，格 %d,%d，新 id %u）", (const char *)&(uint32_t){ code_of(nu) }, n, mode, x, y, *(uint32_t *)(nu + 8));
    return 1;
}

// 命令分派入口（diag.c c2s_obs，regs：x0 游戏、x1 玩家、x2 包、x3 长度，可改）。包字段照 main+0x2cecf0：+6 目标类型，+0xa 物品 id
// Command dispatch entry (c2s_obs in diag.c; regs: x0 game, x1 player, x2 packet, x3 length, writable). Fields as in main+0x2cecf0: +6 target type, +0xa item id
void dm_stack_c2s(uint64_t *regs) {
    static int inside; uint64_t G = regs[0], player = regs[1]; uint8_t *pk = (uint8_t *)(uintptr_t)regs[2]; uint32_t len = (uint32_t)regs[3];
    if (inside || !B || !G || !player || !pk || len < 0x24 || pk[0] != 0x26) return;
    uint32_t id; memcpy(&id, pk + 0xa, 4); uint64_t it = dm_diag_srv_item(id); if (!it || !is_potion(it)) return;
    int n = qty(it); if (n < 2) return;
    uint8_t buf[1024]; int bl = dm_item_write(B, it, buf); uint64_t pp = *(uint64_t *)(it + 0x38); uint32_t mode = *(uint32_t *)(it + 0xc), code = code_of(it); if (!bl || !pp) return;
    int x = *(int32_t *)(pp + 0x10), y = *(int32_t *)(pp + 0x14);
    inside = 1; ((void (*)(uint64_t, uint64_t, uint8_t *, uint32_t))(B + 0x6e5b40))(G, player, pk, len); inside = 0;
    uint64_t still = dm_diag_srv_item(id);
    if (still && *(uint64_t *)(still + 0x10)) { LOG("堆叠：%.3s×%d（id %u）原版没喝成，不动", (const char *)&code, n, id); }
    else if (!put_back(G, player, buf, bl, n - 1, mode, x, y)) LOG("【红】堆叠：%.3s 放不回去（方式 %u，格 %d,%d），丢了 %d 瓶", (const char *)&code, mode, x, y, n - 1);
    static uint8_t noop[0x24]; memcpy(noop, pk, sizeof noop); noop[6] = 0xff; regs[2] = (uint64_t)(uintptr_t)noop; regs[3] = sizeof noop;
}

// 摘下并释放（同百宝囊收进）
// Detach and free (same as stowing into the pouch)
static void remove_item(uint64_t G, uint64_t player, uint64_t it, int x, int y) {
    ((void (*)(uint64_t))(B + 0x55e430))(it);
    ((void (*)(uint64_t, uint64_t, uint64_t, int, int, int, int, int, int))(B + 0x642f70))(G, player, it, 0x20, 4, 0, 0, y, x);
    ((void (*)(uint64_t, uint64_t))(B + 0x658be0))(G, it);
}
static void normalize(uint64_t G, uint64_t player) {
    uint64_t inv = *(uint64_t *)(player + 0x90); if (inv && *(uint64_t *)(inv + 0x40)) return;   // 手上拿着东西：不动背包 | Something is in hand: leave the inventory alone
    uint64_t head[16] = { 0 }; int hq[16] = { 0 }, nh = 0, merged = 0;
    for (uint64_t it = inv_first(player), k = 0; it && k < 2048; k++) { uint64_t d = *(uint64_t *)(it + 0x10), nx = d ? *(uint64_t *)(d + 0xb0) : 0; if (!d) break;
        if (is_potion(it)) { uint32_t mode = *(uint32_t *)(it + 0xc); int q = qty(it);
            if (is_rejuv(it) && q >= 2 && *(uint8_t *)(d + 0x9c) != q) *(uint8_t *)(d + 0x9c) = (uint8_t)q;   // 原版并叠只改属性 | Native merging only changes the stat
            if (is_rejuv(it) && q < 2 && *(uint8_t *)(d + 0x9c) >= 2) { q = *(uint8_t *)(d + 0x9c); set_qty(player, it, q); LOG("堆叠：读档后紫药 %.3s 数量抄回 %d", (const char *)&(uint32_t){ code_of(it) }, q); }
            else if (q < 1) { q = 1; set_qty(player, it, 1); }
            if (mode == 0 && *(uint8_t *)(d + 0x55) == 0 && !merged && q < DM_STACK_MAX) { uint32_t c = code_of(it); int h = -1; for (int i = 0; i < nh && h < 0; i++) if (code_of(head[i]) == c && hq[i] < DM_STACK_MAX) h = i;
                if (h < 0) { if (nh < 16) { head[nh] = it; hq[nh++] = q; } }
                else { uint64_t pp = *(uint64_t *)(it + 0x38); int take = q; if (hq[h] + take > DM_STACK_MAX) take = DM_STACK_MAX - hq[h];
                    hq[h] += take; set_qty(player, head[h], hq[h]); if (take == q && pp) remove_item(G, player, it, *(int32_t *)(pp + 0x10), *(int32_t *)(pp + 0x14)); else set_qty(player, it, q - take);
                    LOG("堆叠：并进 %.3s，那一叠现在 %d", (const char *)&c, hq[h]); merged = 1; } } }   // 链表被动过，这一拍到此为止 | The list was changed, stop for this tick
        if (merged) break; it = nx; }
    if (merged) { int dm_request_save(void); dm_request_save(); }
}

void dm_stack_srv_tick(uint8_t *b, uint64_t G, uint64_t player) {
    B = b; if (!player) return;
    uint32_t tq = *(uint32_t *)(G + 0x170); if (!(tq % 5)) normalize(G, player);
    static int per = -1; if (per < 0) { const char *e = getenv("DM_TEST_STACK_LOG"); per = e ? atoi(e) : 0; }
    if (!per || tq % (uint32_t)per) return;
    char s[768]; int k = 0; s[0] = 0;
    for (uint64_t it = inv_first(player), n = 0; it && n < 2048 && k < (int)sizeof s - 40; n++) { uint64_t d = *(uint64_t *)(it + 0x10), pp = *(uint64_t *)(it + 0x38); if (!d) break;
        if (is_potion(it)) { uint32_t c = code_of(it); k += snprintf(s + k, sizeof s - k, " %.3s×%d/%u@%u/%u/%d,%d", (const char *)&c, qty(it), *(uint8_t *)(d + 0x9c), *(uint32_t *)(it + 0xc), *(uint8_t *)(d + 0x55), pp ? *(int32_t *)(pp + 0x10) : -1, pp ? *(int32_t *)(pp + 0x14) : -1); }
        it = *(uint64_t *)(d + 0xb0); }
    LOG("堆叠：拍 %u 药水（码×数量/+9c@方式/页/x,y）:%s", tq, s);
}
