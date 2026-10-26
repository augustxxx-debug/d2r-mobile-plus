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
    for (int row=0;row<NK-N_GFX;row++) CHECK(!graphics_key(custom_key_at(row)));
    // 渲染分辨率档位 render_res：读盘范围 0–3、非法回 0；应用才生效并通知游戏重读；放弃不通知；默认回 0 | Render resolution level: parsed 0–3, invalid falls back to 0; only Apply takes effect and notifies the game; Discard does not notify; Defaults return to 0
    { const char *rs[]={"render_res=2\n","render_res=3\n","render_res=4\n","render_res=-1\n","render_res=1x\n","render_res=0\n",""}; const int re[]={2,3,0,0,0,0,0};
      for (unsigned i=0;i<sizeof rs/sizeof *rs;i++) { FILE *file=fopen(path,"w"); CHECK(file); fputs("q3d=0\nq3d_v2=1\n",file); fputs(rs[i],file); fclose(file);
          atomic_store(&g_loaded,0); CHECK(dm_render_res_level()==re[i]); }
      unsigned n0=dm_render_res_notify_count();
      graphics_select_res(2); CHECK(atomic_load(&g_pend[K_RES])==2); CHECK(dm_render_res_level()==0); CHECK(dm_render_res_notify_count()==n0);
      dm_custom_on_uimsg("OptionsPanelMessage","CancelChanges",NULL); CHECK(atomic_load(&g_pend[K_RES])==-1); CHECK(dm_render_res_level()==0); CHECK(dm_render_res_notify_count()==n0);
      graphics_select_res(2); dm_custom_on_uimsg("OptionsPanelMessage","AcceptChanges",NULL); CHECK(dm_render_res_level()==2); CHECK(dm_render_res_notify_count()==n0+1);
      graphics_select_res(2); dm_custom_on_uimsg("OptionsPanelMessage","AcceptChanges",NULL); CHECK(dm_render_res_notify_count()==n0+1);   // 没变不通知 | unchanged: no notification
      graphics_select_res(-1); graphics_select_res(DM_RES_LEVELS); CHECK(atomic_load(&g_pend[K_RES])==-1);
      atomic_store(&g_loaded,0); CHECK(dm_render_res_level()==2);   // 已存盘 | persisted
      { FILE *file=fopen(path,"r"); CHECK(file); char text[4096]={0}; fread(text,1,sizeof text-1,file); fclose(file); CHECK(strstr(text,"render_res=2\n")); }
      dm_custom_on_uimsg("OptionsPanelMessage","ResetToDefaults",NULL); CHECK(atomic_load(&g_pend[K_RES])==0); CHECK(dm_render_res_level()==2);
      dm_custom_on_uimsg("OptionsPanelMessage","AcceptChanges",NULL); CHECK(dm_render_res_level()==0); CHECK(dm_render_res_notify_count()==n0+2); }
    // DM_TEST_RENDER_RES 时间表（纯函数）：单档、带时刻的多档、非法串 | DM_TEST_RENDER_RES schedule (pure function): single level, timed levels, invalid strings
    CHECK(res_schedule_level("2",0)==2); CHECK(res_schedule_level("2",999)==2);
    CHECK(res_schedule_level("1@0,3@720,0@1440,2@2160",0)==1); CHECK(res_schedule_level("1@0,3@720,0@1440,2@2160",719.9)==1);
    CHECK(res_schedule_level("1@0,3@720,0@1440,2@2160",720)==3); CHECK(res_schedule_level("1@0,3@720,0@1440,2@2160",1500)==0); CHECK(res_schedule_level("1@0,3@720,0@1440,2@2160",5000)==2);
    CHECK(res_schedule_level("3@60",0)==-1 || res_schedule_level("3@60",0)==3);   // 首项晚于 0 秒：0 秒前无档，实现按首项补 | first entry later than 0 s: implementation falls back to the first entry
    CHECK(res_schedule_level("",0)==-1); CHECK(res_schedule_level("x",0)==-1); CHECK(res_schedule_level("4",0)==-1); CHECK(res_schedule_level("1@-5",0)==-1); CHECK(res_schedule_level("1@0;2@5",0)==-1);
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
    // Every enhanced preference participates in global defaults, and Reset
    // itself must neither change live values nor write the preferences file.
    for (int i=1+VOICE_ROW;i<NK;i++) atomic_store(&g_val[i], k_def[i] ? 0 : 1);
    save();
    char before_reset[4096]={0}, after_reset[4096]={0};
    FILE *reset_file=fopen(path,"r"); CHECK(reset_file);
    fread(before_reset,1,sizeof before_reset-1,reset_file); fclose(reset_file);
    dm_custom_on_uimsg("OptionsPanelMessage","ResetToDefaults",NULL);
    for (int i=1+VOICE_ROW;i<NK;i++) {
        CHECK(pending_value(i)==k_def[i]); CHECK(atomic_load(&g_val[i])!=k_def[i]);
    }
    reset_file=fopen(path,"r"); CHECK(reset_file);
    fread(after_reset,1,sizeof after_reset-1,reset_file); fclose(reset_file);
    CHECK(!strcmp(before_reset,after_reset));
    dm_custom_on_uimsg("OptionsPanelMessage","CancelChanges",NULL);
    for (int i=1+VOICE_ROW;i<NK;i++) CHECK(atomic_load(&g_val[i])!=k_def[i]);
    dm_custom_on_uimsg("OptionsPanelMessage","ResetToDefaults",NULL);
    dm_custom_on_uimsg("OptionsPanelMessage","AcceptChanges",NULL);
    for (int i=1+VOICE_ROW;i<NK;i++) CHECK(atomic_load(&g_val[i])==k_def[i]);
    CHECK(atomic_load(&g_val[0])==saved_lang);
    graphics_select_fps(0); CHECK(dm_fps_setting()==60); CHECK(pending_value(K_FPS)==1);
    dm_custom_on_uimsg("OptionsPanelMessage","AcceptChanges",NULL); CHECK(dm_fps_setting()==30);
    graphics_select_fps(1); dm_custom_on_uimsg("OptionsPanelMessage","CancelChanges",NULL); CHECK(dm_fps_setting()==30);
    graphics_select_fps(1); dm_custom_on_uimsg("OptionsPanelMessage","AcceptChanges",NULL); CHECK(dm_fps_setting()==60);
    graphics_select_fps(-1); graphics_select_fps(2); CHECK(atomic_load(&g_pend[K_FPS])==-1);
    unlink(path); rmdir(directory); printf("PASS render profiles: %d migration/persistence checks\n",checks); return 0;
}
