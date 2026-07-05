/*=============================================================
 * unit_orders.c  –  Unit spawning, stat tables, formations,
 *                    and command/order dispatch
 *
 * Contains: unit stat definitions, tech upgrade application,
 * formation calculations, and all unit_give_*_order functions.
 *=============================================================*/
#include "game.h"
#include "threadpool.h"

/* Map tile type to the resource it yields when gathered. */
static ResType tile_to_resource(TileType tile_type)
{
    switch (tile_type) {
        case TILE_FOREST:  return RES_WOOD;
        case TILE_GOLD:    return RES_GOLD;
        case TILE_STONE:   return RES_STONE;
        case TILE_BERRIES:
        case TILE_FARM:    return RES_FOOD;
        default:           return RES_FOOD;
    }
}

bool unit_tile_occupied(GameState *gs, int tx, int ty)
{
    if (!map_in_bounds(tx, ty)) return true;
    for (int i = 0; i < MAX_UNITS; i++) {
        Unit *unit = &gs->units[i];
        if (!unit->active || unit->state == US_DEAD || unit->state == US_DYING) continue;
        int unit_tx = (int)(unit->wx / TILE_SIZE);
        int unit_ty = (int)(unit->wy / TILE_SIZE);
        if (unit_tx == tx && unit_ty == ty) return true;
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
                                    int unit_count, FormationType formation,
                                    PathCell *out_targets){
    if(unit_count <= 0 || !out_targets) return;

    for(int i=0;i<unit_count;i++){
        int desired_tx = anchor_tx;
        int desired_ty = anchor_ty;
        switch(formation){
            case FORMATION_LINE: {
                float center = (float)(unit_count - 1) * 0.5f;
                desired_tx = anchor_tx + (int)lroundf((float)i - center);
                desired_ty = anchor_ty;
            } break;
            case FORMATION_COLUMN: {
                float center = (float)(unit_count - 1) * 0.5f;
                desired_tx = anchor_tx;
                desired_ty = anchor_ty + (int)lroundf((float)i - center);
            } break;
            case FORMATION_WEDGE: {
                int row = 0;
                int first = 0;
                while(first + row + 1 <= i){
                    first += row + 1;
                    row++;
                }
                int col = i - first;
                float center = (float)row * 0.5f;
                desired_tx = anchor_tx + (int)lroundf((float)col - center);
                desired_ty = anchor_ty + row;
            } break;
            case FORMATION_BOX:
            default: {
                int width = (int)ceilf(sqrtf((float)unit_count));
                if(width < 1) width = 1;
                if(width > 5) width = 5;
                int height = (unit_count + width - 1) / width;
                int col = i % width;
                int row = i / width;
                float center_col = (float)(width - 1) * 0.5f;
                float center_row = (float)(height - 1) * 0.5f;
                desired_tx = anchor_tx + (int)lroundf((float)col - center_col);
                desired_ty = anchor_ty + (int)lroundf((float)row - center_row);
            } break;
        }
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
    /*FISHING_SHIP*/{60,  0, 0, 1.3f, 6.0f,  85.0f, 2.0f, 15},
    /*WAR_GALLEY*/  {135, 7, 1, 6.0f, 8.0f,  95.0f, 2.4f,  0},
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

int unit_spawn(GameState *gs, int player, UnitType type, float wx, float wy)
{
    if (gs->res[player].population >= gs->res[player].pop_cap) return -1;

    for (int i = 0; i < MAX_UNITS; i++) {
        Unit *unit = &gs->units[i];
        if (unit->active) continue;

        memset(unit, 0, sizeof(Unit));
        unit->active = true;
        unit->id     = i;
        unit->player = player;
        unit->type   = type;
        unit->state  = US_IDLE;
        unit->wx     = wx;
        unit->wy     = wy;
        unit->facing = 0.0f;
        unit_init_stats(gs, unit);

        gs->res[player].population++;
        if (i >= gs->unit_count) gs->unit_count = i + 1;
        return i;
    }
    return -1;
}

/* ─── Adjacency helper ──────────────────────────────────────── */

/* Find the closest passable tile adjacent to a building's footprint. */
void find_adjacent_tile(GameState *gs, int bld_x, int bld_y,
                       int bld_w, int bld_h,
                       float unit_wx, float unit_wy,
                       int *out_x, int *out_y)
{
    int best_dist     = 9999;
    int fallback_dist = 9999;
    int fallback_x = -1, fallback_y = -1;
    *out_x = -1;
    *out_y = -1;

    int unit_tx = (int)(unit_wx / TILE_SIZE);
    int unit_ty = (int)(unit_wy / TILE_SIZE);

    for (int dy = -1; dy <= bld_h; dy++) {
        for (int dx = -1; dx <= bld_w; dx++) {
            int nx = bld_x + dx;
            int ny = bld_y + dy;

            /* Skip tiles inside the building footprint. */
            if (nx >= bld_x && nx < bld_x + bld_w &&
                ny >= bld_y && ny < bld_y + bld_h) continue;
            if (!map_in_bounds(nx, ny)) continue;
            if (!map_is_passable(gs, nx, ny)) continue;

            int dist = abs(nx - unit_tx) + abs(ny - unit_ty);
            if (dist < fallback_dist) {
                fallback_dist = dist;
                fallback_x = nx;
                fallback_y = ny;
            }
            if (unit_tile_occupied(gs, nx, ny)) continue;
            if (dist < best_dist) {
                best_dist = dist;
                *out_x = nx;
                *out_y = ny;
            }
        }
    }

    /* If no unoccupied tile found, fall back to nearest passable. */
    if (*out_x < 0) {
        *out_x = fallback_x;
        *out_y = fallback_y;
    }
}

/* ── Orders ──────────────────────────────────────────────── */

/* Clear all targeting state and return unit to idle. */
static void clear_unit_targets(Unit *unit)
{
    unit->path_len    = 0;
    unit->path_idx    = 0;
    unit->state       = US_IDLE;
    unit->target_unit = -1;
    unit->target_bld  = -1;
    unit->build_id    = -1;
    unit->gather_tx   = -1;
}

void unit_give_move_order(GameState *gs, Unit *unit, int tx, int ty)
{
    bool water = unit_is_ship(unit->type);
    int move_tx = tx;
    int move_ty = ty;
    if (!map_find_passable_near_dom(gs, tx, ty, water, &move_tx, &move_ty)) {
        clear_unit_targets(unit);
        return;
    }

    int src_tx = (int)(unit->wx / TILE_SIZE);
    int src_ty = (int)(unit->wy / TILE_SIZE);
    unit->path_len = pathfind_dom(gs, src_tx, src_ty, move_tx, move_ty,
                                  unit->path, ASTAR_PATH_CAP, water);
    unit->path_idx    = 0;
    unit->state       = (unit->path_len > 0) ? US_MOVING : US_IDLE;
    unit->target_unit = -1;
    unit->target_bld  = -1;
    unit->build_id    = -1;
    unit->gather_tx   = -1;
}

/* ── Batched group move ───────────────────────────────────────
 * Issuing a move order to a large selection runs one A* search
 * per unit in a single frame — the biggest frame spike in the
 * game.  Each search only reads the map and writes its own
 * unit's fields (pathfind's workspace is thread-local), so the
 * batch fans out across the pool.  Per-unit results depend only
 * on that unit's inputs, keeping the sim deterministic. */
typedef struct {
    GameState      *gs;
    const int      *unit_ids;
    const PathCell *dests;  /* one destination per unit */
} MoveBatchCtx;

static void move_batch_range(int start, int end, void *ctx)
{
    MoveBatchCtx *mb = ctx;
    for (int i = start; i < end; i++) {
        unit_give_move_order(mb->gs, &mb->gs->units[mb->unit_ids[i]],
                             mb->dests[i].x, mb->dests[i].y);
    }
}

void unit_give_move_orders_batch(GameState *gs, const int *unit_ids,
                                 const PathCell *dests, int count)
{
    MoveBatchCtx ctx = { gs, unit_ids, dests };
    if (count >= 8) {
        tp_parallel_for(count, move_batch_range, &ctx);
    } else {
        move_batch_range(0, count, &ctx);
    }
}

void unit_give_gather_order(GameState *gs, Unit *unit, int tx, int ty)
{
    /* Fishing ships gather food from fish (water tiles with stock),
       routing over water and standing on the fish tile itself. */
    if (unit_is_ship(unit->type)) {
        if (unit->type != UNIT_FISHING_SHIP) return;
        if (!map_in_bounds(tx, ty)) return;
        unit->carry_type = RES_FOOD;
        unit->gather_tx = tx;
        unit->gather_ty = ty;
        int src_tx = (int)(unit->wx / TILE_SIZE);
        int src_ty = (int)(unit->wy / TILE_SIZE);
        unit->path_len = pathfind_dom(gs, src_tx, src_ty, tx, ty,
                                      unit->path, ASTAR_PATH_CAP, true);
        unit->path_idx    = 0;
        unit->state       = (unit->path_len > 0) ? US_MOVING : US_GATHERING;
        unit->target_unit = -1;
        unit->target_bld  = -1;
        return;
    }

    if (unit->type != UNIT_VILLAGER) return;

    /* Discard carried resources when switching to a different type. */
    if (map_in_bounds(tx, ty)) {
        ResType target_res = tile_to_resource(gs->map[ty][tx].type);
        if (unit->carry_amt > 0 && unit->carry_type != target_res) {
            unit->carry_amt = 0;
        }
        unit->carry_type = target_res;
    }

    unit->gather_tx = tx;
    unit->gather_ty = ty;

    int adj_x, adj_y;
    find_adjacent_tile(gs, tx, ty, 1, 1, unit->wx, unit->wy, &adj_x, &adj_y);
    if (adj_x < 0) {
        unit->state = US_GATHERING;
        return;
    }

    int src_tx = (int)(unit->wx / TILE_SIZE);
    int src_ty = (int)(unit->wy / TILE_SIZE);
    unit->path_len = pathfind(gs, src_tx, src_ty, adj_x, adj_y,
                              unit->path, ASTAR_PATH_CAP);
    unit->path_idx    = 0;
    unit->state       = (unit->path_len > 0) ? US_MOVING : US_GATHERING;
    unit->target_unit = -1;
    unit->target_bld  = -1;
}

void unit_give_dropoff_order(GameState *gs, Unit *unit, int tx, int ty)
{
    if (unit->type != UNIT_VILLAGER || unit->carry_amt == 0) return;

    /* Find the building at (tx,ty) to determine its footprint size. */
    int bld_w = 1, bld_h = 1;
    for (int i = 0; i < MAX_BUILDINGS; i++) {
        Building *bld = &gs->buildings[i];
        if (bld->active &&
            tx >= bld->tx && tx < bld->tx + bld->tw &&
            ty >= bld->ty && ty < bld->ty + bld->th) {
            bld_w = bld->tw;
            bld_h = bld->th;
            break;
        }
    }

    int adj_x, adj_y;
    find_adjacent_tile(gs, tx, ty, bld_w, bld_h,
                       unit->wx, unit->wy, &adj_x, &adj_y);
    if (adj_x < 0) {
        unit->state    = US_RETURNING;
        unit->path_len = 0;
        return;
    }

    int src_tx = (int)(unit->wx / TILE_SIZE);
    int src_ty = (int)(unit->wy / TILE_SIZE);
    unit->path_len = pathfind(gs, src_tx, src_ty, adj_x, adj_y,
                              unit->path, ASTAR_PATH_CAP);
    unit->path_idx    = 0;
    unit->state       = (unit->path_len > 0) ? US_RETURNING : US_IDLE;
    unit->target_unit = -1;
    unit->target_bld  = -1;
    unit->gather_tx   = -1;  /* Cancel gather target on manual drop-off. */
}

void unit_give_attack_order(GameState *gs, Unit *unit,
                            int target_unit_id, int target_bld_id)
{
    unit->target_unit = target_unit_id;
    unit->target_bld  = target_bld_id;
    unit->gather_tx   = -1;
    unit->build_id    = -1;
    unit->anim_timer  = 1.0f;  /* Allow immediate re-pathfinding. */

    bool water = unit_is_ship(unit->type);
    int dest_tx, dest_ty;

    if (target_unit_id >= 0) {
        /* Pathfind to a tile adjacent to the target (in our domain). */
        Unit *target = &gs->units[target_unit_id];
        int enemy_tx = (int)(target->wx / TILE_SIZE);
        int enemy_ty = (int)(target->wy / TILE_SIZE);
        int src_tx   = (int)(unit->wx / TILE_SIZE);
        int src_ty   = (int)(unit->wy / TILE_SIZE);

        int best_dist_sq = 99999;
        dest_tx = enemy_tx;
        dest_ty = enemy_ty;
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                int nx = enemy_tx + dx, ny = enemy_ty + dy;
                if (nx == enemy_tx && ny == enemy_ty) continue;
                if (!map_in_bounds(nx, ny) || !map_is_passable_dom(gs, nx, ny, water)) continue;
                int d = (nx - src_tx) * (nx - src_tx) + (ny - src_ty) * (ny - src_ty);
                if (d < best_dist_sq) {
                    best_dist_sq = d;
                    dest_tx = nx;
                    dest_ty = ny;
                }
            }
        }
        /* A ship that can't reach the target's tile (e.g. a land
           unit inland) still closes to the nearest open water and
           relies on ranged fire once in range. */
        if (water && best_dist_sq == 99999 &&
            !map_find_passable_near_dom(gs, enemy_tx, enemy_ty, true, &dest_tx, &dest_ty)) {
            unit->path_len = 0;
            unit->path_idx = 0;
            unit->state    = US_ATTACKING;
            return;
        }
    } else if (target_bld_id >= 0) {
        Building *bld = &gs->buildings[target_bld_id];
        int adj_x = -1, adj_y = -1;
        if (water) {
            /* Nearest open-water tile bordering the building. */
            long best = -1;
            for (int dy = -1; dy <= bld->th; dy++)
                for (int dx = -1; dx <= bld->tw; dx++) {
                    int nx = bld->tx + dx, ny = bld->ty + dy;
                    if (!map_tile_is_water(gs, nx, ny)) continue;
                    long d = (long)(nx*TILE_SIZE - (int)unit->wx)*(nx*TILE_SIZE - (int)unit->wx);
                    if (best < 0 || d < best) { best = d; adj_x = nx; adj_y = ny; }
                }
        } else {
            find_adjacent_tile(gs, bld->tx, bld->ty, bld->tw, bld->th,
                               unit->wx, unit->wy, &adj_x, &adj_y);
        }
        if (adj_x < 0) {
            unit->path_len = 0;
            unit->path_idx = 0;
            unit->state    = US_ATTACKING;
            return;
        }
        dest_tx = adj_x;
        dest_ty = adj_y;
    } else {
        unit->state = US_IDLE;
        return;
    }

    int src_tx = (int)(unit->wx / TILE_SIZE);
    int src_ty = (int)(unit->wy / TILE_SIZE);
    unit->path_len = pathfind_dom(gs, src_tx, src_ty, dest_tx, dest_ty,
                                  unit->path, ASTAR_PATH_CAP, water);
    unit->path_idx = 0;
    unit->state    = (unit->path_len > 0) ? US_MOVING : US_ATTACKING;
}

void unit_give_build_order(GameState *gs, Unit *unit, int bld_id)
{
    if (unit->type != UNIT_VILLAGER || bld_id < 0) return;

    Building *bld = &gs->buildings[bld_id];
    if (!bld->active || bld->player != unit->player) return;
    if (bld->complete && bld->hp >= bld->max_hp) return;

    unit->build_id = bld_id;

    int adj_x, adj_y;
    find_adjacent_tile(gs, bld->tx, bld->ty, bld->tw, bld->th,
                       unit->wx, unit->wy, &adj_x, &adj_y);
    if (adj_x < 0) {
        unit->state = US_IDLE;
        return;
    }

    int src_tx = (int)(unit->wx / TILE_SIZE);
    int src_ty = (int)(unit->wy / TILE_SIZE);
    unit->path_len = pathfind(gs, src_tx, src_ty, adj_x, adj_y,
                              unit->path, ASTAR_PATH_CAP);
    unit->path_idx    = 0;
    unit->state       = (unit->path_len > 0) ? US_MOVING : US_BUILDING;
    unit->gather_tx   = -1;
    unit->target_unit = -1;
    unit->target_bld  = -1;
}
