// 画质配置迁移、持久化与旧布尔键兼容：只使用临时设置文件，不读游戏存档。
// Test profile migration, persistence and legacy boolean compatibility using temporary settings only, never game saves.
#include "../dm_custom.c"
#include <unistd.h>
#include <sys/stat.h>
static int quality;
void dm_3d_quality_mode_set(int value) { quality=value; }
int dm_lang_group(void) { return DM_LG_EN; }
const char *dm_save_commitdir(void) { return NULL; }
int dm_3d_quality_mode_get(void) { return quality; }
float dm_3d_scale_cur(void) { return .75f; }
static int checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while (0)
int main(void) {
    char directory[]="/tmp/d2r-profile-XXXXXX"; if (!mkdtemp(directory)) return 1;
    char path[1024]; snprintf(path,sizeof path,"%s/sw_custom.txt",directory);
    const char *settings[]={"", "q3d=0\n", "q3d=1\n", "q3d=0\nq3d_v2=1\n", "q3d=1\nq3d_v2=1\n", "q3d=2\nq3d_v2=1\n", "q3d=3\nq3d_v2=1\n", "q3d=4\nq3d_v2=1\n", "q3d=5\nq3d_v2=1\n", "q3d=-1\nq3d_v2=1\n", "q3d=1oops\nq3d_v2=1\n"};
    const int expected[]={0,0,3,0,1,2,3,4,0,0,0};
    dm_custom_init(directory);
    for (unsigned i=0;i<sizeof settings/sizeof *settings;i++) {
        FILE *file=fopen(path,"w"); CHECK(file); fputs(settings[i],file); fclose(file);
        atomic_store(&g_loaded,0); CHECK(dm_3d_profile_get()==expected[i]); CHECK(quality==(expected[i]==3));
        save(); atomic_store(&g_loaded,0); CHECK(dm_3d_profile_get()==expected[i]);
        file=fopen(path,"r"); CHECK(file); char text[4096]={0}; fread(text,1,sizeof text-1,file); fclose(file); CHECK(strstr(text,"q3d_v2=1\n"));
    }
    // 应用才更新生效值；取消丢弃待选值，默认值也必须再应用。
    // Only Apply changes the live value; Discard drops pending values and Defaults also requires Apply.
    for (int mode=0;mode<5;mode++) {
        int previous=dm_3d_profile_get(); atomic_store(&g_pend[K_Q3D],mode);
        CHECK(dm_3d_profile_get()==previous);
        dm_custom_on_uimsg("OptionsPanelMessage","AcceptChanges",NULL);
        CHECK(dm_3d_profile_get()==mode); CHECK(quality==(mode==3));
    }
    atomic_store(&g_pend[K_Q3D],1);
    dm_custom_on_uimsg("OptionsPanelMessage","CancelChanges",NULL);
    CHECK(dm_3d_profile_get()==4); CHECK(atomic_load(&g_pend[K_Q3D])==-1);
    dm_custom_on_uimsg("OptionsPanelMessage","ResetToDefaults",NULL);
    CHECK(dm_3d_profile_get()==4); CHECK(atomic_load(&g_pend[K_Q3D])==0);
    dm_custom_on_uimsg("OptionsPanelMessage","AcceptChanges",NULL);
    CHECK(dm_3d_profile_get()==0);
    graphics_select_profile(2);
    CHECK(atomic_load(&g_pend[K_Q3D])==2); CHECK(dm_3d_profile_get()==0);
    dm_custom_on_uimsg("OptionsPanelMessage","AcceptChanges",NULL); CHECK(dm_3d_profile_get()==2);
    atomic_store(&g_pend[K_UPSCALE],0); dm_custom_on_uimsg("OptionsPanelMessage","AcceptChanges",NULL);
    CHECK(dm_3d_profile_get()==4); CHECK(atomic_load(&g_val[K_Q3D])==2);
    atomic_store(&g_pend[K_UPSCALE],1); dm_custom_on_uimsg("OptionsPanelMessage","AcceptChanges",NULL);
    CHECK(dm_3d_profile_get()==2);
    graphics_select_profile(-1); graphics_select_profile(4);
    CHECK(atomic_load(&g_pend[K_Q3D])==-1);
    for (int row=0;row<NK-3;row++) CHECK(!graphics_key(custom_key_at(row)));
    // Upstream defaults reset all enhanced preferences together, retaining language/voice.
    int auto_mana=key_index("auto_mana"); atomic_store(&g_val[auto_mana],0);
    atomic_store(&g_val[K_Q3D],3); atomic_store(&g_val[K_UPSCALE],0);
    int saved_lang=atomic_load(&g_val[0]);
    dm_custom_on_uimsg("OptionsPanelMessage","ResetToDefaults",NULL);
    CHECK(atomic_load(&g_pend[auto_mana])==1); CHECK(atomic_load(&g_pend[K_Q3D])==0);
    CHECK(atomic_load(&g_pend[K_UPSCALE])==1); CHECK(atomic_load(&g_pend[0])==-1);
#if VOICE_ROW
    CHECK(atomic_load(&g_pend[K_VOICE])==-1);
#endif
    CHECK(dm_3d_profile_get()==4); CHECK(enhanced_settings_dirty()); // Reset remains pending until Apply.
    dm_custom_on_uimsg("OptionsPanelMessage","CancelChanges",NULL);
    CHECK(atomic_load(&g_val[auto_mana])==0); CHECK(dm_3d_profile_get()==4);
    dm_custom_on_uimsg("OptionsPanelMessage","ResetToDefaults",NULL);
    dm_custom_on_uimsg("OptionsPanelMessage","AcceptChanges",NULL);
    CHECK(atomic_load(&g_val[auto_mana])==1); CHECK(dm_3d_profile_get()==0);
    CHECK(atomic_load(&g_val[0])==saved_lang); CHECK(!enhanced_settings_dirty());
    atomic_store(&g_pend[auto_mana],0); CHECK(enhanced_settings_dirty()); // Custom-only changes still enable Graphics Apply.
    dm_custom_on_uimsg("OptionsPanelMessage","CancelChanges",NULL); CHECK(!enhanced_settings_dirty());
    graphics_select_fps(0); CHECK(dm_fps_setting()==60); CHECK(pending_value(K_FPS)==1);
    dm_custom_on_uimsg("OptionsPanelMessage","AcceptChanges",NULL); CHECK(dm_fps_setting()==30);
    graphics_select_fps(1); dm_custom_on_uimsg("OptionsPanelMessage","CancelChanges",NULL); CHECK(dm_fps_setting()==30);
    graphics_select_fps(1); dm_custom_on_uimsg("OptionsPanelMessage","AcceptChanges",NULL); CHECK(dm_fps_setting()==60);
    graphics_select_fps(-1); graphics_select_fps(2); CHECK(atomic_load(&g_pend[K_FPS])==-1);
    unlink(path); rmdir(directory); printf("PASS render profiles: %d migration/persistence checks\n",checks); return 0;
}
