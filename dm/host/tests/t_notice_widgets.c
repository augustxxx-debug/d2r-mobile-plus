// Regression for native ImageWidget relayout and owned Prompt centering.
#include "../dm_cursor.c"
#include <assert.h>
#include <signal.h>
module_t g_mod[MAX_MOD]; int g_nmod;
static unsigned char image[1];
static _Alignas(8) unsigned char note[160],border[160],tile[512],prompt[1024];
static uint64_t children[2],vt[4],sprite_ref;
static _Alignas(8) unsigned char sprite[256],expected_sprite[256];
static int resets;
static void native_image_layout(uint64_t w) { resets++;*(int32_t *)(w+0x78)=2;*(int32_t *)(w+0x7c)=2; }
static void rejected(uint64_t w,const int wanted[4]) {
    unsigned char old_tile[sizeof tile],old_sprite[sizeof sprite];
    memcpy(old_tile,tile,sizeof tile);memcpy(old_sprite,sprite,sizeof sprite);
    ui_notice_tile_rect(w,wanted);
    assert(!memcmp(old_tile,tile,sizeof tile));
    assert(!memcmp(old_sprite,sprite,sizeof sprite));
}
static void assertion_exit(int sig) { (void)sig;_Exit(86); }
int main(void) {
    signal(SIGABRT,assertion_exit);
    uint64_t t=(uint64_t)tile,p=(uint64_t)prompt,n=(uint64_t)note;
    strcpy(g_mod[0].name,"main");g_mod[0].base=image;g_nmod=1;
    *(uint64_t *)tile=(uint64_t)vt;vt[2]=(uint64_t)native_image_layout;
    *(const char **)(tile+8)="TouchNoticeTile11";
    const int wanted[4]={52,24,1200,150};
    rejected(t,wanted);assert(!resets);
    *(uint64_t *)tile=(uint64_t)image+DM_VT_IMAGE;
    *(uint64_t *)(tile+0x88)=(uint64_t)&sprite_ref; sprite_ref=(uint64_t)sprite;
    const char *file="Ztouch_notification/tile11";
    *(const char **)(tile+0x108)=file;*(uint64_t *)(tile+0x110)=strlen(file);
    memset(sprite,0xab,sizeof sprite);
    *(uint32_t *)(sprite+0xbc)=*(uint32_t *)(sprite+0xd0)=1576;
    *(uint32_t *)(sprite+0xc0)=78;*(uint32_t *)(sprite+0xcc)=1;
    memcpy(expected_sprite,sprite,sizeof sprite);
    *(uint32_t *)(expected_sprite+0xbc)=*(uint32_t *)(expected_sprite+0xd0)=1200;
    *(uint32_t *)(expected_sprite+0xc0)=150;
    ui_notice_tile_rect(t,wanted);
    assert(!memcmp(sprite,expected_sprite,sizeof sprite)); // Only draw sizes changed.
    assert(!memcmp(tile+0x70,wanted,sizeof wanted));
    *(uint64_t *)(tile+0x88)=0;rejected(t,wanted);
    *(uint64_t *)(tile+0x88)=(uint64_t)&sprite_ref;
    *(uint64_t *)(tile+0x110)=1;rejected(t,wanted);
    *(uint64_t *)(tile+0x110)=strlen(file);
    const int next[4]={52,24,900,250};
    *(const char **)(tile+0x108)="PANEL/TooltipBorder/TooltipBorder";
    rejected(t,next);assert(!memcmp(sprite,expected_sprite,sizeof sprite));
    *(const char **)(tile+0x108)=file;
    *(uint32_t *)(sprite+0xcc)=*(uint32_t *)(expected_sprite+0xcc)=9;
    rejected(t,next);assert(!memcmp(sprite,expected_sprite,sizeof sprite));
    *(uint32_t *)(sprite+0xcc)=*(uint32_t *)(expected_sprite+0xcc)=1;
    *(int32_t *)(tile+0x90)=1;
    rejected(t,next);assert(!memcmp(sprite,expected_sprite,sizeof sprite));
    *(int32_t *)(tile+0x90)=0;
    *(uint32_t *)(sprite+0xbc)=*(uint32_t *)(expected_sprite+0xbc)=1600;
    rejected(t,next);assert(!memcmp(sprite,expected_sprite,sizeof sprite));
    const char *invalid[]={"TouchNoticeTile000","TouchNoticeTile50","TouchNoticeTile13","NotificationBorder"};
    for (unsigned i=0;i<sizeof invalid/sizeof *invalid;i++) {
        *(const char **)(tile+8)=invalid[i];memset(tile+0x70,0,16);
        ui_notice_tile_rect(t,wanted);assert(!memcmp(tile+0x70,(int[4]){0},16));
    }
    *(const char **)(tile+8)="TouchNoticeTile00";
    *(const char **)(border+8)="NotificationBorder";
    *(uint64_t *)(border+0x30)=n;*(uint64_t *)(border+0x58)=(uint64_t)children;
    *(uint64_t *)(border+0x60)=2;children[0]=t;children[1]=p;
    *(uint64_t *)prompt=(uint64_t)image+DM_VT_TEXT;
    *(uint64_t *)(prompt+0x30)=(uint64_t)border;*(int32_t *)(prompt+0x7c)=91;
    ui_notice_prompt_center(n,p);assert(*(int32_t *)(prompt+0x74)==76);
    *(int32_t *)(prompt+0x74)=999;*(uint64_t *)(border+0x30)=0;
    ui_notice_prompt_center(n,p);assert(*(int32_t *)(prompt+0x74)==999);
    puts("PASS notice widgets: full-frame UV span retained; only owned one-frame draw sizes change; foreign/multiframe/atlas mismatch rejected; owned Prompt center");
    return 0;
}
