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
// B035 两级提示（审计 r2 P1-4 / r1 P1-3）：副文只跟着它那一条通知；别的通知出现时副文已清空、在屏外，框高与没有这两个控件时一样，也只量 Prompt 一个字宽
// B035 two-level tip (audit r2 P1-4 / r1 P1-3): the subtext follows its own notice only; when any other notice shows, the subtext is cleared and parked, the frame
// height equals the one without these widgets, and only Prompt's width is measured
static dm_notice_pairs pairs;
const dm_notice_pair *dm_game_notice_pair_for(const char *text,size_t len) { return dm_notice_pair_find(&pairs,text,len); }
static unsigned writes;
static void spy_set_text(module_t *m,uint64_t w,const char *t,size_t len) {   // 代替原版改字 + 重排：记下字、按有没有字给 fitToText 的高 | stands in for the native assign + relayout: keeps the text, gives the fitToText height by emptiness
    (void)m; static char store[4][1024]; static int k; char *d=store[k++%4]; memcpy(d,t,len); d[len]=0; writes++;
    *(const char **)(w+0x88)=d; *(uint64_t *)(w+0x90)=len;
    *(int32_t *)(w+0x7c)=len ? (!strcmp(*(const char **)(w+8),"TouchNoticeBody") ? 48 : 40) : 0;
}
static _Alignas(8) unsigned char body_w[1024],sub_w[1024];
static uint64_t kids[4];
static const char *text_of(const unsigned char *w) { static char out[1024]; uint64_t n=*(uint64_t *)(w+0x90); memcpy(out,*(const char **)(w+0x88),n); out[n]=0; return out; }
static void show(uint64_t p,const char *t) { *(const char **)(p+0x88)=t; *(uint64_t *)(p+0x90)=strlen(t); }
static void two_level(uint64_t n,uint64_t p) {
    ui_notice_set_text=spy_set_text;
    uint64_t b=(uint64_t)body_w,s=(uint64_t)sub_w;
    *(uint64_t *)body_w=*(uint64_t *)sub_w=(uint64_t)image+DM_VT_TEXT;
    *(const char **)(body_w+8)="TouchNoticeBody"; *(const char **)(sub_w+8)="TouchNoticeSub";
    *(const char **)(body_w+0x88)=*(const char **)(sub_w+0x88)="";
    *(int32_t *)(body_w+0x70)=*(int32_t *)(sub_w+0x70)=99999;*(int32_t *)(body_w+0x74)=*(int32_t *)(sub_w+0x74)=99999;
    *(int32_t *)(prompt+0x70)=64;*(int32_t *)(prompt+0x78)=1552;*(int32_t *)(body_w+0x78)=*(int32_t *)(sub_w+0x78)=1552;
    *(const char **)(note+8)="NotificationPanel"; *(int32_t *)(note+0x78)=1680; *(const char **)(prompt+8)="Prompt";
    *(uint64_t *)(note+0x58)=(uint64_t)children; *(uint64_t *)(note+0x60)=1; children[0]=(uint64_t)border;
    *(uint64_t *)(border+0x58)=(uint64_t)kids; *(uint64_t *)(border+0x60)=4; kids[0]=(uint64_t)tile; kids[1]=p; kids[2]=b; kids[3]=s;
    const char *bodytxt="Game Difficulty 3: same as 3 players", *subtxt="More players: tougher monsters, more XP and drops";
    int two; uint64_t bw,sw;
    // 没有任何两级提示：原版单段，两个控件不写字、在屏外；框高 = 根本没有这两个控件时的框高 | no two-level tip at all: native single text, no writes, parked; frame height = without the widgets
    show(p,"You cannot do that here"); *(int32_t *)(prompt+0x7c)=300;   // 三行长的原版提示：框比最小 244 高 | a three-line native notice: the frame is taller than the 244 minimum
    *(uint64_t *)(border+0x60)=2; int native_h=ui_notice_frame_fit(n); *(uint64_t *)(border+0x60)=4; assert(native_h==300+24);
    g_notice_width_calls=0; assert(ui_notice_frame_fit(n)==native_h && g_notice_width_calls==1 && !writes);
    assert(*(int32_t *)(prompt+0x74)==(244-300)/2 && *(int32_t *)(sub_w+0x70)==99999 && *(int32_t *)(sub_w+0x74)==99999);
    // 难度提示轮到显示：正文 / 副文进我们的两个控件，Prompt 让到屏外，两段一起居中、副文在正文下面 8 | the tip's turn: body / subtext go to our widgets, Prompt steps off screen, both centred, subtext 8 below the body
    dm_notice_pair_add(&pairs,bodytxt,subtxt); show(p,bodytxt);
    int c=ui_notice_prompt_center(n,p,&two,&bw,&sw); assert(two && bw==b && sw==s && c==48+8+40);
    assert(!strcmp(text_of(body_w),bodytxt) && !strcmp(text_of(sub_w),subtxt));
    assert(*(int32_t *)(prompt+0x74)==99999 && *(int32_t *)(body_w+0x70)==64 && *(int32_t *)(body_w+0x74)==(244-96)/2 && *(int32_t *)(sub_w+0x70)==64 && *(int32_t *)(sub_w+0x74)==(244-96)/2+48+8);
    g_notice_width_calls=0; ui_notice_frame_fit(n); assert(g_notice_width_calls==2);
    // 之后原版自己的系统提示（不走我们的泵）：副文已清空、挪到屏外，Prompt 回原位，框高、度量次数同原版 | then an original system notice (not through our pump): subtext cleared and parked, Prompt back in place, native frame height and measuring
    show(p,"Your inventory is full"); unsigned w0=writes;
    g_notice_width_calls=0; assert(ui_notice_frame_fit(n)==native_h && g_notice_width_calls==1);
    assert(!*(uint64_t *)(sub_w+0x90) && !*(uint64_t *)(body_w+0x90) && writes==w0+2);
    assert(*(int32_t *)(sub_w+0x70)==99999 && *(int32_t *)(sub_w+0x74)==99999 && *(int32_t *)(body_w+0x70)==99999 && *(int32_t *)(prompt+0x74)==(244-300)/2);
    ui_notice_frame_fit(n); assert(writes==w0+2);   // 已经是空串：不再写 | already empty: no more writes
    // 选择条刚展开（正文空，只有副文）：交给原版的是副文那串，只摆副文一行 | selector just opened (empty body, subtext only): the native text is the subtext; only that line is placed
    dm_notice_pair_add(&pairs,"",subtxt); show(p,subtxt);
    c=ui_notice_prompt_center(n,p,&two,&bw,&sw); assert(two && !bw && sw==s && c==40 && *(int32_t *)(sub_w+0x74)==(244-40)/2 && *(int32_t *)(body_w+0x70)==99999 && !*(uint64_t *)(body_w+0x90));
    // 不是我们的控件（类不对）：不写字、不摆 | not our widget (wrong class): no text write, no placing
    *(uint64_t *)sub_w=(uint64_t)image+DM_VT_IMAGE; show(p,bodytxt); w0=writes; *(int32_t *)(sub_w+0x74)=1234;
    ui_notice_prompt_center(n,p,&two,&bw,&sw); assert(!two && *(int32_t *)(sub_w+0x74)==1234 && *(int32_t *)(prompt+0x74)==(244-300)/2);
    *(uint64_t *)sub_w=(uint64_t)image+DM_VT_TEXT;
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
    int two; uint64_t bw,sw;
    ui_notice_prompt_center(n,p,&two,&bw,&sw);assert(*(int32_t *)(prompt+0x74)==76 && !two);
    *(int32_t *)(prompt+0x74)=999;*(uint64_t *)(border+0x30)=0;
    ui_notice_prompt_center(n,p,&two,&bw,&sw);assert(*(int32_t *)(prompt+0x74)==999);
    *(uint64_t *)(border+0x30)=n;
    two_level(n,p);
    puts("PASS notice widgets: full-frame UV span retained; only owned one-frame draw sizes change; foreign/multiframe/atlas mismatch rejected; owned Prompt center; "
         "two-level tip bound to its own notice (other notices: subtext cleared, parked, native frame height, one width query)");
    return 0;
}
