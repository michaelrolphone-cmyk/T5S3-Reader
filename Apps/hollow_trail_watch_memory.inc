/* A fixed remembered bedside and a closed door. No extra arena or gameplay
 * mutation. Ordinary shared two-bone poses carry the small act of attending. */
static ht_person_pose ht_watch_attending_pose(unsigned tick) {
    ht_person_pose p=ht_register_pose(228,159,1);
    int lift=ht_register_ease(tick,365,430);
    p.hip=ht_joint_at(228,152);p.shoulder=ht_joint_at(229,143);
    p.foot[0]=ht_joint_at(231,159);p.foot[1]=ht_joint_at(236,159);
    p.knee[0]=p.knee[1]=-1;
    p.hand[0]=ht_joint_at(224,150);
    p.hand[1]=ht_joint_at(239,148-lift*3/256);
    p.elbow[0]=p.elbow[1]=1;
    return p;
}
static void ht_watch_bedroom(unsigned tick) {
    ht_world_scale=256;ht_cutscene_fill(108);
    ht_rect(ht_scene,22,182,437,50,151);
    ht_limb(ht_scene,24,181,457,181,2,2,167);
    for(int i=0;i<7;++i)ht_limb(ht_scene,34+i*60,185,15+i*73,232,0,0,166);
    /* Sister holds the doorway. A small ordinary cup is already in reach. */
    ht_rect(ht_scene,326,34,87,170,219);ht_rect(ht_scene,333,41,71,160,160);
    ht_rect(ht_scene,325,33,5,172,238);ht_rect(ht_scene,411,33,5,173,238);
    ht_limb(ht_scene,332,201,405,201,1,1,102);
    /* Mother rests under a blanket; no invented diagnosis or distress. */
    ht_limb(ht_scene,47,135,47,210,4,3,212);
    ht_limb(ht_scene,178,155,178,211,3,3,205);
    ht_rect(ht_scene,49,148,130,26,181);ht_rect(ht_scene,50,169,128,7,215);
    ht_register_quad(51,143,84,141,94,151,52,154,56);
    ht_disk(ht_scene,76,140,7,174);ht_limb(ht_scene,79,143,94,147,4,5,174);
    ht_register_quad(91,144,121,148,173,155,169,179,139);
    ht_register_quad(91,144,169,179,74,168,63,154,139);
    ht_limb(ht_scene,97,149,135,169,0,0,166);
    ht_limb(ht_scene,119,151,158,167,0,0,165);
    /* Seat is ordinary furniture at the established foot plane. */
    ht_limb(ht_scene,187,188,219,188,3,3,225);
    ht_limb(ht_scene,189,187,188,211,2,2,225);
    ht_limb(ht_scene,217,188,220,211,2,2,225);
    ht_limb(ht_scene,187,188,186,151,2,2,218);
    ht_limb(ht_scene,230,181,253,181,2,2,202);
    ht_limb(ht_scene,233,182,233,212,2,1,207);ht_limb(ht_scene,250,181,250,212,2,1,207);
    ht_world_scale=768;
    ht_person_pose sister=ht_register_pose(277,157,-1);ht_person_draw(&sister);
    /* A narrow braid differentiates the remembered sister without new anatomy. */
    ht_limb(ht_scene,sister.shoulder.x+2,sister.shoulder.y-6,sister.shoulder.x+4,sister.shoulder.y+2,1,0,229);
    ht_person_pose seated=ht_watch_attending_pose(tick);ht_person_draw(&seated);
    /* Cup stays attached to the actual hand throughout the lift. */
    ht_rect(ht_scene,seated.hand[1].x-1,seated.hand[1].y-1,3,3,179);
    ht_limb(ht_scene,seated.hand[1].x+2,seated.hand[1].y,seated.hand[1].x+3,seated.hand[1].y+1,0,0,186);
    ht_world_scale=256;ht_checkpoint();
}
static void ht_watch_closed_door(void) {
    /* Separate remembered night, not the mother disappearing from her bed.
     * The narrator remains offscreen; no imagined figure answers this door. */
    ht_world_scale=256;ht_cutscene_fill(175);
    ht_rect(ht_scene,145,25,181,188,231);ht_rect(ht_scene,154,33,163,179,190);
    for(int board=0;board<6;++board)ht_limb(ht_scene,165+board*27,35,165+board*27,208,0,0,199);
    ht_rect(ht_scene,160,43,150,6,205);ht_rect(ht_scene,161,124,149,6,205);
    ht_limb(ht_scene,285,122,299,122,2,1,230);ht_disk(ht_scene,298,122,2,227);
    ht_limb(ht_scene,152,213,320,213,1,1,141);
    ht_rect(ht_scene,22,215,436,17,190);
    ht_checkpoint();
}
static void ht_cutscene_draw_watch_memory(const ht_cutscene_state *s) {
    ht_world_scale=256;
    if(s->tick<144 || s->tick>=912) {
        ht_cutscene_fill(64);
        unsigned phase=s->tick<144?s->tick:s->tick-912;
        ht_signal_seated_study(ht_cutscene_anchor.ticks+phase);
    } else if(s->tick<560)ht_watch_bedroom(s->tick);
    else ht_watch_closed_door();
    ht_world_scale=256;
}
