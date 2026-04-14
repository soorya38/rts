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
    /* Buildings – farms are passable so villagers can gather; others block */
    if(t->building_id >= 0){
        Building *b = &gs->buildings[t->building_id];
        if(b->active && b->complete && b->type != BLD_FARM) return false;
    }
    return true;
}

bool map_is_buildable(GameState *gs,int x,int y,int w,int h){
    for(int dy=0;dy<h;dy++) for(int dx=0;dx<w;dx++){
        int nx=x+dx, ny=y+dy;
        if(!map_in_bounds(nx,ny)) return false;
        Tile *t=&gs->map[ny][nx];
        if(t->type==TILE_WATER) return false;
        if(t->building_id>=0) return false;
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

void map_init(GameState *gs){
    /* Base: all grass */
    for(int y=0;y<MAP_H;y++) for(int x=0;x<MAP_W;x++){
        gs->map[y][x].type         = TILE_GRASS;
        gs->map[y][x].resource_amt = 0;
        gs->map[y][x].building_id  = -1;
        gs->map[y][x].variant      = (uint8_t)(rng_next()%4);
        gs->map[y][x].fog[0]       = FOG_HIDDEN;
        gs->map[y][x].fog[1]       = FOG_HIDDEN;
    }

    /* Water lakes (stay away from start corners) */
    place_water_body(gs, 32,14, 6,4);
    place_water_body(gs, 20,40, 5,3);
    place_water_body(gs, 48,40, 5,3);
    place_water_body(gs, 32,50, 4,3);

    /* Mid-map forests, gold, stone, berries */
    int forests[][2]={{20,8},{8,20},{45,12},{52,8},{56,20},
                      {12,45},{8,52},{20,56},{45,45},{52,56},{56,52},
                      {30,25},{35,35},{25,35},{38,22}};
    for(int i=0;i<15;i++)
        place_forest_cluster(gs,forests[i][0],forests[i][1],3+rng_range(0,2));

    int golds[][2]={{18,15},{46,15},{15,48},{49,48},{32,32},{24,24},{42,42}};
    for(int i=0;i<7;i++)
        place_resource_patch(gs,golds[i][0],golds[i][1],TILE_GOLD,1,800);

    int stones[][2]={{22,18},{42,18},{18,44},{44,44},{28,30},{36,28}};
    for(int i=0;i<6;i++)
        place_resource_patch(gs,stones[i][0],stones[i][1],TILE_STONE,1,700);

    int berries[][2]={{32,18},{18,32},{46,32},{32,46}};
    for(int i=0;i<4;i++)
        place_resource_patch(gs,berries[i][0],berries[i][1],TILE_BERRIES,1,500);

    /* ── Clear small start zones (radius 4 only) ── */
    clear_zone(gs,  7,  7, 4);   /* player 1 TC center ~(6,6) */
    clear_zone(gs, 57, 57, 4);   /* AI TC center ~(56,56) */

    /* ── GUARANTEED starting resources, placed AFTER clear so they survive ── */

    /* Player 1 – TC at tile (4,4).  Trees, berries, gold, stone all within 8-14 tiles */
    force_place_patch(gs, 13,  5, TILE_FOREST,  2, 160);   /* NE woodline */
    force_place_patch(gs,  5, 13, TILE_FOREST,  2, 160);   /* S woodline  */
    force_place_patch(gs, 14, 12, TILE_FOREST,  2, 160);   /* E woodline  */
    force_place_patch(gs, 12,  4, TILE_BERRIES, 1, 500);   /* NE berries  */
    force_place_patch(gs,  4, 12, TILE_BERRIES, 1, 500);   /* S berries   */
    force_place_patch(gs, 13,  8, TILE_GOLD,    1, 800);   /* nearby gold */
    force_place_patch(gs,  8, 13, TILE_STONE,   1, 700);   /* nearby stone*/

    /* AI – TC at tile (54,54).  Mirror layout */
    force_place_patch(gs, 51, 60, TILE_FOREST,  2, 160);
    force_place_patch(gs, 60, 51, TILE_FOREST,  2, 160);
    force_place_patch(gs, 50, 50, TILE_FOREST,  2, 160);
    force_place_patch(gs, 62, 56, TILE_BERRIES, 1, 500);
    force_place_patch(gs, 56, 62, TILE_BERRIES, 1, 500);
    force_place_patch(gs, 51, 62, TILE_GOLD,    1, 800);
    force_place_patch(gs, 62, 51, TILE_STONE,   1, 700);
}

/* ---------- Fog of war ---------- */

void map_update_fog(GameState *gs){
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
