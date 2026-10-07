/* Full-resolution book pages. Only reader.typography can supply reader fonts. */
#include "hollow_trail_lore.inc"
#include "hollow_trail_endings.inc"
/* Canonical black-handle page is the exact Chapter XI notebook passage.
 * The alternate mechanism remains an explicit game branch; it cannot claim
 * that the sealed original was seen or copied. */
static const char ht_door_notebook_book[] =
    "I took out the notebook.\n\nNear the beginning, beneath the city map, I found the sentence I had written in the schoolroom: She had been trying to stay where someone could see her. I had left a space underneath it.\n\nI looked at the space without filling it.\n\nThen I turned to a clean page near the end. My pencil was short; I had been gripping it too hard. I wrote the date as well as I could reckon it and described the room at the top of the tower. The black handle, the shutter, the seal. I copied her instruction exactly. I did not improve the order of the sentences or leave out the place where I had first misunderstood them.\n\nBelow that I wrote what I had done.\n\nI broke the feed because I thought it would free her.\n\nI waited until I could read the sentence without reaching to change it.\n\nOn the next line I wrote:\n\nI still want to find her.";
static const char ht_door_notebook_alternate[] =
    "I took out the notebook.\n\n"
    "The original was sealed behind its shutter. I wrote down what I had seen the live pen do, and copied the words it addressed to me: 'There is a place for you here. Finish what I could not.'\n\n"
    "Below that I wrote what I had done.\n\n"
    "I completed the circuit because I believed she knew what she was doing.\n\n"
    "I waited until I could read the sentence without reaching to change it.\n\n"
    "On the next line I wrote:\n\nI still want to find her.";
#define HT_JOURNAL_RECORDS (HT_LEVELS*4+3)
#define HT_DOOR_JOURNAL (HT_LEVELS*4+2)
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
    if(ht.door_notebook)ids[count++]=HT_DOOR_JOURNAL;
    return count;
}
static const char *ht_journal_title(unsigned id) {
    if(id==HT_DOOR_JOURNAL)return "A DOOR WITHOUT A LIGHT";
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
        else if((down&(HT_LEFT|HT_UP)) && count) ht_journal_selection=(ht_journal_selection+count-1)%count;
        else if((down&(HT_RIGHT|HT_DOWN)) && count) ht_journal_selection=(ht_journal_selection+1)%count;
    } else {
        if(down&(HT_BACK|HT_EXIT|HT_PAUSE|HT_ACCEPT)) {
            if((down&HT_ACCEPT) && journal_page==5 && ht_cutscene_watch_read(&ht) &&
               ht_journal_page_ready && ht_journal_next==ht_journal_length &&
               ht_read_submitted_revision==scene_revision) {
                reading=false;ht_cutscene_begin(HT_CUTSCENE_WATCH);
                ht_input_rearm=true;held=previous=0;return;
            }
            if((down&HT_ACCEPT) && journal_page==HT_DOOR_JOURNAL && ht.door_stage==HT_DOOR_NOTEBOOK &&
               ht_journal_page_ready && ht_journal_next==ht_journal_length &&
               ht_read_submitted_revision==scene_revision) {
                ht.door_notebook=true;ht.door_stage=HT_DOOR_GRIEF;ht.door_tick=0;reading=false;
                ht_input_rearm=true;held=previous=0;return;
            }
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
static bool ht_reader_has_ink(const uint8_t *pixels,size_t size) {
    for(size_t i=0;i<size;++i) if(pixels[i]) return true;
    return false;
}
/* A physical notebook carried through the same ink-dark world. Texture stays
 * outside the typeset rectangle so no mark can obscure a clue or a choice. */
static void ht_journal_frame(void) {
    memset(ht_scene,255,HT_PIXELS);
    for(int y=8;y<HT_H-8;++y) for(int x=14;x<HT_W-14;++x) {
        unsigned grain=ht_hash((unsigned)(x+HT_W*y));
        ht_scene[y*HT_W+x]=(uint8_t)(grain%7?215:120);
    }
    ht_rect(ht_scene,24,18,432,236,255); /* cover and cast shadow */
    ht_rect(ht_scene,27,19,427,232,0);
    for(int y=21;y<250;++y) {
        int tooth=(int)(ht_hash((unsigned)y)%3);
        ht_rect(ht_scene,27,y,tooth+1,1,170);
        ht_rect(ht_scene,451-tooth,y,tooth+2,1,170);
    }
    /* A chapter stamp and hand-drawn trail sketch form a visual page rail. */
    ht_rect(ht_scene,63,30,1,207,170);
    char stamp[8];snprintf(stamp,sizeof(stamp),"%02u",ht.door_stage?11:ht.level+1);
    ht_text(32,35,stamp,2);ht_text(31,55,"TRAIL",1);
    ht_limb(ht_scene,47,217,40,118,2,1,255);
    for(int i=0;i<5;++i) {
        int y=205-i*17;
        ht_limb(ht_scene,44,y,31,y-21,1,0,255);
        ht_limb(ht_scene,43,y-8,56,y-30,1,0,255);
    }
    ht_disk(ht_scene,44,94,12,255);ht_disk(ht_scene,44,94,10,0);
    ht_limb(ht_scene,44,85,44,103,1,1,255);
    ht_limb(ht_scene,36,94,52,94,1,1,255);
    ht_text(32,226,"NOTES",1);
    /* Binding stitches, torn corners and an ink branch remain screen-fixed. */
    for(int y=31;y<240;y+=17) {
        ht_disk(ht_scene,24,y,3,0);ht_limb(ht_scene,22,y,31,y+1,1,1,170);
    }
    for(int i=0;i<8;++i) {
        int y=245-i*19;
        ht_limb(ht_scene,466,y,458,y-32,1,1,0);
        ht_limb(ht_scene,463,y-10,474,y-20,1,1,0);
    }
    ht_text_color(35,9,"HOLLOW TRAIL / FIELD ARCHIVE",1,0);
    ht_text_color(35,258,"FOUND WORDS. UNRELIABLE MEMORIES.",1,0);
    ht_framed=false;ht_pack_mono(ht_reader_bitmap,120);
}
static void ht_journal_render(void) {
    /* Journal composition has its own 480x270 frame plus a physical packed
     * typography layer. Never inherit the native gameplay surface pointer. */
    ht_native_active=false;
    ht_scene=ht_scene_low;
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
        snprintf(title,sizeof(title),"FIELD NOTES / %u DISCOVERIES",count);
        size_t used=0;
        unsigned first=(ht_journal_selection/4)*4;
        for(unsigned i=first;i<count && i<first+4;++i) {
            int n=snprintf(ht_journal_body+used,sizeof(ht_journal_body)-used,"%s %02u  %s\n\n",
                i==ht_journal_selection?">":" ",i+1,ht_journal_title(ids[i]));
            if(n<0 || (size_t)n>=sizeof(ht_journal_body)-used) break;
            used+=(size_t)n;
        }
        if(!count) snprintf(ht_journal_body,sizeof(ht_journal_body),
            "The trail has left no traces here yet.\n\nApproach a found object and press A to collect its story.");
        snprintf(footer,sizeof(footer),"%u/%u | Arrows select | A read | X trail",count?ht_journal_selection+1:0,count);
    } else {
        snprintf(title,sizeof(title),"%s",ht_journal_title(journal_page));
        if(journal_page==HT_DOOR_JOURNAL) {
            const char *page=ht.verdict==2?ht_door_notebook_alternate:ht_door_notebook_book;
            /* A debug jump may bypass the schoolroom. Do not manufacture its
             * remembered page; a normal book route keeps the exact passage. */
            const char *school=ht_evidence_found(&ht,3)?
                "She had been trying to stay where someone could see her.\n\n":"";
            if(ht.verdict==2)snprintf(ht_journal_body,sizeof(ht_journal_body),"%s%s",school,page);
            else if(!ht_evidence_found(&ht,3)) {
                const char *clean=strstr(page,"Then I turned to a clean page");
                snprintf(ht_journal_body,sizeof(ht_journal_body),"I took out the notebook.\n\n%s",clean?clean:page);
            } else snprintf(ht_journal_body,sizeof(ht_journal_body),"%s",page);
        } else if(journal_page>=HT_LEVELS*4) {
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
        snprintf(footer,sizeof(footer),journal_page==HT_DOOR_JOURNAL && ht.door_stage==HT_DOOR_NOTEBOOK?
            "Page %u | L/R | A on last page: close":journal_page==HT_LEVELS*3-1 && ht.verdict && !ht.verdict_read?
            "Page %u | L/R | A on last page: leave":"Page %u | L/R pages | X journal",ht_journal_leaf+1);
    }
    ht_journal_length=strlen(ht_journal_body);
    ht_journal_frame();
    risc_reader_page_request_v1 request={
        .pixels=ht_temp,.capacity=768u*456u/8u,
        .width=768,.height=456,.stride=96,.title=title,.footer=footer,
        .text=ht_journal_body,.length=ht_journal_length,
        .offset=(ht_journal_index || ht_journal_deciding)?0:ht_journal_offsets[ht_journal_leaf]
    };
    risc_reader_page_result_v1 result={0};
    if(ht_reader && ht_reader->page(ht_reader->context,&request,&result) && ht_reader_has_ink(request.pixels,request.capacity) &&
       result.next_offset<=request.length &&
       (request.offset==request.length || result.next_offset>request.offset) &&
       (!(ht_journal_deciding || ht_journal_index) || result.next_offset==request.length)) {
        for(unsigned y=0;y<request.height;++y)
            memcpy(ht_reader_bitmap+(y+42)*120+17,request.pixels+y*request.stride,request.stride);
        ht_journal_next=result.next_offset;
        ht_journal_page_ready=true;
    } else {
        ht_journal_next=0;
        /* Marked compact fallback keeps the ending readable after a reader
         * service failure. Only ASCII authored text uses this emergency path. */
        ht_text(84,36,"FIELD NOTES / COMPACT LETTERING",1);
        char heading[59]; snprintf(heading,sizeof(heading),"%.58s",title);
        ht_text(84,52,heading,1);
        size_t at=request.offset;
        for(unsigned row=0;row<16 && at<request.length;++row) {
            char line[59]; size_t used=0,last_space=0;
            while(used<58 && at+used<request.length && request.text[at+used]!='\n') {
                line[used]=request.text[at+used]; if(line[used]==' ') last_space=used+1; ++used;
            }
            if(used==58 && at+used<request.length && request.text[at+used]!='\n' && last_space) used=last_space;
            memcpy(line,request.text+at,used); line[used]=0;
            ht_text(84,70+(int)row*9,line,1); at+=used;
            if(at<request.length && request.text[at]=='\n') ++at;
        }
        char help[59]; snprintf(help,sizeof(help),"%.58s",footer); ht_text(84,225,help,1);
        ht_framed=false; ht_pack_mono(ht_reader_bitmap,120);
        ht_journal_next=at; ht_journal_page_ready=true;
    }
}
