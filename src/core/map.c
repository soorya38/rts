/*=============================================================
 * map.c  –  Map generation + fog of war
 *=============================================================*/
#include "game.h"

bool map_in_bounds(int x,int y){
    return x>=0 && x<MAP_W && y>=0 && y<MAP_H;
}

bool map_is_passable(GameState *gs,int x,int y){
    if(!map_in_bounds(x,y)) return false;
    Tile *t = &gs->map[y][x];
    /* Terrain blockers */
    if(t->type == TILE_WATER)   return false;
    if(t->type == TILE_FOREST)  return false;
    if(t->type == TILE_GOLD)    return false;
    if(t->type == TILE_STONE)   return false;
    if(t->type == TILE_BERRIES) return false;
    /* TILE_DESERT and TILE_ROAD are passable */
    /* Buildings – farms are passable so villagers can gather; others block */
    if(t->building_id >= 0){
        Building *b = &gs->buildings[t->building_id];
        if(b->active && b->complete && b->type != BLD_FARM && b->type != BLD_GATE) return false;
    }
    return true;
}

bool map_is_buildable(GameState *gs,int x,int y,int w,int h){
    for(int dy=0;dy<h;dy++) for(int dx=0;dx<w;dx++){
        int nx=x+dx, ny=y+dy;
        if(!map_in_bounds(nx,ny)) return false;
        Tile *t=&gs->map[ny][nx];
        if(t->type==TILE_WATER)   return false;
        if(t->type==TILE_FOREST)  return false;
        if(t->type==TILE_GOLD)    return false;
        if(t->type==TILE_STONE)   return false;
        if(t->type==TILE_BERRIES) return false;
        /* TILE_DESERT and TILE_ROAD are buildable */
        if(t->building_id>=0)     return false;
    }
    return true;
}

void map_place_building(GameState *gs,int tx,int ty,int w,int h,int bid){
    for(int dy=0;dy<h;dy++) for(int dx=0;dx<w;dx++)
        if(map_in_bounds(tx+dx,ty+dy))
            gs->map[ty+dy][tx+dx].building_id = bid;
}

void map_clear_building(GameState *gs,int tx,int ty,int w,int h){
    for(int dy=0;dy<h;dy++) for(int dx=0;dx<w;dx++)
        if(map_in_bounds(tx+dx,ty+dy))
            gs->map[ty+dy][tx+dx].building_id = -1;
}

/* ---------- Map generation ---------- */

/* Forward declaration (defined below) */
static void force_place_patch(GameState *gs,int cx,int cy,TileType t,int r,int amt);

/* Scatter the 7 guaranteed starting resources around a TC in random
 * directions. Uses 8 compass-direction unit vectors (x10 integer),
 * shuffles them, then assigns each resource to a random distance
 * (7-13 tiles) plus a small positional jitter.
 */
static void place_start_resources(GameState *gs, int tc_x, int tc_y){
    /* Integer unit-vector table (x10) for 8 compass directions */
    static const int DX[8] = { 10,  7,  0, -7, -10, -7,  0,  7 };
    static const int DY[8] = {  0,  7, 10,  7,   0, -7, -10, -7 };

    /* Fisher-Yates shuffle of direction indices */
    int idx[8] = {0,1,2,3,4,5,6,7};
    for(int i=7;i>0;i--){
        int j=(int)(rng_next()%(i+1));
        int tmp=idx[i]; idx[i]=idx[j]; idx[j]=tmp;
    }

    /* 7 resource slots: 3 forests, 2 berries, 1 gold, 1 stone */
    TileType types[7]  = {TILE_FOREST, TILE_FOREST, TILE_FOREST,
                          TILE_BERRIES, TILE_BERRIES, TILE_GOLD, TILE_STONE};
    int amounts[7]     = {140+(int)(rng_next()%40), 140+(int)(rng_next()%40), 140+(int)(rng_next()%40),
                          450+(int)(rng_next()%100), 450+(int)(rng_next()%100),
                          700+(int)(rng_next()%100), 600+(int)(rng_next()%100)};
    int radii[7]       = {2, 2, 2, 1, 1, 1, 1};

    for(int i=0; i<7; i++){
        int d    = idx[i];
        int dist = 9 + (int)(rng_next() % 7);  /* 9-15 tiles */
        int jx   = (int)(rng_next() % 3) - 1;  /* −1..+1 jitter */
        int jy   = (int)(rng_next() % 3) - 1;
        int cx   = tc_x + (DX[d] * dist) / 10 + jx;
        int cy   = tc_y + (DY[d] * dist) / 10 + jy;
        cx = clampi(cx, 2, MAP_W-3);
        cy = clampi(cy, 2, MAP_H-3);
        force_place_patch(gs, cx, cy, types[i], radii[i], amounts[i]);
    }
}

static void place_forest_cluster(GameState *gs,int cx,int cy,int r){
    for(int dy=-r;dy<=r;dy++) for(int dx=-r;dx<=r;dx++){
        if(dx*dx+dy*dy > r*r) continue;
        int x=cx+dx, y=cy+dy;
        if(!map_in_bounds(x,y)) continue;
        if(gs->map[y][x].type!=TILE_GRASS) continue;
        gs->map[y][x].type = TILE_FOREST;
        gs->map[y][x].resource_amt = 150 + rng_range(0,100);
    }
}

static void place_resource_patch(GameState *gs,int cx,int cy,TileType t,int r,int amt){
    for(int dy=-r;dy<=r;dy++) for(int dx=-r;dx<=r;dx++){
        if(dx*dx+dy*dy > r*r) continue;
        int x=cx+dx, y=cy+dy;
        if(!map_in_bounds(x,y)) continue;
        if(gs->map[y][x].type!=TILE_GRASS) continue;
        gs->map[y][x].type = t;
        gs->map[y][x].resource_amt = amt + rng_range(0,amt/2);
    }
}

static void place_water_body(GameState *gs,int cx,int cy,int w,int h){
    for(int dy=-h;dy<=h;dy++) for(int dx=-w;dx<=w;dx++){
        float nx=(float)dx/w, ny=(float)dy/h;
        if(nx*nx+ny*ny>1.0f) continue;
        int x=cx+dx, y=cy+dy;
        if(!map_in_bounds(x,y)) continue;
        gs->map[y][x].type = TILE_WATER;
        gs->map[y][x].resource_amt = 0;
    }
}

/* Force-place a resource patch regardless of existing terrain (for start areas) */
static void force_place_patch(GameState *gs,int cx,int cy,TileType t,int r,int amt){
    for(int dy=-r;dy<=r;dy++) for(int dx=-r;dx<=r;dx++){
        if(dx*dx+dy*dy > r*r) continue;
        int x=cx+dx, y=cy+dy;
        if(!map_in_bounds(x,y)) continue;
        if(gs->map[y][x].building_id >= 0) continue;
        gs->map[y][x].type = t;
        gs->map[y][x].resource_amt = amt + rng_range(0,amt/4);
    }
}

/* Keep a safe zone clear around a start position */
static void clear_zone(GameState *gs,int cx,int cy,int r){
    for(int dy=-r;dy<=r;dy++) for(int dx=-r;dx<=r;dx++){
        int x=cx+dx, y=cy+dy;
        if(map_in_bounds(x,y)) gs->map[y][x].type=TILE_GRASS;
    }
}

static bool point_is_safe_from_starts(int x, int y, const int *start_x, const int *start_y,
                                      int num_players, int safe_r){
    for(int i=0; i<num_players; i++){
        if(abs(x - start_x[i]) <= safe_r && abs(y - start_y[i]) <= safe_r) return false;
    }
    return true;
}

static void pick_quadrant_start(int quadrant, int *out_x, int *out_y){
    const int margin = 15;
    const int mid_x = MAP_W / 2;
    const int mid_y = MAP_H / 2;

    int min_x = (quadrant == 0 || quadrant == 2) ? margin : mid_x + 8;
    int max_x = (quadrant == 0 || quadrant == 2) ? mid_x - 8 : MAP_W - margin - 1;
    int min_y = (quadrant == 0 || quadrant == 1) ? margin : mid_y + 8;
    int max_y = (quadrant == 0 || quadrant == 1) ? mid_y - 8 : MAP_H - margin - 1;

    if(max_x < min_x) max_x = min_x;
    if(max_y < min_y) max_y = min_y;

    *out_x = min_x + (int)(rng_next() % (uint32_t)(max_x - min_x + 1));
    *out_y = min_y + (int)(rng_next() % (uint32_t)(max_y - min_y + 1));
}

void map_init(GameState *gs, int *start_x, int *start_y, int num_players){
    /* Base: all grass */
    for(int y=0;y<MAP_H;y++) for(int x=0;x<MAP_W;x++){
        gs->map[y][x].type         = TILE_GRASS;
        gs->map[y][x].resource_amt = 0;
        gs->map[y][x].building_id  = -1;
        gs->map[y][x].variant      = (uint8_t)(rng_next()%4);
        for(int p=0;p<NUM_PLAYERS;p++)
            gs->map[y][x].fog[p]   = FOG_HIDDEN;
    }

    /* ── Randomise start slots ────────────────────────────────────
     * Pick one random position per quadrant so starts are still well
     * separated, but no longer glued to fixed corners.
     *   0: top-left   1: top-right   2: bottom-left   3: bottom-right
     * ─────────────────────────────────────────────────────────── */
    if (num_players <= 4) {
        int quad_x[4], quad_y[4];
        for(int i=0; i<4; i++) pick_quadrant_start(i, &quad_x[i], &quad_y[i]);

        int chosen_quads[4] = {0, 3, 1, 2};
        if(num_players <= 1){
            chosen_quads[0] = (int)(rng_next() % 4);
        } else if(num_players == 2){
            if((rng_next() & 1u) == 0u){
                chosen_quads[0] = 0; chosen_quads[1] = 3;
            } else {
                chosen_quads[0] = 1; chosen_quads[1] = 2;
            }
            if((rng_next() & 1u) != 0u){
                int tmp = chosen_quads[0];
                chosen_quads[0] = chosen_quads[1];
                chosen_quads[1] = tmp;
            }
        } else if(num_players == 3){
            int quads[4] = {0, 1, 2, 3};
            for(int i=3; i>0; i--){
                int j = (int)(rng_next() % (uint32_t)(i + 1));
                int tmp = quads[i]; quads[i] = quads[j]; quads[j] = tmp;
            }
            chosen_quads[0] = quads[0];
            chosen_quads[1] = quads[1];
            chosen_quads[2] = quads[2];
        } else {
            int quads[4] = {0, 1, 2, 3};
            for(int i=3; i>0; i--){
                int j = (int)(rng_next() % (uint32_t)(i + 1));
                int tmp = quads[i]; quads[i] = quads[j]; quads[j] = tmp;
            }
            for(int i=0; i<4; i++) chosen_quads[i] = quads[i];
        }

        for(int i=0; i<num_players; i++){
            start_x[i] = quad_x[chosen_quads[i]];
            start_y[i] = quad_y[chosen_quads[i]];
        }
    } else {
        float angle_offset = rng_frac() * 2.0f * 3.14159265f;
        for(int i=0; i<num_players; i++){
            float angle = angle_offset + (2.0f * 3.14159265f * i) / num_players;
            start_x[i] = 32 + (int)(20.0f * cosf(angle));
            start_y[i] = 32 + (int)(20.0f * sinf(angle));
            start_x[i] = clampi(start_x[i], 8, MAP_W - 9);
            start_y[i] = clampi(start_y[i], 8, MAP_H - 9);
        }
    }

    const int SAFE_R = 10;     /* tiles to keep clear of random features */

    /* Random int in [lo, hi] */
    #define rrand(lo,hi) ((int)(rng_next()%((hi)-(lo)+1))+(lo))

    /* ── Water bodies (2-4 lakes) ── */
    int num_lakes = rrand(2, 4);
    for(int i=0;i<num_lakes;i++){
        int cx, cy, tries=0;
        do {
            cx = rrand(12, MAP_W-13);
            cy = rrand(12, MAP_H-13);
            tries++;
        } while(!point_is_safe_from_starts(cx, cy, start_x, start_y, num_players, SAFE_R) && tries<20);
        int rw = rrand(3, 7), rh = rrand(2, 5);
        place_water_body(gs, cx, cy, rw, rh);
    }

    /* ── Forest clusters (12-18) ── */
    int num_forests = rrand(12, 18);
    for(int i=0;i<num_forests;i++){
        int cx, cy, tries=0;
        do {
            cx = rrand(4, MAP_W-5);
            cy = rrand(4, MAP_H-5);
            tries++;
        } while(!point_is_safe_from_starts(cx, cy, start_x, start_y, num_players, SAFE_R) && tries<20);
        int r = rrand(2, 4);
        place_forest_cluster(gs, cx, cy, r);
    }

    /* ── Gold patches (5-8) ── */
    int num_gold = rrand(5, 8);
    for(int i=0;i<num_gold;i++){
        int cx, cy, tries=0;
        do {
            cx = rrand(4, MAP_W-5);
            cy = rrand(4, MAP_H-5);
            tries++;
        } while(!point_is_safe_from_starts(cx, cy, start_x, start_y, num_players, SAFE_R) && tries<20);
        place_resource_patch(gs, cx, cy, TILE_GOLD, rrand(1,2), rrand(600,900));
    }

    /* ── Stone patches (4-6) ── */
    int num_stone = rrand(4, 6);
    for(int i=0;i<num_stone;i++){
        int cx, cy, tries=0;
        do {
            cx = rrand(4, MAP_W-5);
            cy = rrand(4, MAP_H-5);
            tries++;
        } while(!point_is_safe_from_starts(cx, cy, start_x, start_y, num_players, SAFE_R) && tries<20);
        place_resource_patch(gs, cx, cy, TILE_STONE, rrand(1,2), rrand(500,800));
    }

    /* ── Berry bushes (4-6) ── */
    int num_berries = rrand(4, 6);
    for(int i=0;i<num_berries;i++){
        int cx, cy, tries=0;
        do {
            cx = rrand(4, MAP_W-5);
            cy = rrand(4, MAP_H-5);
            tries++;
        } while(!point_is_safe_from_starts(cx, cy, start_x, start_y, num_players, SAFE_R) && tries<20);
        place_resource_patch(gs, cx, cy, TILE_BERRIES, 1, rrand(400,600));
    }

    #undef rrand

    /* ── Clear starter zones and place starting resources for each player ── */
    for(int i=0; i<num_players; i++) {
        clear_zone(gs, start_x[i], start_y[i], 5);
        place_start_resources(gs, start_x[i], start_y[i]);
    }
}


/* ---------- Fog of war ---------- */

void map_update_fog(GameState *gs){
    if(gs->mode == GAME_MODE_SANDBOX){
        for(int y=0;y<MAP_H;y++) for(int x=0;x<MAP_W;x++)
            for(int p=0;p<NUM_PLAYERS;p++)
                gs->map[y][x].fog[p] = FOG_VISIBLE;
        return;
    }

    /* Reset visible → explored */
    for(int y=0;y<MAP_H;y++) for(int x=0;x<MAP_W;x++)
        for(int p=0;p<NUM_PLAYERS;p++)
            if(gs->map[y][x].fog[p]==FOG_VISIBLE)
                gs->map[y][x].fog[p]=FOG_EXPLORED;

    /* Mark tiles visible from each unit */
    for(int i=0;i<MAX_UNITS;i++){
        Unit *u=&gs->units[i];
        if(!u->active || u->state==US_DEAD) continue;
        int p = u->player;
        int vr = (int)u->vision_range;
        int ux = (int)(u->wx/TILE_SIZE);
        int uy = (int)(u->wy/TILE_SIZE);
        for(int dy=-vr;dy<=vr;dy++) for(int dx=-vr;dx<=vr;dx++){
            if(dx*dx+dy*dy > vr*vr) continue;
            int tx=ux+dx, ty=uy+dy;
            if(map_in_bounds(tx,ty))
                gs->map[ty][tx].fog[p]=FOG_VISIBLE;
        }
    }

    /* Mark tiles visible from each complete building */
    for(int i=0;i<MAX_BUILDINGS;i++){
        Building *b=&gs->buildings[i];
        if(!b->active || !b->complete) continue;
        int p=b->player;
        int vr=4;
        int bx=b->tx+b->tw/2, by=b->ty+b->th/2;
        for(int dy=-vr;dy<=vr;dy++) for(int dx=-vr;dx<=vr;dx++){
            int tx=bx+dx, ty=by+dy;
            if(map_in_bounds(tx,ty))
                gs->map[ty][tx].fog[p]=FOG_VISIBLE;
        }
    }
}

/* ---------- Resource search ---------- */

int map_find_resource(GameState *gs,int player,ResType res,int ntx,int nty,int *ox,int *oy){
    (void)player;
    TileType want;
    switch(res){
        case RES_FOOD:  want=TILE_BERRIES; break;  /* also check TILE_FARM below */
        case RES_WOOD:  want=TILE_FOREST;  break;
        case RES_GOLD:  want=TILE_GOLD;    break;
        case RES_STONE: want=TILE_STONE;   break;
        default: return 0;
    }
    int best_dist=9999, found=0;
    for(int y=0;y<MAP_H;y++) for(int x=0;x<MAP_W;x++){
        Tile *t=&gs->map[y][x];
        bool match = (t->type==want) ||
                     (res==RES_FOOD && t->type==TILE_FARM);
        if(!match || t->resource_amt<=0) continue;
        int d=abs(x-ntx)+abs(y-nty);
        if(d<best_dist){ best_dist=d; *ox=x; *oy=y; found=1; }
    }
    return found;
}

int map_find_dropoff(GameState *gs,int player,ResType res,float wx,float wy,int *otx,int *oty){
    /* Find the nearest appropriate drop-off building */
    float best=1e30f;
    int found=0;
    for(int i=0;i<MAX_BUILDINGS;i++){
        Building *b=&gs->buildings[i];
        if(!b->active || !b->complete || b->player!=player) continue;
        bool ok=false;
        switch(res){
            case RES_FOOD:  ok=(b->type==BLD_TOWN_CENTER||b->type==BLD_MILL); break;
            case RES_WOOD:  ok=(b->type==BLD_TOWN_CENTER||b->type==BLD_LUMBER_CAMP); break;
            case RES_GOLD:
            case RES_STONE: ok=(b->type==BLD_TOWN_CENTER||b->type==BLD_MINING_CAMP); break;
            default: break;
        }
        if(!ok) continue;
        float bwx=(b->tx+b->tw/2.0f)*TILE_SIZE;
        float bwy=(b->ty+b->th/2.0f)*TILE_SIZE;
        float d=dist2f(wx,wy,bwx,bwy);
        if(d<best){ best=d; *otx=b->tx+b->tw/2; *oty=b->ty+b->th/2; found=1; }
    }
    return found;
}

int map_find_passable_near(GameState *gs, int tx, int ty, int *ox, int *oy) {
    if (map_is_passable(gs, tx, ty)) {
        *ox = tx; *oy = ty; return 1;
    }
    /* Search in expanding rings */
    for (int r = 1; r < 3; r++) {
        for (int dy = -r; dy <= r; dy++) {
            for (int dx = -r; dx <= r; dx++) {
                if (abs(dx) != r && abs(dy) != r) continue;
                int nx = tx + dx, ny = ty + dy;
                if (map_is_passable(gs, nx, ny)) {
                    *ox = nx; *oy = ny; return 1;
                }
            }
        }
    }
    return 0;
}
