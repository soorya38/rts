#include "ui_state.h"
#include <string.h>
#include "net.h"

void ui_state_init(UIState *ui, GameState *gs) {
    memset(ui, 0, sizeof(UIState));

    ui->sel_building   = -1;
    ui->sel_tile_x     = -1;
    ui->sel_tile_y     = -1;
    ui->hover_unit     = -1;
    ui->hover_building = -1;
    ui->hover_tile_x   = -1;
    ui->hover_tile_y   = -1;
    ui->rally_mode     = false;

    /* Default placeholder IP — user overwrites this to join */
    strncpy(ui->net_ip, "192.168.1.1", sizeof(ui->net_ip)-1);
    ui->net_ip_active  = false;

    ui->camera.zoom    = 1.0f;
    ui->camera.offset  = (Vector2){GetScreenWidth()*0.5f, GetScreenHeight()*0.5f};
    ui->camera.rotation= 0.0f;

    ui_center_on_tc(ui, gs);

    ui->tex_buildings[BLD_TOWN_CENTER]   = LoadTexture("assets/buildings/town_center.png");
    ui->tex_buildings[BLD_HOUSE]         = LoadTexture("assets/buildings/house.png");
    ui->tex_buildings[BLD_BARRACKS]      = LoadTexture("assets/buildings/barracks.png");
    ui->tex_buildings[BLD_ARCHERY_RANGE] = LoadTexture("assets/buildings/archery_range.png");
    ui->tex_buildings[BLD_STABLE]        = LoadTexture("assets/buildings/stable.png");
    ui->tex_buildings[BLD_MILL]          = LoadTexture("assets/buildings/mill.png");
    ui->tex_buildings[BLD_LUMBER_CAMP]   = LoadTexture("assets/buildings/lumber_camp.png");
    ui->tex_buildings[BLD_MINING_CAMP]   = LoadTexture("assets/buildings/mining_camp.png");
    ui->tex_buildings[BLD_BLACKSMITH]    = LoadTexture("assets/buildings/blacksmith.png");
    ui->tex_buildings[BLD_MARKET]        = LoadTexture("assets/buildings/market.png");
    ui->tex_buildings[BLD_FARM]          = LoadTexture("assets/buildings/farm.png");

    ui->tex_units[UNIT_VILLAGER]         = LoadTexture("assets/units/villager_f.png");
    ui->tex_units[UNIT_SCOUT]            = LoadTexture("assets/units/scout.png");
    ui->tex_units[UNIT_MILITIA]          = LoadTexture("assets/units/militia.png");
    ui->tex_units[UNIT_MAN_AT_ARMS]      = LoadTexture("assets/units/man_at_arms.png");
    ui->tex_units[UNIT_ARCHER]           = LoadTexture("assets/units/archer.png");
    ui->tex_units[UNIT_KNIGHT]           = LoadTexture("assets/units/knight.png");

    ui->tex_env_tree                     = LoadTexture("assets/env/tree.png");
    ui->tex_env_gold                     = LoadTexture("assets/env/gold_mine.png");
    ui->tex_env_stone                    = LoadTexture("assets/env/stone_mine.png");
    ui->tex_env_berries                  = LoadTexture("assets/env/berry_bush.png");

    ui->tex_ui_food                      = LoadTexture("assets/ui/food.png");
    ui->tex_ui_wood                      = LoadTexture("assets/ui/wood.png");
    ui->tex_ui_gold                      = LoadTexture("assets/ui/gold.png");
    ui->tex_ui_stone                     = LoadTexture("assets/ui/stone.png");
    ui->tex_ui_pop                       = LoadTexture("assets/ui/population.png");
}

void ui_state_deinit(UIState *ui) {
    for (int i = 0; i < BLD_COUNT; i++) UnloadTexture(ui->tex_buildings[i]);
    for (int i = 0; i < UNIT_COUNT; i++) UnloadTexture(ui->tex_units[i]);
    UnloadTexture(ui->tex_env_tree);
    UnloadTexture(ui->tex_env_gold);
    UnloadTexture(ui->tex_env_stone);
    UnloadTexture(ui->tex_env_berries);
    UnloadTexture(ui->tex_ui_food);
    UnloadTexture(ui->tex_ui_wood);
    UnloadTexture(ui->tex_ui_gold);
    UnloadTexture(ui->tex_ui_stone);
    UnloadTexture(ui->tex_ui_pop);
}

void ui_center_on_tc(UIState *ui, GameState *gs) {
    int lp = net_get_local_player();
    float target_wx = (MAP_W/2) * TILE_SIZE; 
    float target_wy = (MAP_H/2) * TILE_SIZE;
    if (gs) {
        for (int i = 0; i < MAX_BUILDINGS; i++) {
            Building *b = &gs->buildings[i];
            if (b->active && b->player == lp && b->type == BLD_TOWN_CENTER) {
                target_wx = (b->tx + b->tw / 2.0f) * TILE_SIZE;
                target_wy = (b->ty + b->th / 2.0f) * TILE_SIZE;
                break;
            }
        }
    }
    Vec2 iso_target = world_to_iso(target_wx, target_wy);
    ui->camera.target = to_rvec2(iso_target);
}

/* Scale factor relative to 720p so all HUD elements are readable on phones */
float hud_scale(void) {
    float s = GetScreenHeight() / 720.0f;
    if (s < 1.0f) s = 1.0f;
    if (s > 2.5f) s = 2.5f;
    return s;
}
