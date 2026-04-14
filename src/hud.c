/*=============================================================
 * hud.c  –  HUD overlay: resources, age, unit info, minimap
 *=============================================================*/
#include "game.h"
#include <stdio.h>
#include <string.h>

/* ─── HUD constants ──────────────────────────────────────── */
#define HUD_TOP_H    42
#define HUD_BOT_H   130
#define HUD_BOT_Y   (SCREEN_H - HUD_BOT_H)
#define MINI_SIZE   180
#define MINI_X      (SCREEN_W - MINI_SIZE - 8)
#define MINI_Y      (HUD_BOT_Y + 4)

/* ─── Colors ─────────────────────────────────────────────── */
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

static const char *age_names[4]={"Dark Age","Feudal Age","Castle Age","Imperial Age"};

/* ─── Tiny icon drawing ──────────────────────────────────── */
static void draw_food_icon(int x,int y){
    DrawCircle(x+8,y+8,7,CLITERAL(Color){50,170,40,255});
    DrawCircle(x+8,y+7,4,CLITERAL(Color){100,210,70,255});
}
static void draw_wood_icon(int x,int y){
    DrawRectangle(x+5,y+3,6,13,CLITERAL(Color){120,75,25,255});
    DrawRectangle(x+3,y+5,10,3,CLITERAL(Color){150,100,40,255});
}
static void draw_gold_icon(int x,int y){
    DrawCircle(x+8,y+8,7,CLITERAL(Color){210,170,20,255});
    DrawCircle(x+8,y+7,4,CLITERAL(Color){240,210,60,255});
}
static void draw_stone_icon(int x,int y){
    DrawCircle(x+8,y+9,7,CLITERAL(Color){155,148,138,255});
    DrawCircle(x+7,y+7,4,CLITERAL(Color){195,188,178,255});
}

/* ─── Button helper ──────────────────────────────────────── */
static bool draw_button(const char *label, int x, int y, int w, int h, bool enabled){
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

/* ─── Top resource bar ────────────────────────────────────── */
static void draw_top_bar(GameState *gs){
    PlayerRes *pr=&gs->res[0];
    DrawRectangle(0,0,SCREEN_W,HUD_TOP_H,C_HUD_BG);
    DrawRectangle(0,HUD_TOP_H-1,SCREEN_W,2,C_HUD_LINE);

    char buf[32];
    int cx=10;

    /* Food */
    draw_food_icon(cx,11); cx+=20;
    snprintf(buf,sizeof(buf),"%d",pr->amount[RES_FOOD]);
    DrawText(buf,cx,14,14,C_FOOD); cx+=MeasureText(buf,14)+18;

    /* Wood */
    draw_wood_icon(cx,11); cx+=20;
    snprintf(buf,sizeof(buf),"%d",pr->amount[RES_WOOD]);
    DrawText(buf,cx,14,14,C_WOOD); cx+=MeasureText(buf,14)+18;

    /* Gold */
    draw_gold_icon(cx,11); cx+=20;
    snprintf(buf,sizeof(buf),"%d",pr->amount[RES_GOLD]);
    DrawText(buf,cx,14,14,C_GOLD); cx+=MeasureText(buf,14)+18;

    /* Stone */
    draw_stone_icon(cx,11); cx+=20;
    snprintf(buf,sizeof(buf),"%d",pr->amount[RES_STONE]);
    DrawText(buf,cx,14,14,C_STONE); cx+=MeasureText(buf,14)+18;

    /* Population */
    Color pc=(pr->population>=pr->pop_cap)?C_POP_WARN:C_POP_OK;
    snprintf(buf,sizeof(buf),"Pop: %d/%d",pr->population,pr->pop_cap);
    DrawText(buf,cx,14,13,pc);

    /* Age display (center) */
    const char *an=age_names[pr->age];
    if(pr->advancing){
        snprintf(buf,sizeof(buf),"-> %s (%.0fs)",age_names[pr->age+1],pr->advance_timer);
        DrawText(buf,SCREEN_W/2-MeasureText(buf,12)/2,14,12,C_AGE);
    } else {
        DrawText(an,SCREEN_W/2-MeasureText(an,13)/2,14,13,C_AGE);
    }

    /* Advance Age button (right side) */
    if(pr->age<3 && !pr->advancing){
        Cost c=age_advance_cost(pr->age);
        bool can=res_can_afford(pr,c);
        snprintf(buf,sizeof(buf),"Advance Age (%dF)",c.food);
        if(draw_button(buf,SCREEN_W-190,6,182,30,can))
            res_try_advance_age(gs,0);
    }

    /* Game time */
    int minutes=(int)(gs->game_time/60);
    int seconds=(int)(gs->game_time)%60;
    snprintf(buf,sizeof(buf),"%02d:%02d",minutes,seconds);
    DrawText(buf,SCREEN_W-48,14,12,CLITERAL(Color){140,130,100,255});
}

/* ─── Bottom panel ────────────────────────────────────────── */
static void draw_bottom_panel(GameState *gs){
    DrawRectangle(0,HUD_BOT_Y,SCREEN_W-MINI_SIZE-16,HUD_BOT_H,C_HUD_BG);
    DrawRectangle(0,HUD_BOT_Y,SCREEN_W-MINI_SIZE-16,2,C_HUD_LINE);

    int panel_w = SCREEN_W-MINI_SIZE-16;
    char buf[64];

    /* ── Selected Building ── */
    if(gs->sel_building>=0){
        Building *b=&gs->buildings[gs->sel_building];
        if(!b->active){gs->sel_building=-1;return;}

        static const char *BLD_NAMES[BLD_COUNT]={
            "Town Center","House","Barracks","Archery Range","Stable",
            "Mill","Lumber Camp","Mining Camp","Farm"
        };
        const char *name=BLD_NAMES[b->type];
        DrawText(name,12,HUD_BOT_Y+8,16,C_HUD_TXT);
        snprintf(buf,sizeof(buf),"HP: %d / %d",b->hp,b->max_hp);
        DrawText(buf,12,HUD_BOT_Y+28,12,CLITERAL(Color){180,165,130,255});

        if(!b->complete){
            snprintf(buf,sizeof(buf),"Under construction: %.0f%%",b->construction*100);
            DrawText(buf,12,HUD_BOT_Y+44,12,CLITERAL(Color){200,180,100,255});
            return;
        }

        /* Training queue */
        if(b->queue_len>0){
            static const char *UN[UNIT_COUNT]={"Villager","Scout","Militia","Man-at-Arms","Archer","Knight"};
            snprintf(buf,sizeof(buf),"Training: %s (%.0fs)",UN[b->queue[0]],b->train_timer);
            DrawText(buf,12,HUD_BOT_Y+44,12,C_GOLD);
            /* Progress bar */
            float prog=1.0f-(b->train_timer/building_train_time(b->queue[0]));
            DrawRectangle(12,HUD_BOT_Y+60,160,6,CLITERAL(Color){40,35,20,255});
            DrawRectangle(12,HUD_BOT_Y+60,(int)(160*prog),6,CLITERAL(Color){50,200,60,255});
        }

        /* Action buttons for this building */
        int bx=220, by=HUD_BOT_Y+10;
        switch(b->type){
            case BLD_TOWN_CENTER:
                if(draw_button("Villager\n50F",bx,by,80,50,gs->res[0].amount[RES_FOOD]>=50))
                    building_enqueue_unit(gs,b,UNIT_VILLAGER);
                if(draw_button("Scout\n80F",bx+88,by,80,50,gs->res[0].amount[RES_FOOD]>=80))
                    building_enqueue_unit(gs,b,UNIT_SCOUT);
                (void)(gs->res[0].age>=1); /* placeholder for Castle Age button */
                break;
            case BLD_BARRACKS:
                if(draw_button("Militia\n60F 20G",bx,by,90,50,
                               gs->res[0].amount[RES_FOOD]>=60&&gs->res[0].amount[RES_GOLD]>=20))
                    building_enqueue_unit(gs,b,UNIT_MILITIA);
                if(gs->res[0].age>=1 && draw_button("Man@Arms\n60F 20G",bx+98,by,90,50,
                               gs->res[0].amount[RES_FOOD]>=60&&gs->res[0].amount[RES_GOLD]>=20))
                    building_enqueue_unit(gs,b,UNIT_MAN_AT_ARMS);
                break;
            case BLD_ARCHERY_RANGE:
                if(draw_button("Archer\n25W 45G",bx,by,90,50,
                               gs->res[0].amount[RES_WOOD]>=25&&gs->res[0].amount[RES_GOLD]>=45))
                    building_enqueue_unit(gs,b,UNIT_ARCHER);
                break;
            case BLD_STABLE:
                if(draw_button("Knight\n60F 75G",bx,by,90,50,
                               gs->res[0].amount[RES_FOOD]>=60&&gs->res[0].amount[RES_GOLD]>=75))
                    building_enqueue_unit(gs,b,UNIT_KNIGHT);
                break;
            default: break;
        }
        return;
    }

    /* ── Selected Units ── */
    if(gs->sel_count==0){
        DrawText("No units selected",12,HUD_BOT_Y+8,13,CLITERAL(Color){100,90,65,200});
        DrawText("Click unit/building to select  |  Drag to box-select",
                 12,HUD_BOT_Y+28,11,CLITERAL(Color){90,80,55,180});
        DrawText("B: build menu  |  WASD: scroll  |  Mouse wheel: zoom",
                 12,HUD_BOT_Y+44,11,CLITERAL(Color){90,80,55,180});
        return;
    }

    if(gs->sel_count==1){
        Unit *u=&gs->units[gs->sel_units[0]];
        static const char *UN[UNIT_COUNT]={"Villager","Scout","Militia","Man-at-Arms","Archer","Knight"};
        static const char *ST[]={"Idle","Moving","Gathering","Returning","Building","Attacking","Dying","Dead"};
        DrawText(UN[u->type],12,HUD_BOT_Y+8,16,CLITERAL(Color){220,200,155,255});
        snprintf(buf,sizeof(buf),"HP: %d/%d  Atk: %d  Armor: %d",u->hp,u->max_hp,u->attack_dmg,u->armor);
        DrawText(buf,12,HUD_BOT_Y+28,12,CLITERAL(Color){180,165,130,255});
        snprintf(buf,sizeof(buf),"State: %s",ST[u->state]);
        DrawText(buf,12,HUD_BOT_Y+44,12,CLITERAL(Color){160,145,110,255});
        if(u->type==UNIT_VILLAGER && u->carry_amt>0){
            static const char *RT[]={"Food","Wood","Gold","Stone"};
            snprintf(buf,sizeof(buf),"Carrying: %d %s",u->carry_amt,RT[u->carry_type]);
            DrawText(buf,12,HUD_BOT_Y+60,12,C_GOLD);
        }
        /* Unit portrait (colored shape) */
        DrawRectangle(panel_w-60,HUD_BOT_Y+8,48,48,CLITERAL(Color){35,28,16,255});
        DrawRectangleLinesEx((Rectangle){(float)(panel_w-60),(float)(HUD_BOT_Y+8),48,48},1.5f,C_HUD_LINE);
        DrawCircle(panel_w-36,HUD_BOT_Y+24,8,CLITERAL(Color){220,185,145,255});
        Color mc={30,110,220,255}; if(u->player==1) mc=(Color){210,50,40,255};
        DrawRectangle(panel_w-42,HUD_BOT_Y+35,12,14,mc);
    } else {
        snprintf(buf,sizeof(buf),"%d units selected",gs->sel_count);
        DrawText(buf,12,HUD_BOT_Y+8,14,C_HUD_TXT);
        /* Show small color squares per unit */
        for(int i=0;i<gs->sel_count&&i<12;i++){
            Unit *u=&gs->units[gs->sel_units[i]];
            Color mc=(u->player==0)?CLITERAL(Color){30,110,220,255}:CLITERAL(Color){210,50,40,255};
            DrawRectangle(12+i*22,HUD_BOT_Y+30,18,18,mc);
            DrawRectangleLinesEx((Rectangle){12.0f+i*22,HUD_BOT_Y+30.0f,18,18},1,C_HUD_LINE);
            /* mini hp bar */
            float frac=(float)u->hp/u->max_hp;
            DrawRectangle(12+i*22,HUD_BOT_Y+50,18,3,CLITERAL(Color){30,30,30,200});
            DrawRectangle(12+i*22,HUD_BOT_Y+50,(int)(18*frac),3,
                          frac>0.5f?CLITERAL(Color){50,200,60,255}:CLITERAL(Color){210,50,40,255});
        }
    }
    /* Build menu button (bottom-left) */
    if(gs->sel_count>=1){
        bool vil=false;
        for(int i=0;i<gs->sel_count;i++)
            if(gs->units[gs->sel_units[i]].type==UNIT_VILLAGER){vil=true;break;}
        if(vil){
            bool menu_active = gs->build_panel_open || gs->build_mode.active;
            if(draw_button(menu_active?"[B] Cancel":"[B] Build",12,HUD_BOT_Y+80,90,36,true)){
                if(menu_active){
                    gs->build_panel_open=false;
                    gs->build_mode.active=false;
                } else {
                    gs->build_panel_open=true;
                    gs->build_mode.active=false;  /* show picker first */
                }
            }
        }
    }
}

/* ─── Build type picker panel ────────────────────────────── */
static void draw_build_menu(GameState *gs){
    /* Guard: only show when picker panel is open */
    if(!gs->build_panel_open) return;

    /* Cancel if no villager selected anymore */
    bool vil=false;
    for(int i=0;i<gs->sel_count;i++)
        if(gs->units[gs->sel_units[i]].type==UNIT_VILLAGER){vil=true;break;}
    if(!vil){gs->build_panel_open=false;return;}

    static const char *BLD_NAMES[BLD_COUNT]={
        "Town Center","House","Barracks","Archery Range","Stable",
        "Mill","Lumber Camp","Mining Camp","Farm"
    };

    int mx=100, my=HUD_BOT_Y-238, bw=116, bh=48, gap=6;
    float pw=(float)(bw*3+gap*2+16), ph=(float)(bh*3+gap*2+36);
    DrawRectangleRounded((Rectangle){(float)(mx-8),(float)(my-30),pw,ph},
                         0.06f,6,CLITERAL(Color){18,14,8,245});
    DrawRectangleRoundedLines((Rectangle){(float)(mx-8),(float)(my-30),pw,ph},
                              0.06f,6,C_HUD_LINE);
    DrawText("BUILD MENU  — select a structure",mx,my-22,12,
             CLITERAL(Color){200,180,100,255});

    struct { BldType t; const char *n; Cost c; const char *hot; } items[]={
        {BLD_HOUSE,        "House (H)\n25 Wood",        {0,25, 0,0}, "H"},
        {BLD_MILL,         "Mill (M)\n100 Wood",        {0,100,0,0}, "M"},
        {BLD_LUMBER_CAMP,  "Lumber Camp\n100 Wood",     {0,100,0,0}, ""},
        {BLD_MINING_CAMP,  "Mining Camp\n100 Wood",     {0,100,0,0}, ""},
        {BLD_BARRACKS,     "Barracks (R)\n175 Wood",    {0,175,0,0}, "R"},
        {BLD_ARCHERY_RANGE,"Arch.Range (A)\n175 Wood",  {0,175,0,0}, "A"},
        {BLD_STABLE,       "Stable\n175 Wood",          {0,175,0,0}, ""},
        {BLD_FARM,         "Farm (F)\n60 Wood",         {0,60, 0,0}, "F"},
    };
    int n=(int)(sizeof(items)/sizeof(items[0]));
    for(int i=0;i<n;i++){
        int col=i%3, row=i/3;
        int bx=mx+col*(bw+gap), by=my+row*(bh+gap);
        bool can=res_can_afford(&gs->res[0],items[i].c);
        bool pressed=draw_button(items[i].n,bx,by,bw,bh,can);
        if(pressed && can){
            /* ── KEY TRANSITION: picker → ghost placement ── */
            gs->build_mode.type=items[i].t;
            gs->build_mode.active=true;      /* start ghost placement   */
            gs->build_panel_open=false;  /* close the picker panel  */
            /* Notify */
            char msg[48];
            snprintf(msg,sizeof(msg),"Placing: %s",BLD_NAMES[items[i].t]);
            game_set_alert(gs,msg);
        }
        /* Hotkey: also activate via letter key */
        if(can && items[i].hot[0] && IsKeyPressed(items[i].hot[0]-'A'+KEY_A+(items[i].hot[0]-'A'<0?0:0))){
            /* handled in input.c via KEY_H/R/A/M/F */
        }
        (void)can; /* suppress warning if pressed branch used */
    }
    DrawText("[ESC] Cancel  |  Hotkeys: H=House R=Barracks A=Arch F=Farm M=Mill",
             mx,my+bh*3+gap*2+4,9,CLITERAL(Color){100,90,60,200});
}

/* ─── Minimap ─────────────────────────────────────────────── */
static void draw_minimap(GameState *gs){
    /* Background */
    DrawRectangle(MINI_X-2,MINI_Y-2,MINI_SIZE+4,MINI_SIZE+4,C_HUD_BG);
    DrawRectangleLinesEx((Rectangle){MINI_X-2.0f,MINI_Y-2.0f,MINI_SIZE+4.0f,MINI_SIZE+4.0f},
                         1.5f,C_HUD_LINE);

    float sx=(float)MINI_SIZE/MAP_W;
    float sy=(float)MINI_SIZE/MAP_H;
    int ps=(int)(sx<1?1:sx), qs=(int)(sy<1?1:sy);

    /* Tiles */
    for(int y=0;y<MAP_H;y++) for(int x=0;x<MAP_W;x++){
        FogState fs=gs->map[y][x].fog[0];
        if(fs==FOG_HIDDEN) continue;
        Color c;
        switch(gs->map[y][x].type){
            case TILE_GRASS:   c=CLITERAL(Color){55,100,38,255};  break;
            case TILE_WATER:   c=CLITERAL(Color){30, 80,160,255}; break;
            case TILE_FOREST:  c=CLITERAL(Color){22, 60,22,255};  break;
            case TILE_GOLD:    c=CLITERAL(Color){200,170,20,255}; break;
            case TILE_STONE:   c=CLITERAL(Color){140,130,120,255};break;
            case TILE_BERRIES: c=CLITERAL(Color){160,30,30,255};  break;
            default:           c=CLITERAL(Color){130,100,50,255}; break;
        }
        if(fs==FOG_EXPLORED){ c.r/=2;c.g/=2;c.b/=2; }
        DrawRectangle(MINI_X+(int)(x*sx),MINI_Y+(int)(y*sy),ps,qs,c);
    }

    /* Buildings */
    for(int i=0;i<MAX_BUILDINGS;i++){
        Building *b=&gs->buildings[i];
        if(!b->active) continue;
        FogState fs=gs->map[clampi(b->ty,0,MAP_H-1)][clampi(b->tx,0,MAP_W-1)].fog[0];
        if(fs==FOG_HIDDEN&&b->player!=0) continue;
        Color c=(b->player==0)?CLITERAL(Color){30,110,220,255}:CLITERAL(Color){210,50,40,255};
        DrawRectangle(MINI_X+(int)(b->tx*sx),MINI_Y+(int)(b->ty*sy),
                      (int)(b->tw*sx)+1,(int)(b->th*sy)+1,c);
    }

    /* Units */
    for(int i=0;i<MAX_UNITS;i++){
        Unit *u=&gs->units[i];
        if(!u->active||u->state==US_DEAD) continue;
        int utx=(int)(u->wx/TILE_SIZE),uty=(int)(u->wy/TILE_SIZE);
        FogState fs=gs->map[clampi(uty,0,MAP_H-1)][clampi(utx,0,MAP_W-1)].fog[0];
        if(fs==FOG_HIDDEN&&u->player!=0) continue;
        Color c=(u->player==0)?CLITERAL(Color){80,180,255,255}:CLITERAL(Color){255,100,80,255};
        DrawRectangle(MINI_X+(int)(u->wx/TILE_SIZE*sx)-1,
                      MINI_Y+(int)(u->wy/TILE_SIZE*sy)-1,2,2,c);
    }

    /* Camera viewport rectangle */
    float cam_l=gs->camera.target.x - (SCREEN_W/2)/gs->camera.zoom;
    float cam_t=gs->camera.target.y - (SCREEN_H/2)/gs->camera.zoom;
    float cam_w=SCREEN_W/gs->camera.zoom;
    float cam_h=SCREEN_H/gs->camera.zoom;
    DrawRectangleLinesEx((Rectangle){
        MINI_X + cam_l/TILE_SIZE*sx,
        MINI_Y + cam_t/TILE_SIZE*sy,
        cam_w/TILE_SIZE*sx,
        cam_h/TILE_SIZE*sy
    },1,CLITERAL(Color){220,200,150,200});

    /* Click on minimap → pan camera */
    Vector2 mp=GetMousePosition();
    if(IsMouseButtonDown(MOUSE_LEFT_BUTTON) &&
       mp.x>=MINI_X && mp.x<=MINI_X+MINI_SIZE &&
       mp.y>=MINI_Y && mp.y<=MINI_Y+MINI_SIZE){
        float rx=(mp.x-MINI_X)/MINI_SIZE;
        float ry=(mp.y-MINI_Y)/MINI_SIZE;
        gs->camera.target=(Vector2){rx*MAP_W*TILE_SIZE, ry*MAP_H*TILE_SIZE};
    }
}

/* ─── Alert banner ────────────────────────────────────────── */
static void draw_alert(GameState *gs){
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

/* ─── Victory / Defeat screen ────────────────────────────── */
static void draw_end_screen(GameState *gs){
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

/* ─── Menu screen ────────────────────────────────────────── */
static void draw_menu(GameState *gs){
    /* Gradient background */
    DrawRectangleGradientV(0,0,SCREEN_W,SCREEN_H,
        CLITERAL(Color){8,12,22,255},CLITERAL(Color){18,28,48,255});

    /* Title stars */
    for(int i=0;i<60;i++){
        int sx=(i*137)%SCREEN_W, sy=(i*197)%SCREEN_H;
        int bs=(i%3==0)?2:1;
        DrawRectangle(sx,sy,bs,bs,CLITERAL(Color){255,255,255,(unsigned char)(100+i*3)});
    }

    /* Title */
    const char *t1="AGE OF EMPIRES II";
    const char *t2="Raylib Edition";
    int f1=48, f2=22;
    DrawText(t1,SCREEN_W/2-MeasureText(t1,f1)/2,SCREEN_H/2-140,f1,
             CLITERAL(Color){220,185,40,255});
    DrawText(t2,SCREEN_W/2-MeasureText(t2,f2)/2,SCREEN_H/2-85,f2,
             CLITERAL(Color){170,155,110,255});
    DrawRectangle(SCREEN_W/2-120,SCREEN_H/2-58,240,2,CLITERAL(Color){130,110,60,200});

    /* Instructions preview */
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

    /* Play button */
    int bw=200,bh=52,bx=SCREEN_W/2-bw/2,by=SCREEN_H/2+80;
    Vector2 mp=GetMousePosition();
    bool hover=mp.x>=bx&&mp.x<=bx+bw&&mp.y>=by&&mp.y<=by+bh;
    DrawRectangleRounded((Rectangle){(float)bx,(float)by,(float)bw,(float)bh},0.2f,8,
                         hover?CLITERAL(Color){70,58,28,255}:CLITERAL(Color){45,36,16,255});
    DrawRectangleRoundedLines((Rectangle){(float)bx,(float)by,(float)bw,(float)bh},0.2f,8,
                              CLITERAL(Color){180,155,60,255});
    const char *play="Start Game";
    DrawText(play,bx+(bw-MeasureText(play,20))/2,by+(bh-20)/2,20,CLITERAL(Color){220,195,100,255});
    gs->menu_start_hover=hover;

    /* Copyright */
    DrawText("Built with Raylib 5.5",8,SCREEN_H-20,10,CLITERAL(Color){60,55,40,200});
}

/* ─── Ghost placement status bar ─────────────────────────── */
static void draw_placement_bar(GameState *gs){
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

/* ─── Master HUD draw ─────────────────────────────────────── */
void hud_draw(GameState *gs){
    if(gs->phase==PHASE_MENU){ draw_menu(gs); return; }
    if(gs->phase==PHASE_VICTORY||gs->phase==PHASE_DEFEAT){ draw_end_screen(gs); return; }

    draw_top_bar(gs);
    draw_bottom_panel(gs);
    draw_minimap(gs);
    /* Type picker (choose which building) */
    if(gs->build_panel_open) draw_build_menu(gs);
    /* Ghost placement bar (after type chosen, click to place) */
    if(gs->build_mode.active) draw_placement_bar(gs);
    draw_alert(gs);

    /* Pause overlay */
    if(gs->phase==PHASE_PAUSED){
        DrawRectangle(0,0,SCREEN_W,SCREEN_H,CLITERAL(Color){0,0,0,100});
        const char *pm="PAUSED  –  Press [P] to resume";
        DrawText(pm,SCREEN_W/2-MeasureText(pm,22)/2,SCREEN_H/2-11,22,
                 CLITERAL(Color){220,200,140,255});
    }

    /* AI phase debug (small) */
    char dbuf[64];
    const char *ap[]={"Gather","Build","Military","Attack"};
    snprintf(dbuf,sizeof(dbuf),"AI: %s | mil: %d",ap[gs->ai_phase],unit_count_military(gs,1));
    DrawText(dbuf,8,HUD_TOP_H+4,10,CLITERAL(Color){100,90,65,180});
}
