/*=============================================================
 * main.c  –  Entry point, window initialisation, game loop
 *
 * Owns the top-level lifecycle: create window → allocate state →
 * run input/update/render loop → tear down.
 *=============================================================*/
#include "game.h"
#include "raylib.h"
#include <stdio.h>
#include <stdlib.h>
#include "net.h"
#include "ui_state.h"

/* Forward declarations from other modules */
void game_init(GameState *game);
void game_update(GameState *game, float dt);

/* Prevent a single long frame from snowballing into further long
   frames (the "spiral of death").  50 ms ≈ 20 FPS floor. */
static const float MAX_FRAME_DT = 0.05f;

static const Color CLEAR_COLOR = {10, 10, 10, 255};

/* Keep the FPS counter visible regardless of window size. */
static const int FPS_RIGHT_MARGIN = 70;
static const int FPS_TOP_MARGIN   = 4;

/*──────────────────────────────────────────────────────────────
 * Render – draw the world and HUD for the current frame.
 *──────────────────────────────────────────────────────────────*/
static void render_frame(GameState *game, UIState *ui)
{
    BeginDrawing();
    ClearBackground(CLEAR_COLOR);

    bool in_gameplay = (game->phase == PHASE_PLAYING  ||
                        game->phase == PHASE_PAUSED   ||
                        game->phase == PHASE_VICTORY  ||
                        game->phase == PHASE_DEFEAT);

    if (in_gameplay) {
        bool show_isometric_world =
            (game->hero.phase == HERO_POSSESSION_OFF      ||
             game->hero.phase == HERO_POSSESSION_ENTERING ||
             game->hero.phase == HERO_POSSESSION_EXITING);

        if (show_isometric_world) {
            BeginMode2D(ui->camera);
                renderer_draw_world(game, ui);
            EndMode2D();
        }

        if (game->hero.phase != HERO_POSSESSION_OFF) {
            renderer_draw_hero_possession(game, ui);
        }
    }

    hud_draw(game, ui);
    DrawFPS(GetScreenWidth() - FPS_RIGHT_MARGIN, FPS_TOP_MARGIN);
    EndDrawing();
}

/*──────────────────────────────────────────────────────────────
 * Entry point
 *──────────────────────────────────────────────────────────────*/
int main(void)
{
    TraceLog(LOG_INFO, "RTS >> main() entered");

    /* ── Window ─────────────────────────────────────────────── */
#if defined(PLATFORM_ANDROID) || defined(ANDROID)
    /* On Android the window is always full-screen; pass 0×0 so
       Raylib reads the actual device resolution. */
    TraceLog(LOG_INFO, "RTS >> platform=ANDROID, InitWindow(0, 0)");
    InitWindow(0, 0, "Sangora");
#else
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(SCREEN_W, SCREEN_H, "Sangora");
#endif
    TraceLog(LOG_INFO, "RTS >> InitWindow done. screen=%dx%d",
             GetScreenWidth(), GetScreenHeight());
    InitAudioDevice();
    SetExitKey(KEY_NULL); /* Don't quit on ESC */

    /* ── Allocate core state ────────────────────────────────── */
    TraceLog(LOG_INFO, "RTS >> allocating GameState (%zu bytes)", sizeof(GameState));
    GameState *game = malloc(sizeof(GameState));

    TraceLog(LOG_INFO, "RTS >> allocating UIState  (%zu bytes)", sizeof(UIState));
    UIState *ui = malloc(sizeof(UIState));

    if (!game || !ui) {
        TraceLog(LOG_ERROR, "RTS >> malloc FAILED: game=%p ui=%p",
                 (void *)game, (void *)ui);
        CloseWindow();
        return 1;
    }

    TraceLog(LOG_INFO, "RTS >> calling game_init");
    game_init(game);

    TraceLog(LOG_INFO, "RTS >> calling ui_state_init");
    ui_state_init(ui, game);

    TraceLog(LOG_INFO, "RTS >> init complete, entering game loop");

    GamePhase previous_phase = game->phase;

    /* ── Main loop ──────────────────────────────────────────── */
    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        if (dt > MAX_FRAME_DT) {
            dt = MAX_FRAME_DT;
        }

        input_update(game, ui);
        net_update(game);

        float sim_dt = dt * game->game_speed;
        game_update(game, sim_dt);

        /* Re-centre camera when transitioning from menu to gameplay. */
        if (previous_phase == PHASE_MENU && game->phase == PHASE_PLAYING) {
            ui_center_on_tc(ui, game);
        }
        previous_phase = game->phase;

        render_frame(game, ui);
    }

    /* ── Cleanup ────────────────────────────────────────────── */
    net_deinit();
    ui_state_deinit(ui);
    if (IsAudioDeviceReady()) {
        CloseAudioDevice();
    }
    CloseWindow();

    free(game);
    free(ui);
    return 0;
}
