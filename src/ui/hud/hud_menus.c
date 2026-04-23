/*=============================================================
 * hud_menus.c  –  Build menu panel, master hud_draw
 *=============================================================*/
#include "game.h"
#include "ui_state.h"
#include "hud_common.h"
#include "renderer.h"
#include "net.h"
#include <stdio.h>
#include <string.h>

/* Forward declarations from other hud files */
extern void draw_top_bar(GameState *gs, UIState *ui);
extern void draw_bottom_panel(GameState *gs, UIState *ui);
extern void draw_minimap(GameState *gs, UIState *ui);
extern void draw_age_bar(GameState *gs);
extern void draw_alert(GameState *gs, UIState *ui);
extern void draw_end_screen(GameState *gs, UIState *ui);
extern void draw_menu(GameState *gs, UIState *ui);
extern void draw_placement_bar(GameState *gs, UIState *ui);
extern void draw_campaign_panel(GameState *gs, UIState *ui);
extern void draw_campaign_briefing(GameState *gs, UIState *ui);

static void draw_build_menu(GameState *gs, UIState *ui){
    if(!ui->build_panel_open) return;
    bool vil=false;
    for(int i=0;i<ui->sel_count;i++)
        if(gs->units[ui->sel_units[i]].type==UNIT_VILLAGER){vil=true;break;}
    if(!vil){ui->build_panel_open=false;return;}

    int mx=100, bw=116, bh=48, gap=6;
    int lp = net_get_local_player();
    int cur_age = gs->res[lp].age;
    bool has_mill=(building_find(gs,lp,BLD_MILL,true)>=0);
    bool is_feudal = (cur_age >= 1);
    bool is_castle = (cur_age >= 2);
    struct { BldType t; const char *n; Cost c; } items[]={
        {BLD_HOUSE,        "House (H)\n25 Wood",         {0,25, 0,0}},
        {BLD_MILL,         "Mill (M)\n100 Wood",         {0,100,0,0}},
        {BLD_LUMBER_CAMP,  "Lumber (L)\n100 Wood",       {0,100,0,0}},
        {BLD_MINING_CAMP,  "Mining (N)\n100 Wood",       {0,100,0,0}},
        {BLD_BARRACKS,     "Barracks (R)\n175 Wood",     {0,175,0,0}},
        {BLD_ARCHERY_RANGE,"Arch.Range (A)\n175 Wood",   {0,175,0,0}},
        {BLD_STABLE,       "Stable (V)\n175 Wood",       {0,175,0,0}},
        {BLD_BLACKSMITH,   "Smith (K)\n150 Wood",        {0,150,0,0}},
        {BLD_MARKET,       "Market (Y)\n175 Wood",       {0,175,0,0}},
        {BLD_FARM,         "Farm (F)\n60 Wood",          {0,60, 0,0}},
        {BLD_WATCH_TOWER,  "Tower (T)\n125W 125S",       {0,125,0,125}},
        {BLD_MONASTERY,    "Monastery (O)\n175W",        {0,175,0,0}},
        {BLD_SIEGE_WORKSHOP,"Siege (I)\n200W",           {0,200,0,0}},
        {BLD_UNIVERSITY,   "University (C)\n200W",       {0,200,0,0}},
        {BLD_WALL,         "Wall (U)\n20 Wood",          {0,20, 0,0}},
        {BLD_GATE,         "Gate (J)\n35W 15S",          {0,35, 0,15}},
    };
    int n=(int)(sizeof(items)/sizeof(items[0]));
    int rows = (n + 2) / 3;
    int my = HUD_BOT_Y - (bh * rows + gap * (rows - 1) + 6);
    float pw=(float)(bw*3+gap*2+16), ph=(float)(bh*rows+gap*(rows-1)+36);

    DrawRectangleRounded((Rectangle){(float)(mx-8),(float)(my-30),pw,ph},0.06f,6,CLITERAL(Color){18,14,8,245});
#if RAYLIB_VERSION_MAJOR >= 5 && RAYLIB_VERSION_MINOR >= 5
    DrawRectangleRoundedLines((Rectangle){(float)(mx-8),(float)(my-30),pw,ph},0.06f,6,C_HUD_LINE);
#elif RAYLIB_VERSION_MAJOR >= 5
    DrawRectangleRoundedLines((Rectangle){(float)(mx-8),(float)(my-30),pw,ph},0.06f,6,1.0f,C_HUD_LINE);
#else
    DrawRectangleRoundedLines((Rectangle){(float)(mx-8),(float)(my-30),pw,ph},0.06f,6,C_HUD_LINE);
#endif
    DrawText("BUILD MENU  — select a structure",mx,my-22,12,CLITERAL(Color){200,180,100,255});

    for(int i=0;i<n;i++){
        int col=i%3, row=i/3;
        int bx=mx+col*(bw+gap), by=my+row*(bh+gap);
        bool can=res_can_afford(&gs->res[lp],items[i].c);
        bool prereq_ok=true;
        if(items[i].t==BLD_FARM && !has_mill) prereq_ok=false;
        /* Archery Range, Stable, Blacksmith, Market require Feudal Age */
        if((items[i].t==BLD_ARCHERY_RANGE || items[i].t==BLD_STABLE ||
            items[i].t==BLD_BLACKSMITH || items[i].t==BLD_MARKET ||
            items[i].t==BLD_WATCH_TOWER) && !is_feudal) prereq_ok=false;
        if((items[i].t==BLD_MONASTERY || items[i].t==BLD_SIEGE_WORKSHOP ||
            items[i].t==BLD_UNIVERSITY) && !is_castle) prereq_ok=false;
        bool clickable = can && prereq_ok;
        bool pressed=draw_button(items[i].n,bx,by,bw,bh,clickable);
        if(items[i].t==BLD_FARM && !has_mill)
            DrawText("Needs Mill",bx+4,by+bh-14,9,CLITERAL(Color){220,140,60,220});
        if((items[i].t==BLD_ARCHERY_RANGE || items[i].t==BLD_STABLE ||
            items[i].t==BLD_BLACKSMITH || items[i].t==BLD_MARKET ||
            items[i].t==BLD_WATCH_TOWER) && !is_feudal)
            DrawText("Feudal Age",bx+4,by+bh-14,9,CLITERAL(Color){220,140,60,220});
        if((items[i].t==BLD_MONASTERY || items[i].t==BLD_SIEGE_WORKSHOP ||
            items[i].t==BLD_UNIVERSITY) && !is_castle)
            DrawText("Castle Age",bx+4,by+bh-14,9,CLITERAL(Color){220,140,60,220});
        if(pressed && clickable){
            gs->build_mode.type=items[i].t;
            gs->build_mode.active=true;
            gs->build_mode.dragging=false;
            ui->build_panel_open=false;
            char msg[48];
            snprintf(msg,sizeof(msg),"Placing: %s",building_name(items[i].t));
            game_set_alert(gs,msg);
        }
    }
    DrawText("[ESC] Cancel  |  H R A V K Y M L N F T O I C U J",
             mx,my+bh*rows+gap*(rows-1)+4,9,CLITERAL(Color){100,90,60,200});
}

/* ─── TAB Map Overview ─────────────────────────────────────── */
static void draw_map_overview(GameState *gs, UIState *ui) {
    (void)ui;
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    /* Dim background */
    DrawRectangle(0, 0, sw, sh, CLITERAL(Color){0, 0, 0, 180});

    /* Map dimensions */
    int pad = 40;
    int map_size = (sw < sh ? sw : sh) - pad * 2;
    if (map_size > 600) map_size = 600;
    int ox = (sw - map_size) / 2;
    int oy = (sh - map_size) / 2;
    float tile_px = (float)map_size / MAP_W;

    /* Background */
    DrawRectangle(ox - 2, oy - 2, map_size + 4, map_size + 4, CLITERAL(Color){30, 24, 14, 255});
    DrawRectangleLinesEx((Rectangle){(float)(ox-2), (float)(oy-2),
                         (float)(map_size+4), (float)(map_size+4)}, 2.0f,
                         CLITERAL(Color){160, 140, 80, 255});

    /* Draw tiles */
    for (int y = 0; y < MAP_H; y++) {
        for (int x = 0; x < MAP_W; x++) {
            Color c;
            switch (gs->map[y][x].type) {
                case TILE_WATER:   c = CLITERAL(Color){38, 100, 185, 255}; break;
                case TILE_FOREST:  c = CLITERAL(Color){28, 72, 28, 255}; break;
                case TILE_GOLD:    c = CLITERAL(Color){220, 185, 30, 255}; break;
                case TILE_STONE:   c = CLITERAL(Color){155, 148, 138, 255}; break;
                case TILE_BERRIES: c = CLITERAL(Color){190, 40, 40, 255}; break;
                case TILE_FARM:    c = CLITERAL(Color){155, 125, 55, 255}; break;
                case TILE_DESERT:  c = CLITERAL(Color){194, 168, 118, 255}; break;
                case TILE_ROAD:    c = CLITERAL(Color){125, 105, 75, 255}; break;
                default:           c = CLITERAL(Color){75, 120, 45, 255}; break;
            }
            DrawRectangle(ox + (int)(x * tile_px), oy + (int)(y * tile_px),
                          (int)(tile_px + 1), (int)(tile_px + 1), c);
        }
    }

    /* Draw buildings */
    for (int i = 0; i < MAX_BUILDINGS; i++) {
        Building *b = &gs->buildings[i];
        if (!b->active) continue;
        Color bc = player_color(b->player);
        bc.a = 220;
        int bx = ox + (int)(b->tx * tile_px);
        int by = oy + (int)(b->ty * tile_px);
        int bw2 = (int)(b->tw * tile_px + 1);
        int bh2 = (int)(b->th * tile_px + 1);
        DrawRectangle(bx, by, bw2, bh2, bc);
        DrawRectangleLinesEx((Rectangle){(float)bx, (float)by, (float)bw2, (float)bh2},
                             1.0f, CLITERAL(Color){255, 255, 255, 100});
    }

    /* Draw units as small dots */
    for (int i = 0; i < MAX_UNITS; i++) {
        Unit *u = &gs->units[i];
        if (!u->active || u->state == US_DEAD) continue;
        Color uc = player_color(u->player);
        int ux2 = ox + (int)((u->wx / TILE_SIZE) * tile_px);
        int uy2 = oy + (int)((u->wy / TILE_SIZE) * tile_px);
        int dot = (int)(tile_px * 0.6f);
        if (dot < 2) dot = 2;
        DrawRectangle(ux2 - dot/2, uy2 - dot/2, dot, dot, uc);
    }

    /* Title */
    float sc = hud_scale();
    int fs16 = (int)(16 * sc);
    int fs11 = (int)(11 * sc);
    const char *title = "MAP OVERVIEW";
    int tw2 = MeasureText(title, fs16);
    DrawText(title, (sw - tw2) / 2, oy - (int)(30 * sc), fs16,
             CLITERAL(Color){220, 200, 140, 255});

    /* Legend */
    int lx = ox + map_size + (int)(16 * sc);
    int ly = oy;
    int lh = (int)(16 * sc);
    struct { Color c; const char *name; } legend[] = {
        {CLITERAL(Color){75, 120, 45, 255}, "Grass"},
        {CLITERAL(Color){38, 100, 185, 255}, "Water"},
        {CLITERAL(Color){28, 72, 28, 255}, "Forest"},
        {CLITERAL(Color){194, 168, 118, 255}, "Desert"},
        {CLITERAL(Color){125, 105, 75, 255}, "Road"},
        {CLITERAL(Color){220, 185, 30, 255}, "Gold"},
        {CLITERAL(Color){155, 148, 138, 255}, "Stone"},
        {CLITERAL(Color){190, 40, 40, 255}, "Berries"},
    };
    int lcount = (int)(sizeof(legend) / sizeof(legend[0]));
    if (lx + 100 < sw) { /* Only draw if room */
        for (int i = 0; i < lcount; i++) {
            DrawRectangle(lx, ly + i * lh, (int)(10 * sc), (int)(10 * sc), legend[i].c);
            DrawText(legend[i].name, lx + (int)(14 * sc), ly + i * lh, fs11,
                     CLITERAL(Color){200, 185, 140, 230});
        }
    }

    /* Hint */
    const char *hint = "Hold [TAB] to view  |  Release to return";
    int hw = MeasureText(hint, fs11);
    DrawText(hint, (sw - hw) / 2, oy + map_size + (int)(10 * sc), fs11,
             CLITERAL(Color){130, 120, 90, 220});
}

/* ─── Master HUD draw ─────────────────────────────────────── */
void hud_draw(GameState *gs, UIState *ui){
    if(gs->phase==PHASE_MENU){ draw_menu(gs, ui); return; }
    if(gs->phase==PHASE_VICTORY||gs->phase==PHASE_DEFEAT){ draw_end_screen(gs, ui); return; }

    draw_top_bar(gs, ui);
    draw_bottom_panel(gs, ui);
    draw_minimap(gs, ui);
    draw_age_bar(gs);
    if(ui->build_panel_open) draw_build_menu(gs, ui);
    if(gs->build_mode.active) draw_placement_bar(gs, ui);
    draw_campaign_panel(gs, ui);
    draw_alert(gs, ui);
    draw_campaign_briefing(gs, ui);

    if(gs->phase==PHASE_PAUSED){
        DrawRectangle(0,0,GetScreenWidth(),GetScreenHeight(),CLITERAL(Color){0,0,0,100});
        const char *pm="PAUSED  –  Press [P] to resume";
        DrawText(pm,GetScreenWidth()/2-MeasureText(pm,22)/2,GetScreenHeight()/2-11,22,CLITERAL(Color){220,200,140,255});
    }

    /* TAB map overview */
    if (IsKeyDown(KEY_TAB)) draw_map_overview(gs, ui);

    if(gs->mode == GAME_MODE_STANDARD ||
       (gs->mode == GAME_MODE_CAMPAIGN && gs->campaign_mission >= 5)){
        char dbuf[64];
        const char *ap[]={"Gather","Build","Military","Attack"};
        snprintf(dbuf,sizeof(dbuf),"AI: %s | mil: %d",ap[gs->ai_phase],unit_count_military(gs,1));
        DrawText(dbuf,8,HUD_TOP_H+4,10,CLITERAL(Color){100,90,65,180});
    }
}

