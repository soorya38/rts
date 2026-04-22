/*=============================================================
 * renderer_world.c  –  Buildings, units, selection, ghost, master render
 *=============================================================*/
#include "game.h"
#include "ui_state.h"
#include "renderer.h"
#include "net.h"
#include "hud_common.h"
#include <stdio.h>

/* Forward declarations of functions in other renderer files */
extern void draw_tile(GameState *gs, UIState *ui, int x, int y);
extern void draw_fog(GameState *gs, int x, int y);

/* ─── Building rendering ─────────────────────────────────── */

static bool wall_connected(GameState *gs, Building *b, int dx, int dy){
    int nx = b->tx + dx;
    int ny = b->ty + dy;
    if(!map_in_bounds(nx, ny)) return false;
    int nid = gs->map[ny][nx].building_id;
    if(nid < 0) return false;

    Building *nb = &gs->buildings[nid];
    return nb->active && nb->complete && nb->player == b->player && building_is_walllike(nb->type);
}

static void draw_wall_piece(GameState *gs, Building *b, Color mc, Color dc){
    float cx = (b->tx + 0.5f) * TILE_SIZE;
    float cy = (b->ty + 0.5f) * TILE_SIZE;
    Vector2 center = to_rvec2(world_to_iso(cx, cy));
    bool n = wall_connected(gs, b, 0, -1);
    bool s = wall_connected(gs, b, 0, 1);
    bool w = wall_connected(gs, b, -1, 0);
    bool e = wall_connected(gs, b, 1, 0);

    draw_shadow(cx, cy, 16.0f, 9.0f);

    int dirs[4][2] = {{0,-1},{0,1},{-1,0},{1,0}};
    for(int i=0;i<4;i++){
        int dx = dirs[i][0], dy = dirs[i][1];
        if((dx == 0 && dy == -1 && !n) || (dx == 0 && dy == 1 && !s) ||
           (dx == -1 && dy == 0 && !w) || (dx == 1 && dy == 0 && !e)) continue;

        float tx = (b->tx + dx + 0.5f) * TILE_SIZE;
        float ty = (b->ty + dy + 0.5f) * TILE_SIZE;
        Vector2 next = to_rvec2(world_to_iso(tx, ty));
        DrawLineEx(center, next, b->type == BLD_GATE ? 9.0f : 7.5f, dc);
        DrawLineEx(center, next, b->type == BLD_GATE ? 5.0f : 4.0f, mc);
    }

    if(b->type == BLD_GATE){
        bool horizontal = (w || e) && !(n || s);
        if(!horizontal && !(n || s) && (w || e)) horizontal = true;

        if(horizontal){
            DrawRectangle((int)(center.x - 11), (int)(center.y - 14), 6, 18, dc);
            DrawRectangle((int)(center.x + 5), (int)(center.y - 14), 6, 18, dc);
            DrawRectangle((int)(center.x - 8), (int)(center.y - 11), 22, 4, mc);
            DrawRectangle((int)(center.x - 8), (int)(center.y - 13), 22, 2, CLITERAL(Color){210, 190, 145, 255});
        } else {
            DrawRectangle((int)(center.x - 7), (int)(center.y - 16), 14, 5, dc);
            DrawRectangle((int)(center.x - 7), (int)(center.y + 5), 14, 5, dc);
            DrawRectangle((int)(center.x - 4), (int)(center.y - 13), 8, 24, mc);
            DrawRectangle((int)(center.x - 6), (int)(center.y - 10), 12, 4, CLITERAL(Color){210, 190, 145, 255});
        }
        DrawCircleV(center, 5.0f, CLITERAL(Color){70, 55, 35, 255});
    } else {
        DrawCircleV(center, 6.0f, dc);
        DrawCircleV(center, 4.0f, mc);
        DrawCircleV((Vector2){center.x, center.y - 6.0f}, 4.0f, CLITERAL(Color){200, 185, 150, 255});
    }
}

static void draw_projectiles(GameState *gs){
    for(int i=0;i<MAX_PROJECTILES;i++){
        Projectile *p = &gs->projectiles[i];
        if(!p->active || p->duration <= 0.0f) continue;

        float t = clampf(p->elapsed / p->duration, 0.0f, 1.0f);
        float wx = lerpf(p->sx, p->ex, t);
        float wy = lerpf(p->sy, p->ey, t);
        float arc = 4.0f * t * (1.0f - t) * p->arc_height;

        Vector2 cur = to_rvec2(world_to_iso(wx, wy));
        Vector2 prev = to_rvec2(world_to_iso(lerpf(p->sx, p->ex, t > 0.03f ? t - 0.03f : 0.0f),
                                             lerpf(p->sy, p->ey, t > 0.03f ? t - 0.03f : 0.0f)));
        cur.y -= arc;
        prev.y -= 4.0f * (t > 0.03f ? t - 0.03f : 0.0f) * (1.0f - (t > 0.03f ? t - 0.03f : 0.0f)) * p->arc_height;

        Color tc = player_color(p->owner_player);
        if(p->type == PROJ_ARROW){
            Vector2 dir = (Vector2){cur.x - prev.x, cur.y - prev.y};
            float len = sqrtf(dir.x * dir.x + dir.y * dir.y);
            if(len < 0.001f) len = 1.0f;
            dir.x /= len;
            dir.y /= len;
            Vector2 shaft_end = (Vector2){cur.x - dir.x * 5.0f, cur.y - dir.y * 5.0f};
            Vector2 perp = (Vector2){-dir.y, dir.x};
            Color shaft = CLITERAL(Color){170, 120, 60, 255};
            Color fletch = CLITERAL(Color){245, 235, 190, 255};
            DrawLineEx(prev, shaft_end, 2.6f, shaft);
            DrawTriangle(cur,
                         (Vector2){shaft_end.x + perp.x * 2.6f, shaft_end.y + perp.y * 2.6f},
                         (Vector2){shaft_end.x - perp.x * 2.6f, shaft_end.y - perp.y * 2.6f},
                         tc);
            DrawLineEx((Vector2){prev.x + perp.x * 1.6f, prev.y + perp.y * 1.6f},
                       (Vector2){prev.x - dir.x * 3.0f, prev.y - dir.y * 3.0f}, 1.2f, fletch);
            DrawLineEx((Vector2){prev.x - perp.x * 1.6f, prev.y - perp.y * 1.6f},
                       (Vector2){prev.x - dir.x * 3.0f, prev.y - dir.y * 3.0f}, 1.2f, fletch);
        } else {
            Color pc = (p->type == PROJ_BOLT)
                ? CLITERAL(Color){160, 210, 255, 255}
                : CLITERAL(Color){180, 170, 155, 255};
            DrawLineEx(prev, cur, p->type == PROJ_BOLT ? 3.0f : 2.4f, pc);
            DrawCircleV(cur, p->type == PROJ_BOLT ? 3.4f : 3.0f, tc);
        }
    }
}

static void draw_selected_rally_point(GameState *gs, UIState *ui){
    int lp = net_get_local_player();
    if(ui->sel_building < 0) return;

    Building *b = &gs->buildings[ui->sel_building];
    if(!b->active || !b->complete || b->player != lp || !building_supports_rally(b->type)) return;

    float bx = (b->tx + b->tw * 0.5f) * TILE_SIZE;
    float by = (b->ty + b->th * 0.5f) * TILE_SIZE;
    float rx = (b->rally_tx + 0.5f) * TILE_SIZE;
    float ry = (b->rally_ty + 0.5f) * TILE_SIZE;
    Vector2 from = to_rvec2(world_to_iso(bx, by));
    Vector2 to = to_rvec2(world_to_iso(rx, ry));

    DrawLineEx(from, to, 2.0f, CLITERAL(Color){120, 220, 255, 150});
    draw_flag(rx, ry, b->player);

    if(!ui->rally_mode) return;

    Vector2 mp = GetMousePosition();
#if defined(PLATFORM_ANDROID) || defined(ANDROID)
    if(GetTouchPointCount() > 0) mp = GetTouchPosition(0);
#endif
    bool over_hud = mp.y < HUD_TOP_H || mp.y > GetScreenHeight() - HUD_BOT_H ||
                    (mp.x > GetScreenWidth() - MINI_SIZE - 16 && mp.y > GetScreenHeight() - HUD_BOT_H - 8);
    if(over_hud) return;

    Vector2 wp = GetScreenToWorld2D(mp, ui->camera);
    Vector2 cart = to_rvec2(iso_to_world(wp.x, wp.y));
    int tx = (int)(cart.x / TILE_SIZE);
    int ty = (int)(cart.y / TILE_SIZE);
    if(!map_in_bounds(tx, ty)) return;

    int ptx = tx;
    int pty = ty;
    if(!map_find_passable_near(gs, tx, ty, &ptx, &pty)) return;

    float prx = (ptx + 0.5f) * TILE_SIZE;
    float pry = (pty + 0.5f) * TILE_SIZE;
    Vector2 preview = to_rvec2(world_to_iso(prx, pry));
    DrawLineEx(from, preview, 2.0f, CLITERAL(Color){240, 240, 180, 150});
    draw_flag(prx, pry, b->player);
    DrawCircleV(preview, 5.0f, CLITERAL(Color){240, 240, 180, 120});
}

static void draw_building_outline(float px, float py, float w, float h, float pad, float line_w, Color color){
    Vector2 p1 = to_rvec2(world_to_iso(px - pad,     py - pad));
    Vector2 p2 = to_rvec2(world_to_iso(px + w + pad, py - pad));
    Vector2 p3 = to_rvec2(world_to_iso(px + w + pad, py + h + pad));
    Vector2 p4 = to_rvec2(world_to_iso(px - pad,     py + h + pad));
    DrawLineEx(p1, p2, line_w, color);
    DrawLineEx(p2, p3, line_w, color);
    DrawLineEx(p3, p4, line_w, color);
    DrawLineEx(p4, p1, line_w, color);
}

static void draw_building(GameState *gs, UIState *ui, Building *b){
    float px=(float)(b->tx*TILE_SIZE), py=(float)(b->ty*TILE_SIZE);
    float w=(float)(b->tw*TILE_SIZE), h=(float)(b->th*TILE_SIZE);
    Color mc=player_color(b->player);
    Color dc=player_color_dark(b->player);
    int lp = net_get_local_player();
    bool hovered = false;
    for (int i = 0; i < MAX_BUILDINGS; i++) if (&gs->buildings[i] == b && ui->hover_building == i) hovered = true;

    if(!b->complete){
        draw_shadow(px + w * 0.5f, py + h * 0.5f, w * 0.8f, h * 0.8f);
        draw_construction(px,py,w,h,b->construction,mc);
        if(b->selected && b->player==lp)
            draw_building_outline(px, py, w, h, 2.0f, 2.0f, C_SEL);
        else if(hovered)
            draw_building_outline(px, py, w, h, 0.0f, 1.5f, C_HOVER);
        draw_hp_bar(px+w*0.5f,py+h*0.5f,w*0.8f,(int)(b->construction*100),100,25);
        return;
    }

    draw_shadow(px + w * 0.5f, py + h * 0.5f, w * 0.9f, h * 0.9f);

    int age = 0;
    if (b->player >= 0 && b->player < NUM_PLAYERS) {
        age = gs->res[b->player].age;
    }
    if (age < 0) age = 0;
    if (age > 3) age = 3;
    Texture2D tex = ui_get_building_texture(ui, b->type, age);
    bool is_town_center = (b->type == BLD_TOWN_CENTER);
    bool is_house = (b->type == BLD_HOUSE);
    bool is_mill = (b->type == BLD_MILL);
    bool is_lumber_camp = (b->type == BLD_LUMBER_CAMP);
    bool is_barracks = (b->type == BLD_BARRACKS);
    bool is_blacksmith = (b->type == BLD_BLACKSMITH);
    bool is_market = (b->type == BLD_MARKET);
    bool is_mining_camp = (b->type == BLD_MINING_CAMP);
    bool is_watch_tower = (b->type == BLD_WATCH_TOWER);
    if (building_is_walllike(b->type)) {
        draw_wall_piece(gs, b, mc, dc);
    } else if (tex.id != 0) {
        /* Town Centers and age-variant building art need normalized draw boxes
           because the age sprites are much larger source images than the old assets. */
        float sc;
        if (is_town_center) {
            const float tc_target_width = 315.0f;
            const float tc_target_height = 255.0f;
            float scale_x = tc_target_width / (float)tex.width;
            float scale_y = tc_target_height / (float)tex.height;
            sc = scale_x < scale_y ? scale_x : scale_y;
        } else if (is_house) {
            const float house_target_width = 150.0f;
            const float house_target_height = 157.0f;
            float scale_x = house_target_width / (float)tex.width;
            float scale_y = house_target_height / (float)tex.height;
            sc = scale_x < scale_y ? scale_x : scale_y;
        } else if (is_mill) {
            const float mill_target_width = 145.0f;
            const float mill_target_height = 150.0f;
            float scale_x = mill_target_width / (float)tex.width;
            float scale_y = mill_target_height / (float)tex.height;
            sc = scale_x < scale_y ? scale_x : scale_y;
        } else if (is_lumber_camp) {
            const float lumber_target_width = 165.0f;
            const float lumber_target_height = 135.0f;
            float scale_x = lumber_target_width / (float)tex.width;
            float scale_y = lumber_target_height / (float)tex.height;
            sc = scale_x < scale_y ? scale_x : scale_y;
        } else if (is_barracks) {
            const float barracks_target_width = 175.0f;
            const float barracks_target_height = 145.0f;
            float scale_x = barracks_target_width / (float)tex.width;
            float scale_y = barracks_target_height / (float)tex.height;
            sc = scale_x < scale_y ? scale_x : scale_y;
        } else if (is_blacksmith) {
            const float blacksmith_target_width = 170.0f;
            const float blacksmith_target_height = 150.0f;
            float scale_x = blacksmith_target_width / (float)tex.width;
            float scale_y = blacksmith_target_height / (float)tex.height;
            sc = scale_x < scale_y ? scale_x : scale_y;
        } else if (is_market) {
            const float market_target_width = 180.0f;
            const float market_target_height = 150.0f;
            float scale_x = market_target_width / (float)tex.width;
            float scale_y = market_target_height / (float)tex.height;
            sc = scale_x < scale_y ? scale_x : scale_y;
        } else if (is_mining_camp) {
            const float mining_target_width = 180.0f;
            const float mining_target_height = 145.0f;
            float scale_x = mining_target_width / (float)tex.width;
            float scale_y = mining_target_height / (float)tex.height;
            sc = scale_x < scale_y ? scale_x : scale_y;
        } else if (is_watch_tower) {
            const float tower_target_width = 170.0f;
            const float tower_target_height = 250.0f;
            float scale_x = tower_target_width / (float)tex.width;
            float scale_y = tower_target_height / (float)tex.height;
            sc = scale_x < scale_y ? scale_x : scale_y;
        } else {
            /* Scale buildings biased towards footprint size, with a global boost */
            float base_ratio = 1.25f / 4.0f; /* TC as baseline */
            float boost = 1.25f;            /* Scale boost for 'premium' look */
            sc = (float)b->tw * base_ratio * boost;
        }
        
        float tw = tex.width * sc;
        float th = tex.height * sc;
        float y_offset = h * 0.4f;
        if (is_house) {
            y_offset += 30.0f; /* push the sprite down to sit on the ground */
        } else if (is_mill) {
            y_offset += 34.0f; /* mills need a stronger ground anchor than the raw art box suggests */
        } else if (is_lumber_camp) {
            y_offset += 18.0f; /* lumber camps read best a little lower than their square source art */
        } else if (is_barracks) {
            y_offset += 18.0f; /* barracks variants sit better when anchored closer to the footprint */
        } else if (is_blacksmith) {
            y_offset += 18.0f; /* blacksmith sets read better when grounded slightly lower */
        } else if (is_market) {
            y_offset += 18.0f; /* market stalls look best when anchored a little deeper into the footprint */
        } else if (is_mining_camp) {
            y_offset += 30.0f; /* mining camps need a stronger drop so the pit and rails sit on the footprint */
        } else if (is_watch_tower) {
            y_offset += 32.0f; /* watch towers are tall, so they need a stronger ground anchor */
        }
        Vector2 bc = to_rvec2(world_to_iso(px + w * 0.5f, py + h * 0.5f));
        DrawTextureEx(tex, (Vector2){bc.x - tw/2.0f, bc.y - th + y_offset}, 0.0f, sc, WHITE);
    } else {
        draw_iso_box(px+2, py+2, w-4, h-4, 15, CLITERAL(Color){140,120,90,255}, dc, dc);
    }

    if(b->hp <= b->max_hp / 2) draw_smoke(px + w*0.4f, py + h*0.4f, gs->game_time, b->id);
    if(b->hp <= b->max_hp / 4) draw_smoke(px + w*0.6f, py + h*0.6f, gs->game_time, b->id + 100);

    if(b->selected && b->player==lp){
        draw_building_outline(px, py, w, h, 2.0f, 2.0f, C_SEL);
    }

    if (hovered) {
        draw_building_outline(px, py, w, h, 0.0f, 1.5f, C_HOVER);
    }

    draw_hp_bar(px+w*0.5f,py+h*0.5f,w*0.8f,b->hp,b->max_hp,35);

    if(b->player == lp && b->queue_len>0){
        float progress=1.0f-(b->train_timer/building_train_time(b->queue[0]));
        Vector2 p = to_rvec2(world_to_iso(px + w*0.5f, py + h*0.5f));
        DrawRectangleRec((Rectangle){p.x-w*0.4f, p.y-40, w*0.8f, 4}, C_HP_BG);
        DrawRectangleRec((Rectangle){p.x-w*0.4f, p.y-40, w*0.8f*progress, 4}, C_HP_GREEN);
    }
}

/* ─── Unit rendering ─────────────────────────────────────── */

static void draw_unit(GameState *gs, UIState *ui, Unit *u, float t){
    if(u->state==US_DEAD) return;
    float wx=u->wx, wy=u->wy;
    Vector2 p = to_rvec2(world_to_iso(wx, wy));
    float alpha = u->state==US_DYING ? clampf(u->death_timer/0.8f,0,1)*255 : 255;
    Color mc = player_color_alpha(u->player,(unsigned char)alpha);
    Color dc = player_color_dark(u->player); dc.a=(unsigned char)alpha;

    float size_mult = 1.0f;
    if (u->type == UNIT_SCOUT) size_mult = 1.4f;
    else if (u->type == UNIT_BATTERING_RAM) size_mult = 1.8f;
    else if (u->type == UNIT_MANGONEL) size_mult = 1.6f;
    else if (u->type == UNIT_SCORPION) size_mult = 1.5f;
    else if (u->type == UNIT_BOMBARD_CANNON) size_mult = 1.7f;
    draw_shadow(wx, wy, 10 * size_mult, 8 * size_mult);

    if(u->selected) {
        float pulse = sinf(t * 8.0f) * 1.5f;
        DrawEllipse((int)p.x,(int)p.y, (10 + pulse) * size_mult, (5 + pulse * 0.5f) * size_mult, CLITERAL(Color){80,220,100,140});
    }

    bool u_hovered = false;
    for (int i = 0; i < MAX_UNITS; i++) if (&gs->units[i] == u && ui->hover_unit == i) u_hovered = true;
    if (u_hovered && !u->selected) DrawEllipseLines((int)p.x, (int)p.y, 11 * size_mult, 6 * size_mult, C_HOVER);

    float px = p.x, py = p.y - 10;

    (void)ui;
    (void)px;
    (void)py;
    float box_w = 8.0f * size_mult;
    float box_h = 8.0f * size_mult;
    float box_z = 10.0f * size_mult;
    draw_iso_box(wx - box_w * 0.5f, wy - box_h * 0.5f, box_w, box_h, box_z,
                 CLITERAL(Color){mc.r, mc.g, mc.b, (unsigned char)alpha},
                 dc, dc);

    if(u->type==UNIT_VILLAGER && u->carry_amt>0){
        Color rc;
        switch(u->carry_type){
            case RES_FOOD:  rc=CLITERAL(Color){100,200,50,255}; break;
            case RES_WOOD:  rc=CLITERAL(Color){120,80,30,255};  break;
            case RES_GOLD:  rc=CLITERAL(Color){220,190,30,255}; break;
            case RES_STONE: rc=CLITERAL(Color){170,160,150,255};break;
            default:        rc=WHITE; break;
        }
        Vector2 carry = to_rvec2(world_to_iso(wx + 8.0f * size_mult, wy - 2.0f * size_mult));
        DrawRectangle((int)(carry.x - 3), (int)(carry.y - 10), 6, 6, rc);
    }
    draw_hp_bar(wx,wy,18,u->hp,u->max_hp,25);
}

/* ─── Selection box ───────────────────────────────────────── */
static void draw_selection_box(GameState *gs, UIState *ui){
    (void)gs;
#if defined(PLATFORM_ANDROID) || defined(ANDROID)
    return;
#endif
    if(!ui->box_selecting) return;
    Vector2 a=GetScreenToWorld2D(ui->box_start,ui->camera);
    Vector2 b=GetScreenToWorld2D(GetMousePosition(),ui->camera);
    float x=a.x<b.x?a.x:b.x, y=a.y<b.y?a.y:b.y;
    float w=fabsf(b.x-a.x), h=fabsf(b.y-a.y);
    DrawRectangleRec((Rectangle){x,y,w,h},CLITERAL(Color){80,220,100,25});
    DrawRectangleLinesEx((Rectangle){x,y,w,h},1.5f,C_SEL);
}

static bool build_hover_tile(UIState *ui, int *out_tx, int *out_ty){
    Vector2 mp = GetMousePosition();
#if defined(PLATFORM_ANDROID) || defined(ANDROID)
    if(GetTouchPointCount() > 0) mp = GetTouchPosition(0);
#endif
    Vector2 wp = GetScreenToWorld2D(mp, ui->camera);
    Vector2 cart = to_rvec2(iso_to_world(wp.x, wp.y));
    int tx = (int)(cart.x / TILE_SIZE);
    int ty = (int)(cart.y / TILE_SIZE);
    if(!map_in_bounds(tx, ty)) return false;
    *out_tx = tx;
    *out_ty = ty;
    return true;
}

static void draw_build_grid(GameState *gs, UIState *ui){
    if(!gs->build_mode.active) return;

    int lp = net_get_local_player();
    int hover_tx = gs->build_mode.ghost_tx;
    int hover_ty = gs->build_mode.ghost_ty;
    int tw = building_tw(gs->build_mode.type);
    int th = building_th(gs->build_mode.type);
    float s = (float)TILE_SIZE;

    if(build_hover_tile(ui, &hover_tx, &hover_ty) == false){
        hover_tx += tw / 2;
        hover_ty += th / 2;
    }

    int radius = 10;
    int x0 = clampi(hover_tx - radius, 0, MAP_W - 1);
    int x1 = clampi(hover_tx + radius, 0, MAP_W - 1);
    int y0 = clampi(hover_ty - radius, 0, MAP_H - 1);
    int y1 = clampi(hover_ty + radius, 0, MAP_H - 1);

    for(int y=y0; y<=y1; y++){
        for(int x=x0; x<=x1; x++){
            if(gs->map[y][x].fog[lp] == FOG_HIDDEN) continue;
            float px = (float)(x * TILE_SIZE);
            float py = (float)(y * TILE_SIZE);
            Vector2 top   = to_rvec2(world_to_iso(px + s * 0.5f, py));
            Vector2 right = to_rvec2(world_to_iso(px + s,        py + s * 0.5f));
            Vector2 bot   = to_rvec2(world_to_iso(px + s * 0.5f, py + s));
            Vector2 left  = to_rvec2(world_to_iso(px,            py + s * 0.5f));

            Color base = CLITERAL(Color){210, 200, 165, 28};
            DrawLineEx(top, right, 0.9f, base);
            DrawLineEx(right, bot, 0.9f, base);
            DrawLineEx(bot, left, 0.9f, base);
            DrawLineEx(left, top, 0.9f, base);
        }
    }

    if(map_in_bounds(hover_tx, hover_ty) && gs->map[hover_ty][hover_tx].fog[lp] != FOG_HIDDEN){
        float px = (float)(hover_tx * TILE_SIZE);
        float py = (float)(hover_ty * TILE_SIZE);
        Vector2 top   = to_rvec2(world_to_iso(px + s * 0.5f, py));
        Vector2 right = to_rvec2(world_to_iso(px + s,        py + s * 0.5f));
        Vector2 bot   = to_rvec2(world_to_iso(px + s * 0.5f, py + s));
        Vector2 left  = to_rvec2(world_to_iso(px,            py + s * 0.5f));
        Color fill = gs->build_mode.valid
            ? CLITERAL(Color){60, 220, 100, 60}
            : CLITERAL(Color){220, 70, 70, 60};
        Color border = gs->build_mode.valid
            ? CLITERAL(Color){150, 255, 190, 230}
            : CLITERAL(Color){255, 150, 150, 230};
        draw_iso_quad(px, py, s, s, fill);
        DrawLineEx(top, right, 2.6f, border);
        DrawLineEx(right, bot, 2.6f, border);
        DrawLineEx(bot, left, 2.6f, border);
        DrawLineEx(left, top, 2.6f, border);
    }
}

/* ─── Build ghost ─────────────────────────────────────────── */
static void draw_build_ghost(GameState *gs, UIState *ui){
    if(!gs->build_mode.active) return;
    int tw=building_tw(gs->build_mode.type), th=building_th(gs->build_mode.type);
    float s = (float)TILE_SIZE;
    
    int pts_x[200], pts_y[200];
    int pt_count = 1;
    pts_x[0] = gs->build_mode.ghost_tx;
    pts_y[0] = gs->build_mode.ghost_ty;
    
    if (building_is_walllike(gs->build_mode.type) && gs->build_mode.dragging) {
        pt_count = get_wall_line_points(gs->build_mode.drag_start_tx, gs->build_mode.drag_start_ty, gs->build_mode.ghost_tx, gs->build_mode.ghost_ty, pts_x, pts_y, 200);
    }

    int lp = net_get_local_player();
    for (int p = 0; p < pt_count; p++) {
        int tx = pts_x[p], ty = pts_y[p];
        bool valid = map_is_buildable(gs, tx, ty, tw, th) &&
                     res_can_afford(&gs->res[lp], building_cost(gs->build_mode.type));

        float px=(float)(tx*TILE_SIZE),py=(float)(ty*TILE_SIZE);
        float w=(float)(tw*TILE_SIZE),h=(float)(th*TILE_SIZE);
        for(int dy=0; dy<th; dy++){
            for(int dx=0; dx<tw; dx++){
                int tx2 = tx + dx, ty2 = ty + dy;
                bool tile_ok = map_in_bounds(tx2, ty2) &&
                               gs->map[ty2][tx2].type != TILE_WATER &&
                               gs->map[ty2][tx2].type != TILE_FOREST &&
                               gs->map[ty2][tx2].type != TILE_GOLD &&
                               gs->map[ty2][tx2].type != TILE_STONE &&
                               gs->map[ty2][tx2].type != TILE_BERRIES &&
                               gs->map[ty2][tx2].building_id < 0;
                float tpx = (float)(tx2 * TILE_SIZE);
                float tpy = (float)(ty2 * TILE_SIZE);
                Color fill = tile_ok
                    ? CLITERAL(Color){70, 230, 110, 85}
                    : CLITERAL(Color){230, 70, 70, 85};
                draw_iso_quad(tpx, tpy, s, s, fill);
            }
        }

        if(!building_is_walllike(gs->build_mode.type)){
            int age = 0;
            if (lp >= 0 && lp < NUM_PLAYERS) age = gs->res[lp].age;
            Texture2D tex = ui_get_building_texture(ui, gs->build_mode.type, age);
            if(tex.id != 0){
                float sc, y_offset = h * 0.4f;
                if (gs->build_mode.type == BLD_HOUSE) {
                    float s_x = 150.0f / (float)tex.width;
                    float s_y = 157.0f / (float)tex.height;
                    sc = s_x < s_y ? s_x : s_y;
                    y_offset += 30.0f;
                } else if (gs->build_mode.type == BLD_TOWN_CENTER) {
                    float s_x = 315.0f / (float)tex.width;
                    float s_y = 255.0f / (float)tex.height;
                    sc = s_x < s_y ? s_x : s_y;
                } else if (gs->build_mode.type == BLD_MILL) {
                    const float mill_target_width = 145.0f;
                    const float mill_target_height = 150.0f;
                    float s_x = mill_target_width / (float)tex.width;
                    float s_y = mill_target_height / (float)tex.height;
                    sc = s_x < s_y ? s_x : s_y;
                    y_offset += 34.0f;
                } else if (gs->build_mode.type == BLD_LUMBER_CAMP) {
                    const float lumber_target_width = 165.0f;
                    const float lumber_target_height = 135.0f;
                    float s_x = lumber_target_width / (float)tex.width;
                    float s_y = lumber_target_height / (float)tex.height;
                    sc = s_x < s_y ? s_x : s_y;
                    y_offset += 18.0f;
                } else if (gs->build_mode.type == BLD_BARRACKS) {
                    const float barracks_target_width = 175.0f;
                    const float barracks_target_height = 145.0f;
                    float s_x = barracks_target_width / (float)tex.width;
                    float s_y = barracks_target_height / (float)tex.height;
                    sc = s_x < s_y ? s_x : s_y;
                    y_offset += 18.0f;
                } else if (gs->build_mode.type == BLD_BLACKSMITH) {
                    const float blacksmith_target_width = 170.0f;
                    const float blacksmith_target_height = 150.0f;
                    float s_x = blacksmith_target_width / (float)tex.width;
                    float s_y = blacksmith_target_height / (float)tex.height;
                    sc = s_x < s_y ? s_x : s_y;
                    y_offset += 18.0f;
                } else if (gs->build_mode.type == BLD_MARKET) {
                    const float market_target_width = 180.0f;
                    const float market_target_height = 150.0f;
                    float s_x = market_target_width / (float)tex.width;
                    float s_y = market_target_height / (float)tex.height;
                    sc = s_x < s_y ? s_x : s_y;
                    y_offset += 18.0f;
                } else if (gs->build_mode.type == BLD_MINING_CAMP) {
                    const float mining_target_width = 180.0f;
                    const float mining_target_height = 145.0f;
                    float s_x = mining_target_width / (float)tex.width;
                    float s_y = mining_target_height / (float)tex.height;
                    sc = s_x < s_y ? s_x : s_y;
                    y_offset += 30.0f;
                } else if (gs->build_mode.type == BLD_WATCH_TOWER) {
                    const float tower_target_width = 170.0f;
                    const float tower_target_height = 250.0f;
                    float s_x = tower_target_width / (float)tex.width;
                    float s_y = tower_target_height / (float)tex.height;
                    sc = s_x < s_y ? s_x : s_y;
                    y_offset += 32.0f;
                } else {
                    float base_ratio = 1.25f / 4.0f;
                    float boost = 1.25f;
                    sc = (float)tw * base_ratio * boost;
                }
                float tex_w = tex.width * sc;
                float tex_h = tex.height * sc;
                Vector2 bc = to_rvec2(world_to_iso(px + w * 0.5f, py + h * 0.5f));
                Color tint = valid
                    ? CLITERAL(Color){180, 255, 200, 165}
                    : CLITERAL(Color){255, 170, 170, 165};
                DrawTextureEx(tex, (Vector2){bc.x - tex_w/2.0f, bc.y - tex_h + y_offset}, 0.0f, sc, tint);
            }
        }

        Color lc=valid ? CLITERAL(Color){120,255,150,255} : CLITERAL(Color){255,110,110,255};
        Vector2 p1 = to_rvec2(world_to_iso(px, py));
        Vector2 p2 = to_rvec2(world_to_iso(px + w, py));
        Vector2 p3 = to_rvec2(world_to_iso(px + w, py + h));
        Vector2 p4 = to_rvec2(world_to_iso(px, py + h));
        DrawLineEx(p1, p2, 3.0f, lc); DrawLineEx(p2, p3, 3.0f, lc);
        DrawLineEx(p3, p4, 3.0f, lc); DrawLineEx(p4, p1, 3.0f, lc);
    }
}

/* ─── Master render ───────────────────────────────────────── */
void renderer_draw_world(GameState *gs, UIState *ui){
    int lp = net_get_local_player();
    for(int y=0;y<MAP_H;y++)
        for(int x=0;x<MAP_W;x++)
            if(gs->map[y][x].fog[lp]!=FOG_HIDDEN) draw_tile(gs, ui, x, y);

    for(int i=0;i<MAX_BUILDINGS;i++){
        Building *b=&gs->buildings[i];
        if(!b->active) continue;
        int bx=b->tx+b->tw/2, by=b->ty+b->th/2;
        if(gs->map[clampi(by,0,MAP_H-1)][clampi(bx,0,MAP_W-1)].fog[lp]==FOG_HIDDEN) continue;
        draw_building(gs, ui, b);
    }

    for(int i=0;i<MAX_UNITS;i++){
        Unit *u=&gs->units[i];
        if(!u->active||u->state==US_DEAD) continue;
        int utx=(int)(u->wx/TILE_SIZE), uty=(int)(u->wy/TILE_SIZE);
        if(!map_in_bounds(utx,uty)) continue;
        FogState fs=gs->map[uty][utx].fog[lp];
        if(u->player!=lp && fs!=FOG_VISIBLE) continue;
        draw_unit(gs, ui, u, gs->game_time);
    }

    draw_projectiles(gs);
    draw_selected_rally_point(gs, ui);
    
    for(int y=0;y<MAP_H;y++) for(int x=0;x<MAP_W;x++) draw_fog(gs,x,y);

    draw_build_grid(gs, ui);
    draw_build_ghost(gs, ui);
    draw_selection_box(gs, ui);
}
