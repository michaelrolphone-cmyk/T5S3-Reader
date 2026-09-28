/* Drive the actual app input/journal paths. No alternate decision model. */
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include "../../Apps/hollow_trail.c"
static uint32_t now,buttons;
static bool poll_input(t5_app_input_t *out,uint32_t wait) {
    now+=wait; memset(out,0,sizeof(*out)); out->buttons=buttons; return true;
}
static uint32_t clock_ms(void) { return now; }
static const t5_app_api_v1 fake_app={.abi_version=1,.struct_size=sizeof(fake_app),.poll=poll_input,.millis=clock_ms};
const t5_app_api_v1 *t5_app_get_api(uint32_t v) {(void)v; return &fake_app;}
const t5_video_api_v1 *t5_video_get_api(uint32_t v) {(void)v; return NULL;}
const t5_math_api_v1 *t5_math_get_api(uint32_t v) {(void)v; return NULL;}
const t5_provider_capability_api_v1 *t5_provider_capability_get_api(uint32_t v) {(void)v; return NULL;}
static bool page(void *ctx,const risc_reader_page_request_v1*q,risc_reader_page_result_v1*r) {
    (void)ctx; assert(q->offset<=q->length);
    r->next_offset=q->offset+240; if(r->next_offset>q->length) r->next_offset=q->length;
    memset(q->pixels,0,q->capacity); return true;
}
static const risc_reader_typography_v1 reader={1,sizeof(reader),NULL,page};
static void present(void) {
    ht_journal_render(); assert(ht_journal_page_ready);
    ht_read_submitted_revision=scene_revision;
}
static void press(uint32_t button) {
    buttons=0; ht_input(1); buttons=button; ht_input(1);
}
static void tower(void) {
    ht.level=HT_LEVELS-1; ht_select_level(ht.level); ht_spawn(true);
    ht.puzzle.solved=true; ht.x=HT_PUZZLE_GATE*256;
    ht.y=ht_land[9].top*256; ht.vx=ht.vy=0; ht.grounded=true;
    reading=paused=loading=quitting=false; previous=held=0; simulation_started=false;
    assert(!ht_decide(0) && !ht_decide(3));
    ht.x=HT_GOAL*256; ht_step(1,false,false);
    assert(ht.level==HT_LEVELS-1 && ht.x==HT_PUZZLE_GATE*256);
    press(T5_APP_BUTTON_CONFIRM); assert(reading && ht_journal_deciding && !ht.verdict);
}
int main(void) {
    uint8_t *memory=malloc(HT_MEMORY),*bitmap=malloc(HT_PACKED_BYTES);
    assert(memory && bitmap); ht_bind(memory); app=&fake_app; ht_reader=&reader; ht_reader_bitmap=bitmap;
    unsigned ids[HT_JOURNAL_RECORDS];
    memset(&ht,0,sizeof(ht)); ht_spawn(true);
    assert(!ht_decide(1)); assert(ht_journal_list(ids)==1 && ids[0]==30);
    for(unsigned choice=1;choice<=2;++choice) {
        tower();
        /* Cancellation never commits, including Back while a key stays held. */
        present(); press(T5_APP_BUTTON_CONFIRM); assert(ht_journal_confirm && !ht.verdict);
        press(T5_APP_BUTTON_BACK); assert(!ht_journal_confirm && reading && !ht.verdict);
        press(T5_APP_BUTTON_BACK); assert(!reading && !quitting && !ht.verdict);
        ht_input(1); assert(!quitting);
        press(T5_APP_BUTTON_CONFIRM); assert(ht_journal_deciding);
        if(choice==2) press(T5_APP_BUTTON_RIGHT);
        int frozen=ht.x; unsigned ticks=ht.ticks;
        present(); press(T5_APP_BUTTON_CONFIRM); assert(ht_journal_confirm && !ht.verdict);
        ht_input(100); assert(!ht.verdict && ht.x==frozen && ht.ticks==ticks);
        /* An unseen confirmation cannot commit, even on a new A edge. */
        press(T5_APP_BUTTON_CONFIRM); assert(!ht.verdict);
        present(); press(T5_APP_BUTTON_CONFIRM);
        assert(ht.verdict==choice && !ht.verdict_read && !ht_journal_deciding && reading);
        assert(!ht_decide(choice==1?2:1));
        assert(ht_evidence_found(&ht,29));
        present(); assert(!strcmp(ht_journal_body,ht_final_testimony[choice-1]));
        assert(strcmp(ht_journal_body,ht_final_testimony[2-choice]));
        unsigned count=ht_journal_list(ids); assert(ids[count-1]==39+choice);
        if(choice==1) for(unsigned i=0;i<count;++i) assert(ids[i]!=41);
        /* A first-page close does not acknowledge unread ending pages. */
        press(T5_APP_BUTTON_CONFIRM); assert(ht_journal_index && !ht.verdict_read);
        press(T5_APP_BUTTON_BACK); assert(!reading);
        ht.x=HT_GOAL*256; ht_step(1,false,false); assert(ht.x==HT_PUZZLE_GATE*256 && !ht.laps);
        press(T5_APP_BUTTON_CONFIRM); assert(reading && !ht_journal_deciding && journal_page==29);
        unsigned turns=0;
        do {
            present();
            if(ht_journal_next==ht_journal_length) break;
            press(T5_APP_BUTTON_RIGHT); assert(++turns<63);
        } while(true);
        assert(turns>0);
        press(T5_APP_BUTTON_CONFIRM); assert(ht.verdict_read && !reading);
        ht_spawn(false); assert(ht.verdict==choice && ht.verdict_read);
        ht.x=HT_GOAL*256; ht.y=ht_land[9].top*256; ht.grounded=true; ht.vy=0;
        ht_step(0,false,false);
        assert(ht.level==0 && !ht.verdict && !ht.verdict_read && ht.last_verdict==choice);
        assert(ht.endings==(choice==1?1:3));
        ht.laps=0;
    }
    /* Missing reader service still exposes all text and supports completion. */
    ht_reader=NULL; tower(); present(); press(T5_APP_BUTTON_CONFIRM);
    present(); press(T5_APP_BUTTON_CONFIRM); assert(ht.verdict==1);
    unsigned pages=0;
    do { present(); if(ht_journal_next==ht_journal_length) break;
        press(T5_APP_BUTTON_RIGHT); assert(++pages<63);
    } while(true);
    assert(pages>0); press(T5_APP_BUTTON_CONFIRM); assert(ht.verdict_read && !reading);
    free(bitmap); free(memory);
    puts("Hollow Trail endings: both choices, cancellation, display/edge guards, reveal pagination, history, replay and font failure PASS");
}
