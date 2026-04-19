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
}

void ui_state_deinit(UIState *ui) {
    for (int i = 0; i < BLD_COUNT; i++) UnloadTexture(ui->tex_buildings[i]);
    for (int i = 0; i < 4; i++) UnloadTexture(ui->tex_town_centers[i]);
    for (int i = 0; i < 4; i++) UnloadTexture(ui->tex_houses[i]);
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
