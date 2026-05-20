/*=============================================================
 * input_commands.c  –  Hotkeys, build mode, hero possession
 *                       controls, and the master input_update
 *
 * Coordinates keyboard/mouse/touch input and dispatches to the
 * appropriate subsystem: build placement, rally points, hero
 * movement, attack-move, and general hotkeys.
 *=============================================================*/
#include "game.h"
#include "ui_state.h"
#include <math.h>
#include <stdio.h>
#include "net.h"
#include "hud_common.h"   /* HUD_TOP_H, HUD_BOT_H */

/* ── Hero possession tuning ────────────────────────────────── */
static const float HERO_MOUSE_SENS_YAW   = 0.0032f;
static const float HERO_MOUSE_SENS_PITCH = 0.0024f;
static const float HERO_PITCH_MIN        = -0.62f;
static const float HERO_PITCH_MAX        =  0.48f;
static const float HERO_SPEED_BOOST      = 1.15f;
static const float HERO_BLOCK_SPEED_MULT = 0.42f;
static const float HERO_DODGE_SPEED_MULT = 3.1f;
static const float HERO_DODGE_DURATION   = 0.28f;
static const float HERO_DODGE_COOLDOWN   = 1.1f;
static const float HERO_DODGE_STAM_COST  = 24.0f;
static const float HERO_BLOCK_DRAIN_RATE = 34.0f;
static const float HERO_STAM_REGEN_RATE  = 20.0f;
static const float HERO_MAX_STAMINA      = 100.0f;
static const float HERO_RECOIL_SHAKE     = 0.15f;

/* Forward declarations from input split files */
extern void update_camera(GameState *gs, UIState *ui, float dt);
extern void update_hover(GameState *gs, UIState *ui);
extern void clear_selection(GameState *gs, UIState *ui);
extern void handle_left_down(GameState *gs, UIState *ui);
extern void handle_left_up(GameState *gs, UIState *ui);
extern void handle_tap(GameState *gs, UIState *ui);

static int selected_hero_candidate(GameState *gs, UIState *ui) {
    int lp = net_get_local_player();
    if (ui->sel_count != 1) return -1;
    int uid = ui->sel_units[0];
    if (!hero_possession_is_unit_eligible(gs, uid, lp)) return -1;
    return uid;
}

static bool try_start_selected_hero_possession(GameState *gs, UIState *ui) {
    int uid = selected_hero_candidate(gs, ui);
    if (uid < 0) return false;
    if (g_net_active) {
        game_set_alert(gs, "Hero Possession is available in solo play only.");
        return true;
    }
    if (hero_possession_start(gs, uid, net_get_local_player())) {
        ui_sync_hero_possession(ui, gs);
        return true;
    }
    if (gs->hero.cooldown_timer > 0.0f) {
        char msg[80];
        snprintf(msg, sizeof(msg), "Hero Possession cooling down: %.0fs.", gs->hero.cooldown_timer);
        game_set_alert(gs, msg);
        return true;
    }
    return false;
}

static bool hero_try_move(GameState *gs, Unit *u, float dx, float dy) {
    float nx = u->wx + dx;
    float ny = u->wy + dy;
    int tx = (int)(nx / TILE_SIZE);
    int ty = (int)(ny / TILE_SIZE);
    if (!map_in_bounds(tx, ty) || !map_is_passable(gs, tx, ty)) return false;
    u->wx = clampf(nx, 1.0f, (MAP_W * TILE_SIZE) - 1.0f);
    u->wy = clampf(ny, 1.0f, (MAP_H * TILE_SIZE) - 1.0f);
    return true;
}

static float hero_building_distance_tiles(Unit *u, Building *b) {
    float bx1 = b->tx * TILE_SIZE;
    float by1 = b->ty * TILE_SIZE;
    float bx2 = bx1 + b->tw * TILE_SIZE;
    float by2 = by1 + b->th * TILE_SIZE;
    float cx = clampf(u->wx, bx1, bx2);
    float cy = clampf(u->wy, by1, by2);
    return dist2f(u->wx, u->wy, cx, cy) / TILE_SIZE;
}

static int hero_find_unit_in_aim(GameState *gs, Unit *u, float yaw, float range_tiles, float cone_cos) {
    float fx = cosf(yaw);
    float fy = sinf(yaw);
    int best = -1;
    float best_score = 99999.0f;
    int fallback = -1;
    float fallback_d = range_tiles;

    for (int i = 0; i < MAX_UNITS; i++) {
        Unit *t = &gs->units[i];
        if (!t->active || t->player == u->player || t->state == US_DEAD || t->state == US_DYING) continue;
        float dx = t->wx - u->wx;
        float dy = t->wy - u->wy;
        float dist_px = sqrtf(dx * dx + dy * dy);
        if (dist_px < 1.0f) continue;
        float dist_tiles = dist_px / TILE_SIZE;
        if (dist_tiles > range_tiles) continue;
        float dot = (dx / dist_px) * fx + (dy / dist_px) * fy;
        if (dist_tiles < fallback_d) {
            fallback = i;
            fallback_d = dist_tiles;
        }
        if (dot < cone_cos) continue;

        float aim_penalty = (1.0f - dot) * 7.0f;
        float score = dist_tiles + aim_penalty;
        if (score < best_score) {
            best = i;
            best_score = score;
        }
    }

    return best >= 0 ? best : fallback;
}

static int hero_find_building_in_aim(GameState *gs, Unit *u, float yaw,
                                     float range_tiles, float cone_cos, float *out_dist) {
    float fx = cosf(yaw);
    float fy = sinf(yaw);
    int best = -1;
    float best_d = range_tiles;

    for (int i = 0; i < MAX_BUILDINGS; i++) {
        Building *b = &gs->buildings[i];
        if (!b->active || b->player == u->player || !b->complete) continue;
        float cx = (b->tx + b->tw * 0.5f) * TILE_SIZE;
        float cy = (b->ty + b->th * 0.5f) * TILE_SIZE;
        float dx = cx - u->wx;
        float dy = cy - u->wy;
        float dist_px = sqrtf(dx * dx + dy * dy);
        if (dist_px < 1.0f) continue;
        float dist_tiles = hero_building_distance_tiles(u, b);
        if (dist_tiles > best_d) continue;
        float dot = (dx / dist_px) * fx + (dy / dist_px) * fy;
        if (dot < cone_cos) continue;
        best = i;
        best_d = dist_tiles;
    }

    if (out_dist) *out_dist = best_d;
    return best;
}

static void hero_perform_attack(GameState *gs, UIState *ui, Unit *u) {
    if (gs->hero.attack_timer > 0.0f) return;

    bool ranged = unit_uses_projectiles(u->type);
    float range = ranged ? clampf(u->attack_range + 3.0f, 6.0f, 11.5f) : 2.35f;
    float cone = ranged ? 0.72f : -0.15f;
    int target_unit = hero_find_unit_in_aim(gs, u, gs->hero.yaw, range, cone);
    float target_dist = range;
    int target_bld = -1;

    if (target_unit < 0) {
        target_bld = hero_find_building_in_aim(gs, u, gs->hero.yaw, range, cone, &target_dist);
    } else {
        Unit *t = &gs->units[target_unit];
        target_dist = dist2f(u->wx, u->wy, t->wx, t->wy) / TILE_SIZE;
    }

    int dmg = (int)ceilf((float)u->attack_dmg * (ranged ? 2.8f : 2.25f)) + (ranged ? 3 : 5);
    if (gs->hero.dodge_timer > 0.0f) dmg += 4;

    if (target_unit >= 0) {
        Unit *t = &gs->units[target_unit];
        int final_dmg = dmg - t->armor;
        if (final_dmg < 1) final_dmg = 1;
        if (ranged) {
            float spawn_x = u->wx + cosf(gs->hero.yaw) * TILE_SIZE * 0.5f;
            float spawn_y = u->wy + sinf(gs->hero.yaw) * TILE_SIZE * 0.5f;
            game_spawn_projectile(gs, u->player, PROJ_ARROW,
                                  spawn_x, spawn_y, t->wx, t->wy,
                                  target_unit, -1, final_dmg,
                                  clampf(0.08f + target_dist * 0.045f, 0.12f, 0.42f),
                                  24.0f);
        } else {
            game_damage_unit(gs, target_unit, final_dmg);
        }
        gs->hero.shake = 0.15f; // Small recoil
    } else if (target_bld >= 0) {
        Building *b = &gs->buildings[target_bld];
        int final_dmg = dmg + (ranged ? 0 : 2);
        if (ranged) {
            float bx = (b->tx + b->tw * 0.5f) * TILE_SIZE;
            float by = (b->ty + b->th * 0.5f) * TILE_SIZE;
            float spawn_x = u->wx + cosf(gs->hero.yaw) * TILE_SIZE * 0.5f;
            float spawn_y = u->wy + sinf(gs->hero.yaw) * TILE_SIZE * 0.5f;
            game_spawn_projectile(gs, u->player, PROJ_ARROW,
                                  spawn_x, spawn_y, bx, by,
                                  -1, target_bld, final_dmg,
                                  clampf(0.08f + target_dist * 0.045f, 0.12f, 0.42f),
                                  22.0f);
        } else {
            game_damage_building(gs, target_bld, final_dmg);
        }
        gs->hero.shake = 0.15f; // Small recoil
    } else {
        if (ranged) {
            float spawn_x = u->wx + cosf(gs->hero.yaw) * TILE_SIZE * 0.5f;
            float spawn_y = u->wy + sinf(gs->hero.yaw) * TILE_SIZE * 0.5f;
            float miss_tx = u->wx + cosf(gs->hero.yaw) * TILE_SIZE * range;
            float miss_ty = u->wy + sinf(gs->hero.yaw) * TILE_SIZE * range;
            
            game_spawn_projectile(gs, u->player, PROJ_ARROW,
                                  spawn_x, spawn_y, miss_tx, miss_ty,
                                  -1, -1, 0, // 0 damage, no target
                                  0.4f, // 0.4s duration for max range
                                  18.0f); // slight arc
        }
        gs->hero.shake = 0.12f;
    }

    gs->hero.blur = 0.38f;
    gs->hero.attack_timer = clampf(u->attack_cd * 0.55f, 0.35f, 1.1f);
    ui_play_hero_attack(ui);
}

static void hero_perform_interact(GameState *gs, UIState *ui, Unit *u) {
    float fx = cosf(gs->hero.yaw);
    float fy = sinf(gs->hero.yaw);
    int tx = (int)((u->wx + fx * TILE_SIZE * 1.25f) / TILE_SIZE);
    int ty = (int)((u->wy + fy * TILE_SIZE * 1.25f) / TILE_SIZE);
    if (!map_in_bounds(tx, ty)) return;

    Tile *tile = &gs->map[ty][tx];
    bool resource = tile->type == TILE_FOREST || tile->type == TILE_GOLD ||
                    tile->type == TILE_STONE || tile->type == TILE_BERRIES ||
                    tile->type == TILE_FARM;
    if (resource && tile->resource_amt > 0) {
        tile->resource_amt -= u->attack_dmg * 5;
        if (tile->resource_amt <= 0 && tile->type != TILE_FARM) {
            tile->resource_amt = 0;
            tile->type = TILE_GRASS;
        }
        gs->hero.shake = 0.22f;
        gs->hero.blur = 0.25f;
        game_set_alert(gs, "Hero interaction: obstacle cleared.");
        ui_play_hero_attack(ui);
        return;
    }

    float dist = 2.1f;
    int b = hero_find_building_in_aim(gs, u, gs->hero.yaw, 2.1f, 0.0f, &dist);
    if (b >= 0) {
        game_damage_building(gs, b, u->attack_dmg + 4);
        gs->hero.shake = 0.28f;
        gs->hero.blur = 0.22f;
        game_set_alert(gs, "Hero interaction: breached enemy structure.");
        ui_play_hero_attack(ui);
    }
}

static void update_hero_possession_controls(GameState *gs, UIState *ui, float dt)
{
    ui_sync_hero_possession(ui, gs);
    if (gs->hero.unit_id < 0 || gs->hero.unit_id >= MAX_UNITS) return;

    Unit *u = &gs->units[gs->hero.unit_id];
    if (!u->active || u->state == US_DEAD || u->state == US_DYING) return;

    if (IsKeyPressed(KEY_ESCAPE)) {
        hero_possession_request_exit(gs, "Hero Possession canceled: returning to command view.");
        ui_sync_hero_possession(ui, gs);
        return;
    }

    /* Mouse-look */
    Vector2 md = GetMouseDelta();
    gs->hero.yaw  += md.x * HERO_MOUSE_SENS_YAW;
    gs->hero.pitch = clampf(gs->hero.pitch - md.y * HERO_MOUSE_SENS_PITCH,
                            HERO_PITCH_MIN, HERO_PITCH_MAX);
    u->facing = gs->hero.yaw;

    if (gs->hero.phase != HERO_POSSESSION_ACTIVE) return;

    /* Movement input */
    float forward = 0.0f, strafe = 0.0f;
    if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP))   forward += 1.0f;
    if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)) forward -= 1.0f;
    if (IsKeyDown(KEY_D)) strafe += 1.0f;
    if (IsKeyDown(KEY_A)) strafe -= 1.0f;

    /* Blocking drains stamina while held. */
    bool blocking = (IsMouseButtonDown(MOUSE_BUTTON_RIGHT) || IsKeyDown(KEY_Q)) &&
                    gs->hero.stamina > 1.0f && gs->hero.dodge_timer <= 0.0f;
    if (blocking) {
        gs->hero.block_timer = 0.12f;
        gs->hero.stamina -= HERO_BLOCK_DRAIN_RATE * dt;
        if (gs->hero.stamina < 0.0f) gs->hero.stamina = 0.0f;
        if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) || IsKeyPressed(KEY_Q))
            ui_play_hero_block(ui);
    } else {
        gs->hero.stamina += HERO_STAM_REGEN_RATE * dt;
        if (gs->hero.stamina > HERO_MAX_STAMINA) gs->hero.stamina = HERO_MAX_STAMINA;
    }

    /* Dodge roll */
    bool dodge_pressed = IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_LEFT_SHIFT);
    if (dodge_pressed && gs->hero.dodge_cooldown <= 0.0f &&
        gs->hero.stamina >= HERO_DODGE_STAM_COST) {
        gs->hero.dodge_timer    = HERO_DODGE_DURATION;
        gs->hero.dodge_cooldown = HERO_DODGE_COOLDOWN;
        gs->hero.stamina       -= HERO_DODGE_STAM_COST;
        gs->hero.shake = 0.32f;
        gs->hero.blur  = 0.85f;
        if (fabsf(forward) < 0.1f && fabsf(strafe) < 0.1f) forward = 1.0f;
    }

    /* Attack & interact */
    if (!blocking && (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsKeyPressed(KEY_F)))
        hero_perform_attack(gs, ui, u);
    if (IsKeyPressed(KEY_E))
        hero_perform_interact(gs, ui, u);

    /* Apply movement in the hero's local frame. */
    float fx = cosf(gs->hero.yaw),  fy = sinf(gs->hero.yaw);
    float rx = -sinf(gs->hero.yaw), ry = cosf(gs->hero.yaw);
    float mx = fx * forward + rx * strafe;
    float my = fy * forward + ry * strafe;
    float mag = sqrtf(mx * mx + my * my);
    if (mag > 0.001f) {
        mx /= mag;
        my /= mag;
        float speed = u->move_speed * HERO_SPEED_BOOST;
        if (blocking)                    speed *= HERO_BLOCK_SPEED_MULT;
        if (gs->hero.dodge_timer > 0.0f) speed *= HERO_DODGE_SPEED_MULT;
        float step_x = mx * speed * dt;
        float step_y = my * speed * dt;
        /* Axis-separated collision: try full step first, then 25% slide. */
        if (!hero_try_move(gs, u, step_x, 0.0f)) hero_try_move(gs, u, step_x * 0.25f, 0.0f);
        if (!hero_try_move(gs, u, 0.0f, step_y)) hero_try_move(gs, u, 0.0f, step_y * 0.25f);
        gs->hero.blur = fmaxf(gs->hero.blur, gs->hero.dodge_timer > 0.0f ? 0.65f : 0.18f);
    }
}

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
    if (IsKeyPressed(KEY_E)) {
        if (try_start_selected_hero_possession(gs, ui)) return;
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
        if (IsKeyPressed(KEY_X) && cur_age >= 2) qt = BLD_CASTLE;
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
        if (IsKeyPressed(KEY_X) && cur_age < 2) game_set_alert(gs, "Castle requires Castle Age!");
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
                building_sell(gs, ui->sel_building);
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

    /* ── Idle villager cycling (Period / '.' key) ────────────── */
    if (IsKeyPressed(KEY_PERIOD)) {
        int lp = net_get_local_player();
        int start = ui->idle_villager_last + 1;
        int found = -1;
        /* Search from last index forward, wrapping around */
        for (int pass = 0; pass < 2 && found < 0; pass++) {
            int begin = (pass == 0) ? start : 0;
            int end   = (pass == 0) ? MAX_UNITS : start;
            for (int i = begin; i < end; i++) {
                Unit *u = &gs->units[i];
                if (u->active && u->player == lp && u->type == UNIT_VILLAGER &&
                    u->state == US_IDLE && u->state != US_DEAD && u->state != US_DYING) {
                    found = i;
                    break;
                }
            }
        }
        if (found >= 0) {
            clear_selection(gs, ui);
            ui->sel_count = 1;
            ui->sel_units[0] = found;
            gs->units[found].selected = true;
            ui->idle_villager_last = found;
            /* Center camera on the villager */
            Unit *u = &gs->units[found];
            Vec2 iso = world_to_iso(u->wx, u->wy);
            ui->camera.target = (Vector2){iso.x, iso.y};
            game_set_alert(gs, "Idle Villager selected.");
        } else {
            game_set_alert(gs, "No idle villagers.");
        }
    }

    /* ── Select all military (Ctrl+Shift+A) ─────────────────── */
    if (IsKeyDown(KEY_LEFT_CONTROL) && IsKeyDown(KEY_LEFT_SHIFT) && IsKeyPressed(KEY_A)) {
        int lp = net_get_local_player();
        clear_selection(gs, ui);
        ui->sel_count = 0;
        for (int i = 0; i < MAX_UNITS && ui->sel_count < MAX_UNITS; i++) {
            Unit *u = &gs->units[i];
            if (!u->active || u->player != lp) continue;
            if (u->state == US_DEAD || u->state == US_DYING) continue;
            if (u->type == UNIT_VILLAGER) continue;
            u->selected = true;
            ui->sel_units[ui->sel_count++] = i;
        }
        if (ui->sel_count > 0) {
            char msg[48];
            snprintf(msg, sizeof(msg), "%d military units selected.", ui->sel_count);
            game_set_alert(gs, msg);
        } else {
            game_set_alert(gs, "No military units available.");
        }
    }

    /* ── Game speed controls (+/- keys) ──────────────────────── */
    if (IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD)) {  /* + key */
        gs->game_speed = clampf(gs->game_speed + 0.5f, 0.5f, 3.0f);
        char msg[32];
        snprintf(msg, sizeof(msg), "Speed: %.1fx", gs->game_speed);
        game_set_alert(gs, msg);
    }
    if (IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT)) {  /* - key */
        gs->game_speed = clampf(gs->game_speed - 0.5f, 0.5f, 3.0f);
        char msg[32];
        snprintf(msg, sizeof(msg), "Speed: %.1fx", gs->game_speed);
        game_set_alert(gs, msg);
    }

    /* ── Attack-move (A key when military selected, not building) ── */
    if (IsKeyPressed(KEY_A) && !IsKeyDown(KEY_LEFT_CONTROL) && !IsKeyDown(KEY_LEFT_SHIFT)) {
        bool has_military = false;
        bool has_villager = false;
        for (int i = 0; i < ui->sel_count; i++) {
            Unit *u = &gs->units[ui->sel_units[i]];
            if (u->type == UNIT_VILLAGER) has_villager = true;
            else has_military = true;
        }
        /* Only activate attack-move if military units selected, no villagers with A key for building */
        if (has_military && !has_villager) {
            ui->attack_move_mode = true;
            game_set_alert(gs, "Attack-move: click a destination.");
        }
    }
}

/* ─── Master input update ─────────────────────────────────── */
void input_update(GameState *gs, UIState *ui) {
    float dt = GetFrameTime();
    ui_sync_hero_possession(ui, gs);

    if (gs->mode == GAME_MODE_CAMPAIGN && gs->campaign_briefing_open && gs->phase == PHASE_PLAYING) {
        bool dismiss = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) ||
                       IsKeyPressed(KEY_SPACE) || IsMouseButtonPressed(MOUSE_LEFT_BUTTON);
#if defined(PLATFORM_ANDROID) || defined(ANDROID)
        dismiss = dismiss || IsGestureDetected(GESTURE_TAP);
#endif
        if (dismiss) {
            gs->campaign_briefing_open = false;
            game_set_alert(gs, "Mission underway.");
        }
        return;
    }

    if (gs->hero.phase != HERO_POSSESSION_OFF) {
        update_hero_possession_controls(gs, ui, dt);
        return;
    }

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
        if (gs->mode == GAME_MODE_CAMPAIGN) {
            if (IsKeyPressed(KEY_Q)) game_init(gs);
            if (IsKeyPressed(KEY_R)) game_campaign_restart(gs);
            if (gs->phase == PHASE_VICTORY && IsKeyPressed(KEY_N)) game_campaign_advance(gs);
        } else {
            if (IsKeyPressed(KEY_Q)) CloseWindow();
            if (IsKeyPressed(KEY_R)) game_init(gs);
        }
    }
}
