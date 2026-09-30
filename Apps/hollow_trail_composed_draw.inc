/* Chapter-scale composition. These are continuous depth planes, not tiles of
 * small props. Lighting is evaluated before actors/UI and follows the world. */
typedef struct {int16_t light_x,light_y;uint8_t spread,shade;int16_t ridge[9];} ht_composition;
static const ht_composition ht_compositions[HT_LEVELS]={
 {370,-80,150,44,{140,72,116,40,93,18,85,38,125}}, /* forest clearings */
 {340,-90,110,56,{130,35,78,-45,18,-88,58,-20,110}}, /* city canyon */
 {350,10,180,35,{160,145,91,124,63,111,80,149,177}}, /* oil basin */
 {360,25,130,46,{188,169,145,183,139,166,118,155,193}}, /* railway cutting */
 {330,10,160,34,{180,145,170,131,162,115,143,158,178}}, /* marsh */
 {380,-100,110,58,{93,-14,48,-66,26,-94,12,-30,119}}, /* quarry amphitheatre */
 {320,-60,130,45,{157,98,118,56,109,63,131,90,165}}, /* glasshouse valley */
 {370,-100,100,60,{116,54,-40,25,-102,-24,72,-53,140}}, /* dam gorge */
 {360,-160,150,44,{98,-105,10,-175,-37,-214,-61,-137,104}}, /* mountain ranges */
 {340,-100,95,65,{100,22,-80,-32,-152,-60,8,-41,126}} /* final tower */
};
static int ht_composed_ridge(const ht_composition *c,int x,int plane) {
    int q=ht_floor_div(x+600,340),u=x+600-q*340;
    int n=((q%8)+8)%8;
    if(plane!=2)u=u*u/340*(1020-2*u)/340;
    return c->ridge[n]+(c->ridge[n+1]-c->ridge[n])*u/340+plane*34;
}
static void ht_composed_backdrop(const ht_game *g) {
    if(g->level==4 && g->camera/256<=900)return; /* preserve the authored grotto */
    const ht_composition *c=&ht_compositions[g->level];
    int cx=g->camera/256,cy=g->camera_y/256;
    /* Three distant openings carry the composition across the full chapter. */
    int lx=ht_project_x(c->light_x-cx*26/100),ly=ht_project_y(c->light_y-cy*26/100);
    static int16_t ridges[3][HT_W]; /* Keep large raster scratch off the app stack. */
    for(int p=0;p<3;++p)for(int x=0;x<HT_W;++x) {
        int parallax=18+p*19;
        int wx=240+(x-240)*256/ht_world_scale+cx*parallax/100;
        ridges[p][x]=ht_project_y(ht_composed_ridge(c,wx,p)-cy*parallax/100);
    }
    for(int y=0;y<HT_H;++y) {
        for(int x=0;x<HT_W;++x) {
            int dx=ht_min(ht_abs(x-lx),ht_min(ht_abs(x-lx-286*ht_world_scale/256),ht_abs(x-lx-572*ht_world_scale/256))),dy=ht_abs(y-ly);
            int radial=ht_min(180,(dx*dx+dy*dy/2)/ht_max(1,c->spread*5));
            int ink=ht_clamp(8+radial+c->shade/3,10,155);
            for(int p=0;p<3;++p)if(y>=ridges[p][x]) {
                int depth=y-ridges[p][x];
                ink=ht_max(ink,58+p*43+ht_min(32,depth/5));
            }
            if(g->level==0)ht_scene[y*HT_W+x]=(uint8_t)ink;
            else {
                /* Retain the existing thematic architecture in the lit field. */
                int old=ht_scene[y*HT_W+x];
                ht_scene[y*HT_W+x]=(uint8_t)ht_clamp(ht_max(ink,old*3/4+radial/5),0,245);
            }
        }
        if((y&15)==15)ht_checkpoint();
    }
    /* Sparse, monumental mid-distance silhouettes rooted into their own hills. */
    if(g->level==0 || g->level==6 || g->level==8) {
        for(int p=0;p<2;++p) {
            int parallax=30+p*25,spacing=225-p*42;
            int offset=cx*parallax/100;
            int begin=ht_floor_div(offset-ht_view_margin()-180,spacing);
            int end=ht_floor_div(offset+HT_W+ht_view_margin()+180,spacing);
            for(int n=begin;n<=end;++n) {
                unsigned seed=ht_hash((unsigned)n+91u+p*719u);
                int wx=n*spacing+(int)(seed%63),base=ht_composed_ridge(c,wx,p+1);
                ht_tree(ht_scene,wx-offset,base-cy*parallax/100,270+(int)(seed%180),11+p*5,seed,95+p*57);
            }
            ht_checkpoint();
        }
    }
    /* Forest shafts illuminate mist between silhouettes, not the player/HUD. */
    if(g->level==0) {
        for(int y=0;y<HT_H;++y) {
            int beam=lx+(y-ly)/3;
            for(int x=0;x<HT_W;++x) {
                int d=ht_min(ht_abs(x-beam),ht_abs(x-beam-89));
                int strength=ht_max(0,30-d)*ht_clamp(y+40,0,160)/160;
                int ink=ht_scene[y*HT_W+x];
                if(ink<195)ht_scene[y*HT_W+x]=(uint8_t)ht_max(6,ink-strength);
            }
            if((y&15)==15)ht_checkpoint();
        }
    }
}
static void ht_canopy_mass(int x,int y,int radius,unsigned seed,int tone) {
    for(int dx=-radius;dx<=radius;++dx) {
        int h=(int)ht_isqrt((unsigned)(radius*radius-dx*dx));
        int cell=ht_floor_div(dx,9),u=dx-cell*9;
        int a=(int)(ht_hash(seed+(unsigned)cell)%13),b=(int)(ht_hash(seed+(unsigned)(cell+1))%13);
        int fringe=a+(b-a)*u/9;
        int half=h*2/3;
        ht_rect(ht_scene,x+dx,y-half-fringe/2,1,ht_max(1,half*2+fringe),tone);
    }
    for(int n=0;n<3;++n)ht_leaf_cluster(ht_scene,x+(n-1)*radius/2,y+radius/2,16,seed+(unsigned)n*37,tone);
}
/* Tall trunks use consistent grip edges and limbs use the collision height.
 * Organic roots spread onto sampled soil; crowns are above the climbing route. */
static void ht_living_trees_draw(const ht_game *g) {
    int count,cx=g->camera/256,cy=g->camera_y/256;
    const ht_living_tree *trees=ht_trees(g,&count);
    for(int i=0;i<count;++i) {
        const ht_living_tree *t=&trees[i];int x=t->x-cx,base=ht_tree_base(g,t),top=base-t->height;
        if(x<-180-ht_view_margin() || x>HT_W+180+ht_view_margin())continue;
        ht_rect(ht_scene,x-t->radius,top+35-cy,t->radius*2,base-top-32,247);
        ht_limb(ht_scene,x,top+65-cy,x-13,top-28-cy,t->radius,t->radius/3,247);
        /* Broad lit bark facets and sparse winding fissures. */
        ht_limb(ht_scene,x+t->radius-5,base-15-cy,x+t->radius-8,top+40-cy,3,2,164);
        for(int k=0;k<5;++k) {
            int bx=x-t->radius+7+k*(t->radius*2-12)/5;
            for(int y=base-12;y>top+45;y-=47) {
                int lean=((y+k*11)%13)-6;
                ht_limb(ht_scene,bx,y-cy,bx+lean,y-36-cy,1,0,210);
            }
        }
        for(int side=-1;side<=1;side+=2) {
            int prev=t->x,prev_y=base-49;
            for(int step=1;step<=8;++step) {
                int wx=t->x+side*step*9,soil=ht_surface_at(g,t->parcel,wx)+3;
                int y=soil-(8-step)*(8-step)*49/64;
                ht_triangle(ht_scene,prev-cx,prev_y-cy,wx-cx,y-cy,prev-cx,soil-cy,247);
                ht_triangle(ht_scene,wx-cx,y-cy,wx-cx,soil-cy,prev-cx,soil-cy,247);
                ht_limb(ht_scene,prev-cx,prev_y-cy,wx-cx,y-cy,1,1,175);
                prev=wx;prev_y=y;
            }
        }
        for(int b=0;b<HT_TREE_BRANCHES;++b) {
            int l,r;ht_branch_bounds(t,b,&l,&r);
            int a=ht_branch_top(g,t,b,l),z=ht_branch_top(g,t,b,r-1);
            int side=ht_branch_side(b),tip=side>0?r:l;
            /* Surface begins at exactly the collision height, thickness below. */
            ht_triangle(ht_scene,l-cx,a-cy,r-cx,z-cy,l-cx,a+10-cy,247);
            ht_triangle(ht_scene,r-cx,z-cy,r-cx,z+5-cy,l-cx,a+10-cy,247);
            int root=base-58-b*(t->height-100)/HT_TREE_BRANCHES;
            ht_limb(ht_scene,x,root+15-cy,tip-cx,z+3-cy,12,2,247);
            ht_limb(ht_scene,l-cx,a+1-cy,r-cx,z+1-cy,1,1,153);
            ht_limb(ht_scene,tip-cx-side*14,z+4-cy,tip-cx+side*19,z-21-cy,3,0,247);
            if(b>1)for(int leaf=0;leaf<4;++leaf)
                ht_leaf_cluster(ht_scene,tip-cx+side*(leaf*9-8),z-25-cy-(leaf%2)*9,11+leaf%3*3,(unsigned)(i*917+b*43+leaf),226);
        }
        for(int crown=0;crown<9;++crown) {
            int dx=(crown-4)*22,dy=ht_abs(crown-4)*6;
            ht_limb(ht_scene,x,top+45-cy,x+dx,top+dy-cy,t->radius/2,3,247);
            ht_canopy_mass(x+dx,top+dy-cy,32+(crown%3)*7,(unsigned)(i*631+crown*71),235);
        }
        ht_checkpoint();
    }
}
/* Light catches the same ground that supports the feet. Each chapter uses its
 * own source position; the dark ground mass remains legible below this rim. */
static void ht_composed_contacts(const ht_game *g) {
    if(g->level==4)return;
    const ht_composition *c=&ht_compositions[g->level];
    int cx=g->camera/256,cy=g->camera_y/256;
    const ht_platform *land=ht_level_land(g->level);
    for(int i=0;i<HT_PLATFORMS;++i) {
        if(ht_terrain_kind(g->level,i)==HT_DECK)continue;
        int begin=ht_max(land[i].left+2,cx-ht_view_margin()),end=ht_min(land[i].right-2,cx+HT_W+ht_view_margin());
        for(int wx=begin;wx<end;wx+=3) {
            int l,r,flat;bool solid=false;
            for(int part=0;part<2;++part)if(ht_platform_piece(g,i,part,&l,&r,&flat) && wx>=l && wx<r)solid=true;
            if(!solid)continue;
            int distance=ht_min(ht_abs(wx-c->light_x),ht_min(ht_abs(wx-c->light_x-1100),ht_abs(wx-c->light_x-2200)));
            int strength=ht_max(0,110-distance/4);
            if(g->level==0)strength=ht_max(strength,ht_max(0,80-ht_abs(wx-1950)/5));
            if(!strength)continue;
            int y=ht_surface_at(g,i,wx)-cy;
            ht_rect(ht_scene,wx-cx,y+2,3,2,255-strength);
            if(g->level==0 && wx%21==0)ht_limb(ht_scene,wx-cx,y,wx-cx+3,y-7,1,0,170);
        }
        ht_checkpoint();
    }
}
