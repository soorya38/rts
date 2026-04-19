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

#define CAMPAIGN_MISSION_COUNT 5

static void campaign_set_text(GameState *gs, const char *title,
                              const char *objective, const char *hint,
                              const char *story, const char *result){
    snprintf(gs->campaign_title, sizeof(gs->campaign_title), "%s", title ? title : "");
    snprintf(gs->campaign_objective, sizeof(gs->campaign_objective), "%s", objective ? objective : "");
    snprintf(gs->campaign_hint, sizeof(gs->campaign_hint), "%s", hint ? hint : "");
    snprintf(gs->campaign_story, sizeof(gs->campaign_story), "%s", story ? story : "");
    snprintf(gs->campaign_result, sizeof(gs->campaign_result), "%s", result ? result : "");
}

static bool campaign_place_ready_near_tc(GameState *gs, int player, BldType type,
                                         int min_r, int max_r){
    int tc = building_find(gs, player, BLD_TOWN_CENTER, true);
    if(tc < 0) return false;

    Building *town = &gs->buildings[tc];
    int w = building_tw(type), h = building_th(type);
    for(int r=min_r; r<=max_r; r++){
        for(int dy=-r; dy<=r; dy++){
            for(int dx=-r; dx<=r; dx++){
                if(abs(dx) != r && abs(dy) != r) continue;
                int tx = town->tx + dx;
                int ty = town->ty + dy;
                if(!map_in_bounds(tx, ty) || !map_in_bounds(tx + w - 1, ty + h - 1)) continue;
                if(!map_is_buildable(gs, tx, ty, w, h)) continue;
                return building_place_ready(gs, player, type, tx, ty) >= 0;
            }
        }
    }
    return false;
}

static int game_count_player_buildings(GameState *gs, int player, BldType type, bool complete_only){
    int count = 0;
    for(int i=0; i<MAX_BUILDINGS; i++){
        Building *b = &gs->buildings[i];
        if(!b->active || b->player != player || b->type != type) continue;
        if(complete_only && !b->complete) continue;
        count++;
    }
    return count;
}

static int game_count_player_units(GameState *gs, int player, UnitType type){
    int count = 0;
    for(int i=0; i<MAX_UNITS; i++){
        Unit *u = &gs->units[i];
        if(!u->active || u->player != player || u->type != type) continue;
        if(u->state == US_DEAD || u->state == US_DYING) continue;
        count++;
    }
    return count;
}

static int game_count_player_barracks_troops(GameState *gs, int player){
    return game_count_player_units(gs, player, UNIT_MILITIA) +
           game_count_player_units(gs, player, UNIT_MAN_AT_ARMS) +
           game_count_player_units(gs, player, UNIT_SPEARMAN);
}

static void campaign_spawn_units_near_tc(GameState *gs, int player, UnitType type, int count){
    int tc = building_find(gs, player, BLD_TOWN_CENTER, true);
    if(tc < 0) return;
    Building *town = &gs->buildings[tc];
    for(int i=0; i<count; i++){
        int tx = -1;
        int ty = -1;
        for(int r=1; r<=8 && tx < 0; r++){
            for(int dy=-r; dy<=r && tx < 0; dy++){
                for(int dx=-r; dx<=r; dx++){
                    if(abs(dx) != r && abs(dy) != r) continue;
                    int cx = town->tx + town->tw / 2 + dx;
                    int cy = town->ty + town->th / 2 + dy;
                    if(!map_in_bounds(cx, cy) || !map_is_passable(gs, cx, cy)) continue;
                    if(unit_tile_occupied(gs, cx, cy)) continue;
                    tx = cx;
                    ty = cy;
                    break;
                }
            }
        }
        if(tx < 0 || ty < 0) break;
        float wx = (tx + 0.5f) * TILE_SIZE;
        float wy = (ty + 0.5f) * TILE_SIZE;
        unit_spawn(gs, player, type, wx, wy);
    }
}

static void game_setup_random_starts(GameState *gs){
    int start_x[NUM_PLAYERS] = {0};
    int start_y[NUM_PLAYERS] = {0};
    map_init(gs, start_x, start_y, gs->num_players);

    for (int p=0; p < gs->num_players; p++) {
        int tx = start_x[p], ty = start_y[p];
        buildings_init_player(gs, p, tx - 2, ty - 2);
        gs->res[p].pop_cap = pop_cap_from_buildings(gs, p);

        float vx = (tx + 2) * TILE_SIZE + 16.0f;
        unit_spawn(gs, p, UNIT_VILLAGER, vx, (ty - 2) * TILE_SIZE + 16.0f);
        unit_spawn(gs, p, UNIT_VILLAGER, vx, (ty)     * TILE_SIZE + 16.0f);
        unit_spawn(gs, p, UNIT_VILLAGER, vx, (ty + 2) * TILE_SIZE + 16.0f);
        unit_spawn(gs, p, UNIT_SCOUT,    vx + TILE_SIZE, (ty) * TILE_SIZE + 16.0f);
    }

    int lp = net_get_local_player();
    for(int y=0;y<MAP_H;y++) for(int x=0;x<MAP_W;x++)
        for(int p=0; p<NUM_PLAYERS; p++)
            if(p != lp) gs->map[y][x].fog[p]=FOG_HIDDEN;

    map_update_fog(gs);
}

static void campaign_refresh_text(GameState *gs){
    if(!gs || gs->mode != GAME_MODE_CAMPAIGN) return;

    int houses = game_count_player_buildings(gs, 0, BLD_HOUSE, true);
    int mill = game_count_player_buildings(gs, 0, BLD_MILL, true);
    int lumber = game_count_player_buildings(gs, 0, BLD_LUMBER_CAMP, true);
    int mining = game_count_player_buildings(gs, 0, BLD_MINING_CAMP, true);
    int barracks = game_count_player_buildings(gs, 0, BLD_BARRACKS, true);
    int farms = game_count_player_buildings(gs, 0, BLD_FARM, true);
    int villagers = game_count_player_units(gs, 0, UNIT_VILLAGER);
    int archery = game_count_player_buildings(gs, 0, BLD_ARCHERY_RANGE, true);
    int archers = game_count_player_units(gs, 0, UNIT_ARCHER);
    int barracks_troops = game_count_player_barracks_troops(gs, 0);
    int enemy_barracks = game_count_player_buildings(gs, 1, BLD_BARRACKS, true);
    int enemy_tc = building_find(gs, 1, BLD_TOWN_CENTER, true);
    int enemy_tc_hp = (enemy_tc >= 0) ? gs->buildings[enemy_tc].hp : 0;
    int army = unit_count_military(gs, 0);

    switch(gs->campaign_mission){
        case 0:
            campaign_set_text(gs,
                "Lesson I: First Harvest",
                "Stock 120 food and 80 wood.\nFood %d/120  Wood %d/80",
                "Select villagers, then right-click berries or trees.\nUse the scout to reveal nearby land and resources.",
                "The River Clan reaches Ashfall Vale with one wagon of tools.\nIf the settlers cannot gather before nightfall, the expedition ends.",
                "The wagons are stocked, and the clan can survive its first night.");
            snprintf(gs->campaign_objective, sizeof(gs->campaign_objective),
                     "Stock 120 food and 80 wood.\nFood %d/120  Wood %d/80",
                     gs->res[0].amount[RES_FOOD], gs->res[0].amount[RES_WOOD]);
            break;
        case 1:
            campaign_set_text(gs,
                "Lesson II: Raise the Hamlet",
                "Build 2 Houses, a Mill, and a Lumber Camp.\nReach 6 Villagers.",
                "Select the Town Center to queue villagers.\nUse [B] with a villager to open the build menu.",
                "Elder Mira orders the camp turned into a real hamlet.\nHomes, food stores, and timber yards must come before war.",
                "New roofs rise over the camp, and Ashfall Vale becomes a village.");
            snprintf(gs->campaign_objective, sizeof(gs->campaign_objective),
                     "Build 2 Houses, a Mill, and a Lumber Camp.\nH%d/2  M%d/1  L%d/1  V%d/6",
                     houses, mill, lumber, villagers);
            break;
        case 2:
            campaign_set_text(gs,
                "Lesson III: Prepare for Feudal",
                "Add a Mining Camp, Barracks, and 2 Farms.\nAdvance to Feudal Age.",
                "Gold and stone need a Mining Camp drop-off.\nWhen you have 400 food, use the top bar age-up button.",
                "Scouts return with word of the Ashen Banner beyond the hills.\nThe village needs farms, steel, and a faster age of war.",
                "The town reaches Feudal Age before the raiders descend.");
            snprintf(gs->campaign_objective, sizeof(gs->campaign_objective),
                     "Add a Mining Camp, Barracks, and 2 Farms.\nMine%d/1  Barr%d/1  Farm%d/2  Age:%s",
                     mining, barracks, farms, gs->res[0].age >= 1 ? "Feudal" : "Dark");
            break;
        case 3:
            campaign_set_text(gs,
                "Lesson IV: Answer the Raid",
                "Build an Archery Range, train 2 Archers and 3 Barracks troops.\nDestroy the raider Barracks.",
                "Barracks train melee units; Archery Ranges train ranged units.\nRight-click enemies to attack, and use [G] to set rally points.",
                "At dawn a raider outpost appears on the ridge above the valley.\nYou must field a mixed force before the camp strikes first.",
                "The raider camp burns, but its banners point toward a larger army.");
            snprintf(gs->campaign_objective, sizeof(gs->campaign_objective),
                     "Build an Archery Range, train 2 Archers and 3 Barracks troops.\nRange%d/1  Arch%d/2  Troops%d/3  Camp:%s",
                     archery, archers, barracks_troops, enemy_barracks > 0 ? "Up" : "Down");
            break;
        case 4:
        default:
            campaign_set_text(gs,
                "Finale: Break the Siege",
                "Destroy the enemy Town Center.\nEnemy TC %d HP  Army %d",
                "Keep villagers gathering while reinforcements train.\nAttack in groups and keep ranged units behind your front line.",
                "The Ashen Banner marches on Ashfall Vale for a final siege.\nWin here and the valley becomes the River Clan's first stronghold.",
                "Ashfall Vale stands. The River Clan has won its first war.");
            snprintf(gs->campaign_objective, sizeof(gs->campaign_objective),
                     "Destroy the enemy Town Center.\nEnemy TC %d HP  Army %d",
                     enemy_tc_hp, army);
            break;
    }
}

static void game_update_campaign(GameState *gs){
    if(!gs || gs->mode != GAME_MODE_CAMPAIGN || gs->phase != PHASE_PLAYING) return;

    campaign_refresh_text(gs);

    switch(gs->campaign_mission){
        case 0:
            if(gs->res[0].amount[RES_FOOD] >= 120 && gs->res[0].amount[RES_WOOD] >= 80){
                gs->phase = PHASE_VICTORY;
                game_set_alert(gs, "Mission complete: your first supplies are secured.");
            }
            break;
        case 1: {
            int houses = 0, villagers = 0;
            for(int i=0;i<MAX_BUILDINGS;i++){
                Building *b = &gs->buildings[i];
                if(b->active && b->complete && b->player == 0 && b->type == BLD_HOUSE) houses++;
            }
            for(int i=0;i<MAX_UNITS;i++){
                Unit *u = &gs->units[i];
                if(u->active && u->player == 0 && u->type == UNIT_VILLAGER && u->state != US_DEAD) villagers++;
            }
            if(houses >= 2 &&
               building_find(gs, 0, BLD_MILL, true) >= 0 &&
               building_find(gs, 0, BLD_LUMBER_CAMP, true) >= 0 &&
               villagers >= 6){
                gs->phase = PHASE_VICTORY;
                game_set_alert(gs, "Mission complete: the hamlet stands ready.");
            }
            break;
        }
        case 2:
            if(building_find(gs, 0, BLD_MINING_CAMP, true) >= 0 &&
               building_find(gs, 0, BLD_BARRACKS, true) >= 0 &&
               game_count_player_buildings(gs, 0, BLD_FARM, true) >= 2 &&
               gs->res[0].age >= 1){
                gs->phase = PHASE_VICTORY;
                game_set_alert(gs, "Mission complete: your town has reached Feudal Age.");
            }
            break;
        case 3:
            if(building_find(gs, 0, BLD_ARCHERY_RANGE, true) >= 0 &&
               game_count_player_units(gs, 0, UNIT_ARCHER) >= 2 &&
               game_count_player_barracks_troops(gs, 0) >= 3 &&
               building_find(gs, 1, BLD_BARRACKS, true) < 0){
                gs->phase = PHASE_VICTORY;
                game_set_alert(gs, "Mission complete: the raider outpost has fallen.");
            }
            break;
        default:
            break;
    }
}

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
    if(gs->mode == GAME_MODE_CAMPAIGN && gs->campaign_mission == 3){
        game_set_alert(gs, "The outpost is crippled. Finish off the raider Barracks.");
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
           type == UNIT_MANGONEL || type == UNIT_SCORPION ||
           type == UNIT_BOMBARD_CANNON;
}

bool building_uses_projectiles(BldType type){
    return type == BLD_WATCH_TOWER || type == BLD_TOWN_CENTER;
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
        UNIT_BATTERING_RAM, UNIT_MANGONEL, UNIT_SCORPION, UNIT_BOMBARD_CANNON
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
    building_place_ready(gs, 0, BLD_UNIVERSITY,   32, 18);
    building_place_ready(gs, 0, BLD_WATCH_TOWER,  18, 30);

    building_place_ready(gs, 1, BLD_TOWN_CENTER,   48, 28);
    building_place_ready(gs, 1, BLD_HOUSE,         54, 22);
    building_place_ready(gs, 1, BLD_HOUSE,         54, 36);
    building_place_ready(gs, 1, BLD_MILL,          50, 18);
    building_place_ready(gs, 1, BLD_FARM,          50, 34);
    building_place_ready(gs, 1, BLD_BARRACKS,      40, 18);
    building_place_ready(gs, 1, BLD_ARCHERY_RANGE, 44, 18);
    building_place_ready(gs, 1, BLD_UNIVERSITY,    48, 18);
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
    game_setup_random_starts(gs);
}

void game_init_campaign(GameState *gs, uint32_t seed, int mission){
    if(mission < 0) mission = 0;
    if(mission >= CAMPAIGN_MISSION_COUNT) mission = CAMPAIGN_MISSION_COUNT - 1;

    int num_players = (mission < 3) ? 1 : 2;
    game_init_match(gs, seed, num_players, GAME_MODE_CAMPAIGN);
    gs->campaign_seed = seed;
    gs->campaign_mission = mission;
    game_setup_random_starts(gs);

    switch(mission){
        case 0:
            gs->res[0].amount[RES_FOOD] = 0;
            gs->res[0].amount[RES_WOOD] = 0;
            gs->res[0].amount[RES_GOLD] = 0;
            gs->res[0].amount[RES_STONE] = 0;
            break;
        case 1:
            gs->res[0].amount[RES_FOOD] = 180;
            gs->res[0].amount[RES_WOOD] = 260;
            gs->res[0].amount[RES_GOLD] = 0;
            gs->res[0].amount[RES_STONE] = 0;
            break;
        case 2:
            gs->res[0].amount[RES_FOOD] = 520;
            gs->res[0].amount[RES_WOOD] = 340;
            gs->res[0].amount[RES_GOLD] = 80;
            gs->res[0].amount[RES_STONE] = 0;
            campaign_place_ready_near_tc(gs, 0, BLD_HOUSE, 4, 14);
            campaign_place_ready_near_tc(gs, 0, BLD_HOUSE, 4, 14);
            campaign_place_ready_near_tc(gs, 0, BLD_MILL, 4, 14);
            campaign_place_ready_near_tc(gs, 0, BLD_LUMBER_CAMP, 4, 14);
            campaign_spawn_units_near_tc(gs, 0, UNIT_VILLAGER, 3);
            break;
        case 3:
            gs->res[0].age = 1;
            gs->res[1].age = 0;
            gs->res[0].amount[RES_FOOD] = 260;
            gs->res[0].amount[RES_WOOD] = 320;
            gs->res[0].amount[RES_GOLD] = 220;
            gs->res[0].amount[RES_STONE] = 0;
            gs->res[1].amount[RES_FOOD] = 0;
            gs->res[1].amount[RES_WOOD] = 0;
            gs->res[1].amount[RES_GOLD] = 0;
            gs->res[1].amount[RES_STONE] = 0;
            campaign_place_ready_near_tc(gs, 0, BLD_HOUSE, 4, 14);
            campaign_place_ready_near_tc(gs, 0, BLD_HOUSE, 4, 14);
            campaign_place_ready_near_tc(gs, 0, BLD_MILL, 4, 14);
            campaign_place_ready_near_tc(gs, 0, BLD_LUMBER_CAMP, 4, 14);
            campaign_place_ready_near_tc(gs, 0, BLD_MINING_CAMP, 4, 14);
            campaign_place_ready_near_tc(gs, 0, BLD_BARRACKS, 4, 14);
            campaign_place_ready_near_tc(gs, 0, BLD_FARM, 4, 14);
            campaign_place_ready_near_tc(gs, 0, BLD_FARM, 4, 14);
            campaign_spawn_units_near_tc(gs, 0, UNIT_VILLAGER, 4);
            campaign_place_ready_near_tc(gs, 1, BLD_HOUSE, 4, 14);
            campaign_place_ready_near_tc(gs, 1, BLD_BARRACKS, 4, 14);
            campaign_spawn_units_near_tc(gs, 1, UNIT_MILITIA, 3);
            break;
        case 4:
        default:
            gs->res[0].age = 1;
            gs->res[1].age = 1;
            gs->res[0].amount[RES_FOOD] = 650;
            gs->res[0].amount[RES_WOOD] = 480;
            gs->res[0].amount[RES_GOLD] = 320;
            gs->res[0].amount[RES_STONE] = 80;
            gs->res[1].amount[RES_FOOD] = 180;
            gs->res[1].amount[RES_WOOD] = 260;
            gs->res[1].amount[RES_GOLD] = 180;
            gs->res[1].amount[RES_STONE] = 125;
            campaign_place_ready_near_tc(gs, 0, BLD_HOUSE, 4, 14);
            campaign_place_ready_near_tc(gs, 0, BLD_HOUSE, 4, 14);
            campaign_place_ready_near_tc(gs, 0, BLD_MILL, 4, 14);
            campaign_place_ready_near_tc(gs, 0, BLD_LUMBER_CAMP, 4, 14);
            campaign_place_ready_near_tc(gs, 0, BLD_MINING_CAMP, 4, 14);
            campaign_place_ready_near_tc(gs, 0, BLD_BARRACKS, 5, 16);
            campaign_place_ready_near_tc(gs, 0, BLD_ARCHERY_RANGE, 5, 16);
            campaign_place_ready_near_tc(gs, 0, BLD_BLACKSMITH, 5, 16);
            campaign_place_ready_near_tc(gs, 0, BLD_FARM, 4, 14);
            campaign_place_ready_near_tc(gs, 0, BLD_FARM, 4, 14);
            campaign_place_ready_near_tc(gs, 0, BLD_FARM, 4, 14);
            campaign_spawn_units_near_tc(gs, 0, UNIT_VILLAGER, 5);
            campaign_spawn_units_near_tc(gs, 0, UNIT_MILITIA, 3);
            campaign_spawn_units_near_tc(gs, 0, UNIT_ARCHER, 2);
            campaign_place_ready_near_tc(gs, 1, BLD_HOUSE, 4, 14);
            campaign_place_ready_near_tc(gs, 1, BLD_HOUSE, 4, 14);
            campaign_place_ready_near_tc(gs, 1, BLD_MILL, 4, 14);
            campaign_place_ready_near_tc(gs, 1, BLD_LUMBER_CAMP, 4, 14);
            campaign_place_ready_near_tc(gs, 1, BLD_BARRACKS, 5, 16);
            campaign_place_ready_near_tc(gs, 1, BLD_ARCHERY_RANGE, 5, 16);
            campaign_place_ready_near_tc(gs, 1, BLD_WATCH_TOWER, 5, 16);
            campaign_spawn_units_near_tc(gs, 1, UNIT_MILITIA, 3);
            campaign_spawn_units_near_tc(gs, 1, UNIT_ARCHER, 2);
            break;
    }

    campaign_refresh_text(gs);
    game_set_alert(gs, gs->campaign_title);
}

void game_campaign_restart(GameState *gs){
    if(!gs || gs->mode != GAME_MODE_CAMPAIGN) return;
    game_init_campaign(gs, gs->campaign_seed ? gs->campaign_seed : (uint32_t)time(NULL), gs->campaign_mission);
}

bool game_campaign_has_next(const GameState *gs){
    return gs && gs->mode == GAME_MODE_CAMPAIGN && gs->campaign_mission + 1 < CAMPAIGN_MISSION_COUNT;
}

void game_campaign_advance(GameState *gs){
    if(!gs || gs->mode != GAME_MODE_CAMPAIGN) return;
    if(game_campaign_has_next(gs)){
        game_init_campaign(gs, gs->campaign_seed ? gs->campaign_seed : (uint32_t)time(NULL), gs->campaign_mission + 1);
    } else {
        game_init(gs);
    }
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
    int anchor_tx = (player == 0) ? 22 : 42;
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
    if (!g_net_active && gs->mode != GAME_MODE_SANDBOX &&
        !(gs->mode == GAME_MODE_CAMPAIGN && gs->campaign_mission < 4)) {
        ai_update(gs,dt);
    }

    if (gs->mode == GAME_MODE_CAMPAIGN) {
        game_update_campaign(gs);
    }

    /* Fog of war */
    map_update_fog(gs);
}
