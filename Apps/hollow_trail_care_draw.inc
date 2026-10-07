/* Chapter VII evening care uses the existing sloped count-room row. Geometry
 * is shared by the props and the hands; nothing here is a new collision solid. */
#define HT_CARE_X 1574
#define HT_CARE_TRAY_X 1568
#define HT_CARE_DRY_X 1540
#define HT_CARE_STEM_X 1601
#define HT_CARE_SEED_X 1616
static int ht_care_floor(int x){return ht_land_height(6,4,x);}
typedef struct {int x,left,right,shim,stem,turn;bool thread;} ht_care_geometry;
static ht_care_geometry ht_care_layout(unsigned stage,unsigned tick){
 ht_care_geometry a={HT_CARE_TRAY_X,ht_care_floor(HT_CARE_TRAY_X)-10,ht_care_floor(HT_CARE_TRAY_X)-7,0,0,0,false};
 int dry=ht_care_floor(HT_CARE_DRY_X)-7;
 if(stage==1){
  int lift=ht_clamp((int)tick-32,0,32)*5/32,move=ht_clamp((int)tick-64,0,64);
  a.x-=28*move/64;
  int y=ht_care_floor(a.x)-10-lift;
  if(tick>=128)y+=(int)ht_min(tick-128,32)*8/32;
  a.left=y;a.right=y+3;
 }else if(stage>=2){a.x=HT_CARE_DRY_X;a.left=dry;a.right=dry+3;}
 if(stage==3)a.shim=ht_clamp((int)tick-64,0,48)*256/48;
 else if(stage>=4)a.shim=256;
 a.right-=a.shim*3/256;
 if(stage==5){a.stem=ht_clamp((int)tick-144,0,32)*256/32;a.thread=tick>=192;}
 else if(stage>=6){a.stem=256;a.thread=true;}
 if(stage==7)a.turn=ht_clamp((int)tick-64,0,96)*256/96;
 else if(stage>=8)a.turn=256;
 return a;
}
static void ht_care_rows(const ht_game *g){
 unsigned stage=g->care_view_active?g->care_view_stage:g->care_tended?8:0,tick=g->care_view_active?g->care_view_tick:0;
 ht_care_geometry a=ht_care_layout(stage,tick);int cx=g->camera/256,cy=g->camera_y/256;
 int x=a.x-cx,l=a.left-cy,r=a.right-cy,source=HT_CARE_TRAY_X-cx,dry=HT_CARE_DRY_X-cx;
 /* Broken gutter and one slow drop remain fixed when the wet tray is moved. */
 int f=ht_care_floor(HT_CARE_TRAY_X)-cy;
 ht_limb(ht_scene,source-25,f-63,source+5,f-57,2,2,181);ht_limb(ht_scene,source+5,f-57,source+3,f-52,1,1,193);
 unsigned ambient=g->care_view_active?g->care_view_ambient:g->ticks&127u;
 if(ambient>=112)ht_limb(ht_scene,source+3,f-48+(int)(ambient-112)*33/16,source+3,f-46+(int)(ambient-112)*33/16,1,1,49);
 ht_limb(ht_scene,source-6,f-1,source+9,f+1,1,1,87);
 /* Low fixed rests, each rooted in the real sloping surface. */
 for(int n=0;n<2;++n){int xx=dry+(n?9:-9),ground=ht_care_floor(HT_CARE_DRY_X+(n?9:-9))-cy;
  ht_rect(ht_scene,xx-2,ground-4,4,4,190);}
 ht_limb(ht_scene,x-10,l,x+10,r,2,2,207);ht_limb(ht_scene,x-9,l+3,x+9,r+3,2,2,194);
 ht_limb(ht_scene,x-8,l-1,x+8,r-1,1,1,68); /* Water pooled above saturated soil. */
 for(int n=0;n<3;++n){int xx=x-6+n*6,yy=l+(r-l)*(n+1)/4;ht_limb(ht_scene,xx,yy,xx-2,yy-7,1,1,189);ht_limb(ht_scene,xx-2,yy-6,xx-5,yy-8,1,0,155);}
 /* Scraps come from the row, never a new required inventory item. */
 if(a.shim){int yy=ht_care_floor(HT_CARE_DRY_X)-cy-1;ht_triangle(ht_scene,dry+5,yy,dry+12,yy,dry+12,yy-a.shim*3/256,158);ht_limb(ht_scene,dry+5,yy+1,dry+13,yy+1,1,1,183);}
 if(stage<3 || (stage==3 && tick<48)){int xx=dry+10,yy=ht_care_floor(HT_CARE_DRY_X+10)-cy-1;ht_limb(ht_scene,xx,yy,xx+7,yy-1,1,1,171);ht_limb(ht_scene,xx+1,yy-2,xx+5,yy-3,1,1,152);}
 /* The fallen stem rises around its rooted base; the cuff thread holds it
  * loosely against an existing cane only after the tying hand arrives. */
 int sx=HT_CARE_STEM_X-cx,sf=ht_care_floor(HT_CARE_STEM_X)-cy;
 ht_rect(ht_scene,sx-8,sf-3,15,3,199);ht_limb(ht_scene,sx+2,sf-2,sx+2,sf-20,1,1,159);
 int tx=sx-11+a.stem*11/256,ty=sf-6-a.stem*10/256;
 ht_limb(ht_scene,sx,sf-3,tx,ty,1,1,198);ht_limb(ht_scene,tx,ty,tx-4,ty-4,2,0,163);ht_limb(ht_scene,tx,ty,tx+3,ty-4,2,0,154);
 if(a.thread){ht_limb(ht_scene,sx-2,sf-13,sx+4,sf-14,1,1,54);ht_limb(ht_scene,sx+4,sf-14,sx-2,sf-15,0,0,224);}
 /* Seed tray turning exposes the other face of the leaves to the clear pane.
  * Rim notch travels with the turn; fixed slate/wire do not rotate with it. */
 int px=HT_CARE_SEED_X-cx,pf=ht_care_floor(HT_CARE_SEED_X)-cy;
 ht_rect(ht_scene,px-9,pf-5,19,4,192);ht_limb(ht_scene,px-9,pf-6,px+10,pf-6,1,1,217);
 ht_rect(ht_scene,px-7+a.turn*14/256,pf-4,2,2,48);
 for(int n=0;n<3;++n){int bx=px-6+n*6,by=pf-7,lean=-7+a.turn*12/256;ht_limb(ht_scene,bx,by,bx+lean,by-12-n%2*3,1,1,211);ht_limb(ht_scene,bx+lean,by-12,bx+lean-3,by-16,2,0,156);ht_limb(ht_scene,bx+lean,by-12,bx+lean+4,by-15,2,0,169);}
 for(int n=0;n<2;++n){int xx=n?1637:1580,yy=ht_care_floor(xx)-cy;ht_rect(ht_scene,xx-cx,yy-14,7,13,217);ht_text_color(xx-cx+1,yy-12,"7",1,62);}
}
