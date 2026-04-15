#include "ui_state.h"
#include <string.h>
#include "net.h"

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
    ui->camera.offset  = (Vector2){GetScreenWidth()*0.5f, GetScreenHeight()*0.5f};
    ui->camera.rotation= 0.0f;

    ui_center_on_tc(ui, gs);
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
