#include "../../lib/GfxRenderer/ReaderPageLayout.h"
#include <cassert>
#include <string>
#include <vector>
#include <cstdio>
static int measure(const char *s) {
    int n=0;
    for(;*s;++s) if((static_cast<unsigned char>(*s)&0xc0)!=0x80) n+=*s=='W'?3:1;
    return n;
}
int main() {
    assert(readerUtf8Valid("Caf\xc3\xa9",5));
    assert(readerUtf8Valid("\xf0\x9f\x8c\xab",4));
    assert(!readerUtf8Valid("\xc0\xaf",2));
    assert(!readerUtf8Valid("\xed\xa0\x80",3));
    assert(!readerUtf8Valid("\xf4\x90\x80\x80",4));
    assert(!readerUtf8Valid("\xc3",1));
    assert(!readerUtf8Valid("\x80",1));
    const std::string text="Wide WWW words\n\nCaf\xc3\xa9 and mist.\nUnbrokenlongword.";
    std::vector<std::string> all;
    size_t offset=0; unsigned pages=0;
    do {
        size_t next=0; unsigned lines=0;
        assert(readerPageLayout(text.c_str(),text.size(),offset,10,2,measure,
            [&](const char*s,unsigned){ all.emplace_back(s); assert(measure(s)<=10); },
            [](){return true;},next,lines));
        assert(next>offset && lines<=2); offset=next; assert(++pages<32);
    } while(offset<text.size());
    std::string joined;
    for(const auto&s:all) joined+=s;
    assert(joined=="WideWWWwordsCaf\xc3\xa9 andmist.Unbrokenlongword.");
    assert(all[3].empty()); // Blank paragraph preserved across a page boundary.
    size_t next; unsigned lines;
    assert(!readerPageLayout("\xc3\xa9",2,1,10,1,measure,[](const char*,unsigned){},[](){return true;},next,lines));
    assert(!readerPageLayout("W",1,0,2,1,measure,[](const char*,unsigned){},[](){return true;},next,lines));
    assert(!readerPageLayout("abc",3,0,10,1,measure,[](const char*,unsigned){},[](){return false;},next,lines));
    assert(readerPageLayout("",0,0,10,1,measure,[](const char*,unsigned){assert(false);},[](){return true;},next,lines));
    std::string huge(1024,'x'); offset=0;
    while(offset<huge.size()) {
        assert(readerPageLayout(huge.c_str(),huge.size(),offset,1000,1,measure,
            [](const char*s,unsigned){assert(strlen(s)<=255);},[](){return true;},next,lines));
        assert(next>offset); offset=next;
    }
    puts("Reader page layout: wrapping, UTF-8, paragraphs, forward progress and cancellation PASS");
}
