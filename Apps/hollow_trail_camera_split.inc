/* Background camera at half width/height. Foreground remains a full-resolution
 * raster, with the original bilinear camera applied only to conservative spans.
 * The original background under foreground is retained for halos/edge blending.
 * No cache-build buffers are borrowed: their contents can survive between frames. */
static uint32_t ht_camera_low_samples,ht_camera_foreground_samples,ht_camera_low_skipped,ht_camera_fill_pixels;
static int16_t ht_camera_dest_left[HT_H],ht_camera_dest_right[HT_H];
/* Exact (coefficient * 2^28) / denominator using bounded 32-bit steps.
 * Camera coefficients are <=4096; remainder doubling stays below 2^26. */
static int ht_camera_inverse_q16(unsigned coefficient,unsigned denominator) {
    unsigned numerator=coefficient*65536u,q=numerator/denominator,r=numerator%denominator;
    for(unsigned bit=0;bit<12;++bit){r*=2;q*=2;if(r>=denominator){r-=denominator;++q;}}
    return (int)q;
}
static unsigned ht_camera_project_spans(int turn,int scale) {
    uint32_t denominator=(uint32_t)(scale*scale+turn*turn);
    /* Q16 inverse coefficients, evaluated once. Screen-sized coordinates keep
     * subsequent products in int32_t. Two-pixel guards cover coefficient
     * truncation and raster rounding in addition to the bilinear tap halo. */
    int inverse_scale=ht_camera_inverse_q16((unsigned)scale,denominator);
    int inverse_turn=ht_camera_inverse_q16((unsigned)ht_abs(turn),denominator);
    if(turn<0)inverse_turn=-inverse_turn;
    for(int y=0;y<HT_H;++y){ht_camera_dest_left[y]=HT_W;ht_camera_dest_right[y]=-1;}
    for(int y=0;y<HT_H;++y) {
        if(ht_camera_source_right[y]<ht_camera_source_left[y])continue;
        int xs[2]={ht_camera_source_left[y]-1,ht_camera_source_right[y]+1};
        int ys[2]={y-1,y+1},left=HT_W,right=-1,top=HT_H,bottom=-1;
        for(int j=0;j<2;++j)for(int i=0;i<2;++i) {
            int dx=xs[i]-HT_W/2,dy=ys[j]-HT_H/2;
            int x=HT_W/2+((dx*inverse_scale-dy*inverse_turn)>>16);
            int py=HT_H/2+((dx*inverse_turn+dy*inverse_scale)>>16);
            left=ht_min(left,x-2);right=ht_max(right,x+2);
            top=ht_min(top,py-2);bottom=ht_max(bottom,py+2);
        }
        for(int py=ht_max(HT_BORDER,top);py<=ht_min(HT_H-HT_BORDER-1,bottom);++py) {
            ht_camera_dest_left[py]=(int16_t)ht_min(ht_camera_dest_left[py],left);
            ht_camera_dest_right[py]=(int16_t)ht_max(ht_camera_dest_right[py],right);
        }
    }
    unsigned pixels=0;
    for(int y=HT_BORDER;y<HT_H-HT_BORDER;++y) {
        int left=ht_max(ht_camera_dest_left[y],ht_visible_left[y]);
        int right=ht_min(ht_camera_dest_right[y],HT_W-ht_visible_left[y]-1);
        ht_camera_dest_left[y]=(int16_t)left;ht_camera_dest_right[y]=(int16_t)right;
        if(right>=left)pixels+=(unsigned)(right-left+1);
    }
    return pixels;
}
static void ht_camera_low_into(uint8_t *dest,int turn,int scale) {
    const unsigned options=ht_render_test_mask();
    for(int y=0;y<HT_SCENE_H;++y) {
        int u=(HT_W/2)*4096-(HT_W/2)*scale+(2*y-HT_H/2)*turn;
        int v=(HT_H/2)*4096+(HT_W/2)*turn+(2*y-HT_H/2)*scale;
        int covered_left=0,covered_right=HT_W-1;
        for(int row=ht_max(0,2*y-1);row<=ht_min(HT_H-1,2*y+1);++row) {
            covered_left=ht_max(covered_left,ht_camera_dest_left[row]);
            covered_right=ht_min(covered_right,ht_camera_dest_right[row]);
        }
        int skip_left=(covered_left+2)/2,skip_right=(covered_right-1)/2;
        skip_left=ht_max(0,skip_left);skip_right=ht_min(HT_SCENE_W-1,skip_right);
        for(int x=0;x<HT_SCENE_W;++x,u+=2*scale,v-=2*turn) {
            if(x==skip_left && skip_right>=skip_left) {
                int count=skip_right-skip_left+1;memset(dest+y*HT_SCENE_W+x,0,(size_t)count);
                ht_camera_low_skipped+=(unsigned)count;x+=count-1;u+=2*scale*(count-1);v-=2*turn*(count-1);continue;
            }
            int cu=ht_clamp(u,0,(HT_SCENE_W-1)*8192),cv=ht_clamp(v,0,(HT_SCENE_H-1)*8192);
            int ix=cu>>13,iy=cv>>13,fx=(cu>>9)&15,fy=(cv>>9)&15;
            const uint8_t *a=ht_low_scene+iy*HT_SCENE_W;
            const uint8_t *b=ht_low_scene+ht_min(iy+1,HT_SCENE_H-1)*HT_SCENE_W;
            int nx=ht_min(ix+1,HT_SCENE_W-1);
            dest[y*HT_SCENE_W+x]=ht_camera_sample(a[ix],a[nx],b[ix],b[nx],fx,fy,options);
            ++ht_camera_low_samples;
        }
        if((y&7)==7)ht_checkpoint();
    }
}
/* Existing packed bilinear arithmetic, skipping only eight-output-pixel
 * groups wholly replaced on BOTH output rows. No fine-edge coverage mask. */
static void ht_upscale_visible_background(void) {
    for(int y=0;y<HT_SCENE_H;++y) {
        const uint8_t *row=ht_low_scene+y*HT_SCENE_W;
        const uint8_t *next=ht_low_scene+ht_min(y+1,HT_SCENE_H-1)*HT_SCENE_W;
        uint8_t *top=ht_scene+2*y*HT_W,*bottom=top+HT_W;
        int left=ht_max(ht_camera_dest_left[2*y],ht_camera_dest_left[2*y+1]);
        int right=ht_min(ht_camera_dest_right[2*y],ht_camera_dest_right[2*y+1]);
        for(int x=0;x<HT_SCENE_W;x+=4) {
            if(ht_expanded_active(HT_OPT_UPSCALE) && !(x&15) && x+16<HT_SCENE_W) {
                if(!(2*x>=left && 2*x+31<=right))
                    ht_expand_span(row+x,next+x,top+2*x,bottom+2*x,1);
                x+=12;continue;
            }
            if(2*x>=left && 2*x+7<=right)continue;
            uint32_t a=ht_load4(row+x),c=ht_load4(next+x);
            int nx=ht_min(x+4,HT_SCENE_W-1);
            uint32_t b=(a>>8)|((uint32_t)row[nx]<<24),d=(c>>8)|((uint32_t)next[nx]<<24);
            uint32_t h=ht_average4(a,b),v=ht_average4(a,c),diagonal=ht_average4(h,ht_average4(c,d));
            ht_store4(top+2*x,ht_zip2(a,h));ht_store4(top+2*x+4,ht_zip2(a>>16,h>>16));
            ht_store4(bottom+2*x,ht_zip2(v,diagonal));ht_store4(bottom+2*x+4,ht_zip2(v>>16,diagonal>>16));
        }
        if((y&15)==15)ht_checkpoint();
    }
}
/* Classify actual final pixels, not collision bounds: details/windows/halos
 * can interrupt a terrain fill. 256 means nonuniform. Constant-color interiors
 * are exact under bilinear interpolation, for every fractional sample weight. */
static uint16_t ht_camera_flat[HT_OCC_ROWS][HT_OCC_COLS];
static uint8_t ht_camera_flat_right[HT_OCC_ROWS][HT_OCC_COLS];
static void ht_camera_classify_fills(const uint8_t *source) {
    for(int ty=0;ty<HT_OCC_ROWS;++ty) {
        int y0=ty*16,y1=ht_min(HT_H,y0+16);
        for(int tx=0;tx<HT_OCC_COLS;++tx) {
            int x0=tx*16;unsigned ink=source[y0*HT_W+x0];uint32_t word=ink*0x01010101u;
            bool flat=true;
            for(int y=y0;y<y1 && flat;++y)for(int x=x0;x<ht_min(HT_W,x0+16);x+=4)
                if(ht_load4(source+y*HT_W+x)!=word){flat=false;break;}
            ht_camera_flat[ty][tx]=(uint16_t)(flat?ink:256);
        }
        for(int tx=HT_OCC_COLS-1;tx>=0;--tx) {
            unsigned end=(unsigned)tx+1;
            if(tx+1<HT_OCC_COLS && ht_camera_flat[ty][tx]<256 && ht_camera_flat[ty][tx]==ht_camera_flat[ty][tx+1])
                end=ht_camera_flat_right[ty][tx+1];
            ht_camera_flat_right[ty][tx]=(uint8_t)end;
        }
        ht_checkpoint();
    }
}
static void ht_camera_foreground_into(const uint8_t *source,int turn,int scale,bool fills) {
    const unsigned options=ht_render_test_mask();
    for(int y=HT_BORDER;y<HT_H-HT_BORDER;++y) {
        int x=ht_camera_dest_left[y],right=ht_camera_dest_right[y];
        int u=(HT_W/2)*4096+(x-HT_W/2)*scale+(y-HT_H/2)*turn;
        int v=(HT_H/2)*4096-(x-HT_W/2)*turn+(y-HT_H/2)*scale;
        if(!fills) {
            for(;x<=right;++x,u+=scale,v-=turn) {
                int fx=(u>>8)&15,fy=(v>>8)&15;
                const uint8_t *a=source+(v>>12)*HT_W+(u>>12),*b=a+HT_W;
                ht_scene[y*HT_W+x]=ht_camera_sample(a[0],a[1],b[0],b[1],fx,fy,options);
                ++ht_camera_foreground_samples;
            }
        }
        while(x<=right) {
            int ix=u>>12,iy=v>>12,tx=ix/16,ty=iy/16;
            unsigned ink=ht_camera_flat[ty][tx];
            int umax=(ht_min(HT_W,ht_camera_flat_right[ty][tx]*16)-1)*4096;
            int vmax=(ht_min(HT_H,ty*16+16)-1)*4096;
            if(ink<256 && u<umax && v<vmax) {
                /* Advance the ray to the first unsafe bilinear footprint.
                 * All four taps stay inside the same constant tile. */
                int count=ht_min(right-x+1,(umax-1-u)/scale+1);
                if(turn<0)count=ht_min(count,(vmax-1-v)/(-turn)+1);
                else if(turn>0)count=ht_min(count,(v-ty*16*4096)/turn+1);
                memset(ht_scene+y*HT_W+x,(int)ink,(size_t)count);
                ht_camera_fill_pixels+=(unsigned)count;x+=count;u+=scale*count;v-=turn*count;continue;
            }
            /* Classify once per crossed tile, not once per detailed pixel.
             * Keep the unchanged scalar sampler's tight loop for this run. */
            int count=ht_min(right-x+1,((tx+1)*16*4096-1-u)/scale+1);
            if(turn<0)count=ht_min(count,((ty+1)*16*4096-1-v)/(-turn)+1);
            else if(turn>0)count=ht_min(count,(v-ty*16*4096)/turn+1);
            ht_camera_foreground_samples+=(unsigned)count;
            int end=x+count;
            for(;x<end;++x,u+=scale,v-=turn) {
                int fx=(u>>8)&15,fy=(v>>8)&15;
                const uint8_t *a=source+(v>>12)*HT_W+(u>>12),*b=a+HT_W;
                ht_scene[y*HT_W+x]=ht_camera_sample(a[0],a[1],b[0],b[1],fx,fy,options);
            }
        }
        if((y&15)==15)ht_checkpoint();
    }
}
static void ht_camera_split_into(const uint8_t *source,const ht_game *game) {
    int turn,scale;ht_camera_coefficients(game,&turn,&scale);
    ht_camera_low_samples=ht_camera_low_skipped=ht_camera_foreground_samples=ht_camera_fill_pixels=0;
    unsigned covered=ht_camera_project_spans(turn,scale),visible=0;
    for(int y=HT_BORDER;y<HT_H-HT_BORDER;++y)visible+=(unsigned)(HT_W-2*ht_visible_left[y]);
    bool fills=(ht_render_test_mask()&HT_OPT_FILL_CAMERA)!=0;
    if(fills)ht_camera_classify_fills(source);
    if(covered==visible) {
        /* Generic full-coverage fallback: avoid an unused low transform and
         * second upscale when foreground drawing covers the whole view. */
        ht_camera_foreground_into(source,turn,scale,fills);return;
    }
    ht_camera_low_into(ht_scene,turn,scale);
    memcpy(ht_low_scene,ht_scene,HT_SCENE_PIXELS);
    ht_upscale_visible_background();
    ht_camera_foreground_into(source,turn,scale,fills);
}

/* Independent test: original full-resolution camera with fill acceleration,
 * without low-background sampling, write tracking or terrain occlusion. */
static void ht_camera_fills_into(const uint8_t *source,const ht_game *game) {
    int turn,scale;ht_camera_coefficients(game,&turn,&scale);
    ht_camera_fill_pixels=ht_camera_foreground_samples=0;
    ht_camera_classify_fills(source);
    for(int y=HT_BORDER;y<HT_H-HT_BORDER;++y) {
        ht_camera_dest_left[y]=ht_visible_left[y];ht_camera_dest_right[y]=HT_W-ht_visible_left[y]-1;
    }
    ht_camera_foreground_into(source,turn,scale,true);
}
