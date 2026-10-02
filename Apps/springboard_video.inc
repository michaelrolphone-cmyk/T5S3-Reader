// App-owned animation/cache state. The existing video API owns DMA and scan
// tasks. Host rasterization happens only when a cached page changes.
#include "springboard_slide.h"
static const t5_video_api_v1* sv_video;
static t5_app_frame_t sv_frame;
static uint8_t* sv_pages[3]; // current, previous, next; bounded PSRAM only
static bool sv_enabled,sv_fatal,sv_tracking,sv_dragging,sv_vertical,sv_animating,sv_dirty;
static bool sv_cached_edit;
static uint32_t sv_cached_page,sv_started,sv_anim_start,sv_last_submit,sv_blocked_since;
static bool sv_blocked;
static int sv_x,sv_y,sv_offset,sv_from,sv_to,sv_direction;
static size_t sv_bytes;
static int sv_width(void) {return (sv_frame.orientation&1u)?sv_frame.width:sv_frame.height;}
static void sv_close(void) {
    if(sv_video) sv_video->stop();
    for(unsigned i=0;i<3;++i) if(sv_pages[i]) {api->psram_free(sv_pages[i]);sv_pages[i]=NULL;}
    sv_enabled=false;
}
static bool sv_open(void) {
    const size_t required=offsetof(t5_app_api_v1,touch_contact)+sizeof(api->touch_contact);
    if(api->struct_size<required || !api->copy_ui_frame || !api->touch_contact ||
       !api->psram_alloc || !api->psram_free || !api->copy_ui_frame(NULL,0,&sv_frame)) return false;
    sv_video=t5_video_get_api(T5_VIDEO_API_VERSION);
    if(!sv_video || sv_video->struct_size<offsetof(t5_video_api_v1,start_format)+sizeof(sv_video->start_format) ||
       !sv_video->start_format || !sv_video->backbuffer || !sv_video->can_submit ||
       !sv_video->submit || !sv_video->stop) {sv_video=NULL;sv_fatal=true;return false;}
    sv_bytes=(size_t)sv_frame.stride_bytes*sv_frame.height;
    if(sv_frame.width!=960 || sv_frame.height!=540 || sv_frame.stride_bytes!=240 || sv_frame.orientation>3) {
        sv_fatal=true;return false;
    }
    for(unsigned i=0;i<3;++i) {
        sv_pages[i]=(uint8_t*)api->psram_alloc(sv_bytes);
        if(!sv_pages[i]) {sv_fatal=true;sv_close();return false;}
    }
    t5_video_surface_v1 surface={0};
    if(!sv_video->start_format(&surface,T5_VIDEO_PIXEL_GRAY_2BPP_MSB) ||
       surface.width!=sv_frame.width || surface.height!=sv_frame.height ||
       surface.stride_bytes!=sv_frame.stride_bytes || surface.pixel_format!=T5_VIDEO_PIXEL_GRAY_2BPP_MSB ||
       !(surface.flags&T5_VIDEO_FLAG_ONE_IS_BLACK)) {sv_fatal=true;sv_close();return false;}
    sv_cached_page=UINT32_MAX;sv_enabled=true;sv_dirty=false;sv_blocked=false;
    sv_tracking=sv_dragging=sv_animating=sv_vertical=false;sv_offset=0;
    sv_last_submit=api->millis()-40u;
    return true;
}
static bool sv_copy(unsigned slot,const char* status) {
    raster_page(status);
    t5_app_frame_t geometry={0};
    if(!api->copy_ui_frame(sv_pages[slot],sv_bytes,&geometry) ||
       memcmp(&geometry,&sv_frame,sizeof(geometry))) {sv_fatal=true;return false;}
    return true;
}
static void sv_refresh(const char* status) {
    const uint32_t saved=selected,page=current_page(),pages=page_count();
    const bool visible=selection_visible;
    if(sv_cached_page!=page || sv_cached_edit!=edit_mode) {
        selection_visible=false;
        selected=((page+pages-1u)%pages)*(uint32_t)page_size;
        if(!sv_copy(1,NULL)) goto restore;
        selected=((page+1u)%pages)*(uint32_t)page_size;
        if(!sv_copy(2,NULL)) goto restore;
    }
restore:
    selected=saved;selection_visible=visible;
    if(sv_fatal || !sv_copy(0,status)) return;
    sv_cached_page=page;sv_cached_edit=edit_mode;sv_dirty=true;
}
static void sv_animate(int target,uint32_t now) {
    sv_from=sv_offset;sv_to=target;sv_anim_start=now;
    sv_direction=target<0?1:(target>0?-1:0);
    sv_animating=true;sv_dirty=true;
}
// Returns true while consuming a drag/settle so no dragged icon can launch.
static bool sv_input(const t5_app_input_t* input,bool swiped,const t5_app_swipe_t* swipe) {
    (void)input;
    if(!sv_enabled) return false;
    const uint32_t now=api->millis();
    bool consumed=sv_animating || sv_dragging;
    t5_app_contact_t contact={0};
    if(!api->touch_contact(&contact)) contact.down=false;
    if(sv_animating) {
        const uint32_t elapsed=now-sv_anim_start;
        if(elapsed>=180u) {
            sv_offset=sv_to;
            if(sv_direction) {change_page(sv_direction>0);sv_refresh(NULL);}
            sv_offset=0;sv_animating=false;sv_dirty=true;
            // A contact started during settling must lift before becoming a tap.
            sv_tracking=contact.down;sv_dragging=contact.down;sv_vertical=contact.down;
            sv_x=contact.x;sv_y=contact.y;sv_started=now;
        } else {
            const int remain=(180-(int)elapsed)*1024/180;
            const int eased=1024-((remain*remain/1024)*remain/1024);
            sv_offset=sv_from+(sv_to-sv_from)*eased/1024;
            sv_dirty=true;
        }
    } else if(contact.down) {
        if(!sv_tracking) {sv_tracking=true;sv_dragging=false;sv_vertical=false;sv_x=contact.x;sv_y=contact.y;sv_started=now;}
        const int dx=contact.x-sv_x,dy=contact.y-sv_y;
        if(!sv_dragging && !sv_vertical && abs(dy)>12 && abs(dy)>=abs(dx)) sv_vertical=true;
        if(!sv_vertical && page_count()>1 && abs(dx)>=12 && abs(dx)>2*abs(dy)) sv_dragging=true;
        if(sv_dragging && !sv_vertical) {
            const int prior=sv_offset;
            sv_offset=dx;
            if(sv_offset>sv_width()) sv_offset=sv_width();
            if(sv_offset < -sv_width()) sv_offset=-sv_width();
            if(prior/4!=sv_offset/4) sv_dirty=true;
            consumed=true;
        }
    } else {
        const bool was_dragging=sv_dragging;
        int dx=0,dy=0;
        if(swiped) {dx=(int)swipe->end_x-swipe->start_x;dy=(int)swipe->end_y-swipe->start_y;}
        const bool horizontal=swiped && abs(dx)>=50 && abs(dx)>2*abs(dy) && page_count()>1;
        if(was_dragging || horizontal) {
            // Explicit completed swipe confirms continuity; GAP/outage snaps back.
            const uint32_t duration=now-sv_started;
            const bool commit=horizontal && (!sv_tracking || abs(dx)>=sv_width()/5 ||
                (duration<=350u && abs(dx)>=80));
            sv_animate(commit ? (dx<0?-sv_width():sv_width()) : 0,now);
            consumed=true;
        }
        sv_tracking=sv_dragging=sv_vertical=false;
    }
    if(swiped) consumed=true;
    return consumed;
}
static void sv_present(void) {
    if(!sv_enabled || !sv_dirty || sv_fatal) return;
    const uint32_t now=api->millis();
    if(now-sv_last_submit<40u) return;
    if(!sv_video->can_submit()) {
        if(!sv_blocked) {sv_blocked=true;sv_blocked_since=now;}
        if(now-sv_blocked_since>=2500u) sv_fatal=true;
        return;
    }
    size_t bytes=0;
    uint8_t* target=sv_video->backbuffer(&bytes);
    if(!target || bytes<sv_bytes) {sv_fatal=true;return;}
    springboard_compose(target,sv_pages[0],sv_pages[1],sv_pages[2],sv_frame.width,sv_frame.height,sv_frame.orientation,sv_offset);
    if(sv_video->submit(0,0)) {sv_last_submit=now;sv_dirty=false;sv_blocked=false;}
    else {
        if(!sv_blocked) {sv_blocked=true;sv_blocked_since=now;}
        if(now-sv_blocked_since>=2500u) sv_fatal=true;
    }
}
