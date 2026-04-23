/*=============================================================
 * building.c  –  Building placement, construction, production
 *=============================================================*/
#include "game.h"
#include <stdio.h>

static uint8_t building_choose_variant(int slot, int player, BldType type, int tx, int ty){
    if(type != BLD_HOUSE) return 0;
    unsigned int seed = (unsigned int)(slot * 37 + player * 53 + tx * 97 + ty * 131 + 17);
    return (uint8_t)(seed % HOUSE_VARIANT_COUNT);
}

static void building_apply_combat_stats(Building *b){
    b->attack_dmg = 0;
    b->attack_range = 0.0f;
    b->attack_cd = 1.5f;
    b->attack_timer = 0.0f;
    if(b->type == BLD_TOWN_CENTER){
        b->attack_dmg = 5;
        b->attack_range = 6.0f;
    } else if(b->type == BLD_WATCH_TOWER){
        b->attack_dmg = 6;
        b->attack_range = 8.0f;
    }
}

static void building_apply_arrow_upgrades(GameState *gs, Building *b){
    if(!gs || !b || b->attack_dmg <= 0) return;
    int atk_bonus = 0;
    int range_bonus = 0;
    PlayerRes *pr = &gs->res[b->player];
    if(pr->tech_unlocked[TECH_FORGED_ARROWS]){ atk_bonus += 1; range_bonus += 1; }
    if(pr->tech_unlocked[TECH_BODKIN_ARROW]) { atk_bonus += 1; range_bonus += 1; }
    if(pr->tech_unlocked[TECH_BRACER])       { atk_bonus += 1; range_bonus += 1; }
    b->attack_dmg += atk_bonus;
    b->attack_range += (float)range_bonus;
}

static bool unit_is_siege_target(UnitType type){
    return type == UNIT_BATTERING_RAM || type == UNIT_MANGONEL ||
           type == UNIT_SCORPION || type == UNIT_BOMBARD_CANNON;
}

static void building_refresh_upgrades(GameState *gs, Building *b){
    if(!gs || !b || !b->active) return;

    int missing_hp = 0;
    if(b->max_hp > 0) missing_hp = clampi(b->max_hp - b->hp, 0, b->max_hp);

    b->max_hp = building_max_hp(b->type);
    building_apply_combat_stats(b);
    building_apply_arrow_upgrades(gs, b);

    PlayerRes *pr = &gs->res[b->player];
    if(pr->tech_unlocked[TECH_MASONRY])     b->max_hp = (int)lroundf((float)b->max_hp * 1.15f);
    if(pr->tech_unlocked[TECH_ARCHITECTURE]) b->max_hp = (int)lroundf((float)b->max_hp * 1.20f);

    if(pr->tech_unlocked[TECH_FORTIFIED_WALL] &&
       (b->type == BLD_WALL || b->type == BLD_GATE)) {
        b->max_hp += 600;
    }
    if(pr->tech_unlocked[TECH_HOARDINGS] && b->type == BLD_TOWN_CENTER) {
        b->max_hp += 500;
    }
    if(b->type == BLD_WATCH_TOWER){
        if(pr->tech_unlocked[TECH_GUARD_TOWER]){
            b->attack_dmg += 2;
            b->attack_range += 1.0f;
        }
        if(pr->tech_unlocked[TECH_KEEP]){
            b->attack_dmg += 3;
            b->attack_range += 1.0f;
        }
        if(pr->tech_unlocked[TECH_CANNON_EMPLACEMENTS]){
            b->attack_dmg += 5;
        }
        if(pr->tech_unlocked[TECH_MISSILE_GUIDANCE]){
            b->attack_dmg += 2;
            b->attack_range += 2.0f;
        }
    }
    if((b->type == BLD_TOWN_CENTER || b->type == BLD_WATCH_TOWER) &&
       pr->tech_unlocked[TECH_MURDER_HOLES]) {
        b->attack_range += 1.0f;
    }
    if((b->type == BLD_TOWN_CENTER || b->type == BLD_WATCH_TOWER) &&
       pr->tech_unlocked[TECH_CHEMISTRY] && b->attack_dmg > 0) {
        b->attack_dmg += 1;
    }

    if(b->hp <= 0) b->hp = b->max_hp;
    else b->hp = clampi(b->max_hp - missing_hp, 1, b->max_hp);
    if(b->attack_timer > b->attack_cd) b->attack_timer = b->attack_cd;
}

static float building_projectile_duration(float dist_tiles){
    return clampf(0.10f + dist_tiles * 0.05f, 0.16f, 0.50f);
}

bool building_supports_rally(BldType type){
    switch(type){
        case BLD_TOWN_CENTER:
        case BLD_BARRACKS:
        case BLD_ARCHERY_RANGE:
        case BLD_STABLE:
        case BLD_MONASTERY:
        case BLD_SIEGE_WORKSHOP:
            return true;
        default:
            return false;
    }
}

int building_place(GameState *gs, int player, BldType type, int tx, int ty){
    int w=building_tw(type), h=building_th(type);
    if(!map_is_buildable(gs,tx,ty,w,h)) { printf("building_place failed: map_is_buildable\n"); return -1; }
    if(gs->res[player].age < building_age_required(type)) {
        printf("building_place failed: requires Feudal Age\n"); return -1;
    }
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
    b->variant      = building_choose_variant(slot, player, type, tx, ty);
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
    building_refresh_upgrades(gs, b);
    map_place_building(gs,tx,ty,w,h,slot);
    if(slot >= gs->bld_count) gs->bld_count=slot+1;
    printf("building_place success: bid=%d\n", slot);
    return slot;
}

/* Finalise building when construction reaches 100% */
void building_on_complete(GameState *gs, Building *b){
    b->complete     = true;
    b->construction = 1.0f;
    building_refresh_upgrades(gs, b);
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

/* Place a pre-built building without checking costs or age requirements */
int building_place_ready(GameState *gs,int player,BldType type,int tx,int ty){
    int w=building_tw(type),h=building_th(type);
    for(int i=0;i<MAX_BUILDINGS;i++){
        Building *b=&gs->buildings[i];
        if(b->active) continue;
        memset(b,0,sizeof(Building));
        b->active=true; b->id=i; b->player=player; b->type=type;
        b->variant=building_choose_variant(i, player, type, tx, ty);
        b->tx=tx; b->ty=ty; b->tw=w; b->th=h;
        b->max_hp=building_max_hp(type);
        b->hp=b->max_hp;
        b->active_tech = TECH_NONE;
        b->construction=1.0f; b->complete=true;
        b->rally_tx=tx+w/2; b->rally_ty=ty+h+1;
        building_refresh_upgrades(gs, b);
        map_place_building(gs,tx,ty,w,h,i);
        if(i>=gs->bld_count) gs->bld_count=i+1;
        building_on_complete(gs,b);  /* handles farm tile conversion */
        return i;
    }
    return -1;
}

int building_queued_population(const Building *b){
    return b ? b->queue_len : 0;
}

void building_enqueue_unit(GameState *gs, Building *b, UnitType ut){
    if(b->queue_len>=BQUEUE_CAP) return;
    if(!b->complete) return;
    if(b->active_tech != TECH_NONE) return;
    if(!building_can_train_unit(b->type, ut)) return;
    if(ut == UNIT_BOMBARD_CANNON &&
       !gs->res[b->player].tech_unlocked[TECH_CANNON_EMPLACEMENTS]) return;
    if(gs->res[b->player].age < unit_age_required(ut)) return;
    Cost c=unit_cost(ut);
    if(!res_can_afford(&gs->res[b->player],c)) return;
    if(gs->res[b->player].population + building_queued_population(b) >= gs->res[b->player].pop_cap) return;
    res_deduct(&gs->res[b->player],c);
    b->queue[b->queue_len++]=ut;
    if(b->queue_len==1) b->train_timer=building_train_time(ut);
}



void building_update(GameState *gs, Building *b, float dt){
    if(!b->active||!b->complete) return;

    if(b->attack_dmg > 0 && b->attack_range > 0.0f){
        b->attack_timer -= dt;
        if(b->attack_timer <= 0.0f){
            int target = -1;
            float best = b->attack_range;
            float cx = (b->tx + b->tw * 0.5f) * TILE_SIZE;
            float cy = (b->ty + b->th * 0.5f) * TILE_SIZE;
            for(int i=0;i<MAX_UNITS;i++){
                Unit *u=&gs->units[i];
                if(!u->active || u->player==b->player || u->state==US_DEAD || u->state==US_DYING) continue;
                float d = dist2f(cx, cy, u->wx, u->wy) / TILE_SIZE;
                if(d < best){ best = d; target = i; }
            }
            if(target >= 0){
                Unit *u = &gs->units[target];
                int dmg = b->attack_dmg - u->armor;
                if(gs->res[b->player].tech_unlocked[TECH_HEATED_SHOT] && unit_is_siege_target(u->type))
                    dmg += 8;
                if(dmg < 1) dmg = 1;
                if(building_uses_projectiles(b->type)){
                    ProjectileType proj = PROJ_BOLT;
                    if(b->type == BLD_WATCH_TOWER &&
                       gs->res[b->player].tech_unlocked[TECH_CANNON_EMPLACEMENTS]) {
                        proj = PROJ_STONE;
                    }
                    game_spawn_projectile(gs, b->player, proj,
                                          cx, cy, u->wx, u->wy,
                                          target, -1, dmg,
                                          building_projectile_duration(best), 34.0f);
                } else {
                    game_damage_unit(gs, target, dmg);
                }
                b->attack_timer = b->attack_cd;
            }
        }
    }
    
    if (b->active_tech != TECH_NONE) {
        b->tech_timer -= dt;
        if (b->tech_timer <= 0) {
            TechType t_id = b->active_tech;
            gs->res[b->player].tech_unlocked[t_id] = true;
            b->active_tech = TECH_NONE;

            for (int i=0; i<MAX_UNITS; i++) {
                Unit *u = &gs->units[i];
                if (u->active && u->player == b->player) unit_refresh_upgrades(gs, u);
            }
            for (int i=0; i<MAX_BUILDINGS; i++) {
                Building *tb = &gs->buildings[i];
                if (tb->active && tb->player == b->player) {
                    building_refresh_upgrades(gs, tb);
                }
            }
        }
        return; /* Block unit training while researching */
    }

    if(b->queue_len<=0) return;
    b->train_timer-=dt;
    if(b->train_timer>0) return;
    UnitType ut=b->queue[0];
    int spawn_tx = b->tx + b->tw / 2;
    int spawn_ty = b->ty + b->th + 1;
    if(!unit_find_free_tile_near(gs, spawn_tx, spawn_ty, NULL, 0, &spawn_tx, &spawn_ty)){
        spawn_tx = b->rally_tx;
        spawn_ty = b->rally_ty;
        if(!unit_find_free_tile_near(gs, spawn_tx, spawn_ty, NULL, 0, &spawn_tx, &spawn_ty)){
            b->train_timer = 0.5f;
            return;
        }
    }
    float wx=(spawn_tx+0.5f)*TILE_SIZE;
    float wy=(spawn_ty+0.5f)*TILE_SIZE;
    int uid = unit_spawn(gs,b->player,ut,wx,wy);
    if(uid < 0){
        b->train_timer = 0.5f;
        return;
    }
    if(b->rally_tx != spawn_tx || b->rally_ty != spawn_ty){
        unit_give_move_order(gs, &gs->units[uid], b->rally_tx, b->rally_ty);
    }
    /* Shift queue */
    for(int i=0;i<b->queue_len-1;i++) b->queue[i]=b->queue[i+1];
    b->queue_len--;
    if(b->queue_len>0) b->train_timer=building_train_time(b->queue[0]);
}

Cost tech_cost(TechType t) {
    switch(t){
        case TECH_CROP_ROTATION:  return (Cost){75, 75, 0, 0};
        case TECH_FERTILIZER:     return (Cost){125, 125, 0, 0};
        case TECH_HAND_MILL:      return (Cost){75, 75, 0, 0};
        case TECH_GRANARY_BASKETS:return (Cost){125, 75, 0, 0};
        case TECH_IRRIGATION:     return (Cost){200, 100, 0, 0};
        case TECH_REAPING:        return (Cost){175, 125, 0, 0};
        case TECH_DOUBLE_BIT_AXE: return (Cost){50, 100, 0, 0};
        case TECH_LOG_STRAPS:     return (Cost){75, 100, 0, 0};
        case TECH_BOW_SAW:        return (Cost){100, 150, 0, 0};
        case TECH_TIMBER_ROUTE:   return (Cost){125, 125, 0, 0};
        case TECH_TWO_MAN_SAW:    return (Cost){150, 225, 0, 0};
        case TECH_HARDWOOD_CARTS: return (Cost){175, 175, 0, 0};
        case TECH_LOOM:           return (Cost){50, 0, 0, 0};
        case TECH_WHEELBARROW:    return (Cost){150, 50, 0, 0};
        case TECH_HAND_CART:      return (Cost){300, 200, 0, 0};
        case TECH_IRON_WEAPONRY:  return (Cost){100, 0, 50, 0};
        case TECH_SQUIRES:        return (Cost){100, 0, 50, 0};
        case TECH_CHAIN_MAIL:     return (Cost){150, 0, 100, 0};
        case TECH_HARDENED_BLADES:return (Cost){125, 0, 100, 0};
        case TECH_IMPERIAL_INFANTRY:return (Cost){250, 0, 150, 0};
        case TECH_VETERAN_LEGION: return (Cost){200, 0, 175, 0};
        case TECH_COMPOSITE_BOWS: return (Cost){100, 50, 50, 0};
        case TECH_THUMB_RING:     return (Cost){100, 50, 75, 0};
        case TECH_REINFORCED_STRINGS:return (Cost){100, 100, 100, 0};
        case TECH_EAGLE_EYE:      return (Cost){0, 125, 100, 0};
        case TECH_IMPERIAL_ARCHERY:return (Cost){0, 250, 200, 0};
        case TECH_FIELD_CRAFT:    return (Cost){75, 100, 125, 0};
        case TECH_MOUNTED_ARMOR:  return (Cost){150, 0, 100, 0};
        case TECH_HUSBANDRY:      return (Cost){100, 0, 75, 0};
        case TECH_CAVALRY_DRILL:  return (Cost){150, 0, 125, 0};
        case TECH_BLOODLINES:     return (Cost){150, 0, 125, 0};
        case TECH_IMPERIAL_CAVALRY:return (Cost){250, 0, 200, 0};
        case TECH_STEEL_SPURS:    return (Cost){150, 0, 150, 0};
        case TECH_SANCTITY:       return (Cost){0, 0, 100, 0};
        case TECH_DEVOTION:       return (Cost){0, 0, 120, 0};
        case TECH_FERVOR:         return (Cost){80, 0, 120, 0};
        case TECH_ILLUMINATION:   return (Cost){75, 0, 125, 0};
        case TECH_BLOCK_PRINTING: return (Cost){0, 0, 180, 0};
        case TECH_HOLY_VISION:    return (Cost){0, 0, 150, 0};
        case TECH_REINFORCED_RAM: return (Cost){0, 150, 120, 0};
        case TECH_SIEGE_ENGINEERS:return (Cost){0, 150, 150, 0};
        case TECH_ONAGER:         return (Cost){0, 250, 300, 0};
        case TECH_DRILL_CREW:     return (Cost){100, 100, 100, 0};
        case TECH_HEAVY_SCORPION: return (Cost){0, 180, 200, 0};
        case TECH_TORSION_ENGINES:return (Cost){0, 200, 225, 0};
        case TECH_SCALE_ARMOR:    return (Cost){100, 0, 75, 0};
        case TECH_BLAST_FURNACE:  return (Cost){150, 0, 150, 0};
        case TECH_PLATE_ARMOR:    return (Cost){0, 150, 175, 0};
        case TECH_FORGED_ARROWS:  return (Cost){75, 50, 50, 0};
        case TECH_BODKIN_ARROW:   return (Cost){0, 150, 100, 0};
        case TECH_BRACER:         return (Cost){0, 200, 200, 0};
        case TECH_MASONRY:        return (Cost){150, 175, 0, 0};
        case TECH_ARCHITECTURE:   return (Cost){300, 200, 0, 0};
        case TECH_FORTIFIED_WALL: return (Cost){0, 200, 100, 0};
        case TECH_GUARD_TOWER:    return (Cost){0, 150, 100, 0};
        case TECH_KEEP:           return (Cost){0, 250, 200, 0};
        case TECH_MURDER_HOLES:   return (Cost){0, 150, 100, 0};
        case TECH_TREADMILL_CRANE:return (Cost){150, 125, 0, 0};
        case TECH_CHEMISTRY:      return (Cost){100, 0, 200, 0};
        case TECH_HOARDINGS:      return (Cost){200, 150, 0, 0};
        case TECH_HEATED_SHOT:    return (Cost){0, 0, 200, 0};
        case TECH_CANNON_EMPLACEMENTS:return (Cost){0, 250, 350, 0};
        case TECH_MISSILE_GUIDANCE:return (Cost){0, 200, 300, 0};
        default: return (Cost){0,0,0,0};
    }
}

float tech_time(TechType t) {
    switch(t){
        case TECH_CROP_ROTATION:  return 20.0f;
        case TECH_FERTILIZER:     return 40.0f;
        case TECH_HAND_MILL:      return 30.0f;
        case TECH_GRANARY_BASKETS:return 35.0f;
        case TECH_IRRIGATION:     return 45.0f;
        case TECH_REAPING:        return 50.0f;
        case TECH_DOUBLE_BIT_AXE: return 30.0f;
        case TECH_LOG_STRAPS:     return 30.0f;
        case TECH_BOW_SAW:        return 40.0f;
        case TECH_TIMBER_ROUTE:   return 40.0f;
        case TECH_TWO_MAN_SAW:    return 50.0f;
        case TECH_HARDWOOD_CARTS: return 45.0f;
        case TECH_LOOM:           return 20.0f;
        case TECH_WHEELBARROW:    return 35.0f;
        case TECH_HAND_CART:      return 45.0f;
        case TECH_IRON_WEAPONRY:  return 40.0f;
        case TECH_SQUIRES:        return 30.0f;
        case TECH_CHAIN_MAIL:     return 45.0f;
        case TECH_HARDENED_BLADES:return 40.0f;
        case TECH_IMPERIAL_INFANTRY:return 55.0f;
        case TECH_VETERAN_LEGION: return 55.0f;
        case TECH_COMPOSITE_BOWS: return 35.0f;
        case TECH_THUMB_RING:     return 35.0f;
        case TECH_REINFORCED_STRINGS:return 45.0f;
        case TECH_EAGLE_EYE:      return 40.0f;
        case TECH_IMPERIAL_ARCHERY:return 55.0f;
        case TECH_FIELD_CRAFT:    return 50.0f;
        case TECH_MOUNTED_ARMOR:  return 50.0f;
        case TECH_HUSBANDRY:      return 35.0f;
        case TECH_CAVALRY_DRILL:  return 45.0f;
        case TECH_BLOODLINES:     return 45.0f;
        case TECH_IMPERIAL_CAVALRY:return 55.0f;
        case TECH_STEEL_SPURS:    return 50.0f;
        case TECH_SANCTITY:       return 30.0f;
        case TECH_DEVOTION:       return 35.0f;
        case TECH_FERVOR:         return 35.0f;
        case TECH_ILLUMINATION:   return 45.0f;
        case TECH_BLOCK_PRINTING: return 45.0f;
        case TECH_HOLY_VISION:    return 45.0f;
        case TECH_REINFORCED_RAM: return 35.0f;
        case TECH_SIEGE_ENGINEERS:return 40.0f;
        case TECH_ONAGER:         return 55.0f;
        case TECH_DRILL_CREW:     return 35.0f;
        case TECH_HEAVY_SCORPION: return 50.0f;
        case TECH_TORSION_ENGINES:return 55.0f;
        case TECH_SCALE_ARMOR:    return 40.0f;
        case TECH_BLAST_FURNACE:  return 45.0f;
        case TECH_PLATE_ARMOR:    return 50.0f;
        case TECH_FORGED_ARROWS:  return 35.0f;
        case TECH_BODKIN_ARROW:   return 40.0f;
        case TECH_BRACER:         return 55.0f;
        case TECH_MASONRY:        return 40.0f;
        case TECH_ARCHITECTURE:   return 55.0f;
        case TECH_FORTIFIED_WALL: return 45.0f;
        case TECH_GUARD_TOWER:    return 45.0f;
        case TECH_KEEP:           return 55.0f;
        case TECH_MURDER_HOLES:   return 35.0f;
        case TECH_TREADMILL_CRANE:return 40.0f;
        case TECH_CHEMISTRY:      return 50.0f;
        case TECH_HOARDINGS:      return 45.0f;
        case TECH_HEATED_SHOT:    return 45.0f;
        case TECH_CANNON_EMPLACEMENTS:return 60.0f;
        case TECH_MISSILE_GUIDANCE:return 55.0f;
        default: return 10.0f;
    }
}

int tech_age_required(TechType t) {
    switch(t){
        case TECH_LOOM:
            return 0;
        case TECH_HAND_MILL:
        case TECH_CROP_ROTATION:
        case TECH_DOUBLE_BIT_AXE:
        case TECH_LOG_STRAPS:
        case TECH_IRON_WEAPONRY:
        case TECH_SQUIRES:
        case TECH_COMPOSITE_BOWS:
        case TECH_THUMB_RING:
        case TECH_MOUNTED_ARMOR:
        case TECH_HUSBANDRY:
        case TECH_SCALE_ARMOR:
        case TECH_FORGED_ARROWS:
        case TECH_WHEELBARROW:
            return 1;
        case TECH_GRANARY_BASKETS:
        case TECH_FERTILIZER:
        case TECH_BOW_SAW:
        case TECH_TIMBER_ROUTE:
        case TECH_CHAIN_MAIL:
        case TECH_HARDENED_BLADES:
        case TECH_REINFORCED_STRINGS:
        case TECH_EAGLE_EYE:
        case TECH_CAVALRY_DRILL:
        case TECH_BLOODLINES:
        case TECH_SANCTITY:
        case TECH_DEVOTION:
        case TECH_FERVOR:
        case TECH_ILLUMINATION:
        case TECH_REINFORCED_RAM:
        case TECH_SIEGE_ENGINEERS:
        case TECH_MASONRY:
        case TECH_FORTIFIED_WALL:
        case TECH_GUARD_TOWER:
        case TECH_MURDER_HOLES:
        case TECH_TREADMILL_CRANE:
        case TECH_HOARDINGS:
            return 2;
        case TECH_IRRIGATION:
        case TECH_REAPING:
        case TECH_TWO_MAN_SAW:
        case TECH_HARDWOOD_CARTS:
        case TECH_HAND_CART:
        case TECH_IMPERIAL_INFANTRY:
        case TECH_VETERAN_LEGION:
        case TECH_IMPERIAL_ARCHERY:
        case TECH_FIELD_CRAFT:
        case TECH_IMPERIAL_CAVALRY:
        case TECH_STEEL_SPURS:
        case TECH_BLOCK_PRINTING:
        case TECH_HOLY_VISION:
        case TECH_ONAGER:
        case TECH_DRILL_CREW:
        case TECH_HEAVY_SCORPION:
        case TECH_TORSION_ENGINES:
        case TECH_PLATE_ARMOR:
        case TECH_BRACER:
        case TECH_ARCHITECTURE:
        case TECH_KEEP:
        case TECH_CHEMISTRY:
        case TECH_HEATED_SHOT:
        case TECH_CANNON_EMPLACEMENTS:
        case TECH_MISSILE_GUIDANCE:
            return 3;
        case TECH_BLAST_FURNACE:
        case TECH_BODKIN_ARROW:
            return 2;
        default:
            return 0;
    }
}

const char* tech_name(TechType t) {
    switch(t){
        case TECH_CROP_ROTATION:  return "Crop Rotation";
        case TECH_FERTILIZER:     return "Fertilizer";
        case TECH_HAND_MILL:      return "Hand Mill";
        case TECH_GRANARY_BASKETS:return "Granary Baskets";
        case TECH_IRRIGATION:     return "Irrigation";
        case TECH_REAPING:        return "Reaping";
        case TECH_DOUBLE_BIT_AXE: return "Double-Bit Axe";
        case TECH_LOG_STRAPS:     return "Log Straps";
        case TECH_BOW_SAW:        return "Bow Saw";
        case TECH_TIMBER_ROUTE:   return "Timber Route";
        case TECH_TWO_MAN_SAW:    return "Two-Man Saw";
        case TECH_HARDWOOD_CARTS: return "Hardwood Carts";
        case TECH_LOOM:           return "Loom";
        case TECH_WHEELBARROW:    return "Wheelbarrow";
        case TECH_HAND_CART:      return "Hand Cart";
        case TECH_IRON_WEAPONRY:  return "Iron Weaponry";
        case TECH_SQUIRES:        return "Squires";
        case TECH_CHAIN_MAIL:     return "Chain Mail";
        case TECH_HARDENED_BLADES:return "Hardened Blades";
        case TECH_IMPERIAL_INFANTRY:return "Imperial Infantry";
        case TECH_VETERAN_LEGION: return "Veteran Legion";
        case TECH_COMPOSITE_BOWS: return "Composite Bows";
        case TECH_THUMB_RING:     return "Thumb Ring";
        case TECH_REINFORCED_STRINGS:return "Reinforced Strings";
        case TECH_EAGLE_EYE:      return "Eagle Eye";
        case TECH_IMPERIAL_ARCHERY:return "Imperial Archery";
        case TECH_FIELD_CRAFT:    return "Field Craft";
        case TECH_MOUNTED_ARMOR:  return "Mounted Armor";
        case TECH_HUSBANDRY:      return "Husbandry";
        case TECH_CAVALRY_DRILL:  return "Cavalry Drill";
        case TECH_BLOODLINES:     return "Bloodlines";
        case TECH_IMPERIAL_CAVALRY:return "Imperial Cavalry";
        case TECH_STEEL_SPURS:    return "Steel Spurs";
        case TECH_SANCTITY:       return "Sanctity";
        case TECH_DEVOTION:       return "Devotion";
        case TECH_FERVOR:         return "Fervor";
        case TECH_ILLUMINATION:   return "Illumination";
        case TECH_BLOCK_PRINTING: return "Block Printing";
        case TECH_HOLY_VISION:    return "Holy Vision";
        case TECH_REINFORCED_RAM: return "Reinforced Ram";
        case TECH_SIEGE_ENGINEERS:return "Siege Engineers";
        case TECH_ONAGER:         return "Onager";
        case TECH_DRILL_CREW:     return "Drill Crew";
        case TECH_HEAVY_SCORPION: return "Heavy Scorpion";
        case TECH_TORSION_ENGINES:return "Torsion Engines";
        case TECH_SCALE_ARMOR:    return "Scale Armor";
        case TECH_BLAST_FURNACE:  return "Blast Furnace";
        case TECH_PLATE_ARMOR:    return "Plate Armor";
        case TECH_FORGED_ARROWS:  return "Forged Arrows";
        case TECH_BODKIN_ARROW:   return "Bodkin Arrow";
        case TECH_BRACER:         return "Bracer";
        case TECH_MASONRY:        return "Masonry";
        case TECH_ARCHITECTURE:   return "Architecture";
        case TECH_FORTIFIED_WALL: return "Fortified Wall";
        case TECH_GUARD_TOWER:    return "Guard Tower";
        case TECH_KEEP:           return "Keep";
        case TECH_MURDER_HOLES:   return "Murder Holes";
        case TECH_TREADMILL_CRANE:return "Treadmill Crane";
        case TECH_CHEMISTRY:      return "Chemistry";
        case TECH_HOARDINGS:      return "Hoardings";
        case TECH_HEATED_SHOT:    return "Heated Shot";
        case TECH_CANNON_EMPLACEMENTS:return "Cannon Emplacements";
        case TECH_MISSILE_GUIDANCE:return "Missile Guidance";
        default: return "Unknown Tech";
    }
}

const char* tech_desc(TechType t) {
    switch(t){
        case TECH_CROP_ROTATION:  return "+75 Food to new farms";
        case TECH_FERTILIZER:     return "+125 Food to new farms";
        case TECH_HAND_MILL:      return "+15% Food gather rate";
        case TECH_GRANARY_BASKETS:return "+1 Food carry (Villagers)";
        case TECH_IRRIGATION:     return "+15% Food gather rate";
        case TECH_REAPING:        return "+2 Food carry (Villagers)";
        case TECH_DOUBLE_BIT_AXE: return "+15% Wood gather rate";
        case TECH_LOG_STRAPS:     return "+1 Wood carry (Villagers)";
        case TECH_BOW_SAW:        return "+15% Wood gather rate";
        case TECH_TIMBER_ROUTE:   return "+6 Speed (Villagers)";
        case TECH_TWO_MAN_SAW:    return "+20% Wood gather rate";
        case TECH_HARDWOOD_CARTS: return "+2 Wood carry, +6 Speed (Villagers)";
        case TECH_LOOM:           return "+15 HP, +1 Armor (Villagers)";
        case TECH_WHEELBARROW:    return "+2 Carry, +8 Speed (Villagers)";
        case TECH_HAND_CART:      return "+3 Carry, +10 Speed, +10 HP (Villagers)";
        case TECH_IRON_WEAPONRY:  return "+1 Att, +10 HP (Infantry)";
        case TECH_SQUIRES:        return "+8 Speed (Infantry)";
        case TECH_CHAIN_MAIL:     return "+1 Armor, +10 HP (Infantry)";
        case TECH_HARDENED_BLADES:return "+1 Att (Infantry)";
        case TECH_IMPERIAL_INFANTRY:return "+2 Att, +15 HP (Infantry)";
        case TECH_VETERAN_LEGION: return "+15 HP (Infantry)";
        case TECH_COMPOSITE_BOWS: return "+1 Att, +1 Range (Archers)";
        case TECH_THUMB_RING:     return "+8 Speed, faster fire (Archers)";
        case TECH_REINFORCED_STRINGS:return "+1 Att, +1 Armor (Archers)";
        case TECH_EAGLE_EYE:      return "+1 Range, +1 Vision (Archers)";
        case TECH_IMPERIAL_ARCHERY:return "+1 Att, +1 Range (Archers)";
        case TECH_FIELD_CRAFT:    return "+10 HP (Archers)";
        case TECH_MOUNTED_ARMOR:  return "+20 HP (Cavalry)";
        case TECH_HUSBANDRY:      return "+12 Speed (Cavalry)";
        case TECH_CAVALRY_DRILL:  return "+1 Att, +10 Speed (Cavalry)";
        case TECH_BLOODLINES:     return "+20 HP (Cavalry)";
        case TECH_IMPERIAL_CAVALRY:return "+20 HP, +1 Armor (Cavalry)";
        case TECH_STEEL_SPURS:    return "+1 Att (Cavalry)";
        case TECH_SANCTITY:       return "+15 HP (Monks)";
        case TECH_DEVOTION:       return "+15 HP (Monks)";
        case TECH_FERVOR:         return "+12 Speed (Monks)";
        case TECH_ILLUMINATION:   return "Faster healing/conversion (Monks)";
        case TECH_BLOCK_PRINTING: return "+1 Range (Monks)";
        case TECH_HOLY_VISION:    return "+2 Vision (Monks)";
        case TECH_REINFORCED_RAM: return "+80 HP, +4 Att (Rams)";
        case TECH_SIEGE_ENGINEERS:return "+1 Range (Siege)";
        case TECH_ONAGER:         return "+12 Att, +1 Range (Mangonels)";
        case TECH_DRILL_CREW:     return "+8 Speed (Siege)";
        case TECH_HEAVY_SCORPION: return "+8 Att, +1 Armor/Range (Scorpions)";
        case TECH_TORSION_ENGINES:return "+8 Att (Siege)";
        case TECH_SCALE_ARMOR:    return "+1 Armor (All Military)";
        case TECH_BLAST_FURNACE:  return "+1 Att (Melee Military)";
        case TECH_PLATE_ARMOR:    return "+1 Armor (Military)";
        case TECH_FORGED_ARROWS:  return "+1 Att (Archers)";
        case TECH_BODKIN_ARROW:   return "+1 Att, +1 Range (Archers/Towers)";
        case TECH_BRACER:         return "+1 Att, +1 Range (Archers/Towers)";
        case TECH_MASONRY:        return "+15% HP (Buildings)";
        case TECH_ARCHITECTURE:   return "+20% HP (Buildings)";
        case TECH_FORTIFIED_WALL: return "+600 HP (Walls/Gates)";
        case TECH_GUARD_TOWER:    return "+2 Att, +1 Rng (Towers)";
        case TECH_KEEP:           return "+3 Att, +1 Rng (Towers)";
        case TECH_MURDER_HOLES:   return "+1 Rng (TC/Towers)";
        case TECH_TREADMILL_CRANE:return "+50% Build Speed";
        case TECH_CHEMISTRY:      return "+1 Att (Archers/Defenses)";
        case TECH_HOARDINGS:      return "+500 HP (Town Center)";
        case TECH_HEATED_SHOT:    return "+8 vs Siege (Defenses)";
        case TECH_CANNON_EMPLACEMENTS:return "Unlock Bombard Cannon";
        case TECH_MISSILE_GUIDANCE:return "+2 Att, +2 Rng (Towers)";
        default: return "";
    }
}

void building_start_tech(GameState *gs, Building *b, TechType t) {
    if (!b->active || !b->complete || b->active_tech != TECH_NONE) return;
    if (gs->res[b->player].tech_unlocked[t]) return;
    if (b->queue_len > 0) return; /* Disallow tech when units queued */
    if (gs->res[b->player].age < tech_age_required(t)) return;
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
    building_place_ready(gs,player,BLD_TOWN_CENTER,tc_tx,tc_ty);
    gs->res[player].pop_cap=pop_cap_from_buildings(gs,player);
}
