/*=============================================================
 * renderer.h  –  Internal renderer declarations (non-public)
 *=============================================================*/
#pragma once
#include "game.h"
#include "ui_state.h"

/* Iso primitives (renderer_fx.c) */
void draw_iso_quad(float bx, float by, float bw, float bh, Color c);
void draw_iso_box(float bx, float by, float bw, float bh, float h, Color top, Color left, Color right);
void draw_hp_bar(float wx, float wy, float w, int hp, int max_hp, float above);
void draw_construction(float px, float py, float w, float h, float prog, Color mc);
void draw_shadow(float wx, float wy, float rw, float rh);
void draw_flag(float fx, float fy, int player);
void draw_smoke(float bx, float by, float time, int seed);
Color player_color(int p);
Color player_color_dark(int p);
Color player_color_alpha(int p, unsigned char a);
extern const Color GRASS_COLS[4];

/* Colors (shared via defines used in each .c) */
#define C_SEL       CLITERAL(Color){ 80, 220, 100, 180}
#define C_HOVER     CLITERAL(Color){255, 255, 255, 120}
#define C_FOG_EXP   CLITERAL(Color){  0,   0,   0, 120}
#define C_FOG_HID   CLITERAL(Color){  0,   0,   0, 235}
#define C_HP_GREEN  CLITERAL(Color){ 50, 200,  60, 255}
#define C_HP_BG     CLITERAL(Color){ 30,  30,  30, 200}
