/*=============================================================
 * ai.c  –  Enemy AI: economy, tech, army, and attack control
 *=============================================================*/
#include "game.h"

#define AI 1
#define HU 0

#define AI_GATHER   0
#define AI_BUILD    1
#define AI_MILITARY 2
#define AI_ATTACK   3

#define AI_THINK_RATE 1.5f
#define AI_ATTACK_CD  60.0f

static bool ai_send_villager_gather(GameState *gs, int uid, ResType rt){
    Unit *u = &gs->units[uid];
    int tx, ty;
    int utx = (int)(u->wx / TILE_SIZE);
    int uty = (int)(u->wy / TILE_SIZE);
    if(!map_find_resource(gs, AI, rt, utx, uty, &tx, &ty)) return false;
    unit_give_gather_order(gs, u, tx, ty);
    return true;
}

static int ai_count_units(GameState *gs, UnitType type){
    int count = 0;
    for(int i=0; i<MAX_UNITS; i++){
        Unit *u = &gs->units[i];
        if(!u->active || u->player != AI || u->state == US_DEAD) continue;
        if(type >= 0 && u->type != type) continue;
        count++;
    }
    return count;
}

static int ai_count_buildings(GameState *gs, BldType type, bool complete_only){
    int count = 0;
    for(int i=0; i<MAX_BUILDINGS; i++){
        Building *b = &gs->buildings[i];
        if(!b->active || b->player != AI || b->type != type) continue;
        if(complete_only && !b->complete) continue;
        count++;
    }
    return count;
}

static int ai_count_total_queued_population(GameState *gs){
    int queued = 0;
    for(int i=0; i<MAX_BUILDINGS; i++){
        Building *b = &gs->buildings[i];
        if(!b->active || b->player != AI) continue;
        queued += building_queued_population(b);
    }
    return queued;
}

static int ai_count_resource_workers(GameState *gs, ResType rt){
    int count = 0;
    for(int i=0; i<MAX_UNITS; i++){
        Unit *u = &gs->units[i];
        if(!u->active || u->player != AI || u->type != UNIT_VILLAGER) continue;
        if(u->state != US_GATHERING && u->state != US_RETURNING) continue;
        if(u->carry_type == rt) count++;
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
        case 0: return pr->amount[RES_FOOD] >= 325;
        case 1: return pr->amount[RES_FOOD] >= 550 || pr->amount[RES_WOOD] >= 120;
        case 2: return pr->amount[RES_FOOD] >= 700 || pr->amount[RES_GOLD] >= 500;
        default: return false;
    }
}

static void ai_auto_assign_villagers(GameState *gs){
    PlayerRes *pr = &gs->res[AI];
    int desired[RES_COUNT] = {6, 4, 1, 0};
    int current[RES_COUNT] = {
        ai_count_resource_workers(gs, RES_FOOD),
        ai_count_resource_workers(gs, RES_WOOD),
        ai_count_resource_workers(gs, RES_GOLD),
        ai_count_resource_workers(gs, RES_STONE)
    };

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

    if(pr->age < 3){
        Cost age_cost = age_advance_cost(pr->age);
        if(pr->amount[RES_FOOD] < age_cost.food) desired[RES_FOOD] += 2;
        if(pr->amount[RES_WOOD] < age_cost.wood) desired[RES_WOOD] += 2;
        if(pr->amount[RES_GOLD] < age_cost.gold) desired[RES_GOLD] += 3;
    }

    if(ai_count_buildings(gs, BLD_HOUSE, false) < 4) desired[RES_WOOD] += 1;
    if(ai_count_buildings(gs, BLD_FARM, false) < 2 && pr->amount[RES_FOOD] < 250) desired[RES_FOOD] += 1;

    for(int i=0; i<MAX_UNITS; i++){
        Unit *u = &gs->units[i];
        if(!u->active || u->player != AI || u->type != UNIT_VILLAGER) continue;
        if(u->state != US_IDLE) continue;

        int best_rt = RES_FOOD;
        int best_need = -999;
        for(int rt=0; rt<RES_COUNT; rt++){
            int need = desired[rt] - current[rt];
            if(need > best_need){
                best_need = need;
                best_rt = rt;
            }
        }

        if(!ai_send_villager_gather(gs, i, (ResType)best_rt) &&
           !ai_send_villager_gather(gs, i, RES_FOOD) &&
           !ai_send_villager_gather(gs, i, RES_WOOD) &&
           !ai_send_villager_gather(gs, i, RES_GOLD)){
            ai_send_villager_gather(gs, i, RES_STONE);
        } else {
            current[best_rt]++;
        }
    }
}

static bool ai_try_build(GameState *gs, BldType type){
    int tc = building_find(gs, AI, BLD_TOWN_CENTER, true);
    if(tc < 0) return false;

    Building *town = &gs->buildings[tc];
    int w = building_tw(type), h = building_th(type);
    Cost c = building_cost(type);
    if(!res_can_afford(&gs->res[AI], c)) return false;

    for(int r=5; r<20; r++){
        for(int attempt=0; attempt<24; attempt++){
            int tx = town->tx + rng_range(-r, r);
            int ty = town->ty + rng_range(-r, r);
            if(!map_in_bounds(tx, ty) || !map_in_bounds(tx + w - 1, ty + h - 1)) continue;
            if(!map_is_buildable(gs, tx, ty, w, h)) continue;
            int bid = building_place(gs, AI, type, tx, ty);
            if(bid < 0) continue;
            int vid = unit_find_idle_villager(gs, AI);
            if(vid >= 0) unit_give_build_order(gs, &gs->units[vid], bid);
            return true;
        }
    }
    return false;
}

static bool ai_train_unit(GameState *gs, BldType bld_type, UnitType ut){
    for(int i=0; i<MAX_BUILDINGS; i++){
        Building *b = &gs->buildings[i];
        if(!b->active || b->player != AI || b->type != bld_type || !b->complete) continue;
        if(b->queue_len >= BQUEUE_CAP) continue;
        if(gs->res[AI].age < unit_age_required(ut)) continue;
        building_enqueue_unit(gs, b, ut);
        return true;
    }
    return false;
}

static bool ai_try_research(GameState *gs, BldType bld_type, TechType tech){
    PlayerRes *pr = &gs->res[AI];
    if(pr->tech_unlocked[tech]) return false;
    if(pr->age < tech_age_required(tech)) return false;
    if(!res_can_afford(pr, tech_cost(tech))) return false;

    for(int i=0; i<MAX_BUILDINGS; i++){
        Building *b = &gs->buildings[i];
        if(!b->active || !b->complete || b->player != AI || b->type != bld_type) continue;
        if(b->active_tech != TECH_NONE || b->queue_len > 0) continue;
        building_start_tech(gs, b, tech);
        return b->active_tech == tech;
    }
    return false;
}

static void ai_manage_housing(GameState *gs){
    PlayerRes *pr = &gs->res[AI];
    int free_pop = pr->pop_cap - (pr->population + ai_count_total_queued_population(gs));
    int pending_houses = ai_count_buildings(gs, BLD_HOUSE, false) - ai_count_buildings(gs, BLD_HOUSE, true);
    if(free_pop <= 4 && pending_houses < 2 && pr->amount[RES_WOOD] >= 25)
        ai_try_build(gs, BLD_HOUSE);
}

static void ai_manage_economy_buildings(GameState *gs){
    PlayerRes *pr = &gs->res[AI];
    int villagers = ai_count_units(gs, UNIT_VILLAGER);

    if(ai_count_buildings(gs, BLD_MILL, false) < 1 && pr->amount[RES_WOOD] >= 100)
        ai_try_build(gs, BLD_MILL);
    if(ai_count_buildings(gs, BLD_LUMBER_CAMP, false) < 1 && pr->amount[RES_WOOD] >= 100)
        ai_try_build(gs, BLD_LUMBER_CAMP);
    if(pr->age >= 1 && ai_count_buildings(gs, BLD_MINING_CAMP, false) < 1 && pr->amount[RES_WOOD] >= 100)
        ai_try_build(gs, BLD_MINING_CAMP);

    if(ai_count_buildings(gs, BLD_MILL, true) > 0 &&
       villagers >= 10 &&
       pr->amount[RES_WOOD] >= 60 &&
       ai_count_buildings(gs, BLD_FARM, false) < 2 + pr->age * 2 &&
       pr->amount[RES_FOOD] < 250){
        ai_try_build(gs, BLD_FARM);
    }
}

static void ai_manage_military_buildings(GameState *gs){
    PlayerRes *pr = &gs->res[AI];
    int military = unit_count_military(gs, AI);

    if(ai_count_buildings(gs, BLD_BARRACKS, false) < 1 && pr->amount[RES_WOOD] >= 175)
        ai_try_build(gs, BLD_BARRACKS);
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
    PlayerRes *pr = &gs->res[AI];
    int villagers = ai_count_units(gs, UNIT_VILLAGER);
    int infantry = ai_count_infantry(gs);
    int archers = ai_count_archers(gs);
    int cavalry = ai_count_cavalry(gs);
    int siege = ai_count_siege(gs);
    int military = unit_count_military(gs, AI);

    if(ai_try_research(gs, BLD_TOWN_CENTER, TECH_LOOM)) return;
    if(villagers >= 8 && ai_try_research(gs, BLD_TOWN_CENTER, TECH_WHEELBARROW)) return;
    if(pr->age >= 3 && villagers >= 16 && ai_try_research(gs, BLD_TOWN_CENTER, TECH_HAND_CART)) return;

    if(ai_try_research(gs, BLD_LUMBER_CAMP, TECH_DOUBLE_BIT_AXE)) return;
    if(pr->age >= 2 && ai_try_research(gs, BLD_LUMBER_CAMP, TECH_BOW_SAW)) return;
    if(pr->age >= 3 && ai_try_research(gs, BLD_LUMBER_CAMP, TECH_TWO_MAN_SAW)) return;

    if(ai_try_research(gs, BLD_MILL, TECH_HAND_MILL)) return;
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
    PlayerRes *pr = &gs->res[AI];
    bool save_for_age = ai_should_save_for_age(pr);
    int villagers = ai_count_units(gs, UNIT_VILLAGER);
    int infantry = ai_count_infantry(gs);
    int archers = ai_count_archers(gs);
    int cavalry = ai_count_cavalry(gs);
    int monks = ai_count_units(gs, UNIT_MONK);
    int siege = ai_count_siege(gs);
    int military = unit_count_military(gs, AI);
    int villager_target = (pr->age == 0) ? 10 : (pr->age == 1) ? 16 : (pr->age == 2) ? 22 : 26;

    if(villagers < villager_target &&
       pr->population + ai_count_total_queued_population(gs) < pr->pop_cap - 1 &&
       (!save_for_age || villagers < 10)){
        ai_train_unit(gs, BLD_TOWN_CENTER, UNIT_VILLAGER);
    }

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
    } else if(!save_for_age || military < 3){
        ai_train_unit(gs, BLD_BARRACKS, UNIT_MILITIA);
    }
}

static int ai_find_enemy_building(GameState *gs){
    int best_id = -1;
    float best_dist = 1e30f;
    int tc = building_find(gs, AI, BLD_TOWN_CENTER, true);
    float ax = 32.0f * TILE_SIZE;
    float ay = 32.0f * TILE_SIZE;
    if(tc >= 0){
        Building *home = &gs->buildings[tc];
        ax = (home->tx + home->tw * 0.5f) * TILE_SIZE;
        ay = (home->ty + home->th * 0.5f) * TILE_SIZE;
    }

    int enemy_tc = building_find(gs, HU, BLD_TOWN_CENTER, true);
    if(enemy_tc >= 0) return enemy_tc;

    for(int i=0; i<MAX_BUILDINGS; i++){
        Building *b = &gs->buildings[i];
        if(!b->active || !b->complete || b->player != HU) continue;
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
        if(!u->active || u->player != AI) continue;
        if(u->type == UNIT_VILLAGER || u->type == UNIT_SCOUT || u->type == UNIT_MONK) continue;
        if(u->state == US_DEAD || u->state == US_DYING) continue;

        int enemy_unit = -1;
        float best = 1e30f;
        for(int j=0; j<MAX_UNITS; j++){
            Unit *t = &gs->units[j];
            if(!t->active || t->player != HU || t->state == US_DEAD) continue;
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

void ai_update(GameState *gs, float dt){
    gs->ai_timer += dt;
    gs->ai_attack_cd -= dt;
    if(gs->ai_timer < AI_THINK_RATE) return;
    gs->ai_timer = 0.0f;

    PlayerRes *pr = &gs->res[AI];
    int military = unit_count_military(gs, AI);
    int attack_min = (pr->age == 0) ? 4 : (pr->age == 1) ? 6 : (pr->age == 2) ? 8 : 10;

    ai_auto_assign_villagers(gs);
    ai_manage_housing(gs);
    ai_manage_economy_buildings(gs);
    ai_manage_military_buildings(gs);
    ai_manage_research(gs);
    ai_manage_training(gs);

    if(building_find(gs, AI, BLD_BARRACKS, true) < 0 || pr->age == 0)
        gs->ai_phase = AI_BUILD;
    else if(military < attack_min)
        gs->ai_phase = AI_MILITARY;
    else
        gs->ai_phase = AI_ATTACK;

    if(military >= attack_min && gs->ai_attack_cd <= 0.0f){
        gs->ai_attack_cd = AI_ATTACK_CD;
        gs->ai_attack_count++;
        ai_launch_attack(gs);
    }

    if(pr->age == 0 && !pr->advancing && pr->amount[RES_FOOD] >= 500)
        res_try_advance_age(gs, AI);
    else if(pr->age == 1 && !pr->advancing &&
            pr->amount[RES_FOOD] >= 800 && pr->amount[RES_WOOD] >= 200 &&
            ai_count_buildings(gs, BLD_BLACKSMITH, true) > 0 &&
            ai_count_buildings(gs, BLD_MARKET, true) > 0)
        res_try_advance_age(gs, AI);
    else if(pr->age == 2 && !pr->advancing &&
            pr->amount[RES_FOOD] >= 1000 && pr->amount[RES_GOLD] >= 800 &&
            ai_count_buildings(gs, BLD_UNIVERSITY, true) > 0)
        res_try_advance_age(gs, AI);
}
