/*=============================================================
 * input_commands.c  –  Hotkeys, build mode, master input_update
 *=============================================================*/
#include "game.h"
#include "ui_state.h"
#include <math.h>
#include <stdio.h>
#include "net.h"
#include "hud_common.h"   /* HUD_TOP_H, HUD_BOT_H */

/* Forward declarations from input split files */
extern void update_camera(GameState *gs, UIState *ui, float dt);
extern void update_hover(GameState *gs, UIState *ui);
extern void clear_selection(GameState *gs, UIState *ui);
extern void handle_left_down(GameState *gs, UIState *ui);
extern void handle_left_up(GameState *gs, UIState *ui);
extern void handle_tap(GameState *gs, UIState *ui);

/* ─── Build ghost placement ───────────────────────────────── */
static void update_build_mode(GameState *gs, UIState *ui) {
    if (!gs->build_mode.active) return;
    Vector2 mp = GetMousePosition();
#if defined(PLATFORM_ANDROID) || defined(ANDROID)
    /* On Android use touch position for ghost placement */
    if (GetTouchPointCount() > 0) mp = GetTouchPosition(0);
#endif
    if (IsKeyPressed(KEY_ESCAPE)) { gs->build_mode.active = false; return; }
    if (mp.y < HUD_TOP_H || mp.y > GetScreenHeight() - HUD_BOT_H) return;

    Vector2 wp = GetScreenToWorld2D(mp, ui->camera);
    Vector2 cart = to_rvec2(iso_to_world(wp.x, wp.y));
    int tx = (int)(cart.x / TILE_SIZE), ty = (int)(cart.y / TILE_SIZE);
    int tw = building_tw(gs->build_mode.type), th = building_th(gs->build_mode.type);
    int lp = net_get_local_player();
    tx -= tw / 2; ty -= th / 2;
    gs->build_mode.ghost_tx = tx; gs->build_mode.ghost_ty = ty;
    gs->build_mode.valid = map_is_buildable(gs, tx, ty, tw, th) &&
                           res_can_afford(&gs->res[lp], building_cost(gs->build_mode.type));

    bool is_wall = building_is_walllike(gs->build_mode.type);

#if defined(PLATFORM_ANDROID) || defined(ANDROID)
    bool press_trigger = IsMouseButtonPressed(MOUSE_LEFT_BUTTON) || (GetTouchPointCount() > 0 && IsGestureDetected(GESTURE_TAP));
    bool release_trigger = IsMouseButtonReleased(MOUSE_LEFT_BUTTON);
    bool place_trigger = release_trigger && !is_wall;
#else
    bool press_trigger = IsMouseButtonPressed(MOUSE_LEFT_BUTTON);
    bool release_trigger = IsMouseButtonReleased(MOUSE_LEFT_BUTTON);
    bool place_trigger = press_trigger && !is_wall;
#endif

    if (is_wall) {
        if (press_trigger) {
            gs->build_mode.dragging = true;
            gs->build_mode.drag_start_tx = tx;
            gs->build_mode.drag_start_ty = ty;
        }
    }

    if (place_trigger || (is_wall && release_trigger && gs->build_mode.dragging)) {
        int pts_x[200], pts_y[200];
        int pt_count = 1;
        pts_x[0] = tx; pts_y[0] = ty;

        if (is_wall && gs->build_mode.dragging) {
            pt_count = get_wall_line_points(gs->build_mode.drag_start_tx, gs->build_mode.drag_start_ty, tx, ty, pts_x, pts_y, 200);
            gs->build_mode.dragging = false;
        }

        for (int p = 0; p < pt_count; p++) {
            int cx = pts_x[p], cy = pts_y[p];
            if (!map_is_buildable(gs, cx, cy, tw, th) || !res_can_afford(&gs->res[lp], building_cost(gs->build_mode.type))) {
                continue;
            }

            if (g_net_active) {
                NetPacket pkt = {0};
                pkt.type = PKT_PLACE_BLD;
                pkt.player = g_local_player_id;
                pkt.extra = gs->build_mode.type;
                pkt.tx = cx;
                pkt.ty = cy;
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
            } else {
                int bid = building_place(gs, lp, gs->build_mode.type, cx, cy);
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
                }
            }
        }
        if (!IsKeyDown(KEY_LEFT_SHIFT)) gs->build_mode.active = false;
    }
    if (IsMouseButtonPressed(MOUSE_RIGHT_BUTTON)) gs->build_mode.active = false;
}

static bool get_selected_rally_building(GameState *gs, UIState *ui, Building **out_b) {
    int lp = net_get_local_player();
    if (ui->sel_building < 0) return false;
    Building *b = &gs->buildings[ui->sel_building];
    if (!b->active || !b->complete || b->player != lp || !building_supports_rally(b->type)) return false;
    if (out_b) *out_b = b;
    return true;
}

static bool set_selected_rally_from_screen(GameState *gs, UIState *ui, Vector2 mp) {
    bool over_hud = mp.y < HUD_TOP_H || mp.y > GetScreenHeight() - HUD_BOT_H ||
                    (mp.x > GetScreenWidth() - MINI_SIZE - 16 && mp.y > GetScreenHeight() - HUD_BOT_H - 8);
    if (over_hud) return false;

    Building *b = NULL;
    if (!get_selected_rally_building(gs, ui, &b)) {
        ui->rally_mode = false;
        return false;
    }

    Vector2 wp = GetScreenToWorld2D(mp, ui->camera);
    Vector2 cart = to_rvec2(iso_to_world(wp.x, wp.y));
    int tx = (int)(cart.x / TILE_SIZE);
    int ty = (int)(cart.y / TILE_SIZE);
    if (!map_in_bounds(tx, ty)) return false;

    int rally_tx = tx;
    int rally_ty = ty;
    if (!map_find_passable_near(gs, tx, ty, &rally_tx, &rally_ty)) {
        game_set_alert(gs, "No room for a rally point there.");
        return true;
    }

    if (g_net_active) {
        NetPacket pkt = {0};
        pkt.type = PKT_SET_RALLY;
        pkt.player = net_get_local_player();
        pkt.target_id = ui->sel_building;
        pkt.tx = rally_tx;
        pkt.ty = rally_ty;
        net_dispatch_packet(gs, &pkt);
    } else {
        b->rally_tx = rally_tx;
        b->rally_ty = rally_ty;
    }

    ui->rally_mode = false;
    game_set_alert(gs, "Rally point set.");
    return true;
}

static void update_rally_mode(GameState *gs, UIState *ui) {
    if (!ui->rally_mode) return;

    Building *b = NULL;
    if (!get_selected_rally_building(gs, ui, &b)) {
        ui->rally_mode = false;
        return;
    }
    (void)b;

#if defined(PLATFORM_ANDROID) || defined(ANDROID)
    if (IsGestureDetected(GESTURE_TAP)) {
        Vector2 mp = GetTouchPointCount() > 0 ? GetTouchPosition(0) : GetMousePosition();
        set_selected_rally_from_screen(gs, ui, mp);
    }
#else
    if (IsMouseButtonPressed(MOUSE_RIGHT_BUTTON)) {
        ui->rally_mode = false;
        game_set_alert(gs, "Rally point canceled.");
        return;
    }
    if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) {
        set_selected_rally_from_screen(gs, ui, GetMousePosition());
    }
#endif
}

/* ─── Hotkeys ─────────────────────────────────────────────── */
static void update_hotkeys(GameState *gs, UIState *ui) {
    if (IsKeyPressed(KEY_ESCAPE)) {
        if (gs->build_mode.active) { gs->build_mode.active = false; return; }
        if (ui->rally_mode) { ui->rally_mode = false; game_set_alert(gs, "Rally point canceled."); return; }
        if (ui->build_panel_open) { ui->build_panel_open = false; return; }
        clear_selection(gs, ui);
    }

    if (gs->mode == GAME_MODE_SANDBOX) {
        int lp = net_get_local_player();
        int enemy = (gs->num_players > 1 && lp == 0) ? 1 : 0;
        if (IsKeyPressed(KEY_F1)) game_sandbox_add_resources(gs, lp, 1000);
        if (IsKeyPressed(KEY_F2)) game_sandbox_next_age(gs, lp);
        if (IsKeyPressed(KEY_F3)) game_sandbox_spawn_wave(gs, lp);
        if (IsKeyPressed(KEY_F4)) game_sandbox_spawn_wave(gs, enemy);
        if (IsKeyPressed(KEY_F5))
            game_sandbox_heal_selection(gs, lp, ui->sel_building, ui->sel_units, ui->sel_count);
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
    if (IsKeyPressed(KEY_G)) {
        if (ui->rally_mode) {
            ui->rally_mode = false;
            game_set_alert(gs, "Rally point canceled.");
            return;
        }
        Building *b = NULL;
        if (get_selected_rally_building(gs, ui, &b)) {
            ui->rally_mode = true;
            ui->build_panel_open = false;
            gs->build_mode.active = false;
            game_set_alert(gs, "Click the map to place the rally point.");
            return;
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
        if (IsKeyPressed(KEY_V) && cur_age >= 1) qt = BLD_STABLE;
        if (IsKeyPressed(KEY_K) && cur_age >= 1) qt = BLD_BLACKSMITH;
        if (IsKeyPressed(KEY_Y) && cur_age >= 1) qt = BLD_MARKET;
        if (IsKeyPressed(KEY_M)) qt = BLD_MILL;
        if (IsKeyPressed(KEY_L)) qt = BLD_LUMBER_CAMP;
        if (IsKeyPressed(KEY_N)) qt = BLD_MINING_CAMP;
        if (IsKeyPressed(KEY_F)) qt = BLD_FARM;
        if (IsKeyPressed(KEY_T) && cur_age >= 1) qt = BLD_WATCH_TOWER;
        if (IsKeyPressed(KEY_O) && cur_age >= 2) qt = BLD_MONASTERY;
        if (IsKeyPressed(KEY_I) && cur_age >= 2) qt = BLD_SIEGE_WORKSHOP;
        if (IsKeyPressed(KEY_C) && cur_age >= 2) qt = BLD_UNIVERSITY;
        if (IsKeyPressed(KEY_U)) qt = BLD_WALL;
        if (IsKeyPressed(KEY_J)) qt = BLD_GATE;
        if (qt != BLD_COUNT && res_can_afford(&gs->res[lp], building_cost(qt))) {
            gs->build_mode.type = qt;
            gs->build_mode.active = true;
            gs->build_mode.dragging = false;
            ui->build_panel_open = false;
            char msg[48];
            snprintf(msg, sizeof(msg), "Placing: %s", building_name(qt));
            game_set_alert(gs, msg);
        }
        if (IsKeyPressed(KEY_I) && cur_age < 2) game_set_alert(gs, "Siege Workshop requires Castle Age!");
        if (IsKeyPressed(KEY_C) && cur_age < 2) game_set_alert(gs, "University requires Castle Age!");
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
    if (ui->rally_mode) {
        update_rally_mode(gs, ui);
        return;
    }

#if !defined(PLATFORM_ANDROID) && !defined(ANDROID)
    if (IsMouseButtonPressed(MOUSE_RIGHT_BUTTON) && set_selected_rally_from_screen(gs, ui, GetMousePosition())) {
        return;
    }
#endif

#if defined(PLATFORM_ANDROID) || defined(ANDROID)
    /* On Android: GESTURE_TAP drives selection/commands; GESTURE_DRAG is panning (camera). */
    if (IsGestureDetected(GESTURE_TAP)) {
        handle_tap(gs, ui);
    }
#else
    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON))   handle_left_down(gs, ui);
    if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON))  handle_left_up(gs, ui);
#endif

    if (gs->phase == PHASE_VICTORY || gs->phase == PHASE_DEFEAT) {
        if (IsKeyPressed(KEY_Q)) CloseWindow();
        if (IsKeyPressed(KEY_R)) game_init(gs);
    }
}
