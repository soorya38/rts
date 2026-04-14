/*=============================================================
 * input.c  –  Cross-platform input (no right-click required)
 *
 * Click model:
 *   Left-click on friendly unit/building  → select it
 *   Left-click on map (units selected)    → context command:
 *       enemy unit / building → attack
 *       resource tile         → gather  (villagers)
 *       unfinished building   → build   (villagers)
 *       empty ground          → move
 *   Box-drag                             → multi-select
 *=============================================================*/
#include "game.h"
#include <math.h>
#include <stdio.h>

#define MINI_SIZE  180
#define CAM_SPEED  280.0f
#define CAM_EDGE   12
#define ZOOM_SPEED 0.12f
#define ZOOM_MIN   0.35f
#define ZOOM_MAX   2.8f

/* ─── Camera ─────────────────────────────────────────────── */
static void update_camera(GameState *gs, float dt){
    Camera2D *cam=&gs->camera;
    Vector2 mp=GetMousePosition();
    float speed=CAM_SPEED/cam->zoom;

    if(IsKeyDown(KEY_W)||IsKeyDown(KEY_UP))    cam->target.y -= speed*dt;
    if(IsKeyDown(KEY_S)||IsKeyDown(KEY_DOWN))  cam->target.y += speed*dt;
    if(IsKeyDown(KEY_A)||IsKeyDown(KEY_LEFT))  cam->target.x -= speed*dt;
    if(IsKeyDown(KEY_D)||IsKeyDown(KEY_RIGHT)) cam->target.x += speed*dt;

    /* Edge scrolling (only in normal mode) */
    if(!gs->build_mode.active && !gs->build_panel_open){
        if(mp.x<CAM_EDGE)           cam->target.x -= speed*dt;
        if(mp.x>SCREEN_W-CAM_EDGE)  cam->target.x += speed*dt;
        if(mp.y<CAM_EDGE)           cam->target.y -= speed*dt;
        if(mp.y>SCREEN_H-CAM_EDGE)  cam->target.y += speed*dt;
    }

    float wheel=GetMouseWheelMove();
    if(wheel!=0.0f){
        cam->zoom+=wheel*ZOOM_SPEED*cam->zoom;
        cam->zoom=clampf(cam->zoom,ZOOM_MIN,ZOOM_MAX);
    }

    float hw=(SCREEN_W*0.5f)/cam->zoom, hh=(SCREEN_H*0.5f)/cam->zoom;
    cam->target.x=clampf(cam->target.x,hw,(float)(MAP_W*TILE_SIZE)-hw);
    cam->target.y=clampf(cam->target.y,hh,(float)(MAP_H*TILE_SIZE)-hh);
}

/* ─── Selection helpers ───────────────────────────────────── */
static bool point_in_unit(Unit *u, Vector2 wp){
    return fabsf(u->wx-wp.x)<12 && fabsf(u->wy-wp.y)<12;
}
static bool rect_intersects_unit(Unit *u,float x0,float y0,float x1,float y1){
    return u->wx>=x0&&u->wx<=x1&&u->wy>=y0&&u->wy<=y1;
}
static void clear_selection(GameState *gs){
    for(int i=0;i<gs->sel_count;i++)
        gs->units[gs->sel_units[i]].selected=false;
    gs->sel_count=0;
    if(gs->sel_building>=0){
        gs->buildings[gs->sel_building].selected=false;
        gs->sel_building=-1;
    }
}
static void select_unit(GameState *gs,int uid){
    if(gs->sel_count>=MAX_UNITS) return;
    gs->units[uid].selected=true;
    gs->sel_units[gs->sel_count++]=uid;
}

/* ─── World-hit testers ───────────────────────────────────── */
static int find_friendly_unit_at(GameState *gs, Vector2 wp){
    for(int i=0;i<MAX_UNITS;i++){
        Unit *u=&gs->units[i];
        if(!u->active||u->player!=0||u->state==US_DEAD) continue;
        if(point_in_unit(u,wp)) return i;
    }
    return -1;
}
static int find_friendly_building_at(GameState *gs, Vector2 wp){
    for(int i=0;i<MAX_BUILDINGS;i++){
        Building *b=&gs->buildings[i];
        if(!b->active||b->player!=0) continue;
        float bx=(float)b->tx*TILE_SIZE,by=(float)b->ty*TILE_SIZE;
        float bw=(float)b->tw*TILE_SIZE,bh=(float)b->th*TILE_SIZE;
        if(wp.x>=bx&&wp.x<=bx+bw&&wp.y>=by&&wp.y<=by+bh) return i;
    }
    return -1;
}
static int find_enemy_unit_at(GameState *gs, Vector2 wp){
    for(int i=0;i<MAX_UNITS;i++){
        Unit *u=&gs->units[i];
        if(!u->active||u->player!=1||u->state==US_DEAD) continue;
        /* Must be visible */
        int utx=(int)(u->wx/TILE_SIZE),uty=(int)(u->wy/TILE_SIZE);
        if(!map_in_bounds(utx,uty)) continue;
        if(gs->map[uty][utx].fog[0]!=FOG_VISIBLE) continue;
        if(point_in_unit(u,wp)) return i;
    }
    return -1;
}
static int find_enemy_building_at(GameState *gs, Vector2 wp){
    for(int i=0;i<MAX_BUILDINGS;i++){
        Building *b=&gs->buildings[i];
        if(!b->active||b->player!=1) continue;
        int bmx=clampi(b->tx,0,MAP_W-1),bmy=clampi(b->ty,0,MAP_H-1);
        if(gs->map[bmy][bmx].fog[0]==FOG_HIDDEN) continue;
        float bx=(float)b->tx*TILE_SIZE,by=(float)b->ty*TILE_SIZE;
        float bw=(float)b->tw*TILE_SIZE,bh=(float)b->th*TILE_SIZE;
        if(wp.x>=bx&&wp.x<=bx+bw&&wp.y>=by&&wp.y<=by+bh) return i;
    }
    return -1;
}
static int find_unfinished_building_at(GameState *gs, Vector2 wp){
    for(int i=0;i<MAX_BUILDINGS;i++){
        Building *b=&gs->buildings[i];
        if(!b->active||b->player!=0||b->complete) continue;
        float bx=(float)b->tx*TILE_SIZE,by=(float)b->ty*TILE_SIZE;
        float bw=(float)b->tw*TILE_SIZE,bh=(float)b->th*TILE_SIZE;
        if(wp.x>=bx&&wp.x<=bx+bw&&wp.y>=by&&wp.y<=by+bh) return i;
    }
    return -1;
}

/* ─── Issue context command to all selected units ─────────── */
static void issue_command_at(GameState *gs, Vector2 world){
    int tx=(int)(world.x/TILE_SIZE), ty=(int)(world.y/TILE_SIZE);
    if(!map_in_bounds(tx,ty)) return;

    int eu = find_enemy_unit_at(gs,world);
    int eb = (eu<0) ? find_enemy_building_at(gs,world) : -1;
    int ub = find_unfinished_building_at(gs,world);

    TileType tt=gs->map[ty][tx].type;
    bool is_resource=(tt==TILE_FOREST||tt==TILE_GOLD||
                      tt==TILE_STONE||tt==TILE_BERRIES||tt==TILE_FARM);

    for(int i=0;i<gs->sel_count;i++){
        Unit *u=&gs->units[gs->sel_units[i]];
        if(!u->active||u->player!=0) continue;

        if(eu>=0 || eb>=0){
            unit_give_attack_order(gs,u,eu,eb);
        } else if(ub>=0 && u->type==UNIT_VILLAGER){
            unit_give_build_order(gs,u,ub);
        } else if(is_resource && u->type==UNIT_VILLAGER){
            unit_give_gather_order(gs,u,tx,ty);
        } else {
            /* Spread into a loose formation */
            int col=i%5, row=i/5;
            int ntx=clampi(tx+(col-2),0,MAP_W-1);
            int nty=clampi(ty+row,    0,MAP_H-1);
            unit_give_move_order(gs,u,ntx,nty);
        }
    }
}

/* ─── Build ghost placement ───────────────────────────────── */
static void update_build_mode(GameState *gs){
    if(!gs->build_mode.active) return;
    Vector2 mp=GetMousePosition();
    if(IsKeyPressed(KEY_ESCAPE)){ gs->build_mode.active=false; return; }
    if(mp.y<42||mp.y>SCREEN_H-130) return;

    Vector2 wp=GetScreenToWorld2D(mp,gs->camera);
    int tx=(int)(wp.x/TILE_SIZE), ty=(int)(wp.y/TILE_SIZE);
    int tw=building_tw(gs->build_mode.type), th=building_th(gs->build_mode.type);
    tx-=tw/2; ty-=th/2;
    gs->build_mode.ghost_tx=tx;
    gs->build_mode.ghost_ty=ty;
    gs->build_mode.valid=map_is_buildable(gs,tx,ty,tw,th)&&
                         res_can_afford(&gs->res[0],building_cost(gs->build_mode.type));

    if(IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && gs->build_mode.valid){
        int bid=building_place(gs,0,gs->build_mode.type,tx,ty);
        if(bid>=0){
            int vid=unit_find_idle_villager(gs,0);
            if(vid>=0) unit_give_build_order(gs,&gs->units[vid],bid);
            if(!IsKeyDown(KEY_LEFT_SHIFT)) gs->build_mode.active=false;
        }
    }
    /* ESC/right-click cancel (on platforms that have it) */
    if(IsMouseButtonPressed(MOUSE_RIGHT_BUTTON)) gs->build_mode.active=false;
}

/* ─── Hotkeys ─────────────────────────────────────────────── */
static void update_hotkeys(GameState *gs){
    /* ESC: cancel build ghost → close picker → deselect */
    if(IsKeyPressed(KEY_ESCAPE)){
        if(gs->build_mode.active)    { gs->build_mode.active=false;  return; }
        if(gs->build_panel_open)     { gs->build_panel_open=false;   return; }
        clear_selection(gs);
    }
    /* B: toggle build picker (need villager) */
    if(IsKeyPressed(KEY_B)){
        bool vil=false;
        for(int i=0;i<gs->sel_count;i++)
            if(gs->units[gs->sel_units[i]].type==UNIT_VILLAGER){vil=true;break;}
        if(vil){
            bool any=gs->build_panel_open||gs->build_mode.active;
            gs->build_panel_open = any ? false : true;
            gs->build_mode.active= false;
        }
    }
    /* Quick-build hotkeys → straight to ghost placement */
    bool vil=false;
    for(int i=0;i<gs->sel_count;i++)
        if(gs->units[gs->sel_units[i]].type==UNIT_VILLAGER){vil=true;break;}
    if(vil){
        BldType qt=BLD_COUNT;
        if(IsKeyPressed(KEY_H)) qt=BLD_HOUSE;
        if(IsKeyPressed(KEY_R)) qt=BLD_BARRACKS;
        if(IsKeyPressed(KEY_A)) qt=BLD_ARCHERY_RANGE;
        if(IsKeyPressed(KEY_M)) qt=BLD_MILL;
        if(IsKeyPressed(KEY_F)) qt=BLD_FARM;
        if(qt!=BLD_COUNT && res_can_afford(&gs->res[0],building_cost(qt))){
            gs->build_mode.type      =qt;
            gs->build_mode.active    =true;
            gs->build_panel_open     =false;
            static const char *BN[BLD_COUNT]={
                "Town Center","House","Barracks","Archery Range","Stable",
                "Mill","Lumber Camp","Mining Camp","Farm"};
            char msg[48]; snprintf(msg,sizeof(msg),"Placing: %s",BN[qt]);
            game_set_alert(gs,msg);
        }
    }
    if(IsKeyPressed(KEY_P)&&gs->phase==PHASE_PLAYING)  gs->phase=PHASE_PAUSED;
    else if(IsKeyPressed(KEY_P)&&gs->phase==PHASE_PAUSED) gs->phase=PHASE_PLAYING;
    if(IsKeyPressed(KEY_DELETE) && gs->sel_building>=0){
        Building *b=&gs->buildings[gs->sel_building];
        if(b->player==0){
            map_clear_building(gs,b->tx,b->ty,b->tw,b->th);
            b->active=false; gs->sel_building=-1;
        }
    }
    if(IsKeyPressed(KEY_S)&&IsKeyDown(KEY_LEFT_SHIFT)){
        for(int i=0;i<gs->sel_count;i++){
            Unit *u=&gs->units[gs->sel_units[i]];
            u->state=US_IDLE; u->path_len=0;
        }
    }
}

/* ─── Left-click start (box-select anchor) ────────────────── */
static void handle_left_down(GameState *gs){
    Vector2 mp=GetMousePosition();
    bool over_hud=mp.y<42||mp.y>SCREEN_H-130||
                  (mp.x>SCREEN_W-MINI_SIZE-16&&mp.y>SCREEN_H-130-8);
    if(over_hud) return;
    if(gs->build_mode.active||gs->build_panel_open) return;
    gs->box_selecting=true;
    gs->box_start=mp;
}

/* ─── Left-click release (main logic) ────────────────────── */
static void handle_left_up(GameState *gs){
    if(!gs->box_selecting) return;
    gs->box_selecting=false;

    Vector2 mp  = GetMousePosition();
    Vector2 ws  = GetScreenToWorld2D(gs->box_start, gs->camera);
    Vector2 we  = GetScreenToWorld2D(mp,             gs->camera);

    /* HUD guard on release too */
    bool over_hud=mp.y<42||mp.y>SCREEN_H-130||
                  (mp.x>SCREEN_W-MINI_SIZE-16&&mp.y>SCREEN_H-130-8);
    if(over_hud){ return; }

    float dx=fabsf(we.x-ws.x), dy=fabsf(we.y-ws.y);
    bool is_box=(dx>10||dy>10);

    /* ── BOX DRAG: always selects friendly units ── */
    if(is_box){
        bool shift=IsKeyDown(KEY_LEFT_SHIFT);
        if(!shift) clear_selection(gs);
        float x0=ws.x<we.x?ws.x:we.x, x1=ws.x>we.x?ws.x:we.x;
        float y0=ws.y<we.y?ws.y:we.y, y1=ws.y>we.y?ws.y:we.y;
        for(int i=0;i<MAX_UNITS;i++){
            Unit *u=&gs->units[i];
            if(!u->active||u->player!=0||u->state==US_DEAD) continue;
            if(rect_intersects_unit(u,x0,y0,x1,y1)) select_unit(gs,i);
        }
        return;
    }

    /* ── SINGLE CLICK ── */
    bool shift=IsKeyDown(KEY_LEFT_SHIFT);
    int fu=find_friendly_unit_at(gs,we);
    int fb=find_friendly_building_at(gs,we);

    if(fu>=0){
        /* Clicked a friendly unit → select it */
        if(!shift) clear_selection(gs);
        select_unit(gs,fu);
    } else if(fb>=0 && gs->buildings[fb].complete){
        /* Clicked a complete friendly building → select it */
        clear_selection(gs);
        gs->sel_building=fb;
        gs->buildings[fb].selected=true;
    } else if(gs->sel_count>0){
        /* Units already selected, clicked on world → context command */
        issue_command_at(gs,we);
    } else {
        /* Nothing selected, clicked on empty → just clear */
        clear_selection(gs);
    }
}

/* ─── Master input update ─────────────────────────────────── */
void input_update(GameState *gs){
    float dt=GetFrameTime();
    update_camera(gs,dt);

    /* Build ghost takes over left-click entirely while active */
    if(gs->build_mode.active){
        update_build_mode(gs);
        update_hotkeys(gs);
        return;
    }

    update_hotkeys(gs);

    if(IsMouseButtonPressed(MOUSE_LEFT_BUTTON))  handle_left_down(gs);
    if(IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) handle_left_up(gs);

    /* Minimap click → pan (handled inside hud_draw) */

    /* Menu start */
    if(gs->phase==PHASE_MENU && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
        if(gs->menu_start_hover) gs->phase=PHASE_PLAYING;

    /* End screen */
    if(gs->phase==PHASE_VICTORY||gs->phase==PHASE_DEFEAT){
        if(IsKeyPressed(KEY_Q)) CloseWindow();
        if(IsKeyPressed(KEY_R)) game_init(gs);
    }
}
