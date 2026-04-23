#pragma once
#include "raylib.h"
#include "game.h"

/* Helper to bridge domain Vec2 to Raylib Vector2 */
static inline Vector2 to_rvec2(Vec2 v) {
    return (Vector2){ v.x, v.y };
}

static inline Vec2 to_vec2(Vector2 v) {
    return (Vec2){ v.x, v.y };
}

typedef struct {
    Camera2D camera;

    /* Selection state (belongs to UI) */
    bool       box_selecting;
    Vector2    box_start;
    
    int        sel_units[MAX_UNITS];
    int        sel_count;
    int        sel_building;        /* -1 = none */
    int        sel_tile_x, sel_tile_y; /* -1 = none; last clicked resource tile */

    /* Hover state */
    int        hover_unit;          /* -1 = none */
    int        hover_building;      /* -1 = none */
    int        hover_tile_x, hover_tile_y; /* -1 = none */

    /* UI State overlays */
    bool       build_panel_open;
    bool       menu_start_hover;
    bool       rally_mode;

    /* Multiplayer lobby */
    char  net_ip[64];      /* IP address the client will connect to */
    bool  net_ip_active;   /* IP text-box has keyboard focus */

    /* OSM map generator */
    char  osm_location[128];   /* location name for OSM map gen */
    bool  osm_location_active; /* location text-box has keyboard focus */

    /* Assets */
    Texture2D tex_buildings[BLD_COUNT];
    Texture2D tex_town_centers[4];
    Texture2D tex_houses[4];
    Texture2D tex_mills[4];
    Texture2D tex_lumber_camps[4];
    Texture2D tex_barracks[4];
    Texture2D tex_archery_ranges[4];
    Texture2D tex_stables[4];
    Texture2D tex_blacksmiths[4];
    Texture2D tex_markets[4];
    Texture2D tex_mining_camps[4];
    Texture2D tex_watch_towers[4];
    Texture2D tex_monasteries[4];
    Texture2D tex_land_grass[4];
    Texture2D tex_units[UNIT_COUNT];
    Texture2D tex_env_tree;
    Texture2D tex_env_gold;
    Texture2D tex_env_stone;
    Texture2D tex_env_berries;
    Texture2D tex_ui_food;
    Texture2D tex_ui_wood;
    Texture2D tex_ui_gold;
    Texture2D tex_ui_stone;
    Texture2D tex_ui_pop;
} UIState;

void ui_state_init(UIState *ui, GameState *gs);
void ui_state_deinit(UIState *ui);
void ui_center_on_tc(UIState *ui, GameState *gs);
Texture2D ui_get_building_texture(const UIState *ui, BldType type, int age);
/* Returns a scale factor relative to 720p so HUD elements are legible on any
   screen size, including high-DPI phones.  Range [1.0, 2.5]. */
float hud_scale(void);

/* Abstracted prototypes that now take UIState for rendering / input */
void renderer_draw_world(GameState *gs, UIState *ui);
void hud_draw(GameState *gs, UIState *ui);
void input_update(GameState *gs, UIState *ui);
