#include "NativeReaderTypography.h"
#include "CrossPointSettings.h"
#include "SdCardFontGlobals.h"
#include "activities/ActivityManager.h"
#include <GfxRenderer.h>
#include <ReaderPageLayout.h>
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <algorithm>
#include <cstring>

bool nativeReaderPage(const risc_reader_page_request_v1 *q,risc_reader_page_result_v1 *out) {
  if(out) *out={};
  if(!q || !out || !q->pixels || !q->text || !q->title || !q->footer ||
     q->width<320 || q->width>2048 || q->height<240 || q->height>2048 || q->width%8 ||
     q->stride!=q->width/8 || q->capacity<static_cast<size_t>(q->stride)*q->height ||
     q->capacity>256u*1024u || q->length>RISC_READER_TEXT_MAX || q->offset>q->length ||
     strnlen(q->title,129)>128 || strnlen(q->footer,129)>128 ||
     strnlen(q->text,q->length+1)!=q->length) return false;
  if(!readerUtf8Valid(q->text,q->length) || !readerUtf8Valid(q->title,strlen(q->title)) ||
     !readerUtf8Valid(q->footer,strlen(q->footer))) return false;
  const uint32_t started=millis();
  ensureSdFontLoaded();
  vTaskDelay(1);
  if(millis()-started>=5000u) return false;
  auto &host=activityManager.nativeAppRenderer();
  const int font=SETTINGS.getReaderFontId();
  // Prepare the small, bounded body before copying SD font registrations.
  // Bounded UTF-8 chunks keep SD metric preparation cooperative too.
  for(size_t at=0;at<q->length;) {
    size_t end=std::min(at+240u,q->length);
    while(end<q->length && (static_cast<unsigned char>(q->text[end])&0xc0)==0x80) --end;
    if(end==at) return false;
    char chunk[241]; memcpy(chunk,q->text+at,end-at); chunk[end-at]=0;
    host.ensureSdCardFontReady(font,chunk,1); at=end;
    vTaskDelay(1); if(millis()-started>=5000u) return false;
  }
  host.ensureSdCardFontReady(font,q->title,1);
  host.ensureSdCardFontReady(font,q->footer,1);
  GfxRenderer page(host,q->pixels,q->width,q->height);
  const int natural=page.getLineHeight(font);
  const int line=std::max(1,static_cast<int>(natural*SETTINGS.getReaderLineCompression()));
  if(natural<=0 || line>(q->height-96)/4) return false;
  const int margin=32,top=margin+line*2,bottom=q->height-margin-line*2;
  if(bottom<=top) return false;
  memset(q->pixels,0xff,static_cast<size_t>(q->stride)*q->height);
  page.drawText(font,margin,margin,page.truncatedText(font,q->title,q->width-2*margin).c_str());
  page.drawText(font,margin,q->height-margin-line,
                page.truncatedText(font,q->footer,q->width-2*margin).c_str());
  size_t next=q->offset; unsigned lines=0;
  const bool okay=readerPageLayout(q->text,q->length,q->offset,q->width-2*margin,
      static_cast<unsigned>((bottom-top)/line),
      [&](const char *s){ return page.getTextWidth(font,s); },
      [&](const char *s,unsigned n){ page.drawText(font,margin,top+static_cast<int>(n)*line,s); },
      [&](){ vTaskDelay(1); return millis()-started<5000u; },next,lines);
  if(!okay) return false;
  // Renderer masks use zero for black; the capability contract uses one.
  for(unsigned y=0;y<q->height;++y) {
    for(unsigned x=0;x<q->stride;++x) q->pixels[y*q->stride+x]^=0xff;
    if((y&31)==31) vTaskDelay(1);
  }
  out->next_offset=next; out->line_height=static_cast<uint16_t>(line);
  out->lines=static_cast<uint16_t>(lines); return true;
}
