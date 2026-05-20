/*=============================================================
 * renderer.h  –  Internal renderer declarations
 *
 * Shared by all renderer_*.c files.  Not part of the public API.
 *=============================================================*/
#pragma once
#include "game.h"
#include "ui_state.h"

/* ── Isometric primitives (renderer_fx.c) ──────────────────── */

void  draw_iso_quad(float world_x, float world_y,
                    float width, float height, Color color);
void  draw_iso_box(float world_x, float world_y,
                   float width, float height, float elevation,
                   Color top, Color left, Color right);
void  draw_iso_box_outline(float world_x, float world_y,
                           float width, float height, float elevation,
                           Color color);
void  draw_hp_bar(float world_x, float world_y, float bar_width,
                  int hp, int max_hp, float vertical_offset);
void  draw_construction(float world_x, float world_y,
                        float width, float height,
                        float progress, Color player_col);
void  draw_shadow(float world_x, float world_y, float radius_w, float radius_h);
void  draw_flag(float world_x, float world_y, int player_id);
void  draw_smoke(float world_x, float world_y, float time, int seed);

Color player_color(int player_id);
Color player_color_dark(int player_id);
Color player_color_alpha(int player_id, unsigned char alpha);

extern const Color GRASS_COLS[4];

/* ── Tile overlays (renderer_tiles.c) ──────────────────────── */

void draw_forest_overlay(GameState *gs, UIState *ui, int tile_x, int tile_y);

/* ── Shared colour constants ───────────────────────────────── */

#define C_SEL       CLITERAL(Color){ 80, 220, 100, 180}
#define C_HOVER     CLITERAL(Color){255, 255, 255, 120}
#define C_FOG_EXP   CLITERAL(Color){  0,   0,   0, 120}
#define C_FOG_HID   CLITERAL(Color){  0,   0,   0, 235}
#define C_HP_GREEN  CLITERAL(Color){ 50, 200,  60, 255}
#define C_HP_BG     CLITERAL(Color){ 30,  30,  30, 200}
