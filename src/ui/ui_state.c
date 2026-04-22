#include "ui_state.h"
#include <stdio.h>
#include <string.h>
#include "net.h"

static const char *asset_path_without_prefix(const char *path) {
    return (path && strncmp(path, "assets/", 7) == 0) ? path + 7 : NULL;
}

static Texture2D load_game_texture(const char *path) {
    Texture2D tex = {0};
    if (!path || !path[0]) return tex;

    tex = LoadTexture(path);
    if (tex.id != 0) return tex;

    const char *trimmed = asset_path_without_prefix(path);
    if (trimmed) {
        tex = LoadTexture(trimmed);
        if (tex.id != 0) return tex;
    }

    const char *app_dir = GetApplicationDirectory();
    if (app_dir && app_dir[0]) {
        char full_path[1024];

        snprintf(full_path, sizeof(full_path), "%s%s", app_dir, path);
        tex = LoadTexture(full_path);
        if (tex.id != 0) return tex;

        snprintf(full_path, sizeof(full_path), "%s../%s", app_dir, path);
        tex = LoadTexture(full_path);
        if (tex.id != 0) return tex;

        if (trimmed) {
            snprintf(full_path, sizeof(full_path), "%s%s", app_dir, trimmed);
            tex = LoadTexture(full_path);
            if (tex.id != 0) return tex;

            snprintf(full_path, sizeof(full_path), "%s../%s", app_dir, trimmed);
            tex = LoadTexture(full_path);
            if (tex.id != 0) return tex;
        }
    }

    TraceLog(LOG_WARNING, "RTS >> failed to load texture: %s", path);
    return tex;
}

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

    /* Keep the box-only renderer everywhere else, but allow age-specific
       town center art. */
    ui->tex_town_centers[0]              = load_game_texture("assets/buildings/town_center_dark.png");
    ui->tex_town_centers[1]              = load_game_texture("assets/buildings/town_center_feudal.png");
    ui->tex_town_centers[2]              = load_game_texture("assets/buildings/town_center_castle.png");
    ui->tex_town_centers[3]              = load_game_texture("assets/buildings/town_center_imperial.png");
    ui->tex_houses[0]                    = load_game_texture("assets/buildings/house_dark.png");
    ui->tex_houses[1]                    = load_game_texture("assets/buildings/house_feudal.png");
    ui->tex_houses[2]                    = load_game_texture("assets/buildings/house_castle.png");
    ui->tex_houses[3]                    = load_game_texture("assets/buildings/house_imperial.png");
    ui->tex_mills[0]                     = load_game_texture("assets/buildings/mill_dark.png");
    ui->tex_mills[1]                     = load_game_texture("assets/buildings/mill_feudal.png");
    ui->tex_mills[2]                     = load_game_texture("assets/buildings/mill_castle.png");
    ui->tex_mills[3]                     = load_game_texture("assets/buildings/mill_imperial.png");
    ui->tex_lumber_camps[0]              = load_game_texture("assets/buildings/lumber_camp_dark.png");
    ui->tex_lumber_camps[1]              = load_game_texture("assets/buildings/lumber_camp_feudal.png");
    ui->tex_lumber_camps[2]              = load_game_texture("assets/buildings/lumber_camp_castle.png");
    ui->tex_lumber_camps[3]              = load_game_texture("assets/buildings/lumber_camp_imperial.png");
    ui->tex_barracks[0]                  = load_game_texture("assets/buildings/barracks_dark.png");
    ui->tex_barracks[1]                  = load_game_texture("assets/buildings/barracks_feudal.png");
    ui->tex_barracks[2]                  = load_game_texture("assets/buildings/barracks_castle.png");
    ui->tex_barracks[3]                  = load_game_texture("assets/buildings/barracks_imperial.png");
    ui->tex_blacksmiths[0]               = load_game_texture("assets/buildings/blacksmith_dark.png");
    ui->tex_blacksmiths[1]               = load_game_texture("assets/buildings/blacksmith_feudal.png");
    ui->tex_blacksmiths[2]               = load_game_texture("assets/buildings/blacksmith_castle.png");
    ui->tex_blacksmiths[3]               = load_game_texture("assets/buildings/blacksmith_imperial.png");
    ui->tex_markets[0]                   = load_game_texture("assets/buildings/market_dark.png");
    ui->tex_markets[1]                   = load_game_texture("assets/buildings/market_feudal.png");
    ui->tex_markets[2]                   = load_game_texture("assets/buildings/market_castle.png");
    ui->tex_markets[3]                   = load_game_texture("assets/buildings/market_imperial.png");
    ui->tex_mining_camps[0]              = load_game_texture("assets/buildings/mining_camp_dark.png");
    ui->tex_mining_camps[1]              = load_game_texture("assets/buildings/mining_camp_feudal.png");
    ui->tex_mining_camps[2]              = load_game_texture("assets/buildings/mining_camp_castle.png");
    ui->tex_mining_camps[3]              = load_game_texture("assets/buildings/mining_camp_imperial.png");
    ui->tex_watch_towers[0]              = load_game_texture("assets/buildings/watch_tower_dark.png");
    ui->tex_watch_towers[1]              = load_game_texture("assets/buildings/watch_tower_feudal.png");
    ui->tex_watch_towers[2]              = load_game_texture("assets/buildings/watch_tower_castle.png");
    ui->tex_watch_towers[3]              = load_game_texture("assets/buildings/watch_tower_imperial.png");
}

void ui_state_deinit(UIState *ui) {
    for (int i = 0; i < BLD_COUNT; i++) UnloadTexture(ui->tex_buildings[i]);
    for (int i = 0; i < 4; i++) UnloadTexture(ui->tex_town_centers[i]);
    for (int i = 0; i < 4; i++) UnloadTexture(ui->tex_houses[i]);
    for (int i = 0; i < 4; i++) UnloadTexture(ui->tex_mills[i]);
    for (int i = 0; i < 4; i++) UnloadTexture(ui->tex_lumber_camps[i]);
    for (int i = 0; i < 4; i++) UnloadTexture(ui->tex_barracks[i]);
    for (int i = 0; i < 4; i++) UnloadTexture(ui->tex_blacksmiths[i]);
    for (int i = 0; i < 4; i++) UnloadTexture(ui->tex_markets[i]);
    for (int i = 0; i < 4; i++) UnloadTexture(ui->tex_mining_camps[i]);
    for (int i = 0; i < 4; i++) UnloadTexture(ui->tex_watch_towers[i]);
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

Texture2D ui_get_building_texture(const UIState *ui, BldType type, int age) {
    if (!ui) return (Texture2D){0};

    if (age < 0) age = 0;
    if (age > 3) age = 3;

    Texture2D tex = ui->tex_buildings[type];
    switch (type) {
        case BLD_TOWN_CENTER:
            if (ui->tex_town_centers[age].id != 0) tex = ui->tex_town_centers[age];
            break;
        case BLD_HOUSE:
            if (ui->tex_houses[age].id != 0) tex = ui->tex_houses[age];
            break;
        case BLD_MILL:
            if (ui->tex_mills[age].id != 0) tex = ui->tex_mills[age];
            break;
        case BLD_LUMBER_CAMP:
            if (ui->tex_lumber_camps[age].id != 0) tex = ui->tex_lumber_camps[age];
            break;
        case BLD_BARRACKS:
            if (ui->tex_barracks[age].id != 0) tex = ui->tex_barracks[age];
            break;
        case BLD_BLACKSMITH:
            if (ui->tex_blacksmiths[age].id != 0) tex = ui->tex_blacksmiths[age];
            break;
        case BLD_MARKET:
            if (ui->tex_markets[age].id != 0) tex = ui->tex_markets[age];
            break;
        case BLD_MINING_CAMP:
            if (ui->tex_mining_camps[age].id != 0) tex = ui->tex_mining_camps[age];
            break;
        case BLD_WATCH_TOWER:
            if (ui->tex_watch_towers[age].id != 0) tex = ui->tex_watch_towers[age];
            break;
        default:
            break;
    }
    return tex;
}

/* Scale factor relative to 720p so all HUD elements are readable on phones */
float hud_scale(void) {
    float s = GetScreenHeight() / 720.0f;
    if (s < 1.0f) s = 1.0f;
    if (s > 2.5f) s = 2.5f;
    return s;
}
