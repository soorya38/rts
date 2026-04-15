/*=============================================================
 * input_commands.c  –  Hotkeys, build mode, master input_update
 *=============================================================*/
#include "game.h"
#include "ui_state.h"
#include <math.h>
#include <stdio.h>
#include "net.h"

/* Forward declarations from input split files */
extern void update_camera(GameState *gs, UIState *ui, float dt);
extern void update_hover(GameState *gs, UIState *ui);
extern void clear_selection(GameState *gs, UIState *ui);
extern void handle_left_down(GameState *gs, UIState *ui);
extern void handle_left_up(GameState *gs, UIState *ui);

/* ─── Build ghost placement ───────────────────────────────── */
static void update_build_mode(GameState *gs, UIState *ui) {
    if (!gs->build_mode.active) return;
    Vector2 mp = GetMousePosition();
    if (IsKeyPressed(KEY_ESCAPE)) { gs->build_mode.active = false; return; }
    if (mp.y < 42 || mp.y > SCREEN_H - 130) return;

    Vector2 wp = GetScreenToWorld2D(mp, ui->camera);
    Vector2 cart = to_rvec2(iso_to_world(wp.x, wp.y));
    int tx = (int)(cart.x / TILE_SIZE), ty = (int)(cart.y / TILE_SIZE);
    int tw = building_tw(gs->build_mode.type), th = building_th(gs->build_mode.type);
    int lp = net_get_local_player();
    tx -= tw / 2; ty -= th / 2;
    gs->build_mode.ghost_tx = tx; gs->build_mode.ghost_ty = ty;
    gs->build_mode.valid = map_is_buildable(gs, tx, ty, tw, th) &&
                           res_can_afford(&gs->res[lp], building_cost(gs->build_mode.type));

    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && gs->build_mode.valid) {
        if (g_net_active) {
            NetPacket pkt = {0};
            pkt.type = PKT_PLACE_BLD;
            pkt.player = g_local_player_id;
            pkt.extra = gs->build_mode.type;
            pkt.tx = tx;
            pkt.ty = ty;
            int uc = 0;
            for (int i = 0; i < ui->sel_count && uc < 64; i++) {
                Unit *u = &gs->units[ui->sel_units[i]];
                if (u->active && u->player == g_local_player_id && u->type == UNIT_VILLAGER) {
                    pkt.units[uc++] = ui->sel_units[i];
                }
            }
            if (uc == 0) {
                int vid = unit_find_idle_villager(gs, g_local_player_id);
                if (vid >= 0) pkt.units[uc++] = vid;
            }
            pkt.unit_count = uc;
            net_dispatch_packet(gs, &pkt);
            if (!IsKeyDown(KEY_LEFT_SHIFT)) gs->build_mode.active = false;
        } else {
            int lp = net_get_local_player();
            int bid = building_place(gs, lp, gs->build_mode.type, tx, ty);
            if (bid >= 0) {
                bool any = false;
                for (int i = 0; i < ui->sel_count; i++) {
                    Unit *u = &gs->units[ui->sel_units[i]];
                    if (u->active && u->player == lp && u->type == UNIT_VILLAGER) {
                        unit_give_build_order(gs, u, bid); any = true;
                    }
                }
                if (!any) {
                    int vid = unit_find_idle_villager(gs, lp);
                    if (vid >= 0) unit_give_build_order(gs, &gs->units[vid], bid);
                }
                if (!IsKeyDown(KEY_LEFT_SHIFT)) gs->build_mode.active = false;
            }
        }
    }
    if (IsMouseButtonPressed(MOUSE_RIGHT_BUTTON)) gs->build_mode.active = false;
}

/* ─── Hotkeys ─────────────────────────────────────────────── */
static void update_hotkeys(GameState *gs, UIState *ui) {
    if (IsKeyPressed(KEY_ESCAPE)) {
        if (gs->build_mode.active) { gs->build_mode.active = false; return; }
        if (ui->build_panel_open) { ui->build_panel_open = false; return; }
        clear_selection(gs, ui);
    }
    if (IsKeyPressed(KEY_B)) {
        bool vil = false;
        for (int i = 0; i < ui->sel_count; i++)
            if (gs->units[ui->sel_units[i]].type == UNIT_VILLAGER) { vil = true; break; }
        if (vil) {
            bool any = ui->build_panel_open || gs->build_mode.active;
            ui->build_panel_open = any ? false : true;
            gs->build_mode.active = false;
        }
    }
    bool vil = false;
    for (int i = 0; i < ui->sel_count; i++)
        if (gs->units[ui->sel_units[i]].type == UNIT_VILLAGER) { vil = true; break; }
    if (vil) {
        BldType qt = BLD_COUNT;
        int lp = net_get_local_player();
        int cur_age = gs->res[lp].age;
        if (IsKeyPressed(KEY_H)) qt = BLD_HOUSE;
        if (IsKeyPressed(KEY_R)) qt = BLD_BARRACKS;
        if (IsKeyPressed(KEY_A) && cur_age >= 1) qt = BLD_ARCHERY_RANGE;
        if (IsKeyPressed(KEY_A) && cur_age < 1)  game_set_alert(gs, "Archery Range requires Feudal Age!");
        if (IsKeyPressed(KEY_M)) qt = BLD_MILL;
        if (IsKeyPressed(KEY_F)) qt = BLD_FARM;
        if (qt != BLD_COUNT && res_can_afford(&gs->res[lp], building_cost(qt))) {
            gs->build_mode.type = qt;
            gs->build_mode.active = true;
            ui->build_panel_open = false;
            static const char *BN[BLD_COUNT] = {
                "Town Center","House","Barracks","Archery Range","Stable","Mill","Lumber Camp","Mining Camp","Farm"};
            char msg[48];
            snprintf(msg, sizeof(msg), "Placing: %s", BN[qt]);
            game_set_alert(gs, msg);
        }
    }
    if (IsKeyPressed(KEY_P) && gs->phase == PHASE_PLAYING)  gs->phase = PHASE_PAUSED;
    else if (IsKeyPressed(KEY_P) && gs->phase == PHASE_PAUSED) gs->phase = PHASE_PLAYING;
    if (IsKeyPressed(KEY_DELETE) && ui->sel_building >= 0) {
        Building *b = &gs->buildings[ui->sel_building];
        int lp = net_get_local_player();
        if (b->player == lp) {
            if (g_net_active) {
                NetPacket pkt = {0};
                pkt.type = PKT_DELETE_BLD;
                pkt.player = g_local_player_id;
                pkt.target_id = ui->sel_building;
                net_dispatch_packet(gs, &pkt);
            } else {
                map_clear_building(gs, b->tx, b->ty, b->tw, b->th); 
                b->active = false; 
            }
            ui->sel_building = -1; 
        }
    }
    if (IsKeyPressed(KEY_S) && IsKeyDown(KEY_LEFT_SHIFT)) {
        for (int i = 0; i < ui->sel_count; i++) {
            Unit *u = &gs->units[ui->sel_units[i]];
            u->state = US_IDLE; u->path_len = 0;
        }
    }
}

/* ─── Master input update ─────────────────────────────────── */
void input_update(GameState *gs, UIState *ui) {
    float dt = GetFrameTime();
    update_hover(gs, ui);
    update_camera(gs, ui, dt);

    if (gs->build_mode.active) {
        update_build_mode(gs, ui);
        update_hotkeys(gs, ui);
        return;
    }

    update_hotkeys(gs, ui);

    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON))   handle_left_down(gs, ui);
    if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON))  handle_left_up(gs, ui);

    if (gs->phase == PHASE_VICTORY || gs->phase == PHASE_DEFEAT) {
        if (IsKeyPressed(KEY_Q)) CloseWindow();
        if (IsKeyPressed(KEY_R)) game_init(gs);
    }
}
