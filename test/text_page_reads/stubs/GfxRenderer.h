#pragma once
#include <EpdFontFamily.h>
#include <string>
#include <vector>
class GfxRenderer {public:
 mutable std::vector<std::string> commands;
 void drawText(int font,int x,int y,const char* text,bool black,EpdFontFamily::Style style)const{
 commands.push_back("text:"+std::to_string(font)+":"+std::to_string(x)+":"+std::to_string(y)+":"+text+":"+std::to_string(black)+":"+std::to_string(style));}
 int getTextWidth(int,const char* s,EpdFontFamily::Style style)const{return std::string(s).size()*(5+int(style));}
 int getTextAdvanceX(int f,const char* s,EpdFontFamily::Style style)const{return getTextWidth(f,s,style);}
 int getFontAscenderSize(int)const{return 12;}
 void drawLine(int x,int y,int endX,int endY,bool black)const{
 commands.push_back("line:"+std::to_string(x)+":"+std::to_string(y)+":"+std::to_string(endX)+":"+std::to_string(endY)+":"+std::to_string(black));}
};
