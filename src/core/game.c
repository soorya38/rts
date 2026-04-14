/*=============================================================
 * game.c  –  Game initialisation and master update tick
 *=============================================================*/
#include "game.h"
#include <string.h>
#include <stdio.h>

uint32_t _rng = 12345;

/* Forward declare the building init helper from building.c */
void buildings_init_player(GameState *gs,int player,int tc_tx,int tc_ty);

void game_set_alert(GameState *gs, const char *msg){
    snprintf(gs->alert,sizeof(gs->alert),"%s",msg);
    gs->alert_timer=3.5f;
}

/* ─── Game init ────────────────────────────────────────────── */
void game_init(GameState *gs){
    memset(gs,0,sizeof(GameState));

    gs->phase        = PHASE_MENU;
    gs->game_time    = 0.0f;

    gs->ai_phase     = 0;           /* AI starts in GATHER */
    gs->ai_timer     = 0.0f;
    gs->ai_attack_cd = 90.0f;       /* First attack after 90 seconds */


    /* Player 1 resources (Dark Age start) */
    gs->res[0].amount[RES_FOOD]  = 200;
    gs->res[0].amount[RES_WOOD]  = 200;
    gs->res[0].amount[RES_GOLD]  = 0;
    gs->res[0].amount[RES_STONE] = 0;
    gs->res[0].age    = 0;
    gs->res[0].pop_cap = 5;

    /* AI resources */
    gs->res[1].amount[RES_FOOD]  = 200;
    gs->res[1].amount[RES_WOOD]  = 200;
    gs->res[1].amount[RES_GOLD]  = 0;
    gs->res[1].amount[RES_STONE] = 0;
    gs->res[1].age    = 0;
    gs->res[1].pop_cap = 5;

    /* Generate map */
    _rng=42;
    map_init(gs);

    /* ── Player 1 start (top-left) ── */
    buildings_init_player(gs,0,4,4);   /* Town Center at tile (4,4) */
    gs->res[0].pop_cap=pop_cap_from_buildings(gs,0);

    /* 3 starting villagers */
    unit_spawn(gs,0,UNIT_VILLAGER,(8.0f*TILE_SIZE)+16,(4.0f*TILE_SIZE)+16);
    unit_spawn(gs,0,UNIT_VILLAGER,(8.0f*TILE_SIZE)+16,(6.0f*TILE_SIZE)+16);
    unit_spawn(gs,0,UNIT_VILLAGER,(8.0f*TILE_SIZE)+16,(8.0f*TILE_SIZE)+16);
    /* 1 scout */
    unit_spawn(gs,0,UNIT_SCOUT,   (10.0f*TILE_SIZE)+16,(6.0f*TILE_SIZE)+16);

    /* Send villagers to nearest resource slots */
    int wood_x,wood_y, food_x,food_y;
    if(map_find_resource(gs,0,RES_WOOD,8,6,&wood_x,&wood_y)){
        unit_give_gather_order(gs,&gs->units[0],wood_x,wood_y);
        unit_give_gather_order(gs,&gs->units[1],wood_x,wood_y);
    }
    if(map_find_resource(gs,0,RES_FOOD,8,6,&food_x,&food_y)){
        unit_give_gather_order(gs,&gs->units[2],food_x,food_y);
    }

    /* ── AI start (bottom-right) ── */
    buildings_init_player(gs,1,54,54); /* Town Center at tile (54,54) */
    gs->res[1].pop_cap=pop_cap_from_buildings(gs,1);

    unit_spawn(gs,1,UNIT_VILLAGER,(58.0f*TILE_SIZE)+16,(54.0f*TILE_SIZE)+16);
    unit_spawn(gs,1,UNIT_VILLAGER,(58.0f*TILE_SIZE)+16,(56.0f*TILE_SIZE)+16);
    unit_spawn(gs,1,UNIT_VILLAGER,(58.0f*TILE_SIZE)+16,(58.0f*TILE_SIZE)+16);
    unit_spawn(gs,1,UNIT_SCOUT,   (60.0f*TILE_SIZE)+16,(56.0f*TILE_SIZE)+16);

    /* AI fog: mark all tiles as visible for AI from start */
    for(int y=0;y<MAP_H;y++) for(int x=0;x<MAP_W;x++)
        gs->map[y][x].fog[1]=FOG_EXPLORED;

    /* Initial fog update for player */
    map_update_fog(gs);
}

/* ─── Master update ────────────────────────────────────────── */
void game_update(GameState *gs, float dt){
    if(gs->phase!=PHASE_PLAYING) return;

    gs->game_time+=dt;

    /* Alert countdown */
    if(gs->alert_timer>0) gs->alert_timer-=dt;

    /* Update pop caps */
    gs->res[0].pop_cap=pop_cap_from_buildings(gs,0);
    gs->res[1].pop_cap=pop_cap_from_buildings(gs,1);
    if(gs->res[0].pop_cap<5) gs->res[0].pop_cap=5;
    if(gs->res[1].pop_cap<5) gs->res[1].pop_cap=5;

    /* Age advancement timers */
    res_update_age_advance(gs,dt);

    /* Units */
    units_update_all(gs,dt);

    /* Buildings */
    buildings_update_all(gs,dt);

    /* AI */
    ai_update(gs,dt);

    /* Fog of war */
    map_update_fog(gs);
}
