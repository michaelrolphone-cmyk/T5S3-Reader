/* Distinct depth compositions, generated once per cached strip. The far plane
 * describes landscape and settlement scale; the middle plane owns recognisable
 * structures; close framing never repeats either composition. */
static void ht_distant_landscape(int offset) {
    static const int spacing[HT_LEVELS]={313,257,397,353,331,389,367,419,431,307};
    int period=spacing[ht_geometry_level];
    int first=ht_floor_div(offset-440,period),last=ht_floor_div(offset+HT_W+440,period);
    for(int j=first;j<=last;++j) {
        uint32_t h=ht_hash((uint32_t)j+7301u+ht_geometry_level*197u);
        int x=j*period-offset,base=225+(int)(h%13),height=45+(int)((h>>8)%48),top=base-height;
        int tone=65+(int)((h>>16)%15);
        switch(ht_geometry_level) {
        case 0: /* Long ridges and a dense, small canopy, not giant bare trunks. */
            ht_triangle(ht_raw,x-80,HT_H,x+100,top+12,x+period+80,HT_H,tone-17);
            for(int k=0;k<8;++k) {
                int tx=x+k*37,ty=base-21-(int)((h>>(k%4*4))%23);
                ht_disk(ht_raw,tx,ty,20+k%3*4,tone);
                ht_rect(ht_raw,tx-20,ty,41,HT_H-ty,tone);
            }
            break;
        case 1: /* Compressed skyline clusters behind isolated near towers. */
            for(int k=0;k<7;++k) {
                int bx=x+k*31,w=19+(int)((h>>(k%3*5))%10);
                int roof=base-45-(int)(ht_hash(h+(unsigned)k)%69);
                ht_rect(ht_raw,bx,roof,w,HT_H-roof,tone);
                if(k%3==0) {ht_rect(ht_raw,bx+5,roof-8,w-10,8,tone);ht_rect(ht_raw,bx+w/2,roof-17,1,17,tone);}
            }
            break;
        case 2: /* Eroded mesas; the derricks/pumpjacks belong to the middle. */
            ht_triangle(ht_raw,x-50,HT_H,x+35,top,x+35,HT_H,tone);
            ht_rect(ht_raw,x+35,top,period-125,HT_H-top,tone);
            ht_triangle(ht_raw,x+period-90,top,x+period+30,HT_H,x+period-90,HT_H,tone);
            ht_rect(ht_raw,x+52,top+15,period-160,2,tone-12);
            ht_rect(ht_raw,x+71,top+31,period-196,2,tone-9);
            break;
        case 3: /* Earth cuttings, tunnel mouth and distant overhead lines. */
            ht_triangle(ht_raw,x-80,HT_H,x+45,top+8,x+period+45,HT_H,tone-12);
            ht_rect(ht_raw,x+47,base-30,33,30,tone);
            ht_disk(ht_raw,x+63,base-30,16,tone);
            ht_rect(ht_raw,x+53,base-28,20,29,tone-30);
            for(int k=0;k<3;++k) {ht_rect(ht_raw,x+110+k*60,base-43,2,43,tone);ht_rect(ht_raw,x+107+k*60,base-43,64,1,tone);}
            break;
        case 4: /* Low island banks and drowned stumps across open water. */
            ht_triangle(ht_raw,x-60,base+12,x+100,base-12,x+period+30,base+12,tone);
            ht_rect(ht_raw,x-60,base+12,period+90,2,tone-22);
            for(int k=0;k<5;++k) {
                int tx=x+k*51;ht_limb(ht_raw,tx,base,tx+2,base-18-k%3*5,1,0,tone);
                ht_limb(ht_raw,tx+1,base-10,tx-5,base-17,0,0,tone);
            }
            break;
        case 5: /* Vast stratified escarpments, without repeating near cranes. */
            ht_rect(ht_raw,x-25,top-8,period-20,HT_H-top+8,tone);
            ht_triangle(ht_raw,x+period-45,top-8,x+period+35,base,x+period-45,base,tone);
            for(int k=0;k<4;++k) {
                ht_rect(ht_raw,x-24+k*5,top+11+k*18,period-35-k*7,2,tone-14);
                ht_limb(ht_raw,x+55+k*57,top+8+k*11,x+67+k*57,top+25+k*11,0,0,tone-18);
            }
            break;
        case 6: /* Rounded conservatory domes behind broken angular glasshouses. */
            for(int k=0;k<3;++k) {
                int bx=x+k*103,roof=base-31-k%2*14;
                ht_disk(ht_raw,bx,roof,38,tone);
                ht_rect(ht_raw,bx-38,roof,76,HT_H-roof,tone);
                ht_rect(ht_raw,bx-32,roof+3,64,2,tone-20);
                ht_rect(ht_raw,bx-2,roof-34,3,67,tone-18);
            }
            break;
        case 7: /* Reservoir valley and a fine aqueduct, not another dam wall. */
            ht_triangle(ht_raw,x-120,base+10,x+42,top-16,x+232,base+10,tone-17);
            ht_triangle(ht_raw,x+160,base+10,x+300,top+8,x+period+80,base+10,tone-8);
            ht_rect(ht_raw,x-20,base-20,period+20,3,tone);
            for(int k=0;k<6;++k) ht_rect(ht_raw,x+k*65,base-18,3,30,tone);
            ht_rect(ht_raw,x-20,base+15,period+20,2,tone-32);
            break;
        case 8: /* Broad pale snow ranges; middle distance has crags and pines. */
            ht_triangle(ht_raw,x-150,HT_H,x+85,top-54,x+period+90,HT_H,tone);
            ht_triangle(ht_raw,x+85,top-54,x+39,top+1,x+148,top+9,tone-24);
            ht_triangle(ht_raw,x+85,top-54,x+95,top+16,x+148,top+9,tone-10);
            break;
        default: /* A small settlement spread across a distant hillside. */
            ht_triangle(ht_raw,x-60,HT_H,x+120,base-31,x+period+60,HT_H,tone-18);
            for(int k=0;k<6;++k) {
                int bx=x+k*43,by=base-9-(int)(ht_hash(h+(unsigned)k)%21);
                ht_rect(ht_raw,bx,by-18,28,20,tone);
                ht_triangle(ht_raw,bx-3,by-18,bx+14,by-31,bx+31,by-18,tone);
                ht_rect(ht_raw,bx+7,by-12,4,6,25);ht_rect(ht_raw,bx+18,by-12,4,6,25);
            }
            break;
        }
        ht_checkpoint();
    }
}
static void ht_close_framing(int offset) {
    int period=757+(int)ht_geometry_level*19;
    int first=ht_floor_div(offset-300,period),last=ht_floor_div(offset+HT_W+300,period);
    for(int j=first;j<=last;++j) {
        int x=j*period-offset;
        switch(ht_geometry_level) {
        case 0: /* Large close limbs across the canopy. */
            ht_limb(ht_raw,x-110,-20,x+100,24,18,6,255);
            ht_limb(ht_raw,x+55,15,x+140,83,5,0,255);
            ht_limb(ht_raw,x+7,0,x-34,67,7,0,255);break;
        case 1: /* A fire escape seen from beneath and a loose service cable. */
            ht_rect(ht_raw,x-65,0,5,20,255);ht_rect(ht_raw,x-65,12,164,8,255);
            for(int k=0;k<7;++k) ht_rect(ht_raw,x-60+k*24,20,3,14,255);
            ht_limb(ht_raw,x-54,33,x+82,33,1,1,255);
            ht_limb(ht_raw,x-46,20,x-12,63,3,1,255);
            ht_limb(ht_raw,x+90,0,x+105,63,0,0,255);break;
        case 2: /* Thick service pipes and flanged joints. */
            ht_rect(ht_raw,x-90,0,10,32,255);ht_rect(ht_raw,x-90,22,174,10,255);ht_rect(ht_raw,x+70,22,13,52,255);
            for(int k=0;k<3;++k) {ht_rect(ht_raw,x-58+k*54,18,5,18,255);ht_disk(ht_raw,x-56+k*54,20,2,190);}
            ht_rect(ht_raw,x+66,57,21,5,255);break;
        case 3: /* A close signal gantry and hanging insulators. */
            ht_rect(ht_raw,x-110,0,6,24,255);ht_rect(ht_raw,x-110,18,212,6,255);
            for(int k=0;k<5;++k) {ht_limb(ht_raw,x-100+k*42,24,x-78+k*42,44,2,1,255);ht_limb(ht_raw,x-78+k*42,44,x-58+k*42,24,1,1,255);}
            ht_rect(ht_raw,x+84,24,2,40,255);ht_disk(ht_raw,x+85,61,4,255);break;
        case 4: /* Drooping willow fronds, unlike upright distant stumps. */
            ht_limb(ht_raw,x-90,-8,x+85,20,9,2,255);
            for(int k=0;k<7;++k) ht_limb(ht_raw,x-65+k*22,9+k,x-74+k*22,38+(k%3)*14,2,0,255);
            break;
        case 5: /* Quarry conveyor rollers and one slack lifting chain. */
            ht_limb(ht_raw,x-100,2,x+95,37,6,6,255);
            for(int k=0;k<7;++k) ht_disk(ht_raw,x-85+k*27,10+k*5,4,255);
            for(int k=0;k<7;++k) {ht_disk(ht_raw,x+63,37+k*5,3,255);ht_disk(ht_raw,x+63,37+k*5,1,0);}
            break;
        case 6: /* Broken close glazing and a climbing vine. */
            ht_limb(ht_raw,x-90,0,x+80,38,3,3,255);
            ht_limb(ht_raw,x+35,-4,x-32,55,2,2,255);
            ht_triangle(ht_raw,x-48,9,x-12,18,x-28,35,165);
            ht_limb(ht_raw,x+73,0,x+83,54,1,0,255);
            for(int k=0;k<4;++k) ht_limb(ht_raw,x+76+k*2,12+k*10,x+87+k*2,8+k*10,2,0,255);
            break;
        case 7: /* A valve housing, conduit bends and suspended cable. */
            ht_rect(ht_raw,x-85,0,13,28,255);ht_rect(ht_raw,x-85,15,139,13,255);ht_rect(ht_raw,x+41,15,13,53,255);
            ht_disk(ht_raw,x-30,30,13,255);ht_disk(ht_raw,x-30,30,8,0);
            ht_limb(ht_raw,x-40,30,x-20,30,1,1,255);
            ht_limb(ht_raw,x-30,20,x-30,40,1,1,255);
            ht_limb(ht_raw,x+79,0,x+88,68,0,0,255);break;
        case 8: /* A near rock overhang, not a third row of mountains. */
            ht_triangle(ht_raw,x-100,-5,x+90,-5,x-14,57,255);
            ht_triangle(ht_raw,x-14,57,x+30,3,x+58,38,255);
            ht_limb(ht_raw,x-61,5,x-36,26,1,1,195);break;
        default: /* Insulated return wires and the underside of a roof. */
            ht_triangle(ht_raw,x-94,0,x+18,0,x-31,34,255);
            ht_limb(ht_raw,x-10,12,x+160,48,0,0,255);
            ht_limb(ht_raw,x-8,19,x+161,55,0,0,255);
            ht_rect(ht_raw,x+97,0,6,53,255);
            for(int k=0;k<3;++k) ht_rect(ht_raw,x+92,27+k*6,16,3,255);
            break;
        }
        ht_checkpoint();
    }
}
