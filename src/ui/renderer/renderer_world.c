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

static void draw_building(GameState *gs, UIState *ui, Building *b){
    float px=(float)(b->tx*TILE_SIZE), py=(float)(b->ty*TILE_SIZE);
    float w=(float)(b->tw*TILE_SIZE), h=(float)(b->th*TILE_SIZE);
    Color mc=player_color(b->player);
    Color dc=player_color_dark(b->player);

    if(!b->complete){
        draw_shadow(px + w * 0.5f, py + h * 0.5f, w * 0.8f, h * 0.8f);
        draw_construction(px,py,w,h,b->construction,mc);
        draw_hp_bar(px+w*0.5f,py+h*0.5f,w*0.8f,(int)(b->construction*100),100,25);
        return;
    }

    draw_shadow(px + w * 0.5f, py + h * 0.5f, w * 0.9f, h * 0.9f);

    Texture2D tex = ui->tex_buildings[b->type];
    if (building_is_walllike(b->type)) {
        draw_wall_piece(gs, b, mc, dc);
    } else if (tex.id != 0) {
        /* Scale buildings biased towards footprint size, with a global boost */
        float base_ratio = 1.25f / 4.0f; /* TC as baseline */
        float boost = 1.25f;            /* Scale boost for 'premium' look */
        float sc = (float)b->tw * base_ratio * boost;
        
        float tw = tex.width * sc;
        float th = tex.height * sc;
        Vector2 bc = to_rvec2(world_to_iso(px + w * 0.5f, py + h * 0.5f));
        DrawTextureEx(tex, (Vector2){bc.x - tw/2.0f, bc.y - th + h*0.4f}, 0.0f, sc, WHITE);
    } else {
        draw_iso_box(px+2, py+2, w-4, h-4, 15, CLITERAL(Color){140,120,90,255}, dc, dc);
    }

    if(b->hp <= b->max_hp / 2) draw_smoke(px + w*0.4f, py + h*0.4f, gs->game_time, b->id);
    if(b->hp <= b->max_hp / 4) draw_smoke(px + w*0.6f, py + h*0.6f, gs->game_time, b->id + 100);

    int lp = net_get_local_player();
    if(b->selected && b->player==lp){
        Vector2 p1 = to_rvec2(world_to_iso(px - 2, py - 2));
        Vector2 p2 = to_rvec2(world_to_iso(px + w + 2, py - 2));
        Vector2 p3 = to_rvec2(world_to_iso(px + w + 2, py + h + 2));
        Vector2 p4 = to_rvec2(world_to_iso(px - 2, py + h + 2));
        DrawLineEx(p1, p2, 2, C_SEL); DrawLineEx(p2, p3, 2, C_SEL);
        DrawLineEx(p3, p4, 2, C_SEL); DrawLineEx(p4, p1, 2, C_SEL);
    }

    bool hovered = false;
    for (int i = 0; i < MAX_BUILDINGS; i++) if (&gs->buildings[i] == b && ui->hover_building == i) hovered = true;
    if (hovered) {
        Vector2 p1 = to_rvec2(world_to_iso(px, py));
        Vector2 p2 = to_rvec2(world_to_iso(px + w, py));
        Vector2 p3 = to_rvec2(world_to_iso(px + w, py + h));
        Vector2 p4 = to_rvec2(world_to_iso(px, py + h));
        DrawLineEx(p1, p2, 1.5f, C_HOVER); DrawLineEx(p2, p3, 1.5f, C_HOVER);
        DrawLineEx(p3, p4, 1.5f, C_HOVER); DrawLineEx(p4, p1, 1.5f, C_HOVER);
    }

    draw_hp_bar(px+w*0.5f,py+h*0.5f,w*0.8f,b->hp,b->max_hp,35);

    if(b->queue_len>0){
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
    draw_shadow(wx, wy, 10 * size_mult, 8 * size_mult);

    if(u->selected) {
        float pulse = sinf(t * 8.0f) * 1.5f;
        DrawEllipse((int)p.x,(int)p.y, (10 + pulse) * size_mult, (5 + pulse * 0.5f) * size_mult, CLITERAL(Color){80,220,100,140});
    }

    bool u_hovered = false;
    for (int i = 0; i < MAX_UNITS; i++) if (&gs->units[i] == u && ui->hover_unit == i) u_hovered = true;
    if (u_hovered && !u->selected) DrawEllipseLines((int)p.x, (int)p.y, 11 * size_mult, 6 * size_mult, C_HOVER);

    float px = p.x, py = p.y - 10;

    Texture2D utex = ui->tex_units[u->type];
    if (utex.id != 0) {
        float sc = 0.18f * size_mult;
        float tw = utex.width * sc;
        float th = utex.height * sc;
        DrawTextureEx(utex, (Vector2){px - tw/2.0f, py - th + 12 * size_mult}, 0.0f, sc, WHITE);
    } else {
        /* Fallback primitive drawing */
        if (u->type == UNIT_BATTERING_RAM) {
            DrawRectangle((int)(px-10),(int)(py-6),20,12,mc);
            DrawRectangle((int)(px-14),(int)(py-3),28,6,dc);
        } else if (u->type == UNIT_MANGONEL) {
            DrawRectangle((int)(px-8),(int)(py-5),16,10,mc);
            DrawCircle((int)px,(int)(py-8),5,CLITERAL(Color){150,140,120,255});
        } else if (u->type == UNIT_SCORPION) {
            DrawRectangle((int)(px-9),(int)(py-4),18,8,mc);
            DrawLine((int)px,(int)(py-8),(int)(px+10),(int)(py-14),CLITERAL(Color){200,190,150,255});
        } else {
            DrawRectangle((int)(px-4),(int)(py-3),8,8,mc);
            DrawCircle((int)px,(int)(py-7),5,CLITERAL(Color){220,185,145,255});
        }
    }

    if(u->type==UNIT_VILLAGER && u->carry_amt>0){
        Color rc;
        switch(u->carry_type){
            case RES_FOOD:  rc=CLITERAL(Color){100,200,50,255}; break;
            case RES_WOOD:  rc=CLITERAL(Color){120,80,30,255};  break;
            case RES_GOLD:  rc=CLITERAL(Color){220,190,30,255}; break;
            case RES_STONE: rc=CLITERAL(Color){170,160,150,255};break;
            default:        rc=WHITE; break;
        }
        DrawCircle((int)(px+6),(int)(py-12),4,rc);
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

/* ─── Build ghost ─────────────────────────────────────────── */
static void draw_build_ghost(GameState *gs){
    if(!gs->build_mode.active) return;
    int tw=building_tw(gs->build_mode.type), th=building_th(gs->build_mode.type);
    
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
        Color gc=valid ?
            CLITERAL(Color){80,220,100,100}:CLITERAL(Color){220,60,60,100};
        draw_iso_quad(px, py, w, h, gc);
        Color lc=valid ? GREEN : RED;
        Vector2 p1 = to_rvec2(world_to_iso(px, py));
        Vector2 p2 = to_rvec2(world_to_iso(px + w, py));
        Vector2 p3 = to_rvec2(world_to_iso(px + w, py + h));
        Vector2 p4 = to_rvec2(world_to_iso(px, py + h));
        DrawLineEx(p1, p2, 2.0f, lc); DrawLineEx(p2, p3, 2.0f, lc);
        DrawLineEx(p3, p4, 2.0f, lc); DrawLineEx(p4, p1, 2.0f, lc);
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

    draw_build_ghost(gs);
    draw_selection_box(gs, ui);
}
