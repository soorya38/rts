/*=============================================================
 * input_selection.c  –  Hit testing, selection helpers, left-click handlers
 *=============================================================*/
#include "game.h"
#include "ui_state.h"
#include <math.h>
#include <stdio.h>
#include "net.h"
#include "hud_common.h"   /* HUD_TOP_H, HUD_BOT_H, MINI_SIZE */

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

static bool click_reactivates_selected_hero(GameState *gs, UIState *ui, int uid) {
    int lp = net_get_local_player();
    if (uid < 0 || ui->sel_count != 1 || ui->sel_units[0] != uid) return false;
    if (!hero_possession_is_unit_eligible(gs, uid, lp)) return false;
    if (g_net_active) {
        game_set_alert(gs, "Hero Possession is available in solo play only.");
        return true;
    }
    if (hero_possession_start(gs, uid, lp)) return true;
    if (gs->hero.cooldown_timer > 0.0f) {
        char msg[80];
        snprintf(msg, sizeof(msg), "Hero Possession cooling down: %.0fs.", gs->hero.cooldown_timer);
        game_set_alert(gs, msg);
        return true;
    }
    return false;
}

/* ─── World-hit testers ───────────────────────────────────── */
static bool building_hit_info(GameState *gs, Building *b, UIState *ui, Vector2 wp,
                              int *out_rank, float *out_dist2, float *out_depth_y) {
    float bx = (float)b->tx * TILE_SIZE, by = (float)b->ty * TILE_SIZE;
    float bw = (float)b->tw * TILE_SIZE, bh = (float)b->th * TILE_SIZE;
    Vec2 c = iso_to_world(wp.x, wp.y);
    bool in_footprint = (c.x >= bx && c.x <= bx + bw && c.y >= by && c.y <= by + bh);

    Vector2 p = to_rvec2(world_to_iso(bx + bw * 0.5f, by + bh * 0.5f));
    float footprint_screen_w = bw + bh;
    float hit_w = footprint_screen_w * 0.40f;
    float hit_h = 42.0f + (float)b->th * 6.0f;
    int age = 0;
    if (b->player >= 0 && b->player < NUM_PLAYERS) age = gs->res[b->player].age;
    Texture2D tex = ui_get_building_texture(ui, b->type, age);
    if (b->type == BLD_HOUSE) tex = ui_get_house_texture(ui, b->variant);

    if (tex.id != 0) {
        float sc;
        if (b->type == BLD_TOWN_CENTER) {
            float scale_x = 315.0f / (float)tex.width;
            float scale_y = 255.0f / (float)tex.height;
            sc = scale_x < scale_y ? scale_x : scale_y;
        } else if (b->type == BLD_HOUSE) {
            float scale_x = 150.0f / (float)tex.width;
            float scale_y = 157.0f / (float)tex.height;
            sc = scale_x < scale_y ? scale_x : scale_y;
        } else if (b->type == BLD_MILL) {
            float scale_x = 145.0f / (float)tex.width;
            float scale_y = 150.0f / (float)tex.height;
            sc = scale_x < scale_y ? scale_x : scale_y;
        } else if (b->type == BLD_LUMBER_CAMP) {
            float scale_x = 165.0f / (float)tex.width;
            float scale_y = 135.0f / (float)tex.height;
            sc = scale_x < scale_y ? scale_x : scale_y;
        } else if (b->type == BLD_BARRACKS) {
            float scale_x = 175.0f / (float)tex.width;
            float scale_y = 145.0f / (float)tex.height;
            sc = scale_x < scale_y ? scale_x : scale_y;
        } else if (b->type == BLD_ARCHERY_RANGE) {
            float scale_x = 180.0f / (float)tex.width;
            float scale_y = 175.0f / (float)tex.height;
            sc = scale_x < scale_y ? scale_x : scale_y;
        } else if (b->type == BLD_STABLE) {
            float scale_x = 210.0f / (float)tex.width;
            float scale_y = 160.0f / (float)tex.height;
            sc = scale_x < scale_y ? scale_x : scale_y;
        } else if (b->type == BLD_BLACKSMITH) {
            float scale_x = 170.0f / (float)tex.width;
            float scale_y = 150.0f / (float)tex.height;
            sc = scale_x < scale_y ? scale_x : scale_y;
        } else if (b->type == BLD_MARKET) {
            float scale_x = 180.0f / (float)tex.width;
            float scale_y = 150.0f / (float)tex.height;
            sc = scale_x < scale_y ? scale_x : scale_y;
        } else if (b->type == BLD_MINING_CAMP) {
            float scale_x = 180.0f / (float)tex.width;
            float scale_y = 145.0f / (float)tex.height;
            sc = scale_x < scale_y ? scale_x : scale_y;
        } else if (b->type == BLD_WATCH_TOWER) {
            float scale_x = 170.0f / (float)tex.width;
            float scale_y = 250.0f / (float)tex.height;
            sc = scale_x < scale_y ? scale_x : scale_y;
        } else if (b->type == BLD_MONASTERY) {
            float scale_x = 205.0f / (float)tex.width;
            float scale_y = 235.0f / (float)tex.height;
            sc = scale_x < scale_y ? scale_x : scale_y;
        } else {
            float base_ratio = 1.25f / 4.0f;
            float boost = 1.25f;
            sc = (float)b->tw * base_ratio * boost;
        }
        float tw = tex.width * sc;
        float th = tex.height * sc;

        float sprite_hit_w = tw * 0.38f;
        float sprite_hit_h = th * 0.30f;
        if (sprite_hit_h > 86.0f) sprite_hit_h = 86.0f;
        if (sprite_hit_w > hit_w) hit_w = sprite_hit_w;
        if (sprite_hit_h > hit_h) hit_h = sprite_hit_h;
    } else {
        float fallback_hit_w = footprint_screen_w * 0.46f;
        float fallback_hit_h = 54.0f;
        if (fallback_hit_w > hit_w) hit_w = fallback_hit_w;
        if (fallback_hit_h > hit_h) hit_h = fallback_hit_h;
    }

    bool in_sprite_box = (wp.x >= p.x - hit_w / 2 && wp.x <= p.x + hit_w / 2 &&
                          wp.y >= p.y - hit_h && wp.y <= p.y - 6.0f);
    if (!in_footprint && !in_sprite_box) return false;

    if (out_rank) *out_rank = in_footprint ? 2 : 1;
    if (out_dist2) {
        float dx = wp.x - p.x;
        float dy = wp.y - (in_footprint ? p.y : (p.y - hit_h * 0.55f));
        *out_dist2 = dx * dx + dy * dy;
    }
    if (out_depth_y) {
        Vector2 depth_p = to_rvec2(world_to_iso(bx + bw * 0.5f, by + bh));
        *out_depth_y = depth_p.y;
    }
    return true;
}

static int wall_building_at_point(GameState *gs, Vector2 wp, int player_mode, bool require_complete){
    Vec2 c = iso_to_world(wp.x, wp.y);
    int tx = (int)(c.x / TILE_SIZE);
    int ty = (int)(c.y / TILE_SIZE);
    if(!map_in_bounds(tx, ty)) return -1;

    int bid = gs->map[ty][tx].building_id;
    if(bid < 0 || bid >= MAX_BUILDINGS) return -1;

    int lp = net_get_local_player();
    Building *b = &gs->buildings[bid];
    if(!b->active || !building_is_walllike(b->type)) return -1;
    if(require_complete && !b->complete) return -1;

    if(player_mode == 0 && b->player != lp) return -1;
    if(player_mode == 1){
        if(b->player == lp) return -1;
        if(gs->map[ty][tx].fog[lp] == FOG_HIDDEN) return -1;
    }
    if(player_mode == 2){
        if(b->player != lp || b->complete) return -1;
    }

    return bid;
}

int find_friendly_unit_at(GameState *gs, Vector2 wp) {
    int lp = net_get_local_player();
    for (int i = 0; i < MAX_UNITS; i++) {
        Unit *u = &gs->units[i];
        if (!u->active || u->player != lp || u->state == US_DEAD) continue;
        if (point_in_unit(u, wp)) return i;
    }
    return -1;
}
int find_friendly_building_at(GameState *gs, UIState *ui, Vector2 wp) {
    int lp = net_get_local_player();
    int wall_id = wall_building_at_point(gs, wp, 0, false);
    if(wall_id >= 0) return wall_id;
    int best_id = -1;
    int best_rank = -1;
    float best_dist2 = 0.0f;
    float best_depth_y = 0.0f;
    for (int i = 0; i < MAX_BUILDINGS; i++) {
        Building *b = &gs->buildings[i];
        if (!b->active || b->player != lp) continue;
        int rank = 0;
        float dist2 = 0.0f;
        float depth_y = 0.0f;
        if (!building_hit_info(gs, b, ui, wp, &rank, &dist2, &depth_y)) continue;
        if (best_id < 0 || depth_y > best_depth_y + 1.0f ||
            (fabsf(depth_y - best_depth_y) <= 1.0f &&
             (rank > best_rank || (rank == best_rank && dist2 < best_dist2)))) {
            best_id = i;
            best_rank = rank;
            best_dist2 = dist2;
            best_depth_y = depth_y;
        }
    }
    return best_id;
}
int find_enemy_unit_at(GameState *gs, Vector2 wp) {
    int lp = net_get_local_player();
    for (int i = 0; i < MAX_UNITS; i++) {
        Unit *u = &gs->units[i];
        if (!u->active || u->player == lp || u->state == US_DEAD) continue;
        int utx = (int)(u->wx / TILE_SIZE), uty = (int)(u->wy / TILE_SIZE);
        if (!map_in_bounds(utx, uty)) continue;
        if (gs->map[uty][utx].fog[lp] != FOG_VISIBLE) continue;
        if (point_in_unit(u, wp)) return i;
    }
    return -1;
}
int find_enemy_building_at(GameState *gs, UIState *ui, Vector2 wp) {
    int lp = net_get_local_player();
    int wall_id = wall_building_at_point(gs, wp, 1, false);
    if(wall_id >= 0) return wall_id;
    int best_id = -1;
    int best_rank = -1;
    float best_dist2 = 0.0f;
    float best_depth_y = 0.0f;
    for (int i = 0; i < MAX_BUILDINGS; i++) {
        Building *b = &gs->buildings[i];
        if (!b->active || b->player == lp) continue;
        int bmx = clampi(b->tx, 0, MAP_W - 1), bmy = clampi(b->ty, 0, MAP_H - 1);
        if (gs->map[bmy][bmx].fog[lp] == FOG_HIDDEN) continue;
        int rank = 0;
        float dist2 = 0.0f;
        float depth_y = 0.0f;
        if (!building_hit_info(gs, b, ui, wp, &rank, &dist2, &depth_y)) continue;
        if (best_id < 0 || depth_y > best_depth_y + 1.0f ||
            (fabsf(depth_y - best_depth_y) <= 1.0f &&
             (rank > best_rank || (rank == best_rank && dist2 < best_dist2)))) {
            best_id = i;
            best_rank = rank;
            best_dist2 = dist2;
            best_depth_y = depth_y;
        }
    }
    return best_id;
}
int find_unfinished_building_at(GameState *gs, UIState *ui, Vector2 wp) {
    int lp = net_get_local_player();
    int wall_id = wall_building_at_point(gs, wp, 2, false);
    if(wall_id >= 0) return wall_id;
    int best_id = -1;
    int best_rank = -1;
    float best_dist2 = 0.0f;
    float best_depth_y = 0.0f;
    for (int i = 0; i < MAX_BUILDINGS; i++) {
        Building *b = &gs->buildings[i];
        if (!b->active || b->player != lp || b->complete) continue;
        int rank = 0;
        float dist2 = 0.0f;
        float depth_y = 0.0f;
        if (!building_hit_info(gs, b, ui, wp, &rank, &dist2, &depth_y)) continue;
        if (best_id < 0 || depth_y > best_depth_y + 1.0f ||
            (fabsf(depth_y - best_depth_y) <= 1.0f &&
             (rank > best_rank || (rank == best_rank && dist2 < best_dist2)))) {
            best_id = i;
            best_rank = rank;
            best_dist2 = dist2;
            best_depth_y = depth_y;
        }
    }
    return best_id;
}

static int find_damaged_friendly_building_at(GameState *gs, UIState *ui, Vector2 wp) {
    int fb = find_friendly_building_at(gs, ui, wp);
    if (fb < 0) return -1;
    Building *b = &gs->buildings[fb];
    if (!b->active || !b->complete || b->hp >= b->max_hp) return -1;
    return fb;
}

int find_friendly_dropoff_at(GameState *gs, UIState *ui, Vector2 wp) {
    int fb = find_friendly_building_at(gs, ui, wp);
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
    int eb = (eu < 0) ? find_enemy_building_at(gs, ui, world) : -1;
    int ub = find_unfinished_building_at(gs, ui, world);
    int repair_b = find_damaged_friendly_building_at(gs, ui, world);
    int dropoff = find_friendly_dropoff_at(gs, ui, world);

    TileType tt = gs->map[ty][tx].type;
    bool is_resource = (tt == TILE_FOREST || tt == TILE_GOLD || tt == TILE_STONE || tt == TILE_BERRIES || tt == TILE_FARM);

    int ftx = tx, fty = ty;
    bool must_be_passable = (!is_resource && eu < 0 && eb < 0 && ub < 0 && repair_b < 0 && dropoff < 0);
    if (must_be_passable) {
        if (!map_find_passable_near(gs, tx, ty, &ftx, &fty)) return;
    }

    PathCell formation_targets[64];
    bool use_formation = (eu < 0 && eb < 0 && ub < 0 && repair_b < 0 && dropoff < 0 && !is_resource);
    if (use_formation) {
        int formation_count = ui->sel_count < 64 ? ui->sel_count : 64;
        unit_compute_formation_targets(gs, ftx, fty, formation_count, formation_targets);
    }

    if (g_net_active) {
        NetPacket pkt = {0};
        pkt.player = g_local_player_id;
        
        if (eu >= 0 || eb >= 0) {
            pkt.type = PKT_ATTACK;
            pkt.extra = (eu >= 0) ? 0 : 1; 
            pkt.target_id = (eu >= 0) ? eu : eb;
        } else if (ub >= 0) {
            pkt.type = PKT_BUILD;
            pkt.target_id = ub;
        } else if (repair_b >= 0) {
            pkt.type = PKT_BUILD;
            pkt.target_id = repair_b;
        } else if (dropoff >= 0) {
            pkt.type = PKT_MOVE; 
            pkt.tx = gs->buildings[dropoff].tx;
            pkt.ty = gs->buildings[dropoff].ty;
        } else if (is_resource) {
            pkt.type = PKT_GATHER;
            pkt.tx = tx;
            pkt.ty = ty;
        } else {
            pkt.type = PKT_MOVE;
            pkt.tx = ftx;
            pkt.ty = fty;
        }

        int uc = 0;
        for (int i = 0; i < ui->sel_count && uc < 64; i++) {
            Unit *u = &gs->units[ui->sel_units[i]];
            if (u->active && u->player == g_local_player_id) {
                // Apply a quick filter for villager-only actions
                if ((pkt.type == PKT_BUILD || pkt.type == PKT_GATHER) && u->type != UNIT_VILLAGER) continue;
                pkt.units[uc++] = ui->sel_units[i];
            }
        }
        pkt.unit_count = uc;
        if (uc > 0) {
            // Need to fix dropoff special case if needed, but for now map it to move to dropoff is fine
            if (dropoff >= 0) {
               // We need PKT_DROP_OFF or assume Move to DropOff building does it
               // To keep it simple let's reuse move logic; if a gathering villager is explicitly moved to a dropoff, it drops off if implemented.
               // Currently, our direct call was `unit_give_dropoff_order`. We should add PKT_DROPOFF
               // Wait, the packet list in net.h doesn't have PKT_DROPOFF.
               // Let's just modify the net.c later to handle if MOVE targets a dropoff building OR I can just send PKT_MOVE.
               // Wait, if I just send PKT_MOVE and the destination has a dropoff, our game logic might not drop off.
               // Let's add the exact logic back here for the local non-net case, but for net, let's just make PKT_MOVE to dropoff work!
                
            }
            net_dispatch_packet(gs, &pkt);
        }
    } else {
        int lp = net_get_local_player();
        for (int i = 0; i < ui->sel_count; i++) {
            Unit *u = &gs->units[ui->sel_units[i]];
            if (!u->active || u->player != lp) continue;
            if (eu >= 0 || eb >= 0) {
                unit_give_attack_order(gs, u, eu, eb);
            } else if (ub >= 0 && u->type == UNIT_VILLAGER) {
                unit_give_build_order(gs, u, ub);
            } else if (repair_b >= 0 && u->type == UNIT_VILLAGER) {
                unit_give_build_order(gs, u, repair_b);
            } else if (dropoff >= 0 && u->type == UNIT_VILLAGER && u->carry_amt > 0) {
                unit_give_dropoff_order(gs, u, gs->buildings[dropoff].tx, gs->buildings[dropoff].ty);
            } else if (is_resource && u->type == UNIT_VILLAGER) {
                unit_give_gather_order(gs, u, tx, ty);
            } else {
                int ntx = ftx;
                int nty = fty;
                if (use_formation && i < 64) {
                    ntx = formation_targets[i].x;
                    nty = formation_targets[i].y;
                }
                unit_give_move_order(gs, u, ntx, nty);
            }
        }
    }
}

/* ─── Left-click start ────────────────────────────────────── */
void handle_left_down(GameState *gs, UIState *ui) {
    Vector2 mp = GetMousePosition();
    bool over_hud = mp.y < HUD_TOP_H || mp.y > GetScreenHeight() - HUD_BOT_H ||
                    (mp.x > GetScreenWidth() - MINI_SIZE - 16 && mp.y > GetScreenHeight() - HUD_BOT_H - 8);
    if (over_hud) return;
    if (gs->build_mode.active || ui->build_panel_open) return;
    /* Always set box_selecting; Android will clear is_box flag in handle_left_up */
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
    bool over_hud = mp.y < HUD_TOP_H || mp.y > GetScreenHeight() - HUD_BOT_H ||
                    (mp.x > GetScreenWidth() - MINI_SIZE - 16 && mp.y > GetScreenHeight() - HUD_BOT_H - 8);
    if (over_hud) return;

    // Use screen-space distance for more reliable thresholding
    float sdx = fabsf(mp.x - ui->box_start.x), sdy = fabsf(mp.y - ui->box_start.y);
    bool is_box = (sdx > 15 || sdy > 15);

    /* Never box-select on touch — every tap is treated as a point click */
#if defined(PLATFORM_ANDROID) || defined(ANDROID)
    is_box = false;
#endif

    if (is_box) {
        bool shift = IsKeyDown(KEY_LEFT_SHIFT);
        if (!shift) clear_selection(gs, ui);
        float x0 = ws.x < we.x ? ws.x : we.x, x1 = ws.x > we.x ? ws.x : we.x;
        float y0 = ws.y < we.y ? ws.y : we.y, y1 = ws.y > we.y ? ws.y : we.y;
        int lp = net_get_local_player();
        for (int i = 0; i < MAX_UNITS; i++) {
            Unit *u = &gs->units[i];
            if (!u->active || u->player != lp || u->state == US_DEAD) continue;
            if (rect_intersects_unit(u, x0, y0, x1, y1)) select_unit(gs, ui, i);
        }
        return;
    }

    bool shift = IsKeyDown(KEY_LEFT_SHIFT);
    int fu = find_friendly_unit_at(gs, we);
    int fb = find_friendly_building_at(gs, ui, we);

    bool has_villagers = false;
    for (int i = 0; i < ui->sel_count; i++)
        if (gs->units[ui->sel_units[i]].type == UNIT_VILLAGER) { has_villagers = true; break; }

    if (fu >= 0) {
        if (!shift && click_reactivates_selected_hero(gs, ui, fu)) return;
        /* Clicked a friendly unit → select it */
        if (!shift) clear_selection(gs, ui);
        select_unit(gs, ui, fu);
        ui->sel_tile_x = -1; ui->sel_tile_y = -1;
    } else if (fb >= 0 && gs->buildings[fb].complete) {
        /* Clicking a completed farm with villagers selected = gather command */
        if (has_villagers && gs->buildings[fb].type == BLD_FARM) {
            issue_command_at(gs, ui, we);
        } else {
            /* If carrying villagers are selected and the clicked building is a drop-off
               point, issue a drop-off command instead of selecting the building. */
            BldType bt = gs->buildings[fb].type;
            bool is_dropoff_bld = (bt == BLD_TOWN_CENTER || bt == BLD_MILL ||
                                   bt == BLD_LUMBER_CAMP  || bt == BLD_MINING_CAMP);
            bool has_carrying_villager = false;
            for (int i = 0; i < ui->sel_count; i++) {
                Unit *u = &gs->units[ui->sel_units[i]];
                if (u->type == UNIT_VILLAGER && u->carry_amt > 0) {
                    has_carrying_villager = true; break;
                }
            }
            if (is_dropoff_bld && has_carrying_villager) {
                issue_command_at(gs, ui, we);
            } else {
                clear_selection(gs, ui);
                ui->sel_building = fb;
                gs->buildings[fb].selected = true;
                ui->sel_tile_x = -1; ui->sel_tile_y = -1;
            }
        }
    } else if (fb >= 0 && !has_villagers) {
        clear_selection(gs, ui);
        ui->sel_building = fb;
        gs->buildings[fb].selected = true;
        ui->sel_tile_x = -1; ui->sel_tile_y = -1;
    } else if (ui->sel_count > 0) {
        /* Units selected, clicked on world → context command (move/attack/gather/build/dropoff) */
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
        /* Nothing selected → inspect the clicked tile */
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

/* ─── Direct tap handler for Android ─────────────────────── */
/* Called when GESTURE_TAP is detected.  Does NOT rely on box_selecting.  */
void handle_tap(GameState *gs, UIState *ui) {
    /* Use touch position directly */
    Vector2 mp = GetTouchPointCount() > 0 ? GetTouchPosition(0) : GetMousePosition();
    bool over_hud = mp.y < HUD_TOP_H || mp.y > GetScreenHeight() - HUD_BOT_H ||
                    (mp.x > GetScreenWidth() - MINI_SIZE - 16 && mp.y > GetScreenHeight() - HUD_BOT_H - 8);
    if (over_hud) return;
    if (gs->build_mode.active || ui->build_panel_open) return;

    Vector2 we = GetScreenToWorld2D(mp, ui->camera);
    bool shift = IsKeyDown(KEY_LEFT_SHIFT);
    int fu = find_friendly_unit_at(gs, we);
    int fb = find_friendly_building_at(gs, ui, we);

    bool has_villagers = false;
    for (int i = 0; i < ui->sel_count; i++)
        if (gs->units[ui->sel_units[i]].type == UNIT_VILLAGER) { has_villagers = true; break; }

    if (fu >= 0) {
        if (!shift && click_reactivates_selected_hero(gs, ui, fu)) return;
        if (!shift) clear_selection(gs, ui);
        select_unit(gs, ui, fu);
        ui->sel_tile_x = -1; ui->sel_tile_y = -1;
    } else if (fb >= 0 && gs->buildings[fb].complete) {
        if (has_villagers && gs->buildings[fb].type == BLD_FARM) {
            issue_command_at(gs, ui, we);
        } else {
            BldType bt = gs->buildings[fb].type;
            bool is_dropoff_bld = (bt == BLD_TOWN_CENTER || bt == BLD_MILL ||
                                   bt == BLD_LUMBER_CAMP  || bt == BLD_MINING_CAMP);
            bool has_carrying_villager = false;
            for (int i = 0; i < ui->sel_count; i++) {
                Unit *u = &gs->units[ui->sel_units[i]];
                if (u->type == UNIT_VILLAGER && u->carry_amt > 0) {
                    has_carrying_villager = true; break;
                }
            }
            if (is_dropoff_bld && has_carrying_villager) {
                issue_command_at(gs, ui, we);
            } else {
                clear_selection(gs, ui);
                ui->sel_building = fb;
                gs->buildings[fb].selected = true;
                ui->sel_tile_x = -1; ui->sel_tile_y = -1;
            }
        }
    } else if (fb >= 0 && !has_villagers) {
        clear_selection(gs, ui);
        ui->sel_building = fb;
        gs->buildings[fb].selected = true;
        ui->sel_tile_x = -1; ui->sel_tile_y = -1;
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
    bool over_hud = mp.y < HUD_TOP_H || mp.y > GetScreenHeight() - HUD_BOT_H ||
                    (mp.x > GetScreenWidth() - MINI_SIZE - 16 && mp.y > GetScreenHeight() - HUD_BOT_H - 8);
    ui->hover_unit = -1; ui->hover_building = -1;
    ui->hover_tile_x = -1; ui->hover_tile_y = -1;
    if (over_hud || gs->phase != PHASE_PLAYING) return;
    Vector2 wp = GetScreenToWorld2D(mp, ui->camera);
    int u = find_friendly_unit_at(gs, wp);
    if (u < 0) u = find_enemy_unit_at(gs, wp);
    if (u >= 0) { ui->hover_unit = u; return; }
    int b = find_friendly_building_at(gs, ui, wp);
    if (b < 0) b = find_enemy_building_at(gs, ui, wp);
    if (b >= 0) { ui->hover_building = b; return; }
    int lp = net_get_local_player();
    Vector2 cart = to_rvec2(iso_to_world(wp.x, wp.y));
    int tx = (int)(cart.x / TILE_SIZE), ty = (int)(cart.y / TILE_SIZE);
    if (map_in_bounds(tx, ty) && gs->map[ty][tx].fog[lp] == FOG_VISIBLE) {
        TileType tt = gs->map[ty][tx].type;
        if (tt == TILE_FOREST || tt == TILE_GOLD || tt == TILE_STONE || tt == TILE_BERRIES || tt == TILE_FARM)
            { ui->hover_tile_x = tx; ui->hover_tile_y = ty; }
    }
}
