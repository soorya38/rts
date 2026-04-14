/*=============================================================
 * game.c  –  Game initialisation and master update tick
 *=============================================================*/
#include "game.h"
#include <string.h>
#include <stdio.h>
#include <time.h>

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

    /* Generate map with a random seed based on current time.
     * map_init picks a random corner for P1 and the opposite for AI,
     * and returns the chosen tile centres so we can place units correctly. */
    _rng = (uint32_t)time(NULL);
    int p1x, p1y, p2x, p2y;
    map_init(gs, &p1x, &p1y, &p2x, &p2y);

    /* ── Player 1 start (random corner) ── */
    /* TC occupies tiles [p1x-2 .. p1x+1][p1y-2 .. p1y+1] (4x4, centred on p1x,p1y) */
    int p1_tc_tx = p1x - 2, p1_tc_ty = p1y - 2;
    buildings_init_player(gs, 0, p1_tc_tx, p1_tc_ty);
    gs->res[0].pop_cap = pop_cap_from_buildings(gs, 0);

    /* Villagers spawn 4 tiles to the right (+x) of the TC centre */
    float p1_vx = (p1x + 2) * TILE_SIZE + 16.0f;
    float p1_v0y = (p1y - 2) * TILE_SIZE + 16.0f;
    float p1_v1y = (p1y)     * TILE_SIZE + 16.0f;
    float p1_v2y = (p1y + 2) * TILE_SIZE + 16.0f;
    unit_spawn(gs, 0, UNIT_VILLAGER, p1_vx, p1_v0y);
    unit_spawn(gs, 0, UNIT_VILLAGER, p1_vx, p1_v1y);
    unit_spawn(gs, 0, UNIT_VILLAGER, p1_vx, p1_v2y);
    unit_spawn(gs, 0, UNIT_SCOUT,   p1_vx + TILE_SIZE, p1_v1y);

    /* Send villagers to nearest resource slots */
    int wood_x, wood_y, food_x, food_y;
    if(map_find_resource(gs, 0, RES_WOOD, p1x + 2, p1y, &wood_x, &wood_y)){
        unit_give_gather_order(gs, &gs->units[0], wood_x, wood_y);
        unit_give_gather_order(gs, &gs->units[1], wood_x, wood_y);
    }
    if(map_find_resource(gs, 0, RES_FOOD, p1x + 2, p1y, &food_x, &food_y)){
        unit_give_gather_order(gs, &gs->units[2], food_x, food_y);
    }

    /* ── AI start (opposite corner) ── */
    int p2_tc_tx = p2x - 2, p2_tc_ty = p2y - 2;
    buildings_init_player(gs, 1, p2_tc_tx, p2_tc_ty);
    gs->res[1].pop_cap = pop_cap_from_buildings(gs, 1);

    float p2_vx = (p2x + 2) * TILE_SIZE + 16.0f;
    float p2_v0y = (p2y - 2) * TILE_SIZE + 16.0f;
    float p2_v1y = (p2y)     * TILE_SIZE + 16.0f;
    float p2_v2y = (p2y + 2) * TILE_SIZE + 16.0f;
    unit_spawn(gs, 1, UNIT_VILLAGER, p2_vx, p2_v0y);
    unit_spawn(gs, 1, UNIT_VILLAGER, p2_vx, p2_v1y);
    unit_spawn(gs, 1, UNIT_VILLAGER, p2_vx, p2_v2y);
    unit_spawn(gs, 1, UNIT_SCOUT,   p2_vx + TILE_SIZE, p2_v1y);

    /* AI fog: mark all tiles as explored at start */
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
