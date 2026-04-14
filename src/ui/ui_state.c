#include "ui_state.h"
#include <string.h>

void ui_state_init(UIState *ui, GameState *gs) {
    memset(ui, 0, sizeof(UIState));

    ui->sel_building = -1;
    ui->sel_tile_x   = -1;
    ui->sel_tile_y   = -1;
    ui->hover_unit   = -1;
    ui->hover_building = -1;
    ui->hover_tile_x = -1;
    ui->hover_tile_y = -1;

    ui->camera.zoom    = 1.0f;
    ui->camera.offset  = (Vector2){SCREEN_W*0.5f, SCREEN_H*0.5f};
    ui->camera.rotation= 0.0f;

    /* Center camera on Player 1's Town Center */
    float target_wx = 7 * TILE_SIZE + 64; /* fallback */
    float target_wy = 7 * TILE_SIZE + 64;
    
    if (gs) {
        for (int i = 0; i < MAX_BUILDINGS; i++) {
            Building *b = &gs->buildings[i];
            if (b->active && b->player == 0 && b->type == BLD_TOWN_CENTER) {
                target_wx = (b->tx + b->tw / 2.0f) * TILE_SIZE;
                target_wy = (b->ty + b->th / 2.0f) * TILE_SIZE;
                break;
            }
        }
    }
    
    Vec2 iso_target = world_to_iso(target_wx, target_wy);
    ui->camera.target = to_rvec2(iso_target);
}
