/*=============================================================
 * hud_overlays.c  –  Shared helpers, icons, alert, end/menu screens
 *=============================================================*/
#include "game.h"
#include "ui_state.h"
#include "hud_common.h"
#include <stdio.h>
#include <string.h>

static const char *age_names[4]={"Dark Age","Feudal Age","Castle Age","Imperial Age"};

void draw_food_icon(int x,int y){
    DrawCircle(x+8,y+8,7,CLITERAL(Color){50,170,40,255});
    DrawCircle(x+8,y+7,4,CLITERAL(Color){100,210,70,255});
}
void draw_wood_icon(int x,int y){
    DrawRectangle(x+5,y+3,6,13,CLITERAL(Color){120,75,25,255});
    DrawRectangle(x+3,y+5,10,3,CLITERAL(Color){150,100,40,255});
}
void draw_gold_icon(int x,int y){
    DrawCircle(x+8,y+8,7,CLITERAL(Color){210,170,20,255});
    DrawCircle(x+8,y+7,4,CLITERAL(Color){240,210,60,255});
}
void draw_stone_icon(int x,int y){
    DrawCircle(x+8,y+9,7,CLITERAL(Color){155,148,138,255});
    DrawCircle(x+7,y+7,4,CLITERAL(Color){195,188,178,255});
}

bool draw_button(const char *label, int x, int y, int w, int h, bool enabled){
    Vector2 mp=GetMousePosition();
    bool hover=enabled && mp.x>=x && mp.x<=x+w && mp.y>=y && mp.y<=y+h;
    Color bg = hover ? C_BTN_HOV : C_BTN_NORM;
    if(!enabled) bg=CLITERAL(Color){30,25,15,200};
    DrawRectangle(x,y,w,h,bg);
    DrawRectangleLinesEx((Rectangle){(float)x,(float)y,(float)w,(float)h},1.5f,C_BTN_BORD);
    Color tc = enabled ? C_HUD_TXT : CLITERAL(Color){100,90,60,255};
    int fs=12, tw=MeasureText(label,fs);
    DrawText(label,x+(w-tw)/2,y+(h-fs)/2,fs,tc);
    return hover && IsMouseButtonPressed(MOUSE_LEFT_BUTTON);
}

void draw_tooltip(const char *text, int x, int y) {
    int fs = 11, tw = MeasureText(text, fs), th = fs + 6;
    DrawRectangle(x, y, tw + 10, th, CLITERAL(Color){20, 18, 12, 230});
    DrawRectangleLines(x, y, tw + 10, th, C_HUD_LINE);
    DrawText(text, x + 5, y + 3, fs, C_HUD_TXT);
}

void draw_alert(GameState *gs, UIState *ui){
    (void)ui;
    if(gs->alert_timer<=0) return;
    float alpha=clampf(gs->alert_timer/1.5f,0,1)*255;
    int tw=MeasureText(gs->alert,22);
    int bw=tw+40, bh=36;
    int bx=SCREEN_W/2-bw/2, by=HUD_TOP_H+8;
    DrawRectangleRounded((Rectangle){(float)bx,(float)by,(float)bw,(float)bh},0.3f,8,
                         CLITERAL(Color){25,20,10,(unsigned char)(alpha*0.9f)});
    DrawText(gs->alert,bx+(bw-tw)/2,by+(bh-22)/2,22,
             CLITERAL(Color){230,200,100,(unsigned char)alpha});
}

void draw_end_screen(GameState *gs, UIState *ui){
    (void)ui;
    if(gs->phase!=PHASE_VICTORY&&gs->phase!=PHASE_DEFEAT) return;
    DrawRectangle(0,0,SCREEN_W,SCREEN_H,CLITERAL(Color){0,0,0,180});
    bool win=(gs->phase==PHASE_VICTORY);
    const char *msg=win?"VICTORY!":"DEFEATED";
    Color mc=win?CLITERAL(Color){220,200,50,255}:CLITERAL(Color){210,50,40,255};
    int fs=52, tw=MeasureText(msg,fs);
    DrawText(msg,SCREEN_W/2-tw/2,SCREEN_H/2-60,fs,mc);
    const char *sub=win?"The enemy town center has fallen!":"Your town center has been destroyed!";
    int sfs=18, stw=MeasureText(sub,sfs);
    DrawText(sub,SCREEN_W/2-stw/2,SCREEN_H/2+10,sfs,CLITERAL(Color){200,185,150,255});
    DrawText("Press [R] to restart or [Q] to quit",
             SCREEN_W/2-MeasureText("Press [R] to restart or [Q] to quit",14)/2,
             SCREEN_H/2+50,14,CLITERAL(Color){150,135,100,255});
}

void draw_placement_bar(GameState *gs, UIState *ui){
    (void)ui;
    if(!gs->build_mode.active) return;
    static const char *BLD_NAMES[BLD_COUNT]={
        "Town Center","House","Barracks","Archery Range","Stable",
        "Mill","Lumber Camp","Mining Camp","Farm"
    };
    char buf[80];
    snprintf(buf,sizeof(buf),
             "  Placing: %s   ·   Click on map to place   ·   [ESC] to cancel  ",
             BLD_NAMES[gs->build_mode.type]);
    int tw=MeasureText(buf,12);
    Color bg = gs->build_mode.valid ?
        CLITERAL(Color){20,60,20,230} : CLITERAL(Color){60,20,20,230};
    DrawRectangle(SCREEN_W/2-tw/2-8,HUD_TOP_H+2,tw+16,20,bg);
    DrawRectangleLinesEx((Rectangle){(float)(SCREEN_W/2-tw/2-8),(float)(HUD_TOP_H+2),(float)(tw+16),20},
                         1,gs->build_mode.valid?CLITERAL(Color){60,180,60,200}:CLITERAL(Color){180,60,60,200});
    DrawText(buf,SCREEN_W/2-tw/2,HUD_TOP_H+5,12,
             gs->build_mode.valid?CLITERAL(Color){180,240,180,255}:CLITERAL(Color){240,160,160,255});
}

void draw_menu(GameState *gs, UIState *ui){
    DrawRectangleGradientV(0,0,SCREEN_W,SCREEN_H,
        CLITERAL(Color){8,12,22,255},CLITERAL(Color){18,28,48,255});
    for(int i=0;i<60;i++){
        int sx=(i*137)%SCREEN_W, sy=(i*197)%SCREEN_H;
        int bs=(i%3==0)?2:1;
        DrawRectangle(sx,sy,bs,bs,CLITERAL(Color){255,255,255,(unsigned char)(100+i*3)});
    }
    const char *t1="AGE OF EMPIRES II";
    const char *t2="Raylib Edition";
    int f1=48, f2=22;
    DrawText(t1,SCREEN_W/2-MeasureText(t1,f1)/2,SCREEN_H/2-140,f1,CLITERAL(Color){220,185,40,255});
    DrawText(t2,SCREEN_W/2-MeasureText(t2,f2)/2,SCREEN_H/2-85,f2,CLITERAL(Color){170,155,110,255});
    DrawRectangle(SCREEN_W/2-120,SCREEN_H/2-58,240,2,CLITERAL(Color){130,110,60,200});
    const char *lines[]={
        "Gather resources  •  Build structures  •  Train armies",
        "Destroy the enemy Town Center to win!",
        "",
        "Controls:  WASD / edge scroll  |  Mouse wheel: zoom",
        "Click to select | Drag to box-select | Click (selected) = command"
    };
    for(int i=0;i<5;i++)
        DrawText(lines[i],SCREEN_W/2-MeasureText(lines[i],12)/2,
                 SCREEN_H/2-30+i*18,12,CLITERAL(Color){150,140,110,220});
    int bw=200,bh=52,bx=SCREEN_W/2-bw/2,by=SCREEN_H/2+80;
    Vector2 mp=GetMousePosition();
    bool hover=mp.x>=bx&&mp.x<=bx+bw&&mp.y>=by&&mp.y<=by+bh;
    DrawRectangleRounded((Rectangle){(float)bx,(float)by,(float)bw,(float)bh},0.2f,8,
                         hover?CLITERAL(Color){70,58,28,255}:CLITERAL(Color){45,36,16,255});
    DrawRectangleRoundedLines((Rectangle){(float)bx,(float)by,(float)bw,(float)bh},0.2f,8,
                               CLITERAL(Color){180,155,60,255});
    const char *play="Start Game";
    int pfs=20;
    DrawText(play,bx+(bw-MeasureText(play,pfs))/2,by+(bh-pfs)/2,pfs,CLITERAL(Color){220,195,100,255});
    if (hover && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) gs->phase = PHASE_PLAYING;
    ui->menu_start_hover=hover;
    DrawText("Built with Raylib 5.5",8,SCREEN_H-20,10,CLITERAL(Color){60,55,40,200});
}

static const char *_age_names_unused(void){ return age_names[0]; } /* suppress warning */
