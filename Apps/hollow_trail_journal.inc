/* Full-resolution book pages. Only reader.typography can supply reader fonts. */
#include "hollow_trail_lore.inc"
static const risc_reader_typography_v1 *ht_reader;
static t5_provider_capability_lease_t ht_reader_lease;
static uint8_t *ht_reader_bitmap;
static bool ht_journal_index=true;
static unsigned ht_journal_selection,ht_journal_leaf;
static size_t ht_journal_offsets[64],ht_journal_next,ht_journal_length;
static char ht_journal_body[4096];
static void ht_release_reader(void) {
    if(caps && ht_reader_lease) (void)caps->release(ht_reader_lease);
    ht_reader_lease=0; ht_reader=NULL; ht_reader_bitmap=NULL;
}
static void ht_acquire_reader(void) {
    const void *api=NULL;
    if(!caps || !caps->acquire("reader.typography",RISC_READER_TYPOGRAPHY_V1,&ht_reader_lease,&api)) return;
    ht_reader=(const risc_reader_typography_v1 *)api;
    if(!ht_reader || ht_reader->api_version!=RISC_READER_TYPOGRAPHY_V1 ||
       ht_reader->struct_size<sizeof(*ht_reader) || !ht_reader->page) {
        if(ht_reader_lease) (void)caps->release(ht_reader_lease);
        ht_reader_lease=0; ht_reader=NULL;
    }
}
static unsigned ht_journal_list(unsigned ids[40]) {
    unsigned count=0;
    for(unsigned i=0;i<HT_LEVELS*3;++i) if(ht_evidence_found(&ht,i)) ids[count++]=i;
    for(unsigned i=0;i<HT_LEVELS;++i) if(ht.story_seen[i]) ids[count++]=HT_LEVELS*3+i;
    return count;
}
static const char *ht_journal_title(unsigned id) {
    return id<HT_LEVELS*3?ht_evidence[id/3][id%3].title:ht_chapters[id-HT_LEVELS*3].title;
}
static void ht_journal_open(unsigned id) {
    journal_page=id; ht_journal_index=false; ht_journal_leaf=0;
    memset(ht_journal_offsets,0,sizeof(ht_journal_offsets));
    ht_journal_next=ht_journal_length=0;
}
/* Consume edges here so reading controls never leak into simulation. */
static void ht_journal_input(uint32_t down) {
    unsigned ids[40],count=ht_journal_list(ids);
    if(ht_journal_index) {
        if(down&(HT_INTERACT|HT_EXIT|HT_PAUSE)) reading=false;
        else if((down&HT_JUMP) && count) ht_journal_open(ids[ht_journal_selection%count]);
        else if((down&HT_LEFT) && count) ht_journal_selection=(ht_journal_selection+count-1)%count;
        else if((down&HT_RIGHT) && count) ht_journal_selection=(ht_journal_selection+1)%count;
    } else {
        if(down&(HT_INTERACT|HT_EXIT|HT_PAUSE|HT_JUMP)) {
            ht_journal_index=true;
            for(unsigned i=0;i<count;++i) if(ids[i]==journal_page) ht_journal_selection=i;
        } else if((down&HT_LEFT) && ht_journal_leaf) { --ht_journal_leaf; ht_journal_next=0; }
        else if((down&HT_RIGHT) && ht_journal_next<ht_journal_length &&
                ht_journal_next>ht_journal_offsets[ht_journal_leaf] && ht_journal_leaf+1<64) {
            ht_journal_offsets[++ht_journal_leaf]=ht_journal_next;
            ht_journal_next=0; /* Do not turn again until this leaf is laid out. */
        }
    }
}
static void ht_journal_render(void) {
    char title[128],footer[128];
    ht_journal_body[0]=0;
    if(ht_journal_index) {
        unsigned ids[40],count=ht_journal_list(ids);
        if(count) ht_journal_selection%=count;
        unsigned id=count?ids[ht_journal_selection]:0;
        snprintf(title,sizeof(title),"Hollow Trail - Journal");
        /* One selected record per index page stays readable at large fonts. */
        snprintf(ht_journal_body,sizeof(ht_journal_body),
            "%s\n\n%s\n\nRecord %u of %u\n\nOnly discoveries and reached story passages appear here.\n\nLeft / Right: choose record\nA / Confirm: read\nB / Up / Back: return to trail",
            count?(id<HT_LEVELS*3?"Collected evidence":"Story so far"):"No records yet",
            count?ht_journal_title(id):"Explore the trail to begin your journal.",
            count?ht_journal_selection+1:0,count);
        snprintf(footer,sizeof(footer),"L/R choose | A read | B close");
    } else {
        snprintf(title,sizeof(title),"%s",ht_journal_title(journal_page));
        if(journal_page<HT_LEVELS*3) {
            snprintf(ht_journal_body,sizeof(ht_journal_body),"%s",ht_evidence_prose[journal_page]);
        } else {
            unsigned chapter=journal_page-HT_LEVELS*3;
            /* Only reached beats: no clues or later revelations are unlocked by a recap. */
            size_t used=0;
            for(unsigned beat=0;beat<ht.story_seen[chapter] && beat<3;++beat) {
                int n=snprintf(ht_journal_body+used,sizeof(ht_journal_body)-used,"%s%s\n\n",
                    beat?"":"The journey remembered\n\n",ht_story_prose[chapter][beat]);
                if(n<0 || (size_t)n>=sizeof(ht_journal_body)-used) break;
                used+=(size_t)n;
            }
        }
        snprintf(footer,sizeof(footer),"Page %u | L/R pages | B journal",ht_journal_leaf+1);
    }
    ht_journal_length=strlen(ht_journal_body);
    risc_reader_page_request_v1 request={
        .pixels=ht_reader_bitmap,.capacity=960u*540u/8u,
        .width=960,.height=540,.stride=120,.title=title,.footer=footer,
        .text=ht_journal_body,.length=ht_journal_length,
        .offset=ht_journal_index?0:ht_journal_offsets[ht_journal_leaf]
    };
    risc_reader_page_result_v1 result={0};
    if(ht_reader && ht_reader->page(ht_reader->context,&request,&result) &&
       result.next_offset<=request.length &&
       (request.offset==request.length || result.next_offset>request.offset)) {
        ht_journal_next=result.next_offset;
    } else {
        ht_journal_next=0;
        /* Explicit error, not a silent substitute for the configured font. */
        memset(ht_scene,0,HT_PIXELS);
        ht_text(72,105,"READER TYPOGRAPHY UNAVAILABLE",2);
        ht_text(72,133,"B / BACK: JOURNAL   B / BACK AGAIN: CLOSE",1);
        ht_pack_mono(ht_reader_bitmap,120);
    }
}
