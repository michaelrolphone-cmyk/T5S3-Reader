#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../Apps/hollow_trail_engine.inc"
static int length(ht_joint a,ht_joint b) {int x=a.x-b.x,y=a.y-b.y;return (int)ht_isqrt((unsigned)(x*x+y*y));}
int main(void) {
    uint8_t *memory=malloc(HT_MEMORY);assert(memory);ht_bind(memory);ht.level=0;ht_spawn(true);
    ht.camera=ht.camera_y=0;ht.x=100*256;ht.y=220*256;ht.grounded=true;ht.traversal.support=5;
    for(int facing=-1;facing<=1;facing+=2)for(int speed=272;speed<=352;speed+=80) {
        ht.facing=facing;ht.vx=facing*speed;
        for(int phase=0;phase<32;++phase) {
            ht.stride=phase*256;ht_person_pose p=ht_person_pose_at(100,220,&ht);
            assert(length(p.hip,p.shoulder)>=7 && length(p.hip,p.shoulder)<=10);
            for(int i=0;i<2;++i) {
                ht_joint end=ht_joint_at(p.foot[i].x,p.foot[i].y-1);
                ht_joint knee=ht_person_joint(p.hip,end,8,7,p.knee[i]);
                assert(ht_abs(length(p.hip,knee)-8)<=2 && ht_abs(length(knee,end)-7)<=2);
            }
            if(speed==272 && phase<15) {
                ht.stride+=256;ht_person_pose next=ht_person_pose_at(100+facing,220,&ht);
                assert(p.foot[0].x==next.foot[0].x && p.foot[0].y==next.foot[0].y);
            }
        }
    }
    ht.vx=0;ht.stride=0;ht_person_pose idle=ht_person_pose_at(100,220,&ht);
    ht.ticks+=100;ht_person_pose still=ht_person_pose_at(100,220,&ht);
    assert(idle.foot[0].x==still.foot[0].x && idle.foot[1].y==still.foot[1].y);
    ht.grounded=false;ht.vy=-1200;ht_person_pose up=ht_person_pose_at(100,220,&ht);
    ht.vy=1200;ht_person_pose down=ht_person_pose_at(100,220,&ht);
    assert(up.foot[1].y<down.foot[1].y && up.hand[0].y<down.hand[0].y);
    ht.traversal.mode=HT_BOAT;ht.traversal.boat_vx=128;
    for(int i=0;i<64;++i) {
        ht.ticks=i;ht_person_pose p=ht_person_pose_at(100,220,&ht);
        assert(p.hand[0].x==105+ht_boat_stroke(&ht)/2 && p.hand[0].y==214);
    }
    /* Contact effort is half the active pose, without changing live state. */
    ht.level=0;ht_spawn(true);ht.x=189*256;ht.y=220*256;ht.vx=0;
    ht.traversal.push_hint=HT_ROLL;ht_game original=ht;
    ht_person_pose hint=ht_person_pose_at(100,220,&ht);
    assert(!memcmp(&original,&ht,sizeof(ht)));
    ht.traversal.push_hint=0;ht.traversal.mode=HT_ROLL;
    ht_person_pose full=ht_person_pose_at(100,220,&ht);
    assert(hint.shoulder.x==101+(full.shoulder.x-101)/2);
    assert(hint.hand[0].x==99+(full.hand[0].x-99)/2);
    free(memory);puts("Character: joint lengths, planted stance, jump phases, idle stability and oar contact PASS");
}
