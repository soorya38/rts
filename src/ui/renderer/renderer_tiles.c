/*=============================================================
 * renderer_tiles.c  –  Tile drawing and fog-of-war overlays
 *
 * Each tile type has its own visual: animated water, textured
 * forests, gold/stone deposits drawn as isometric boxes, etc.
 * Fog is drawn as a semi-transparent overlay after all entities.
 *=============================================================*/
#include "game.h"
#include "ui_state.h"
#include "renderer.h"
#include "net.h"
#include <stdio.h>

/* ── Terrain colours (duplicated from renderer_fx.c for locality) */
#define C_FOREST_G  CLITERAL(Color){ 28,  72,  28, 255}
#define C_FOREST_L  CLITERAL(Color){ 45,  95,  40, 255}
#define C_FOREST_D  CLITERAL(Color){ 18,  50,  18, 255}
#define C_GOLD_TILE CLITERAL(Color){ 90,  80,  40, 255}
#define C_GOLD_ORE  CLITERAL(Color){220, 185,  30, 255}
#define C_GOLD_HI   CLITERAL(Color){255, 220,  80, 255}
#define C_STONE_T   CLITERAL(Color){ 90,  85,  80, 255}
#define C_STONE_O   CLITERAL(Color){155, 148, 138, 255}
#define C_BERRY_T   CLITERAL(Color){ 70, 110,  45, 255}
#define C_BERRY_F   CLITERAL(Color){190,  40,  40, 255}
#define C_FARM_T    CLITERAL(Color){155, 125,  55, 255}
#define C_FARM_S    CLITERAL(Color){135, 105,  45, 255}
#define C_WATER     CLITERAL(Color){ 38, 100, 185, 255}
#define C_WATER2    CLITERAL(Color){ 28,  80, 155, 255}
#define C_FOG_EXP   CLITERAL(Color){  0,   0,   0, 120}
#define C_FOG_HID   CLITERAL(Color){  0,   0,   0, 235}
#define C_HOVER     CLITERAL(Color){255, 255, 255, 120}

/* ── Grass base tile (textured or flat fallback) ───────────── */

static void draw_grass_base(UIState *ui, uint8_t variant, float px, float py, float size)
{
    Texture2D tex = ui ? ui->tex_land_grass[variant % 4] : (Texture2D){0};
    if (tex.id == 0) {
        draw_iso_quad(px, py, size, size, GRASS_COLS[variant % 4]);
        return;
    }

    /* The land art includes the tile's visual thickness, so it is
       drawn slightly larger than the logical diamond and anchored
       near the bottom edge for correct depth appearance. */
    float target_width  = size * 2.1f;
    float target_height = ((float)tex.height / (float)tex.width) * target_width;
    Vector2 bottom_center = to_rvec2(world_to_iso(px + size * 0.5f, py + size * 0.5f));

    Rectangle src = {0.0f, 0.0f, (float)tex.width, (float)tex.height};
    Rectangle dst = {
        bottom_center.x - target_width * 0.5f,
        bottom_center.y - target_height + size * 0.92f,
        target_width,
        target_height
    };
    DrawTexturePro(tex, src, dst, (Vector2){0}, 0.0f, WHITE);
}

/* ── Forest canopy (sprite or fallback box) ────────────────── */

static void draw_forest_canopy(UIState *ui, Tile *tile,
                               int tile_x, int tile_y,
                               float px, float py, float size)
{
    /* Deterministic seed per tile for consistent tree appearance. */
    unsigned seed = (unsigned)(tile_x * 92821u + tile_y * 68917u + tile->variant * 131u);
    int tree_index = (int)(seed % TREE_VARIANT_COUNT);
    Texture2D tex  = ui->tex_env_trees[tree_index];

    if (tex.id == 0) {
        /* Fallback: simple isometric box. */
        draw_iso_box(px + 6, py + 6, size - 12, size - 12, 22,
                     C_FOREST_L, C_FOREST_D, C_FOREST_D);
        return;
    }

    float tex_w = (float)tex.width;
    float tex_h = (float)tex.height;

    /* Slight size variation per tree so forests look organic. */
    float size_jitter = 4.56f + (float)((seed >> 3) % 48) / 100.0f;
    float scale       = (size * size_jitter) / tex_w;
    float draw_width  = tex_w * scale;
    float draw_height = tex_h * scale;

    /* Small positional jitter to break grid alignment. */
    float offset_x = (float)((int)((seed >>  8) % 7) - 3) * 0.6f;
    float offset_y = (float)((int)((seed >> 12) % 5) - 2) * 0.8f;

    /* Random horizontal flip for variety. */
    bool flip = ((seed >> 16) & 1u) != 0;
    float src_w = flip ? -tex_w : tex_w;
    float src_x = flip ?  tex_w : 0.0f;

    /* Subtle colour tint variation. */
    unsigned tint_delta = (seed >> 20) % 28u;
    Color tint = {255,
                  (unsigned char)(232 - tint_delta),
                  (unsigned char)(232 - tint_delta / 2u),
                  255};

    Vector2 anchor = to_rvec2(world_to_iso(px + size * 0.5f, py + size * 0.5f));
    Rectangle src = {src_x, 0.0f, src_w, tex_h};
    Rectangle dst = {
        anchor.x - draw_width * 0.5f + offset_x,
        anchor.y - draw_height + size * 0.42f + offset_y,
        draw_width,
        draw_height
    };
    DrawTexturePro(tex, src, dst, (Vector2){0, 0}, 0.0f, tint);
}

/* ── Main tile drawing ─────────────────────────────────────── */

/* Tile-interior inset for resource boxes. */
static const float RESOURCE_INSET = 5.0f;
static const float RESOURCE_HEIGHT = 14.0f;

static const float BERRY_INSET  = 7.0f;
static const float BERRY_HEIGHT = 10.0f;

static const int FARM_ROW_COUNT = 3;

void draw_tile(GameState *gs, UIState *ui, int tile_x, int tile_y)
{
    Tile *tile = &gs->map[tile_y][tile_x];
    float px   = (float)(tile_x * TILE_SIZE);
    float py   = (float)(tile_y * TILE_SIZE);
    float size = (float)TILE_SIZE;

    switch (tile->type) {
        case TILE_GRASS:
            draw_grass_base(ui, tile->variant, px, py, size);
            break;

        case TILE_WATER: {
            /* Multi-frequency wave animation for water shimmer. */
            float t = gs->game_time;
            float wave1 = sinf(t * 1.2f + (tile_x + tile_y) * 0.4f) * 0.5f + 0.5f;
            float wave2 = sinf(t * 0.8f - (tile_x - tile_y) * 0.3f) * 0.5f + 0.5f;
            float wave3 = cosf(t * 1.5f + (float)tile_x * 0.5f)     * 0.5f + 0.5f;
            float combined = wave1 * 0.5f + wave2 * 0.3f + wave3 * 0.2f;

            Color water_color = ColorAlphaBlend(
                C_WATER, C_WATER2,
                (Color){255, 255, 255, (unsigned char)(combined * 80)});
            draw_iso_quad(px, py, size, size, water_color);

            /* Occasional specular highlight. */
            if (combined > 0.85f) {
                draw_iso_quad(px + size * 0.2f, py + size * 0.2f,
                              size * 0.6f, size * 0.6f,
                              (Color){255, 255, 255, 40});
            }
            break;
        }

        case TILE_FOREST:
            draw_grass_base(ui, tile->variant, px, py, size);
            draw_forest_canopy(ui, tile, tile_x, tile_y, px, py, size);
            break;

        case TILE_GOLD:
            draw_grass_base(ui, tile->variant, px, py, size);
            draw_iso_box(px + RESOURCE_INSET, py + RESOURCE_INSET,
                         size - RESOURCE_INSET * 2, size - RESOURCE_INSET * 2,
                         RESOURCE_HEIGHT, C_GOLD_HI, C_GOLD_ORE, C_GOLD_TILE);
            break;

        case TILE_STONE:
            draw_grass_base(ui, tile->variant, px, py, size);
            draw_iso_box(px + RESOURCE_INSET, py + RESOURCE_INSET,
                         size - RESOURCE_INSET * 2, size - RESOURCE_INSET * 2,
                         RESOURCE_HEIGHT,
                         CLITERAL(Color){190, 186, 176, 255}, C_STONE_O, C_STONE_T);
            break;

        case TILE_BERRIES:
            draw_grass_base(ui, tile->variant, px, py, size);
            draw_iso_box(px + BERRY_INSET, py + BERRY_INSET,
                         size - BERRY_INSET * 2, size - BERRY_INSET * 2,
                         BERRY_HEIGHT,
                         CLITERAL(Color){120, 160, 75, 255}, C_BERRY_T, C_BERRY_F);
            break;

        case TILE_FARM:
            draw_iso_quad(px, py, size, size, C_FARM_T);
            for (int row = 0; row < FARM_ROW_COUNT; row++) {
                draw_iso_quad(px + 2, py + 4 + row * 9, size - 4, 2, C_FARM_S);
            }
            break;

        case TILE_DESERT: {
            static const Color SAND_COLORS[4] = {
                {194, 168, 118, 255}, {188, 160, 112, 255},
                {200, 174, 124, 255}, {182, 155, 108, 255}
            };
            draw_iso_quad(px, py, size, size, SAND_COLORS[tile->variant % 4]);

            /* Sparse wind-ripple lines for visual texture. */
            if ((tile_x + tile_y) % 5 == 0) {
                draw_iso_quad(px + 4, py + size * 0.3f, size - 8, 1.5f,
                              CLITERAL(Color){210, 188, 140, 120});
                draw_iso_quad(px + 6, py + size * 0.6f, size - 12, 1.5f,
                              CLITERAL(Color){175, 150, 100, 100});
            }
            break;
        }

        case TILE_ROAD: {
            draw_grass_base(ui, tile->variant, px, py, size);
            Color road_main = CLITERAL(Color){125, 105, 75, 255};
            Color road_edge = CLITERAL(Color){105,  88, 62, 255};
            draw_iso_quad(px + 4, py + 4, size - 8, size - 8, road_main);
            draw_iso_quad(px + 3, py + 3, size - 6, 2, road_edge);
            draw_iso_quad(px + 3, py + size - 5, size - 6, 2, road_edge);
            break;
        }

        default:
            draw_grass_base(ui, 0, px, py, size);
            break;
    }

    /* Hover highlight — diamond outline on the hovered tile. */
    if (ui->hover_tile_x == tile_x && ui->hover_tile_y == tile_y) {
        Vector2 p1 = to_rvec2(world_to_iso(px,        py));
        Vector2 p2 = to_rvec2(world_to_iso(px + size, py));
        Vector2 p3 = to_rvec2(world_to_iso(px + size, py + size));
        Vector2 p4 = to_rvec2(world_to_iso(px,        py + size));
        DrawLineEx(p1, p2, 2, C_HOVER);
        DrawLineEx(p2, p3, 2, C_HOVER);
        DrawLineEx(p3, p4, 2, C_HOVER);
        DrawLineEx(p4, p1, 2, C_HOVER);
    }
}

/* ── Forest overlay (redrawn on top of entities for depth) ── */

void draw_forest_overlay(GameState *gs, UIState *ui, int tile_x, int tile_y)
{
    Tile *tile = &gs->map[tile_y][tile_x];
    if (tile->type != TILE_FOREST) return;

    float px = (float)(tile_x * TILE_SIZE);
    float py = (float)(tile_y * TILE_SIZE);
    draw_forest_canopy(ui, tile, tile_x, tile_y, px, py, (float)TILE_SIZE);
}

/* ── Fog of war overlay ────────────────────────────────────── */

void draw_fog(GameState *gs, int tile_x, int tile_y)
{
    int local_player = net_get_local_player();
    FogState fog = gs->map[tile_y][tile_x].fog[local_player];

    if (fog == FOG_VISIBLE) return;

    float px   = (float)(tile_x * TILE_SIZE);
    float py   = (float)(tile_y * TILE_SIZE);
    float size = (float)TILE_SIZE;
    Color fog_color = (fog == FOG_HIDDEN) ? C_FOG_HID : C_FOG_EXP;
    draw_iso_quad(px, py, size, size, fog_color);
}
