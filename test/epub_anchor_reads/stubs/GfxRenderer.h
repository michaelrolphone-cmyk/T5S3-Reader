#pragma once
#include <EpdFontFamily.h>
#include <string>
#include <vector>
#include <cstdint>
class GfxRenderer {public:
struct TextPrefixMetrics {static constexpr size_t MAX_BYTES=200;int plain[201]{},hyphenated[201]{};int advanceAt(size_t,bool=false)const{return 0;}};
bool getTextPrefixMetrics(int,const std::string&,TextPrefixMetrics&,EpdFontFamily::Style)const{return false;}
bool isSdCardFont(int)const{return false;}
void ensureSdCardFontReady(int,const std::vector<std::string>&,bool,uint8_t)const{assert(false);}
int getTextWidth(int,const char*s,EpdFontFamily::Style=EpdFontFamily::REGULAR)const{return strlen(s)*6;}
int getTextAdvanceX(int f,const char*s,EpdFontFamily::Style st=EpdFontFamily::REGULAR)const{return getTextWidth(f,s,st);}
int getSpaceWidth(int,EpdFontFamily::Style)const{return 6;}
int getSpaceAdvance(int,uint32_t,uint32_t,EpdFontFamily::Style)const{return 6;}
int getKerning(int,uint32_t,uint32_t,EpdFontFamily::Style)const{return 0;}
int getFontAscenderSize(int)const{return 24;}
int getLineHeight(int)const{return 30;}
void drawText(int,int,int,const char*,bool,EpdFontFamily::Style)const{}
void drawLine(int,int,int,int,bool)const{}
};
