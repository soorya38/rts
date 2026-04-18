/*=============================================================
 * ai.c  –  Enemy AI: gather → build → train → attack FSM
 *=============================================================*/
#include "game.h"

#define AI  1   /* AI player index */
#define HU  0   /* Human player index */

/* ─── AI phases ────────────────────────────────────────────── */
#define AI_GATHER   0
#define AI_BUILD    1
#define AI_MILITARY 2
#define AI_ATTACK   3

/* How often (seconds) the AI "thinks" */
#define AI_THINK_RATE   2.5f
#define AI_ATTACK_CD   90.0f  /* attack wave every 90s */

/* ─── Helpers ────────────────────────────────────────────── */

static void ai_send_villager_gather(GameState *gs, int uid, ResType rt){
    Unit *u=&gs->units[uid];
    int tx,ty;
    int utx=(int)(u->wx/TILE_SIZE), uty=(int)(u->wy/TILE_SIZE);
    if(map_find_resource(gs,AI,rt,utx,uty,&tx,&ty))
        unit_give_gather_order(gs,u,tx,ty);
}

static void ai_auto_assign_villagers(GameState *gs){
    /* Assign idle villagers to gather: food-heavy opening with wood/gold support */
    int food_workers=0,wood_workers=0,gold_workers=0,stone_workers=0;
    for(int i=0;i<MAX_UNITS;i++){
        Unit *u=&gs->units[i];
        if(!u->active||u->player!=AI||u->type!=UNIT_VILLAGER) continue;
        if(u->state==US_GATHERING||u->state==US_RETURNING){
            switch(u->carry_type){
                case RES_FOOD: food_workers++; break;
                case RES_WOOD: wood_workers++; break;
                case RES_GOLD: gold_workers++; break;
                case RES_STONE: stone_workers++; break;
                default: break;
            }
        }
    }
    for(int i=0;i<MAX_UNITS;i++){
        Unit *u=&gs->units[i];
        if(!u->active||u->player!=AI||u->type!=UNIT_VILLAGER) continue;
        if(u->state!=US_IDLE&&u->state!=US_BUILDING) continue;
        if(u->state==US_BUILDING) continue;
        /* Assign to most needed resource */
        if(food_workers<4){
            ai_send_villager_gather(gs,i,RES_FOOD);
            food_workers++;
        } else if(wood_workers<4){
            ai_send_villager_gather(gs,i,RES_WOOD);
            wood_workers++;
        } else if(gold_workers<2){
            ai_send_villager_gather(gs,i,RES_GOLD);
            gold_workers++;
        } else if(gs->res[AI].age >= 1 && stone_workers < 2){
            ai_send_villager_gather(gs,i,RES_STONE);
            stone_workers++;
        } else {
            ai_send_villager_gather(gs,i,RES_WOOD);
        }
    }
}

static bool ai_try_build(GameState *gs, BldType type){
    /* Find a suitable location near AI town center */
    int tc=building_find(gs,AI,BLD_TOWN_CENTER,true);
    if(tc<0) return false;
    Building *town=&gs->buildings[tc];
    int w=building_tw(type),h=building_th(type);
    Cost c=building_cost(type);
    if(!res_can_afford(&gs->res[AI],c)) return false;

    /* Search in a ring around town center */
    for(int r=5;r<20;r++){
        for(int attempt=0;attempt<20;attempt++){
            int tx=town->tx+rng_range(-r,r);
            int ty=town->ty+rng_range(-r,r);
            if(!map_in_bounds(tx,ty)||!map_in_bounds(tx+w-1,ty+h-1)) continue;
            if(!map_is_buildable(gs,tx,ty,w,h)) continue;
            int bid=building_place(gs,AI,type,tx,ty);
            if(bid<0) continue;
            /* Find idle villager to build it */
            int vid=unit_find_idle_villager(gs,AI);
            if(vid>=0) unit_give_build_order(gs,&gs->units[vid],bid);
            return true;
        }
    }
    return false;
}

static void ai_train_unit(GameState *gs, BldType bld_type, UnitType ut){
    for(int i=0;i<MAX_BUILDINGS;i++){
        Building *b=&gs->buildings[i];
        if(!b->active||b->player!=AI||b->type!=bld_type||!b->complete) continue;
        if(b->queue_len>=BQUEUE_CAP) continue;
        if(gs->res[AI].age < unit_age_required(ut)) continue;
        building_enqueue_unit(gs,b,ut);
        return;
    }
}

static void ai_launch_attack(GameState *gs){
    /* Find player's Town Center */
    int tc=building_find(gs,HU,BLD_TOWN_CENTER,true);
    int tx=32,ty=32;
    if(tc>=0){ Building *b=&gs->buildings[tc]; tx=b->tx+b->tw/2; ty=b->ty+b->th/2; }

    /* Move all idle AI military to attack */
    for(int i=0;i<MAX_UNITS;i++){
        Unit *u=&gs->units[i];
        if(!u->active||u->player!=AI) continue;
        if(u->type==UNIT_VILLAGER||u->type==UNIT_SCOUT) continue;
        if(u->state==US_IDLE||u->state==US_MOVING){
            /* Find nearest player unit/building */
            int eu=-1;
            float best=1e30f;
            for(int j=0;j<MAX_UNITS;j++){
                Unit *t=&gs->units[j];
                if(!t->active||t->player!=HU||t->state==US_DEAD) continue;
                float d=dist2f(u->wx,u->wy,t->wx,t->wy);
                if(d<best){best=d;eu=j;}
            }
            if(eu>=0){
                unit_give_attack_order(gs,u,eu,-1);
            } else if(tc>=0){
                unit_give_attack_order(gs,u,-1,tc);
            } else {
                unit_give_move_order(gs,u,tx,ty);
            }
        }
    }
}

/* ─── Main AI update ──────────────────────────────────────── */
void ai_update(GameState *gs, float dt){
    gs->ai_timer+=dt;
    gs->ai_attack_cd-=dt;
    if(gs->ai_timer < AI_THINK_RATE) return;
    gs->ai_timer=0.0f;

    PlayerRes *pr=&gs->res[AI];

    /* ── Always keep villagers busy ── */
    ai_auto_assign_villagers(gs);

    /* ── Train villagers from TC ── */
    if(pr->population < pr->pop_cap-2 && pr->amount[RES_FOOD]>=50){
        ai_train_unit(gs,BLD_TOWN_CENTER,UNIT_VILLAGER);
    }

    /* ── Pop cap management ── */
    int houses=0;
    for(int i=0;i<MAX_BUILDINGS;i++){
        Building *b=&gs->buildings[i];
        if(b->active&&b->player==AI&&b->type==BLD_HOUSE) houses++;
    }
    if(pr->pop_cap < 30 && houses < 6 && pr->amount[RES_WOOD]>=25){
        ai_try_build(gs,BLD_HOUSE);
    }

    /* ── Phase transitions ── */
    switch(gs->ai_phase){
        case AI_GATHER:
            if(pr->amount[RES_WOOD]>=175 || pr->amount[RES_FOOD]>=200)
                gs->ai_phase=AI_BUILD;
            break;

        case AI_BUILD: {
            bool has_barracks = building_find(gs,AI,BLD_BARRACKS,false)>=0;
            bool has_mill     = building_find(gs,AI,BLD_MILL,false)>=0;
            bool has_blacksmith = building_find(gs,AI,BLD_BLACKSMITH,false)>=0;
            if(!has_mill && pr->amount[RES_WOOD]>=100)
                ai_try_build(gs,BLD_MILL);
            if(!has_barracks && pr->amount[RES_WOOD]>=175)
                ai_try_build(gs,BLD_BARRACKS);
            if(pr->age >= 1 && !has_blacksmith && pr->amount[RES_WOOD] >= 150)
                ai_try_build(gs,BLD_BLACKSMITH);
            if(has_barracks && building_find(gs,AI,BLD_BARRACKS,true)>=0)
                gs->ai_phase=AI_MILITARY;
            break;
        }

        case AI_MILITARY:
            /* Keep training */
            if(pr->age >= 2 && pr->amount[RES_GOLD]>=75&&pr->amount[RES_FOOD]>=60){
                ai_train_unit(gs,BLD_STABLE,UNIT_KNIGHT);
            } else if(pr->age >= 1 && pr->amount[RES_FOOD]>=35&&pr->amount[RES_WOOD]>=25){
                ai_train_unit(gs,BLD_BARRACKS,UNIT_SPEARMAN);
            } else if(pr->amount[RES_GOLD]>=60&&pr->amount[RES_FOOD]>=60){
                ai_train_unit(gs,BLD_BARRACKS,UNIT_MAN_AT_ARMS);
            } else if(pr->amount[RES_FOOD]>=60&&pr->amount[RES_GOLD]>=20){
                ai_train_unit(gs,BLD_BARRACKS,UNIT_MILITIA);
            }
            /* Try archery range */
            if(building_find(gs,AI,BLD_ARCHERY_RANGE,false)<0 && pr->amount[RES_WOOD]>=175)
                ai_try_build(gs,BLD_ARCHERY_RANGE);
            if(building_find(gs,AI,BLD_ARCHERY_RANGE,true)>=0 && pr->amount[RES_WOOD]>=25 && pr->amount[RES_GOLD]>=45)
                ai_train_unit(gs,BLD_ARCHERY_RANGE,UNIT_ARCHER);
            if(pr->age >= 1 && building_find(gs,AI,BLD_ARCHERY_RANGE,true)>=0 &&
               pr->amount[RES_FOOD]>=25 && pr->amount[RES_WOOD]>=35)
                ai_train_unit(gs,BLD_ARCHERY_RANGE,UNIT_SKIRMISHER);
            if(pr->age >= 1 && building_find(gs,AI,BLD_STABLE,false)<0 && pr->amount[RES_WOOD] >= 175)
                ai_try_build(gs,BLD_STABLE);
            if(pr->age >= 1 && building_find(gs,AI,BLD_WATCH_TOWER,false)<0 &&
               pr->amount[RES_WOOD] >= 125 && pr->amount[RES_STONE] >= 125)
                ai_try_build(gs,BLD_WATCH_TOWER);
            if(pr->age >= 2 && building_find(gs,AI,BLD_MONASTERY,false)<0 && pr->amount[RES_WOOD] >= 175)
                ai_try_build(gs,BLD_MONASTERY);
            if(pr->age >= 2 && building_find(gs,AI,BLD_MONASTERY,true)>=0 && pr->amount[RES_GOLD] >= 100)
                ai_train_unit(gs,BLD_MONASTERY,UNIT_MONK);
            if(pr->age >= 2 && building_find(gs,AI,BLD_SIEGE_WORKSHOP,false)<0 && pr->amount[RES_WOOD] >= 200)
                ai_try_build(gs,BLD_SIEGE_WORKSHOP);
            if(pr->age >= 2 && building_find(gs,AI,BLD_SIEGE_WORKSHOP,true)>=0){
                if(pr->tech_unlocked[TECH_CANNON_EMPLACEMENTS] &&
                   pr->amount[RES_WOOD] >= 225 && pr->amount[RES_GOLD] >= 225)
                    ai_train_unit(gs,BLD_SIEGE_WORKSHOP,UNIT_BOMBARD_CANNON);
                else if(pr->amount[RES_WOOD] >= 160 && pr->amount[RES_GOLD] >= 135)
                    ai_train_unit(gs,BLD_SIEGE_WORKSHOP,UNIT_MANGONEL);
                else if(pr->amount[RES_WOOD] >= 75 && pr->amount[RES_GOLD] >= 75)
                    ai_train_unit(gs,BLD_SIEGE_WORKSHOP,UNIT_SCORPION);
                else if(pr->amount[RES_WOOD] >= 160 && pr->amount[RES_GOLD] >= 75)
                    ai_train_unit(gs,BLD_SIEGE_WORKSHOP,UNIT_BATTERING_RAM);
            }

            if(unit_count_military(gs,AI)>=5)
                gs->ai_phase=AI_ATTACK;
            break;

        case AI_ATTACK:
            if(gs->ai_attack_cd<=0.0f){
                gs->ai_attack_cd=AI_ATTACK_CD;
                ai_launch_attack(gs);
                /* After attacking, go back to build more */
                if(unit_count_military(gs,AI)<3) gs->ai_phase=AI_MILITARY;
            }
            /* Keep training during attack phase */
            if(pr->amount[RES_FOOD]>=60)
                ai_train_unit(gs,BLD_BARRACKS,UNIT_MILITIA);
            if(building_find(gs,AI,BLD_ARCHERY_RANGE,true)>=0 && pr->amount[RES_WOOD]>=25 && pr->amount[RES_GOLD]>=45)
                ai_train_unit(gs,BLD_ARCHERY_RANGE,UNIT_ARCHER);
            if(building_find(gs,AI,BLD_STABLE,true)>=0 && pr->age >= 2 && pr->amount[RES_FOOD]>=60 && pr->amount[RES_GOLD]>=75)
                ai_train_unit(gs,BLD_STABLE,UNIT_KNIGHT);
            if(building_find(gs,AI,BLD_SIEGE_WORKSHOP,true)>=0 && pr->age >= 2 &&
               pr->amount[RES_WOOD]>=75 && pr->amount[RES_GOLD]>=75)
                ai_train_unit(gs,BLD_SIEGE_WORKSHOP,UNIT_SCORPION);
            break;
    }

    /* ── Age advance if resources allow ──────────────────── */
    if(pr->age==0 && !pr->advancing && pr->amount[RES_FOOD]>=500)
        res_try_advance_age(gs,AI);
    else if(pr->age==1 && !pr->advancing && pr->amount[RES_FOOD]>=800 && pr->amount[RES_WOOD]>=200)
        res_try_advance_age(gs,AI);
    else if(pr->age==2 && !pr->advancing && pr->amount[RES_FOOD]>=1000 && pr->amount[RES_GOLD]>=800)
        res_try_advance_age(gs,AI);
}
