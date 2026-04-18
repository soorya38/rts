/*=============================================================
 * resources.c  –  Resource management, age advancement
 *=============================================================*/
#include "game.h"
#include "net.h"
#include <stdio.h>

bool res_can_afford(PlayerRes *pr, Cost c){
    return pr->amount[RES_FOOD]  >= c.food  &&
           pr->amount[RES_WOOD]  >= c.wood  &&
           pr->amount[RES_GOLD]  >= c.gold  &&
           pr->amount[RES_STONE] >= c.stone;
}

void res_deduct(PlayerRes *pr, Cost c){
    pr->amount[RES_FOOD]  -= c.food;
    pr->amount[RES_WOOD]  -= c.wood;
    pr->amount[RES_GOLD]  -= c.gold;
    pr->amount[RES_STONE] -= c.stone;
}

void res_add(PlayerRes *pr, ResType rt, int amt){
    if(rt<RES_COUNT) pr->amount[rt] += amt;
}

Cost age_advance_cost(int age){
    switch(age){
        case 0: return (Cost){500,0,0,0};     /* Dark → Feudal */
        case 1: return (Cost){800,200,0,0};   /* Feudal → Castle */
        case 2: return (Cost){1000,0,800,0};  /* Castle → Imperial */
        default: return (Cost){0,0,0,0};
    }
}

bool res_try_advance_age(GameState *gs, int player){
    PlayerRes *pr = &gs->res[player];
    if(pr->age >= 3) return false;
    if(pr->advancing) return false;
    Cost c = age_advance_cost(pr->age);
    if(!res_can_afford(pr,c)) return false;
    res_deduct(pr,c);
    pr->advancing    = true;
    pr->advance_timer= 30.0f - pr->age*5.0f;  /* 30/25/20 seconds */
    const char *names[]={"Feudal Age","Castle Age","Imperial Age"};
    if(player == net_get_local_player()) game_set_alert(gs, names[pr->age]);
    return true;
}

void res_update_age_advance(GameState *gs, float dt){
    for(int p=0;p<NUM_PLAYERS;p++){
        PlayerRes *pr=&gs->res[p];
        if(!pr->advancing) continue;
        pr->advance_timer -= dt;
        if(pr->advance_timer<=0){
            pr->advancing=false;
            pr->age++;
        }
    }
}

int pop_cap_from_buildings(GameState *gs, int player){
    int cap=5;  /* Town Center gives 5 if present */
    int house_count=0;
    for(int i=0;i<MAX_BUILDINGS;i++){
        Building *b=&gs->buildings[i];
        if(!b->active || !b->complete || b->player!=player) continue;
        if(b->type==BLD_HOUSE){ cap+=5; house_count++; }
    }
    if(cap>POP_CAP_MAX) cap=POP_CAP_MAX;
    printf("pop_cap_from_buildings: player=%d houses=%d cap=%d\n", player, house_count, cap);
    return cap;
}

/* ─── Building cost / size tables ─────────────────────────── */

Cost building_cost(BldType t){
    switch(t){
        case BLD_TOWN_CENTER:  return (Cost){0,0,0,0};     /* cannot build, pre-placed */
        case BLD_HOUSE:        return (Cost){0,25,0,0};
        case BLD_BARRACKS:     return (Cost){0,175,0,0};
        case BLD_ARCHERY_RANGE:return (Cost){0,175,0,0};
        case BLD_STABLE:       return (Cost){0,175,0,0};
        case BLD_BLACKSMITH:   return (Cost){0,150,0,0};
        case BLD_MARKET:       return (Cost){0,175,0,0};
        case BLD_MILL:         return (Cost){0,100,0,0};
        case BLD_LUMBER_CAMP:  return (Cost){0,100,0,0};
        case BLD_MINING_CAMP:  return (Cost){0,100,0,0};
        case BLD_FARM:         return (Cost){0,60,0,0};
        case BLD_WATCH_TOWER:  return (Cost){0,125,0,125};
        case BLD_MONASTERY:    return (Cost){0,175,0,0};
        case BLD_SIEGE_WORKSHOP:return (Cost){0,200,0,0};
        case BLD_UNIVERSITY:   return (Cost){0,200,0,0};
        case BLD_WALL:         return (Cost){0,20,0,0};
        case BLD_GATE:         return (Cost){0,35,0,15};
        default:               return (Cost){0,0,0,0};
    }
}

int building_tw(BldType t){
    switch(t){
        case BLD_TOWN_CENTER: return 4;
        case BLD_STABLE:      return 4;
        case BLD_BARRACKS:    return 3;
        case BLD_ARCHERY_RANGE:return 3;
        case BLD_BLACKSMITH:  return 3;
        case BLD_MARKET:      return 3;
        case BLD_FARM:        return 3;
        case BLD_HOUSE:       return 2;
        case BLD_MILL:        return 2;
        case BLD_LUMBER_CAMP: return 2;
        case BLD_MINING_CAMP: return 2;
        case BLD_WATCH_TOWER: return 2;
        case BLD_MONASTERY:   return 3;
        case BLD_SIEGE_WORKSHOP:return 3;
        case BLD_UNIVERSITY:  return 3;
        case BLD_WALL:        return 1;
        case BLD_GATE:        return 1;
        default:              return 2;
    }
}

int building_th(BldType t){ return building_tw(t); /* all square */ }

int building_max_hp(BldType t){
    switch(t){
        case BLD_TOWN_CENTER:  return 2400;
        case BLD_HOUSE:        return 550;
        case BLD_BARRACKS:     return 1200;
        case BLD_ARCHERY_RANGE:return 1300;
        case BLD_STABLE:       return 1400;
        case BLD_BLACKSMITH:   return 1200;
        case BLD_MARKET:       return 800;
        case BLD_MILL:         return 1000;
        case BLD_LUMBER_CAMP:  return 600;
        case BLD_MINING_CAMP:  return 600;
        case BLD_FARM:         return 400;
        case BLD_WATCH_TOWER:  return 1020;
        case BLD_MONASTERY:    return 900;
        case BLD_SIEGE_WORKSHOP:return 1500;
        case BLD_UNIVERSITY:   return 1200;
        case BLD_WALL:         return 900;
        case BLD_GATE:         return 1100;
        default:               return 500;
    }
}

/* ─── Unit cost table ──────────────────────────────────────── */

Cost unit_cost(UnitType t){
    switch(t){
        case UNIT_VILLAGER:     return (Cost){50,0,0,0};
        case UNIT_SCOUT:        return (Cost){80,0,0,0};
        case UNIT_MILITIA:      return (Cost){60,0,20,0};
        case UNIT_MAN_AT_ARMS:  return (Cost){60,0,20,0};
        case UNIT_SPEARMAN:     return (Cost){35,25,0,0};
        case UNIT_ARCHER:       return (Cost){0,25,45,0};
        case UNIT_SKIRMISHER:   return (Cost){25,35,0,0};
        case UNIT_CAVALRY_ARCHER:return (Cost){40,0,70,0};
        case UNIT_KNIGHT:       return (Cost){60,0,75,0};
        case UNIT_MONK:         return (Cost){0,0,100,0};
        case UNIT_BATTERING_RAM:return (Cost){0,160,75,0};
        case UNIT_MANGONEL:     return (Cost){0,160,135,0};
        case UNIT_SCORPION:     return (Cost){0,75,75,0};
        case UNIT_BOMBARD_CANNON:return (Cost){0,225,225,0};
        default:                return (Cost){50,0,0,0};
    }
}

float building_train_time(UnitType t){
    switch(t){
        case UNIT_VILLAGER:    return 25.0f;
        case UNIT_SCOUT:       return 20.0f;
        case UNIT_MILITIA:     return 21.0f;
        case UNIT_MAN_AT_ARMS: return 21.0f;
        case UNIT_SPEARMAN:    return 22.0f;
        case UNIT_ARCHER:      return 35.0f;
        case UNIT_SKIRMISHER:  return 22.0f;
        case UNIT_CAVALRY_ARCHER:return 34.0f;
        case UNIT_KNIGHT:      return 30.0f;
        case UNIT_MONK:        return 45.0f;
        case UNIT_BATTERING_RAM:return 36.0f;
        case UNIT_MANGONEL:    return 46.0f;
        case UNIT_SCORPION:    return 34.0f;
        case UNIT_BOMBARD_CANNON:return 56.0f;
        default:               return 25.0f;
    }
}

int unit_age_required(UnitType t){
    switch(t){
        case UNIT_MAN_AT_ARMS:
        case UNIT_SPEARMAN:
        case UNIT_SKIRMISHER:
            return 1;
        case UNIT_KNIGHT:
        case UNIT_CAVALRY_ARCHER:
        case UNIT_MONK:
        case UNIT_BATTERING_RAM:
        case UNIT_MANGONEL:
        case UNIT_SCORPION:
            return 2;
        case UNIT_BOMBARD_CANNON:
            return 3;
        default:
            return 0;
    }
}

int building_age_required(BldType t){
    switch(t){
        case BLD_ARCHERY_RANGE:
        case BLD_STABLE:
        case BLD_BLACKSMITH:
        case BLD_MARKET:
        case BLD_WATCH_TOWER:
            return 1;
        case BLD_MONASTERY:
        case BLD_SIEGE_WORKSHOP:
        case BLD_UNIVERSITY:
            return 2;
        default:
            return 0;
    }
}

bool building_can_train_unit(BldType bt, UnitType ut){
    switch(bt){
        case BLD_TOWN_CENTER:
            return ut == UNIT_VILLAGER || ut == UNIT_SCOUT;
        case BLD_BARRACKS:
            return ut == UNIT_MILITIA || ut == UNIT_MAN_AT_ARMS || ut == UNIT_SPEARMAN;
        case BLD_ARCHERY_RANGE:
            return ut == UNIT_ARCHER || ut == UNIT_SKIRMISHER || ut == UNIT_CAVALRY_ARCHER;
        case BLD_STABLE:
            return ut == UNIT_KNIGHT;
        case BLD_MONASTERY:
            return ut == UNIT_MONK;
        case BLD_SIEGE_WORKSHOP:
            return ut == UNIT_BATTERING_RAM || ut == UNIT_MANGONEL ||
                   ut == UNIT_SCORPION || ut == UNIT_BOMBARD_CANNON;
        default:
            return false;
    }
}

const char* unit_name(UnitType t){
    switch(t){
        case UNIT_VILLAGER: return "Villager";
        case UNIT_SCOUT: return "Scout";
        case UNIT_MILITIA: return "Militia";
        case UNIT_MAN_AT_ARMS: return "Man-at-Arms";
        case UNIT_SPEARMAN: return "Spearman";
        case UNIT_ARCHER: return "Archer";
        case UNIT_SKIRMISHER: return "Skirmisher";
        case UNIT_CAVALRY_ARCHER: return "Cavalry Archer";
        case UNIT_KNIGHT: return "Knight";
        case UNIT_MONK: return "Monk";
        case UNIT_BATTERING_RAM: return "Battering Ram";
        case UNIT_MANGONEL: return "Mangonel";
        case UNIT_SCORPION: return "Scorpion";
        case UNIT_BOMBARD_CANNON: return "Bombard Cannon";
        default: return "Unit";
    }
}

const char* building_name(BldType t){
    switch(t){
        case BLD_TOWN_CENTER: return "Town Center";
        case BLD_HOUSE: return "House";
        case BLD_BARRACKS: return "Barracks";
        case BLD_ARCHERY_RANGE: return "Archery Range";
        case BLD_STABLE: return "Stable";
        case BLD_BLACKSMITH: return "Blacksmith";
        case BLD_MARKET: return "Market";
        case BLD_MILL: return "Mill";
        case BLD_LUMBER_CAMP: return "Lumber Camp";
        case BLD_MINING_CAMP: return "Mining Camp";
        case BLD_FARM: return "Farm";
        case BLD_WATCH_TOWER: return "Watch Tower";
        case BLD_MONASTERY: return "Monastery";
        case BLD_SIEGE_WORKSHOP: return "Siege Workshop";
        case BLD_UNIVERSITY: return "University";
        case BLD_WALL: return "Wall";
        case BLD_GATE: return "Gate";
        default: return "Building";
    }
}
