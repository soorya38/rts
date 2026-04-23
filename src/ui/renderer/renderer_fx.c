/*=============================================================
 * renderer_fx.c  –  Isometric helpers, color palette, draw primitives
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
#define C_P2_MAIN   CLITERAL(Color){ 40, 180,  60, 255}
#define C_P2_DARK   CLITERAL(Color){ 20, 120,  30, 255}
#define C_P3_MAIN   CLITERAL(Color){230, 210,  30, 255}
#define C_P3_DARK   CLITERAL(Color){170, 150,  15, 255}

#define C_HP_GREEN  CLITERAL(Color){ 50, 200,  60, 255}
#define C_HP_YELLOW CLITERAL(Color){220, 200,  20, 255}
#define C_HP_RED    CLITERAL(Color){210,  40,  40, 255}
#define C_HP_BG     CLITERAL(Color){ 30,  30,  30, 200}
#define C_SEL       CLITERAL(Color){ 80, 220, 100, 180}
#define C_HOVER     CLITERAL(Color){255, 255, 255, 120}
#define C_FOG_EXP   CLITERAL(Color){  0,   0,   0, 120}
#define C_FOG_HID   CLITERAL(Color){  0,   0,   0, 235}

Color player_color(int p) {
    if (p == 0) return C_P1_MAIN;
    if (p == 1) return C_AI_MAIN;
    if (p == 2) return C_P2_MAIN;
    if (p == 3) return C_P3_MAIN;
    return C_AI_MAIN;
}
Color player_color_dark(int p) {
    if (p == 0) return C_P1_DARK;
    if (p == 1) return C_AI_DARK;
    if (p == 2) return C_P2_DARK;
    if (p == 3) return C_P3_DARK;
    return C_AI_DARK;
}
Color player_color_alpha(int p, unsigned char a){
    Color c=player_color(p); c.a=a; return c;
}

/* ─── Isometric Helpers ──────────────────────────────────── */

void draw_iso_quad(float bx, float by, float bw, float bh, Color c) {
    Vector2 p1 = to_rvec2(world_to_iso(bx, by));
    Vector2 p2 = to_rvec2(world_to_iso(bx + bw, by));
    Vector2 p3 = to_rvec2(world_to_iso(bx + bw, by + bh));
    Vector2 p4 = to_rvec2(world_to_iso(bx, by + bh));
    DrawTriangle(p1, p4, p2, c);
    DrawTriangle(p4, p3, p2, c);
}

void draw_iso_box(float bx, float by, float bw, float bh, float h, Color top, Color left, Color right) {
    Vector2 p1 = to_rvec2(world_to_iso(bx, by));
    Vector2 p2 = to_rvec2(world_to_iso(bx + bw, by));
    Vector2 p3 = to_rvec2(world_to_iso(bx + bw, by + bh));
    Vector2 p4 = to_rvec2(world_to_iso(bx, by + bh));
    Vector2 t1 = {p1.x, p1.y - h};
    Vector2 t2 = {p2.x, p2.y - h};
    Vector2 t3 = {p3.x, p3.y - h};
    Vector2 t4 = {p4.x, p4.y - h};
    DrawTriangle(p3, p2, t2, right);
    DrawTriangle(p3, t2, t3, right);
    DrawTriangle(p4, p3, t4, left);
    DrawTriangle(t4, p3, t3, left);
    DrawTriangle(t4, p3, t3, left);
    DrawTriangle(t1, t4, t2, top);
    DrawTriangle(t4, t3, t2, top);
}

void draw_iso_box_outline(float bx, float by, float bw, float bh, float h, Color c) {
    Vector2 p1 = to_rvec2(world_to_iso(bx, by));
    Vector2 p2 = to_rvec2(world_to_iso(bx + bw, by));
    Vector2 p3 = to_rvec2(world_to_iso(bx + bw, by + bh));
    Vector2 p4 = to_rvec2(world_to_iso(bx, by + bh));
    Vector2 t1 = {p1.x, p1.y - h};
    Vector2 t2 = {p2.x, p2.y - h};
    Vector2 t3 = {p3.x, p3.y - h};
    Vector2 t4 = {p4.x, p4.y - h};
    
    DrawLineEx(t1, t2, 1.5f, c);
    DrawLineEx(t2, t3, 1.5f, c);
    DrawLineEx(t3, t4, 1.5f, c);
    DrawLineEx(t4, t1, 1.5f, c);
    
    DrawLineEx(p1, t1, 1.5f, c);
    DrawLineEx(p2, t2, 1.5f, c);
    DrawLineEx(p3, t3, 1.5f, c);
    DrawLineEx(p4, t4, 1.5f, c);
    
    DrawLineEx(p1, p2, 1.5f, c);
    DrawLineEx(p2, p3, 1.5f, c);
    DrawLineEx(p3, p4, 1.5f, c);
    DrawLineEx(p4, p1, 1.5f, c);
}

void draw_hp_bar(float wx, float wy, float w, int hp, int max_hp, float above){
    if(hp==max_hp) return;
    Vector2 p = to_rvec2(world_to_iso(wx, wy));
    float bw=w, bh=4;
    float bx=p.x-bw*0.5f, by=p.y-above;
    DrawRectangleRec((Rectangle){bx,by,bw,bh},C_HP_BG);
    float frac=(float)hp/max_hp;
    Color hc = frac>0.6f ? C_HP_GREEN : frac>0.3f ? C_HP_YELLOW : C_HP_RED;
    DrawRectangleRec((Rectangle){bx,by,bw*frac,bh},hc);
}

void draw_construction(float px,float py,float w,float h,float prog,Color mc){
    draw_iso_box(px, py, w, h, prog * 15.0f, (Color){mc.r, mc.g, mc.b, 200}, mc, mc);
}

void draw_shadow(float wx, float wy, float rw, float rh) {
    Vector2 p = to_rvec2(world_to_iso(wx, wy));
    DrawEllipse((int)p.x, (int)p.y, (int)rw, (int)(rh * 0.5f), (Color){0, 0, 0, 60});
}

void draw_flag(float fx, float fy, int player){
    Vector2 p = to_rvec2(world_to_iso(fx, fy));
    DrawLineEx((Vector2){p.x,p.y},(Vector2){p.x,p.y-15},2,(Color){80,60,40,255});
    Color fc=player_color(player);
    DrawTriangle((Vector2){p.x,p.y-15},(Vector2){p.x+10,p.y-10},(Vector2){p.x,p.y-5},fc);
}

void draw_smoke(float bx, float by, float time, int seed) {
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

/* Grass color table used by tile renderer */
const Color GRASS_COLS[4]={C_GRASS1,C_GRASS2,C_GRASS3,C_GRASS4};
