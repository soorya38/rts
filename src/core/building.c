/*=============================================================
 * building.c  –  Building placement, construction, production
 *=============================================================*/
#include "game.h"

int building_place(GameState *gs, int player, BldType type, int tx, int ty){
    int w=building_tw(type), h=building_th(type);
    if(!map_is_buildable(gs,tx,ty,w,h)) return -1;
    Cost c=building_cost(type);
    if(!res_can_afford(&gs->res[player],c)) return -1;
    res_deduct(&gs->res[player],c);

    for(int i=0;i<MAX_BUILDINGS;i++){
        Building *b=&gs->buildings[i];
        if(b->active) continue;
        memset(b,0,sizeof(Building));
        b->active       = true;
        b->id           = i;
        b->player       = player;
        b->type         = type;
        b->tx           = tx; b->ty=ty;
        b->tw           = w;  b->th=h;
        b->max_hp       = building_max_hp(type);
        b->hp           = 1;
        b->construction = 0.0f;
        b->complete     = false;
        b->queue_len    = 0;
        b->train_timer  = 0.0f;
        b->rally_tx     = tx+w/2;
        b->rally_ty     = ty+h+1;
        map_place_building(gs,tx,ty,w,h,i);
        if(i >= gs->bld_count) gs->bld_count=i+1;
        return i;
    }
    return -1;
}

/* Finalise building when construction reaches 100% */
void building_on_complete(GameState *gs, Building *b){
    b->complete     = true;
    b->construction = 1.0f;
    b->hp           = b->max_hp;
    /* Farm: convert tile footprint to TILE_FARM so villagers can gather food */
    if(b->type == BLD_FARM){
        for(int dy=0;dy<b->th;dy++) for(int dx=0;dx<b->tw;dx++){
            int x=b->tx+dx, y=b->ty+dy;
            if(map_in_bounds(x,y)){
                gs->map[y][x].type        = TILE_FARM;
                gs->map[y][x].resource_amt= 400; /* food supply per tile (sync) */
            }
        }
        b->resource_amt = 400; /* total food supply for the farm building */
    }
}

void building_destroy(GameState *gs, int bid){
    Building *b = &gs->buildings[bid];
    if(!b->active) return;
    
    /* Clear from map */
    map_clear_building(gs, b->tx, b->ty, b->tw, b->th);
    
    /* If it was a farm, revert tiles to grass */
    if(b->type == BLD_FARM){
        for(int dy=0;dy<b->th;dy++) for(int dx=0;dx<b->tw;dx++){
            int x=b->tx+dx, y=b->ty+dy;
            if(map_in_bounds(x,y)){
                gs->map[y][x].type = TILE_GRASS;
                gs->map[y][x].resource_amt = 0;
            }
        }
    }
    
    b->active = false;
    b->complete = false;
    b->selected = false;
}

void building_sell(GameState *gs, int bid){
    Building *b = &gs->buildings[bid];
    if(!b->active) return;
    Cost cost = building_cost(b->type);
    res_add(&gs->res[b->player], RES_FOOD, (int)(cost.food * 0.95f));
    res_add(&gs->res[b->player], RES_WOOD, (int)(cost.wood * 0.95f));
    res_add(&gs->res[b->player], RES_GOLD, (int)(cost.gold * 0.95f));
    res_add(&gs->res[b->player], RES_STONE, (int)(cost.stone * 0.95f));
    building_destroy(gs, bid);
}

/* Place a pre-built building (for game init) */
static int building_place_complete(GameState *gs,int player,BldType type,int tx,int ty){
    int w=building_tw(type),h=building_th(type);
    for(int i=0;i<MAX_BUILDINGS;i++){
        Building *b=&gs->buildings[i];
        if(b->active) continue;
        memset(b,0,sizeof(Building));
        b->active=true; b->id=i; b->player=player; b->type=type;
        b->tx=tx; b->ty=ty; b->tw=w; b->th=h;
        b->max_hp=building_max_hp(type);
        b->hp=b->max_hp;
        b->construction=1.0f; b->complete=true;
        b->rally_tx=tx+w/2; b->rally_ty=ty+h+1;
        map_place_building(gs,tx,ty,w,h,i);
        if(i>=gs->bld_count) gs->bld_count=i+1;
        building_on_complete(gs,b);  /* handles farm tile conversion */
        return i;
    }
    return -1;
}

void building_enqueue_unit(GameState *gs, Building *b, UnitType ut){
    if(b->queue_len>=BQUEUE_CAP) return;
    if(!b->complete) return;
    Cost c=unit_cost(ut);
    if(!res_can_afford(&gs->res[b->player],c)) return;
    if(gs->res[b->player].population>=gs->res[b->player].pop_cap) return;
    res_deduct(&gs->res[b->player],c);
    b->queue[b->queue_len++]=ut;
    if(b->queue_len==1) b->train_timer=building_train_time(ut);
}



void building_update(GameState *gs, Building *b, float dt){
    if(!b->active||!b->complete) return;
    if(b->queue_len<=0) return;
    b->train_timer-=dt;
    if(b->train_timer>0) return;
    /* Spawn unit at rally point */
    UnitType ut=b->queue[0];
    float wx=(b->rally_tx+0.5f)*TILE_SIZE;
    float wy=(b->rally_ty+0.5f)*TILE_SIZE;
    unit_spawn(gs,b->player,ut,wx,wy);
    /* Shift queue */
    for(int i=0;i<b->queue_len-1;i++) b->queue[i]=b->queue[i+1];
    b->queue_len--;
    if(b->queue_len>0) b->train_timer=building_train_time(b->queue[0]);
}

void buildings_update_all(GameState *gs, float dt){
    for(int i=0;i<MAX_BUILDINGS;i++)
        building_update(gs,&gs->buildings[i],dt);
}

int building_find(GameState *gs, int player, BldType type, bool complete_only){
    for(int i=0;i<MAX_BUILDINGS;i++){
        Building *b=&gs->buildings[i];
        if(!b->active||b->player!=player||b->type!=type) continue;
        if(complete_only&&!b->complete) continue;
        return i;
    }
    return -1;
}

/* ── Public init helper (called from game_init) ─────────────── */
void buildings_init_player(GameState *gs, int player, int tc_tx, int tc_ty){
    building_place_complete(gs,player,BLD_TOWN_CENTER,tc_tx,tc_ty);
    gs->res[player].pop_cap=pop_cap_from_buildings(gs,player);
}
