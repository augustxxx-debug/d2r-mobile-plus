#pragma once
#include <stdint.h>
// B163 读档后「看不见的手上物品」的纯函数（宿主 dm_bag.c 与单测共用）。
// 原版把存盘时手上的物品存成「在光标上」，读档后它挂在服务端玩家物品栏的光标上（inv + 0x40），客户端手上在载入收尾时被清空；
// 两端持续不一致满 50 拍就该把它放回背包页（0x18），背包页放不下就丢到脚边（0x17）。
// Pure helpers for B163's "invisible held item" after loading (shared by the host dm_bag.c and the unit test).
// The original saves an item held at save time as "on the cursor"; after loading it hangs on the server player's inventory cursor
// (inv + 0x40), while the client hand is cleared at the end of loading. When the two sides stay inconsistent for 50 ticks the item
// should go back to the bag page (0x18), or be dropped at the player's feet if the bag page is full (0x17).
enum { DM_CURSOR_RESCUE_TICKS = 50 };   // 两端不一致持续多少拍才动手（约 2 秒） | how many ticks of inconsistency before acting (about 2 seconds)

// 服务端有、客户端空时 streak 加一，否则清零；到 50 拍返回 1 并把 streak 清零。server_has / client_held：非零 = 拿着。
// streak increments while the server holds and the client hand is empty, and is cleared otherwise; returns 1 on the 50th tick and resets streak.
// server_has / client_held: nonzero = holding.
static inline int dm_cursor_rescue_due(int server_has, int client_held, int *streak) {
    if (!streak) return 0;
    if (!server_has || client_held) { *streak = 0; return 0; }
    if (++*streak >= DM_CURSOR_RESCUE_TICKS) { *streak = 0; return 1; }
    return 0;
}

// B205 救回去处（宿主照 dm_cursor_rescue_plan 走）：已鉴定的护身符先放护身符页（页 5），放不下或别的物品放背包页（页 0），都放不下才落地。
// 护身符页只属于护身符，非护身符不看 charm_room；place_held 内部会再查一次空位，可能失败，宿主失败后退回背包页、再不行才落地。
// B205 rescue destination (the host follows dm_cursor_rescue_plan): an identified charm goes to the charm page (page 5) first, otherwise or
// when that fails the bag page (page 0), and only when neither has room does it drop. The charm page is only for charms, so a non-charm
// ignores charm_room; place_held checks the room again internally and may fail, so the host falls back to the bag page and then to a drop.
enum { DM_RESCUE_CHARM = 0, DM_RESCUE_BAG = 1, DM_RESCUE_DROP = 2 };
static inline int dm_cursor_rescue_plan(int is_charm, int charm_room, int bag_room) {
    if (is_charm && charm_room) return DM_RESCUE_CHARM;
    if (bag_room) return DM_RESCUE_BAG;
    return DM_RESCUE_DROP;
}

// 服务端光标上那件物品的模式（item + 0xc）为 0 表示它已经在某个容器里，光标指针只是残留（把魔盒拖进仓库后常见）；
// 这一拍不动手，免得背包满时把它丢到脚边。地上是 3；救回动手时日志记下光标物品的模式，供整机核对。
// Mode 0 (item + 0xc) means the server cursor item is already inside some container and the cursor pointer is only stale (common after
// dragging the cube into the stash); do not act on such a tick, or a full bag would drop an item that is already stored. Ground is 3;
// the rescue log records the cursor item's mode when it acts, for checking in whole-game runs.
static inline int dm_cursor_rescue_stale(uint32_t mode) { return mode == 0; }

static inline void dm_cursor_rescue_put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

// 「放进容器」包 0x18，17 字节：18 [物品 id][x][y][页]，都是 4 字节小端，页 = 0（背包页）。
// 与 dm_bag.c 的 dm_bag_c2s 解析的格式相同；页号 0 不会被护身符页接管。
// "put into container" packet 0x18, 17 bytes: 18 [item id][x][y][page], all 4-byte little-endian, page = 0 (bag page).
// Same layout dm_bag_c2s parses in dm_bag.c; page 0 is never taken over by the charm page.
static inline void dm_cursor_rescue_place_pkt(uint8_t pk[17], uint32_t id, int x, int y) {
    for (int i = 0; i < 17; i++) pk[i] = 0;
    pk[0] = 0x18;
    dm_cursor_rescue_put32(pk + 1, id);
    dm_cursor_rescue_put32(pk + 5, (uint32_t)x);
    dm_cursor_rescue_put32(pk + 9, (uint32_t)y);
    dm_cursor_rescue_put32(pk + 13, 0);   // 页 = 背包页 | page = bag page
}

// 「丢下光标物品」包 0x17，5 字节：17 [物品 id]（4 字节小端）。
// 依据一局真实记录（11-04 大背包验收录下的客户端→服务端包 c2s.json）：
// {"tick":406,"unit":1,"len":5,"pkt":"1726000000"}（物品 id 0x26）；dm_actions.c / dm_cursor.c 的注释也记录面板外按返回时原版紧跟着发 0x17。
// "drop the cursor item" packet 0x17, 5 bytes: 17 [item id] (4-byte little-endian).
// Evidence: a real recording in that c2s.json (item id 0x26); comments in dm_actions.c / dm_cursor.c also note the original sends 0x17 right after
// pressing Back outside a panel.
static inline void dm_cursor_rescue_drop_pkt(uint8_t pk[5], uint32_t id) {
    pk[0] = 0x17;
    dm_cursor_rescue_put32(pk + 1, id);
}
