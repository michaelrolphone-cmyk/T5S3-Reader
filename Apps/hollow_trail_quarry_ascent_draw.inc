/* The second ladder opens a backward view through the actual cuttings.
 * The opening follows near rock at full world speed; the country moves at
 * quarter speed. No camera jump, timer, evidence or traversal state is used. */
static void ht_quarry_country(const ht_game *g){
 if(g->level!=5)return;
 int cx=g->camera/256,cy=g->camera_y/256,margin=ht_view_margin();
 int left=ht_max(1600,cx-margin),right=ht_min(2320,cx+HT_W+margin);
 if(left>=right)return;
 static const int16_t ridge[][2]={{120,55},{220,42},{300,49},{360,37},{410,44},{480,39},{540,55},{620,44},{700,60},{810,43},{970,54}};
 for(int world=left;world<=right;++world){
  int x=world-cx,far=world-cx*3/4;
  /* Irregular shoulders of the missing hill reveal the country gradually. */
  int edge=ht_min(world-1600,2320-world);
  int roof=81-edge/2+(int)(ht_hash((unsigned)(world/23)+703)%5)-cy;
  int horizon=55;
  for(unsigned n=1;n<sizeof(ridge)/sizeof(ridge[0]);++n)if(far<=ridge[n][0]){
   int span=ridge[n][0]-ridge[n-1][0],u=ht_clamp(far-ridge[n-1][0],0,span);
   horizon=ridge[n-1][1]+(ridge[n][1]-ridge[n-1][1])*u/span;break;
  }
  int distant=horizon-cy/4,field=70+(far-400)/35-cy/4;
  int bend=ht_abs(far-630)/8-ht_abs(far-730)/10;
  int marsh=102+bend-cy/4,bank=marsh+4+ht_abs(far-680)/100;
  /* The upper bench hides this country from the lower approach. Its
   * real world height supplies the aperture bottom, not a picture edge. */
  int floor=ht_level_land(5)[6].top-cy;
  int bounds[5]={distant,field,marsh,bank,floor},tones[5]={39,91,119,33,151};
  int top=roof;
  for(int band=0;band<5;++band){
   int end=ht_max(top,ht_min(bounds[band],floor));
   if(end>top)ht_rect(ht_scene,x,top,1,end-top,tones[band]);
   top=end;
  }
  /* The railway has contracted to one line across the flat oil country. */
  int rail=field+18;
  if(rail>=roof && rail<marsh && rail<floor)ht_rect(ht_scene,x,rail,1,1,166);
  /* Dull elliptical tank roofs, with short shaded sides, sit beyond the marsh. */
  static const int tank_x[5]={554,588,624,655,695};
  for(int tank=0;tank<5;++tank){
   int dx=far-tank_x[tank],r=tank%2?8:10;
   if(ht_abs(dx)>r)continue;
   int arc=ht_max(1,3*(r*r-dx*dx)/(r*r));
   int ty=field+3-(tank%2)*3;
   int a=ht_max(roof,ty-arc),b=ht_min(floor,ty+arc);
   if(b>a)ht_rect(ht_scene,x,a,1,b-a,184);
   if(ty+arc>=roof && ty+arc+2<=floor)ht_rect(ht_scene,x,ty+arc,1,2,157);
  }
  if((world&31)==0){ht_checkpoint();if(ht_abort)return;}
 }
 ht_checkpoint();
}
static void ht_quarry_ascent_traces(const ht_game *g){
 if(g->level!=5)return;
 int cx=g->camera/256,cy=g->camera_y/256,margin=ht_view_margin();
 if(1137-cx< -margin || 1058-cx>HT_W+margin)return;
 /* Old lime lies on the original bench; the wall-side row stays above the
  * adult tracks in this shallow view across its exposed outer edge. */
 for(int wx=1058;wx<=1137;++wx){
  int floor=ht_surface_at(g,3,wx)-cy;
  int taper=ht_min(wx-1058,1137-wx);
  ht_rect(ht_scene,wx-cx,floor-1,1,ht_min(12,taper/2+2),82);
  if(wx%7==0)ht_rect(ht_scene,wx-cx,floor+2,2,1,123);
 }
 static const int small_x[3]={1080,1097,1114};
 for(int i=0;i<3;++i){
  int x=small_x[i]-cx,y=ht_surface_at(g,3,small_x[i])-cy+1;
  /* Three discrete heel-and-toe impressions, with a gap at each instep. */
  ht_limb(ht_scene,x,y,x+3,y+(i%2),1,1,202);
  ht_rect(ht_scene,x-3,y,2,2,196);
 }
 static const int adult_x[4]={1069,1090,1110,1128};
 for(int i=0;i<4;++i){
  int x=adult_x[i]-cx,y=ht_surface_at(g,3,adult_x[i])-cy+7;
  ht_limb(ht_scene,x,y,x+5,y+(i%2),1,1,179);
  ht_rect(ht_scene,x-4,y,2,3,169);
 }
 /* Two short pins and tied cloth at a child's shoulder height. No rail is
  * drawn above/below it: its shallow sag cannot read as a bearing cable. */
 for(int end=0;end<2;++end){
  int wx=end?HT_QUARRY_CLOTH_RIGHT:HT_QUARRY_CLOTH_LEFT;
  int y=ht_quarry_cloth_y(wx)-cy,x=wx-cx;
  ht_limb(ht_scene,x,ht_surface_at(g,3,wx)-cy+8,x,y-2,0,0,164);
  ht_disk(ht_scene,x,y,1,225);
  ht_limb(ht_scene,x,y,x+(end?-3:3),y+5,0,0,212);
 }
 for(int wx=HT_QUARRY_CLOTH_LEFT;wx<HT_QUARRY_CLOTH_RIGHT;++wx){
  int y=ht_quarry_cloth_y(wx)-cy;
  ht_rect(ht_scene,wx-cx,y,1,2,221);
  if(wx%5==0)ht_rect(ht_scene,wx-cx,y,1,1,122);
 }
 ht_checkpoint();
}
