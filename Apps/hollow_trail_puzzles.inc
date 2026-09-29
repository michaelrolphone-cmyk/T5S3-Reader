/* Bounded chapter mechanisms. No timed solutions or destructive mistakes. */
enum { HT_SIGNAL, HT_GEARS, HT_CIRCUIT, HT_VESSELS, HT_POINTS,
       HT_LOCK, HT_BALANCE, HT_MIRRORS, HT_DISTRIBUTOR };
enum { HT_FEEDBACK_NONE, HT_CAM_BLOCKED, HT_CROSSOVER_SHORT, HT_RETURN_WRONG,
    HT_SHARES_WRONG, HT_SIDING_FULL, HT_SIDING_EMPTY, HT_TRAIN_ORDER,
    HT_PRESSURE, HT_OPEN_GATE, HT_EMPTY_RESERVOIR, HT_LEFT_HEAVY,
    HT_RIGHT_HEAVY, HT_BRAKE_EMPTY, HT_RAY_DARK, HT_GLASS_FOGGED,
    HT_VENT_OPEN, HT_PRIME_FAILED, HT_PRIMED, HT_SUPPLY_LOW,
    HT_ECHO_CONNECTED, HT_ECHO_ISOLATED, HT_COUPLED, HT_DECOUPLED };
typedef struct {
    uint8_t value[3], progress, opening, stage, feedback, selected;
    uint8_t cars[3][3],count[3]; /* Main road, passing loop, short dead-end spur. */
    bool solved, wrong;
} ht_puzzle_state;
typedef struct {
    const char *name;
    uint8_t kind, notes[4], length;
    const char *reveal[2];
} ht_puzzle_definition;
static const ht_puzzle_definition ht_puzzles[HT_LEVELS] = {
    { "THE MILL TRANSMISSION", HT_GEARS, { 0 }, 0,
        { "THE MAP INSIDE THE MILL PREDATES HER TRAIL.", "BOTH HANDS HAVE MARKED THE SAME CROSSING." } },
    { "THE ROOFTOP RELAY", HT_CIRCUIT, { 0 }, 0,
        { "THE RELAY WORKS EXACTLY AS BOTH NOTES SAY.", "THAT DOES NOT TELL ME WHY IT WAS STOPPED." } },
    { "THE FUEL MEASURES", HT_VESSELS, { 0 }, 0,
        { "THE RATIONS ADD UP. THE ACCOUNTS DO NOT.", "A GIFT AND A SEIZURE LEAVE THE SAME SUM." } },
    { "THE RAIL INTERLOCK", HT_POINTS, { 0 }, 0,
        { "THE DIVERGING TRACK REACHES THE MARSH.", "WHO CLOSED THE OTHERS BEFORE THE FALL?" } },
    { "THE FERRY LOCK", HT_LOCK, { 0 }, 0,
        { "THE WATER RISES. THE FAR GATE YIELDS.", "RELEASED AND RECEIVED NAME THE SAME DAWN." } },
    { "THE QUARRY BALANCE", HT_BALANCE, { 0 }, 0,
        { "THE HOIST RETURNS WITH ROOM FOR SEVEN.", "WAS SHE THEIR GUIDE OR ONE OF THE ADMISSIONS?" } },
    { "THE GARDEN HELIOSTAT", HT_MIRRORS, { 0 }, 0,
        { "LIGHT REACHES THE RECEIVER. THE LATCH LIFTS.", "A SHELTER CAN ALSO KEEP ITS PEOPLE INSIDE." } },
    { "THE DAM DISTRIBUTOR", HT_DISTRIBUTOR, { 0 }, 0,
        { "POWER REACHES BOTH THE LAMP AND THE RECORDER.", "ONE WORKING DOES NOT PROVE THE OTHER HARMLESS." } },
    { "THE RIDGE CHIMES", HT_SIGNAL, { 1, 0, 2, 1 }, 4,
        { "THE CHIMES OPEN THE RIDGE CROSSING.", "THE ANSWERING LIGHT COULD BE A RETURN PULSE." } },
    { "THE ANSWERING LIGHT", HT_SIGNAL, { 0, 0, 0, 0 }, 3,
        { "THE GLASS CABINET IS NOW WITHIN REACH.", "A / CONFIRM AT THE TOWER EXAMINES THE RELEASES." } },
};

static int ht_puzzle_controls(unsigned level) {return level==9?3:4;}
static void ht_puzzle_reset(ht_puzzle_state *s,unsigned level) {
    memset(s,0,sizeof(*s));s->selected=3;
    if(level==2) s->value[0]=8;
    if(level==3) {s->count[0]=3;s->cars[0][0]=1;s->cars[0][1]=2;s->cars[0][2]=3;}
    if(level==4) {s->value[1]=4;s->value[2]=1;}
    if(level==5) s->value[1]=6;
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
static int ht_mirror_receiver(const ht_puzzle_state *s) {
    int x=0,y=4,d=0;
    static const int dx[4]={1,0,-1,0},dy[4]={0,1,0,-1};
    for(int step=0;step<40;++step) {
        x+=dx[d];y+=dy[d];
        if(x==5 && y==5) return d==1?2:0;
        if(x==6 && y==1) return d==0?1:0;
        if(x<0 || x>6 || y<0 || y>5) break;
        int mirror=x==1 && y==4?0:x==1 && y==1?1:x==5 && y==1?2:-1;
        if(mirror>=0) {d=ht_ray_turn(d,s->value[mirror]);if(d<0) break;}
    }
    return 0;
}
static bool ht_mirror_lit(const ht_puzzle_state *s) {return ht_mirror_receiver(s)==2;}
static const char *ht_puzzle_feedback(const ht_puzzle_state *s) {
    static const char *const message[]={
        "THE APPARATUS WAITS.","THE CAMS STRIKE THE FRAME.",
        "THE SCORCHED CROSSOVER ARCS.","A RETURN REACHES THE WRONG CONTACT.",
        "THE TWO WATCH VESSELS DO NOT BALANCE.","THE SIDING BUFFER LEAVES NO ROOM.",
        "THE COUPLER FINDS NO WAGON.","THE DEPARTURE INTERLOCK HOLDS.",
        "WATER PRESSURE HOLDS THE GATE.","WATER ESCAPES THROUGH AN OPEN GATE.",
        "THE PUMP ROCKER REVERSES AT ITS STOP.","THE LONG ARM SINKS.",
        "THE SHORT ARM SINKS.","THE BRAKE HAS NO LOAD.",
        "THE BEAM DOES NOT REACH A RECEIVER.","THE LOWER GLASS IS STILL FOGGED.",
        "THE VENT OPENS. THE LOWER GLASS CLEARS.","THE COLD RETURN CANNOT HOLD PRESSURE.",
        "THE RETURN FILLS. THE TURBINE SETTLES.","A RUNNING BRANCH FALLS BELOW ITS MARK.",
        "MY STRIKE RETURNS BEFORE I FINISH.","THE INWARD WIRE IS ISOLATED.",
        "THE REAR WAGON IS COUPLED.","THE COUPLER IS RELEASED."
    };
    return message[s->feedback<sizeof(message)/sizeof(message[0])?s->feedback:0];
}
static void ht_puzzle_fail(ht_puzzle_state *s,int reason) {s->wrong=true;s->feedback=(uint8_t)reason;}
/* The fourth lever tests the physical result; touching the right setting is
 * not an omniscient auto-unlock. Lock/Ridge use it as a pump/isolator instead. */
static void ht_puzzle_test(ht_puzzle_state *s,unsigned level) {
    uint8_t *v=s->value;
    switch(ht_puzzles[level].kind) {
    case HT_GEARS:
        if(v[0]==1 && v[1]==2 && v[2]==3) s->solved=true;
        else ht_puzzle_fail(s,HT_CAM_BLOCKED);
        break;
    case HT_CIRCUIT:
        if(v[0]==1) ht_puzzle_fail(s,HT_CROSSOVER_SHORT);
        else if(ht_circuit_lane(s,0,3)==2 && ht_circuit_lane(s,1,3)==1 && ht_circuit_lane(s,2,3)==0) s->solved=true;
        else ht_puzzle_fail(s,HT_RETURN_WRONG);
        break;
    case HT_VESSELS:
        if(v[0]==4 && v[1]==4 && !v[2]) s->solved=true;
        else ht_puzzle_fail(s,HT_SHARES_WRONG);
        break;
    case HT_POINTS:
        s->selected=3;
        if(s->count[0]==3 && s->cars[0][0]==3 && s->cars[0][1]==1 && s->cars[0][2]==2) s->solved=true;
        else ht_puzzle_fail(s,HT_TRAIN_ORDER);
        break;
    case HT_BALANCE:
        if(v[0]*3>v[2]*2) ht_puzzle_fail(s,HT_LEFT_HEAVY);
        else if(v[0]*3<v[2]*2) ht_puzzle_fail(s,HT_RIGHT_HEAVY);
        else if(!v[1] || !v[0] || !v[2]) ht_puzzle_fail(s,HT_BRAKE_EMPTY);
        else s->solved=true;
        break;
    case HT_MIRRORS: {
        int receiver=ht_mirror_receiver(s);
        if(receiver==1) {s->stage=1;s->feedback=HT_VENT_OPEN;}
        else if(receiver==2 && s->stage) s->solved=true;
        else ht_puzzle_fail(s,receiver==2?HT_GLASS_FOGGED:HT_RAY_DARK);
        break;
    }
    case HT_DISTRIBUTOR:
        if(!s->stage) {
            if(v[0]>=3 && v[1]>=3 && !v[2]) {s->stage=1;s->feedback=HT_PRIMED;}
            else ht_puzzle_fail(s,HT_PRIME_FAILED);
        } else if(v[0]>=2 && v[1]>=1 && v[2]>=3) s->solved=true;
        else ht_puzzle_fail(s,HT_SUPPLY_LOW);
        break;
    default: break;
    }
}
static void ht_puzzle_operate(ht_puzzle_state *s,unsigned level,int station) {
    if(s->solved || station<0 || station>=ht_puzzle_controls(level)) return;
    const ht_puzzle_definition *p=&ht_puzzles[level];
    uint8_t *v=s->value;int next=(station+1)%3;s->wrong=false;s->feedback=HT_FEEDBACK_NONE;
    if(station==3 && p->kind!=HT_LOCK && p->kind!=HT_SIGNAL) {ht_puzzle_test(s,level);return;}
    switch(p->kind) {
    case HT_SIGNAL:
        if(level==8 && station==3) {s->stage^=1;s->progress=0;s->feedback=s->stage?HT_ECHO_ISOLATED:HT_ECHO_CONNECTED;break;}
        if(level==8 && !s->stage) {ht_puzzle_fail(s,HT_ECHO_CONNECTED);break;}
        if(station==p->notes[s->progress]) ++s->progress;
        else {s->progress=station==p->notes[0]?1:0;s->wrong=true;}
        s->solved=s->progress==p->length;break;
    case HT_GEARS:
        v[station]=(v[station]+1)%4;v[next]=(v[next]+1)%4;break;
    case HT_CIRCUIT:
        v[station]=(v[station]+1)%3;break;
    case HT_VESSELS: {
        static const int capacity[3]={8,5,3};
        int amount=v[station]<capacity[next]-v[next]?v[station]:capacity[next]-v[next];
        v[station]-=amount;v[next]+=amount;break;
    }
    case HT_POINTS: {
        static const int capacity[3]={3,2,1};
        if(s->selected==3) {
            if(!s->count[station]) ht_puzzle_fail(s,HT_SIDING_EMPTY);
            else {s->selected=(uint8_t)station;s->feedback=HT_COUPLED;}
        } else if(s->selected==station) {s->selected=3;s->feedback=HT_DECOUPLED;}
        else if(s->count[station]==capacity[station]) ht_puzzle_fail(s,HT_SIDING_FULL);
        else {
            int from=s->selected;s->cars[station][s->count[station]++]=s->cars[from][--s->count[from]];
            s->cars[from][s->count[from]]=0;s->selected=3;
        }break;
    }
    case HT_LOCK:
        if(station==3) {
            int receiver=s->stage^s->progress,donor=1-receiver;
            if(v[2]) ht_puzzle_fail(s,HT_OPEN_GATE);
            else {
                if(!v[donor]) {
                    s->progress^=1;receiver^=1;donor^=1;
                    s->feedback=HT_EMPTY_RESERVOIR;
                }
                --v[donor];++v[receiver];
            }
        } else {
            int bit=1<<station;
            if(v[2]&bit) v[2]^=(uint8_t)bit;
            else if((station==0 && v[0]!=0) || (station==2 && v[1]!=4) ||
                    (station==1 && (v[0]!=v[1] || (v[2]&5)))) ht_puzzle_fail(s,HT_PRESSURE);
            else {
                v[2]|=(uint8_t)bit;
                if(station==1) {s->stage^=1;s->progress=0;} /* Equal water lets the ferry pass between chambers. */
                if(station==2 && s->stage && !(v[2]&3)) s->solved=true;
            }
        }break;
    case HT_BALANCE:
    case HT_DISTRIBUTOR:
        if(v[station]) {--v[station];++v[next];}break;
    case HT_MIRRORS:
        v[station]=(v[station]+1)%4;break;
    }
}
