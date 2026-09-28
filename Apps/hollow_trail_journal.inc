/* Full-resolution book pages. Only reader.typography can supply reader fonts. */
#include "hollow_trail_lore.inc"
#include "hollow_trail_endings.inc"
#define HT_JOURNAL_RECORDS (HT_LEVELS*4+2)
static bool ht_journal_deciding,ht_journal_confirm,ht_journal_page_ready;
static unsigned ht_journal_choice;
static uint32_t ht_read_submitted_revision;
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
static unsigned ht_journal_list(unsigned ids[HT_JOURNAL_RECORDS]) {
    unsigned count=0;
    for(unsigned i=0;i<HT_LEVELS*3;++i) if(ht_evidence_found(&ht,i)) ids[count++]=i;
    for(unsigned i=0;i<HT_LEVELS;++i) if(ht.story_seen[i]) ids[count++]=HT_LEVELS*3+i;
    for(unsigned i=0;i<2;++i) if(ht.endings&(1u<<i)) ids[count++]=HT_LEVELS*4+i;
    return count;
}
static const char *ht_journal_title(unsigned id) {
    if(id>=HT_LEVELS*4) return id==HT_LEVELS*4?"ARCHIVE: THE ORIGINAL":"ARCHIVE: THE LIVE PEN";
    if(id==HT_LEVELS*3-1 && ht.last_verdict) return "THE CHANGED TESTIMONY";
    return id<HT_LEVELS*3?ht_evidence[id/3][id%3].title:ht_chapters[id-HT_LEVELS*3].title;
}
static void ht_journal_open(unsigned id) {
    journal_page=id; ht_journal_index=false; ht_journal_leaf=0;
    ht_journal_deciding=ht_journal_confirm=ht_journal_page_ready=false;
    memset(ht_journal_offsets,0,sizeof(ht_journal_offsets));
    ht_journal_next=ht_journal_length=0;
}
static void ht_journal_tower(void) {
    ht_journal_open(HT_LEVELS*3-1);
    if(!ht.verdict) { ht_journal_deciding=true; ht_journal_choice=0; }
}
static void ht_journal_choose(unsigned choice) {
    ht_journal_choice=choice; ht_journal_confirm=false; ht_journal_leaf=0;
    ht_journal_offsets[0]=0; ht_journal_next=0; ht_journal_page_ready=false;
}
/* Consume edges here so reading controls never leak into simulation. */
static void ht_journal_input(uint32_t down) {
    unsigned ids[HT_JOURNAL_RECORDS],count=ht_journal_list(ids);
    if(ht_journal_deciding) {
        if(down&(HT_BACK|HT_EXIT|HT_PAUSE)) {
            if(ht_journal_confirm) { ht_journal_choose(ht_journal_choice); }
            else { ht_journal_deciding=false; reading=false; }
        } else if(down&HT_LEFT) ht_journal_choose(0);
        else if(down&HT_RIGHT) ht_journal_choose(1);
        else if((down&HT_ACCEPT) && ht_journal_page_ready && ht_read_submitted_revision==scene_revision) {
            if(!ht_journal_confirm) { ht_journal_confirm=true; ht_journal_page_ready=false; }
            else if(ht_decide(ht_journal_choice+1)) ht_journal_open(HT_LEVELS*3-1);
        }
        return;
    }
    if(ht_journal_index) {
        if(down&(HT_BACK|HT_EXIT|HT_PAUSE)) reading=false;
        else if((down&HT_ACCEPT) && count) ht_journal_open(ids[ht_journal_selection%count]);
        else if((down&HT_LEFT) && count) ht_journal_selection=(ht_journal_selection+count-1)%count;
        else if((down&HT_RIGHT) && count) ht_journal_selection=(ht_journal_selection+1)%count;
    } else {
        if(down&(HT_BACK|HT_EXIT|HT_PAUSE|HT_ACCEPT)) {
            if((down&HT_ACCEPT) && journal_page==HT_LEVELS*3-1 && ht.verdict &&
               ht_journal_page_ready && ht_journal_next==ht_journal_length &&
               ht_read_submitted_revision==scene_revision) {
                ht.verdict_read=true; reading=false; return;
            }
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
/* Capability success is not proof that any glyph reached the page. Reject
 * all-white output (including older firmware's missing decompressor path).
 * Fixed 64,800-byte bound; no allocation or device I/O. */
static bool ht_reader_has_ink(void) {
    for(size_t i=0;i<960u*540u/8u;++i) if(ht_reader_bitmap[i]) return true;
    return false;
}
static void ht_journal_render(void) {
    char title[128],footer[128];
    ht_journal_body[0]=0;
    ht_journal_page_ready=false;
    if(ht_journal_deciding) {
        snprintf(title,sizeof(title),ht_journal_confirm?"Confirm your decision":"The two releases");
        snprintf(ht_journal_body,sizeof(ht_journal_body),"%s\n\n%s\n\n%s",
            ht_journal_choice?"COMPLETE THE CIRCUIT":"BREAK THE CIRCUIT",
            ht_journal_choice?"Trust her work. Power the live pen; seal the original away.":
                "Interrupt her work. Release the original; shutter the live pen.",
            ht_journal_confirm?"This choice lasts for this journey. A: commit. X: reconsider.":
                "Neither face can prove her freedom. L/R: choose. A: review. X: leave and reread.");
        snprintf(footer,sizeof(footer),ht_journal_confirm?"A commit | X reconsider":"L/R choose | A review | X leave");
    } else if(ht_journal_index) {
        unsigned ids[HT_JOURNAL_RECORDS],count=ht_journal_list(ids);
        if(count) ht_journal_selection%=count;
        unsigned id=count?ids[ht_journal_selection]:0;
        snprintf(title,sizeof(title),"Hollow Trail - Journal");
        /* One selected record per index page stays readable at large fonts. */
        snprintf(ht_journal_body,sizeof(ht_journal_body),
            "%s\n\n%s\n\nRecord %u of %u\n\nOnly discoveries and reached story passages appear here.\n\nLeft / Right: choose record\nA / Confirm: read\nX / Back: return to trail",
            count?(id<HT_LEVELS*3?"Collected evidence":id<HT_LEVELS*4?"Story so far":"Witnessed endings"):"No records yet",
            count?ht_journal_title(id):"Explore the trail to begin your journal.",
            count?ht_journal_selection+1:0,count);
        snprintf(footer,sizeof(footer),"L/R choose | A read | X close");
    } else {
        snprintf(title,sizeof(title),"%s",ht_journal_title(journal_page));
        if(journal_page>=HT_LEVELS*4) {
            snprintf(ht_journal_body,sizeof(ht_journal_body),"%s",ht_final_testimony[journal_page-HT_LEVELS*4]);
        } else if(journal_page==HT_LEVELS*3-1 && ht.last_verdict) {
            snprintf(ht_journal_body,sizeof(ht_journal_body),"%s",ht_final_testimony[ht.last_verdict-1]);
        } else if(journal_page<HT_LEVELS*3) {
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
        snprintf(footer,sizeof(footer),journal_page==HT_LEVELS*3-1 && ht.verdict && !ht.verdict_read?
            "Page %u | L/R | A on last page: leave":"Page %u | L/R pages | X journal",ht_journal_leaf+1);
    }
    ht_journal_length=strlen(ht_journal_body);
    risc_reader_page_request_v1 request={
        .pixels=ht_reader_bitmap,.capacity=960u*540u/8u,
        .width=960,.height=540,.stride=120,.title=title,.footer=footer,
        .text=ht_journal_body,.length=ht_journal_length,
        .offset=(ht_journal_index || ht_journal_deciding)?0:ht_journal_offsets[ht_journal_leaf]
    };
    risc_reader_page_result_v1 result={0};
    if(ht_reader && ht_reader->page(ht_reader->context,&request,&result) && ht_reader_has_ink() &&
       result.next_offset<=request.length &&
       (request.offset==request.length || result.next_offset>request.offset) &&
       (!ht_journal_deciding || result.next_offset==request.length)) {
        ht_journal_next=result.next_offset;
        ht_journal_page_ready=true;
    } else {
        ht_journal_next=0;
        /* Marked compact fallback keeps the ending readable after a reader
         * service failure. Only ASCII authored text uses this emergency path. */
        memset(ht_scene,0,HT_PIXELS);
        ht_text(60,36,"READER PAGE UNAVAILABLE - COMPACT VIEW",1);
        char heading[61]; snprintf(heading,sizeof(heading),"%.60s",title);
        ht_text(60,52,heading,1);
        size_t at=request.offset;
        for(unsigned row=0;row<16 && at<request.length;++row) {
            char line[61]; size_t used=0,last_space=0;
            while(used<60 && at+used<request.length && request.text[at+used]!='\n') {
                line[used]=request.text[at+used]; if(line[used]==' ') last_space=used+1; ++used;
            }
            if(used==60 && at+used<request.length && request.text[at+used]!='\n' && last_space) used=last_space;
            memcpy(line,request.text+at,used); line[used]=0;
            ht_text(60,70+(int)row*9,line,1); at+=used;
            if(at<request.length && request.text[at]=='\n') ++at;
        }
        char help[61]; snprintf(help,sizeof(help),"%.60s",footer); ht_text(60,225,help,1);
        ht_pack_mono(ht_reader_bitmap,120);
        ht_journal_next=at; ht_journal_page_ready=true;
    }
}
