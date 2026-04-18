/*=============================================================
 * unit_orders.c  –  Unit command/order functions
 *=============================================================*/
#include "game.h"

static ResType tile_to_res(TileType t){
    switch(t){
        case TILE_FOREST:  return RES_WOOD;
        case TILE_GOLD:    return RES_GOLD;
        case TILE_STONE:   return RES_STONE;
        case TILE_BERRIES:
        case TILE_FARM:    return RES_FOOD;
        default:           return RES_FOOD;
    }
}

bool unit_tile_occupied(GameState *gs, int tx, int ty){
    if(!map_in_bounds(tx, ty)) return true;
    for(int i=0;i<MAX_UNITS;i++){
        Unit *u = &gs->units[i];
        if(!u->active || u->state == US_DEAD || u->state == US_DYING) continue;
        int utx = (int)(u->wx / TILE_SIZE);
        int uty = (int)(u->wy / TILE_SIZE);
        if(utx == tx && uty == ty) return true;
    }
    return false;
}

static bool tile_reserved(const PathCell *reserved, int reserved_count, int tx, int ty){
    for(int i=0;i<reserved_count;i++){
        if(reserved[i].x == tx && reserved[i].y == ty) return true;
    }
    return false;
}

bool unit_find_free_tile_near(GameState *gs, int desired_tx, int desired_ty,
                              const PathCell *reserved, int reserved_count,
                              int *out_tx, int *out_ty){
    int best_x = -1, best_y = -1;
    int best_score = 0x7fffffff;
    int max_radius = (MAP_W > MAP_H) ? MAP_W : MAP_H;

    for(int radius=0; radius<max_radius; radius++){
        for(int dy=-radius; dy<=radius; dy++){
            for(int dx=-radius; dx<=radius; dx++){
                if(radius > 0 && abs(dx) != radius && abs(dy) != radius) continue;
                int tx = desired_tx + dx;
                int ty = desired_ty + dy;
                if(!map_in_bounds(tx, ty)) continue;
                if(!map_is_passable(gs, tx, ty)) continue;
                if(tile_reserved(reserved, reserved_count, tx, ty)) continue;
                if(unit_tile_occupied(gs, tx, ty)) continue;

                int score = dx*dx + dy*dy;
                if(score < best_score){
                    best_score = score;
                    best_x = tx;
                    best_y = ty;
                }
            }
        }
        if(best_x >= 0) break;
    }

    if(best_x < 0){
        for(int radius=0; radius<max_radius; radius++){
            for(int dy=-radius; dy<=radius; dy++){
                for(int dx=-radius; dx<=radius; dx++){
                    if(radius > 0 && abs(dx) != radius && abs(dy) != radius) continue;
                    int tx = desired_tx + dx;
                    int ty = desired_ty + dy;
                    if(!map_in_bounds(tx, ty)) continue;
                    if(!map_is_passable(gs, tx, ty)) continue;
                    if(tile_reserved(reserved, reserved_count, tx, ty)) continue;

                    int score = dx*dx + dy*dy;
                    if(score < best_score){
                        best_score = score;
                        best_x = tx;
                        best_y = ty;
                    }
                }
            }
            if(best_x >= 0) break;
        }
    }

    if(best_x < 0) return false;
    *out_tx = best_x;
    *out_ty = best_y;
    return true;
}

void unit_compute_formation_targets(GameState *gs, int anchor_tx, int anchor_ty,
                                    int unit_count, PathCell *out_targets){
    if(unit_count <= 0 || !out_targets) return;

    int width = (int)ceilf(sqrtf((float)unit_count));
    if(width < 1) width = 1;
    if(width > 5) width = 5;
    int height = (unit_count + width - 1) / width;
    float center_col = (float)(width - 1) * 0.5f;
    float center_row = (float)(height - 1) * 0.5f;

    for(int i=0;i<unit_count;i++){
        int col = i % width;
        int row = i / width;
        int desired_tx = anchor_tx + (int)lroundf((float)col - center_col);
        int desired_ty = anchor_ty + (int)lroundf((float)row - center_row);
        int tx = desired_tx;
        int ty = desired_ty;

        if(!unit_find_free_tile_near(gs, desired_tx, desired_ty, out_targets, i, &tx, &ty)){
            tx = clampi(anchor_tx, 0, MAP_W - 1);
            ty = clampi(anchor_ty, 0, MAP_H - 1);
        }
        out_targets[i] = (PathCell){tx, ty};
    }
}

/* ─── Stat table ─────────────────────────────────────────── */
typedef struct {
    int   hp, attack_dmg, armor;
    float attack_range, vision_range, move_speed, attack_cd;
    int   carry_cap;
} UnitStats;

static const UnitStats STATS[UNIT_COUNT] = {
    /*VILLAGER*/    {25,  3, 0, 1.3f, 4.0f,  80.0f, 1.5f, 10},
    /*SCOUT*/       {45,  3, 0, 1.3f, 7.0f, 130.0f, 2.0f,  0},
    /*MILITIA*/     {40,  4, 0, 1.3f, 4.0f,  95.0f, 2.0f,  0},
    /*MAN_AT_ARMS*/ {45,  6, 2, 1.3f, 4.0f,  95.0f, 2.0f,  0},
    /*SPEARMAN*/    {45,  3, 0, 1.3f, 4.0f,  90.0f, 2.0f,  0},
    /*ARCHER*/      {30,  5, 0, 5.0f, 6.0f,  90.0f, 2.0f,  0},
    /*SKIRMISHER*/  {35,  3, 1, 4.5f, 6.0f,  92.0f, 1.9f,  0},
    /*CAV_ARCHER*/  {50,  6, 0, 5.0f, 7.0f, 110.0f, 2.2f,  0},
    /*KNIGHT*/      {100,10, 3, 1.3f, 4.0f, 115.0f, 2.0f,  0},
    /*MONK*/        {30,  0, 0, 5.0f, 8.0f,  80.0f, 6.0f,  0},
};

void unit_init_stats(GameState *gs, Unit *u){
    const UnitStats *s = &STATS[u->type];
    u->hp = u->max_hp   = s->hp;
    u->attack_dmg       = s->attack_dmg;
    u->armor            = s->armor;
    u->attack_range     = s->attack_range;
    u->vision_range     = s->vision_range;
    u->move_speed       = s->move_speed;
    u->attack_cd        = s->attack_cd;
    u->attack_timer     = 0.0f;
    u->carry_cap        = s->carry_cap;
    u->carry_amt        = 0;
    u->carry_type       = RES_FOOD;
    u->gather_tx        = -1;
    u->gather_ty        = -1;
    u->build_id         = -1;
    u->target_unit      = -1;
    u->target_bld       = -1;
    u->stance_manual    = false;
    u->path_len         = 0;
    u->path_idx         = 0;
    u->death_timer      = 0.8f;
    
    if (gs) {
        if (gs->res[u->player].tech_unlocked[TECH_IRON_WEAPONRY] && (u->type == UNIT_MILITIA || u->type == UNIT_MAN_AT_ARMS)) {
            u->max_hp += 10;
            u->hp += 10;
            u->attack_dmg += 1;
        }
        if (gs->res[u->player].tech_unlocked[TECH_COMPOSITE_BOWS] && u->type == UNIT_ARCHER) {
            u->attack_dmg += 1;
            u->attack_range += 1.0f;
        }
        if (gs->res[u->player].tech_unlocked[TECH_FORGED_ARROWS] &&
            (u->type == UNIT_ARCHER || u->type == UNIT_SKIRMISHER || u->type == UNIT_CAVALRY_ARCHER)) {
            u->attack_dmg += 1;
            u->attack_range += 1.0f;
        }
        if (gs->res[u->player].tech_unlocked[TECH_MOUNTED_ARMOR] &&
            (u->type == UNIT_KNIGHT || u->type == UNIT_SCOUT || u->type == UNIT_CAVALRY_ARCHER)) {
            u->max_hp += 20;
            u->hp += 20;
        }
    }
}

int unit_spawn(GameState *gs, int player, UnitType type, float wx, float wy){
    if(gs->res[player].population >= gs->res[player].pop_cap) return -1;
    for(int i=0;i<MAX_UNITS;i++){
        Unit *u=&gs->units[i];
        if(u->active) continue;
        memset(u,0,sizeof(Unit));
        u->active = true;
        u->id     = i;
        u->player = player;
        u->type   = type;
        u->state  = US_IDLE;
        u->wx     = wx; u->wy = wy;
        u->facing = 0.0f;
        unit_init_stats(gs, u);
        gs->res[player].population++;
        if(i >= gs->unit_count) gs->unit_count = i+1;
        return i;
    }
    return -1;
}

/* ─── Adjacency helper ────────────────────────────────────── */
void find_adjacent_tile(GameState *gs, int bx, int by, int bw, int bh,
                         float ux, float uy, int *ox, int *oy){
    int best=9999; *ox=-1; *oy=-1;
    int fallback_best=9999, fallback_x=-1, fallback_y=-1;
    int utx=(int)(ux/TILE_SIZE), uty=(int)(uy/TILE_SIZE);
    for(int dy=-1;dy<=bh;dy++) for(int dx=-1;dx<=bw;dx++){
        int nx=bx+dx, ny=by+dy;
        /* Skip interior tiles */
        if(nx>=bx&&nx<bx+bw&&ny>=by&&ny<by+bh) continue;
        if(!map_in_bounds(nx,ny)) continue;
        if(!map_is_passable(gs,nx,ny)) continue;
        int d=abs(nx-utx)+abs(ny-uty);
        if(d<fallback_best){fallback_best=d;fallback_x=nx;fallback_y=ny;}
        if(unit_tile_occupied(gs, nx, ny)) continue;
        if(d<best){best=d;*ox=nx;*oy=ny;}
    }
    if(*ox<0){*ox=fallback_x;*oy=fallback_y;}
}

/* ─── Orders ──────────────────────────────────────────────── */

void unit_give_move_order(GameState *gs, Unit *u, int tx, int ty){
    int sx=(int)(u->wx/TILE_SIZE), sy=(int)(u->wy/TILE_SIZE);
    u->path_len = pathfind(gs,sx,sy,tx,ty,u->path,ASTAR_PATH_CAP);
    u->path_idx = 0;
    u->state    = (u->path_len>0) ? US_MOVING : US_IDLE;
    u->target_unit=-1; u->target_bld=-1;
    u->build_id=-1; u->gather_tx=-1;
}

void unit_give_gather_order(GameState *gs, Unit *u, int tx, int ty){
    if(u->type!=UNIT_VILLAGER) return;

    /* Discard carried resource if switching to a different type without dropping off */
    if (map_in_bounds(tx, ty)) {
        ResType target_res = tile_to_res(gs->map[ty][tx].type);
        if (u->carry_amt > 0 && u->carry_type != target_res) {
            u->carry_amt = 0; /* Discard */
        }
        u->carry_type = target_res;
    }

    u->gather_tx=tx; u->gather_ty=ty;
    int bx,by;
    find_adjacent_tile(gs,tx,ty,1,1,u->wx,u->wy,&bx,&by);
    if(bx<0){u->state=US_GATHERING;return;}
    int sx=(int)(u->wx/TILE_SIZE),sy=(int)(u->wy/TILE_SIZE);
    u->path_len=pathfind(gs,sx,sy,bx,by,u->path,ASTAR_PATH_CAP);
    u->path_idx=0;
    u->state=(u->path_len>0)?US_MOVING:US_GATHERING;
    u->target_unit=-1; u->target_bld=-1;
}

void unit_give_dropoff_order(GameState *gs, Unit *u, int tx, int ty){
    if(u->type!=UNIT_VILLAGER || u->carry_amt == 0) return;
    int sx=(int)(u->wx/TILE_SIZE),sy=(int)(u->wy/TILE_SIZE);
    
    /* Find the building at these coordinates to get its actual size */
    int bw = 1, bh = 1;
    for(int i=0;i<MAX_BUILDINGS;i++){
        Building *b = &gs->buildings[i];
        if(b->active && tx >= b->tx && tx < b->tx + b->tw && ty >= b->ty && ty < b->ty + b->th){
            bw = b->tw; bh = b->th;
            break;
        }
    }
    
    /* Find adjacent tile to the drop-off building */
    int bx, by;
    find_adjacent_tile(gs,tx,ty,bw,bh,u->wx,u->wy,&bx,&by);
    
    if(bx<0){
        u->state = US_RETURNING;
        u->path_len = 0;
        return;
    }
    
    u->path_len=pathfind(gs,sx,sy,bx,by,u->path,ASTAR_PATH_CAP);
    u->path_idx=0;
    u->state=(u->path_len>0)?US_RETURNING:US_IDLE;
    u->target_unit=-1; u->target_bld=-1;
    u->gather_tx=-1; /* Cancel gather target on manual drop-off to stay idle */
}

void unit_give_attack_order(GameState *gs, Unit *u, int tunit, int tbld){
    u->target_unit=tunit; u->target_bld=tbld;
    u->gather_tx=-1; u->build_id=-1;
    int tx,ty;
    if(tunit>=0){
        Unit *t=&gs->units[tunit];
        tx=(int)(t->wx/TILE_SIZE); ty=(int)(t->wy/TILE_SIZE);
    } else if(tbld>=0){
        Building *b=&gs->buildings[tbld];
        tx=b->tx+b->tw/2; ty=b->ty+b->th/2;
    } else { u->state=US_IDLE; return; }
    int sx=(int)(u->wx/TILE_SIZE),sy=(int)(u->wy/TILE_SIZE);
    u->path_len=pathfind(gs,sx,sy,tx,ty,u->path,ASTAR_PATH_CAP);
    u->path_idx=0;
    u->state=US_MOVING;
}

void unit_give_build_order(GameState *gs, Unit *u, int bld_id){
    if(u->type!=UNIT_VILLAGER||bld_id<0) return;
    Building *b=&gs->buildings[bld_id];
    u->build_id=bld_id;
    int bx,by;
    find_adjacent_tile(gs,b->tx,b->ty,b->tw,b->th,u->wx,u->wy,&bx,&by);
    if(bx<0){u->state=US_IDLE;return;}
    int sx=(int)(u->wx/TILE_SIZE),sy=(int)(u->wy/TILE_SIZE);
    u->path_len=pathfind(gs,sx,sy,bx,by,u->path,ASTAR_PATH_CAP);
    u->path_idx=0;
    u->state=(u->path_len>0)?US_MOVING:US_BUILDING;
    u->gather_tx=-1; u->target_unit=-1; u->target_bld=-1;
}
