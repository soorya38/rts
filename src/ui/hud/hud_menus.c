/*=============================================================
 * hud_menus.c  –  Build menu panel, master hud_draw
 *=============================================================*/
#include "game.h"
#include "ui_state.h"
#include "hud_common.h"
#include "net.h"
#include <stdio.h>
#include <string.h>

/* Forward declarations from other hud files */
extern void draw_top_bar(GameState *gs, UIState *ui);
extern void draw_bottom_panel(GameState *gs, UIState *ui);
extern void draw_minimap(GameState *gs, UIState *ui);
extern void draw_alert(GameState *gs, UIState *ui);
extern void draw_end_screen(GameState *gs, UIState *ui);
extern void draw_menu(GameState *gs, UIState *ui);
extern void draw_placement_bar(GameState *gs, UIState *ui);

static void draw_build_menu(GameState *gs, UIState *ui){
    if(!ui->build_panel_open) return;
    bool vil=false;
    for(int i=0;i<ui->sel_count;i++)
        if(gs->units[ui->sel_units[i]].type==UNIT_VILLAGER){vil=true;break;}
    if(!vil){ui->build_panel_open=false;return;}

    static const char *BLD_NAMES[BLD_COUNT]={
        "Town Center","House","Barracks","Archery Range","Stable",
        "Mill","Lumber Camp","Mining Camp","Farm"
    };

    int mx=100, my=HUD_BOT_Y-238, bw=116, bh=48, gap=6;
    float pw=(float)(bw*3+gap*2+16), ph=(float)(bh*3+gap*2+36);
    DrawRectangleRounded((Rectangle){(float)(mx-8),(float)(my-30),pw,ph},0.06f,6,CLITERAL(Color){18,14,8,245});
    DrawRectangleRoundedLines((Rectangle){(float)(mx-8),(float)(my-30),pw,ph},0.06f,6,C_HUD_LINE);
    DrawText("BUILD MENU  — select a structure",mx,my-22,12,CLITERAL(Color){200,180,100,255});

    int lp = net_get_local_player();
    int cur_age = gs->res[lp].age;
    bool has_mill=(building_find(gs,lp,BLD_MILL,true)>=0);
    bool is_feudal = (cur_age >= 1); /* Feudal Age or higher */
    struct { BldType t; const char *n; Cost c; } items[]={
        {BLD_HOUSE,        "House (H)\n25 Wood",        {0,25, 0,0}},
        {BLD_MILL,         "Mill (M)\n100 Wood",        {0,100,0,0}},
        {BLD_LUMBER_CAMP,  "Lumber Camp\n100 Wood",     {0,100,0,0}},
        {BLD_MINING_CAMP,  "Mining Camp\n100 Wood",     {0,100,0,0}},
        {BLD_BARRACKS,     "Barracks (R)\n175 Wood",    {0,175,0,0}},
        {BLD_ARCHERY_RANGE,"Arch.Range (A)\n175 Wood",  {0,175,0,0}},
        {BLD_STABLE,       "Stable\n175 Wood",          {0,175,0,0}},
        {BLD_FARM,         "Farm (F)\n60 Wood",         {0,60, 0,0}},
    };
    int n=(int)(sizeof(items)/sizeof(items[0]));
    for(int i=0;i<n;i++){
        int col=i%3, row=i/3;
        int bx=mx+col*(bw+gap), by=my+row*(bh+gap);
        bool can=res_can_afford(&gs->res[lp],items[i].c);
        bool prereq_ok=true;
        if(items[i].t==BLD_FARM && !has_mill) prereq_ok=false;
        /* Archery Range and Stable require Feudal Age */
        if((items[i].t==BLD_ARCHERY_RANGE || items[i].t==BLD_STABLE) && !is_feudal) prereq_ok=false;
        bool clickable = can && prereq_ok;
        bool pressed=draw_button(items[i].n,bx,by,bw,bh,clickable);
        if(items[i].t==BLD_FARM && !has_mill)
            DrawText("Needs Mill",bx+4,by+bh-14,9,CLITERAL(Color){220,140,60,220});
        if((items[i].t==BLD_ARCHERY_RANGE || items[i].t==BLD_STABLE) && !is_feudal)
            DrawText("Feudal Age",bx+4,by+bh-14,9,CLITERAL(Color){220,140,60,220});
        if(pressed && clickable){
            gs->build_mode.type=items[i].t;
            gs->build_mode.active=true;
            ui->build_panel_open=false;
            char msg[48];
            snprintf(msg,sizeof(msg),"Placing: %s",BLD_NAMES[items[i].t]);
            game_set_alert(gs,msg);
        }
    }
    DrawText("[ESC] Cancel  |  Hotkeys: H=House R=Barracks A=Arch F=Farm M=Mill",
             mx,my+bh*3+gap*2+4,9,CLITERAL(Color){100,90,60,200});
}

/* ─── Master HUD draw ─────────────────────────────────────── */
void hud_draw(GameState *gs, UIState *ui){
    if(gs->phase==PHASE_MENU){ draw_menu(gs, ui); return; }
    if(gs->phase==PHASE_VICTORY||gs->phase==PHASE_DEFEAT){ draw_end_screen(gs, ui); return; }

    draw_top_bar(gs, ui);
    draw_bottom_panel(gs, ui);
    draw_minimap(gs, ui);
    if(ui->build_panel_open) draw_build_menu(gs, ui);
    if(gs->build_mode.active) draw_placement_bar(gs, ui);
    draw_alert(gs, ui);

    if(gs->phase==PHASE_PAUSED){
        DrawRectangle(0,0,SCREEN_W,SCREEN_H,CLITERAL(Color){0,0,0,100});
        const char *pm="PAUSED  –  Press [P] to resume";
        DrawText(pm,SCREEN_W/2-MeasureText(pm,22)/2,SCREEN_H/2-11,22,CLITERAL(Color){220,200,140,255});
    }

    char dbuf[64];
    const char *ap[]={"Gather","Build","Military","Attack"};
    snprintf(dbuf,sizeof(dbuf),"AI: %s | mil: %d",ap[gs->ai_phase],unit_count_military(gs,1));
    DrawText(dbuf,8,HUD_TOP_H+4,10,CLITERAL(Color){100,90,65,180});
}
