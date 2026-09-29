/* Bounded chapter mechanisms. No timed solutions or destructive mistakes. */
enum { HT_SIGNAL, HT_GEARS, HT_CIRCUIT, HT_VESSELS, HT_POINTS,
       HT_LOCK, HT_BALANCE, HT_MIRRORS, HT_DISTRIBUTOR };
typedef struct { uint8_t value[3], progress, opening; bool solved, wrong; } ht_puzzle_state;
typedef struct {
    const char *name;
    uint8_t kind, notes[4], length;
    const char *clue[2], *reveal[2];
} ht_puzzle_definition;
static const ht_puzzle_definition ht_puzzles[HT_LEVELS] = {
    { "THE MILL TRANSMISSION", HT_GEARS, { 0 }, 0,
        { "THE TOOTHED BELTS TURN TWO SHAFTS TOGETHER.", "FIT THE CAMS INTO THE WORN SLOTS IN THE FRAME." },
        { "THE MAP INSIDE THE MILL PREDATES HER TRAIL.", "BOTH HANDS HAVE MARKED THE SAME CROSSING." } },
    { "THE ROOFTOP RELAY", HT_CIRCUIT, { 0 }, 0,
        { "THE UPPER FEED MUST REACH THE LOWER CONTACT.", "RETURN THE LOWER FEED. KEEP THE HEATER CONNECTED." },
        { "THE RELAY WORKS EXACTLY AS BOTH NOTES SAY.", "THAT DOES NOT TELL ME WHY IT WAS STOPPED." } },
    { "THE FUEL MEASURES", HT_VESSELS, { 0 }, 0,
        { "ONE SUPPLY. TWO EQUAL SHIFTS. NO FUEL TO WASTE.", "EACH TAP POURS UNTIL ITS VESSEL EMPTIES OR FILLS." },
        { "THE RATIONS ADD UP. THE ACCOUNTS DO NOT.", "A GIFT AND A SEIZURE LEAVE THE SAME SUM." } },
    { "THE RAIL INTERLOCK", HT_POINTS, { 0 }, 0,
        { "THE POINTS CANNOT MOVE BENEATH A LOADED WAGON.", "THE WINCH NEEDS A HELD LOAD; FOLLOW THE TRACK." },
        { "THE DIVERGING TRACK REACHES THE MARSH.", "WHO CLOSED THE OTHERS BEFORE THE FALL?" } },
    { "THE FERRY LOCK", HT_LOCK, { 0 }, 0,
        { "A GATE WILL NOT MOVE AGAINST UNEQUAL WATER.", "SEAL THE CHAMBER BEFORE OPENING THE SLUICE." },
        { "THE WATER RISES. THE FAR GATE YIELDS.", "RELEASED AND RECEIVED NAME THE SAME DAWN." } },
    { "THE QUARRY BALANCE", HT_BALANCE, { 0 }, 0,
        { "SIX LOADS. THE LONG ARM NEEDS LESS WEIGHT.", "BALANCE THE ARMS; THE REMAINDER HOLDS THE BRAKE." },
        { "THE HOIST RETURNS WITH ROOM FOR SEVEN.", "WAS SHE THEIR GUIDE OR ONE OF THE ADMISSIONS?" } },
    { "THE GARDEN HELIOSTAT", HT_MIRRORS, { 0 }, 0,
        { "FOLLOW THE SUN PIPE THROUGH THE RUINED GLASS.", "TURN THE MIRRORS TO WAKE THE LOW RECEIVER." },
        { "LIGHT REACHES THE RECEIVER. THE LATCH LIFTS.", "A SHELTER CAN ALSO KEEP ITS PEOPLE INSIDE." } },
    { "THE DAM DISTRIBUTOR", HT_DISTRIBUTOR, { 0 }, 0,
        { "THE RIDGE NEEDS POWER. THE GARDENS NEED HEAT.", "SIX UNITS MUST KEEP ALL THREE NEEDLES ABOVE RED." },
        { "POWER REACHES BOTH THE LAMP AND THE RECORDER.", "ONE WORKING DOES NOT PROVE THE OTHER HARMLESS." } },
    { "THE RIDGE CHIMES", HT_SIGNAL, { 1, 0, 2, 1 }, 4,
        { "THE SHELTER POSTS TRACE A ROUTE THROUGH THE PASS.", "READ THE NOTCHES FROM THE SCARF TOWARD THE RIDGE." },
        { "THE CHIMES OPEN THE RIDGE CROSSING.", "THE ANSWERING LIGHT COULD BE A RETURN PULSE." } },
    { "THE ANSWERING LIGHT", HT_SIGNAL, { 0, 0, 0, 0 }, 3,
        { "THREE SHORT FLASHES FROM THE FIRST LAMP.", "THE SIGNAL SHE TAUGHT ME BEFORE SHE LEFT." },
        { "THE GLASS CABINET IS NOW WITHIN REACH.", "A / CONFIRM AT THE TOWER EXAMINES THE RELEASES." } },
};

static void ht_puzzle_reset(ht_puzzle_state *s,unsigned level) {
    memset(s,0,sizeof(*s));
    if(level==2) s->value[0]=8;
    if(level==4) s->value[0]=1; /* Entry gate open, chamber at the marsh. */
    if(level==7) s->value[0]=6;
}
/* Each switch is bypass / cross upper pair / cross lower pair. The complete
 * route, including the heater return, matters; a single lit endpoint cannot win. */
static int ht_circuit_lane(const ht_puzzle_state *s,int lane,int count) {
    for(int i=0;i<count;++i) {
        int pair=(int)s->value[i]-1;
        if(pair>=0 && (lane==pair || lane==pair+1)) lane=2*pair+1-lane;
    }
    return lane;
}
/* Fixed optical bench: inlet east, mirrors (1,4),(1,1),(5,1), receiver (5,5).
 * 0 shutter, 1 slash, 2 backslash, 3 clear. The renderer follows this same ray. */
static int ht_ray_turn(int direction,int mirror) {
    if(!mirror) return -1;
    if(mirror==3) return direction;
    return mirror==1?(3-direction): (direction^1);
}
static bool ht_mirror_lit(const ht_puzzle_state *s) {
    int x=0,y=4,d=0;
    static const int dx[4]={1,0,-1,0},dy[4]={0,1,0,-1};
    for(int step=0;step<40;++step) {
        x+=dx[d];y+=dy[d];
        if(x==5 && y==5) return d==1;
        if(x<0 || x>6 || y<0 || y>5) break;
        int mirror=x==1 && y==4?0:x==1 && y==1?1:x==5 && y==1?2:-1;
        if(mirror>=0) {d=ht_ray_turn(d,s->value[mirror]);if(d<0) break;}
    }
    return false;
}
static void ht_puzzle_operate(ht_puzzle_state *s,unsigned level,int station) {
    if(s->solved || station<0 || station>2) return;
    const ht_puzzle_definition *p=&ht_puzzles[level];
    uint8_t *v=s->value;int next=(station+1)%3;s->wrong=false;
    switch(p->kind) {
    case HT_SIGNAL:
        if(station==p->notes[s->progress]) ++s->progress;
        else {s->progress=station==p->notes[0]?1:0;s->wrong=true;}
        s->solved=s->progress==p->length;break;
    case HT_GEARS:
        v[station]=(v[station]+1)%4;v[next]=(v[next]+1)%4;
        s->solved=v[0]==1 && v[1]==2 && v[2]==3;break;
    case HT_CIRCUIT:
        v[station]=(v[station]+1)%3;
        s->solved=ht_circuit_lane(s,0,3)==2 && ht_circuit_lane(s,1,3)==1 && ht_circuit_lane(s,2,3)==0;break;
    case HT_VESSELS: {
        static const int capacity[3]={8,5,3};
        int amount=v[station]<capacity[next]-v[next]?v[station]:capacity[next]-v[next];
        v[station]-=amount;v[next]+=amount;s->wrong=amount==0;
        s->solved=v[0]==4 && v[1]==4 && !v[2];break;
    }
    case HT_POINTS:
        /* Brake, wagon, points. Releasing the brake at the end dispatches.
         * Failed actions leave the mechanism intact, with visible obstruction. */
        if(station==0) v[0]^=1;
        else if(station==1 && v[0]) v[1]^=1;
        else if(station==2 && v[0] && v[1]) v[2]^=1;
        else s->wrong=true;
        s->solved=!v[0] && v[1] && v[2];break;
    case HT_LOCK:
        if(station==0 && !v[2]) v[0]^=1;
        else if(station==1 && v[2]==2) v[1]^=1;
        else if(station==2 && !v[0] && !v[1]) v[2]=(v[2]+1)%3;
        else s->wrong=true;
        s->solved=!v[0] && v[1] && v[2]==2;break;
    case HT_BALANCE:
        v[station]=(v[station]+1)%4;
        s->solved=v[0]+v[1]+v[2]==6 && v[0]*3==v[2]*2 && v[1]>0;break;
    case HT_MIRRORS:
        v[station]=(v[station]+1)%4;s->solved=ht_mirror_lit(s);break;
    case HT_DISTRIBUTOR:
        if(v[station]) {--v[station];++v[next];} else s->wrong=true;
        s->solved=v[0]>=2 && v[1]>=1 && v[2]>=3;break;
    }
}
