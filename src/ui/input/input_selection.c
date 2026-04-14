/*=============================================================
 * input_selection.c  –  Hit testing, selection helpers, left-click handlers
 *=============================================================*/
#include "game.h"
#include "ui_state.h"
#include <math.h>

#define MINI_SIZE 180

/* ─── Selection helpers ───────────────────────────────────── */
bool point_in_unit(Unit *u, Vector2 wp) {
    Vector2 p = to_rvec2(world_to_iso(u->wx, u->wy));
    return fabsf(p.x - wp.x) < 15 && fabsf((p.y - 10) - wp.y) < 15;
}
bool rect_intersects_unit(Unit *u, float x0, float y0, float x1, float y1) {
    Vector2 p = to_rvec2(world_to_iso(u->wx, u->wy));
    return p.x >= x0 && p.x <= x1 && p.y >= y0 && p.y <= y1;
}
void clear_selection(GameState *gs, UIState *ui) {
    for (int i = 0; i < ui->sel_count; i++)
        gs->units[ui->sel_units[i]].selected = false;
    ui->sel_count = 0;
    if (ui->sel_building >= 0) {
        gs->buildings[ui->sel_building].selected = false;
        ui->sel_building = -1;
    }
}
void select_unit(GameState *gs, UIState *ui, int uid) {
    if (ui->sel_count >= MAX_UNITS) return;
    gs->units[uid].selected = true;
    ui->sel_units[ui->sel_count++] = uid;
}

/* ─── World-hit testers ───────────────────────────────────── */
bool hit_building_iso(Building *b, Vector2 wp) {
    float bx = (float)b->tx * TILE_SIZE, by = (float)b->ty * TILE_SIZE;
    float bw = (float)b->tw * TILE_SIZE, bh = (float)b->th * TILE_SIZE;
    Vec2 c = iso_to_world(wp.x, wp.y);
    if (c.x >= bx && c.x <= bx + bw && c.y >= by && c.y <= by + bh) return true;
    Vector2 p = to_rvec2(world_to_iso(bx + bw * 0.5f, by + bh * 0.5f));
    float hit_w = (bw + bh) * 0.7f, hit_h = (bw + bh) * 0.5f + 30;
    return (wp.x >= p.x - hit_w / 2 && wp.x <= p.x + hit_w / 2 &&
            wp.y >= p.y - hit_h && wp.y <= p.y + hit_h / 4);
}

int find_friendly_unit_at(GameState *gs, Vector2 wp) {
    for (int i = 0; i < MAX_UNITS; i++) {
        Unit *u = &gs->units[i];
        if (!u->active || u->player != 0 || u->state == US_DEAD) continue;
        if (point_in_unit(u, wp)) return i;
    }
    return -1;
}
int find_friendly_building_at(GameState *gs, Vector2 wp) {
    for (int i = 0; i < MAX_BUILDINGS; i++) {
        Building *b = &gs->buildings[i];
        if (!b->active || b->player != 0) continue;
        if (hit_building_iso(b, wp)) return i;
    }
    return -1;
}
int find_enemy_unit_at(GameState *gs, Vector2 wp) {
    for (int i = 0; i < MAX_UNITS; i++) {
        Unit *u = &gs->units[i];
        if (!u->active || u->player != 1 || u->state == US_DEAD) continue;
        int utx = (int)(u->wx / TILE_SIZE), uty = (int)(u->wy / TILE_SIZE);
        if (!map_in_bounds(utx, uty)) continue;
        if (gs->map[uty][utx].fog[0] != FOG_VISIBLE) continue;
        if (point_in_unit(u, wp)) return i;
    }
    return -1;
}
int find_enemy_building_at(GameState *gs, Vector2 wp) {
    for (int i = 0; i < MAX_BUILDINGS; i++) {
        Building *b = &gs->buildings[i];
        if (!b->active || b->player != 1) continue;
        int bmx = clampi(b->tx, 0, MAP_W - 1), bmy = clampi(b->ty, 0, MAP_H - 1);
        if (gs->map[bmy][bmx].fog[0] == FOG_HIDDEN) continue;
        if (hit_building_iso(b, wp)) return i;
    }
    return -1;
}
int find_unfinished_building_at(GameState *gs, Vector2 wp) {
    for (int i = 0; i < MAX_BUILDINGS; i++) {
        Building *b = &gs->buildings[i];
        if (!b->active || b->player != 0 || b->complete) continue;
        if (hit_building_iso(b, wp)) return i;
    }
    return -1;
}
int find_friendly_dropoff_at(GameState *gs, Vector2 wp) {
    int fb = find_friendly_building_at(gs, wp);
    if (fb >= 0 && gs->buildings[fb].complete) {
        int t = gs->buildings[fb].type;
        if (t == BLD_TOWN_CENTER || t == BLD_MILL || t == BLD_LUMBER_CAMP || t == BLD_MINING_CAMP)
            return fb;
    }
    return -1;
}

/* ─── Issue context command ───────────────────────────────── */
void issue_command_at(GameState *gs, UIState *ui, Vector2 world) {
    Vector2 cart = to_rvec2(iso_to_world(world.x, world.y));
    int tx = (int)(cart.x / TILE_SIZE), ty = (int)(cart.y / TILE_SIZE);
    if (!map_in_bounds(tx, ty)) return;

    int eu = find_enemy_unit_at(gs, world);
    int eb = (eu < 0) ? find_enemy_building_at(gs, world) : -1;
    int ub = find_unfinished_building_at(gs, world);
    int dropoff = find_friendly_dropoff_at(gs, world);

    TileType tt = gs->map[ty][tx].type;
    bool is_resource = (tt == TILE_FOREST || tt == TILE_GOLD || tt == TILE_STONE || tt == TILE_BERRIES || tt == TILE_FARM);

    int ftx = tx, fty = ty;
    bool must_be_passable = (!is_resource && eu < 0 && eb < 0 && ub < 0 && dropoff < 0);
    if (must_be_passable) {
        if (!map_find_passable_near(gs, tx, ty, &ftx, &fty)) return;
    }

    int width = ui->sel_count < 5 ? ui->sel_count : 5;
    if (width < 1) width = 1;
    int height = (ui->sel_count + width - 1) / width;

    for (int i = 0; i < ui->sel_count; i++) {
        Unit *u = &gs->units[ui->sel_units[i]];
        if (!u->active || u->player != 0) continue;
        if (eu >= 0 || eb >= 0) {
            unit_give_attack_order(gs, u, eu, eb);
        } else if (ub >= 0 && u->type == UNIT_VILLAGER) {
            unit_give_build_order(gs, u, ub);
        } else if (dropoff >= 0 && u->type == UNIT_VILLAGER && u->carry_amt > 0) {
            unit_give_dropoff_order(gs, u, gs->buildings[dropoff].tx, gs->buildings[dropoff].ty);
        } else if (is_resource && u->type == UNIT_VILLAGER) {
            unit_give_gather_order(gs, u, tx, ty);
        } else {
            int col = i % width, row = i / width;
            int ox = col - (width / 2), oy = row - (height / 2);
            int ntx = clampi(ftx + ox, 0, MAP_W - 1), nty = clampi(fty + oy, 0, MAP_H - 1);
            unit_give_move_order(gs, u, ntx, nty);
        }
    }
}

/* ─── Left-click start ────────────────────────────────────── */
void handle_left_down(GameState *gs, UIState *ui) {
    Vector2 mp = GetMousePosition();
    bool over_hud = mp.y < 42 || mp.y > SCREEN_H - 130 ||
                    (mp.x > SCREEN_W - MINI_SIZE - 16 && mp.y > SCREEN_H - 130 - 8);
    if (over_hud) return;
    if (gs->build_mode.active || ui->build_panel_open) return;
    ui->box_selecting = true;
    ui->box_start = mp;
}

/* ─── Left-click release ──────────────────────────────────── */
void handle_left_up(GameState *gs, UIState *ui) {
    if (!ui->box_selecting) return;
    ui->box_selecting = false;
    Vector2 mp = GetMousePosition();
    Vector2 ws = GetScreenToWorld2D(ui->box_start, ui->camera);
    Vector2 we = GetScreenToWorld2D(mp, ui->camera);
    bool over_hud = mp.y < 42 || mp.y > SCREEN_H - 130 ||
                    (mp.x > SCREEN_W - MINI_SIZE - 16 && mp.y > SCREEN_H - 130 - 8);
    if (over_hud) return;

    float dx = fabsf(we.x - ws.x), dy = fabsf(we.y - ws.y);
    bool is_box = (dx > 10 || dy > 10);

    if (is_box) {
        bool shift = IsKeyDown(KEY_LEFT_SHIFT);
        if (!shift) clear_selection(gs, ui);
        float x0 = ws.x < we.x ? ws.x : we.x, x1 = ws.x > we.x ? ws.x : we.x;
        float y0 = ws.y < we.y ? ws.y : we.y, y1 = ws.y > we.y ? ws.y : we.y;
        for (int i = 0; i < MAX_UNITS; i++) {
            Unit *u = &gs->units[i];
            if (!u->active || u->player != 0 || u->state == US_DEAD) continue;
            if (rect_intersects_unit(u, x0, y0, x1, y1)) select_unit(gs, ui, i);
        }
        return;
    }

    bool shift = IsKeyDown(KEY_LEFT_SHIFT);
    int fu = find_friendly_unit_at(gs, we);
    int fb = find_friendly_building_at(gs, we);
    int dropoff = find_friendly_dropoff_at(gs, we);

    bool is_villager_carrying = false, has_villagers = false;
    for (int i = 0; i < ui->sel_count; i++) {
        if (gs->units[ui->sel_units[i]].type == UNIT_VILLAGER) {
            has_villagers = true;
            if (gs->units[ui->sel_units[i]].carry_amt > 0) is_villager_carrying = true;
        }
    }

    if (fu >= 0) {
        if (!shift) clear_selection(gs, ui);
        select_unit(gs, ui, fu);
        ui->sel_tile_x = -1; ui->sel_tile_y = -1;
    } else if (is_villager_carrying && dropoff >= 0) {
        issue_command_at(gs, ui, we);
    } else if (fb >= 0 && gs->buildings[fb].complete) {
        if (has_villagers && gs->buildings[fb].type == BLD_FARM) {
            issue_command_at(gs, ui, we);
        } else {
            clear_selection(gs, ui);
            ui->sel_building = fb;
            gs->buildings[fb].selected = true;
            ui->sel_tile_x = -1; ui->sel_tile_y = -1;
        }
    } else if (ui->sel_count > 0) {
        issue_command_at(gs, ui, we);
        Vector2 cart = to_rvec2(iso_to_world(we.x, we.y));
        int tx = (int)(cart.x / TILE_SIZE), ty = (int)(cart.y / TILE_SIZE);
        if (map_in_bounds(tx, ty)) {
            TileType tt = gs->map[ty][tx].type;
            if (tt == TILE_FOREST || tt == TILE_GOLD || tt == TILE_STONE || tt == TILE_BERRIES || tt == TILE_FARM)
                { ui->sel_tile_x = tx; ui->sel_tile_y = ty; }
            else { ui->sel_tile_x = -1; ui->sel_tile_y = -1; }
        }
    } else {
        Vector2 cart = to_rvec2(iso_to_world(we.x, we.y));
        int tx = (int)(cart.x / TILE_SIZE), ty = (int)(cart.y / TILE_SIZE);
        if (map_in_bounds(tx, ty)) {
            TileType tt = gs->map[ty][tx].type;
            if (tt == TILE_FOREST || tt == TILE_GOLD || tt == TILE_STONE || tt == TILE_BERRIES || tt == TILE_FARM)
                { ui->sel_tile_x = tx; ui->sel_tile_y = ty; }
            else { ui->sel_tile_x = -1; ui->sel_tile_y = -1; clear_selection(gs, ui); }
        }
    }
}

/* ─── Hover detection ─────────────────────────────────────── */
void update_hover(GameState *gs, UIState *ui) {
    Vector2 mp = GetMousePosition();
    bool over_hud = mp.y < 42 || mp.y > SCREEN_H - 130 ||
                    (mp.x > SCREEN_W - MINI_SIZE - 16 && mp.y > SCREEN_H - 130 - 8);
    ui->hover_unit = -1; ui->hover_building = -1;
    ui->hover_tile_x = -1; ui->hover_tile_y = -1;
    if (over_hud || gs->phase != PHASE_PLAYING) return;
    Vector2 wp = GetScreenToWorld2D(mp, ui->camera);
    int u = find_friendly_unit_at(gs, wp);
    if (u < 0) u = find_enemy_unit_at(gs, wp);
    if (u >= 0) { ui->hover_unit = u; return; }
    int b = find_friendly_building_at(gs, wp);
    if (b < 0) b = find_enemy_building_at(gs, wp);
    if (b >= 0) { ui->hover_building = b; return; }
    Vector2 cart = to_rvec2(iso_to_world(wp.x, wp.y));
    int tx = (int)(cart.x / TILE_SIZE), ty = (int)(cart.y / TILE_SIZE);
    if (map_in_bounds(tx, ty) && gs->map[ty][tx].fog[0] == FOG_VISIBLE) {
        TileType tt = gs->map[ty][tx].type;
        if (tt == TILE_FOREST || tt == TILE_GOLD || tt == TILE_STONE || tt == TILE_BERRIES || tt == TILE_FARM)
            { ui->hover_tile_x = tx; ui->hover_tile_y = ty; }
    }
}
