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
} UIState;

void ui_state_init(UIState *ui);

/* Abstracted prototypes that now take UIState for rendering / input */
void renderer_draw_world(GameState *gs, UIState *ui);
void hud_draw(GameState *gs, UIState *ui);
void input_update(GameState *gs, UIState *ui);

