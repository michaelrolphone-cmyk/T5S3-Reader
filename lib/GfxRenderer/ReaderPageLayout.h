#pragma once
#include <stddef.h>
#include <string.h>
// Strict scalar UTF-8, bounded by the supplied byte length. Reject embedded NUL.
inline bool readerUtf8Valid(const char *text,size_t length) {
  if(!text) return false;
  for(size_t at=0;at<length;) {
    unsigned char lead=static_cast<unsigned char>(text[at++]);
    if(!lead) return false;
    if(lead<0x80) continue;
    unsigned count=lead>=0xc2 && lead<=0xdf?1:lead>=0xe0 && lead<=0xef?2:lead>=0xf0 && lead<=0xf4?3:0;
    if(!count || count>length-at) return false;
    unsigned cp=lead&((1u<<(6-count))-1u);
    for(unsigned i=0;i<count;++i) {
      unsigned char c=static_cast<unsigned char>(text[at++]);
      if((c&0xc0)!=0x80) return false;
      cp=(cp<<6)|(c&0x3f);
    }
    if((count==2 && cp<0x800) || (count==3 && cp<0x10000) ||
       (cp>=0xd800 && cp<=0xdfff) || cp>0x10ffff) return false;
  }
  return true;
}
// Allocation-free forward layout. At most 255 bytes are measured per line;
// every source byte is consumed or emitted, including oversized single words.
// Newline paragraphs retain blank lines. Never split a UTF-8 continuation.
template<class Measure,class Draw,class Checkpoint>
bool readerPageLayout(const char *text,size_t length,size_t offset,int width,
                      unsigned maxLines,Measure measure,Draw draw,Checkpoint checkpoint,
                      size_t &next,unsigned &lines) {
  next=offset; lines=0;
  if(!text || offset>length || width<1 || !maxLines ||
     (offset<length && (static_cast<unsigned char>(text[offset])&0xc0)==0x80)) return false;
  while(next<length && lines<maxLines) {
    if(!checkpoint()) return false;
    if(text[next]=='\n') { ++next; draw("",lines++); continue; }
    char line[256]; size_t at=next,used=0,space=0,spaceEnd=0;
    while(at<length && text[at]!='\n') {
      size_t end=at+1;
      while(end<length && (static_cast<unsigned char>(text[end])&0xc0)==0x80) ++end;
      size_t n=end-at;
      if(used+n>=sizeof(line)) break;
      memcpy(line+used,text+at,n); line[used+n]=0;
      const int measured=measure(line);
      if(measured>width && used) break;
      if(measured>width) return false; // Font cannot fit even one glyph.
      used+=n; at=end;
      if(line[used-1]==' ') { space=used-1; spaceEnd=at; }
    }
    if(!used) return false;
    if(at<length && text[at]!='\n' && spaceEnd>next) { used=space; at=spaceEnd; }
    while(used && line[used-1]==' ') --used;
    line[used]=0; draw(line,lines++); next=at;
    while(next<length && text[next]==' ') ++next;
    if(next<length && text[next]=='\n') ++next;
  }
  return true;
}
