#pragma once

/*=============================================================
 * game.h  –  Master header for the AoE2-inspired RTS game
 *             All shared types, constants, and function decls.
 *=============================================================*/

typedef struct { float x, y; } Vec2;
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

/* ─── World constants ──────────────────────────────────────── */
#define SCREEN_W        1280
#define SCREEN_H        720
#define MAP_W           64
#define MAP_H           64
#define TILE_SIZE       32          /* pixels per tile at zoom=1 */
#define MAX_UNITS       256
#define MAX_BUILDINGS   128
#define MAX_PROJECTILES 128
#define ASTAR_PATH_CAP  300         /* max A* path nodes (avoids conflict with Windows MAX_PATH) */
#define NUM_PLAYERS     8
#define POP_CAP_MAX     200
#define HOUSE_VARIANT_COUNT 16
#define HERO_POSSESSION_DURATION 22.0f
#define HERO_POSSESSION_COOLDOWN 45.0f
#define HERO_POSSESSION_TRANSITION_TIME 0.65f

/* ─── Priority queue (min-heap) used by A* ─────────────────── */
#define PQ_CAP 8192
typedef struct { int cell; float f; } PQNode;
typedef struct { PQNode data[PQ_CAP]; int size; } PriorityQueue;

static inline void pq_init(PriorityQueue *q) { q->size = 0; }
static inline bool pq_empty(const PriorityQueue *q) { return q->size == 0; }
static inline void pq_push(PriorityQueue *q, int cell, float f) {
    if (q->size >= PQ_CAP) return;
    int i = q->size++;
    q->data[i] = (PQNode){cell, f};
    while (i > 0) {
        int p = (i-1)/2;
        if (q->data[p].f <= q->data[i].f) break;
        PQNode t = q->data[p]; q->data[p] = q->data[i]; q->data[i] = t;
        i = p;
    }
}
static inline PQNode pq_pop(PriorityQueue *q) {
    PQNode r = q->data[0];
    q->data[0] = q->data[--q->size];
    int i = 0;
    for (;;) {
        int l=2*i+1, r2=2*i+2, s=i;
        if (l < q->size && q->data[l].f < q->data[s].f) s=l;
        if (r2 < q->size && q->data[r2].f < q->data[s].f) s=r2;
        if (s==i) break;
        PQNode t = q->data[s]; q->data[s] = q->data[i]; q->data[i] = t;
        i = s;
    }
    return r;
}

/* ─── Math helpers ──────────────────────────────────────────── */
static inline float clampf(float v,float lo,float hi){return v<lo?lo:v>hi?hi:v;}
static inline int   clampi(int v,int lo,int hi){return v<lo?lo:v>hi?hi:v;}
static inline float lerpf(float a,float b,float t){return a+(b-a)*t;}
static inline float dist2f(float ax,float ay,float bx,float by){
    float dx=bx-ax,dy=by-ay; return sqrtf(dx*dx+dy*dy);
}

/* ─── Isometric Math ──────────────────────────────────────── */
/* 
 * Isometric projection where:
 * Cartesian X goes down-right
 * Cartesian Y goes down-left
 * Scale is exactly 2:1 (width:height)
 */
static inline Vec2 world_to_iso(float wx, float wy) {
    return (Vec2){
        (wx - wy),
        (wx + wy) * 0.5f
    };
}
static inline Vec2 iso_to_world(float ix, float iy) {
    return (Vec2){
        iy + ix * 0.5f,
        iy - ix * 0.5f
    };
}

/* ─── RNG ────────────────────────────────────────────────────── */
extern uint32_t _rng;
static inline uint32_t rng_next(void){
    _rng ^= _rng<<13; _rng ^= _rng>>17; _rng ^= _rng<<5; return _rng;
}
static inline int rng_range(int lo,int hi){return lo+(int)(rng_next()%(unsigned)(hi-lo+1));}
static inline float rng_frac(void){return (rng_next()&0xFFFF)/65535.0f;}

/* ─── Tile / Map ─────────────────────────────────────────────── */
typedef enum {
    TILE_GRASS = 0,
    TILE_WATER,
    TILE_FOREST,
    TILE_GOLD,
    TILE_STONE,
    TILE_BERRIES,
    TILE_FARM,
    TILE_DESERT,        /* sandy/arid terrain (passable, no resources) */
    TILE_ROAD,          /* packed earth/cobble (passable, slightly faster movement) */
} TileType;

typedef enum {
    FOG_HIDDEN   = 0,
    FOG_EXPLORED = 1,
    FOG_VISIBLE  = 2,
} FogState;

typedef struct {
    TileType type;
    int      resource_amt;
    int      building_id;       /* -1 = none */
    FogState fog[NUM_PLAYERS];
    uint8_t  variant;           /* tile color variation 0-3 */
} Tile;

/* ─── Resources ─────────────────────────────────────────────── */
typedef enum { RES_FOOD=0, RES_WOOD, RES_GOLD, RES_STONE, RES_COUNT } ResType;

typedef enum {
    TECH_CROP_ROTATION=0,
    TECH_FERTILIZER,
    TECH_HAND_MILL,
    TECH_GRANARY_BASKETS,
    TECH_IRRIGATION,
    TECH_REAPING,
    TECH_DOUBLE_BIT_AXE,
    TECH_LOG_STRAPS,
    TECH_BOW_SAW,
    TECH_TIMBER_ROUTE,
    TECH_TWO_MAN_SAW,
    TECH_HARDWOOD_CARTS,
    TECH_LOOM,
    TECH_WHEELBARROW,
    TECH_HAND_CART,
    TECH_IRON_WEAPONRY,
    TECH_SQUIRES,
    TECH_CHAIN_MAIL,
    TECH_HARDENED_BLADES,
    TECH_IMPERIAL_INFANTRY,
    TECH_VETERAN_LEGION,
    TECH_COMPOSITE_BOWS,
    TECH_THUMB_RING,
    TECH_REINFORCED_STRINGS,
    TECH_EAGLE_EYE,
    TECH_IMPERIAL_ARCHERY,
    TECH_FIELD_CRAFT,
    TECH_MOUNTED_ARMOR,
    TECH_HUSBANDRY,
    TECH_CAVALRY_DRILL,
    TECH_BLOODLINES,
    TECH_IMPERIAL_CAVALRY,
    TECH_STEEL_SPURS,
    TECH_SANCTITY,
    TECH_DEVOTION,
    TECH_FERVOR,
    TECH_ILLUMINATION,
    TECH_BLOCK_PRINTING,
    TECH_HOLY_VISION,
    TECH_REINFORCED_RAM,
    TECH_SIEGE_ENGINEERS,
    TECH_ONAGER,
    TECH_DRILL_CREW,
    TECH_HEAVY_SCORPION,
    TECH_TORSION_ENGINES,
    TECH_SCALE_ARMOR,       /* Blacksmith: +1 armor all military */
    TECH_BLAST_FURNACE,
    TECH_PLATE_ARMOR,
    TECH_FORGED_ARROWS,     /* Blacksmith: +1 atk archers */
    TECH_BODKIN_ARROW,
    TECH_BRACER,
    TECH_MASONRY,
    TECH_ARCHITECTURE,
    TECH_FORTIFIED_WALL,
    TECH_GUARD_TOWER,
    TECH_KEEP,
    TECH_MURDER_HOLES,
    TECH_TREADMILL_CRANE,
    TECH_CHEMISTRY,
    TECH_HOARDINGS,
    TECH_HEATED_SHOT,
    TECH_CANNON_EMPLACEMENTS,
    TECH_MISSILE_GUIDANCE,
    TECH_COUNT,
    TECH_NONE = -1
} TechType;

typedef struct {
    int   amount[RES_COUNT];
    int   population;
    int   pop_cap;
    int   age;              /* 0=Dark 1=Feudal 2=Castle 3=Imperial */
    bool  advancing;
    float advance_timer;
    bool  tech_unlocked[TECH_COUNT];
} PlayerRes;

/* ─── Path cell ──────────────────────────────────────────────── */
typedef struct { int x, y; } PathCell;

typedef enum {
    FORMATION_BOX = 0,
    FORMATION_LINE,
    FORMATION_COLUMN,
    FORMATION_WEDGE,
    FORMATION_COUNT
} FormationType;

/* ─── Unit types & stats ─────────────────────────────────────── */
typedef enum {
    UNIT_VILLAGER=0, UNIT_SCOUT,
    UNIT_MILITIA, UNIT_MAN_AT_ARMS, UNIT_SPEARMAN,
    UNIT_ARCHER,  UNIT_SKIRMISHER, UNIT_CAVALRY_ARCHER,
    UNIT_KNIGHT,  UNIT_MONK,
    UNIT_BATTERING_RAM, UNIT_MANGONEL, UNIT_SCORPION, UNIT_BOMBARD_CANNON,
    UNIT_COUNT
} UnitType;

typedef enum {
    US_IDLE=0, US_MOVING, US_GATHERING, US_RETURNING,
    US_BUILDING, US_ATTACKING, US_DYING, US_DEAD
} UnitState;

typedef struct {
    bool      active;
    int       id;
    int       player;
    UnitType  type;
    UnitState state;

    float     wx, wy;           /* world position (px, center) */
    float     twx, twy;         /* sub-target waypoint (px) */

    PathCell  path[ASTAR_PATH_CAP];
    int       path_len;
    int       path_idx;

    int       hp, max_hp;
    int       attack_dmg;
    int       armor;
    float     attack_range;     /* tiles */
    float     vision_range;     /* tiles */
    float     move_speed;       /* px/s */
    float     attack_cd;
    float     attack_timer;

    /* Villager work */
    ResType   carry_type;
    int       carry_amt;
    int       carry_cap;
    int       gather_tx, gather_ty;
    int       build_id;         /* -1 = none */

    /* Combat */
    int       target_unit;      /* -1 = none */
    int       target_bld;       /* -1 = none */
    bool      stance_manual;    /* false = auto, true = manual */
    bool      attack_move;      /* true = attack-move order (engage enemies en route) */

    bool      selected;
    float     anim_timer;
    float     facing;           /* radians */
    float     death_timer;
} Unit;

/* ─── Building types ─────────────────────────────────────────── */
typedef enum {
    BLD_TOWN_CENTER=0, BLD_HOUSE,
    BLD_BARRACKS, BLD_ARCHERY_RANGE, BLD_STABLE,
    BLD_BLACKSMITH, BLD_MARKET,
    BLD_MILL, BLD_LUMBER_CAMP, BLD_MINING_CAMP,
    BLD_FARM, BLD_WATCH_TOWER, BLD_MONASTERY,
    BLD_SIEGE_WORKSHOP, BLD_UNIVERSITY, BLD_WALL, BLD_GATE, BLD_CASTLE, BLD_COUNT
} BldType;

#define BQUEUE_CAP 5
typedef struct {
    bool     active;
    int      id;
    int      player;
    BldType  type;
    int      tx, ty;            /* top-left tile */
    int      tw, th;            /* size in tiles */
    int      hp, max_hp;
    float    construction;      /* 0.0 foundation … 1.0 complete */
    bool     complete;
    UnitType queue[BQUEUE_CAP];
    int      queue_len;
    float    train_timer;
    int      rally_tx, rally_ty;
    bool     selected;
    int      resource_amt;      /* For farms: remaining food */
    TechType active_tech;
    float    tech_timer;
    int      attack_dmg;
    float    attack_range;
    float    attack_cd;
    float    attack_timer;
    uint8_t  variant;           /* visual variant for buildings that support it */
} Building;

/* ─── Build mode ghost ───────────────────────────────────────── */
typedef struct {
    bool    active;
    BldType type;
    int     ghost_tx, ghost_ty;
    bool    valid;
    bool    dragging;
    int     drag_start_tx, drag_start_ty;
} BuildMode;

typedef enum {
    PROJ_ARROW = 0,
    PROJ_BOLT,
    PROJ_STONE
} ProjectileType;

typedef struct {
    bool           active;
    int            owner_player;
    ProjectileType type;
    int            target_unit;
    int            target_bld;
    int            damage;
    float          sx, sy;
    float          ex, ey;
    float          elapsed;
    float          duration;
    float          arc_height;
} Projectile;

/* ─── Hero possession mode ─────────────────────────────────── */
typedef enum {
    HERO_POSSESSION_OFF = 0,
    HERO_POSSESSION_ENTERING,
    HERO_POSSESSION_ACTIVE,
    HERO_POSSESSION_EXITING
} HeroPossessionPhase;

typedef struct {
    HeroPossessionPhase phase;
    int       unit_id;             /* possessed unit, -1 = none */
    float     duration;
    float     timer;
    float     cooldown_timer;
    float     transition_timer;
    float     transition_time;

    /* First-person control/view state */
    float     yaw;
    float     pitch;
    float     stamina;
    float     attack_timer;
    float     dodge_timer;
    float     dodge_cooldown;
    float     block_timer;
    float     shake;
    float     blur;
    float     impact_timer;
} HeroPossession;

/* ─── Match mode ─────────────────────────────────────────────── */
typedef enum {
    GAME_MODE_STANDARD = 0,
    GAME_MODE_SANDBOX,
    GAME_MODE_CAMPAIGN
} GameMode;

/* ─── Game phase ─────────────────────────────────────────────── */
typedef enum {
    PHASE_MENU=0, PHASE_PLAYING,
    PHASE_PAUSED, PHASE_VICTORY, PHASE_DEFEAT
} GamePhase;

/* ─── Master game state ──────────────────────────────────────── */
typedef struct {
    GameMode   mode;
    GamePhase  phase;
    float      game_time;
    float      game_speed;       /* simulation speed multiplier (1.0 = normal) */

    Tile       map[MAP_H][MAP_W];
    Unit       units[MAX_UNITS];
    Building   buildings[MAX_BUILDINGS];
    int        unit_count;
    int        bld_count;

    PlayerRes  res[NUM_PLAYERS];


    BuildMode  build_mode;
    Projectile projectiles[MAX_PROJECTILES];
    HeroPossession hero;

    /* AI */
    int        ai_phase;
    float      ai_timer;
    float      ai_attack_cd;
    int        ai_attack_count;
    float      fog_update_timer;  /* throttle fog recalculations */
    int        num_players;     /* Active players in this match (2-4) */

    /* Alert banner */
    char       alert[64];
    float      alert_timer;

    /* Campaign */
    uint32_t   campaign_seed;
    int        campaign_mission;
    char       campaign_title[96];
    char       campaign_objective[224];
    char       campaign_hint[224];
    char       campaign_story[224];
    char       campaign_result[224];
    char       campaign_briefing[320];
    char       campaign_status[160];
    bool       campaign_briefing_open;
    int        campaign_event_stage;
    float      campaign_event_timer;

    /* OSM map generator metadata */
    bool       osm_map_available;
    char       osm_location_name[128];
    int        osm_tile_z;             /* zoom level */
    int        osm_tile_x0, osm_tile_y0; /* top-left tile coords */
    int        osm_tile_cols, osm_tile_rows; /* grid size */
    double     osm_bbox_west, osm_bbox_east, osm_bbox_north, osm_bbox_south;
} GameState;

/* ─── Resource cost table helper ────────────────────────────── */
typedef struct { int food,wood,gold,stone; } Cost;

/* ──────────────────────────────────────────────────────────────
 * Function declarations — implemented in respective .c files
 * ────────────────────────────────────────────────────────────── */

/* map.c */
/* p1_x/p1_y and p2_x/p2_y receive the TC tile centres chosen for each player */
void map_init(GameState *gs, int *start_x, int *start_y, int num_players);
bool map_in_bounds(int x,int y);
bool map_is_passable(GameState *gs,int x,int y);
bool map_is_buildable(GameState *gs,int x,int y,int w,int h);
void map_place_building(GameState *gs,int tx,int ty,int w,int h,int bid);
void map_clear_building(GameState *gs,int tx,int ty,int w,int h);
void map_update_fog(GameState *gs);
int  map_find_resource(GameState *gs,int player,ResType res,int near_tx,int near_ty,int *out_x,int *out_y);
int  map_find_dropoff(GameState *gs,int player,ResType res,float wx,float wy,int *out_tx,int *out_ty);
int  map_find_passable_near(GameState *gs, int tx, int ty, int *ox, int *oy);

/* pathfinding.c */
int pathfind(GameState *gs,int sx,int sy,int ex,int ey,PathCell *out,int max_len);

/* unit.c */
void unit_init_stats(GameState *gs, Unit *u);
void unit_refresh_upgrades(GameState *gs, Unit *u);
int  unit_spawn(GameState *gs,int player,UnitType type,float wx,float wy);
bool unit_tile_occupied(GameState *gs, int tx, int ty);
bool unit_find_free_tile_near(GameState *gs, int desired_tx, int desired_ty,
                              const PathCell *reserved, int reserved_count,
                              int *out_tx, int *out_ty);
void unit_compute_formation_targets(GameState *gs, int anchor_tx, int anchor_ty,
                                    int unit_count, FormationType formation,
                                    PathCell *out_targets);
void unit_give_move_order(GameState *gs,Unit *u,int tx,int ty);
void unit_give_gather_order(GameState *gs,Unit *u,int tx,int ty);
void unit_give_dropoff_order(GameState *gs,Unit *u,int tx,int ty);
void unit_give_attack_order(GameState *gs,Unit *u,int target_unit,int target_bld);
void find_adjacent_tile(GameState *gs, int bx, int by, int bw, int bh,
                        float ux, float uy, int *ox, int *oy);
void unit_give_build_order(GameState *gs,Unit *u,int bld_id);
void unit_update(GameState *gs,Unit *u,float dt);
void units_update_all(GameState *gs,float dt);
int  unit_find_idle_villager(GameState *gs,int player);
int  unit_count_military(GameState *gs,int player);

/* building.c */
int  building_place(GameState *gs,int player,BldType type,int tx,int ty);
int  building_place_ready(GameState *gs,int player,BldType type,int tx,int ty);
void building_on_complete(GameState *gs, Building *b);
void building_destroy(GameState *gs, int bid);
void building_sell(GameState *gs, int bid);
void building_update(GameState *gs,Building *b,float dt);
void buildings_update_all(GameState *gs,float dt);
int  building_find(GameState *gs,int player,BldType type,bool complete_only);
void building_enqueue_unit(GameState *gs,Building *b,UnitType ut);
int  building_queued_population(const Building *b);
Cost building_cost(BldType t);
int  building_tw(BldType t);
int  building_th(BldType t);
int  building_max_hp(BldType t);
bool building_supports_rally(BldType type);
float building_train_time(UnitType ut);
Cost  unit_cost(UnitType ut);
int   unit_age_required(UnitType t);
int   building_age_required(BldType t);
bool  building_can_train_unit(BldType bt, UnitType ut);
const char* unit_name(UnitType t);
const char* building_name(BldType t);

/* Technologies */
Cost  tech_cost(TechType t);
float tech_time(TechType t);
int   tech_age_required(TechType t);
const char* tech_name(TechType t);
const char* tech_desc(TechType t);
void  building_start_tech(GameState *gs, Building *b, TechType t);

static inline bool building_is_walllike(BldType type) {
    return type == BLD_WALL || type == BLD_GATE;
}

static inline int get_wall_line_points(int x0, int y0, int x1, int y1, int *out_x, int *out_y, int max_pts) {
    int dx = (int)fabs((float)x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -(int)fabs((float)y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;
    int count = 0;
    while (count < max_pts) {
        out_x[count] = x0; out_y[count] = y0;
        count++;
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
    return count;
}

/* resources.c */
bool  res_can_afford(PlayerRes *pr,Cost c);
void  res_deduct(PlayerRes *pr,Cost c);
void  res_add(PlayerRes *pr,ResType rt,int amt);
void  res_update_age_advance(GameState *gs,float dt);
bool  res_try_advance_age(GameState *gs,int player);
Cost  age_advance_cost(int current_age);
int   pop_cap_from_buildings(GameState *gs,int player);

/* ai.c */
void ai_update(GameState *gs,float dt);

/* game.c */
void game_init(GameState *gs);
void game_init_started_game(GameState *gs, uint32_t seed, int num_players);
void game_init_campaign(GameState *gs, uint32_t seed, int mission);
void game_campaign_restart(GameState *gs);
void game_campaign_advance(GameState *gs);
bool game_campaign_has_next(const GameState *gs);
void game_init_sandbox(GameState *gs, uint32_t seed);
void game_init_osm(GameState *gs, uint32_t seed, const char *location);
void game_update(GameState *gs,float dt);
void game_set_alert(GameState *gs,const char *msg);
void game_sandbox_add_resources(GameState *gs, int player, int amount);
void game_sandbox_next_age(GameState *gs, int player);
void game_sandbox_spawn_wave(GameState *gs, int player);
void game_sandbox_heal_selection(GameState *gs, int player, int building_id,
                                 const int *unit_ids, int unit_count);
bool game_damage_unit(GameState *gs, int target_unit, int dmg);
bool game_damage_building(GameState *gs, int target_bld, int dmg);
bool unit_uses_projectiles(UnitType type);
bool building_uses_projectiles(BldType type);
void game_spawn_projectile(GameState *gs, int owner_player, ProjectileType type,
                           float sx, float sy, float ex, float ey,
                           int target_unit, int target_bld, int dmg,
                           float duration, float arc_height);
void game_update_projectiles(GameState *gs, float dt);

/* hero_possession.c */
bool hero_possession_is_unit_eligible(const GameState *gs, int unit_id, int player);
bool hero_possession_can_start(const GameState *gs, int unit_id, int player);
bool hero_possession_start(GameState *gs, int unit_id, int player);
void hero_possession_request_exit(GameState *gs, const char *alert);
void hero_possession_update(GameState *gs, float dt);
float hero_possession_time_scale(const GameState *gs);
bool hero_possession_controls_unit(const GameState *gs, int unit_id);
