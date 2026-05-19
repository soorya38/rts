/*=============================================================
 * renderer_fx.c  –  Isometric helpers, colour palette, draw
 *                    primitives used across the rendering layer.
 *
 * This file is the foundation of all visual output: coordinate
 * transforms, player colours, HP bars, shadows, smoke, etc.
 *=============================================================*/
#include "game.h"
#include "ui_state.h"
#include <stdio.h>

/* ── Terrain colour palette ────────────────────────────────── */
#define C_GRASS1    CLITERAL(Color){ 72, 128,  50, 255}
#define C_GRASS2    CLITERAL(Color){ 65, 118,  45, 255}
#define C_GRASS3    CLITERAL(Color){ 80, 138,  55, 255}
#define C_GRASS4    CLITERAL(Color){ 60, 108,  40, 255}
#define C_WATER     CLITERAL(Color){ 38, 100, 185, 255}
#define C_WATER2    CLITERAL(Color){ 28,  80, 155, 255}
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

/* ── Player colour palette ─────────────────────────────────── */
static const Color PLAYER_COLORS_MAIN[NUM_PLAYERS] = {
    { 30, 110, 220, 255},   /* P0: Blue   */
    {210,  50,  40, 255},   /* P1: Red    */
    { 40, 180,  60, 255},   /* P2: Green  */
    {230, 210,  30, 255},   /* P3: Yellow */
    {235, 120,  30, 255},   /* P4: Orange */
    {160,  40, 210, 255},   /* P5: Purple */
    { 30, 200, 200, 255},   /* P6: Cyan   */
    {140, 140, 140, 255},   /* P7: Gray   */
};

static const Color PLAYER_COLORS_DARK[NUM_PLAYERS] = {
    { 15,  70, 160, 255},
    {150,  25,  20, 255},
    { 20, 120,  30, 255},
    {170, 150,  15, 255},
    {170,  80,  15, 255},
    {110,  20, 150, 255},
    { 15, 140, 140, 255},
    { 90,  90,  90, 255},
};

/* ── UI colour constants ───────────────────────────────────── */
#define C_HP_GREEN  CLITERAL(Color){ 50, 200,  60, 255}
#define C_HP_YELLOW CLITERAL(Color){220, 200,  20, 255}
#define C_HP_RED    CLITERAL(Color){210,  40,  40, 255}
#define C_HP_BG     CLITERAL(Color){ 30,  30,  30, 200}
#define C_SEL       CLITERAL(Color){ 80, 220, 100, 180}
#define C_HOVER     CLITERAL(Color){255, 255, 255, 120}
#define C_FOG_EXP   CLITERAL(Color){  0,   0,   0, 120}
#define C_FOG_HID   CLITERAL(Color){  0,   0,   0, 235}

static const float HP_BAR_HEIGHT = 4.0f;
static const float HP_THRESHOLD_HIGH = 0.6f;
static const float HP_THRESHOLD_LOW  = 0.3f;

static const float OUTLINE_THICKNESS = 1.5f;
static const Color FLAG_POLE_COLOR = {80, 60, 40, 255};

/* ── Player colour accessors ───────────────────────────────── */

Color player_color(int player_id)
{
    if (player_id >= 0 && player_id < NUM_PLAYERS)
        return PLAYER_COLORS_MAIN[player_id];
    return PLAYER_COLORS_MAIN[1];
}

Color player_color_dark(int player_id)
{
    if (player_id >= 0 && player_id < NUM_PLAYERS)
        return PLAYER_COLORS_DARK[player_id];
    return PLAYER_COLORS_DARK[1];
}

Color player_color_alpha(int player_id, unsigned char alpha)
{
    Color color = player_color(player_id);
    color.a = alpha;
    return color;
}

/* ── Isometric drawing primitives ──────────────────────────── */

/* Draw a filled isometric diamond (quad) on the ground plane. */
void draw_iso_quad(float world_x, float world_y,
                   float width, float height, Color color)
{
    Vector2 top   = to_rvec2(world_to_iso(world_x,         world_y));
    Vector2 right = to_rvec2(world_to_iso(world_x + width, world_y));
    Vector2 bot   = to_rvec2(world_to_iso(world_x + width, world_y + height));
    Vector2 left  = to_rvec2(world_to_iso(world_x,         world_y + height));

    DrawTriangle(top, left, right, color);
    DrawTriangle(left, bot, right, color);
}

/* Draw a filled isometric box (cuboid) with distinct face colours. */
void draw_iso_box(float world_x, float world_y,
                  float width, float height, float elevation,
                  Color top_color, Color left_color, Color right_color)
{
    /* Bottom-face corners */
    Vector2 b1 = to_rvec2(world_to_iso(world_x,         world_y));
    Vector2 b2 = to_rvec2(world_to_iso(world_x + width, world_y));
    Vector2 b3 = to_rvec2(world_to_iso(world_x + width, world_y + height));
    Vector2 b4 = to_rvec2(world_to_iso(world_x,         world_y + height));

    /* Top-face corners (shifted upward by elevation). */
    Vector2 t1 = {b1.x, b1.y - elevation};
    Vector2 t2 = {b2.x, b2.y - elevation};
    Vector2 t3 = {b3.x, b3.y - elevation};
    Vector2 t4 = {b4.x, b4.y - elevation};

    /* Right face */
    DrawTriangle(b3, b2, t2, right_color);
    DrawTriangle(b3, t2, t3, right_color);

    /* Left face */
    DrawTriangle(b4, b3, t4, left_color);
    DrawTriangle(t4, b3, t3, left_color);
    DrawTriangle(t4, b3, t3, left_color);

    /* Top face */
    DrawTriangle(t1, t4, t2, top_color);
    DrawTriangle(t4, t3, t2, top_color);
}

/* Draw a wireframe isometric box. */
void draw_iso_box_outline(float world_x, float world_y,
                          float width, float height, float elevation,
                          Color color)
{
    Vector2 b1 = to_rvec2(world_to_iso(world_x,         world_y));
    Vector2 b2 = to_rvec2(world_to_iso(world_x + width, world_y));
    Vector2 b3 = to_rvec2(world_to_iso(world_x + width, world_y + height));
    Vector2 b4 = to_rvec2(world_to_iso(world_x,         world_y + height));

    Vector2 t1 = {b1.x, b1.y - elevation};
    Vector2 t2 = {b2.x, b2.y - elevation};
    Vector2 t3 = {b3.x, b3.y - elevation};
    Vector2 t4 = {b4.x, b4.y - elevation};

    /* Top edges */
    DrawLineEx(t1, t2, OUTLINE_THICKNESS, color);
    DrawLineEx(t2, t3, OUTLINE_THICKNESS, color);
    DrawLineEx(t3, t4, OUTLINE_THICKNESS, color);
    DrawLineEx(t4, t1, OUTLINE_THICKNESS, color);

    /* Vertical pillars */
    DrawLineEx(b1, t1, OUTLINE_THICKNESS, color);
    DrawLineEx(b2, t2, OUTLINE_THICKNESS, color);
    DrawLineEx(b3, t3, OUTLINE_THICKNESS, color);
    DrawLineEx(b4, t4, OUTLINE_THICKNESS, color);

    /* Bottom edges */
    DrawLineEx(b1, b2, OUTLINE_THICKNESS, color);
    DrawLineEx(b2, b3, OUTLINE_THICKNESS, color);
    DrawLineEx(b3, b4, OUTLINE_THICKNESS, color);
    DrawLineEx(b4, b1, OUTLINE_THICKNESS, color);
}

/* ── HUD drawing primitives ────────────────────────────────── */

void draw_hp_bar(float world_x, float world_y, float bar_width,
                 int hp, int max_hp, float vertical_offset)
{
    if (hp == max_hp) return;

    Vector2 screen_pos = to_rvec2(world_to_iso(world_x, world_y));
    float bx = screen_pos.x - bar_width * 0.5f;
    float by = screen_pos.y - vertical_offset;

    DrawRectangleRec((Rectangle){bx, by, bar_width, HP_BAR_HEIGHT}, C_HP_BG);

    float hp_fraction = (float)hp / max_hp;
    Color bar_color = (hp_fraction > HP_THRESHOLD_HIGH) ? C_HP_GREEN
                    : (hp_fraction > HP_THRESHOLD_LOW)  ? C_HP_YELLOW
                    :                                     C_HP_RED;

    DrawRectangleRec((Rectangle){bx, by, bar_width * hp_fraction, HP_BAR_HEIGHT}, bar_color);
}

void draw_construction(float world_x, float world_y,
                       float width, float height,
                       float progress, Color player_col)
{
    Color translucent = {player_col.r, player_col.g, player_col.b, 200};
    draw_iso_box(world_x, world_y, width, height,
                 progress * 15.0f, translucent, player_col, player_col);
}

void draw_shadow(float world_x, float world_y, float radius_w, float radius_h)
{
    Vector2 screen_pos = to_rvec2(world_to_iso(world_x, world_y));
    DrawEllipse((int)screen_pos.x, (int)screen_pos.y,
                (int)radius_w, (int)(radius_h * 0.5f),
                (Color){0, 0, 0, 60});
}

void draw_flag(float world_x, float world_y, int player_id)
{
    Vector2 base = to_rvec2(world_to_iso(world_x, world_y));
    Vector2 pole_top = {base.x, base.y - 15};
    DrawLineEx(base, pole_top, 2, FLAG_POLE_COLOR);

    Color flag_color = player_color(player_id);
    Vector2 tip   = {base.x + 10, base.y - 10};
    Vector2 lower = {base.x, base.y - 5};
    DrawTriangle(pole_top, tip, lower, flag_color);
}

/* Animated smoke puffs rising from a world position. */
void draw_smoke(float world_x, float world_y, float time, int seed)
{
    static const int PUFF_COUNT = 3;
    Vector2 origin = to_rvec2(world_to_iso(world_x, world_y));

    for (int i = 0; i < PUFF_COUNT; i++) {
        float phase = fmodf(time * 0.7f + (float)i * 0.33f + (float)seed * 0.1f, 1.0f);
        float drift_x = sinf(time * 2.0f + (float)i + (float)seed) * 5.0f;
        float rise_y  = -phase * 30.0f;
        float radius  = 4.0f + phase * 8.0f;
        unsigned char alpha = (unsigned char)((1.0f - phase) * 160);

        DrawCircle((int)(origin.x + drift_x),
                   (int)(origin.y - 10 + rise_y),
                   radius,
                   (Color){60, 60, 60, alpha});
    }
}

/* Grass colour table used by the tile renderer. */
const Color GRASS_COLS[4] = {C_GRASS1, C_GRASS2, C_GRASS3, C_GRASS4};
