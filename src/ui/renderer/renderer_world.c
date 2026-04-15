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

    switch(b->type) {
        case BLD_TOWN_CENTER: {
            draw_iso_box(px + 4, py + 4, w - 8, h - 8, 12, CLITERAL(Color){160,148,128,255}, CLITERAL(Color){140,128,108,255}, CLITERAL(Color){120,108,88,255});
            draw_iso_box(px + w*0.25f, py + h*0.25f, w*0.5f, h*0.5f, 30, mc, dc, CLITERAL(Color){dc.r/2, dc.g/2, dc.b/2, 255});
            int age = gs->res[b->player].age;
            if(age >= 1) draw_iso_box(px + 2, py + 2, w - 4, h - 4, 3, CLITERAL(Color){100,100,100,255}, CLITERAL(Color){80,80,80,255}, CLITERAL(Color){60,60,60,255});
            draw_flag(px + w*0.5f, py + h*0.5f - 35, b->player);
            break;
        }
        case BLD_HOUSE: {
            draw_iso_box(px+2, py+2, w-4, h-4, 15, CLITERAL(Color){180,158,110,255}, CLITERAL(Color){150,128,80,255}, CLITERAL(Color){120,98,50,255});
            draw_iso_box(px+4, py+4, w-8, h-8, 22, dc, CLITERAL(Color){80,55,25,255}, CLITERAL(Color){60,35,15,255});
            break;
        }
        case BLD_BARRACKS:
        case BLD_ARCHERY_RANGE:
        case BLD_STABLE: {
            Color bc=CLITERAL(Color){110,95,75,255};
            draw_iso_box(px+2, py+2, w-4, h-4, 12, bc, CLITERAL(Color){90,75,55,255}, CLITERAL(Color){70,55,35,255});
            if (b->type == BLD_BARRACKS) {
                draw_iso_box(px + w*0.35f, py + h*0.35f, w*0.3f, h*0.3f, 18, mc, dc, dc);
            } else if (b->type == BLD_ARCHERY_RANGE) {
                draw_iso_box(px + w*0.1f, py + h*0.1f, w*0.8f, 10, 16, mc, dc, dc);
            } else {
                draw_iso_box(px + w*0.1f, py + h*0.7f, w*0.8f, 10, 16, mc, dc, dc);
            }
            break;
        }
        case BLD_MILL:
        case BLD_LUMBER_CAMP:
        case BLD_MINING_CAMP: {
            Color bc=CLITERAL(Color){150,120,75,255};
            draw_iso_box(px+2, py+2, w-4, h-4, 10, bc, CLITERAL(Color){130,100,55,255}, CLITERAL(Color){110,80,35,255});
            draw_iso_box(px+8, py+8, w-16, h-16, 16, mc, dc, dc);
            break;
        }
        case BLD_FARM: {
            draw_iso_quad(px, py, w, h, CLITERAL(Color){145,115,55,255});
            for(int r=0;r<3;r++)
                draw_iso_quad(px+2, py+3+r*8, w-4, 2, CLITERAL(Color){165,135,65,255});
            for(int gy=0; gy<3; gy++) {
                for(int gx=0; gx<3; gx++) {
                    float off_x = (gx * TILE_SIZE) + (TILE_SIZE/2);
                    float off_y = (gy * TILE_SIZE) + (TILE_SIZE/2);
                    Vector2 dot = to_rvec2(world_to_iso(px + off_x, py + off_y));
                    DrawCircle(dot.x, dot.y, 2, CLITERAL(Color){60, 180, 40, 255});
                    DrawCircle(dot.x-4, dot.y+2, 1.5f, CLITERAL(Color){40, 150, 30, 255});
                    DrawCircle(dot.x+4, dot.y-2, 1.5f, CLITERAL(Color){50, 170, 35, 255});
                }
            }
            break;
        }
        case BLD_BLACKSMITH: {
            /* Dark iron-grey walls with glowing forge */
            draw_iso_box(px+2, py+2, w-4, h-4, 12, CLITERAL(Color){90,85,80,255}, CLITERAL(Color){70,65,60,255}, CLITERAL(Color){50,45,40,255});
            /* Forge glow — orange ember dot at center */
            Vector2 fc = to_rvec2(world_to_iso(px + w*0.5f, py + h*0.5f));
            DrawCircle((int)fc.x, (int)fc.y - 10, 6, CLITERAL(Color){255,140,30,200});
            DrawCircle((int)fc.x, (int)fc.y - 10, 3, CLITERAL(Color){255,220,80,255});
            break;
        }
        case BLD_MARKET: {
            /* Warm ochre building with merchant awning stripe */
            draw_iso_box(px+2, py+2, w-4, h-4, 12, CLITERAL(Color){195,160,90,255}, CLITERAL(Color){175,140,70,255}, CLITERAL(Color){145,110,50,255});
            draw_iso_box(px+4, py+4, w-8, 10, 16, mc, dc, dc);   /* coloured awning */
            break;
        }
        default:
            draw_iso_box(px+2, py+2, w-4, h-4, 15, CLITERAL(Color){140,120,90,255}, dc, dc);
            break;
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

    draw_shadow(wx, wy, 10, 8);

    if(u->selected) {
        float pulse = sinf(t * 8.0f) * 1.5f;
        DrawEllipse((int)p.x,(int)p.y, 10 + pulse, 5 + pulse * 0.5f, CLITERAL(Color){80,220,100,140});
    }

    bool u_hovered = false;
    for (int i = 0; i < MAX_UNITS; i++) if (&gs->units[i] == u && ui->hover_unit == i) u_hovered = true;
    if (u_hovered && !u->selected) DrawEllipseLines((int)p.x, (int)p.y, 11, 6, C_HOVER);

    float bob = sinf(t*6.0f+(float)(u->id))*1.5f;
    float px = p.x, py = p.y - 10 + bob;

    switch(u->type){
        case UNIT_VILLAGER: {
            DrawRectangle((int)(px-4),(int)(py-3),8,8,mc);
            DrawCircle((int)px,(int)(py-7),5,CLITERAL(Color){220,185,145,255});
            if(u->state==US_GATHERING||u->state==US_BUILDING){
                float angle=t*8.0f;
                DrawLineEx((Vector2){px+2,py-2},(Vector2){px+2+cosf(angle)*8,py-2+sinf(angle)*8},2,CLITERAL(Color){140,100,40,255});
            }
            break;
        }
        case UNIT_SCOUT: {
            DrawEllipse((int)px,(int)(py+2),9,5,CLITERAL(Color){160,120,80,255});
            DrawRectangle((int)(px-3),(int)(py-5),6,7,mc);
            DrawCircle((int)px,(int)(py-9),4,CLITERAL(Color){220,185,145,255});
            break;
        }
        case UNIT_MILITIA: {
            DrawRectangle((int)(px-4),(int)(py-4),8,9,mc);
            DrawCircle((int)px,(int)(py-9),5,CLITERAL(Color){80,80,80,255});
            DrawCircle((int)px,(int)(py-8),3,CLITERAL(Color){220,185,145,255});
            DrawLineEx((Vector2){px,py-4},(Vector2){px+10,py-4},2,CLITERAL(Color){200,200,210,255});
            DrawRectangle((int)(px-10),(int)(py-5),5,7,dc);
            break;
        }
        case UNIT_MAN_AT_ARMS: {
            DrawRectangle((int)(px-5),(int)(py-5),10,10,dc);
            DrawCircle((int)px,(int)(py-10),5,CLITERAL(Color){90,90,90,255});
            DrawRectangle((int)(px-5),(int)(py-5),10,2,mc);
            DrawLineEx((Vector2){px,py-4},(Vector2){px+11,py-7},2,CLITERAL(Color){200,200,210,255});
            DrawRectangle((int)(px-12),(int)(py-6),5,8,CLITERAL(Color){120,120,110,255});
            break;
        }
        case UNIT_ARCHER: {
            DrawRectangle((int)(px-3),(int)(py-4),6,8,mc);
            DrawCircle((int)px,(int)(py-8),4,CLITERAL(Color){220,185,145,255});
            DrawCircleLines((int)(px-7),(int)(py-2),7,CLITERAL(Color){120,80,30,255});
            DrawLineEx((Vector2){px-7,py-9},(Vector2){px-7,py+5},1,CLITERAL(Color){180,140,60,255});
            float angle=sinf(t*4.0f+(float)u->id)*0.3f;
            DrawLineEx((Vector2){px,py-2},(Vector2){px-14+cosf(angle)*4,py-2+sinf(angle)*4},1,CLITERAL(Color){160,120,40,255});
            break;
        }
        case UNIT_KNIGHT: {
            DrawEllipse((int)px,(int)(py+3),11,6,CLITERAL(Color){80,60,40,255});
            DrawRectangle((int)(px-5),(int)(py-6),10,9,dc);
            DrawRectangle((int)(px-5),(int)(py-6),10,3,mc);
            DrawCircle((int)px,(int)(py-11),5,CLITERAL(Color){100,100,100,255});
            DrawLineEx((Vector2){px+5,py-8},(Vector2){px+5+cosf(u->facing)*16,py-8+sinf(u->facing)*16},2,CLITERAL(Color){180,150,60,255});
            break;
        }
        default: break;
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
    
    for(int y=0;y<MAP_H;y++) for(int x=0;x<MAP_W;x++) draw_fog(gs,x,y);

    draw_build_ghost(gs);
    draw_selection_box(gs, ui);
}
