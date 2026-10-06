/* The page-23 cabinet occupies its original support-seven evidence site. */
#define HT_RECORDER_X 2452
static int ht_recorder_floor(void){return ht_level_land(7)[7].top;}
static ht_joint ht_recorder_latch(void){return ht_joint_at(HT_RECORDER_X+7,ht_recorder_floor()-23);}
static void ht_recorder_cabinet(const ht_game *g,unsigned opening){
 int x=HT_RECORDER_X-g->camera/256,y=ht_recorder_floor()-g->camera_y/256;
 if(x+56< -ht_view_margin() || x-27>HT_W+ht_view_margin())return;
 /* The overhang drains outside the recess; the protected face stays clean. */
 ht_rect(ht_scene,x-22,y-67,78,59,163);ht_rect(ht_scene,x-13,y-58,60,50,116);
 ht_limb(ht_scene,x-27,y-66,x+56,y-66,4,3,220);
 ht_limb(ht_scene,x-23,y-61,x-23,y-3,1,1,75);ht_limb(ht_scene,x+52,y-60,x+54,y-4,1,0,73);
 ht_rect(ht_scene,x+2,y-49,35,43,224);ht_rect(ht_scene,x+5,y-46,29,36,68);
 ht_rect(ht_scene,x+5,y-46,29,2,246);ht_rect(ht_scene,x+5,y-46,2,36,246);
 ht_rect(ht_scene,x+32,y-46,2,36,246);ht_rect(ht_scene,x+5,y-12,29,2,246);
 ht_rect(ht_scene,x+11,y-37,16,23,36);ht_rect(ht_scene,x+14,y-31,10,1,150);
 int width=27-(int)ht_min(opening,256)*25/256;
 ht_rect(ht_scene,x+6,y-45,width,34,opening?174:139);
 ht_limb(ht_scene,x+6,y-45,x+6+width,y-45,1,1,94);
 if(!opening){ht_joint c=ht_recorder_latch();ht_rect(ht_scene,c.x-g->camera/256,c.y-g->camera_y/256,3,2,240);}
 else ht_limb(ht_scene,x+6,y-45,x-4,y-39,1,1,166);
}
