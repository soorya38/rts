/*=============================================================
 * resources.c  –  Resource management, age advancement, and
 *                  cost/stat lookup tables for buildings & units
 *
 * Contains all the data-driven configuration tables: costs,
 * sizes, HP values, training times, age requirements, and
 * display names for every building and unit type.
 *=============================================================*/
#include "game.h"
#include "net.h"
#include <stdio.h>

/* ── Resource operations ───────────────────────────────────── */

bool res_can_afford(PlayerRes *player_res, Cost cost)
{
    return player_res->amount[RES_FOOD]  >= cost.food  &&
           player_res->amount[RES_WOOD]  >= cost.wood  &&
           player_res->amount[RES_GOLD]  >= cost.gold  &&
           player_res->amount[RES_STONE] >= cost.stone;
}

void res_deduct(PlayerRes *player_res, Cost cost)
{
    player_res->amount[RES_FOOD]  -= cost.food;
    player_res->amount[RES_WOOD]  -= cost.wood;
    player_res->amount[RES_GOLD]  -= cost.gold;
    player_res->amount[RES_STONE] -= cost.stone;
}

void res_add(PlayerRes *player_res, ResType type, int amount)
{
    if (type < RES_COUNT) {
        player_res->amount[type] += amount;
    }
}

/* ── Age advancement ───────────────────────────────────────── */

Cost age_advance_cost(int current_age)
{
    switch (current_age) {
        case 0:  return (Cost){400,   0,   0, 0};  /* Dark → Feudal     */
        case 1:  return (Cost){500, 100,   0, 0};  /* Feudal → Castle   */
        case 2:  return (Cost){600,   0, 400, 0};  /* Castle → Imperial */
        default: return (Cost){  0,   0,   0, 0};
    }
}

bool res_try_advance_age(GameState *gs, int player)
{
    PlayerRes *pr = &gs->res[player];
    if (pr->age >= 3)    return false;
    if (pr->advancing)   return false;

    Cost cost = age_advance_cost(pr->age);
    if (!res_can_afford(pr, cost)) return false;

    res_deduct(pr, cost);
    pr->advancing    = true;
    pr->advance_timer = 20.0f - pr->age * 4.0f;   /* 20s / 16s / 12s */

    static const char *AGE_NAMES[] = {"Feudal Age", "Castle Age", "Imperial Age"};
    if (player == net_get_local_player()) {
        game_set_alert(gs, AGE_NAMES[pr->age]);
    }
    return true;
}

void res_update_age_advance(GameState *gs, float dt)
{
    for (int player = 0; player < NUM_PLAYERS; player++) {
        PlayerRes *pr = &gs->res[player];
        if (!pr->advancing) continue;
        pr->advance_timer -= dt;
        if (pr->advance_timer <= 0) {
            pr->advancing = false;
            pr->age++;
        }
    }
}

int pop_cap_from_buildings(GameState *gs, int player)
{
    static const int BASE_POP_CAP     = 5;
    static const int POP_PER_HOUSE    = 5;

    int cap = BASE_POP_CAP;
    for (int i = 0; i < MAX_BUILDINGS; i++) {
        Building *bld = &gs->buildings[i];
        if (!bld->active || !bld->complete || bld->player != player) continue;
        if (bld->type == BLD_HOUSE) {
            cap += POP_PER_HOUSE;
        }
    }
    if (cap > POP_CAP_MAX) cap = POP_CAP_MAX;
    return cap;
}

/* ── Building cost / size / HP tables ──────────────────────── */

Cost building_cost(BldType type)
{
    switch (type) {
        case BLD_TOWN_CENTER:   return (Cost){  0,   0,   0,   0};
        case BLD_HOUSE:         return (Cost){  0,  25,   0,   0};
        case BLD_BARRACKS:      return (Cost){  0, 175,   0,   0};
        case BLD_ARCHERY_RANGE: return (Cost){  0, 175,   0,   0};
        case BLD_STABLE:        return (Cost){  0, 175,   0,   0};
        case BLD_BLACKSMITH:    return (Cost){  0, 150,   0,   0};
        case BLD_MARKET:        return (Cost){  0, 175,   0,   0};
        case BLD_MILL:          return (Cost){  0, 100,   0,   0};
        case BLD_LUMBER_CAMP:   return (Cost){  0, 100,   0,   0};
        case BLD_MINING_CAMP:   return (Cost){  0, 100,   0,   0};
        case BLD_FARM:          return (Cost){  0,  60,   0,   0};
        case BLD_WATCH_TOWER:   return (Cost){  0, 125,   0, 125};
        case BLD_MONASTERY:     return (Cost){  0, 175,   0,   0};
        case BLD_SIEGE_WORKSHOP:return (Cost){  0, 200,   0,   0};
        case BLD_UNIVERSITY:    return (Cost){  0, 200,   0,   0};
        case BLD_WALL:          return (Cost){  0,  20,   0,   0};
        case BLD_GATE:          return (Cost){  0,  35,   0,  15};
        case BLD_CASTLE:        return (Cost){  0,   0,   0, 650};
        default:                return (Cost){  0,   0,   0,   0};
    }
}

int building_tw(BldType type)
{
    switch (type) {
        case BLD_TOWN_CENTER:    return 4;
        case BLD_STABLE:         return 4;
        case BLD_BARRACKS:       return 3;
        case BLD_ARCHERY_RANGE:  return 3;
        case BLD_BLACKSMITH:     return 3;
        case BLD_MARKET:         return 3;
        case BLD_FARM:           return 3;
        case BLD_HOUSE:          return 2;
        case BLD_MILL:           return 2;
        case BLD_LUMBER_CAMP:    return 2;
        case BLD_MINING_CAMP:    return 2;
        case BLD_WATCH_TOWER:    return 2;
        case BLD_MONASTERY:      return 3;
        case BLD_SIEGE_WORKSHOP: return 3;
        case BLD_UNIVERSITY:     return 3;
        case BLD_WALL:           return 1;
        case BLD_GATE:           return 1;
        case BLD_CASTLE:         return 4;
        default:                 return 2;
    }
}

/* All buildings are square. */
int building_th(BldType type)
{
    return building_tw(type);
}

int building_max_hp(BldType type)
{
    switch (type) {
        case BLD_TOWN_CENTER:    return 2400;
        case BLD_HOUSE:          return  550;
        case BLD_BARRACKS:       return 1200;
        case BLD_ARCHERY_RANGE:  return 1300;
        case BLD_STABLE:         return 1400;
        case BLD_BLACKSMITH:     return 1200;
        case BLD_MARKET:         return  800;
        case BLD_MILL:           return 1000;
        case BLD_LUMBER_CAMP:    return  600;
        case BLD_MINING_CAMP:    return  600;
        case BLD_FARM:           return  400;
        case BLD_WATCH_TOWER:    return 1020;
        case BLD_MONASTERY:      return  900;
        case BLD_SIEGE_WORKSHOP: return 1500;
        case BLD_UNIVERSITY:     return 1200;
        case BLD_WALL:           return  900;
        case BLD_GATE:           return 1100;
        case BLD_CASTLE:         return 4800;
        default:                 return  500;
    }
}

/* ── Unit cost / stat tables ───────────────────────────────── */

Cost unit_cost(UnitType type)
{
    switch (type) {
        case UNIT_VILLAGER:       return (Cost){50,   0,   0, 0};
        case UNIT_SCOUT:          return (Cost){80,   0,   0, 0};
        case UNIT_MILITIA:        return (Cost){60,   0,  20, 0};
        case UNIT_MAN_AT_ARMS:    return (Cost){60,   0,  20, 0};
        case UNIT_SPEARMAN:       return (Cost){35,  25,   0, 0};
        case UNIT_ARCHER:         return (Cost){ 0,  25,  45, 0};
        case UNIT_SKIRMISHER:     return (Cost){25,  35,   0, 0};
        case UNIT_CAVALRY_ARCHER: return (Cost){40,   0,  70, 0};
        case UNIT_KNIGHT:         return (Cost){60,   0,  75, 0};
        case UNIT_MONK:           return (Cost){ 0,   0, 100, 0};
        case UNIT_BATTERING_RAM:  return (Cost){ 0, 160,  75, 0};
        case UNIT_MANGONEL:       return (Cost){ 0, 160, 135, 0};
        case UNIT_SCORPION:       return (Cost){ 0,  75,  75, 0};
        case UNIT_BOMBARD_CANNON: return (Cost){ 0, 225, 225, 0};
        default:                  return (Cost){50,   0,   0, 0};
    }
}

float building_train_time(UnitType type)
{
    switch (type) {
        case UNIT_VILLAGER:       return 25.0f;
        case UNIT_SCOUT:          return 20.0f;
        case UNIT_MILITIA:        return 21.0f;
        case UNIT_MAN_AT_ARMS:    return 21.0f;
        case UNIT_SPEARMAN:       return 22.0f;
        case UNIT_ARCHER:         return 35.0f;
        case UNIT_SKIRMISHER:     return 22.0f;
        case UNIT_CAVALRY_ARCHER: return 34.0f;
        case UNIT_KNIGHT:         return 30.0f;
        case UNIT_MONK:           return 45.0f;
        case UNIT_BATTERING_RAM:  return 36.0f;
        case UNIT_MANGONEL:       return 46.0f;
        case UNIT_SCORPION:       return 34.0f;
        case UNIT_BOMBARD_CANNON: return 56.0f;
        default:                  return 25.0f;
    }
}

int unit_age_required(UnitType type)
{
    switch (type) {
        case UNIT_MAN_AT_ARMS:
        case UNIT_SPEARMAN:
        case UNIT_SKIRMISHER:     return 1;  /* Feudal  */
        case UNIT_KNIGHT:
        case UNIT_CAVALRY_ARCHER:
        case UNIT_MONK:
        case UNIT_BATTERING_RAM:
        case UNIT_MANGONEL:
        case UNIT_SCORPION:       return 2;  /* Castle  */
        case UNIT_BOMBARD_CANNON: return 3;  /* Imperial */
        default:                  return 0;  /* Dark Age */
    }
}

int building_age_required(BldType type)
{
    switch (type) {
        case BLD_ARCHERY_RANGE:
        case BLD_STABLE:
        case BLD_BLACKSMITH:
        case BLD_MARKET:
        case BLD_WATCH_TOWER:     return 1;  /* Feudal */
        case BLD_MONASTERY:
        case BLD_SIEGE_WORKSHOP:
        case BLD_UNIVERSITY:
        case BLD_CASTLE:          return 2;  /* Castle */
        default:                  return 0;
    }
}

bool building_can_train_unit(BldType building_type, UnitType unit_type)
{
    switch (building_type) {
        case BLD_TOWN_CENTER:
            return unit_type == UNIT_VILLAGER || unit_type == UNIT_SCOUT;
        case BLD_BARRACKS:
            return unit_type == UNIT_MILITIA   ||
                   unit_type == UNIT_MAN_AT_ARMS ||
                   unit_type == UNIT_SPEARMAN;
        case BLD_ARCHERY_RANGE:
            return unit_type == UNIT_ARCHER     ||
                   unit_type == UNIT_SKIRMISHER  ||
                   unit_type == UNIT_CAVALRY_ARCHER;
        case BLD_STABLE:
            return unit_type == UNIT_KNIGHT;
        case BLD_MONASTERY:
            return unit_type == UNIT_MONK;
        case BLD_SIEGE_WORKSHOP:
            return unit_type == UNIT_BATTERING_RAM  ||
                   unit_type == UNIT_MANGONEL       ||
                   unit_type == UNIT_SCORPION        ||
                   unit_type == UNIT_BOMBARD_CANNON;
        default:
            return false;
    }
}

/* ── Display names ─────────────────────────────────────────── */

const char *unit_name(UnitType type)
{
    switch (type) {
        case UNIT_VILLAGER:       return "Villager";
        case UNIT_SCOUT:          return "Scout";
        case UNIT_MILITIA:        return "Militia";
        case UNIT_MAN_AT_ARMS:    return "Man-at-Arms";
        case UNIT_SPEARMAN:       return "Spearman";
        case UNIT_ARCHER:         return "Archer";
        case UNIT_SKIRMISHER:     return "Skirmisher";
        case UNIT_CAVALRY_ARCHER: return "Cavalry Archer";
        case UNIT_KNIGHT:         return "Knight";
        case UNIT_MONK:           return "Monk";
        case UNIT_BATTERING_RAM:  return "Battering Ram";
        case UNIT_MANGONEL:       return "Mangonel";
        case UNIT_SCORPION:       return "Scorpion";
        case UNIT_BOMBARD_CANNON: return "Bombard Cannon";
        default:                  return "Unit";
    }
}

/* Building names use Tamil terminology for cultural flavour. */
const char *building_name(BldType type)
{
    switch (type) {
        case BLD_TOWN_CENTER:    return "Mandapam";
        case BLD_HOUSE:          return "Veedu";
        case BLD_BARRACKS:       return "Padai Veedu";
        case BLD_ARCHERY_RANGE:  return "Villalar Koodam";
        case BLD_STABLE:         return "Kuthirai Layam";
        case BLD_BLACKSMITH:     return "Kollan Pattarai";
        case BLD_MARKET:         return "Santhai";
        case BLD_MILL:           return "Aalai";
        case BLD_LUMBER_CAMP:    return "Maram Vettu Kalam";
        case BLD_MINING_CAMP:    return "Surangam";
        case BLD_FARM:           return "Vayal";
        case BLD_WATCH_TOWER:    return "Kaval Gopuram";
        case BLD_MONASTERY:      return "Kovil";
        case BLD_SIEGE_WORKSHOP: return "Por Karuvi Pattarai";
        case BLD_UNIVERSITY:     return "Kalvi Koodam";
        case BLD_WALL:           return "Mathil";
        case BLD_GATE:           return "Kottai Vaasal";
        case BLD_CASTLE:         return "Kottai";
        default:                 return "Kattidam";
    }
}
