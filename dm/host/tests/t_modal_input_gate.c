// Exercise production modality detection using synthetic widget trees only.
#include "../dm_cursor.c"
#include <assert.h>
static _Alignas(8) unsigned char root[160], panel[160], child[160];
static uint64_t top[2], children[1];
static uint64_t ptr(void *p) { return (uint64_t)(uintptr_t)p; }
static void name(const char *s) { *(const char **)(panel+8)=s; }
int main(void) {
    *(uint64_t *)(root+0x58)=ptr(top);*(uint64_t *)(root+0x60)=1;top[0]=ptr(panel);
    *(uint64_t *)(panel+0x58)=ptr(children);*(uint64_t *)(panel+0x60)=1;children[0]=ptr(child);
    panel[0x50]=child[0x50]=panel[0x51]=child[0x51]=1;
    const char *modals[]={"ConfirmationModal","DismissableModal","DropGoldModal","ButtonBindingModal","ExampleErrorModal"};
    assert(!dm_ui_modal_active(0));
    for (unsigned i=0;i<sizeof modals/sizeof *modals;i++) {
        name(modals[i]);assert(dm_ui_modal_active(ptr(root)));
        panel[0x50]=0;assert(!dm_ui_modal_active(ptr(root)));panel[0x50]=1;
        child[0x50]=0;assert(!dm_ui_modal_active(ptr(root)));child[0x50]=1;
    }
    name("NotificationPanel");assert(!dm_ui_modal_active(ptr(root)));
    name("TooltipsPanel");assert(!dm_ui_modal_active(ptr(root)));
    name("SettingsPanel");assert(!dm_ui_modal_active(ptr(root)));
    *(uint64_t *)(root+0x60)=257;assert(!dm_ui_modal_active(ptr(root)));
    puts("PASS native modal input ownership: confirmation/error/gold/binding, hidden/empty states and nonmodal notices");
    return 0;
}
