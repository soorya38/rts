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

static void draw_grass_base(UIState *ui, uint8_t variant, float px, float py, float s){
    Texture2D tex = ui ? ui->tex_land_grass[variant % 4] : (Texture2D){0};
    if (tex.id == 0) {
        draw_iso_quad(px, py, s, s, GRASS_COLS[variant % 4]);
        return;
    }

    /* The extracted land art includes the tile thickness, so we draw it a bit
       wider than the logical diamond and anchor it near the bottom edge. */
    float target_w = s * 2.1f;
    float target_h = ((float)tex.height / (float)tex.width) * target_w;
    Vector2 bc = to_rvec2(world_to_iso(px + s * 0.5f, py + s * 0.5f));
    Rectangle src = (Rectangle){0.0f, 0.0f, (float)tex.width, (float)tex.height};
    Rectangle dst = (Rectangle){bc.x - target_w * 0.5f, bc.y - target_h + s * 0.92f, target_w, target_h};
    DrawTexturePro(tex, src, dst, (Vector2){0}, 0.0f, WHITE);
}

static void draw_forest_canopy(UIState *ui, Tile *t, int x, int y, float px, float py, float s) {
    unsigned tree_seed = (unsigned)(x * 92821u + y * 68917u + t->variant * 131u);
    int tree_idx = (int)(tree_seed % TREE_VARIANT_COUNT);
    Texture2D tex = ui->tex_env_trees[tree_idx];
    if (tex.id != 0) {
        float tw = tex.width;
        float th = tex.height;
        float size_jitter = 1.90f + (float)((tree_seed >> 3) % 20) / 100.0f;
        float scale = (s * size_jitter) / tw;
        float dw = tw * scale;
        float dh = th * scale;
        float offset_x = (float)((int)((tree_seed >> 8) % 7) - 3) * 0.6f;
        float offset_y = (float)((int)((tree_seed >> 12) % 5) - 2) * 0.8f;
        bool flip_x = ((tree_seed >> 16) & 1u) != 0;
        float src_w = flip_x ? -tw : tw;
        float src_x = flip_x ? tw : 0.0f;
        unsigned tint_delta = (tree_seed >> 20) % 28u;
        unsigned char green = (unsigned char)(232 - tint_delta);
        unsigned char blue = (unsigned char)(232 - tint_delta / 2u);
        Color tint = (Color){255, green, blue, 255};
        Vector2 bc = to_rvec2(world_to_iso(px + s * 0.5f, py + s * 0.5f));
        Rectangle src = {src_x, 0.0f, src_w, th};
        Rectangle dst = {bc.x - dw * 0.5f + offset_x, bc.y - dh + s * 0.42f + offset_y, dw, dh};
        DrawTexturePro(tex, src, dst, (Vector2){0,0}, 0.0f, tint);
    } else {
        draw_iso_box(px + 6, py + 6, s - 12, s - 12, 22,
                     C_FOREST_L, C_FOREST_D, C_FOREST_D);
    }
}

void draw_tile(GameState *gs, UIState *ui, int x, int y){
    Tile *t=&gs->map[y][x];
    float px=(float)(x*TILE_SIZE), py=(float)(y*TILE_SIZE);
    float s=TILE_SIZE;
    switch(t->type){
        case TILE_GRASS:
            draw_grass_base(ui, t->variant, px, py, s);
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

        case TILE_FOREST: {
            draw_grass_base(ui, t->variant, px, py, s);
            draw_forest_canopy(ui, t, x, y, px, py, s);
            break;
        }

        case TILE_GOLD: {
            draw_grass_base(ui, t->variant, px, py, s);
            draw_iso_box(px + 5, py + 5, s - 10, s - 10, 14,
                         C_GOLD_HI, C_GOLD_ORE, C_GOLD_TILE);
            break;
        }

        case TILE_STONE: {
            draw_grass_base(ui, t->variant, px, py, s);
            draw_iso_box(px + 5, py + 5, s - 10, s - 10, 14,
                         CLITERAL(Color){190, 186, 176, 255},
                         C_STONE_O, C_STONE_T);
            break;
        }

        case TILE_BERRIES: {
            draw_grass_base(ui, t->variant, px, py, s);
            draw_iso_box(px + 7, py + 7, s - 14, s - 14, 10,
                         CLITERAL(Color){120, 160, 75, 255},
                         C_BERRY_T, C_BERRY_F);
            break;
        }

        case TILE_FARM:
            draw_iso_quad(px, py, s, s, C_FARM_T);
            for(int row=0;row<3;row++)
                draw_iso_quad(px+2, py+4+row*9, s-4, 2, C_FARM_S);
            break;

        case TILE_DESERT: {
            /* Sandy terrain with subtle variation */
            Color sand_cols[4] = {
                CLITERAL(Color){194, 168, 118, 255},
                CLITERAL(Color){188, 160, 112, 255},
                CLITERAL(Color){200, 174, 124, 255},
                CLITERAL(Color){182, 155, 108, 255}
            };
            draw_iso_quad(px, py, s, s, sand_cols[t->variant % 4]);
            /* Occasional wind-ripple effect */
            if ((x + y) % 5 == 0) {
                draw_iso_quad(px + 4, py + s*0.3f, s - 8, 1.5f,
                              CLITERAL(Color){210, 188, 140, 120});
                draw_iso_quad(px + 6, py + s*0.6f, s - 12, 1.5f,
                              CLITERAL(Color){175, 150, 100, 100});
            }
            break;
        }

        case TILE_ROAD: {
            /* Packed earth/cobblestone road */
            draw_grass_base(ui, t->variant, px, py, s);
            Color road_main = CLITERAL(Color){125, 105, 75, 255};
            Color road_edge = CLITERAL(Color){105, 88, 62, 255};
            draw_iso_quad(px + 4, py + 4, s - 8, s - 8, road_main);
            /* Edge markings for depth */
            draw_iso_quad(px + 3, py + 3, s - 6, 2, road_edge);
            draw_iso_quad(px + 3, py + s - 5, s - 6, 2, road_edge);
            break;
        }

        default:
            draw_grass_base(ui, 0, px, py, s);
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

void draw_forest_overlay(GameState *gs, UIState *ui, int x, int y){
    Tile *t = &gs->map[y][x];
    if (t->type != TILE_FOREST) return;
    float px = (float)(x * TILE_SIZE);
    float py = (float)(y * TILE_SIZE);
    draw_forest_canopy(ui, t, x, y, px, py, (float)TILE_SIZE);
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
