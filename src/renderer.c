/*=============================================================
 * renderer.c  –  All world-space drawing (map, units, buildings)
 *=============================================================*/
#include "game.h"
#include "ui_state.h"
#include <stdio.h>

/* ─── Color palette ───────────────────────────────────────── */
#define C_GRASS1    CLITERAL(Color){72, 128,  50, 255}
#define C_GRASS2    CLITERAL(Color){65, 118,  45, 255}
#define C_GRASS3    CLITERAL(Color){80, 138,  55, 255}
#define C_GRASS4    CLITERAL(Color){60, 108,  40, 255}
#define C_WATER     CLITERAL(Color){38, 100, 185, 255}
#define C_WATER2    CLITERAL(Color){28,  80, 155, 255}
#define C_FOREST_G  CLITERAL(Color){28,  72,  28, 255}
#define C_FOREST_L  CLITERAL(Color){45,  95,  40, 255}
#define C_FOREST_D  CLITERAL(Color){18,  50,  18, 255}
#define C_GOLD_TILE CLITERAL(Color){ 90,  80,  40, 255}
#define C_GOLD_ORE  CLITERAL(Color){220, 185,  30, 255}
#define C_GOLD_HI   CLITERAL(Color){255, 220,  80, 255}
#define C_STONE_T   CLITERAL(Color){ 90,  85,  80, 255}
#define C_STONE_O   CLITERAL(Color){155, 148, 138, 255}
#define C_BERRY_T   CLITERAL(Color){ 70, 110,  45, 255}
#define C_BERRY_F   CLITERAL(Color){190,  40,  40, 255}
#define C_FARM_T    CLITERAL(Color){155, 125,  55, 255}
#define C_FARM_S    CLITERAL(Color){135, 105,  45, 255}

#define C_P1_MAIN   CLITERAL(Color){ 30, 110, 220, 255}
#define C_P1_DARK   CLITERAL(Color){ 15,  70, 160, 255}
#define C_AI_MAIN   CLITERAL(Color){210,  50,  40, 255}
#define C_AI_DARK   CLITERAL(Color){150,  25,  20, 255}

#define C_HP_GREEN  CLITERAL(Color){ 50, 200,  60, 255}
#define C_HP_YELLOW CLITERAL(Color){220, 200,  20, 255}
#define C_HP_RED    CLITERAL(Color){210,  40,  40, 255}
#define C_HP_BG     CLITERAL(Color){ 30,  30,  30, 200}
#define C_SEL       CLITERAL(Color){ 80, 220, 100, 180}
#define C_HOVER     CLITERAL(Color){255, 255, 255, 120}
#define C_FOG_EXP   CLITERAL(Color){  0,   0,   0, 120}
#define C_FOG_HID   CLITERAL(Color){  0,   0,   0, 235}

static Color player_color(int p)          { return p==0?C_P1_MAIN:C_AI_MAIN; }
static Color player_color_dark(int p)     { return p==0?C_P1_DARK:C_AI_DARK; }
static Color player_color_alpha(int p, unsigned char a){
    Color c=player_color(p); c.a=a; return c;
}

/* ─── Isometric Helpers ──────────────────────────────────── */

static void draw_iso_quad(float bx, float by, float bw, float bh, Color c) {
    Vector2 p1 = to_rvec2(world_to_iso(bx, by));
    Vector2 p2 = to_rvec2(world_to_iso(bx + bw, by));
    Vector2 p3 = to_rvec2(world_to_iso(bx + bw, by + bh));
    Vector2 p4 = to_rvec2(world_to_iso(bx, by + bh));
    DrawTriangle(p1, p4, p2, c);
    DrawTriangle(p4, p3, p2, c);
}

static void draw_iso_box(float bx, float by, float bw, float bh, float h, Color top, Color left, Color right) {
    Vector2 p1 = to_rvec2(world_to_iso(bx, by));
    Vector2 p2 = to_rvec2(world_to_iso(bx + bw, by));
    Vector2 p3 = to_rvec2(world_to_iso(bx + bw, by + bh));
    Vector2 p4 = to_rvec2(world_to_iso(bx, by + bh));
    
    Vector2 t1 = {p1.x, p1.y - h};
    Vector2 t2 = {p2.x, p2.y - h};
    Vector2 t3 = {p3.x, p3.y - h};
    Vector2 t4 = {p4.x, p4.y - h};
    
    DrawTriangle(p3, p2, t2, right);   /* Right face */
    DrawTriangle(p3, t2, t3, right);
    
    DrawTriangle(p4, p3, t4, left);    /* Left face */
    DrawTriangle(t4, p3, t3, left);
    
    DrawTriangle(t1, t4, t2, top);     /* Top face */
    DrawTriangle(t4, t3, t2, top);
}

/* ─── Tile rendering ─────────────────────────────────────── */

static const Color GRASS_COLS[4]={C_GRASS1,C_GRASS2,C_GRASS3,C_GRASS4};

static void draw_tile(GameState *gs, UIState *ui, int x, int y){
    Tile *t=&gs->map[y][x];
    float px=(float)(x*TILE_SIZE), py=(float)(y*TILE_SIZE);
    float s=TILE_SIZE;

    Vector2 cp = to_rvec2(world_to_iso(px + s * 0.5f, py + s * 0.5f));

    switch(t->type){
        case TILE_GRASS:
            draw_iso_quad(px, py, s, s, GRASS_COLS[t->variant]);
            break;

        case TILE_WATER: {
            float t_time = gs->game_time;
            float wave1 = sinf(t_time * 1.2f + (x + y) * 0.4f) * 0.5f + 0.5f;
            float wave2 = sinf(t_time * 0.8f - (x - y) * 0.3f) * 0.5f + 0.5f;
            float wave3 = cosf(t_time * 1.5f + (float)x * 0.5f) * 0.5f + 0.5f;
            float combined = (wave1 * 0.5f + wave2 * 0.3f + wave3 * 0.2f);
            
            Color wc = ColorAlphaBlend(C_WATER, C_WATER2, (Color){255, 255, 255, (unsigned char)(combined * 80)});
            draw_iso_quad(px, py, s, s, wc);
            
            if (combined > 0.85f) {
                draw_iso_quad(px + s * 0.2f, py + s * 0.2f, s * 0.6f, s * 0.6f, (Color){255, 255, 255, 40});
            }
            break;
        }

        case TILE_FOREST:
            draw_iso_quad(px, py, s, s, C_FOREST_G);
            DrawCircle(cp.x, cp.y - 12, s*0.42f, C_FOREST_L);
            DrawCircle(cp.x, cp.y - 15, s*0.30f, C_FOREST_D);
            DrawCircle(cp.x - 3, cp.y - 20, s*0.10f, CLITERAL(Color){70,130,55,160});
            break;

        case TILE_GOLD:
            draw_iso_quad(px, py, s, s, C_GOLD_TILE);
            draw_iso_box(px+4, py+6, s*0.4f, s*0.4f, 8, C_GOLD_HI, C_GOLD_ORE, C_GOLD_ORE);
            draw_iso_box(px+13, py+13, s*0.4f, s*0.4f, 6, C_GOLD_HI, C_GOLD_ORE, C_GOLD_ORE);
            break;

        case TILE_STONE:
            draw_iso_quad(px, py, s, s, C_STONE_T);
            DrawCircle(cp.x - 6, cp.y - 4, s*0.25f, C_STONE_O);
            DrawCircle(cp.x + 6, cp.y - 2, s*0.22f, C_STONE_O);
            DrawCircle(cp.x, cp.y + 4, s*0.18f, C_STONE_O);
            break;

        case TILE_BERRIES:
            draw_iso_quad(px, py, s, s, C_BERRY_T);
            DrawCircle(cp.x, cp.y - 8, s*0.35f, CLITERAL(Color){40,90,30,255});
            DrawCircle(cp.x - 5, cp.y - 12, 3, C_BERRY_F);
            DrawCircle(cp.x + 4, cp.y - 10, 3, C_BERRY_F);
            DrawCircle(cp.x - 2, cp.y - 5, 3, C_BERRY_F);
            break;

        case TILE_FARM:
            draw_iso_quad(px, py, s, s, C_FARM_T);
            for(int row=0;row<3;row++)
                draw_iso_quad(px+2, py+4+row*9, s-4, 2, C_FARM_S);
            break;

        default:
            draw_iso_quad(px, py, s, s, GRASS_COLS[0]);
            break;
    }

    /* Hover highlight */
    if (ui->hover_tile_x == x && ui->hover_tile_y == y) {
        Vector2 p1 = to_rvec2(world_to_iso(px, py));
        Vector2 p2 = to_rvec2(world_to_iso(px + s, py));
        Vector2 p3 = to_rvec2(world_to_iso(px + s, py + s));
        Vector2 p4 = to_rvec2(world_to_iso(px, py + s));
        DrawLineEx(p1, p2, 2, C_HOVER);
        DrawLineEx(p2, p3, 2, C_HOVER);
        DrawLineEx(p3, p4, 2, C_HOVER);
        DrawLineEx(p4, p1, 2, C_HOVER);
    }
}

/* ─── Building rendering ─────────────────────────────────── */

static void draw_hp_bar(float wx, float wy, float w, int hp, int max_hp, float above){
    if(hp==max_hp) return;
    Vector2 p = to_rvec2(world_to_iso(wx, wy));
    float bw=w, bh=4;
    float bx=p.x-bw*0.5f, by=p.y-above;
    DrawRectangleRec((Rectangle){bx,by,bw,bh},C_HP_BG);
    float frac=(float)hp/max_hp;
    Color hc = frac>0.6f ? C_HP_GREEN : frac>0.3f ? C_HP_YELLOW : C_HP_RED;
    DrawRectangleRec((Rectangle){bx,by,bw*frac,bh},hc);
}

static void draw_construction(float px,float py,float w,float h,float prog,Color mc){
    draw_iso_box(px, py, w, h, prog * 15.0f, (Color){mc.r, mc.g, mc.b, 200}, mc, mc);
}

static void draw_shadow(float wx, float wy, float rw, float rh) {
    Vector2 p = to_rvec2(world_to_iso(wx, wy));
    DrawEllipse((int)p.x, (int)p.y, (int)rw, (int)(rh * 0.5f), (Color){0, 0, 0, 60});
}

static void draw_flag(float fx, float fy, int player){
    Vector2 p = to_rvec2(world_to_iso(fx, fy));
    DrawLineEx((Vector2){p.x,p.y},(Vector2){p.x,p.y-15},2,(Color){80,60,40,255});
    Color fc=player_color(player);
    DrawTriangle((Vector2){p.x,p.y-15},(Vector2){p.x+10,p.y-10},(Vector2){p.x,p.y-5},fc);
}

static void draw_smoke(float bx, float by, float time, int seed) {
    Vector2 p = to_rvec2(world_to_iso(bx, by));
    for (int i = 0; i < 3; i++) {
        float f = fmodf(time * 0.7f + (float)i * 0.33f + (float)seed * 0.1f, 1.0f);
        float ox = sinf(time * 2.0f + (float)i + (float)seed) * 5.0f;
        float oy = -f * 30.0f;
        float size = 4.0f + f * 8.0f;
        unsigned char alpha = (unsigned char)((1.0f - f) * 160);
        DrawCircle((int)(p.x + ox), (int)(p.y - 10 + oy), size, (Color){60, 60, 60, alpha});
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
            break;
        }
        default:
            draw_iso_box(px+2, py+2, w-4, h-4, 15, CLITERAL(Color){140,120,90,255}, dc, dc);
            break;
    }

    if(b->hp <= b->max_hp / 2) {
        draw_smoke(px + w*0.4f, py + h*0.4f, gs->game_time, b->id);
    }
    if(b->hp <= b->max_hp / 4) {
        draw_smoke(px + w*0.6f, py + h*0.6f, gs->game_time, b->id + 100);
    }

    /* Selection box */
    if(b->selected && b->player==0){
        Vector2 p1 = to_rvec2(world_to_iso(px - 2, py - 2));
        Vector2 p2 = to_rvec2(world_to_iso(px + w + 2, py - 2));
        Vector2 p3 = to_rvec2(world_to_iso(px + w + 2, py + h + 2));
        Vector2 p4 = to_rvec2(world_to_iso(px - 2, py + h + 2));
        DrawLineEx(p1, p2, 2, C_SEL);
        DrawLineEx(p2, p3, 2, C_SEL);
        DrawLineEx(p3, p4, 2, C_SEL);
        DrawLineEx(p4, p1, 2, C_SEL);
    }

    /* Hover box */
    bool hovered = false;
    for (int i = 0; i < MAX_BUILDINGS; i++) if (&gs->buildings[i] == b && ui->hover_building == i) hovered = true;
    if (hovered) {
        Vector2 p1 = to_rvec2(world_to_iso(px, py));
        Vector2 p2 = to_rvec2(world_to_iso(px + w, py));
        Vector2 p3 = to_rvec2(world_to_iso(px + w, py + h));
        Vector2 p4 = to_rvec2(world_to_iso(px, py + h));
        DrawLineEx(p1, p2, 1.5f, C_HOVER);
        DrawLineEx(p2, p3, 1.5f, C_HOVER);
        DrawLineEx(p3, p4, 1.5f, C_HOVER);
        DrawLineEx(p4, p1, 1.5f, C_HOVER);
    }

    draw_hp_bar(px+w*0.5f,py+h*0.5f,w*0.8f,b->hp,b->max_hp,35);

    /* Production progress arc */
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

    /* Drop shadow */
    draw_shadow(wx, wy, 10, 8);

    /* Selection circle (pulsing) */
    if(u->selected) {
        float pulse = sinf(t * 8.0f) * 1.5f;
        DrawEllipse((int)p.x,(int)p.y, 10 + pulse, 5 + pulse * 0.5f, CLITERAL(Color){80,220,100,140});
    }

    /* Hover circle */
    bool u_hovered = false;
    for (int i = 0; i < MAX_UNITS; i++) if (&gs->units[i] == u && ui->hover_unit == i) u_hovered = true;
    if (u_hovered && !u->selected) {
        DrawEllipseLines((int)p.x, (int)p.y, 11, 6, C_HOVER);
    }

    float bob = sinf(t*6.0f+(float)(u->id))*1.5f;
    float px = p.x, py = p.y - 10 + bob;

    switch(u->type){
        case UNIT_VILLAGER: {
            DrawRectangle((int)(px-4),(int)(py-3),8,8,mc);
            DrawCircle((int)px,(int)(py-7),5,CLITERAL(Color){220,185,145,255});
            if(u->state==US_GATHERING||u->state==US_BUILDING){
                float angle=t*8.0f;
                DrawLineEx((Vector2){px+2,py-2},
                           (Vector2){px+2+cosf(angle)*8,py-2+sinf(angle)*8},2,
                           CLITERAL(Color){140,100,40,255});
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
            DrawLineEx((Vector2){px,py-2},
                       (Vector2){px-14+cosf(angle)*4,py-2+sinf(angle)*4},1,
                       CLITERAL(Color){160,120,40,255});
            break;
        }
        case UNIT_KNIGHT: {
            DrawEllipse((int)px,(int)(py+3),11,6,CLITERAL(Color){80,60,40,255});
            DrawRectangle((int)(px-5),(int)(py-6),10,9,dc);
            DrawRectangle((int)(px-5),(int)(py-6),10,3,mc);
            DrawCircle((int)px,(int)(py-11),5,CLITERAL(Color){100,100,100,255});
            DrawLineEx((Vector2){px+5,py-8},
                       (Vector2){px+5+cosf(u->facing)*16,py-8+sinf(u->facing)*16},2,
                       CLITERAL(Color){180,150,60,255});
            break;
        }
        default: break;
    }

    /* Carry resource indicator */
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

/* ─── Fog overlay ─────────────────────────────────────────── */

static void draw_fog(GameState *gs, int x, int y){
    FogState fs=gs->map[y][x].fog[0];
    if(fs==FOG_VISIBLE) return;
    float px=(float)(x*TILE_SIZE), py=(float)(y*TILE_SIZE);
    float s=TILE_SIZE;
    Color fc = (fs==FOG_HIDDEN) ? C_FOG_HID : C_FOG_EXP;
    draw_iso_quad(px, py, s, s, fc);
}

/* ─── Selection box ───────────────────────────────────────── */

static void draw_selection_box(GameState *gs, UIState *ui){
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
}

/* ─── Attack-move cursor indicator ──────────────────────────── */

/* ─── Master render ───────────────────────────────────────── */
void renderer_draw_world(GameState *gs, UIState *ui){
    /* 1. Tiles */
    for(int y=0;y<MAP_H;y++) {
        for(int x=0;x<MAP_W;x++){
            if(gs->map[y][x].fog[0]!=FOG_HIDDEN) draw_tile(gs, ui, x, y);
        }
    }

    /* 2. Buildings */
    for(int i=0;i<MAX_BUILDINGS;i++){
        Building *b=&gs->buildings[i];
        if(!b->active) continue;
        int bx=b->tx+b->tw/2, by=b->ty+b->th/2;
        if(gs->map[clampi(by,0,MAP_H-1)][clampi(bx,0,MAP_W-1)].fog[0]==FOG_HIDDEN) continue;
        draw_building(gs, ui, b);
    }

    /* 3. Units */
    for(int i=0;i<MAX_UNITS;i++){
        Unit *u=&gs->units[i];
        if(!u->active||u->state==US_DEAD) continue;
        int utx=(int)(u->wx/TILE_SIZE), uty=(int)(u->wy/TILE_SIZE);
        if(!map_in_bounds(utx,uty)) continue;
        FogState fs=gs->map[uty][utx].fog[0];
        /* Hide enemy units in fog */
        if(u->player==1 && fs!=FOG_VISIBLE) continue;

        draw_unit(gs, ui, u, gs->game_time);
    }

    /* 4. Fog overlay */
    for(int y=0;y<MAP_H;y++) for(int x=0;x<MAP_W;x++){
        draw_fog(gs,x,y);
    }

    /* 5. Draw ghost building */
    draw_build_ghost(gs);

    /* 6. Selection box */
    draw_selection_box(gs, ui);
}
