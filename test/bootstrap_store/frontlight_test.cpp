#include <BoardX4Pro.h>
#include <cassert>
static unsigned writes;static uint16_t level;
static bool set(void*,uint16_t n,uint16_t maximum){assert(maximum==1);level=n;++writes;return true;}
static bool get(void*,uint16_t* n,uint16_t* maximum){*n=level;*maximum=1;return true;}
int main(){
 assert(!BoardX4Pro::capabilities().hasBacklight);
 BoardX4Pro::setBacklightLevel(2);assert(!writes);
 risc_frontlight_api_v1 api{1,sizeof(api),nullptr,set,get};
 auto bad=api;bad.struct_size=8;assert(!BoardX4Pro::attachFrontlight(&bad));
 assert(BoardX4Pro::attachFrontlight(&api));assert(BoardX4Pro::capabilities().hasBacklight);
 BoardX4Pro::restoreBacklightLevel(2);assert(level==1 && writes==1);
 BoardX4Pro::setBacklightLevel(0);assert(level==0 && writes==2);
 BoardX4Pro::setBacklightLevel(10);assert(level==1 && writes==3);
}
