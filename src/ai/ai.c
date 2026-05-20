/*=============================================================
 * ai.c  –  Enemy AI_PLAYER: economy, tech, army, and attack control
 *
 * Runs on a 0.5s think cycle.  Manages villager assignment,
 * building placement, research queuing, unit training, scout
 * exploration, and attack wave timing.
 *=============================================================*/
#include "game.h"

/* ── Player IDs ────────────────────────────────────────────── */
static const int AI_PLAYER = 1;
static const int HUMAN_PLAYER = 0;

/* ── AI_PLAYER phase identifiers ──────────────────────────────────── */
enum {
    AI_PHASE_GATHER   = 0,
    AI_PHASE_BUILD    = 1,
    AI_PHASE_MILITARY = 2,
    AI_PHASE_ATTACK   = 3,
};

/* ── Timing constants ──────────────────────────────────────── */
static const float AI_THINK_RATE = 0.5f;   /* seconds between AI ticks */
static const float AI_ATTACK_CD  = 60.0f;  /* cooldown between attack waves */

static int ai_count_queued_units(GameState *gs, UnitType type){
    int count = 0;
    for(int i=0; i<MAX_BUILDINGS; i++){
        Building *b = &gs->buildings[i];
        if(!b->active || b->player != AI_PLAYER) continue;
        for(int j=0; j<b->queue_len; j++){
            if(b->queue[j] == type) count++;
        }
    }
    return count;
}

static int ai_dark_age_villager_target(GameState *gs){
    (void)gs;
    return 22;   /* aim for ~21-23 pop before advancing to Feudal */
}

static int ai_feudal_econ_villager_target(GameState *gs){
    (void)gs;
    return 24;
}

static bool ai_send_villager_gather(GameState *gs, int uid, ResType rt){
    Unit *u = &gs->units[uid];
    int tx, ty;
    int utx = (int)(u->wx / TILE_SIZE);
    int uty = (int)(u->wy / TILE_SIZE);
    if(!map_find_resource(gs, AI_PLAYER, rt, utx, uty, &tx, &ty)) return false;
    unit_give_gather_order(gs, u, tx, ty);
    return true;
}

static bool ai_find_tc_center(GameState *gs, int *out_tx, int *out_ty){
    int tc = building_find(gs, AI_PLAYER, BLD_TOWN_CENTER, true);
    if(tc < 0) return false;
    Building *town = &gs->buildings[tc];
    *out_tx = town->tx + town->tw / 2;
    *out_ty = town->ty + town->th / 2;
    return true;
}

static int ai_find_scout(GameState *gs){
    for(int i=0; i<MAX_UNITS; i++){
        Unit *u = &gs->units[i];
        if(!u->active || u->player != AI_PLAYER || u->state == US_DEAD) continue;
        if(u->type == UNIT_SCOUT) return i;
    }
    return -1;
}

/* Find the nearest unexplored tile, biased heavily toward the base area
 * and resource-rich tiles so the scout covers the immediate surroundings
 * (own base + nearby resources) before ranging far afield. */
static bool ai_find_hidden_explore_target(GameState *gs, int scout_tx, int scout_ty,
                                          int *out_tx, int *out_ty){
    int best_score = 0x7fffffff;
    bool found = false;

    /* Get TC position to bias exploration toward own base first */
    int tcx = MAP_W / 2, tcy = MAP_H / 2;
    ai_find_tc_center(gs, &tcx, &tcy);

    for(int y=0; y<MAP_H; y++){
        for(int x=0; x<MAP_W; x++){
            Tile *t = &gs->map[y][x];
            if(t->fog[AI_PLAYER] != FOG_HIDDEN) continue;

            int move_tx = x;
            int move_ty = y;
            if(!map_find_passable_near(gs, x, y, &move_tx, &move_ty)) continue;

            int dist_scout = abs(move_tx - scout_tx) + abs(move_ty - scout_ty);
            if(dist_scout < 4) continue;

            /* Distance from TC: tiles close to TC get a big bonus so the scout
             * sweeps around the base and nearby resources before going far. */
            int dist_tc = abs(x - tcx) + abs(y - tcy);

            /* Bonus for resource tiles – scout wants to find wood/food/gold */
            int res_bonus = 0;
            if(t->type == TILE_FOREST || t->type == TILE_BERRIES ||
               t->type == TILE_GOLD   || t->type == TILE_STONE)
                res_bonus = -12;  /* negative = lower score = higher priority */

            /* Within 20 tiles of TC: strongly prefer these tiles first */
            int proximity_bias = (dist_tc <= 20) ? dist_tc * 2 : dist_tc * 5;

            int score = proximity_bias + dist_scout + res_bonus;
            if(score < best_score){
                best_score = score;
                *out_tx = move_tx;
                *out_ty = move_ty;
                found = true;
            }
        }
    }

    return found;
}

static bool ai_find_roaming_target(GameState *gs, int scout_tx, int scout_ty,
                                   int *out_tx, int *out_ty){
    static const int LANDMARKS[][2] = {
        {2, 2}, {MAP_W - 3, 2}, {MAP_W - 3, MAP_H - 3}, {2, MAP_H - 3},
        {MAP_W / 2, 2}, {MAP_W - 3, MAP_H / 2}, {MAP_W / 2, MAP_H - 3}, {2, MAP_H / 2},
        {MAP_W / 2, MAP_H / 2}
    };

    int start = ((int)(gs->game_time / 6.0f) + scout_tx + scout_ty) % (int)(sizeof(LANDMARKS) / sizeof(LANDMARKS[0]));
    int best_dist = -1;
    bool found = false;

    for(int i=0; i<(int)(sizeof(LANDMARKS) / sizeof(LANDMARKS[0])); i++){
        int idx = (start + i) % (int)(sizeof(LANDMARKS) / sizeof(LANDMARKS[0]));
        int move_tx = LANDMARKS[idx][0];
        int move_ty = LANDMARKS[idx][1];
        if(!map_find_passable_near(gs, move_tx, move_ty, &move_tx, &move_ty)) continue;

        int dist = abs(move_tx - scout_tx) + abs(move_ty - scout_ty);
        if(dist > best_dist){
            best_dist = dist;
            *out_tx = move_tx;
            *out_ty = move_ty;
            found = true;
        }
    }

    return found;
}

static int ai_find_builder_villager(GameState *gs, int target_tx, int target_ty){
    int best_idle = -1;
    int best_idle_dist = 0x7fffffff;
    int best_worker = -1;
    int best_worker_dist = 0x7fffffff;

    for(int i=0; i<MAX_UNITS; i++){
        Unit *u = &gs->units[i];
        if(!u->active || u->player != AI_PLAYER || u->type != UNIT_VILLAGER) continue;
        if(u->state == US_DEAD || u->state == US_DYING || u->state == US_BUILDING) continue;

        int utx = (int)(u->wx / TILE_SIZE);
        int uty = (int)(u->wy / TILE_SIZE);
        int dist = abs(utx - target_tx) + abs(uty - target_ty);

        if(u->state == US_IDLE){
            if(dist < best_idle_dist){
                best_idle_dist = dist;
                best_idle = i;
            }
            continue;
        }

        if(u->build_id >= 0) continue;
        if(dist < best_worker_dist){
            best_worker_dist = dist;
            best_worker = i;
        }
    }

    return (best_idle >= 0) ? best_idle : best_worker;
}

static bool ai_try_build_near_resource(GameState *gs, BldType type, ResType rt){
    int tcx, tcy;
    if(!ai_find_tc_center(gs, &tcx, &tcy)) return false;

    int rx, ry;
    if(!map_find_resource(gs, AI_PLAYER, rt, tcx, tcy, &rx, &ry)) return false;

    int w = building_tw(type), h = building_th(type);
    Cost c = building_cost(type);
    if(!res_can_afford(&gs->res[AI_PLAYER], c)) return false;

    int best_tx = -1, best_ty = -1;
    int best_score = 999999;
    for(int r=2; r<=6; r++){
        for(int dy=-r; dy<=r; dy++){
            for(int dx=-r; dx<=r; dx++){
                if(abs(dx) != r && abs(dy) != r) continue;
                int tx = rx + dx - w / 2;
                int ty = ry + dy - h / 2;
                if(!map_in_bounds(tx, ty) || !map_in_bounds(tx + w - 1, ty + h - 1)) continue;
                if(!map_is_buildable(gs, tx, ty, w, h)) continue;

                int score = abs((tx + w / 2) - tcx) + abs((ty + h / 2) - tcy);
                if(score < best_score){
                    best_score = score;
                    best_tx = tx;
                    best_ty = ty;
                }
            }
        }
        if(best_tx >= 0) break;
    }

    if(best_tx < 0) return false;
    int vid = ai_find_builder_villager(gs, best_tx + w / 2, best_ty + h / 2);
    if(vid < 0) return false;

    int bid = building_place(gs, AI_PLAYER, type, best_tx, best_ty);
    if(bid < 0) return false;
    unit_give_build_order(gs, &gs->units[vid], bid);
    return true;
}

static int ai_count_units(GameState *gs, UnitType type){
    int count = 0;
    for(int i=0; i<MAX_UNITS; i++){
        Unit *u = &gs->units[i];
        if(!u->active || u->player != AI_PLAYER || u->state == US_DEAD || u->state == US_DYING) continue;
        if(type >= 0 && u->type != type) continue;
        count++;
    }
    return count;
}

static int ai_count_buildings(GameState *gs, BldType type, bool complete_only){
    int count = 0;
    for(int i=0; i<MAX_BUILDINGS; i++){
        Building *b = &gs->buildings[i];
        if(!b->active || b->player != AI_PLAYER || b->type != type) continue;
        if(complete_only && !b->complete) continue;
        count++;
    }
    return count;
}

static int ai_count_total_queued_population(GameState *gs){
    int queued = 0;
    for(int i=0; i<MAX_BUILDINGS; i++){
        Building *b = &gs->buildings[i];
        if(!b->active || b->player != AI_PLAYER) continue;
        queued += building_queued_population(b);
    }
    return queued;
}

static int ai_count_queued_at_building(GameState *gs, BldType type){
    int count = 0;
    for(int i=0; i<MAX_BUILDINGS; i++){
        Building *b = &gs->buildings[i];
        if(!b->active || b->player != AI_PLAYER || b->type != type) continue;
        count += b->queue_len;
    }
    return count;
}

static int ai_total_unit_count(GameState *gs, UnitType type){
    return ai_count_units(gs, type) + ai_count_queued_units(gs, type);
}

static int ai_count_resource_workers(GameState *gs, ResType rt){
    int count = 0;
    for(int i=0; i<MAX_UNITS; i++){
        Unit *u = &gs->units[i];
        if(!u->active || u->player != AI_PLAYER || u->type != UNIT_VILLAGER) continue;
        if(u->state != US_GATHERING && u->state != US_RETURNING) continue;
        if(u->carry_type == rt) count++;
    }
    return count;
}

static int ai_count_tiles_with_resource(GameState *gs, TileType type){
    int count = 0;
    for(int y=0; y<MAP_H; y++){
        for(int x=0; x<MAP_W; x++){
            Tile *t = &gs->map[y][x];
            if(t->type == type && t->resource_amt > 0) count++;
        }
    }
    return count;
}

static int ai_count_infantry(GameState *gs){
    return ai_count_units(gs, UNIT_MILITIA) +
           ai_count_units(gs, UNIT_MAN_AT_ARMS) +
           ai_count_units(gs, UNIT_SPEARMAN);
}

static int ai_count_archers(GameState *gs){
    return ai_count_units(gs, UNIT_ARCHER) +
           ai_count_units(gs, UNIT_SKIRMISHER) +
           ai_count_units(gs, UNIT_CAVALRY_ARCHER);
}

static int ai_count_cavalry(GameState *gs){
    return ai_count_units(gs, UNIT_KNIGHT);
}

static int ai_count_siege(GameState *gs){
    return ai_count_units(gs, UNIT_BATTERING_RAM) +
           ai_count_units(gs, UNIT_MANGONEL) +
           ai_count_units(gs, UNIT_SCORPION) +
           ai_count_units(gs, UNIT_BOMBARD_CANNON);
}

static bool ai_should_save_for_age(const PlayerRes *pr){
    if(pr->advancing || pr->age >= 3) return false;
    switch(pr->age){
        case 0: return pr->amount[RES_FOOD] >= 320;   /* saving for 400F Feudal */
        case 1: return pr->amount[RES_FOOD] >= 400 || pr->amount[RES_WOOD] >= 80;
        case 2: return pr->amount[RES_FOOD] >= 480 || pr->amount[RES_GOLD] >= 300;
        default: return false;
    }
}

static bool ai_is_early_feudal_econ_focus(GameState *gs){
    PlayerRes *pr = &gs->res[AI_PLAYER];
    if(pr->age != 1 || pr->advancing) return false;
    return ai_total_unit_count(gs, UNIT_VILLAGER) < ai_feudal_econ_villager_target(gs);
}

/* Dark Age worker distribution – more food+wood for the longer econ build,
 * delay gold/stone until we have a comfortable worker base. */
static void ai_dark_age_targets(GameState *gs, int desired[RES_COUNT]){
    int villagers = ai_count_units(gs, UNIT_VILLAGER);
    /* Ramp food workers fast (berries then farms), wood workers second */
    desired[RES_FOOD]  = clampi(villagers,       0, 8);
    desired[RES_WOOD]  = clampi(villagers - 4,   0, 8);
    desired[RES_GOLD]  = clampi(villagers - 14,  0, 3);
    desired[RES_STONE] = clampi(villagers - 18,  0, 2);
}

static void ai_auto_assign_villagers(GameState *gs){
    PlayerRes *pr = &gs->res[AI_PLAYER];
    int desired[RES_COUNT] = {6, 4, 1, 0};
    int current[RES_COUNT] = {
        ai_count_resource_workers(gs, RES_FOOD),
        ai_count_resource_workers(gs, RES_WOOD),
        ai_count_resource_workers(gs, RES_GOLD),
        ai_count_resource_workers(gs, RES_STONE)
    };

    if(pr->age == 0) ai_dark_age_targets(gs, desired);
    if(pr->age >= 1){
        desired[RES_FOOD] += 2;
        desired[RES_WOOD] += 1;
        desired[RES_GOLD] = 3;
    }
    if(pr->age >= 2){
        desired[RES_FOOD] += 2;
        desired[RES_WOOD] += 1;
        desired[RES_GOLD] = 5;
        desired[RES_STONE] = 1;
    }
    if(pr->age >= 3){
        desired[RES_FOOD] += 2;
        desired[RES_GOLD] = 7;
        desired[RES_STONE] = 2;
    }

    if(pr->age >= 1 && pr->age < 3){
        Cost age_cost = age_advance_cost(pr->age);
        if(pr->amount[RES_FOOD] < age_cost.food) desired[RES_FOOD] += 2;
        if(pr->amount[RES_WOOD] < age_cost.wood) desired[RES_WOOD] += 2;
        if(pr->amount[RES_GOLD] < age_cost.gold) desired[RES_GOLD] += 3;
    }

    if(pr->age >= 1){
        if(ai_count_buildings(gs, BLD_HOUSE, false) < 4) desired[RES_WOOD] += 1;
        if(ai_count_buildings(gs, BLD_FARM, false) < 2 && pr->amount[RES_FOOD] < 250) desired[RES_FOOD] += 1;
    }

    for(int i=0; i<MAX_UNITS; i++){
        Unit *u = &gs->units[i];
        if(!u->active || u->player != AI_PLAYER || u->type != UNIT_VILLAGER) continue;

        /* Detect stuck gatherers: in GATHERING state but far from their target.
         * This happens when all adjacent tiles were blocked in unit_do_gather.
         * Force idle here so we can reassign them below. */
        if(u->state == US_GATHERING && u->gather_tx >= 0){
            int utx2 = (int)(u->wx / TILE_SIZE);
            int uty2 = (int)(u->wy / TILE_SIZE);
            if(abs(utx2 - u->gather_tx) > 2 || abs(uty2 - u->gather_ty) > 2){
                u->gather_tx = -1; u->gather_ty = -1;
                u->state = US_IDLE;
            }
        }

        if(u->state != US_IDLE) continue;

        /* Pick the most under-served resource type */
        int best_rt = RES_FOOD;
        int best_need = -999;
        for(int rt=0; rt<RES_COUNT; rt++){
            int need = desired[rt] - current[rt];
            if(need > best_need){
                best_need = need;
                best_rt = rt;
            }
        }

        /* Try resources in priority order: best → food → wood → gold → stone.
         * Track which one actually succeeded so current[] is updated correctly. */
        static const ResType fallback_order[] = {RES_FOOD, RES_WOOD, RES_GOLD, RES_STONE};
        bool assigned = false;

        /* Try the preferred resource first */
        if(ai_send_villager_gather(gs, i, (ResType)best_rt)){
            current[best_rt]++;
            assigned = true;
        } else {
            /* Fallback: try each standard resource in order */
            for(int fi = 0; fi < (int)(sizeof(fallback_order)/sizeof(fallback_order[0])); fi++){
                ResType rt2 = fallback_order[fi];
                if(rt2 == (ResType)best_rt) continue; /* already tried */
                if(ai_send_villager_gather(gs, i, rt2)){
                    current[rt2]++;
                    assigned = true;
                    break;
                }
            }
        }
        (void)assigned; /* suppress unused-variable warning */
    }
}

static void ai_manage_foundations(GameState *gs){
    for(int i=0; i<MAX_BUILDINGS; i++){
        Building *b = &gs->buildings[i];
        if(!b->active || b->player != AI_PLAYER || b->complete) continue;

        bool has_builder = false;
        for(int j=0; j<MAX_UNITS; j++){
            Unit *u = &gs->units[j];
            if(!u->active || u->player != AI_PLAYER || u->type != UNIT_VILLAGER) continue;
            if(u->build_id == b->id && u->state != US_DEAD && u->state != US_DYING){
                has_builder = true;
                break;
            }
        }
        if(has_builder) continue;

        int vid = ai_find_builder_villager(gs, b->tx + b->tw / 2, b->ty + b->th / 2);
        if(vid >= 0) unit_give_build_order(gs, &gs->units[vid], b->id);
    }
}

static void ai_manage_scout_exploration(GameState *gs){
    int scout_id = ai_find_scout(gs);
    if(scout_id < 0) return;

    Unit *scout = &gs->units[scout_id];

    /* Trigger a new move when the scout becomes idle OR when its current
     * path is fully consumed (so it never stands still at a waypoint). */
    bool needs_move = (scout->state == US_IDLE) ||
                      (scout->state == US_MOVING && scout->path_idx >= scout->path_len);
    if(!needs_move) return;

    int tx, ty;
    int scout_tx = (int)(scout->wx / TILE_SIZE);
    int scout_ty = (int)(scout->wy / TILE_SIZE);

    if(ai_find_hidden_explore_target(gs, scout_tx, scout_ty, &tx, &ty) ||
       ai_find_roaming_target(gs, scout_tx, scout_ty, &tx, &ty)){
        unit_give_move_order(gs, scout, tx, ty);
    }
}

static bool ai_try_build(GameState *gs, BldType type){
    int tc = building_find(gs, AI_PLAYER, BLD_TOWN_CENTER, true);
    if(tc < 0) return false;

    Building *town = &gs->buildings[tc];
    int w = building_tw(type), h = building_th(type);
    Cost c = building_cost(type);
    if(!res_can_afford(&gs->res[AI_PLAYER], c)) return false;

    for(int r=5; r<20; r++){
        for(int attempt=0; attempt<24; attempt++){
            int tx = town->tx + rng_range(-r, r);
            int ty = town->ty + rng_range(-r, r);
            if(!map_in_bounds(tx, ty) || !map_in_bounds(tx + w - 1, ty + h - 1)) continue;
            if(!map_is_buildable(gs, tx, ty, w, h)) continue;
            int vid = ai_find_builder_villager(gs, tx + w / 2, ty + h / 2);
            if(vid < 0) continue;
            int bid = building_place(gs, AI_PLAYER, type, tx, ty);
            if(bid < 0) continue;
            unit_give_build_order(gs, &gs->units[vid], bid);
            return true;
        }
    }
    return false;
}

static bool ai_train_unit(GameState *gs, BldType bld_type, UnitType ut){
    for(int i=0; i<MAX_BUILDINGS; i++){
        Building *b = &gs->buildings[i];
        if(!b->active || b->player != AI_PLAYER || b->type != bld_type || !b->complete) continue;
        if(b->queue_len >= BQUEUE_CAP) continue;
        if(gs->res[AI_PLAYER].age < unit_age_required(ut)) continue;
        building_enqueue_unit(gs, b, ut);
        return true;
    }
    return false;
}

static bool ai_try_research(GameState *gs, BldType bld_type, TechType tech){
    PlayerRes *pr = &gs->res[AI_PLAYER];
    if(pr->tech_unlocked[tech]) return false;
    if(pr->age < tech_age_required(tech)) return false;
    if(!res_can_afford(pr, tech_cost(tech))) return false;

    for(int i=0; i<MAX_BUILDINGS; i++){
        Building *b = &gs->buildings[i];
        if(!b->active || !b->complete || b->player != AI_PLAYER || b->type != bld_type) continue;
        if(b->active_tech != TECH_NONE || b->queue_len > 0) continue;
        building_start_tech(gs, b, tech);
        return b->active_tech == tech;
    }
    return false;
}

static void ai_manage_housing(GameState *gs){
    PlayerRes *pr = &gs->res[AI_PLAYER];
    int free_pop = pr->pop_cap - (pr->population + ai_count_total_queued_population(gs));
    int pending_houses = ai_count_buildings(gs, BLD_HOUSE, false) - ai_count_buildings(gs, BLD_HOUSE, true);
    /* Just-in-time housing: build when headroom drops below threshold.
     * Larger headroom so we never stall villager training waiting for a house.
     * Dark Age: trigger at 4 free pop so we stay comfortably ahead.  Feudal+: 4. */
    int threshold = 4;
    /* Allow 2 pending houses always – prevents pop-cap deadlock */
    int max_pending = 2;
    if(free_pop <= threshold && pending_houses < max_pending && pr->amount[RES_WOOD] >= 25)
        ai_try_build(gs, BLD_HOUSE);
}

static void ai_manage_economy_buildings(GameState *gs){
    PlayerRes *pr = &gs->res[AI_PLAYER];
    int villagers = ai_count_units(gs, UNIT_VILLAGER);
    bool early_feudal_econ = ai_is_early_feudal_econ_focus(gs);
    bool berries_exhausted = ai_count_tiles_with_resource(gs, TILE_BERRIES) == 0;

    /* Dark Age economy order:
     *   1. Lumber Camp (wood is needed for every building)  – build ASAP
     *   2. Mill (food economy near berries/farms)           – second priority
     *   3. Mining Camp (gold workers)                       – after LC is up
     */
    if(pr->age == 0 &&
       ai_count_buildings(gs, BLD_LUMBER_CAMP, false) < 1 &&
       pr->amount[RES_WOOD] >= 100)
        ai_try_build_near_resource(gs, BLD_LUMBER_CAMP, RES_WOOD);

    if(pr->age == 0 &&
       ai_count_buildings(gs, BLD_LUMBER_CAMP, false) > 0 &&
       ai_count_buildings(gs, BLD_MILL, false) < 1 &&
       pr->amount[RES_WOOD] >= 100)
        ai_try_build_near_resource(gs, BLD_MILL, RES_FOOD);

    if(pr->age == 0 &&
       villagers >= 10 &&
       ai_count_buildings(gs, BLD_LUMBER_CAMP, true) > 0 &&
       ai_count_buildings(gs, BLD_MINING_CAMP, false) < 1 &&
       pr->amount[RES_WOOD] >= 100)
        ai_try_build_near_resource(gs, BLD_MINING_CAMP, RES_GOLD);
    if(pr->age >= 1 && ai_count_buildings(gs, BLD_MINING_CAMP, false) < 1 && pr->amount[RES_WOOD] >= 100)
        ai_try_build_near_resource(gs, BLD_MINING_CAMP, RES_GOLD);

    if(pr->age == 0 &&
       berries_exhausted &&
       ai_count_buildings(gs, BLD_MILL, true) > 0 &&
       pr->amount[RES_WOOD] >= 60 &&
       ai_count_buildings(gs, BLD_FARM, false) < 4){
        ai_try_build(gs, BLD_FARM);
    } else if(pr->age >= 1 &&
              ai_count_buildings(gs, BLD_MILL, true) > 0 &&
              villagers >= 12 &&
              pr->amount[RES_WOOD] >= 60 &&
              ai_count_buildings(gs, BLD_FARM, false) < (early_feudal_econ ? 4 : (1 + pr->age * 2)) &&
              pr->amount[RES_FOOD] < (early_feudal_econ ? 325 : 250)){
        ai_try_build(gs, BLD_FARM);
    }
}

static void ai_manage_military_buildings(GameState *gs){
    PlayerRes *pr = &gs->res[AI_PLAYER];
    int military = unit_count_military(gs, AI_PLAYER);
    int villagers = ai_count_units(gs, UNIT_VILLAGER);
    bool early_feudal_econ = ai_is_early_feudal_econ_focus(gs);

    /* Build Barracks in the Dark Age once Lumber Camp is complete and we have
     * enough villagers (≥18) — required as prereq to advance to Feudal Age.
     * This ensures the AI_PLAYER builds it before trying to age-advance. */
    if(ai_count_buildings(gs, BLD_BARRACKS, false) < 1 &&
       pr->amount[RES_WOOD] >= 175 &&
       (pr->age >= 1 ||
        (villagers >= 18 &&
         ai_count_buildings(gs, BLD_LUMBER_CAMP, true) > 0)))
        ai_try_build(gs, BLD_BARRACKS);
    if(early_feudal_econ) return;

    if(pr->age >= 1 && ai_count_buildings(gs, BLD_ARCHERY_RANGE, false) < 1 && pr->amount[RES_WOOD] >= 175)
        ai_try_build(gs, BLD_ARCHERY_RANGE);
    if(pr->age >= 1 && ai_count_buildings(gs, BLD_STABLE, false) < 1 && pr->amount[RES_WOOD] >= 175)
        ai_try_build(gs, BLD_STABLE);
    if(pr->age >= 1 && ai_count_buildings(gs, BLD_BLACKSMITH, false) < 1 && pr->amount[RES_WOOD] >= 150)
        ai_try_build(gs, BLD_BLACKSMITH);
    if(pr->age >= 1 && ai_count_buildings(gs, BLD_MARKET, false) < 1 && pr->amount[RES_WOOD] >= 175)
        ai_try_build(gs, BLD_MARKET);

    if(pr->age >= 2 && ai_count_buildings(gs, BLD_MONASTERY, false) < 1 && pr->amount[RES_WOOD] >= 175)
        ai_try_build(gs, BLD_MONASTERY);
    if(pr->age >= 2 && ai_count_buildings(gs, BLD_SIEGE_WORKSHOP, false) < 1 && pr->amount[RES_WOOD] >= 200)
        ai_try_build(gs, BLD_SIEGE_WORKSHOP);
    if(pr->age >= 2 && ai_count_buildings(gs, BLD_UNIVERSITY, false) < 1 && pr->amount[RES_WOOD] >= 200)
        ai_try_build(gs, BLD_UNIVERSITY);
    if(pr->age >= 2 && ai_count_buildings(gs, BLD_WATCH_TOWER, false) < 1 &&
       pr->amount[RES_WOOD] >= 125 && pr->amount[RES_STONE] >= 125)
        ai_try_build(gs, BLD_WATCH_TOWER);

    if(military >= 8 && pr->age >= 1 && ai_count_buildings(gs, BLD_ARCHERY_RANGE, false) < 2 &&
       pr->amount[RES_WOOD] >= 175)
        ai_try_build(gs, BLD_ARCHERY_RANGE);
    if(military >= 10 && pr->age >= 2 && ai_count_buildings(gs, BLD_STABLE, false) < 2 &&
       pr->amount[RES_WOOD] >= 175)
        ai_try_build(gs, BLD_STABLE);
    if(military >= 12 && ai_count_buildings(gs, BLD_BARRACKS, false) < 2 &&
       pr->amount[RES_WOOD] >= 175)
        ai_try_build(gs, BLD_BARRACKS);
}

static void ai_manage_research(GameState *gs){
    PlayerRes *pr = &gs->res[AI_PLAYER];
    int villagers = ai_count_units(gs, UNIT_VILLAGER);
    int total_villagers = ai_total_unit_count(gs, UNIT_VILLAGER);
    int infantry = ai_count_infantry(gs);
    int archers = ai_count_archers(gs);
    int cavalry = ai_count_cavalry(gs);
    int siege = ai_count_siege(gs);
    int military = unit_count_military(gs, AI_PLAYER);
    bool early_feudal_econ = ai_is_early_feudal_econ_focus(gs);

    if(pr->age >= 1 && ai_try_research(gs, BLD_TOWN_CENTER, TECH_LOOM)) return;
    if(pr->age >= 1 && villagers >= 10 && ai_try_research(gs, BLD_TOWN_CENTER, TECH_WHEELBARROW)) return;
    if(pr->age >= 3 && villagers >= 16 && ai_try_research(gs, BLD_TOWN_CENTER, TECH_HAND_CART)) return;

    if(pr->age >= 1 && ai_try_research(gs, BLD_LUMBER_CAMP, TECH_DOUBLE_BIT_AXE)) return;
    if(early_feudal_econ && ai_try_research(gs, BLD_MILL, TECH_HAND_MILL)) return;
    if(early_feudal_econ) return;

    if(pr->age >= 2 && ai_try_research(gs, BLD_LUMBER_CAMP, TECH_BOW_SAW)) return;
    if(pr->age >= 3 && ai_try_research(gs, BLD_LUMBER_CAMP, TECH_TWO_MAN_SAW)) return;

    if(pr->age >= 1 && total_villagers >= ai_dark_age_villager_target(gs) && ai_try_research(gs, BLD_MILL, TECH_HAND_MILL)) return;
    if(pr->age >= 2 && ai_try_research(gs, BLD_MILL, TECH_CROP_ROTATION)) return;
    if(pr->age >= 2 && ai_try_research(gs, BLD_MILL, TECH_GRANARY_BASKETS)) return;

    if(infantry >= 3){
        if(ai_try_research(gs, BLD_BARRACKS, TECH_IRON_WEAPONRY)) return;
        if(pr->age >= 2 && ai_try_research(gs, BLD_BARRACKS, TECH_SQUIRES)) return;
        if(pr->age >= 2 && ai_try_research(gs, BLD_BARRACKS, TECH_CHAIN_MAIL)) return;
    }

    if(archers >= 3){
        if(ai_try_research(gs, BLD_ARCHERY_RANGE, TECH_COMPOSITE_BOWS)) return;
        if(ai_try_research(gs, BLD_ARCHERY_RANGE, TECH_THUMB_RING)) return;
        if(pr->age >= 2 && ai_try_research(gs, BLD_ARCHERY_RANGE, TECH_EAGLE_EYE)) return;
    }

    if(cavalry >= 2){
        if(ai_try_research(gs, BLD_STABLE, TECH_HUSBANDRY)) return;
        if(ai_try_research(gs, BLD_STABLE, TECH_MOUNTED_ARMOR)) return;
        if(pr->age >= 2 && ai_try_research(gs, BLD_STABLE, TECH_BLOODLINES)) return;
        if(pr->age >= 2 && ai_try_research(gs, BLD_STABLE, TECH_CAVALRY_DRILL)) return;
    }

    if(military >= 4){
        if(ai_try_research(gs, BLD_BLACKSMITH, TECH_SCALE_ARMOR)) return;
        if((infantry + cavalry) >= 4 && pr->age >= 2 && ai_try_research(gs, BLD_BLACKSMITH, TECH_BLAST_FURNACE)) return;
        if(pr->age >= 3 && ai_try_research(gs, BLD_BLACKSMITH, TECH_PLATE_ARMOR)) return;
        if(archers >= 3 && ai_try_research(gs, BLD_BLACKSMITH, TECH_FORGED_ARROWS)) return;
    }

    if(ai_try_research(gs, BLD_MONASTERY, TECH_SANCTITY)) return;
    if(pr->age >= 2 && ai_try_research(gs, BLD_MONASTERY, TECH_FERVOR)) return;

    if(siege >= 1){
        if(ai_try_research(gs, BLD_SIEGE_WORKSHOP, TECH_SIEGE_ENGINEERS)) return;
        if(pr->age >= 2 && ai_try_research(gs, BLD_SIEGE_WORKSHOP, TECH_REINFORCED_RAM)) return;
        if(pr->age >= 3 && ai_try_research(gs, BLD_SIEGE_WORKSHOP, TECH_DRILL_CREW)) return;
    }

    if(ai_try_research(gs, BLD_UNIVERSITY, TECH_MASONRY)) return;
    if(ai_try_research(gs, BLD_UNIVERSITY, TECH_TREADMILL_CRANE)) return;
    if(pr->age >= 3 && ai_try_research(gs, BLD_UNIVERSITY, TECH_ARCHITECTURE)) return;
    if(pr->age >= 3 && ai_count_buildings(gs, BLD_SIEGE_WORKSHOP, true) > 0 &&
       ai_try_research(gs, BLD_UNIVERSITY, TECH_CANNON_EMPLACEMENTS)) return;
}

static void ai_manage_training(GameState *gs){
    PlayerRes *pr = &gs->res[AI_PLAYER];
    bool save_for_age = (pr->age == 0) ? false : ai_should_save_for_age(pr);
    int villagers = ai_count_units(gs, UNIT_VILLAGER);
    int total_villagers = ai_total_unit_count(gs, UNIT_VILLAGER);
    int infantry = ai_count_infantry(gs);
    int archers = ai_count_archers(gs);
    int cavalry = ai_count_cavalry(gs);
    int monks = ai_count_units(gs, UNIT_MONK);
    int siege = ai_count_siege(gs);
    int military = unit_count_military(gs, AI_PLAYER);
    int villager_target = (pr->age == 0) ? ai_dark_age_villager_target(gs) :
                          (pr->age == 1) ? ai_feudal_econ_villager_target(gs) :
                          (pr->age == 2) ? 30 : 34;
    bool dark_age_feudal_save = (pr->age == 0) &&
                                (total_villagers >= villager_target ||
                                 (total_villagers >= villager_target - 1 &&
                                  ai_count_buildings(gs, BLD_BARRACKS, true) > 0 &&
                                  pr->amount[RES_FOOD] >= 425));
    bool early_feudal_econ = ai_is_early_feudal_econ_focus(gs);
    int tc_queue = ai_count_queued_at_building(gs, BLD_TOWN_CENTER);
    int max_tc_queue = (pr->age == 0) ? 2 : (early_feudal_econ ? 2 : 3);

    if(total_villagers < villager_target &&
       pr->population + ai_count_total_queued_population(gs) < pr->pop_cap - 1 &&
       tc_queue < max_tc_queue &&
       (!(save_for_age || dark_age_feudal_save) || villagers < 8)){
        ai_train_unit(gs, BLD_TOWN_CENTER, UNIT_VILLAGER);
    }

    if(early_feudal_econ) return;

    if(pr->age >= 2){
        if(cavalry < 6 && (!save_for_age || military < 6))
            ai_train_unit(gs, BLD_STABLE, UNIT_KNIGHT);
        if(archers < 4 && !save_for_age)
            ai_train_unit(gs, BLD_ARCHERY_RANGE, UNIT_ARCHER);
        if(monks < 2 && military >= 6 && !save_for_age)
            ai_train_unit(gs, BLD_MONASTERY, UNIT_MONK);
        if(siege < 2 && military >= 6 && !save_for_age){
            if(pr->tech_unlocked[TECH_CANNON_EMPLACEMENTS] && pr->age >= 3)
                ai_train_unit(gs, BLD_SIEGE_WORKSHOP, UNIT_BOMBARD_CANNON);
            if(ai_count_units(gs, UNIT_MANGONEL) < 2)
                ai_train_unit(gs, BLD_SIEGE_WORKSHOP, UNIT_MANGONEL);
            if(ai_count_units(gs, UNIT_SCORPION) < 3)
                ai_train_unit(gs, BLD_SIEGE_WORKSHOP, UNIT_SCORPION);
            ai_train_unit(gs, BLD_SIEGE_WORKSHOP, UNIT_BATTERING_RAM);
        }
    }

    if(pr->age >= 1){
        if(archers < 4 && (!save_for_age || military < 4))
            ai_train_unit(gs, BLD_ARCHERY_RANGE, UNIT_ARCHER);
        if(infantry < 4 && (!save_for_age || military < 4))
            ai_train_unit(gs, BLD_BARRACKS, UNIT_SPEARMAN);
        if(infantry < 6 && !save_for_age)
            ai_train_unit(gs, BLD_BARRACKS, pr->age >= 1 ? UNIT_MAN_AT_ARMS : UNIT_MILITIA);
        if(archers < 3 && !save_for_age)
            ai_train_unit(gs, BLD_ARCHERY_RANGE, UNIT_SKIRMISHER);
    } else if(pr->age == 0){
        return;
    } else if(!save_for_age || military < 3){
        ai_train_unit(gs, BLD_BARRACKS, UNIT_MILITIA);
    }
}

static int ai_find_enemy_building(GameState *gs){
    int best_id = -1;
    float best_dist = 1e30f;
    int tc = building_find(gs, AI_PLAYER, BLD_TOWN_CENTER, true);
    float ax = 32.0f * TILE_SIZE;
    float ay = 32.0f * TILE_SIZE;
    if(tc >= 0){
        Building *home = &gs->buildings[tc];
        ax = (home->tx + home->tw * 0.5f) * TILE_SIZE;
        ay = (home->ty + home->th * 0.5f) * TILE_SIZE;
    }

    int enemy_tc = building_find(gs, HUMAN_PLAYER, BLD_TOWN_CENTER, true);
    if(enemy_tc >= 0) return enemy_tc;

    for(int i=0; i<MAX_BUILDINGS; i++){
        Building *b = &gs->buildings[i];
        if(!b->active || !b->complete || b->player != HUMAN_PLAYER) continue;
        float bx = (b->tx + b->tw * 0.5f) * TILE_SIZE;
        float by = (b->ty + b->th * 0.5f) * TILE_SIZE;
        float d = dist2f(ax, ay, bx, by);
        if(d < best_dist){
            best_dist = d;
            best_id = i;
        }
    }
    return best_id;
}

static void ai_launch_attack(GameState *gs){
    int enemy_bld = ai_find_enemy_building(gs);

    for(int i=0; i<MAX_UNITS; i++){
        Unit *u = &gs->units[i];
        if(!u->active || u->player != AI_PLAYER) continue;
        if(u->type == UNIT_VILLAGER || u->type == UNIT_SCOUT || u->type == UNIT_MONK) continue;
        if(u->state == US_DEAD || u->state == US_DYING) continue;

        int enemy_unit = -1;
        float best = 1e30f;
        for(int j=0; j<MAX_UNITS; j++){
            Unit *t = &gs->units[j];
            if(!t->active || t->player != HUMAN_PLAYER || t->state == US_DEAD) continue;
            float d = dist2f(u->wx, u->wy, t->wx, t->wy);
            if(d < best){
                best = d;
                enemy_unit = j;
            }
        }

        if(enemy_unit >= 0 && best <= 12.0f * TILE_SIZE)
            unit_give_attack_order(gs, u, enemy_unit, -1);
        else if(enemy_bld >= 0)
            unit_give_attack_order(gs, u, -1, enemy_bld);
    }
}

/* ─── Rally / idle-unit management ───────────────────────────
 * Ensures every trained military unit always has something to do:
 *  - Below attack threshold: move to rally point near Barracks
 *  - At/above threshold:     immediately attack (don't wait for cooldown)
 * This runs every AI_PLAYER tick so newly spawned units get orders within 0.5s.
 */
static void ai_manage_idle_military(GameState *gs){
    /* Find a rally tile – prefer Barracks, fall back to Town Center */
    int rally_tx = -1, rally_ty = -1;
    int bld_id = building_find(gs, AI_PLAYER, BLD_BARRACKS, true);
    if(bld_id < 0) bld_id = building_find(gs, AI_PLAYER, BLD_TOWN_CENTER, true);
    if(bld_id >= 0){
        Building *bld = &gs->buildings[bld_id];
        /* Rally 3 tiles to the right of the building */
        rally_tx = bld->tx + bld->tw + 3;
        rally_ty = bld->ty + bld->th / 2;
        if(!map_in_bounds(rally_tx, rally_ty) || !map_is_passable(gs, rally_tx, rally_ty)){
            int ox, oy;
            if(map_find_passable_near(gs, rally_tx, rally_ty, &ox, &oy)){
                rally_tx = ox; rally_ty = oy;
            }
        }
    }

    int enemy_bld = ai_find_enemy_building(gs);
    PlayerRes *pr = &gs->res[AI_PLAYER];
    int attack_min = (pr->age == 0) ? 4 : (pr->age == 1) ? 6 : (pr->age == 2) ? 8 : 10;
    int military = unit_count_military(gs, AI_PLAYER);
    bool should_attack = (military >= attack_min);

    for(int i = 0; i < MAX_UNITS; i++){
        Unit *u = &gs->units[i];
        if(!u->active || u->player != AI_PLAYER) continue;
        if(u->type == UNIT_VILLAGER || u->type == UNIT_SCOUT || u->type == UNIT_MONK) continue;
        if(u->state == US_DEAD || u->state == US_DYING) continue;

        /* Only act on units that have nothing to do */
        bool is_idle = (u->state == US_IDLE);
        /* Also re-direct a unit that finished its path but has no attack target */
        bool path_done = (u->state == US_MOVING && u->path_idx >= u->path_len);
        if(!is_idle && !path_done) continue;

        if(should_attack){
            /* Immediately engage – find nearest enemy unit or fall back to building */
            int enemy_unit = -1;
            float best = 1e30f;
            for(int j = 0; j < MAX_UNITS; j++){
                Unit *t = &gs->units[j];
                if(!t->active || t->player != HUMAN_PLAYER || t->state == US_DEAD) continue;
                float d = dist2f(u->wx, u->wy, t->wx, t->wy);
                if(d < best){ best = d; enemy_unit = j; }
            }
            if(enemy_unit >= 0)
                unit_give_attack_order(gs, u, enemy_unit, -1);
            else if(enemy_bld >= 0)
                unit_give_attack_order(gs, u, -1, enemy_bld);
        } else if(rally_tx >= 0){
            /* Move to rally point to group up */
            int utx = (int)(u->wx / TILE_SIZE);
            int uty = (int)(u->wy / TILE_SIZE);
            /* Only issue move if unit is not already near the rally point */
            if(abs(utx - rally_tx) > 3 || abs(uty - rally_ty) > 3)
                unit_give_move_order(gs, u, rally_tx, rally_ty);
        }
    }
}

void ai_update(GameState *gs, float dt){
    gs->ai_timer += dt;
    gs->ai_attack_cd -= dt;
    if(gs->ai_timer < AI_THINK_RATE) return;
    gs->ai_timer = 0.0f;

    PlayerRes *pr = &gs->res[AI_PLAYER];
    int military = unit_count_military(gs, AI_PLAYER);
    int attack_min = (pr->age == 0) ? 4 : (pr->age == 1) ? 6 : (pr->age == 2) ? 8 : 10;

    ai_auto_assign_villagers(gs);
    ai_manage_scout_exploration(gs);
    ai_manage_foundations(gs);
    ai_manage_housing(gs);
    ai_manage_economy_buildings(gs);
    ai_manage_military_buildings(gs);
    ai_manage_research(gs);
    ai_manage_training(gs);
    ai_manage_idle_military(gs);

    if(building_find(gs, AI_PLAYER, BLD_BARRACKS, true) < 0 || pr->age == 0)
        gs->ai_phase = AI_PHASE_BUILD;
    else if(military < attack_min)
        gs->ai_phase = AI_PHASE_MILITARY;
    else
        gs->ai_phase = AI_PHASE_ATTACK;

    if(military >= attack_min && gs->ai_attack_cd <= 0.0f){
        gs->ai_attack_cd = AI_ATTACK_CD;
        gs->ai_attack_count++;
        ai_launch_attack(gs);
    }

    /* Advance to Feudal Age at ~21-23 population:
     * - Lumber Camp completed (economy foundation)
     * - Barracks completed (Feudal Age prerequisite)
     * - ~22 villagers trained  OR  pop-capped and can't grow further
     * - 400 food saved up */
    int total_vil = ai_count_units(gs, UNIT_VILLAGER) + ai_count_queued_units(gs, UNIT_VILLAGER);
    bool vil_ready = (ai_count_units(gs, UNIT_VILLAGER) >= ai_dark_age_villager_target(gs)) ||
                     (pr->pop_cap <= pr->population + 2 && total_vil >= 18);
    if(pr->age == 0 && !pr->advancing &&
       vil_ready &&
       ai_count_buildings(gs, BLD_LUMBER_CAMP, true) > 0 &&
       ai_count_buildings(gs, BLD_BARRACKS, true) > 0 &&
       pr->amount[RES_FOOD] >= 400)
        res_try_advance_age(gs, AI_PLAYER);
    /* Feudal → Castle: 500F + 100W, no strict building prereq (builds happen naturally) */
    else if(pr->age == 1 && !pr->advancing &&
            pr->amount[RES_FOOD] >= 500 && pr->amount[RES_WOOD] >= 100)
        res_try_advance_age(gs, AI_PLAYER);
    /* Castle → Imperial: 600F + 400G */
    else if(pr->age == 2 && !pr->advancing &&
            pr->amount[RES_FOOD] >= 600 && pr->amount[RES_GOLD] >= 400)
        res_try_advance_age(gs, AI_PLAYER);
}
