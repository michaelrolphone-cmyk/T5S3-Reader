/* VI. Human traces belong to the first upper bench, not to the hoist.
 * These are render contacts over the original surface, never new supports. */
#define HT_QUARRY_CLOTH_LEFT 1065
#define HT_QUARRY_CLOTH_RIGHT 1130
static int ht_quarry_cloth_y(int world){
 int u=ht_clamp(world-HT_QUARRY_CLOTH_LEFT,0,HT_QUARRY_CLOTH_RIGHT-HT_QUARRY_CLOTH_LEFT);
 int left=ht_land_height(5,3,HT_QUARRY_CLOTH_LEFT)-17;
 int right=ht_land_height(5,3,HT_QUARRY_CLOTH_RIGHT)-17;
 return left+(right-left)*u/65+4*u*(65-u)/(65*65);
}
static bool ht_quarry_cloth_contact(const ht_game *g){
 int x=g->x/256;
 return g->level==5 && g->grounded && g->traversal.mode==HT_FREE &&
  !g->traversal.support && !g->traversal.push_hint && !g->traversal.forest_push_release &&
  x>=HT_QUARRY_CLOTH_LEFT+6 && x<=HT_QUARRY_CLOTH_RIGHT-6 &&
  ht_abs(g->y/256-ht_surface_at(g,3,x))<=1;
}
