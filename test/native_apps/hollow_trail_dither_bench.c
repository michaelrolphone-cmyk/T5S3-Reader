/* Compile with -Os -IApps -Ilib/NativeApps/include -Isdk/driver.
 * CPU timings exclude the display. All packers receive the same frozen frame. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "hollow_trail_engine.inc"
static int bit(const uint8_t *p,int x,int y){return (p[y*120+x/8]>>(7-(x&7)))&1;}
int main(void){
 uint8_t *mem=malloc(HT_MEMORY),*out[2];if(!mem)return 2;ht_bind(mem);
 for(int i=0;i<2;++i){out[i]=malloc(64800);if(!out[i])return 2;}
 uint8_t *previous[2]={malloc(64800),malloc(64800)};
 if(!previous[0]||!previous[1])return 2;
 puts("chapter,camera,ai_us,learned_us,changed_dots,block_mae,motion_ai_flips,motion_learned_flips,motion_max_disagreement");
 for(unsigned level=0;level<HT_LEVELS;++level)for(int view=0;view<3;++view){
  ht.level=level;ht_spawn(true);ht.camera=view*1100*256;ht.camera_y=(view?40:0)*256;
  ht.x=(view*1100+190)*256;ht.vista=view==1?256:0;ht.sway_phase=512;ht.rotation_phase=256u<<8;ht.ticks=33;
  ht_ai_rendering=true;ht_render_scene();clock_t elapsed[2]={0,0};
  for(int batch=0;batch<6;++batch)for(int order=0;order<2;++order){
   int mode=(order+batch)%2;clock_t start=clock();
   for(int n=0;n<20;++n){
    if(mode==0)ht_pack_mono_legacy(out[mode],120);
    else ht_pack_mono_learned(out[mode],120);
   }
   elapsed[mode]+=clock()-start;
  }
  unsigned changed=0;double block_error=0;
  for(int y=0;y<540;++y)for(int x=0;x<960;++x){
   changed+=bit(out[0],x,y)!=bit(out[1],x,y);
   if(!(x&1)&&!(y&1))assert(bit(out[0],x,y)==bit(out[1],x,y));
  }
  for(int y=0;y+8<=540;y+=8)for(int x=0;x<960;x+=8){
   int a=0,b=0;for(int dy=0;dy<8;++dy)for(int dx=0;dx<8;++dx){a+=bit(out[0],x+dx,y+dy);b+=bit(out[1],x+dx,y+dy);}
   block_error+=abs(a-b)*255./64;
  }
  unsigned flips[2]={0,0},max_disagreement=changed;
  memcpy(previous[0],out[0],64800);memcpy(previous[1],out[1],64800);
  for(int step=0;step<12;++step){
   ht.camera+=256;ht.x+=256;ht.ticks+=3;ht.sway_phase+=13;ht.rotation_phase+=19u<<8;
   ht_render_scene();ht_pack_mono_legacy(out[0],120);ht_pack_mono_learned(out[1],120);
   unsigned delta=0;
   for(int i=0;i<64800;++i){
    delta+=(unsigned)__builtin_popcount((unsigned)(out[0][i]^out[1][i]));
    for(int mode=0;mode<2;++mode)flips[mode]+=(unsigned)__builtin_popcount((unsigned)(out[mode][i]^previous[mode][i]));
   }
   if(delta>max_disagreement)max_disagreement=delta;
   memcpy(previous[0],out[0],64800);memcpy(previous[1],out[1],64800);
  }
  printf("%u,%d,%.2f,%.2f,%u,%.4f,%u,%u,%u\n",level,view*1100,
   elapsed[0]*1e6/CLOCKS_PER_SEC/120,elapsed[1]*1e6/CLOCKS_PER_SEC/120,changed,block_error/(120*67),flips[0],flips[1],max_disagreement);
  if(level==0&&view==0){
   for(int mode=0;mode<2;++mode){char name[64];snprintf(name,sizeof(name),"/tmp/hollow-dither-%d.pbm",mode);
    FILE *f=fopen(name,"wb");if(f){fprintf(f,"P4\n960 540\n");fwrite(out[mode],1,64800,f);fclose(f);}}
  }
 }
 for(int i=0;i<2;++i)free(out[i]);
 free(previous[0]);free(previous[1]);free(mem);return 0;
}
