#include <cassert>
#include <cstdio>
#include <vector>
#include "../../Apps/springboard_slide.h"
#include "native/NativeUiFrame.h"
static int pixel(const uint8_t* data,int x,int y,int width){return (data[y*(width/4)+x/4]>>(6-2*(x%4)))&3;}
static void physical(int x,int y,unsigned r,int w,int h,int& px,int& py){
 switch(r){case 0:px=y;py=h-1-x;break;case 1:px=w-1-x;py=h-1-y;break;case 2:px=w-1-y;py=x;break;default:px=x;py=y;}
}
int main(){
 const int w=160,h=144;
 std::vector<uint8_t> pages[3],out(w*h/4);
 for(int p=0;p<3;++p){pages[p].resize(w*h/4);for(int i=0;i<w*h/4;++i)pages[p][i]=(uint8_t)(i*31+p*97);}
 for(unsigned r=0;r<4;++r){
  const int lw=(r&1)?w:h,lh=(r&1)?h:w;
  for(int offset=-lw;offset<=lw;offset+=4){
   springboard_compose(out.data(),pages[0].data(),pages[1].data(),pages[2].data(),w,h,r,offset);
   for(int y=0;y<lh;++y)for(int x=0;x<lw;++x){
    int sx=x,p=0;
    if(y>=64 && y<lh-64){sx=x-offset;if(sx<0){sx+=lw;p=1;}else if(sx>=lw){sx-=lw;p=2;}}
    int px,py,ox,oy;physical(sx,y,r,w,h,px,py);physical(x,y,r,w,h,ox,oy);
    assert(pixel(out.data(),ox,oy,w)==pixel(pages[p].data(),px,py,w));
   }
  }
 }
 uint8_t base[1]={0x80},lsb[1]={0x60},msb[1]={0x30},packed[2]={};
 nativeUiPackRow(packed,base,lsb,msb,8,false);
 const int expected[]={0,2,2,1,3,3,3,3};
 for(int x=0;x<8;++x)assert(pixel(packed,x,0,8)==expected[x]);
 nativeUiPackRow(packed,base,lsb,msb,8,true);
 for(int x=0;x<8;++x)assert(pixel(packed,x,0,8)==expected[7-x]);
 puts("springboard composition: every pixel, all rotations, both directions, fixed chrome and gray/flip packing PASS");
}
