/* Compile the production service; fake only the font/device dependencies. */
#include "../../src/native/NativeReaderTypography.cpp"
#include <cassert>
#include <cstdio>
#include <vector>
int main() {
    std::vector<uint8_t> pixels(960*540/8+2,0x5a);
    std::string text;
    for(unsigned i=0;i<500;++i) text+="The light stayed on. ";
    risc_reader_page_request_v1 q={pixels.data()+1,pixels.size()-2,960,540,120,"Found document","Page 1",text.c_str(),text.size(),0};
    risc_reader_page_result_v1 result{};
    assert(nativeReaderPage(&q,&result));
    assert(result.next_offset>0 && result.next_offset<text.size());
    assert(result.line_height==12 && drawnFont==12 && loads==1);
    assert(pixels.front()==0x5a && pixels.back()==0x5a && pixels[1]==0xff && pixels[2]==0);
    size_t first=result.next_offset;
    SETTINGS.font=24; SETTINGS.spacing=1.5f;
    drawn.clear(); assert(nativeReaderPage(&q,&result));
    assert(result.line_height==36 && drawnFont==24 && result.next_offset<first);
    size_t second=result.next_offset;
    q.offset=second; assert(nativeReaderPage(&q,&result)); assert(result.next_offset>second);
    q.capacity=1; assert(!nativeReaderPage(&q,&result)); assert(result.next_offset==0);
    q.capacity=pixels.size()-2; q.offset=q.length+1; assert(!nativeReaderPage(&q,&result));
    q.offset=0; q.stride=119; assert(!nativeReaderPage(&q,&result));
    q.stride=120; readerTick=5001; assert(!nativeReaderPage(&q,&result));
    puts("Reader typography: production service font/spacing selection, bitmap bounds, polarity, paging and timeout PASS");
}
