/*=============================================================
 * renderer_tiles.c  –  Tile and fog drawing
 *=============================================================*/
#include "game.h"
#include "ui_state.h"
#include "renderer.h"
#include "net.h"
#include <stdio.h>

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
#define C_WATER     CLITERAL(Color){38, 100, 185, 255}
#define C_WATER2    CLITERAL(Color){28,  80, 155, 255}

void draw_tile(GameState *gs, UIState *ui, int x, int y){
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

void draw_fog(GameState *gs, int x, int y){
    int lp = net_get_local_player();
    FogState fs=gs->map[y][x].fog[lp];
    if(fs==FOG_VISIBLE) return;
    float px=(float)(x*TILE_SIZE), py=(float)(y*TILE_SIZE);
    float s=TILE_SIZE;
    Color fc = (fs==FOG_HIDDEN) ? C_FOG_HID : C_FOG_EXP;
    draw_iso_quad(px, py, s, s, fc);
}
