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

static void game_handle_tc_destroyed(GameState *gs, int defeated_player){
    int lp = net_get_local_player();
    bool has_tc[NUM_PLAYERS] = {false};
    for(int i=0; i<MAX_BUILDINGS; i++){
        Building *eb = &gs->buildings[i];
        if(eb->active && eb->type == BLD_TOWN_CENTER) has_tc[eb->player] = true;
    }
    if(defeated_player == lp){
        game_set_alert(gs, "DEFEATED...");
        gs->phase = PHASE_DEFEAT;
        return;
    }
    bool any_enemy_tc = false;
    for(int p=0; p<NUM_PLAYERS; p++){
        if(p != lp && has_tc[p]) { any_enemy_tc = true; break; }
    }
    if(!any_enemy_tc){
        game_set_alert(gs, "VICTORY!");
        gs->phase = PHASE_VICTORY;
    }
}

bool game_damage_unit(GameState *gs, int target_unit, int dmg){
    if(target_unit < 0 || target_unit >= MAX_UNITS) return false;
    Unit *t = &gs->units[target_unit];
    if(!t->active || t->state == US_DEAD || t->state == US_DYING) return false;
    t->hp -= dmg;
    if(t->hp <= 0){
        t->state = US_DYING;
        t->death_timer = 0.8f;
        return false;
    }
    return true;
}

bool game_damage_building(GameState *gs, int target_bld, int dmg){
    if(target_bld < 0 || target_bld >= MAX_BUILDINGS) return false;
    Building *b = &gs->buildings[target_bld];
    if(!b->active) return false;
    b->hp -= dmg;
    if(b->hp <= 0){
        int defeated_player = b->player;
        bool was_tc = (b->type == BLD_TOWN_CENTER);
        building_destroy(gs, target_bld);
        if(was_tc && gs->mode != GAME_MODE_SANDBOX){
            game_handle_tc_destroyed(gs, defeated_player);
        }
        return false;
    }
    return true;
}

bool unit_uses_projectiles(UnitType type){
    return type == UNIT_ARCHER || type == UNIT_SKIRMISHER || type == UNIT_CAVALRY_ARCHER ||
           type == UNIT_MANGONEL || type == UNIT_SCORPION;
}

bool building_uses_projectiles(BldType type){
    return type == BLD_WATCH_TOWER;
}

void game_spawn_projectile(GameState *gs, int owner_player, ProjectileType type,
                           float sx, float sy, float ex, float ey,
                           int target_unit, int target_bld, int dmg,
                           float duration, float arc_height){
    for(int i=0;i<MAX_PROJECTILES;i++){
        Projectile *p = &gs->projectiles[i];
        if(p->active) continue;
        p->active = true;
        p->owner_player = owner_player;
        p->type = type;
        p->target_unit = target_unit;
        p->target_bld = target_bld;
        p->damage = dmg;
        p->sx = sx;
        p->sy = sy;
        p->ex = ex;
        p->ey = ey;
        p->elapsed = 0.0f;
        p->duration = duration;
        p->arc_height = arc_height;
        return;
    }
}

static void damage_units_in_radius(GameState *gs, int owner_player, float cx, float cy,
                                   float radius_px, int damage, int skip_unit){
    for(int i=0;i<MAX_UNITS;i++){
        Unit *t = &gs->units[i];
        if(i == skip_unit) continue;
        if(!t->active || t->player == owner_player || t->state == US_DEAD || t->state == US_DYING) continue;
        float d = dist2f(cx, cy, t->wx, t->wy);
        if(d > radius_px) continue;
        int splash = damage - (int)(d / 18.0f);
        if(splash < 1) splash = 1;
        game_damage_unit(gs, i, splash);
    }
}

void game_update_projectiles(GameState *gs, float dt){
    for(int i=0;i<MAX_PROJECTILES;i++){
        Projectile *p = &gs->projectiles[i];
        if(!p->active) continue;
        p->elapsed += dt;
        if(p->elapsed < p->duration) continue;
        if(p->target_unit >= 0){
            game_damage_unit(gs, p->target_unit, p->damage);
            if(p->type == PROJ_STONE){
                damage_units_in_radius(gs, p->owner_player, p->ex, p->ey,
                                       TILE_SIZE * 1.35f, p->damage / 2, p->target_unit);
            }
        } else if(p->target_bld >= 0){
            game_damage_building(gs, p->target_bld, p->damage);
            if(p->type == PROJ_STONE){
                damage_units_in_radius(gs, p->owner_player, p->ex, p->ey,
                                       TILE_SIZE * 1.5f, p->damage / 2, -1);
            }
        }
        p->active = false;
    }
}

static void game_init_match(GameState *gs, uint32_t seed, int num_players, GameMode mode){
    memset(gs, 0, sizeof(GameState));
    gs->mode = mode;
    gs->num_players = (num_players < 1) ? 1 : (num_players > 4 ? 4 : num_players);
    _rng = seed;

    gs->phase        = PHASE_PLAYING;
    gs->game_time    = 0.0f;
    gs->ai_phase     = 0;
    gs->ai_timer     = 0.0f;
    gs->ai_attack_cd = 90.0f;

    for (int i = 0; i < gs->num_players; i++) {
        gs->res[i].amount[RES_FOOD]  = 200;
        gs->res[i].amount[RES_WOOD]  = 200;
        gs->res[i].amount[RES_GOLD]  = 100;
        gs->res[i].amount[RES_STONE] = 0;
        gs->res[i].age               = 0;
        gs->res[i].pop_cap           = 5;
    }
}

static void sandbox_clear_map(GameState *gs){
    for(int y=0;y<MAP_H;y++) for(int x=0;x<MAP_W;x++){
        gs->map[y][x].type         = TILE_GRASS;
        gs->map[y][x].resource_amt = 0;
        gs->map[y][x].building_id  = -1;
        gs->map[y][x].variant      = (uint8_t)(rng_next()%4);
        for(int p=0;p<NUM_PLAYERS;p++) gs->map[y][x].fog[p] = FOG_VISIBLE;
    }
}

static void sandbox_paint_patch(GameState *gs, int cx, int cy, int radius, TileType type, int amount){
    for(int dy=-radius;dy<=radius;dy++) for(int dx=-radius;dx<=radius;dx++){
        if(dx*dx + dy*dy > radius*radius) continue;
        int x = cx + dx, y = cy + dy;
        if(!map_in_bounds(x, y)) continue;
        if(gs->map[y][x].building_id >= 0) continue;
        gs->map[y][x].type = type;
        gs->map[y][x].resource_amt = amount + rng_range(0, amount/5);
    }
}

static void sandbox_spawn_block(GameState *gs, int player, int anchor_tx, int anchor_ty,
                                const UnitType *types, int count){
    int cols = (count > 3) ? 3 : count;
    if(cols < 1) cols = 1;
    for(int i=0;i<count;i++){
        int col = i % cols;
        int row = i / cols;
        float wx = (anchor_tx + col + 0.5f) * TILE_SIZE;
        float wy = (anchor_ty + row + 0.5f) * TILE_SIZE;
        unit_spawn(gs, player, types[i], wx, wy);
    }
}

static void sandbox_setup_bases(GameState *gs){
    static const UnitType preview_units[] = {
        UNIT_SCOUT, UNIT_MILITIA, UNIT_MAN_AT_ARMS,
        UNIT_SPEARMAN, UNIT_ARCHER, UNIT_SKIRMISHER,
        UNIT_CAVALRY_ARCHER, UNIT_KNIGHT, UNIT_MONK,
        UNIT_BATTERING_RAM, UNIT_MANGONEL, UNIT_SCORPION
    };

    sandbox_clear_map(gs);

    sandbox_paint_patch(gs, 9, 15, 2, TILE_FOREST, 180);
    sandbox_paint_patch(gs, 17, 15, 1, TILE_BERRIES, 500);
    sandbox_paint_patch(gs, 8, 43, 1, TILE_GOLD, 900);
    sandbox_paint_patch(gs, 18, 44, 1, TILE_STONE, 800);
    sandbox_paint_patch(gs, 11, 50, 2, TILE_FOREST, 180);

    sandbox_paint_patch(gs, 55, 15, 2, TILE_FOREST, 180);
    sandbox_paint_patch(gs, 47, 15, 1, TILE_BERRIES, 500);
    sandbox_paint_patch(gs, 56, 43, 1, TILE_GOLD, 900);
    sandbox_paint_patch(gs, 46, 44, 1, TILE_STONE, 800);

    building_place_ready(gs, 0, BLD_TOWN_CENTER,   8, 28);
    building_place_ready(gs, 0, BLD_HOUSE,         6, 22);
    building_place_ready(gs, 0, BLD_HOUSE,         6, 36);
    building_place_ready(gs, 0, BLD_MILL,         14, 18);
    building_place_ready(gs, 0, BLD_LUMBER_CAMP,  14, 24);
    building_place_ready(gs, 0, BLD_MINING_CAMP,  14, 38);
    building_place_ready(gs, 0, BLD_FARM,         12, 32);
    building_place_ready(gs, 0, BLD_BARRACKS,     20, 18);
    building_place_ready(gs, 0, BLD_ARCHERY_RANGE,24, 18);
    building_place_ready(gs, 0, BLD_STABLE,       28, 18);
    building_place_ready(gs, 0, BLD_BLACKSMITH,   20, 37);
    building_place_ready(gs, 0, BLD_MARKET,       24, 37);
    building_place_ready(gs, 0, BLD_MONASTERY,    28, 37);
    building_place_ready(gs, 0, BLD_SIEGE_WORKSHOP, 32, 37);
    building_place_ready(gs, 0, BLD_WATCH_TOWER,  18, 30);

    building_place_ready(gs, 1, BLD_TOWN_CENTER,   48, 28);
    building_place_ready(gs, 1, BLD_HOUSE,         54, 22);
    building_place_ready(gs, 1, BLD_HOUSE,         54, 36);
    building_place_ready(gs, 1, BLD_MILL,          50, 18);
    building_place_ready(gs, 1, BLD_FARM,          50, 34);
    building_place_ready(gs, 1, BLD_BARRACKS,      40, 18);
    building_place_ready(gs, 1, BLD_ARCHERY_RANGE, 44, 18);
    building_place_ready(gs, 1, BLD_STABLE,        40, 37);
    building_place_ready(gs, 1, BLD_MARKET,        44, 37);
    building_place_ready(gs, 1, BLD_SIEGE_WORKSHOP, 36, 37);
    building_place_ready(gs, 1, BLD_WATCH_TOWER,   46, 30);

    gs->res[0].amount[RES_FOOD]  = 5000;
    gs->res[0].amount[RES_WOOD]  = 5000;
    gs->res[0].amount[RES_GOLD]  = 5000;
    gs->res[0].amount[RES_STONE] = 5000;
    gs->res[1].amount[RES_FOOD]  = 3000;
    gs->res[1].amount[RES_WOOD]  = 3000;
    gs->res[1].amount[RES_GOLD]  = 3000;
    gs->res[1].amount[RES_STONE] = 3000;

    for(int p=0;p<gs->num_players;p++){
        gs->res[p].pop_cap = POP_CAP_MAX;
    }

    unit_spawn(gs, 0, UNIT_VILLAGER, (12.5f)*TILE_SIZE, (27.5f)*TILE_SIZE);
    unit_spawn(gs, 0, UNIT_VILLAGER, (12.5f)*TILE_SIZE, (29.5f)*TILE_SIZE);
    unit_spawn(gs, 0, UNIT_VILLAGER, (12.5f)*TILE_SIZE, (31.5f)*TILE_SIZE);
    unit_spawn(gs, 0, UNIT_VILLAGER, (11.5f)*TILE_SIZE, (24.5f)*TILE_SIZE);
    unit_spawn(gs, 0, UNIT_VILLAGER, (11.5f)*TILE_SIZE, (35.5f)*TILE_SIZE);
    sandbox_spawn_block(gs, 0, 22, 28, preview_units, (int)(sizeof(preview_units)/sizeof(preview_units[0])));

    unit_spawn(gs, 1, UNIT_VILLAGER, (51.5f)*TILE_SIZE, (27.5f)*TILE_SIZE);
    unit_spawn(gs, 1, UNIT_VILLAGER, (51.5f)*TILE_SIZE, (29.5f)*TILE_SIZE);
    unit_spawn(gs, 1, UNIT_VILLAGER, (51.5f)*TILE_SIZE, (31.5f)*TILE_SIZE);
    sandbox_spawn_block(gs, 1, 42, 28, preview_units, (int)(sizeof(preview_units)/sizeof(preview_units[0])));

    map_update_fog(gs);
}

void game_set_alert(GameState *gs, const char *msg){
    snprintf(gs->alert,sizeof(gs->alert),"%s",msg);
    gs->alert_timer=3.5f;
}

void game_init_started_game(GameState *gs, uint32_t seed, int num_players) {
    game_init_match(gs, seed, num_players, GAME_MODE_STANDARD);

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

void game_init_sandbox(GameState *gs, uint32_t seed){
    game_init_match(gs, seed, 2, GAME_MODE_SANDBOX);
    sandbox_setup_bases(gs);
    game_set_alert(gs, "Sandbox ready: economy, combat, tech, and building tests are all live.");
}

/* ─── Game init (defaults to menu) ─────────────────────────── */
void game_init(GameState *gs){
    memset(gs, 0, sizeof(GameState));
    gs->mode = GAME_MODE_STANDARD;
    gs->phase = PHASE_MENU;
    /* We don't setup the game here anymore. Setup happens when "Start" is clicked. */
    /* For Solo Campaign, we'll call game_init_started_game(gs, time(NULL)) */
}

void game_sandbox_add_resources(GameState *gs, int player, int amount){
    if(!gs || gs->mode != GAME_MODE_SANDBOX || player < 0 || player >= gs->num_players) return;
    res_add(&gs->res[player], RES_FOOD, amount);
    res_add(&gs->res[player], RES_WOOD, amount);
    res_add(&gs->res[player], RES_GOLD, amount);
    res_add(&gs->res[player], RES_STONE, amount);
    game_set_alert(gs, "Sandbox: +resources added.");
}

void game_sandbox_next_age(GameState *gs, int player){
    static const char *AGE_NAMES[] = {"Dark Age", "Feudal Age", "Castle Age", "Imperial Age"};
    if(!gs || gs->mode != GAME_MODE_SANDBOX || player < 0 || player >= gs->num_players) return;
    PlayerRes *pr = &gs->res[player];
    if(pr->age >= 3){
        game_set_alert(gs, "Sandbox: already at Imperial Age.");
        return;
    }
    pr->age++;
    pr->advancing = false;
    pr->advance_timer = 0.0f;
    game_set_alert(gs, AGE_NAMES[pr->age]);
}

void game_sandbox_spawn_wave(GameState *gs, int player){
    static const UnitType WAVE[] = {
        UNIT_MAN_AT_ARMS, UNIT_SPEARMAN, UNIT_ARCHER, UNIT_KNIGHT, UNIT_MANGONEL
    };
    if(!gs || gs->mode != GAME_MODE_SANDBOX || player < 0 || player >= gs->num_players) return;
    int lane = (unit_count_military(gs, player) / 5) % 3;
    int anchor_ty = 26 + lane * 4;
    int anchor_tx = (player == 0) ? 28 : 34;
    sandbox_spawn_block(gs, player, anchor_tx, anchor_ty, WAVE, (int)(sizeof(WAVE)/sizeof(WAVE[0])));
    game_set_alert(gs, (player == 0) ? "Sandbox: allied test squad spawned." :
                                      "Sandbox: enemy test squad spawned.");
}

void game_sandbox_heal_selection(GameState *gs, int player, int building_id,
                                 const int *unit_ids, int unit_count){
    if(!gs || gs->mode != GAME_MODE_SANDBOX || player < 0 || player >= gs->num_players) return;

    bool touched = false;
    if(building_id >= 0 && building_id < MAX_BUILDINGS){
        Building *b = &gs->buildings[building_id];
        if(b->active && b->player == player){
            if(!b->complete) building_on_complete(gs, b);
            b->hp = b->max_hp;
            touched = true;
        }
    }

    for(int i=0;i<unit_count;i++){
        int uid = unit_ids[i];
        if(uid < 0 || uid >= MAX_UNITS) continue;
        Unit *u = &gs->units[uid];
        if(!u->active || u->player != player) continue;
        u->hp = u->max_hp;
        if(u->state == US_DYING){
            u->state = US_IDLE;
            u->death_timer = 0.8f;
        }
        touched = true;
    }

    if(!touched){
        for(int i=0;i<MAX_UNITS;i++){
            Unit *u = &gs->units[i];
            if(!u->active || u->player != player) continue;
            u->hp = u->max_hp;
            if(u->state == US_DYING){
                u->state = US_IDLE;
                u->death_timer = 0.8f;
            }
        }
        for(int i=0;i<MAX_BUILDINGS;i++){
            Building *b = &gs->buildings[i];
            if(!b->active || b->player != player) continue;
            if(!b->complete) building_on_complete(gs, b);
            b->hp = b->max_hp;
        }
        game_set_alert(gs, "Sandbox: restored all friendly units and buildings.");
        return;
    }

    game_set_alert(gs, "Sandbox: restored the current selection.");
}

/* ─── Master update ────────────────────────────────────────── */
void game_update(GameState *gs, float dt){
    if(gs->phase!=PHASE_PLAYING) return;

    gs->game_time+=dt;

    /* Alert countdown */
    if(gs->alert_timer>0) gs->alert_timer-=dt;

    /* Update pop caps */
    for (int i = 0; i < gs->num_players; i++) {
        if (gs->mode == GAME_MODE_SANDBOX) {
            gs->res[i].pop_cap = POP_CAP_MAX;
        } else {
            gs->res[i].pop_cap = pop_cap_from_buildings(gs, i);
            if (gs->res[i].pop_cap < 5) gs->res[i].pop_cap = 5;
        }
    }

    /* Age advancement timers */
    res_update_age_advance(gs,dt);

    /* Units */
    units_update_all(gs,dt);

    /* Buildings */
    buildings_update_all(gs,dt);

    /* Projectiles */
    game_update_projectiles(gs, dt);

    /* AI - only in singleplayer */
    if (!g_net_active && gs->mode != GAME_MODE_SANDBOX) {
        ai_update(gs,dt);
    }

    /* Fog of war */
    map_update_fog(gs);
}
