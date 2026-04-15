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
        case UNIT_ARCHER:       return (Cost){25,35,0,0};
        case UNIT_KNIGHT:       return (Cost){60,0,75,0};
        default:                return (Cost){50,0,0,0};
    }
}

float building_train_time(UnitType t){
    switch(t){
        case UNIT_VILLAGER:    return 25.0f;
        case UNIT_SCOUT:       return 20.0f;
        case UNIT_MILITIA:     return 21.0f;
        case UNIT_MAN_AT_ARMS: return 21.0f;
        case UNIT_ARCHER:      return 35.0f;
        case UNIT_KNIGHT:      return 30.0f;
        default:               return 25.0f;
    }
}
