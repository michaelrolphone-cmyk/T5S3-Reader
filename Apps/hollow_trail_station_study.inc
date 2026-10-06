typedef struct {bool active,paused,pressing;uint8_t focus,crease;} ht_station_state;
static ht_station_state ht_station;
static bool ht_station_near(const ht_game *g) {
 return g->level==3 && g->grounded && !g->traversal.mode && ht_evidence_near(g)==0;
}
static void ht_station_begin(void) {ht_station=(ht_station_state){true,false,false,0,0};}
static void ht_station_step(ht_station_state *s) {
 if(!s->active || s->paused)return;
 if(!s->focus && s->pressing){if(s->crease<24)++s->crease;}
 else if(s->crease)--s->crease;
}
static const char *ht_station_label(const ht_station_state *s) {
 if(s->paused)return "PAUSED - PRESS PAUSE TO RESUME";
 if(!s->focus && s->pressing && s->crease)return "MY PALM SMOOTHED THE FOLD.";
 static const char *labels[]={"HOME: THE FOLD RETURNED WHEN I LET GO.",
  "THE CANCELLATION PRECEDED THE COLLAPSE.",
  "THE WEATHER WARNING WAS EARLIER STILL.",
  "THE RAILS ENTERED THE FALL AND DID NOT COME OUT."};
 return labels[s->focus%4];
}
static void ht_station_study_render(const ht_game *game,const ht_station_state *s) {
 ht_native_active=ht_camera_mode==HT_CAMERA_NATIVE && ht_native_a;
 ht_native_foreground_half_y=false;ht_scene=ht_native_active?ht_native_a:ht_scene_low;ht_world_scale=256;
 for(int y=0;y<HT_H;++y){ht_rect(ht_scene,0,y,HT_W,1,132);if((y&15)==15)ht_checkpoint();}
 if(s->focus==3) {
  ht_game view=*game;view.camera=320*256;view.camera_y=12*256;
  ht_authored_backdrop(&view);ht_scene_geography(&view);
 } else {
  /* The three documents retain their positions on/below the board.
   * Focus changes contrast and contact, not their dates or printed contents. */
  ht_rect(ht_scene,75,35,330,165,193);
  for(int n=0;n<11;++n)ht_limb(ht_scene,79,41+n*14,401,41+n*14,0,0,153);
  ht_limb(ht_scene,77,36,403,36,2,2,222);
  ht_rect(ht_scene,278,43,67,47,s->focus==2?39:115);
  ht_triangle(ht_scene,278,43,283,45,279,89,s->focus==2?75:133);
  ht_disk(ht_scene,309,46,2,229);
  for(int n=0;n<4;++n)ht_rect(ht_scene,285,54+n*8,50-(n%2)*9,1,170);
  /* Stiff warning sheet and dated entry carry strokes, never invented dates. */
  ht_triangle(ht_scene,154,171,226,159,231,206,s->focus==1?31:97);
  ht_triangle(ht_scene,154,171,231,206,156,214,s->focus==1?37:103);
  ht_triangle(ht_scene,231,206,235,159,315,170,s->focus==1?43:106);
  ht_triangle(ht_scene,231,206,315,170,313,214,s->focus==1?48:113);
  ht_limb(ht_scene,232,160,231,207,1,1,172);
  for(int n=0;n<5;++n) {
   ht_limb(ht_scene,166,177+n*6,219,169+n*7,1,0,158);
   ht_limb(ht_scene,243,169+n*7,302,177+n*6,1,0,168);
  }
  ht_rect(ht_scene,167,99,112,43,s->focus?106:31);
  ht_disk(ht_scene,220,101,2,235);
  /* A real circular hole shows the same wood and its grain behind the card. */
  int crease=s->focus?147:162-(int)s->crease*5;
  /* Non-degenerate bands survive native packing independently of the hand. */
  ht_rect(ht_scene,168,120,108,1,crease);
  ht_rect(ht_scene,168,121,108,1,s->focus?100:ht_max(25,85-(int)s->crease*2));
  ht_text_color(181,112,"HOME",2,s->focus?181:204);
  ht_disk(ht_scene,263,119,4,193);
  if(!s->focus && s->crease) {
   /* Palm crosses the fold; fingertips stay on the card. Cropped forearm
    * enters from below, rather than an unconnected pointer arrow. */
   int shift=(int)s->crease,hand=278-shift;
   ht_limb(ht_scene,334,219,306,178,9,7,231);
   ht_limb(ht_scene,306,178,hand+8,140,7,5,242);
   ht_disk(ht_scene,hand+6,137,6,246);
   for(int i=0;i<4;++i)ht_limb(ht_scene,hand+i*3,138,hand+i*3-4,125+i%2,2,1,246);
   ht_limb(ht_scene,hand+10,140,hand+18,132,2,1,234);
  }
 }
 ht_world_scale=256;ht_rect(ht_scene,10,226,460,35,255);
 const char *label=ht_station_label(s);ht_text_color((HT_W-(int)strlen(label)*6)/2,230,label,1,64);
 const char *controls="L/R COMPARE  HOLD A SMOOTH  START NOTE  X RETURN";
 ht_text_color((HT_W-(int)strlen(controls)*6)/2,243,controls,1,94);
 ht_vignette();
}
