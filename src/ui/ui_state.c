#include "ui_state.h"
#include <string.h>

void ui_state_init(UIState *ui) {
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
    ui->camera.target  = (Vector2){7*TILE_SIZE+64, 7*TILE_SIZE+64};
    ui->camera.rotation= 0.0f;
}
