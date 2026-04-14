/*=============================================================
 * main.c  –  Entry point, window init, game loop
 *=============================================================*/
#include "game.h"
#include "raylib.h"
#include <stdio.h>

#include "ui_state.h"
#include <stdio.h>

/* Forward declarations from other modules */
void game_init(GameState *gs);
void game_update(GameState *gs, float dt);

int main(void){
    /* ── Window ── */
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(SCREEN_W, SCREEN_H, "Age of Empires II  –  Raylib Edition");
    SetTargetFPS(0);
    SetExitKey(KEY_NULL);   /* Don't quit on ESC */

    /* ── Game state ── */
    GameState *gs = malloc(sizeof(GameState));
    UIState *ui = malloc(sizeof(UIState));
    if (!gs || !ui) {
        fprintf(stderr, "Failed to allocate memory for GameState / UIState\n");
        CloseWindow();
        return 1;
    }
    game_init(gs);
    ui_state_init(ui, gs);

    /* ── Main loop ── */
    while(!WindowShouldClose()){
        float dt=GetFrameTime();
        if(dt>0.05f) dt=0.05f;   /* cap dt to avoid spiral of death */

        /* Input */
        input_update(gs, ui);

        /* Update */
        game_update(gs,dt);

        /* Render */
        BeginDrawing();
        ClearBackground((Color){10,10,10,255});

        if(gs->phase==PHASE_PLAYING || gs->phase==PHASE_PAUSED ||
           gs->phase==PHASE_VICTORY || gs->phase==PHASE_DEFEAT){
            BeginMode2D(ui->camera);
                renderer_draw_world(gs, ui);
            EndMode2D();
        }

        /* HUD / overlay (screen-space) */
        hud_draw(gs, ui);

        /* FPS counter (debug) */
        DrawFPS(SCREEN_W-70,4);

        EndDrawing();
    }

    CloseWindow();
    free(gs);
    free(ui);
    return 0;
}
