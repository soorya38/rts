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
    /*BAT_RAM*/     {175, 2, 4, 1.2f, 4.5f,  52.0f, 2.8f,  0},
    /*MANGONEL*/    {60, 35, 0, 7.0f, 8.0f,  58.0f, 4.5f,  0},
    /*SCORPION*/    {55, 16, 1, 6.0f, 8.0f,  62.0f, 3.1f,  0},
    /*BOMBARD*/     {75, 42, 1, 8.5f, 9.0f,  56.0f, 5.0f,  0},
};

static bool unit_is_infantry(UnitType t){
    return t == UNIT_MILITIA || t == UNIT_MAN_AT_ARMS || t == UNIT_SPEARMAN;
}

static bool unit_is_archery(UnitType t){
    return t == UNIT_ARCHER || t == UNIT_SKIRMISHER || t == UNIT_CAVALRY_ARCHER;
}

static bool unit_is_cavalry(UnitType t){
    return t == UNIT_SCOUT || t == UNIT_KNIGHT || t == UNIT_CAVALRY_ARCHER;
}

static bool unit_is_melee_military(UnitType t){
    return unit_is_infantry(t) || t == UNIT_SCOUT || t == UNIT_KNIGHT || t == UNIT_BATTERING_RAM;
}

static bool unit_is_siege(UnitType t){
    return t == UNIT_BATTERING_RAM || t == UNIT_MANGONEL ||
           t == UNIT_SCORPION || t == UNIT_BOMBARD_CANNON;
}

void unit_refresh_upgrades(GameState *gs, Unit *u){
    if(!gs || !u) return;

    int missing_hp = 0;
    if(u->max_hp > 0) missing_hp = clampi(u->max_hp - u->hp, 0, u->max_hp);

    const UnitStats *s = &STATS[u->type];
    u->max_hp       = s->hp;
    u->attack_dmg   = s->attack_dmg;
    u->armor        = s->armor;
    u->attack_range = s->attack_range;
    u->vision_range = s->vision_range;
    u->move_speed   = s->move_speed;
    u->attack_cd    = s->attack_cd;
    u->carry_cap    = s->carry_cap;

    PlayerRes *pr = &gs->res[u->player];

    if (pr->tech_unlocked[TECH_LOOM] && u->type == UNIT_VILLAGER) {
        u->max_hp += 15;
        u->armor += 1;
    }
    if (pr->tech_unlocked[TECH_WHEELBARROW] && u->type == UNIT_VILLAGER) {
        u->carry_cap += 2;
        u->move_speed += 8.0f;
    }
    if (pr->tech_unlocked[TECH_HAND_CART] && u->type == UNIT_VILLAGER) {
        u->carry_cap += 3;
        u->move_speed += 10.0f;
        u->max_hp += 10;
    }
    if (pr->tech_unlocked[TECH_TIMBER_ROUTE] && u->type == UNIT_VILLAGER) {
        u->move_speed += 6.0f;
    }
    if (pr->tech_unlocked[TECH_HARDWOOD_CARTS] && u->type == UNIT_VILLAGER) {
        u->move_speed += 6.0f;
    }

    if (pr->tech_unlocked[TECH_IRON_WEAPONRY] && unit_is_infantry(u->type)) {
        u->max_hp += 10;
        u->attack_dmg += 1;
    }
    if (pr->tech_unlocked[TECH_SQUIRES] && unit_is_infantry(u->type)) {
        u->move_speed += 8.0f;
    }
    if (pr->tech_unlocked[TECH_CHAIN_MAIL] && unit_is_infantry(u->type)) {
        u->max_hp += 10;
        u->armor += 1;
    }
    if (pr->tech_unlocked[TECH_HARDENED_BLADES] && unit_is_infantry(u->type)) {
        u->attack_dmg += 1;
    }
    if (pr->tech_unlocked[TECH_IMPERIAL_INFANTRY] && unit_is_infantry(u->type)) {
        u->max_hp += 15;
        u->attack_dmg += 2;
    }
    if (pr->tech_unlocked[TECH_VETERAN_LEGION] && unit_is_infantry(u->type)) {
        u->max_hp += 15;
    }

    if (pr->tech_unlocked[TECH_COMPOSITE_BOWS] && (u->type == UNIT_ARCHER || u->type == UNIT_CAVALRY_ARCHER)) {
        u->attack_dmg += 1;
        u->attack_range += 1.0f;
    }
    if (pr->tech_unlocked[TECH_THUMB_RING] && unit_is_archery(u->type)) {
        u->move_speed += 8.0f;
        u->attack_cd = clampf(u->attack_cd - 0.2f, 0.6f, 10.0f);
    }
    if (pr->tech_unlocked[TECH_REINFORCED_STRINGS] && unit_is_archery(u->type)) {
        u->attack_dmg += 1;
        u->armor += 1;
    }
    if (pr->tech_unlocked[TECH_EAGLE_EYE] && unit_is_archery(u->type)) {
        u->attack_range += 1.0f;
        u->vision_range += 1.0f;
    }
    if (pr->tech_unlocked[TECH_IMPERIAL_ARCHERY] && unit_is_archery(u->type)) {
        u->attack_dmg += 1;
        u->attack_range += 1.0f;
    }
    if (pr->tech_unlocked[TECH_FIELD_CRAFT] && unit_is_archery(u->type)) {
        u->max_hp += 10;
    }

    if (pr->tech_unlocked[TECH_MOUNTED_ARMOR] && unit_is_cavalry(u->type)) {
        u->max_hp += 20;
    }
    if (pr->tech_unlocked[TECH_HUSBANDRY] && unit_is_cavalry(u->type)) {
        u->move_speed += 12.0f;
    }
    if (pr->tech_unlocked[TECH_CAVALRY_DRILL] && unit_is_cavalry(u->type)) {
        u->attack_dmg += 1;
        u->move_speed += 10.0f;
    }
    if (pr->tech_unlocked[TECH_BLOODLINES] && unit_is_cavalry(u->type)) {
        u->max_hp += 20;
    }
    if (pr->tech_unlocked[TECH_IMPERIAL_CAVALRY] && unit_is_cavalry(u->type)) {
        u->max_hp += 20;
        u->armor += 1;
    }
    if (pr->tech_unlocked[TECH_STEEL_SPURS] && unit_is_cavalry(u->type)) {
        u->attack_dmg += 1;
    }

    if (pr->tech_unlocked[TECH_SANCTITY] && u->type == UNIT_MONK) {
        u->max_hp += 15;
    }
    if (pr->tech_unlocked[TECH_DEVOTION] && u->type == UNIT_MONK) {
        u->max_hp += 15;
    }
    if (pr->tech_unlocked[TECH_FERVOR] && u->type == UNIT_MONK) {
        u->move_speed += 12.0f;
    }
    if (pr->tech_unlocked[TECH_ILLUMINATION] && u->type == UNIT_MONK) {
        u->attack_cd = clampf(u->attack_cd - 1.0f, 1.5f, 10.0f);
    }
    if (pr->tech_unlocked[TECH_BLOCK_PRINTING] && u->type == UNIT_MONK) {
        u->attack_range += 1.0f;
    }
    if (pr->tech_unlocked[TECH_HOLY_VISION] && u->type == UNIT_MONK) {
        u->vision_range += 2.0f;
    }

    if (pr->tech_unlocked[TECH_REINFORCED_RAM] && u->type == UNIT_BATTERING_RAM) {
        u->max_hp += 80;
        u->attack_dmg += 4;
    }
    if (pr->tech_unlocked[TECH_SIEGE_ENGINEERS] && unit_is_siege(u->type)) {
        u->attack_range += 1.0f;
    }
    if (pr->tech_unlocked[TECH_ONAGER] && u->type == UNIT_MANGONEL) {
        u->attack_dmg += 12;
        u->attack_range += 1.0f;
    }
    if (pr->tech_unlocked[TECH_DRILL_CREW] && unit_is_siege(u->type)) {
        u->move_speed += 8.0f;
    }
    if (pr->tech_unlocked[TECH_HEAVY_SCORPION] && u->type == UNIT_SCORPION) {
        u->attack_dmg += 8;
        u->armor += 1;
        u->attack_range += 1.0f;
    }
    if (pr->tech_unlocked[TECH_TORSION_ENGINES] && unit_is_siege(u->type)) {
        u->attack_dmg += 8;
    }

    if (pr->tech_unlocked[TECH_SCALE_ARMOR] && u->type != UNIT_VILLAGER && u->type != UNIT_SCOUT) {
        u->armor += 1;
    }
    if (pr->tech_unlocked[TECH_PLATE_ARMOR] && u->type != UNIT_VILLAGER && u->type != UNIT_MONK) {
        u->armor += 1;
    }
    if (pr->tech_unlocked[TECH_BLAST_FURNACE] && unit_is_melee_military(u->type)) {
        u->attack_dmg += 1;
    }
    if (pr->tech_unlocked[TECH_FORGED_ARROWS] &&
        (u->type == UNIT_ARCHER || u->type == UNIT_SKIRMISHER || u->type == UNIT_CAVALRY_ARCHER)) {
        u->attack_dmg += 1;
        u->attack_range += 1.0f;
    }
    if (pr->tech_unlocked[TECH_BODKIN_ARROW] &&
        (u->type == UNIT_ARCHER || u->type == UNIT_SKIRMISHER || u->type == UNIT_CAVALRY_ARCHER)) {
        u->attack_dmg += 1;
        u->attack_range += 1.0f;
    }
    if (pr->tech_unlocked[TECH_BRACER] &&
        (u->type == UNIT_ARCHER || u->type == UNIT_SKIRMISHER || u->type == UNIT_CAVALRY_ARCHER)) {
        u->attack_dmg += 1;
        u->attack_range += 1.0f;
    }
    if (pr->tech_unlocked[TECH_CHEMISTRY] &&
        (u->type == UNIT_ARCHER || u->type == UNIT_SKIRMISHER || u->type == UNIT_CAVALRY_ARCHER)) {
        u->attack_dmg += 1;
    }

    if (u->hp <= 0) u->hp = u->max_hp;
    else u->hp = clampi(u->max_hp - missing_hp, 1, u->max_hp);
    if (u->attack_timer > u->attack_cd) u->attack_timer = u->attack_cd;
}

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

    if (gs) unit_refresh_upgrades(gs, u);
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
    int move_tx = tx;
    int move_ty = ty;
    if(!map_find_passable_near(gs, tx, ty, &move_tx, &move_ty)){
        u->path_len = 0;
        u->path_idx = 0;
        u->state = US_IDLE;
        u->target_unit = -1;
        u->target_bld = -1;
        u->build_id = -1;
        u->gather_tx = -1;
        return;
    }

    int sx=(int)(u->wx/TILE_SIZE), sy=(int)(u->wy/TILE_SIZE);
    u->path_len = pathfind(gs,sx,sy,move_tx,move_ty,u->path,ASTAR_PATH_CAP);
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
