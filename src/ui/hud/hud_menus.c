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
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    float sc = hud_scale();
    int fs16 = (int)(16 * sc);
    int fs12 = (int)(12 * sc);
    int fs11 = (int)(11 * sc);
    int fs10 = (int)(10 * sc);

    /* Dim background */
    DrawRectangle(0, 0, sw, sh, CLITERAL(Color){0, 0, 0, 200});

    /* Lazy-load the OSM tile grid */
    bool have_osm = gs->osm_map_available;
    if (have_osm && ui->osm_tiles_loaded == 0) {
        int loaded = 0;
        for (int r = 0; r < gs->osm_tile_rows && r < 4; r++) {
            for (int c = 0; c < gs->osm_tile_cols && c < 4; c++) {
                char path[64];
                snprintf(path, sizeof(path), "osm_tile_%d_%d.png", c, r);
                if (FileExists(path)) {
                    ui->tex_osm_tiles[r][c] = LoadTexture(path);
                    if (ui->tex_osm_tiles[r][c].id != 0) loaded++;
                }
            }
        }
        ui->osm_tiles_loaded = loaded;
        if (loaded == 0) have_osm = false;
        printf("DEBUG: lazy load complete. loaded=%d, have_osm=%d\n", loaded, have_osm);
    }

    /* Layout: two panels side-by-side if OSM available, else single centered */
    int pad = (int)(30 * sc);
    int gap = (int)(20 * sc);
    int panel_size;
    int left_x, right_x, panels_y;

    if (have_osm && ui->osm_tiles_loaded > 0) {
        /* Two panels side-by-side */
        int avail_w = sw - pad * 2 - gap;
        int avail_h = sh - pad * 2 - (int)(60 * sc);
        panel_size = (avail_w / 2 < avail_h) ? avail_w / 2 : avail_h;
        if (panel_size > 500) panel_size = 500;
        int total_w = panel_size * 2 + gap;
        left_x = (sw - total_w) / 2;
        right_x = left_x + panel_size + gap;
        panels_y = (sh - panel_size) / 2 + (int)(10 * sc);
    } else {
        /* Single panel centered */
        int avail = (sw < sh ? sw : sh) - pad * 2;
        panel_size = avail < 550 ? avail : 550;
        left_x = -1;
        right_x = (sw - panel_size) / 2;
        panels_y = (sh - panel_size) / 2 + (int)(10 * sc);
    }

    /* ── Left panel: OSM real-world map (tile grid) ── */
    if (have_osm && ui->osm_tiles_loaded > 0 && left_x >= 0) {
        int cols = gs->osm_tile_cols;
        int rows = gs->osm_tile_rows;

        /* Border */
        DrawRectangle(left_x - 3, panels_y - 3, panel_size + 6, panel_size + 6,
                      CLITERAL(Color){30, 50, 80, 255});
        DrawRectangleLinesEx((Rectangle){(float)(left_x-3), (float)(panels_y-3),
                             (float)(panel_size+6), (float)(panel_size+6)}, 2.0f,
                             CLITERAL(Color){80, 140, 220, 255});

        /* Calculate exact fractional tile coordinates for the bbox */
        double z = gs->osm_tile_z;
        double tx0_exact = (gs->osm_bbox_west + 180.0) / 360.0 * (1 << (int)z);
        double tx1_exact = (gs->osm_bbox_east + 180.0) / 360.0 * (1 << (int)z);
        double r_north = gs->osm_bbox_north * 3.14159265358979323846 / 180.0;
        double ty0_exact = (1.0 - log(tan(r_north) + 1.0/cos(r_north)) / 3.14159265358979323846) / 2.0 * (1 << (int)z);
        double r_south = gs->osm_bbox_south * 3.14159265358979323846 / 180.0;
        double ty1_exact = (1.0 - log(tan(r_south) + 1.0/cos(r_south)) / 3.14159265358979323846) / 2.0 * (1 << (int)z);

        double exact_cols = tx1_exact - tx0_exact;
        double exact_rows = ty1_exact - ty0_exact;

        /* Prevent division by zero */
        if (exact_cols > 0.0001 && exact_rows > 0.0001) {
            /* We want the bbox (tx0_exact to tx1_exact, ty0_exact to ty1_exact) to perfectly fill panel_size */
            double scale_x = panel_size / exact_cols;
            double scale_y = panel_size / exact_rows;

            Rectangle panel_rect = {(float)left_x, (float)panels_y, (float)panel_size, (float)panel_size};

            for (int r = 0; r < rows && r < 4; r++) {
                for (int c = 0; c < cols && c < 4; c++) {
                    if (ui->tex_osm_tiles[r][c].id == 0) continue;
                    
                    /* The tile's integer coordinates are gs->osm_tile_x0 + c */
                    double tile_x = gs->osm_tile_x0 + c;
                    double tile_y = gs->osm_tile_y0 + r;

                    /* Calculate where this full tile would be drawn relative to the bbox top-left */
                    double draw_x = left_x + (tile_x - tx0_exact) * scale_x;
                    double draw_y = panels_y + (tile_y - ty0_exact) * scale_y;

                    Rectangle full_dst = {
                        (float)draw_x,
                        (float)draw_y,
                        (float)scale_x,
                        (float)scale_y};
                        
                    /* Intersect full_dst with panel_rect */
                    float cx1 = (full_dst.x > panel_rect.x) ? full_dst.x : panel_rect.x;
                    float cy1 = (full_dst.y > panel_rect.y) ? full_dst.y : panel_rect.y;
                    float cx2 = (full_dst.x + full_dst.width < panel_rect.x + panel_rect.width) ? (full_dst.x + full_dst.width) : (panel_rect.x + panel_rect.width);
                    float cy2 = (full_dst.y + full_dst.height < panel_rect.y + panel_rect.height) ? (full_dst.y + full_dst.height) : (panel_rect.y + panel_rect.height);

                    if (cx2 > cx1 && cy2 > cy1) {
                        Rectangle dst = {cx1, cy1, cx2 - cx1, cy2 - cy1};
                        
                        /* Calculate corresponding src rectangle */
                        float src_scale_x = (float)ui->tex_osm_tiles[r][c].width / full_dst.width;
                        float src_scale_y = (float)ui->tex_osm_tiles[r][c].height / full_dst.height;
                        
                        Rectangle src = {
                            (dst.x - full_dst.x) * src_scale_x,
                            (dst.y - full_dst.y) * src_scale_y,
                            dst.width * src_scale_x,
                            dst.height * src_scale_y
                        };

                        DrawTexturePro(ui->tex_osm_tiles[r][c], src, dst, (Vector2){0, 0}, 0.0f, WHITE);
                    }
                }
            }
        }

        /* Label */
        const char *osm_label = "OPENSTREETMAP";
        int lw = MeasureText(osm_label, fs12);
        DrawText(osm_label, left_x + (panel_size - lw) / 2, panels_y - (int)(22 * sc),
                 fs12, CLITERAL(Color){80, 160, 255, 255});

        /* Location name */
        if (gs->osm_location_name[0]) {
            int nw = MeasureText(gs->osm_location_name, fs10);
            DrawText(gs->osm_location_name, left_x + (panel_size - nw) / 2,
                     panels_y + panel_size + (int)(6 * sc), fs10,
                     CLITERAL(Color){140, 180, 230, 220});
        }

        /* Attribution */
        const char *attr = "\xC2\xA9 OpenStreetMap contributors";
        DrawText(attr, left_x, panels_y + panel_size + (int)(20 * sc), fs10,
                 CLITERAL(Color){100, 100, 120, 180});
    }

    /* ── Right panel: Game map ── */
    float tile_px = (float)panel_size / MAP_W;

    /* Border */
    DrawRectangle(right_x - 3, panels_y - 3, panel_size + 6, panel_size + 6,
                  CLITERAL(Color){30, 24, 14, 255});
    DrawRectangleLinesEx((Rectangle){(float)(right_x-3), (float)(panels_y-3),
                         (float)(panel_size+6), (float)(panel_size+6)}, 2.0f,
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
            DrawRectangle(right_x + (int)(x * tile_px), panels_y + (int)(y * tile_px),
                          (int)(tile_px + 1), (int)(tile_px + 1), c);
        }
    }

    /* Draw buildings */
    for (int i = 0; i < MAX_BUILDINGS; i++) {
        Building *b = &gs->buildings[i];
        if (!b->active) continue;
        Color bc = player_color(b->player);
        bc.a = 220;
        int bx = right_x + (int)(b->tx * tile_px);
        int by = panels_y + (int)(b->ty * tile_px);
        int bw2 = (int)(b->tw * tile_px + 1);
        int bh2 = (int)(b->th * tile_px + 1);
        DrawRectangle(bx, by, bw2, bh2, bc);
    }

    /* Draw units as dots */
    for (int i = 0; i < MAX_UNITS; i++) {
        Unit *u = &gs->units[i];
        if (!u->active || u->state == US_DEAD) continue;
        Color uc = player_color(u->player);
        int ux2 = right_x + (int)((u->wx / TILE_SIZE) * tile_px);
        int uy2 = panels_y + (int)((u->wy / TILE_SIZE) * tile_px);
        int dot = (int)(tile_px * 0.6f);
        if (dot < 2) dot = 2;
        DrawRectangle(ux2 - dot/2, uy2 - dot/2, dot, dot, uc);
    }

    /* Game map label */
    const char *game_label = "GAME MAP";
    int glw = MeasureText(game_label, fs12);
    DrawText(game_label, right_x + (panel_size - glw) / 2, panels_y - (int)(22 * sc),
             fs12, CLITERAL(Color){220, 200, 140, 255});

    /* Legend below game map */
    int leg_y = panels_y + panel_size + (int)(6 * sc);
    int leg_x = right_x;
    struct { Color c; const char *name; } legend[] = {
        {CLITERAL(Color){75, 120, 45, 255}, "Grass"},
        {CLITERAL(Color){38, 100, 185, 255}, "Water"},
        {CLITERAL(Color){28, 72, 28, 255}, "Forest"},
        {CLITERAL(Color){194, 168, 118, 255}, "Desert"},
        {CLITERAL(Color){125, 105, 75, 255}, "Road"},
        {CLITERAL(Color){220, 185, 30, 255}, "Gold"},
        {CLITERAL(Color){155, 148, 138, 255}, "Stone"},
    };
    int lcount = 7;
    int leg_col_w = panel_size / 4;
    for (int i = 0; i < lcount; i++) {
        int col = i % 4, row = i / 4;
        int lx = leg_x + col * leg_col_w;
        int ly = leg_y + row * (int)(14 * sc);
        DrawRectangle(lx, ly + 1, (int)(8 * sc), (int)(8 * sc), legend[i].c);
        DrawText(legend[i].name, lx + (int)(11 * sc), ly, fs10,
                 CLITERAL(Color){200, 185, 140, 220});
    }

    /* Title */
    const char *title = "MAP OVERVIEW";
    int tw2 = MeasureText(title, fs16);
    DrawText(title, (sw - tw2) / 2, panels_y - (int)(48 * sc), fs16,
             CLITERAL(Color){220, 200, 140, 255});

    /* Hint */
    const char *hint = "Hold [TAB] to view  |  Release to return";
    int hw = MeasureText(hint, fs11);
    DrawText(hint, (sw - hw) / 2, panels_y + panel_size + (int)(34 * sc), fs11,
             CLITERAL(Color){130, 120, 90, 220});
}

/* ─── Master HUD draw ─────────────────────────────────────── */
void hud_draw(GameState *gs, UIState *ui){
    if(gs->phase==PHASE_MENU){ draw_menu(gs, ui); return; }
    if(gs->phase==PHASE_VICTORY||gs->phase==PHASE_DEFEAT){ draw_end_screen(gs, ui); return; }

    if(gs->hero.phase != HERO_POSSESSION_OFF){
        draw_alert(gs, ui);
        if(gs->phase==PHASE_PAUSED){
            DrawRectangle(0,0,GetScreenWidth(),GetScreenHeight(),CLITERAL(Color){0,0,0,100});
            const char *pm="PAUSED  -  Press [P] to resume";
            DrawText(pm,GetScreenWidth()/2-MeasureText(pm,22)/2,GetScreenHeight()/2-11,22,CLITERAL(Color){220,200,140,255});
        }
        return;
    }

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
