/*=============================================================
 * game.c  –  Game initialisation and master update tick
 *=============================================================*/
#include "game.h"
#include <string.h>
#include <stdio.h>
#include <time.h>
#include "net.h"

uint32_t _rng = 12345;

/* Forward declare the building init helper from building.c */
void buildings_init_player(GameState *gs,int player,int tc_tx,int tc_ty);

void game_set_alert(GameState *gs, const char *msg){
    snprintf(gs->alert,sizeof(gs->alert),"%s",msg);
    gs->alert_timer=3.5f;
}

void game_init_started_game(GameState *gs, uint32_t seed, int num_players) {
    memset(gs, 0, sizeof(GameState));
    gs->num_players = (num_players < 1) ? 1 : (num_players > 4 ? 4 : num_players);
    _rng = seed;

    gs->phase        = PHASE_PLAYING;
    gs->game_time    = 0.0f;
    gs->ai_phase     = 0;
    gs->ai_timer     = 0.0f;
    gs->ai_attack_cd = 90.0f;

    /* Setup player resources */
    for (int i = 0; i < gs->num_players; i++) {
        gs->res[i].amount[RES_FOOD]  = 200;
        gs->res[i].amount[RES_WOOD]  = 200;
        gs->res[i].amount[RES_GOLD]  = 100;
        gs->res[i].amount[RES_STONE] = 0;
        gs->res[i].age    = 0;
        gs->res[i].pop_cap = 5;
    }

    int dmx, dmy;
    map_init(gs, &dmx, &dmy, &dmx, &dmy);

    static const int CX[4] = { 15,          MAP_W-16,   15,          MAP_W-16 };
    static const int CY[4] = { 15,          15,         MAP_H-16,    MAP_H-16 };

    for (int p=0; p < gs->num_players; p++) {
        int corner = p;
        if (gs->num_players == 2) corner = (p == 0) ? 0 : 3;
        else if (gs->num_players == 3 && p == 2) corner = 3;

        int tx = CX[corner], ty = CY[corner];
        buildings_init_player(gs, p, tx - 2, ty - 2);
        gs->res[p].pop_cap = pop_cap_from_buildings(gs, p);

        float vx = (tx + 2) * TILE_SIZE + 16.0f;
        unit_spawn(gs, p, UNIT_VILLAGER, vx, (ty - 2) * TILE_SIZE + 16.0f);
        unit_spawn(gs, p, UNIT_VILLAGER, vx, (ty)     * TILE_SIZE + 16.0f);
        unit_spawn(gs, p, UNIT_VILLAGER, vx, (ty + 2) * TILE_SIZE + 16.0f);
        unit_spawn(gs, p, UNIT_SCOUT,    vx + TILE_SIZE, (ty) * TILE_SIZE + 16.0f);
    }

    /* Fog: mark everyone's fog explored if not the local player? 
       No, better to keep it hidden for competitive feel. */
    int lp = net_get_local_player();
    for(int y=0;y<MAP_H;y++) for(int x=0;x<MAP_W;x++)
        for(int p=0; p<NUM_PLAYERS; p++)
            if(p != lp) gs->map[y][x].fog[p]=FOG_HIDDEN;

    map_update_fog(gs);
}

/* ─── Game init (defaults to menu) ─────────────────────────── */
void game_init(GameState *gs){
    memset(gs, 0, sizeof(GameState));
    gs->phase = PHASE_MENU;
    /* We don't setup the game here anymore. Setup happens when "Start" is clicked. */
    /* For Solo Campaign, we'll call game_init_started_game(gs, time(NULL)) */
}

/* ─── Master update ────────────────────────────────────────── */
void game_update(GameState *gs, float dt){
    if(gs->phase!=PHASE_PLAYING) return;

    gs->game_time+=dt;

    /* Alert countdown */
    if(gs->alert_timer>0) gs->alert_timer-=dt;

    /* Update pop caps */
    for (int i = 0; i < gs->num_players; i++) {
        gs->res[i].pop_cap = pop_cap_from_buildings(gs, i);
        if (gs->res[i].pop_cap < 5) gs->res[i].pop_cap = 5;
    }

    /* Age advancement timers */
    res_update_age_advance(gs,dt);

    /* Units */
    units_update_all(gs,dt);

    /* Buildings */
    buildings_update_all(gs,dt);

    /* AI - only in singleplayer */
    if (!g_net_active) {
        ai_update(gs,dt);
    }

    /* Fog of war */
    map_update_fog(gs);
}
