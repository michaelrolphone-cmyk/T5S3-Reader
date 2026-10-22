/* Compile the production service and composition adapter; fake only the
 * font/device dependencies. No settings or storage policy is reimplemented. */
#include "../../src/native/NativeReaderTypography.h"
#include <Arduino.h>
#include <CrossPointSettings.h>
#include <SdCardFontGlobals.h>
#include <activities/ActivityManager.h>
#include <GfxRenderer.h>
#include <ReaderPageLayout.h>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <vector>

static void reset() {
    readerClock=0; readerTick=1; loadTime=0; afterLoad=nullptr;
    loads=0; drawnFont=0; drawn.clear(); prepared.clear();
    detachedSource=nullptr; detachedCount=0;
    SETTINGS.font=12; SETTINGS.spacing=1.0f;
}

static void clearResult(risc_reader_page_result_v1& result) {
    result.next_offset=73; result.line_height=74; result.lines=75;
}

static void assertCleared(const risc_reader_page_result_v1& result) {
    assert(result.next_offset==0 && result.line_height==0 && result.lines==0);
}

int main() {
    reset();
    std::vector<uint8_t> pixels(960*540/8+2,0x5a);
    std::string text;
    for(unsigned i=0;i<500;++i) text+="The light stayed on. ";
    risc_reader_page_request_v1 q={pixels.data()+1,pixels.size()-2,960,540,120,"Found document","Page 1",text.c_str(),text.size(),0};
    risc_reader_page_result_v1 result{};
    assert(nativeReaderPage(&q,&result));
    assert(result.next_offset>0 && result.next_offset<text.size());
    assert(result.line_height==12 && drawnFont==12 && loads==1);
    assert(detachedSource==&activityManager.renderer && detachedCount==1);
    assert(pixels.front()==0x5a && pixels.back()==0x5a && pixels[1]==0xff && pixels[2]==0);
    size_t first=result.next_offset;
    SETTINGS.font=24; SETTINGS.spacing=1.5f;
    drawn.clear(); assert(nativeReaderPage(&q,&result));
    assert(result.line_height==36 && drawnFont==24 && result.next_offset<first);
    size_t second=result.next_offset;
    q.offset=second; assert(nativeReaderPage(&q,&result)); assert(result.next_offset>second);

    // Direct callers supply independent resources, not whatever global firmware
    // settings or ActivityManager renderer happen to contain. No font load occurs.
    q.offset=0; reset(); SETTINGS.font=30; SETTINGS.spacing=2.0f;
    {
        GfxRenderer source;
        const NativeReaderTypographyResources resources{source,18,1.25f};
        assert(nativeReaderPage(resources,&q,&result,readerClock));
        assert(result.line_height==22 && drawnFont==18 && loads==0);
        assert(detachedSource==&source && detachedCount==1);
        assert(!prepared.empty());
        for(const auto& part:prepared) {
            assert(part.renderer==&source && part.font==18 && part.styles==1);
        }
    }
    // A subsequent call uses its new source and selection, not a retained borrow.
    prepared.clear();
    GfxRenderer other;
    const NativeReaderTypographyResources resources{other,12,1.0f};
    assert(nativeReaderPage(resources,&q,&result,readerClock));
    assert(detachedSource==&other && drawnFont==12 && loads==0);

    // A loader may leave a selected SD font or its existing built-in fallback.
    // The adapter must resolve the selection after loading, not snapshot it early.
    reset(); SETTINGS.font=0;
    afterLoad=[](){ SETTINGS.font=14; SETTINGS.spacing=1.1f; };
    assert(nativeReaderPage(&q,&result));
    assert(loads==1 && drawnFont==14 && result.line_height==15);

    // Invalid requests have no loading, metric, target or timing side effects.
    reset(); std::fill(pixels.begin(),pixels.end(),0x5a);
    const auto valid=q;
    auto rejects=[&]() {
        clearResult(result);
        assert(!nativeReaderPage(&q,&result)); assertCleared(result);
        assert(loads==0 && prepared.empty() && detachedCount==0 && readerClock==0);
        for(auto byte:pixels) assert(byte==0x5a);
        clearResult(result);
        assert(!nativeReaderPage(resources,&q,&result,readerClock)); assertCleared(result);
        assert(prepared.empty() && detachedCount==0);
        q=valid;
    };
    q.capacity=1; rejects();
    q.offset=q.length+1; rejects();
    q.stride=119; rejects();
    q.width=319; rejects();
    q.height=239; rejects();
    q.text="\xc0\x80"; q.length=2; rejects();
    q.title="\xff"; rejects();
    q.footer="\xff"; rejects();
    q.length--; rejects();
    clearResult(result); assert(!nativeReaderPage(nullptr,&result)); assertCleared(result);
    assert(!nativeReaderPage(&q,nullptr)); assert(loads==0);

    // The original preparation delay and SD loading both consume the same
    // unsigned five-second deadline, including clock rollover.
    reset(); loadTime=5000;
    clearResult(result); assert(!nativeReaderPage(&q,&result)); assertCleared(result);
    assert(loads==1 && prepared.empty() && detachedCount==0);
    reset(); readerTick=5001;
    clearResult(result); assert(!nativeReaderPage(&q,&result)); assertCleared(result);
    assert(loads==1 && prepared.empty() && detachedCount==0);
    reset(); readerClock=5000;
    clearResult(result); assert(!nativeReaderPage(resources,&q,&result,0)); assertCleared(result);
    assert(loads==0 && prepared.empty() && detachedCount==0);
    reset(); readerClock=UINT32_MAX-1;
    assert(nativeReaderPage(&q,&result));
    assert(loads==1 && result.line_height==12 && readerClock<5000);
    reset(); readerClock=UINT32_MAX-1; loadTime=5000;
    clearResult(result); assert(!nativeReaderPage(&q,&result)); assertCleared(result);
    assert(loads==1 && readerClock<5000 && prepared.empty() && detachedCount==0);

    // Metric preparation remains cooperative and never splits UTF-8, even at
    // a 240-byte chunk boundary. Body registration precedes the detached copy.
    reset();
    std::string utf8(239,'a'); utf8+="\xe2\x82\xac"; utf8+=std::string(239,'b');
    q.text=utf8.c_str(); q.length=utf8.size();
    assert(nativeReaderPage(&q,&result));
    assert(prepared.size()==5); // three body chunks, title, footer
    std::string body;
    for(size_t i=0;i<prepared.size()-2;++i) {
        assert(prepared[i].text.size()<=240);
        assert(readerUtf8Valid(prepared[i].text.c_str(),prepared[i].text.size()));
        body+=prepared[i].text;
    }
    assert(body==utf8 && prepared[prepared.size()-2].text==q.title && prepared.back().text==q.footer);
    reset(); readerTick=2500;
    clearResult(result); assert(!nativeReaderPage(&q,&result)); assertCleared(result);
    assert(loads==1 && prepared.size()==1 && detachedCount==0);
    assert(pixels.front()==0x5a && pixels.back()==0x5a);
    puts("Reader typography: explicit resources, selection-after-load, synchronous borrowing, validation, font/spacing, bitmap bounds/polarity, paging, UTF-8 preparation and shared deadline PASS");
}
