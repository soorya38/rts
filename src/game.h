#pragma once

/*=============================================================
 * game.h  –  Master header for the AoE2-inspired RTS game
 *             All shared types, constants, and function decls.
 *=============================================================*/

#include "raylib.h"
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
#define MAX_PATH        300
#define NUM_PLAYERS     2
#define POP_CAP_MAX     200

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

typedef struct {
    int   amount[RES_COUNT];
    int   population;
    int   pop_cap;
    int   age;              /* 0=Dark 1=Feudal 2=Castle 3=Imperial */
    bool  advancing;
    float advance_timer;
} PlayerRes;

/* ─── Path cell ──────────────────────────────────────────────── */
typedef struct { int x, y; } PathCell;

/* ─── Unit types & stats ─────────────────────────────────────── */
typedef enum {
    UNIT_VILLAGER=0, UNIT_SCOUT,
    UNIT_MILITIA, UNIT_MAN_AT_ARMS,
    UNIT_ARCHER,  UNIT_KNIGHT,
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

    PathCell  path[MAX_PATH];
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

    bool      selected;
    float     anim_timer;
    float     facing;           /* radians */
    float     death_timer;
} Unit;

/* ─── Building types ─────────────────────────────────────────── */
typedef enum {
    BLD_TOWN_CENTER=0, BLD_HOUSE,
    BLD_BARRACKS, BLD_ARCHERY_RANGE, BLD_STABLE,
    BLD_MILL, BLD_LUMBER_CAMP, BLD_MINING_CAMP,
    BLD_FARM, BLD_COUNT
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
} Building;

/* ─── Build mode ghost ───────────────────────────────────────── */
typedef struct {
    bool    active;
    BldType type;
    int     ghost_tx, ghost_ty;
    bool    valid;
} BuildMode;

/* ─── Game phase ─────────────────────────────────────────────── */
typedef enum {
    PHASE_MENU=0, PHASE_PLAYING,
    PHASE_PAUSED, PHASE_VICTORY, PHASE_DEFEAT
} GamePhase;

/* ─── Master game state ──────────────────────────────────────── */
typedef struct {
    GamePhase  phase;
    float      game_time;

    Tile       map[MAP_H][MAP_W];
    Unit       units[MAX_UNITS];
    Building   buildings[MAX_BUILDINGS];
    int        unit_count;
    int        bld_count;

    PlayerRes  res[NUM_PLAYERS];

    Camera2D   camera;

    /* Selection */
    bool       box_selecting;
    Vector2    box_start;
    int        sel_units[MAX_UNITS];
    int        sel_count;
    int        sel_building;        /* -1 = none */
    int        sel_tile_x, sel_tile_y; /* -1 = none; last clicked resource tile */


    BuildMode  build_mode;

    /* AI */
    int        ai_phase;
    float      ai_timer;
    float      ai_attack_cd;
    int        ai_attack_count;

    /* Alert banner */
    char       alert[64];
    float      alert_timer;

    /* Menu */
    bool       menu_start_hover;
    bool       build_panel_open;  /* build type picker UI open */
} GameState;

/* ─── Resource cost table helper ────────────────────────────── */
typedef struct { int food,wood,gold,stone; } Cost;

/* ──────────────────────────────────────────────────────────────
 * Function declarations — implemented in respective .c files
 * ────────────────────────────────────────────────────────────── */

/* map.c */
void map_init(GameState *gs);
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
void unit_init_stats(Unit *u);
int  unit_spawn(GameState *gs,int player,UnitType type,float wx,float wy);
void unit_give_move_order(GameState *gs,Unit *u,int tx,int ty);
void unit_give_gather_order(GameState *gs,Unit *u,int tx,int ty);
void unit_give_attack_order(GameState *gs,Unit *u,int target_unit,int target_bld);
void unit_give_build_order(GameState *gs,Unit *u,int bld_id);
void unit_update(GameState *gs,Unit *u,float dt);
void units_update_all(GameState *gs,float dt);
int  unit_find_idle_villager(GameState *gs,int player);
int  unit_count_military(GameState *gs,int player);

/* building.c */
int  building_place(GameState *gs,int player,BldType type,int tx,int ty);
void building_update(GameState *gs,Building *b,float dt);
void buildings_update_all(GameState *gs,float dt);
int  building_find(GameState *gs,int player,BldType type,bool complete_only);
void building_enqueue_unit(GameState *gs,Building *b,UnitType ut);
Cost building_cost(BldType t);
int  building_tw(BldType t);
int  building_th(BldType t);
int  building_max_hp(BldType t);
float building_train_time(UnitType ut);
Cost  unit_cost(UnitType ut);

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

/* input.c */
void input_update(GameState *gs);

/* renderer.c */
void renderer_draw_world(GameState *gs);

/* hud.c */
void hud_draw(GameState *gs);

/* game.c */
void game_init(GameState *gs);
void game_update(GameState *gs,float dt);
void game_set_alert(GameState *gs,const char *msg);
