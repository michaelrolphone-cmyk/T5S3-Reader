#pragma once
#include <cstddef>
#include <cstdint>
// Pack one physical row using the same white-mask/gray precedence as HalDisplay.
inline void nativeUiPackRow(uint8_t* destination,const uint8_t* base,
                            const uint8_t* lsb,const uint8_t* msb,
                            size_t width,bool reverse) {
  for(size_t x=0;x<width;x+=4) {
    uint8_t packed=0;
    for(size_t p=0;p<4;++p) {
      const size_t source=reverse ? width-1-x-p : x+p;
      const uint8_t mask=0x80u>>(source&7u);
      const size_t i=source/8;
      const uint8_t tone=(base[i]&mask)?0:((lsb && (lsb[i]&mask))?2:((msb && (msb[i]&mask))?1:3));
      packed|=tone<<(6-2*p);
    }
    destination[x/4]=packed;
  }
}
