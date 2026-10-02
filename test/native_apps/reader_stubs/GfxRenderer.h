#pragma once
#include <stdint.h>
#include <cstring>
#include <string>
#include <vector>
#include <cassert>
inline int drawnFont=0;
inline std::vector<std::string> drawn;
class GfxRenderer {
    uint8_t *pixels=nullptr;
public:
    GfxRenderer()=default;
    GfxRenderer(const GfxRenderer&,uint8_t* p,uint16_t,uint16_t):pixels(p){}
    void ensureSdCardFontReady(int,const char*,uint8_t){}
    int getLineHeight(int f)const {return f;}
    int getTextWidth(int,const char*s)const {return static_cast<int>(strlen(s))*10;}
    std::string truncatedText(int,const char*s,int)const {return s;}
    void drawText(int font,int,int,const char*s) {
        drawnFont=font; drawn.emplace_back(s); assert(pixels); pixels[0]=0;
    }
};
