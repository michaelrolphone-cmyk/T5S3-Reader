/* Novella V: the bank after the ferry, with shallows beside (not across) the
 * existing contact surface. Nothing here changes traversal or lock state. */
#define HT_MARSH_BANK_LEFT 1540
#define HT_MARSH_BANK_RIGHT 2780
static int ht_marsh_cloud(unsigned ticks) {
    /* One slow, continuous cloud crossing: 32.768 seconds at the fixed step.
     * Its two extrema expose the same submerged wood or hide it in sky. */
    unsigned phase=ticks&1023u;
    int u=phase<=512u?(int)phase:1024-(int)phase;
    u=u*256/512;
    return u*u/256*(768-2*u)/256;
}
static int ht_marsh_exposure(const ht_game *g) {
    /* The existing grotto crossfade ends at camera 900. The next landscape
     * opens continuously after it, never at a protagonist/evidence trigger. */
    return g->level==4?ht_clamp((g->camera/256-920)*256/240,0,256):0;
}
static void ht_marsh_willow(int x,int y,int height,int tone,unsigned seed) {
    int fork=y-height*2/3;
    ht_limb(ht_scene,x+3,y,x-5,fork,4,3,tone);
    ht_limb(ht_scene,x-5,fork,x-19,y-height,3,1,tone);
    ht_limb(ht_scene,x-5,fork,x+28,y-height+12,2,0,tone);
    for(int side=-1;side<=1;side+=2)for(int n=0;n<6;++n) {
        int tip=x+side*(17+n*7)+(int)(ht_hash(seed+(unsigned)n)%7)-3;
        int top=y-height+9+n*n/3+(int)(ht_hash(seed+(unsigned)n*19)%11);
        ht_limb(ht_scene,x-7,y-height+12,tip,top,1,0,tone);
        int bend=tip+side*(7+n%3),end=top+height/3+n*2;
        ht_limb(ht_scene,tip,top,bend,top+height/7,0,0,tone-11);
        ht_limb(ht_scene,bend,top+height/7,bend-side*2,end,0,0,tone-11);
        for(int leaf=0;leaf<4;++leaf) {
            int ly=top+7+leaf*(height/12+1),lx=tip+side*(3+leaf);
            ht_detail_leaf(lx,ly,lx-side*4,ly+8,1,tone-7+(int)(seed%9));
        }
    }
    for(int root=-1;root<=1;root+=2)
        ht_limb(ht_scene,x,y-4,x+root*14,y+2,2,0,tone);
}
static void ht_marsh_backdrop(const ht_game *g) {
    int exposure=ht_marsh_exposure(g);if(!exposure)return;
    int cx=g->camera/256,cy=g->camera_y/256,margin=ht_view_margin();
    /* During the short emergence reuse the other render buffer. Camera
     * composition consumes it only after this pass. No allocation/cache key. */
    size_t bytes=ht_native_active?HT_NATIVE_PIXELS:HT_PIXELS;
    uint8_t *previous=ht_native_active?(ht_scene==ht_native_a?ht_native_b:ht_native_a):
        (ht_scene==ht_scene_low?ht_temp:ht_scene_low);
    if(exposure<256)memcpy(previous,ht_scene,bytes);
    int cloud=ht_marsh_cloud(g->ticks),horizon=140-cy/7;
    for(int y=-margin;y<HT_H+margin;y+=4) {
        int tone=23+ht_clamp(y+40,0,240)/13+cloud/36;
        ht_rect(ht_scene,-margin,y,HT_W+2*margin,4,tone);
        if(((y+margin)&31)==0)ht_checkpoint();
    }
    /* One pale missing hillside. Its far edge moves at one tenth the camera
     * rate; the widening cut and seam resolve as the actual route approaches. */
    int approach=ht_clamp(cx-1100,0,1840),w=236+approach/8;
    int x=585-cx/10-w/2,base=horizon+5,top=base-32-approach/20;
    ht_triangle(ht_scene,x-47,base,x+29,top+7,x+w-32,top,48);
    ht_triangle(ht_scene,x-47,base,x+w-32,top,x+w+46,base,48);
    ht_triangle(ht_scene,x+33,top+15,x+w-43,top+8,x+w+21,base,60);
    ht_triangle(ht_scene,x+33,top+15,x+w+21,base,x-12,base,60);
    for(int bench=0;bench<3;++bench) {
        int yy=top+21+bench*(base-top-21)/3,left=x+28-bench*12,right=x+w-29+bench*15;
        ht_limb(ht_scene,left,yy,right,yy-4-bench,1,0,125);
        ht_limb(ht_scene,left+9,yy+3,right-6,yy-bench,0,0,42);
    }
    ht_limb(ht_scene,x+w*2/3,top+10,x+w*2/3-8,base-7,1,0,131);
    for(int n=0;n<5;++n){int tx=x+47+n*(w-100)/5;
        ht_limb(ht_scene,tx,top+5,tx-1,top-1-n%3,0,0,82);}
    /* Low islands, pools and dead stems alternate; no repeated shelter icons. */
    ht_rect(ht_scene,-margin,horizon,HT_W+2*margin,HT_H+margin-horizon,54+cloud/18);
    static const int islands[6][3]={{290,70,9},{590,114,13},{825,65,8},{1100,103,12},{1350,86,8},{1590,109,11}};
    for(unsigned n=0;n<6;++n) {
        int ix=islands[n][0]-cx*42/100,iy=horizon+9+n%2*13,iw=islands[n][1];
        if(ix+iw< -margin || ix-iw>HT_W+margin)continue;
        ht_triangle(ht_scene,ix-iw,iy,ix-9,iy-islands[n][2],ix+iw,iy,111);
        ht_limb(ht_scene,ix-iw+7,iy+3,ix+iw-11,iy+3,0,0,91);
        int stem=ix+iw/3;
        ht_limb(ht_scene,stem,iy,stem+2,iy-31,1,0,137);
        ht_limb(ht_scene,stem+1,iy-18,stem-8,iy-27,0,0,137);
        for(int r=0;r<4;++r)ht_rect(ht_scene,stem+(r&1),iy+5+r*4,2,2,111-r*12);
    }
    static const int willow[4]={825,1105,1370,1690};
    for(unsigned n=0;n<4;++n) {
        int wx=willow[n],xx=wx-cx*57/100,yy=horizon+29+n%2*5;
        if(xx< -100-margin || xx>HT_W+100+margin)continue;
        ht_marsh_willow(xx,yy,73+(int)n%2*17,156,n);
        for(int twig=0;twig<6;++twig)ht_limb(ht_scene,xx-17+twig*6,yy+3,
            xx-9+twig*6,yy+2-twig%3,0,0,126);
        ht_checkpoint();
    }
    /* Weathered posts sit on a separate bank and double in the water. */
    for(int wx=1040;wx<2240;wx+=43) {
        int xx=wx-cx*71/100,yy=horizon+48;
        if(xx< -35-margin || xx>HT_W+35+margin)continue;
        for(int reed=0;reed<4;++reed) {
            int root=xx+reed*6,tip=root-13-reed%3*3;
            ht_limb(ht_scene,root,yy,tip,yy-8-reed%3*4,0,0,139);
            ht_limb(ht_scene,tip,yy-8-reed%3*4,tip-7,yy-9-reed%3*4,0,0,124);
        }
    }
    static const int posts[5]={1050,1220,1500,1725,1920};
    for(unsigned n=0;n<5;++n) {
        int xx=posts[n]-cx*71/100,yy=horizon+44;
        if(xx< -20-margin || xx>HT_W+20+margin)continue;
        ht_limb(ht_scene,xx,yy,xx-2,yy-24,2,1,170);
        ht_limb(ht_scene,xx-2,yy-19,xx+1,yy-14,0,0,92);
        for(int r=0;r<6;++r)ht_rect(ht_scene,xx-1+(r%3==1),yy+3+r*3,3,2,142-r*13);
    }
    if(exposure<256)for(size_t at=0;at<bytes;++at) {
        ht_scene[at]=(uint8_t)((ht_scene[at]*exposure+previous[at]*(256-exposure))/256);
        if((at&8191u)==8191u)ht_checkpoint();
    }
}
static void ht_marsh_bank(const ht_game *g) {
    if(g->level!=4)return;
    int cx=g->camera/256,cy=g->camera_y/256,margin=ht_view_margin();
    int begin=ht_max(HT_MARSH_BANK_LEFT,cx-margin),end=ht_min(HT_MARSH_BANK_RIGHT,cx+HT_W+margin);
    if(begin>=end)return;
    int cloud=ht_marsh_cloud(g->ticks),mirror=cloud,water=83-mirror/5;
    for(int wx=begin;wx<end;++wx) {
        int parcel=-1;for(int i=3;i<9;++i)if(wx>=ht_terraces[4][i].left && wx<ht_terraces[4][i].right){parcel=i;break;}
        if(parcel<0)continue; /* Preserve the real channel/gap, including its lip. */
        int top=ht_surface_at(g,parcel,wx);
        int taper=ht_max(0,110-(wx-HT_MARSH_BANK_LEFT))+ht_max(0,150-(HT_MARSH_BANK_RIGHT-wx));
        int edge=top+27+(int)(ht_hash((unsigned)(wx/41))%7)+taper*taper/100;
        int dark=water;
        ht_rect(ht_scene,wx-cx,edge-cy,1,520-edge,dark);
        ht_rect(ht_scene,wx-cx,edge-6-cy,1,6,168);
        ht_rect(ht_scene,wx-cx,edge-2-cy,1,2,137);
        /* Firm roots remain above the lateral water and on the original bank. */
        if(wx%43<13)ht_rect(ht_scene,wx-cx,top+5+wx%43/4-cy,1,2,171);
        if(wx%67<21)ht_rect(ht_scene,wx-cx,top+13+wx%67/7-cy,1,1,214);
        if((wx&31)==0)ht_checkpoint();
    }
    /* Clear water reveals fixed submerged branch/stone positions. A passing
     * cloud removes that contrast while reflected sky replaces it. Geometry,
     * roots and the walkable edge stay fixed through the whole cycle. */
    for(int wx=HT_MARSH_BANK_LEFT+135;wx<HT_MARSH_BANK_RIGHT-30;wx+=173) {
        if(wx<begin-30 || wx>end+30 || (wx>1755 && wx<1840))continue;
        int parcel=3;while(parcel<8 && wx>=ht_terraces[4][parcel].right)++parcel;
        if(wx<HT_MARSH_BANK_LEFT+120 || wx>HT_MARSH_BANK_RIGHT-150)continue;
        int x=wx-cx,y=ht_surface_at(g,parcel,wx)+46-cy;
        int submerged=water+(151-water)*(256-mirror)/256;
        ht_limb(ht_scene,x-21,y+10,x+29,y-1,1,1,submerged);
        ht_limb(ht_scene,x-3,y+6,x+4,y-6,1,0,submerged);
        ht_triangle(ht_scene,x+24,y+15,x+31,y+10,x+43,y+17,submerged-8*(256-mirror)/256);
        int reflection=water-16*mirror/256;
        ht_limb(ht_scene,x-33,y+12,x+31,y+13,0,0,reflection);
        ht_limb(ht_scene,x-35,y+22,x+17,y+22,0,0,water-13);
        ht_checkpoint();
    }
}
static void ht_marsh_foreground(const ht_game *g) {
    if(g->level!=4)return;
    int cx=g->camera/256,cy=g->camera_y/256,margin=ht_view_margin();
    /* Observer-side trunks pass across the route. They have no platform or
     * wall collision; the original bank remains visible on either side. */
    static const int roots[2]={2000,3030};
    for(unsigned n=0;n<2;++n) {
        int x=roots[n]-cx*5/4,y=304-cy*5/4;
        if(x< -115-margin || x>HT_W+115+margin)continue;
        ht_marsh_willow(x,y,284-(int)n*27,240,n+13);
        ht_limb(ht_scene,x+1,y-24,x-7,y-157,1,0,180);
        ht_limb(ht_scene,x-7,y-157,x-20,y-217,0,0,193);
        ht_checkpoint();
    }
}
