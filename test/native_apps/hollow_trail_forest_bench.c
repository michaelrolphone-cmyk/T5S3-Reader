/* CPU-time comparison only: compile the same source against each revision's
 * Apps include path. Includes moving-camera cache work; excludes scanout. */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "hollow_trail_engine.inc"
int main(void) {
    uint8_t *memory=malloc(HT_MEMORY);if(!memory)return 1;
    for(int moving=0;moving<2;++moving) {
        ht_bind(memory);ht.level=0;ht_spawn(true);
        ht.sway_phase=512;ht.rotation_phase=256u<<8;ht.camera_mood=256;
        ht_render_scene();clock_t begin=clock();
        for(int n=0;n<800;++n) {
            ht.camera=(moving?n*3:0)*256;ht.x=(ht.camera/256+190)*256;
            ht.ticks++;ht_render_scene();
        }
        printf("forest %s: %.3f ms/host render\n",moving?"moving":"settled",
            1000.0*(clock()-begin)/CLOCKS_PER_SEC/800);
    }
    free(memory);return 0;
}
