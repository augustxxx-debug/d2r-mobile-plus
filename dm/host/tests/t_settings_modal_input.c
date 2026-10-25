// Execute the actual settings pumps: modal ownership must prevent key consumption.
#include "../dm_custom.c"
#include <assert.h>
module_t g_mod[MAX_MOD]; int g_nmod;
static unsigned char image[1];
static int blocked, key_reads;
static uint64_t keys;
int dm_ui_modal_active(uint64_t root) { assert(root); return blocked; }
int dm_padmode_current(void) { return 1; }
uint64_t dm_padmode_page_keys(uint64_t mask) { key_reads++;return keys&mask; }
uint32_t dm_ui_gen(void) { return 1; }
uint64_t dm_wref_get(dm_wref *ref) { return ref->gen==1 ? ref->w : 0; }
int dm_ui_top_chk_on(void) { return 0; }
void dm_ui_top_chk(uint64_t root,const char *name,uint64_t got) { (void)root;(void)name;(void)got;assert(0); }
void dm_3d_quality_mode_set(int mode) { (void)mode; }
int dm_lang_group(void) { return DM_LG_EN; }
const char *dm_str(uint8_t *base,const char *key) { (void)base;return key; }
const char *dm_save_commitdir(void) { return NULL; }
void dm_ui_wheel_on(uint64_t widget,int delta) { (void)widget;(void)delta;assert(0); }
uint64_t dm_guest_frame(void) { return 100; }
static _Alignas(8) unsigned char root[160],panel[1024];
static uint64_t children[1],vt[96];
int main(void) {
    strcpy(g_mod[0].name,"main");g_mod[0].base=image;g_nmod=1;
    for (int i=0;i<NK;i++) { atomic_store(&g_val[i],k_def[i]);atomic_store(&g_pend[i],-1); }
    atomic_store(&g_loaded,1);
    *(uint64_t *)(root+0x58)=(uint64_t)children;*(uint64_t *)(root+0x60)=1;
    children[0]=(uint64_t)panel;*(uint64_t *)panel=(uint64_t)vt;panel[0x50]=panel[0x51]=1;
    vt[0]=(uint64_t)image+0xae5f28;
    const char *names[]={"3PlusGraphicsOptionsPanel","VideoOptionsPanel"};
    for (int mode=0;mode<2;mode++) {
        *(const char **)(panel+8)=names[mode];
        blocked=1;keys=DM_BTN_A|DM_BTN_DOWN;key_reads=0;
        if (!mode) dm_graphics_ui_pump((uint64_t)root);else dm_custom_ui_pump((uint64_t)root);
        assert(key_reads==0 && !enhanced_settings_dirty());
        blocked=0;keys=0;
        if (!mode) dm_graphics_ui_pump((uint64_t)root);else dm_custom_ui_pump((uint64_t)root);
        assert(key_reads==1);
    }
    puts("PASS Settings pumps: modals retain keys; Graphics and Custom resume after dismissal without pending changes");
    return 0;
}
