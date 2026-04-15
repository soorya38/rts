/*=============================================================
 * building.c  –  Building placement, construction, production
 *=============================================================*/
#include "game.h"
#include <stdio.h>

int building_place(GameState *gs, int player, BldType type, int tx, int ty){
    int w=building_tw(type), h=building_th(type);
    if(!map_is_buildable(gs,tx,ty,w,h)) { printf("building_place failed: map_is_buildable\n"); return -1; }
    Cost c=building_cost(type);
    if(!res_can_afford(&gs->res[player],c)) { printf("building_place failed: res_can_afford\n"); return -1; }

    /* Find free slot BEFORE deducting resources to avoid leaking on failure */
    int slot = -1;
    for(int i=0;i<MAX_BUILDINGS;i++){
        if(!gs->buildings[i].active){ slot=i; break; }
    }
    if(slot < 0) { printf("building_place failed: no slots\n"); return -1; }

    res_deduct(&gs->res[player],c);

    Building *b=&gs->buildings[slot];
    memset(b,0,sizeof(Building));
    b->active       = true;
    b->id           = slot;
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
    b->active_tech  = TECH_NONE;
    b->tech_timer   = 0.0f;
    map_place_building(gs,tx,ty,w,h,slot);
    if(slot >= gs->bld_count) gs->bld_count=slot+1;
    printf("building_place success: bid=%d\n", slot);
    return slot;
}

/* Finalise building when construction reaches 100% */
void building_on_complete(GameState *gs, Building *b){
    b->complete     = true;
    b->construction = 1.0f;
    b->hp           = b->max_hp;
    /* Farm: convert tile footprint to TILE_FARM so villagers can gather food */
    if(b->type == BLD_FARM){
        int base_food = 400;
        if(gs->res[b->player].tech_unlocked[TECH_CROP_ROTATION]) base_food += 75;
        if(gs->res[b->player].tech_unlocked[TECH_FERTILIZER]) base_food += 125;
        for(int dy=0;dy<b->th;dy++) for(int dx=0;dx<b->tw;dx++){
            int x=b->tx+dx, y=b->ty+dy;
            if(map_in_bounds(x,y)){
                gs->map[y][x].type        = TILE_FARM;
                gs->map[y][x].resource_amt= base_food; /* food supply per tile (sync) */
            }
        }
        b->resource_amt = base_food; /* total food supply for the farm building */
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
        b->active_tech = TECH_NONE;
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
    
    if (b->active_tech != TECH_NONE) {
        b->tech_timer -= dt;
        if (b->tech_timer <= 0) {
            TechType t_id = b->active_tech;
            gs->res[b->player].tech_unlocked[t_id] = true;
            b->active_tech = TECH_NONE;
            
            /* Apply global updates affecting existing units immediately */
            if (t_id == TECH_IRON_WEAPONRY) {
                for (int i=0; i<MAX_UNITS; i++) {
                    Unit *u = &gs->units[i];
                    if (u->active && u->player == b->player && (u->type == UNIT_MILITIA || u->type == UNIT_MAN_AT_ARMS)) {
                        u->max_hp += 10;
                        u->hp += 10;
                        u->attack_dmg += 1;
                    }
                }
            } else if (t_id == TECH_COMPOSITE_BOWS) {
                for (int i=0; i<MAX_UNITS; i++) {
                    Unit *u = &gs->units[i];
                    if (u->active && u->player == b->player && u->type == UNIT_ARCHER) {
                        u->attack_dmg += 1;
                        u->attack_range += 1.0f;
                    }
                }
            } else if (t_id == TECH_MOUNTED_ARMOR) {
                for (int i=0; i<MAX_UNITS; i++) {
                    Unit *u = &gs->units[i];
                    if (u->active && u->player == b->player && (u->type == UNIT_KNIGHT || u->type == UNIT_SCOUT)) {
                        u->max_hp += 20;
                        u->hp += 20;
                    }
                }
            }
        }
        return; /* Block unit training while researching */
    }

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

Cost tech_cost(TechType t) {
    switch(t){
        case TECH_CROP_ROTATION:  return (Cost){75, 75, 0, 0};
        case TECH_FERTILIZER:     return (Cost){125, 125, 0, 0};
        case TECH_IRON_WEAPONRY:  return (Cost){100, 0, 50, 0};
        case TECH_COMPOSITE_BOWS: return (Cost){100, 50, 50, 0};
        case TECH_MOUNTED_ARMOR:  return (Cost){150, 0, 100, 0};
        default: return (Cost){0,0,0,0};
    }
}

float tech_time(TechType t) {
    switch(t){
        case TECH_CROP_ROTATION:  return 20.0f;
        case TECH_FERTILIZER:     return 40.0f;
        case TECH_IRON_WEAPONRY:  return 40.0f;
        case TECH_COMPOSITE_BOWS: return 35.0f;
        case TECH_MOUNTED_ARMOR:  return 50.0f;
        default: return 10.0f;
    }
}

const char* tech_name(TechType t) {
    switch(t){
        case TECH_CROP_ROTATION:  return "Crop Rotation";
        case TECH_FERTILIZER:     return "Fertilizer";
        case TECH_IRON_WEAPONRY:  return "Iron Weaponry";
        case TECH_COMPOSITE_BOWS: return "Composite Bows";
        case TECH_MOUNTED_ARMOR:  return "Mounted Armor";
        default: return "Unknown Tech";
    }
}

const char* tech_desc(TechType t) {
    switch(t){
        case TECH_CROP_ROTATION:  return "+75 Food to new farms";
        case TECH_FERTILIZER:     return "+125 Food to new farms";
        case TECH_IRON_WEAPONRY:  return "+1 Att, +10 HP (Infantry)";
        case TECH_COMPOSITE_BOWS: return "+1 Att, +1 Range (Archers)";
        case TECH_MOUNTED_ARMOR:  return "+20 HP (Cavalry)";
        default: return "";
    }
}

void building_start_tech(GameState *gs, Building *b, TechType t) {
    if (!b->active || !b->complete || b->active_tech != TECH_NONE) return;
    if (gs->res[b->player].tech_unlocked[t]) return;
    if (b->queue_len > 0) return; /* Disallow tech when units queued */
    Cost c = tech_cost(t);
    if (!res_can_afford(&gs->res[b->player], c)) return;
    res_deduct(&gs->res[b->player], c);
    b->active_tech = t;
    b->tech_timer = tech_time(t);
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
