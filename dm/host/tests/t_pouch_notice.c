// UI feedback must not wait for the paused server; chat must never duplicate UI.
#include "../dm_pouch.c"
#include <assert.h>
void (*g_dm_pre_commit_hook[5])(const char *);
uint64_t dm_fs_commit_count(void) { assert(!"notice test must not access saves"); return 0; }
void dm_fs_save_poison(int e) { (void)e; assert(!"notice test must not write saves"); }
void *dm_memfind(const uint8_t *h, size_t hn, const uint8_t *n, size_t nn) { (void)h; (void)hn; (void)n; (void)nn; assert(!"notice test must not inspect game memory"); return NULL; }
static int shown, chatted, stress;
static _Atomic int producer_done;
static char ui_copy[160], chat_copy[160];
void dm_game_notice_ui(uint8_t *base, const char *text) { assert(base == B); shown++; strlcpy(ui_copy,text,sizeof ui_copy); }
void dm_game_notice_chat(uint8_t *base, const char *text) {
    assert(base == B);
    if (stress) { int a=-1,b=-2; char tail=0;
        assert(sscanf(text,"message-%d-end-%d%c",&a,&b,&tail)==2 && a==b && a>=0 && a<5000);
    }
    chatted++; strlcpy(chat_copy,text,sizeof chat_copy);
}
static void *chat_consumer(void *unused) {
    (void)unused;
    while (!atomic_load(&producer_done)) store_notice_chat(B);
    return NULL;
}
int main(void) {
    uint8_t base = 0; B = &base;
    store_notice("No items to deposit");
    assert(shown==1 && chatted==0 && g_store_notice_pending);
    store_notice_chat(B);
    assert(shown==1 && chatted==1 && !g_store_notice_pending);
    assert(!strcmp(ui_copy,chat_copy));
    store_notice_chat(B); assert(chatted==1);
    store_notice("存放：没有可以存放的物品");
    assert(shown==2 && chatted==1);
    store_notice_chat(B); assert(shown==2 && chatted==2);
    assert(!strcmp(ui_copy,chat_copy));
    stress=1; pthread_t consumer;
    assert(!pthread_create(&consumer,NULL,chat_consumer,NULL));
    for (int i=0;i<5000;i++) {
        char text[100];snprintf(text,sizeof text,"message-%d-end-%d",i,i);store_notice(text);
    }
    atomic_store(&producer_done,1);assert(!pthread_join(consumer,NULL));
    store_notice_chat(B);
    assert(shown==5002 && !g_store_notice_pending && !strcmp(chat_copy,"message-4999-end-4999"));
    puts("PASS Deposit requests: UI submitted without server tick, deferred chat without duplicate UI; 5000 concurrent chat handoffs without torn messages");
    return 0;
}
