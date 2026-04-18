/*=============================================================
 * renderer_world.c  –  Buildings, units, selection, ghost, master render
 *=============================================================*/
#include "game.h"
#include "ui_state.h"
#include "renderer.h"
#include "net.h"
#include <stdio.h>

/* Forward declarations of functions in other renderer files */
extern void draw_tile(GameState *gs, UIState *ui, int x, int y);
extern void draw_fog(GameState *gs, int x, int y);

/* ─── Building rendering ─────────────────────────────────── */

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

        Color pc = (p->type == PROJ_BOLT)
            ? CLITERAL(Color){160, 210, 255, 255}
            : CLITERAL(Color){240, 220, 140, 255};
        Color tc = player_color(p->owner_player);
        DrawLineEx(prev, cur, p->type == PROJ_BOLT ? 3.0f : 2.0f, pc);
        DrawCircleV(cur, p->type == PROJ_BOLT ? 3.4f : 2.4f, tc);
    }
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
    if (tex.id != 0) {
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

    float size_mult = (u->type == UNIT_SCOUT) ? 1.4f : 1.0f;
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
        DrawRectangle((int)(px-4),(int)(py-3),8,8,mc);
        DrawCircle((int)px,(int)(py-7),5,CLITERAL(Color){220,185,145,255});
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
    int tx=gs->build_mode.ghost_tx, ty=gs->build_mode.ghost_ty;
    int tw=building_tw(gs->build_mode.type), th=building_th(gs->build_mode.type);
    float px=(float)(tx*TILE_SIZE),py=(float)(ty*TILE_SIZE);
    float w=(float)(tw*TILE_SIZE),h=(float)(th*TILE_SIZE);
    Color gc=gs->build_mode.valid ?
        CLITERAL(Color){80,220,100,100}:CLITERAL(Color){220,60,60,100};
    draw_iso_quad(px, py, w, h, gc);
    Color lc=gs->build_mode.valid ? GREEN : RED;
    Vector2 p1 = to_rvec2(world_to_iso(px, py));
    Vector2 p2 = to_rvec2(world_to_iso(px + w, py));
    Vector2 p3 = to_rvec2(world_to_iso(px + w, py + h));
    Vector2 p4 = to_rvec2(world_to_iso(px, py + h));
    DrawLineEx(p1, p2, 2.0f, lc); DrawLineEx(p2, p3, 2.0f, lc);
    DrawLineEx(p3, p4, 2.0f, lc); DrawLineEx(p4, p1, 2.0f, lc);
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
    
    for(int y=0;y<MAP_H;y++) for(int x=0;x<MAP_W;x++) draw_fog(gs,x,y);

    draw_build_ghost(gs);
    draw_selection_box(gs, ui);
}
