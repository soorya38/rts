/*=============================================================
 * main.c  –  Entry point, window init, game loop
 *=============================================================*/
#include "game.h"
#include "raylib.h"
#include <stdio.h>
#include <stdlib.h>
#include "net.h"
#include "ui_state.h"

/* Forward declarations from other modules */
void game_init(GameState *gs);
void game_update(GameState *gs, float dt);

int main(void){
    /* ── Window ── */
    TraceLog(LOG_INFO, "RTS >> main() entered");

#if defined(PLATFORM_ANDROID) || defined(ANDROID)
    /* On Android the window is always full-screen; pass 0x0 so Raylib
       reads the actual device resolution and avoid the resizable flag. */
    TraceLog(LOG_INFO, "RTS >> platform=ANDROID calling InitWindow(0,0)");
    InitWindow(0, 0, "RTS Game");
#else
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(SCREEN_W, SCREEN_H, "Age of Empires II \u2013 Raylib Edition");
#endif
    TraceLog(LOG_INFO, "RTS >> InitWindow done. screen=%dx%d",
             GetScreenWidth(), GetScreenHeight());
    InitAudioDevice();

    SetExitKey(KEY_NULL);   /* Don't quit on ESC */

    /* ── Game state ── */
    TraceLog(LOG_INFO, "RTS >> allocating GameState (%zu bytes)", sizeof(GameState));
    GameState *gs = malloc(sizeof(GameState));
    TraceLog(LOG_INFO, "RTS >> allocating UIState  (%zu bytes)", sizeof(UIState));
    UIState *ui = malloc(sizeof(UIState));
    if (!gs || !ui) {
        TraceLog(LOG_ERROR, "RTS >> malloc FAILED: gs=%p ui=%p", (void*)gs, (void*)ui);
        CloseWindow();
        return 1;
    }
    TraceLog(LOG_INFO, "RTS >> calling game_init");
    game_init(gs);
    TraceLog(LOG_INFO, "RTS >> calling ui_state_init");
    ui_state_init(ui, gs);
    TraceLog(LOG_INFO, "RTS >> init complete, entering game loop");

    GamePhase last_phase = gs->phase;

    /* ── Main loop ── */
    while(!WindowShouldClose()){
        float dt=GetFrameTime();
        if(dt>0.05f) dt=0.05f;   /* cap dt to avoid spiral of death */

        /* Input */
        input_update(gs, ui);

        /* Network */
        net_update(gs);

        /* Update */
        game_update(gs,dt);

        if (last_phase == PHASE_MENU && gs->phase == PHASE_PLAYING) {
            ui_center_on_tc(ui, gs);
        }
        last_phase = gs->phase;

        /* Render */
        BeginDrawing();
        ClearBackground((Color){10,10,10,255});

        if(gs->phase==PHASE_PLAYING || gs->phase==PHASE_PAUSED ||
           gs->phase==PHASE_VICTORY || gs->phase==PHASE_DEFEAT){
            if(gs->hero.phase == HERO_POSSESSION_OFF ||
               gs->hero.phase == HERO_POSSESSION_ENTERING ||
               gs->hero.phase == HERO_POSSESSION_EXITING){
                BeginMode2D(ui->camera);
                    renderer_draw_world(gs, ui);
                EndMode2D();
            }
            if(gs->hero.phase != HERO_POSSESSION_OFF){
                renderer_draw_hero_possession(gs, ui);
            }
        }

        /* HUD / overlay (screen-space) */
        hud_draw(gs, ui);

        /* FPS counter – use actual screen width so it stays visible on any device */
        DrawFPS(GetScreenWidth()-70, 4);

        EndDrawing();
    }

    net_deinit();
    ui_state_deinit(ui);
    if (IsAudioDeviceReady()) CloseAudioDevice();
    CloseWindow();
    free(gs);
    free(ui);
    return 0;
}
