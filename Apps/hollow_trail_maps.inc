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
#define HT_BOAT_HALF 30
#define HT_BRIDGE_STEP 7
static const ht_platform ht_terraces[HT_LEVELS][HT_PLATFORMS]={
    { {-160,380,220}, {374,686,220}, {670,1080,220}, {1080,1310,220}, {1310,1460,220}, {1560,2058,220}, {2082,2270,220}, {2270,2510,220}, {2510,2780,220}, {2780,3410,220} },
    { {-160,450,220}, {330,650,80}, {630,900,-60}, {924,1200,-20}, {1120,1430,100}, {1440,1730,100}, {1750,2110,70}, {2020,2320,-100}, {2300,2780,60}, {2780,3410,200} },
    { {-160,520,220}, {544,880,220}, {880,1100,220}, {1060,1450,80}, {1485,1710,220}, {1720,1980,220}, {1980,2360,220}, {2360,2570,220}, {2570,2780,220}, {2780,3410,220} },
    { {-160,620,220}, {620,890,220}, {914,1240,220}, {1240,1500,220}, {1600,1900,220}, {1900,2110,220}, {2110,2410,220}, {2410,2620,220}, {2550,2780,90}, {2780,3410,130} },
    { {-160,350,220}, {680,1000,220}, {1000,1260,220}, {1260,1520,220}, {1520,1790,220}, {1814,2130,220}, {2130,2350,220}, {2350,2630,220}, {2630,2800,220}, {2800,3410,220} },
    { {-160,400,220}, {410,690,220}, {710,1050,180}, {980,1260,30}, {1284,1610,80}, {1580,1830,210}, {1800,2140,70}, {2164,2460,40}, {2440,2790,-80}, {2780,3410,60} },
    { {-160,550,220}, {480,820,140}, {844,1020,140}, {1044,1330,150}, {1354,1690,220}, {1714,1990,220}, {1910,2230,80}, {2254,2520,130}, {2544,2770,220}, {2780,3410,220} },
    { {-160,480,220}, {500,800,180}, {760,1010,20}, {970,1260,150}, {1620,1870,150}, {1800,2100,10}, {2050,2310,-120}, {2410,2650,-100}, {2630,2890,30}, {2780,3410,160} },
    { {-160,410,220}, {510,800,200}, {760,1070,60}, {1094,1350,10}, {1300,1690,140}, {1650,1940,60}, {1964,2250,75}, {2210,2510,-65}, {2534,2820,30}, {2780,3410,160} },
    { {-160,430,220}, {454,760,180}, {770,1030,180}, {1000,1280,40}, {1230,1550,-100}, {1574,1880,-50}, {1820,2110,80}, {2400,2640,80}, {2550,2840,-60}, {2780,3410,80} },
};
static const ht_mechanics ht_mechanics_by_level[HT_LEVELS]={
    { .ladders={{0,0,0},{0,0,0},{0,0,0}},
      .ball_x=210,.ball_floor=220,.crate_x=0,.crate_floor=0,
      .rope_anchor_x=1454,.rope_anchor_y=112,.rope_length=84,.boat_left=0,
      .boat_right=0,.boat_deck=0,.water_left=0,.water_right=0,
      .plate_x=0,.plate_y=0,.plate_kind=0,.bridge=1,
      .pit_platform=-1,.pit_x=0,.pit_floor=0,
    },
    { .ladders={{400,0,1},{640,1,2},{2060,6,7}},
      .ball_x=0,.ball_floor=0,.crate_x=1220,.crate_floor=100,
      .rope_anchor_x=0,.rope_anchor_y=0,.rope_length=0,.boat_left=0,
      .boat_right=0,.boat_deck=0,.water_left=0,.water_right=0,
      .plate_x=0,.plate_y=0,.plate_kind=0,.bridge=-1,
      .pit_platform=-1,.pit_x=0,.pit_floor=0,
    },
    { .ladders={{1080,2,3},{0,0,0},{0,0,0}},
      .ball_x=1540,.ball_floor=220,.crate_x=0,.crate_floor=0,
      .rope_anchor_x=0,.rope_anchor_y=0,.rope_length=0,.boat_left=0,
      .boat_right=0,.boat_deck=0,.water_left=0,.water_right=0,
      .plate_x=1670,.plate_y=244,.plate_kind=1,.bridge=5,
      .pit_platform=4,.pit_x=1670,.pit_floor=244,
    },
    { .ladders={{2580,7,8},{0,0,0},{0,0,0}},
      .ball_x=0,.ball_floor=0,.crate_x=0,.crate_floor=0,
      .rope_anchor_x=1494,.rope_anchor_y=112,.rope_length=84,.boat_left=0,
      .boat_right=0,.boat_deck=0,.water_left=0,.water_right=0,
      .plate_x=0,.plate_y=0,.plate_kind=0,.bridge=-1,
      .pit_platform=-1,.pit_x=0,.pit_floor=0,
    },
    { .ladders={{0,0,0},{0,0,0},{0,0,0}},
      .ball_x=0,.ball_floor=0,.crate_x=0,.crate_floor=0,
      .rope_anchor_x=0,.rope_anchor_y=0,.rope_length=0,.boat_left=398,
      .boat_right=632,.boat_deck=258,.water_left=350,.water_right=680,
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
    { .ladders={{1960,5,6},{0,0,0},{0,0,0}},
      .ball_x=0,.ball_floor=0,.crate_x=330,.crate_floor=220,
      .rope_anchor_x=0,.rope_anchor_y=0,.rope_length=0,.boat_left=0,
      .boat_right=0,.boat_deck=0,.water_left=0,.water_right=0,
      .plate_x=0,.plate_y=0,.plate_kind=0,.bridge=-1,
      .pit_platform=-1,.pit_x=0,.pit_floor=0,
    },
    { .ladders={{780,1,2},{1840,4,5},{2070,5,6}},
      .ball_x=0,.ball_floor=0,.crate_x=0,.crate_floor=0,
      .rope_anchor_x=2304,.rope_anchor_y=-228,.rope_length=84,.boat_left=1290,
      .boat_right=1590,.boat_deck=150,.water_left=1260,.water_right=1620,
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
      .rope_anchor_x=0,.rope_anchor_y=0,.rope_length=0,.boat_left=2140,
      .boat_right=2370,.boat_deck=80,.water_left=2110,.water_right=2400,
      .plate_x=710,.plate_y=180,.plate_kind=2,.bridge=2,
      .pit_platform=-1,.pit_x=0,.pit_floor=0,
    },
};
static const ht_platform *ht_level_land(unsigned level) {return ht_terraces[level];}
