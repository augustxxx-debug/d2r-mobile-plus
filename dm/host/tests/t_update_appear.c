// 选角页版本标签与新版本检查要在「玩家落脚的第一页」触发外壳回调（给左安全区、查一次新版本）：触屏版是选角页（OpenPanel CharacterSelectPanel），
// 手柄版是原版手柄主菜单（OpenPanel MainMenuButtonRibbon，左侧按钮栏 + 角色预览）。原先只认选角页，手柄玩家直接从主菜单进游戏，外壳一直不给安全区，版本字一直不写（用户 2026-10-28）
// The character-select version label and the update check must fire the shell callback (left safe area, one update check) on the first page the player lands on: the character select page in the touch layout
// (OpenPanel CharacterSelectPanel), the original controller main menu in the controller layout (OpenPanel MainMenuButtonRibbon, button ribbon + character preview). Only the character select page counted before, so a controller
// player entering the game from the main menu never got the safe area and the version label was never written (user 2026-10-28)
#include "../dm_update.c"
#include <stdio.h>
static int checks, fails;
#define CHECK(x) do { checks++; if (!(x)) { fails++; fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); } } while (0)
static void cb(void) {}
int main(void) {
    dm_update_init(1, "1.0.0", cb);
    const char *pages[] = { "CharacterSelectPanel", "MainMenuButtonRibbon" };
    for (int i = 0; i < 2; i++) {
        atomic_store(&g_appear, 0); atomic_store(&g_dirty, 0);
        dm_update_on_uimsg("PanelManager", "OpenPanel", pages[i]);
        CHECK(atomic_load(&g_appear) == 1); CHECK(atomic_load(&g_dirty) == 30);
    }
    atomic_store(&g_appear, 0); dm_update_on_uimsg("PanelManager", "OpenPanel", "MainMenuPanel"); CHECK(atomic_load(&g_appear) == 0);   // 只重写字、不算页出现 | rewrite only, not a page appearance
    dm_update_on_uimsg("PanelManager", "OpenPanel", "SettingsPanel"); CHECK(atomic_load(&g_appear) == 0);
    printf("%s t_update_appear: %d checks, %d failed\n", fails ? "FAIL" : "PASS", checks, fails); return fails != 0;
}
