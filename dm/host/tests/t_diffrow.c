// 「选项 → 游戏」页难度行（dm_padmode.c 借原版滑块）：滑块值 ↔ 人数档的换算（dm_hud.c），滑块下限 1 或 0 两种刻度、越界夹住、来回一致
// Difficulty row on the Options → Gameplay page (dm_padmode.c borrows the original slider): slider value <-> players level conversion (dm_hud.c), slider scales starting at 1 or 0, clamping, round trip
#include "../dm_hud.h"
#include <stdio.h>
static int checks, fails;
#define CHECK(x) do { checks++; if (!(x)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)
int main(void) {
    for (int v = 1; v <= 8; v++) { CHECK(dm_hud_slider_players(1, 8, v) == v); CHECK(dm_hud_slider_value(1, 8, v) == v); }   // 原版刻度 1..8 | original scale 1..8
    for (int v = 0; v <= 7; v++) { CHECK(dm_hud_slider_players(0, 7, v) == v + 1); CHECK(dm_hud_slider_value(0, 7, v + 1) == v); }   // 0 起的刻度 | zero-based scale
    CHECK(dm_hud_slider_players(1, 8, 0) == 1); CHECK(dm_hud_slider_players(1, 8, 9) == 8); CHECK(dm_hud_slider_players(0, 7, 20) == 8);   // 越界夹住 | clamped
    CHECK(dm_hud_slider_value(1, 8, 0) == 1); CHECK(dm_hud_slider_value(1, 8, 99) == 8); CHECK(dm_hud_slider_value(0, 7, 0) == 0);
    CHECK(dm_hud_slider_players(0, 0, 5) == 1); CHECK(dm_hud_slider_value(0, 0, 5) == 0);   // 坏刻度（下限 ≥ 上限）：当没读到，不乱算 | broken scale (lo >= hi): treated as unread
    for (int lo = 0; lo <= 1; lo++) for (int n = 1; n <= 8; n++) CHECK(dm_hud_slider_players(lo, lo + 7, dm_hud_slider_value(lo, lo + 7, n)) == n);   // 来回一致 | round trip
    printf("t_diffrow: %d checks, %d fails\n", checks, fails);
    return fails != 0;
}
