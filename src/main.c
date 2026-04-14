/*=============================================================
 * main.c  –  Entry point, window init, game loop
 *=============================================================*/
#include "game.h"
#include "raylib.h"
#include <stdio.h>

/* Forward declarations from other modules */
void renderer_draw_world(GameState *gs);
void hud_draw(GameState *gs);
void input_update(GameState *gs);
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
    if (!gs) {
        fprintf(stderr, "Failed to allocate memory for GameState\n");
        CloseWindow();
        return 1;
    }
    game_init(gs);

    /* ── Main loop ── */
    while(!WindowShouldClose()){
        float dt=GetFrameTime();
        if(dt>0.05f) dt=0.05f;   /* cap dt to avoid spiral of death */

        /* Input */
        input_update(gs);

        /* Update */
        game_update(gs,dt);

        /* Render */
        BeginDrawing();
        ClearBackground((Color){10,10,10,255});

        if(gs->phase==PHASE_PLAYING || gs->phase==PHASE_PAUSED ||
           gs->phase==PHASE_VICTORY || gs->phase==PHASE_DEFEAT){
            BeginMode2D(gs->camera);
                renderer_draw_world(gs);
            EndMode2D();
        }

        /* HUD / overlay (screen-space) */
        hud_draw(gs);

        /* FPS counter (debug) */
        DrawFPS(SCREEN_W-70,4);

        EndDrawing();
    }

    CloseWindow();
    free(gs);
    return 0;
}
