/* Individually authored routes and prop placements. Lower/upper platform
 * indices describe ladder connections, rather than implicit global x slots. */
typedef struct {int x,lower,upper;} ht_ladder_def;
typedef struct {
    ht_ladder_def ladders[3];
    int ball_x,ball_floor,crate_x,crate_floor;
    int rope_anchor_x,rope_anchor_y,rope_length;
    int boat_left,boat_right,boat_deck,water_left,water_right;
    int plate_x,plate_y,plate_kind,bridge;
    int pit_platform,pit_x,pit_floor;
} ht_mechanics;
#define HT_BALL_RADIUS 16
#define HT_CRATE_HALF 12
#define HT_CRATE_HEIGHT 26
#define HT_ROPE_NODES 7
static const ht_platform ht_terraces[HT_LEVELS][HT_PLATFORMS]={
    { {-160,380,220}, {390,670,220}, {730,1000,185}, {930,1200,70}, {1150,1460,-60}, {1600,1820,-50}, {1800,2100,40}, {2140,2390,80}, {2420,2770,140}, {2780,3410,200} },
    { {-160,450,220}, {330,650,80}, {630,900,-60}, {950,1200,-20}, {1120,1430,100}, {1440,1730,100}, {1780,2110,40}, {2020,2320,-100}, {2300,2780,60}, {2780,3410,200} },
    { {-160,520,220}, {570,820,200}, {850,1100,150}, {1060,1450,30}, {1475,1710,80}, {1720,1980,80}, {2030,2260,130}, {2210,2450,15}, {2490,2760,100}, {2780,3410,200} },
    { {-160,580,220}, {620,890,180}, {840,1100,40}, {960,1240,-90}, {1380,1650,-70}, {1600,1850,50}, {1840,2110,160}, {2150,2380,120}, {2340,2780,-10}, {2780,3410,130} },
    { {-160,350,220}, {680,940,220}, {890,1200,80}, {1250,1480,40}, {1450,1790,170}, {1830,2080,120}, {2000,2310,-10}, {2340,2580,50}, {2500,2800,180}, {2780,3410,200} },
    { {-160,400,220}, {410,690,220}, {720,1050,170}, {980,1260,30}, {1320,1610,80}, {1580,1830,210}, {1800,2140,70}, {2200,2460,40}, {2440,2790,-80}, {2780,3410,60} },
    { {-160,550,220}, {480,820,105}, {760,1020,-40}, {1080,1330,10}, {1290,1690,140}, {1730,1990,80}, {1910,2230,-50}, {2240,2520,40}, {2480,2830,170}, {2780,3410,200} },
    { {-160,480,220}, {520,800,160}, {760,1010,20}, {970,1260,150}, {1620,1870,150}, {1800,2100,10}, {2050,2310,-120}, {2450,2650,-100}, {2630,2890,30}, {2780,3410,160} },
    { {-160,410,220}, {550,800,200}, {760,1070,60}, {1130,1350,10}, {1300,1690,140}, {1650,1940,25}, {2000,2250,75}, {2210,2510,-65}, {2550,2820,30}, {2780,3410,160} },
    { {-160,430,220}, {470,760,180}, {770,1030,180}, {1000,1280,40}, {1230,1550,-100}, {1610,1880,-50}, {1820,2110,80}, {2400,2640,80}, {2550,2840,-60}, {2780,3410,80} },
};
static const ht_mechanics ht_mechanics_by_level[HT_LEVELS]={
    { .ladders={{1170,3,4},{0,0,0},{0,0,0}},
      .ball_x=210,.ball_floor=220,.crate_x=850,.crate_floor=185,
      .rope_anchor_x=1454,.rope_anchor_y=-168,.rope_length=84,.boat_left=0,
      .boat_right=0,.boat_deck=0,.water_left=0,.water_right=0,
      .plate_x=300,.plate_y=220,.plate_kind=1,.bridge=1,
      .pit_platform=-1,.pit_x=0,.pit_floor=0,
    },
    { .ladders={{400,0,1},{640,1,2},{2060,6,7}},
      .ball_x=0,.ball_floor=0,.crate_x=1220,.crate_floor=100,
      .rope_anchor_x=0,.rope_anchor_y=0,.rope_length=0,.boat_left=0,
      .boat_right=0,.boat_deck=0,.water_left=0,.water_right=0,
      .plate_x=1380,.plate_y=100,.plate_kind=2,.bridge=5,
      .pit_platform=-1,.pit_x=0,.pit_floor=0,
    },
    { .ladders={{1080,2,3},{0,0,0},{0,0,0}},
      .ball_x=1540,.ball_floor=80,.crate_x=2090,.crate_floor=130,
      .rope_anchor_x=0,.rope_anchor_y=0,.rope_length=0,.boat_left=0,
      .boat_right=0,.boat_deck=0,.water_left=0,.water_right=0,
      .plate_x=1670,.plate_y=104,.plate_kind=1,.bridge=5,
      .pit_platform=4,.pit_x=1670,.pit_floor=104,
    },
    { .ladders={{870,1,2},{1040,2,3},{2360,7,8}},
      .ball_x=0,.ball_floor=0,.crate_x=0,.crate_floor=0,
      .rope_anchor_x=1234,.rope_anchor_y=-198,.rope_length=84,.boat_left=0,
      .boat_right=0,.boat_deck=0,.water_left=0,.water_right=0,
      .plate_x=0,.plate_y=0,.plate_kind=0,.bridge=-1,
      .pit_platform=-1,.pit_x=0,.pit_floor=0,
    },
    { .ladders={{920,1,2},{2050,5,6},{0,0,0}},
      .ball_x=0,.ball_floor=0,.crate_x=0,.crate_floor=0,
      .rope_anchor_x=0,.rope_anchor_y=0,.rope_length=0,.boat_left=340,
      .boat_right=700,.boat_deck=220,.water_left=350,.water_right=680,
      .plate_x=0,.plate_y=0,.plate_kind=0,.bridge=-1,
      .pit_platform=-1,.pit_x=0,.pit_floor=0,
    },
    { .ladders={{1020,2,3},{1810,5,6},{2450,7,8}},
      .ball_x=240,.ball_floor=220,.crate_x=0,.crate_floor=0,
      .rope_anchor_x=0,.rope_anchor_y=0,.rope_length=0,.boat_left=0,
      .boat_right=0,.boat_deck=0,.water_left=0,.water_right=0,
      .plate_x=340,.plate_y=244,.plate_kind=1,.bridge=1,
      .pit_platform=0,.pit_x=340,.pit_floor=244,
    },
    { .ladders={{800,1,2},{1960,5,6},{0,0,0}},
      .ball_x=0,.ball_floor=0,.crate_x=330,.crate_floor=220,
      .rope_anchor_x=0,.rope_anchor_y=0,.rope_length=0,.boat_left=0,
      .boat_right=0,.boat_deck=0,.water_left=0,.water_right=0,
      .plate_x=0,.plate_y=0,.plate_kind=0,.bridge=-1,
      .pit_platform=-1,.pit_x=0,.pit_floor=0,
    },
    { .ladders={{780,1,2},{1840,4,5},{2070,5,6}},
      .ball_x=0,.ball_floor=0,.crate_x=0,.crate_floor=0,
      .rope_anchor_x=2304,.rope_anchor_y=-228,.rope_length=84,.boat_left=1250,
      .boat_right=1640,.boat_deck=150,.water_left=1260,.water_right=1620,
      .plate_x=0,.plate_y=0,.plate_kind=0,.bridge=-1,
      .pit_platform=-1,.pit_x=0,.pit_floor=0,
    },
    { .ladders={{790,1,2},{2230,6,7},{0,0,0}},
      .ball_x=0,.ball_floor=0,.crate_x=1440,.crate_floor=140,
      .rope_anchor_x=404,.rope_anchor_y=112,.rope_length=84,.boat_left=0,
      .boat_right=0,.boat_deck=0,.water_left=0,.water_right=0,
      .plate_x=0,.plate_y=0,.plate_kind=0,.bridge=-1,
      .pit_platform=-1,.pit_x=0,.pit_floor=0,
    },
    { .ladders={{1020,2,3},{1260,3,4},{2580,7,8}},
      .ball_x=0,.ball_floor=0,.crate_x=530,.crate_floor=180,
      .rope_anchor_x=0,.rope_anchor_y=0,.rope_length=0,.boat_left=2100,
      .boat_right=2420,.boat_deck=80,.water_left=2110,.water_right=2400,
      .plate_x=710,.plate_y=180,.plate_kind=2,.bridge=2,
      .pit_platform=-1,.pit_x=0,.pit_floor=0,
    },
};
static const ht_platform *ht_level_land(unsigned level) {return ht_terraces[level];}
