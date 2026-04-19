/*=============================================================
 * hud_common.h  –  Shared HUD constants and helpers (internal)
 *=============================================================*/
#pragma once
#include "ui_state.h"   /* for hud_scale() */

/* All size macros are scale-aware so the HUD looks right on phones.
   hud_scale() returns GetScreenHeight()/720 clamped to [1.0, 2.5]. */
#define HUD_TOP_H    ((int)(42  * hud_scale()))
#define HUD_BOT_H    ((int)(154 * hud_scale()))
#define HUD_BOT_Y   (GetScreenHeight() - HUD_BOT_H)
#define MINI_SIZE    ((int)(180 * hud_scale()))
#define MINI_X      (GetScreenWidth()  - MINI_SIZE - 8)
#define MINI_Y      (GetScreenHeight() - MINI_SIZE - 8)

#define C_HUD_BG    CLITERAL(Color){ 22, 17, 10, 235}
#define C_HUD_LINE  CLITERAL(Color){ 80, 65, 40, 220}
#define C_HUD_TXT   CLITERAL(Color){235,220,185, 255}
#define C_FOOD      CLITERAL(Color){ 90,200, 60, 255}
#define C_WOOD      CLITERAL(Color){160,110, 40, 255}
#define C_GOLD      CLITERAL(Color){230,190, 30, 255}
#define C_STONE     CLITERAL(Color){175,168,155, 255}
#define C_POP_OK    CLITERAL(Color){200,200,200, 255}
#define C_POP_WARN  CLITERAL(Color){220, 80, 60, 255}
#define C_BTN_NORM  CLITERAL(Color){ 48, 38, 22, 255}
#define C_BTN_HOV   CLITERAL(Color){ 72, 58, 32, 255}
#define C_BTN_BORD  CLITERAL(Color){110, 90, 50, 255}
#define C_AGE       CLITERAL(Color){220,200,130, 255}

/* Shared button/tooltip helpers (defined in hud_overlays.c) */
bool draw_button(const char *label, int x, int y, int w, int h, bool enabled);
void draw_tooltip(const char *text, int x, int y);
void draw_food_icon(UIState *ui, int x, int y);
void draw_wood_icon(UIState *ui, int x, int y);
void draw_gold_icon(UIState *ui, int x, int y);
void draw_stone_icon(UIState *ui, int x, int y);
