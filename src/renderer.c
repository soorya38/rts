/*=============================================================
 * renderer.c  –  All world-space drawing (map, units, buildings)
 *=============================================================*/
#include "game.h"
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
#define C_FOG_EXP   CLITERAL(Color){  0,   0,   0, 120}
#define C_FOG_HID   CLITERAL(Color){  0,   0,   0, 235}

static Color player_color(int p)          { return p==0?C_P1_MAIN:C_AI_MAIN; }
static Color player_color_dark(int p)     { return p==0?C_P1_DARK:C_AI_DARK; }
static Color player_color_alpha(int p, unsigned char a){
    Color c=player_color(p); c.a=a; return c;
}

/* ─── Tile rendering ─────────────────────────────────────── */

static const Color GRASS_COLS[4]={C_GRASS1,C_GRASS2,C_GRASS3,C_GRASS4};

static void draw_tile(GameState *gs, int x, int y){
    Tile *t=&gs->map[y][x];
    float px=(float)(x*TILE_SIZE), py=(float)(y*TILE_SIZE);
    float s=TILE_SIZE;

    switch(t->type){
        case TILE_GRASS:
            DrawRectangleV((Vector2){px,py},(Vector2){s,s},GRASS_COLS[t->variant]);
            break;

        case TILE_WATER: {
            /* Animated ripple using game_time */
            float wave=sinf(gs->game_time*1.2f+(x+y)*0.4f)*0.5f+0.5f;
            Color wc=ColorAlphaBlend(C_WATER,C_WATER2,(Color){255,255,255,(unsigned char)(wave*60)});
            DrawRectangleV((Vector2){px,py},(Vector2){s,s},wc);
            break;
        }

        case TILE_FOREST:
            /* Ground */
            DrawRectangleV((Vector2){px,py},(Vector2){s,s},C_FOREST_G);
            /* Tree canopy (circle) */
            DrawCircle((int)(px+s*0.5f),(int)(py+s*0.55f),(int)(s*0.42f),C_FOREST_L);
            DrawCircle((int)(px+s*0.5f),(int)(py+s*0.45f),(int)(s*0.30f),C_FOREST_D);
            /* Bright highlight */
            DrawCircle((int)(px+s*0.45f),(int)(py+s*0.38f),(int)(s*0.10f),
                       CLITERAL(Color){70,130,55,160});
            break;

        case TILE_GOLD:
            DrawRectangleV((Vector2){px,py},(Vector2){s,s},C_GOLD_TILE);
            /* Ore chunks */
            DrawRectangle((int)(px+4),(int)(py+6),(int)(s*0.3f),(int)(s*0.35f),C_GOLD_ORE);
            DrawRectangle((int)(px+13),(int)(py+9),(int)(s*0.35f),(int)(s*0.3f),C_GOLD_ORE);
            DrawRectangle((int)(px+8),(int)(py+16),(int)(s*0.25f),(int)(s*0.28f),C_GOLD_ORE);
            DrawRectangle((int)(px+4),(int)(py+6),(int)(4),(int)(4),C_GOLD_HI);
            DrawRectangle((int)(px+13),(int)(py+9),(int)(4),(int)(4),C_GOLD_HI);
            break;

        case TILE_STONE:
            DrawRectangleV((Vector2){px,py},(Vector2){s,s},C_STONE_T);
            DrawCircle((int)(px+8),(int)(py+14),(int)(s*0.25f),C_STONE_O);
            DrawCircle((int)(px+20),(int)(py+10),(int)(s*0.22f),C_STONE_O);
            DrawCircle((int)(px+14),(int)(py+20),(int)(s*0.18f),C_STONE_O);
            DrawCircle((int)(px+8),(int)(py+13),(int)(4),CLITERAL(Color){200,192,182,255});
            break;

        case TILE_BERRIES:
            DrawRectangleV((Vector2){px,py},(Vector2){s,s},C_BERRY_T);
            /* Bush shape */
            DrawCircle((int)(px+s*0.4f),(int)(py+s*0.55f),(int)(s*0.28f),
                       CLITERAL(Color){40,90,30,255});
            /* Berries */
            DrawCircle((int)(px+10),(int)(py+14),(int)(4),C_BERRY_F);
            DrawCircle((int)(px+18),(int)(py+12),(int)(4),C_BERRY_F);
            DrawCircle((int)(px+14),(int)(py+19),(int)(3),C_BERRY_F);
            DrawCircle((int)(px+22),(int)(py+18),(int)(3),C_BERRY_F);
            break;

        case TILE_FARM:
            DrawRectangleV((Vector2){px,py},(Vector2){s,s},C_FARM_T);
            /* Furrow lines */
            for(int row=0;row<3;row++)
                DrawRectangle((int)(px+2),(int)(py+4+row*9),(int)(s-4),(int)(4),C_FARM_S);
            break;

        default:
            DrawRectangleV((Vector2){px,py},(Vector2){s,s},GRASS_COLS[0]);
            break;
    }
}

/* ─── Building rendering ─────────────────────────────────── */

static void draw_hp_bar(float wx, float wy, float w, int hp, int max_hp, float above){
    if(hp==max_hp) return;
    float bw=w, bh=4;
    float bx=wx-bw*0.5f, by=wy-above;
    DrawRectangleRec((Rectangle){bx,by,bw,bh},C_HP_BG);
    float frac=(float)hp/max_hp;
    Color hc = frac>0.6f ? C_HP_GREEN : frac>0.3f ? C_HP_YELLOW : C_HP_RED;
    DrawRectangleRec((Rectangle){bx,by,bw*frac,bh},hc);
}

static void draw_construction(float px,float py,float w,float h,float prog,Color mc){
    /* Scaffold pattern */
    DrawRectangleRec((Rectangle){px,py+h*(1.0f-prog),w,h*prog},
                     CLITERAL(Color){mc.r,mc.g,mc.b,200});
    for(int i=0;i<4;i++)
        DrawLineEx((Vector2){px+w*i/4,py},(Vector2){px+w*i/4,py+h},1.0f,
                   CLITERAL(Color){200,180,120,180});
}

static void draw_flag(float fx, float fy, int player){
    DrawLineEx((Vector2){fx,fy},(Vector2){fx,fy-10},2,(Color){80,60,40,255});
    Color fc=player_color(player);
    DrawTriangle((Vector2){fx,fy-10},(Vector2){fx+8,fy-6},(Vector2){fx,fy-2},fc);
}

static void draw_building(Building *b){
    float px=(float)(b->tx*TILE_SIZE), py=(float)(b->ty*TILE_SIZE);
    float w=(float)(b->tw*TILE_SIZE), h=(float)(b->th*TILE_SIZE);
    Color mc=player_color(b->player);
    Color dc=player_color_dark(b->player);

    if(!b->complete){
        draw_construction(px,py,w,h,b->construction,mc);
        draw_hp_bar(px+w*0.5f,py,w*0.8f,b->hp,b->max_hp,6);
        return;
    }

    switch(b->type){
        case BLD_TOWN_CENTER: {
            /* Stone base */
            DrawRectangleRec((Rectangle){px+2,py+2,w-4,h-4},CLITERAL(Color){140,128,108,255});
            /* Central tower */
            DrawRectangleRec((Rectangle){px+w*0.25f,py+h*0.2f,w*0.5f,h*0.6f},
                             CLITERAL(Color){160,148,128,255});
            /* Roof */
            DrawRectangleRec((Rectangle){px+w*0.2f,py+h*0.15f,w*0.6f,h*0.12f},mc);
            /* Windows */
            DrawRectangle((int)(px+w*0.35f),(int)(py+h*0.3f),8,10,CLITERAL(Color){50,40,20,255});
            DrawRectangle((int)(px+w*0.55f),(int)(py+h*0.3f),8,10,CLITERAL(Color){50,40,20,255});
            /* Entry door */
            DrawRectangle((int)(px+w*0.42f),(int)(py+h*0.65f),12,18,CLITERAL(Color){60,40,20,255});
            /* Merlons */
            for(int i=0;i<4;i++)
                DrawRectangle((int)(px+w*0.22f+i*(w*0.14f)),(int)(py+h*0.12f),8,7,
                              CLITERAL(Color){140,128,108,255});
            /* Flag */
            draw_flag(px+w*0.5f,py+h*0.12f,b->player);
            /* Player color band */
            DrawRectangleRec((Rectangle){px+w*0.2f,py+h*0.75f,w*0.6f,5},mc);
            break;
        }
        case BLD_HOUSE: {
            DrawRectangleRec((Rectangle){px+2,py+6,w-4,h-8},CLITERAL(Color){180,158,110,255});
            /* Roof (triangle-ish) */
            DrawRectangleRec((Rectangle){px,py,w,9},dc);
            DrawRectangle((int)(px+2),(int)(py+9),4,4,dc);
            DrawRectangle((int)(w+px-6),(int)(py+9),4,4,dc);
            /* Door */
            DrawRectangle((int)(px+w*0.38f),(int)(py+h*0.55f),7,10,CLITERAL(Color){80,55,25,255});
            /* Window */
            DrawRectangle((int)(px+6),(int)(py+h*0.45f),6,6,CLITERAL(Color){180,220,255,200});
            break;
        }
        case BLD_BARRACKS:
        case BLD_ARCHERY_RANGE:
        case BLD_STABLE: {
            Color wc=CLITERAL(Color){110,95,75,255};
            DrawRectangleRec((Rectangle){px+2,py+4,w-4,h-6},wc);
            DrawRectangleRec((Rectangle){px,py,w,8},dc);  /* roof band */
            /* Battlements */
            for(int i=0;i<3;i++)
                DrawRectangle((int)(px+6+i*((int)w/3-2)),(int)(py-2),(int)(w/3-4),7,
                              CLITERAL(Color){90,78,60,255});
            /* Entry */
            DrawRectangle((int)(px+w*0.4f),(int)(py+h*0.6f),10,14,CLITERAL(Color){40,30,15,255});
            /* Player color stripe */
            DrawRectangleRec((Rectangle){px+4,py+4,w-8,5},mc);
            /* Label icon */
            if(b->type==BLD_BARRACKS){
                DrawRectangle((int)(px+w*0.35f),(int)(py+h*0.3f),4,16,CLITERAL(Color){180,180,60,255});
                DrawRectangle((int)(px+w*0.35f-4),(int)(py+h*0.3f),12,4,CLITERAL(Color){180,180,60,255});
            }
            break;
        }
        case BLD_MILL:
        case BLD_LUMBER_CAMP:
        case BLD_MINING_CAMP: {
            DrawRectangleRec((Rectangle){px+2,py+4,w-4,h-6},CLITERAL(Color){150,120,75,255});
            DrawRectangleRec((Rectangle){px,py,w,6},dc);
            DrawRectangle((int)(px+w*0.35f),(int)(py+h*0.55f),8,10,CLITERAL(Color){60,40,20,255});
            DrawRectangleRec((Rectangle){px+2,py+h-6,w-4,5},mc);
            break;
        }
        case BLD_FARM: {
            /* Plowed field */
            DrawRectangleRec((Rectangle){px,py,w,h},CLITERAL(Color){145,115,55,255});
            for(int r=0;r<4;r++)
                DrawRectangle((int)(px+2),(int)(py+3+r*((int)h/4-1)),(int)(w-4),
                              (int)(h/4-3),CLITERAL(Color){165,135,65,255});
            /* Fence posts */
            DrawRectangle((int)(px),(int)(py),(int)(w),2,CLITERAL(Color){100,70,30,255});
            DrawRectangle((int)(px),(int)(py+h-2),(int)(w),2,CLITERAL(Color){100,70,30,255});
            break;
        }
        default:
            DrawRectangleRec((Rectangle){px+2,py+2,w-4,h-4},CLITERAL(Color){140,120,90,255});
            break;
    }

    /* Health bar */
    draw_hp_bar(px+w*0.5f,py,w*0.85f,b->hp,b->max_hp,7);

    /* Selection outline */
    if(b->selected)
        DrawRectangleLinesEx((Rectangle){px-2,py-2,w+4,h+4},3,C_SEL);

    /* Production progress arc */
    if(b->queue_len>0){
        float progress=1.0f-(b->train_timer/building_train_time(b->queue[0]));
        DrawRectangleRec((Rectangle){px+2,py+h+2,w-4,4},C_HP_BG);
        DrawRectangleRec((Rectangle){px+2,py+h+2,(w-4)*progress,4},C_HP_GREEN);
    }
}

/* ─── Unit rendering ─────────────────────────────────────── */

static void draw_unit(Unit *u, float t){
    if(u->state==US_DEAD) return;
    float wx=u->wx, wy=u->wy;
    float alpha = u->state==US_DYING ? clampf(u->death_timer/0.8f,0,1)*255 : 255;
    Color mc = player_color_alpha(u->player,(unsigned char)alpha);
    Color dc = player_color_dark(u->player); dc.a=(unsigned char)alpha;

    /* Selection circle */
    if(u->selected)
        DrawEllipse((int)wx,(int)wy,10,5,CLITERAL(Color){80,220,100,140});

    float bob=sinf(t*6.0f+(float)(u->id))*1.5f;

    switch(u->type){
        case UNIT_VILLAGER: {
            /* Body */
            DrawRectangle((int)(wx-4),(int)(wy-3+bob),8,8,mc);
            /* Head */
            DrawCircle((int)wx,(int)(wy-7+bob),5,CLITERAL(Color){220,185,145,255});
            /* Tool (axe/pickaxe handle) */
            if(u->state==US_GATHERING||u->state==US_BUILDING){
                float angle=t*8.0f;
                DrawLineEx((Vector2){wx+2,wy-2+bob},
                           (Vector2){wx+2+cosf(angle)*8,wy-2+bob+sinf(angle)*8},2,
                           CLITERAL(Color){140,100,40,255});
            }
            break;
        }
        case UNIT_SCOUT: {
            /* Horse body */
            DrawEllipse((int)wx,(int)(wy+2+bob),9,5,CLITERAL(Color){160,120,80,255});
            /* Rider */
            DrawRectangle((int)(wx-3),(int)(wy-5+bob),6,7,mc);
            DrawCircle((int)wx,(int)(wy-9+bob),4,CLITERAL(Color){220,185,145,255});
            break;
        }
        case UNIT_MILITIA: {
            DrawRectangle((int)(wx-4),(int)(wy-4+bob),8,9,mc);
            DrawCircle((int)wx,(int)(wy-9+bob),5,CLITERAL(Color){80,80,80,255}); /* helmet */
            DrawCircle((int)wx,(int)(wy-8+bob),3,CLITERAL(Color){220,185,145,255});
            /* Sword */
            DrawLineEx((Vector2){wx,wy-4+bob},(Vector2){wx+10,wy-4+bob},2,
                       CLITERAL(Color){200,200,210,255});
            /* Shield */
            DrawRectangle((int)(wx-10),(int)(wy-5+bob),5,7,dc);
            break;
        }
        case UNIT_MAN_AT_ARMS: {
            DrawRectangle((int)(wx-5),(int)(wy-5+bob),10,10,dc);      /* chainmail */
            DrawCircle((int)wx,(int)(wy-10+bob),5,CLITERAL(Color){90,90,90,255});
            DrawRectangle((int)(wx-5),(int)(wy-5+bob),10,2,mc);       /* color band */
            DrawLineEx((Vector2){wx,wy-4+bob},(Vector2){wx+11,wy-7+bob},2,
                       CLITERAL(Color){200,200,210,255});
            DrawRectangle((int)(wx-12),(int)(wy-6+bob),5,8,
                          CLITERAL(Color){120,120,110,255});
            break;
        }
        case UNIT_ARCHER: {
            DrawRectangle((int)(wx-3),(int)(wy-4+bob),6,8,mc);
            DrawCircle((int)wx,(int)(wy-8+bob),4,CLITERAL(Color){220,185,145,255});
            /* Bow */
            DrawCircleLines((int)(wx-7),(int)(wy-2+bob),7,CLITERAL(Color){120,80,30,255});
            DrawLineEx((Vector2){wx-7,wy-9+bob},(Vector2){wx-7,wy+5+bob},1,
                       CLITERAL(Color){180,140,60,255});
            /* Arrow */
            float angle=sinf(t*4.0f+(float)u->id)*0.3f;
            DrawLineEx((Vector2){wx,wy-2+bob},
                       (Vector2){wx-14+cosf(angle)*4,wy-2+bob+sinf(angle)*4},1,
                       CLITERAL(Color){160,120,40,255});
            break;
        }
        case UNIT_KNIGHT: {
            /* Horse */
            DrawEllipse((int)wx,(int)(wy+3+bob),11,6,CLITERAL(Color){80,60,40,255});
            /* Armored rider */
            DrawRectangle((int)(wx-5),(int)(wy-6+bob),10,9,dc);
            DrawRectangle((int)(wx-5),(int)(wy-6+bob),10,3,mc);
            DrawCircle((int)wx,(int)(wy-11+bob),5,CLITERAL(Color){100,100,100,255});
            /* Lance */
            DrawLineEx((Vector2){wx+5,wy-8+bob},
                       (Vector2){wx+5+cosf(u->facing)*16,wy-8+bob+sinf(u->facing)*16},2,
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
        DrawCircle((int)(wx+6),(int)(wy-12+bob),4,rc);
    }

    /* Health bar */
    draw_hp_bar(wx,wy-14,18,u->hp,u->max_hp,0);
}

/* ─── Fog overlay ─────────────────────────────────────────── */

static void draw_fog(GameState *gs, int x, int y){
    FogState fs=gs->map[y][x].fog[0];
    if(fs==FOG_VISIBLE) return;
    float px=(float)(x*TILE_SIZE), py=(float)(y*TILE_SIZE);
    float s=TILE_SIZE;
    Color fc = (fs==FOG_HIDDEN) ? C_FOG_HID : C_FOG_EXP;
    DrawRectangleV((Vector2){px,py},(Vector2){s,s},fc);
}

/* ─── Selection box ───────────────────────────────────────── */

static void draw_selection_box(GameState *gs){
    if(!gs->box_selecting) return;
    Vector2 a=GetScreenToWorld2D(gs->box_start,gs->camera);
    Vector2 b=GetScreenToWorld2D(GetMousePosition(),gs->camera);
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
    DrawRectangleRec((Rectangle){px,py,w,h},gc);
    DrawRectangleLinesEx((Rectangle){px,py,w,h},2,gs->build_mode.valid?GREEN:RED);
}

/* ─── Attack-move cursor indicator ──────────────────────────── */

/* ─── Master render ───────────────────────────────────────── */
void renderer_draw_world(GameState *gs){
    /* 1. Tiles */
    int vx0=clampi((int)(gs->camera.target.x/TILE_SIZE)-(SCREEN_W/TILE_SIZE/2)-2,0,MAP_W-1);
    int vy0=clampi((int)(gs->camera.target.y/TILE_SIZE)-(SCREEN_H/TILE_SIZE/2)-2,0,MAP_H-1);
    int vx1=clampi(vx0+SCREEN_W/TILE_SIZE+4,0,MAP_W-1);
    int vy1=clampi(vy0+SCREEN_H/TILE_SIZE+4,0,MAP_H-1);

    for(int y=vy0;y<=vy1;y++) for(int x=vx0;x<=vx1;x++){
        if(gs->map[y][x].fog[0]!=FOG_HIDDEN) draw_tile(gs,x,y);
    }

    /* 2. Buildings */
    for(int i=0;i<MAX_BUILDINGS;i++){
        Building *b=&gs->buildings[i];
        if(!b->active) continue;
        int bx=b->tx+b->tw/2, by=b->ty+b->th/2;
        if(gs->map[clampi(by,0,MAP_H-1)][clampi(bx,0,MAP_W-1)].fog[0]==FOG_HIDDEN) continue;
        draw_building(b);
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
        draw_unit(u,gs->game_time);
    }

    /* 4. Fog overlay */
    for(int y=vy0;y<=vy1;y++) for(int x=vx0;x<=vx1;x++)
        draw_fog(gs,x,y);

    /* 5. Build ghost */
    draw_build_ghost(gs);

    /* 6. Selection box */
    draw_selection_box(gs);

    /* 7. Resource tile selection indicator */
    if(gs->sel_tile_x>=0 && gs->sel_tile_y>=0 &&
       map_in_bounds(gs->sel_tile_x,gs->sel_tile_y)){
        Tile *t=&gs->map[gs->sel_tile_y][gs->sel_tile_x];
        /* Only draw if tile is visible and still a resource */
        if(gs->map[gs->sel_tile_y][gs->sel_tile_x].fog[0]==FOG_VISIBLE &&
           (t->type==TILE_FOREST||t->type==TILE_GOLD||t->type==TILE_STONE||
            t->type==TILE_BERRIES||t->type==TILE_FARM)){
            float px=(float)(gs->sel_tile_x*TILE_SIZE);
            float py=(float)(gs->sel_tile_y*TILE_SIZE);
            float s=(float)TILE_SIZE;
            /* Pulsing selection ring */
            float pulse=sinf(gs->game_time*5.0f)*0.5f+0.5f;
            unsigned char palpha=(unsigned char)(140+pulse*100);
            Color ring=CLITERAL(Color){255,220,60,palpha};
            DrawRectangleLinesEx((Rectangle){px-1,py-1,s+2,s+2},2,ring);
            /* Small floating label above the tile */
            static const char *RTYPE_LABEL[]={"Food","Wood","Gold","Stone"};
            ResType rtype;
            switch(t->type){
                case TILE_FOREST:  rtype=RES_WOOD;  break;
                case TILE_GOLD:    rtype=RES_GOLD;  break;
                case TILE_STONE:   rtype=RES_STONE; break;
                default:           rtype=RES_FOOD;  break;
            }
            char lbl[32];
            snprintf(lbl,sizeof(lbl),"%s: %d",RTYPE_LABEL[rtype],t->resource_amt);
            int tw2=MeasureText(lbl,10);
            int lx=(int)(px+s*0.5f)-tw2/2;
            int ly=(int)(py)-16;
            DrawRectangle(lx-3,ly-2,tw2+6,14,CLITERAL(Color){10,8,4,210});
            DrawText(lbl,lx,ly,10,CLITERAL(Color){255,220,80,255});
        } else {
            /* Tile depleted or hidden → clear selection */
            gs->sel_tile_x=-1; gs->sel_tile_y=-1;
        }
    }
}
