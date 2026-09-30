/* Light belongs to the existing landscape. Bake it once into scenery strips,
 * before their normal focus filter; never repaint the finished screen.
 * The two openings are world-anchored at the same positions in both planes. */
static void ht_forest_light(int depth,int offset) {
    int parallax=depth?53:22;
    for(int y=0;y<HT_H;++y) {
        int shaft0=690*parallax/100+(170-y)/3;
        int shaft1=2060*parallax/100+(170-y)/3;
        int width=depth?115:90;
        int mist=ht_clamp(y-160,0,90)*(depth?12:20)/90;
        uint8_t *row=ht_raw+y*HT_W;
        for(int x=0;x<HT_W;++x) {
            int world=x+offset,d=ht_min(ht_abs(world-shaft0),ht_abs(world-shaft1));
            int glow=ht_max(0,width-d);
            glow=glow*glow/(width*width/(depth?32:20));
            int ink=row[x];
            if(ink)row[x]=(uint8_t)ht_max(1,ink-mist-glow);
        }
        if((y&15)==15)ht_checkpoint();
    }
}
