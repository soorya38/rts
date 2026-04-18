/*=============================================================
 * unit_ai.c  –  Unit movement, gathering, building, combat
 *=============================================================*/
#include "game.h"
#include "net.h"
#include <stdio.h>

static ResType tile_to_res_ai(TileType t){
    switch(t){
        case TILE_FOREST:  return RES_WOOD;
        case TILE_GOLD:    return RES_GOLD;
        case TILE_STONE:   return RES_STONE;
        case TILE_BERRIES:
        case TILE_FARM:    return RES_FOOD;
        default:           return RES_FOOD;
    }
}

/* ─── Movement ────────────────────────────────────────────── */
static void unit_step_path(Unit *u, float dt){
    if(u->path_idx>=u->path_len) return;
    PathCell *wp=&u->path[u->path_idx];
    float twx=wp->x*TILE_SIZE+TILE_SIZE*0.5f;
    float twy=wp->y*TILE_SIZE+TILE_SIZE*0.5f;
    float dx=twx-u->wx, dy=twy-u->wy;
    float dist=sqrtf(dx*dx+dy*dy);
    if(dist<2.0f){u->wx=twx;u->wy=twy;u->path_idx++;return;}
    float spd=u->move_speed*dt;
    if(spd>dist) spd=dist;
    u->facing=atan2f(dy,dx);
    u->wx+=(dx/dist)*spd;
    u->wy+=(dy/dist)*spd;
}

/* ─── Gather ───────────────────────────────────────────────── */
static const float GATHER_RATE[RES_COUNT]={0.4f,0.5f,0.33f,0.28f};

static void unit_do_gather(GameState *gs, Unit *u, float dt){
    if(!map_in_bounds(u->gather_tx,u->gather_ty)){u->state=US_IDLE;return;}
    Tile *t=&gs->map[u->gather_ty][u->gather_tx];

    /* Check if this tile belongs to a farm building */
    Building *fb = NULL;
    if(t->building_id >= 0){
        Building *b = &gs->buildings[t->building_id];
        if(b->active && b->type == BLD_FARM) fb = b;
    }

    int current_amt = fb ? fb->resource_amt : t->resource_amt;

    if(current_amt<=0){
        /* Find another deposit of same kind */
        int nx,ny;
        ResType rt=tile_to_res_ai(t->type);
        if(map_find_resource(gs,u->player,rt,u->gather_tx,u->gather_ty,&nx,&ny))
            unit_give_gather_order(gs,u,nx,ny);
        else u->state=US_IDLE;
        return;
    }

    /* Must be adjacent */
    int utx=(int)(u->wx/TILE_SIZE),uty=(int)(u->wy/TILE_SIZE);
    if(abs(utx-u->gather_tx)>1||abs(uty-u->gather_ty)>1){
        unit_give_gather_order(gs,u,u->gather_tx,u->gather_ty); return;
    }
    ResType rt=tile_to_res_ai(t->type);
    if(u->carry_amt>0 && u->carry_type!=rt) u->carry_amt=0;
    u->carry_type=rt;
    u->anim_timer += GATHER_RATE[rt]*dt;
    int gained=(int)u->anim_timer;
    if(gained>0){
        u->anim_timer-=gained;
        
        if (fb) {
            if(gained > fb->resource_amt) gained = fb->resource_amt;
            fb->resource_amt -= gained;
            t->resource_amt = fb->resource_amt;
        } else {
            if(gained>t->resource_amt) gained=t->resource_amt;
            t->resource_amt-=gained;
        }
        
        u->carry_amt+=gained;

        if(fb && fb->resource_amt <= 0){
            building_destroy(gs, fb->id);
            u->gather_tx = -1; u->gather_ty = -1;
        } else if(!fb && t->resource_amt<=0){
            t->resource_amt=0;
            if(t->type!=TILE_FARM) t->type=TILE_GRASS;
        }
    }
    if(u->carry_amt>=u->carry_cap){
        int dtx,dty;
        if(map_find_dropoff(gs,u->player,rt,u->wx,u->wy,&dtx,&dty)){
            /* Find the building at dropoff to get its actual size */
            int bw=1, bh=1;
            for(int i=0;i<MAX_BUILDINGS;i++){
                Building *b=&gs->buildings[i];
                if(b->active && dtx>=b->tx && dtx<b->tx+b->tw && dty>=b->ty && dty<b->ty+b->th){
                    bw=b->tw; bh=b->th; break;
                }
            }
            /* Find adjacent tile to the dropoff building */
            int bx,by;
            find_adjacent_tile(gs,dtx,dty,bw,bh,u->wx,u->wy,&bx,&by);
            if(bx<0){ u->state=US_IDLE; }
            else {
                int sx=(int)(u->wx/TILE_SIZE),sy=(int)(u->wy/TILE_SIZE);
                u->path_len=pathfind(gs,sx,sy,bx,by,u->path,ASTAR_PATH_CAP);
                u->path_idx=0;
                u->state=(u->path_len>0)?US_RETURNING:US_IDLE;
            }
        } else u->state=US_IDLE;
    }
}

static void unit_do_return(GameState *gs, Unit *u){
    if(u->path_idx<u->path_len) return;
    res_add(&gs->res[u->player],u->carry_type,u->carry_amt);
    u->carry_amt=0;
    if(u->gather_tx>=0)
        unit_give_gather_order(gs,u,u->gather_tx,u->gather_ty);
    else u->state=US_IDLE;
}

/* ─── Build ────────────────────────────────────────────────── */
static void unit_do_build(GameState *gs, Unit *u, float dt){
    if(u->build_id<0){u->state=US_IDLE;return;}
    Building *b=&gs->buildings[u->build_id];
    if(!b->active){u->build_id=-1;u->state=US_IDLE;return;}
    if(b->complete){u->build_id=-1;u->state=US_IDLE;return;}
    int utx=(int)(u->wx/TILE_SIZE),uty=(int)(u->wy/TILE_SIZE);
    bool adj=false;
    for(int dy=-1;dy<=b->th&&!adj;dy++)
        for(int dx=-1;dx<=b->tw&&!adj;dx++){
            int nx=b->tx+dx,ny=b->ty+dy;
            if(nx>=b->tx&&nx<b->tx+b->tw&&ny>=b->ty&&ny<b->ty+b->th) continue;
            if(nx==utx&&ny==uty) adj=true;
        }
    if(!adj){unit_give_build_order(gs,u,u->build_id);return;}
    b->construction+=dt*0.035f;
    if(b->construction>=1.0f){
        extern void building_on_complete(GameState *gs, Building *b);
        building_on_complete(gs, b);
        int old_cap = gs->res[b->player].pop_cap;
        gs->res[b->player].pop_cap=pop_cap_from_buildings(gs,b->player);
        printf("House completed for player %d: pop_cap %d -> %d\n", b->player, old_cap, gs->res[b->player].pop_cap);
        u->build_id=-1; u->state=US_IDLE;
    }
}

/* ─── Combat ───────────────────────────────────────────────── */
static float dist_to_unit(Unit *a,Unit *b){return dist2f(a->wx,a->wy,b->wx,b->wy)/TILE_SIZE;}
static float dist_to_bld(Unit *u,Building *b){
    float bx1 = b->tx * TILE_SIZE;
    float by1 = b->ty * TILE_SIZE;
    float bx2 = bx1 + b->tw * TILE_SIZE;
    float by2 = by1 + b->th * TILE_SIZE;
    float cx = clampf(u->wx, bx1, bx2);
    float cy = clampf(u->wy, by1, by2);
    return sqrtf((u->wx - cx) * (u->wx - cx) + (u->wy - cy) * (u->wy - cy)) / TILE_SIZE;
}

static int auto_find_enemy_unit(GameState *gs,Unit *u){
    float best=u->vision_range; int found=-1;
    for(int i=0;i<MAX_UNITS;i++){
        Unit *t=&gs->units[i];
        if(!t->active||t->player==u->player||t->state==US_DEAD||t->state==US_DYING) continue;
        float d=dist_to_unit(u,t);
        if(d<best){best=d;found=i;}
    }
    return found;
}

static int bonus_damage_vs_unit(Unit *u, Unit *t){
    switch(u->type){
        case UNIT_SPEARMAN:
            if(t->type == UNIT_SCOUT || t->type == UNIT_KNIGHT || t->type == UNIT_CAVALRY_ARCHER) return 8;
            break;
        case UNIT_SKIRMISHER:
            if(t->type == UNIT_ARCHER || t->type == UNIT_CAVALRY_ARCHER) return 7;
            break;
        case UNIT_SCOUT:
            if(t->type == UNIT_MONK) return 10;
            break;
        case UNIT_ARCHER:
            if(t->type == UNIT_SPEARMAN) return 2;
            break;
        case UNIT_KNIGHT:
            if(t->type == UNIT_ARCHER || t->type == UNIT_SKIRMISHER || t->type == UNIT_MONK) return 2;
            break;
        case UNIT_CAVALRY_ARCHER:
            if(t->type == UNIT_SPEARMAN) return 3;
            break;
        default:
            break;
    }
    return 0;
}

static int bonus_damage_vs_building(Unit *u){
    switch(u->type){
        case UNIT_MILITIA:
        case UNIT_MAN_AT_ARMS:
            return 2;
        case UNIT_KNIGHT:
            return 1;
        default:
            return 0;
    }
}

static float projectile_duration_for_distance(float dist_tiles){
    return clampf(0.08f + dist_tiles * 0.045f, 0.12f, 0.42f);
}

static void unit_do_monk_support(GameState *gs, Unit *u, float dt){
    u->attack_timer -= dt;
    if(u->attack_timer > 0.0f) return;
    int best = -1;
    float best_d = u->attack_range;
    for(int i=0;i<MAX_UNITS;i++){
        Unit *t = &gs->units[i];
        if(!t->active || t->player != u->player || t->id == u->id || t->hp >= t->max_hp) continue;
        float d = dist_to_unit(u, t);
        if(d < best_d){ best_d = d; best = i; }
    }
    if(best >= 0){
        Unit *t = &gs->units[best];
        t->hp += 3;
        if(t->hp > t->max_hp) t->hp = t->max_hp;
        u->attack_timer = 0.75f;
    }
}
static int auto_find_enemy_bld(GameState *gs,Unit *u){
    float best=u->vision_range; int found=-1;
    for(int i=0;i<MAX_BUILDINGS;i++){
        Building *b=&gs->buildings[i];
        if(!b->active||b->player==u->player||!b->complete) continue;
        float d=dist_to_bld(u,b);
        if(d<best){best=d;found=i;}
    }
    return found;
}

static void unit_do_attack(GameState *gs,Unit *u,float dt){
    u->attack_timer-=dt;
    if(u->target_unit>=0){
        Unit *t=&gs->units[u->target_unit];
        if(!t->active||t->state==US_DEAD||t->state==US_DYING) u->target_unit=-1;
    }
    if(u->target_bld>=0){
        if(!gs->buildings[u->target_bld].active) u->target_bld=-1;
    }
    if(u->target_unit<0&&u->target_bld<0){
        if(!u->stance_manual) {
            u->target_unit=auto_find_enemy_unit(gs,u);
            if(u->target_unit<0) u->target_bld=auto_find_enemy_bld(gs,u);
        }
        if(u->target_unit<0&&u->target_bld<0){u->state=US_IDLE;return;}
    }
    float dist=9999.0f;
    if(u->target_unit>=0) dist=dist_to_unit(u,&gs->units[u->target_unit]);
    else                   dist=dist_to_bld(u,&gs->buildings[u->target_bld]);

    if(dist>u->attack_range){
        if(u->path_idx>=u->path_len){
            int tx,ty;
            if(u->target_unit>=0){
                Unit *t=&gs->units[u->target_unit];
                tx=(int)(t->wx/TILE_SIZE);ty=(int)(t->wy/TILE_SIZE);
            } else {
                Building *b=&gs->buildings[u->target_bld];
                /* Find nearest passable tile adjacent to the building perimeter */
                int best_d = 99999, bx=-1, by=-1;
                int utx=(int)(u->wx/TILE_SIZE), uty=(int)(u->wy/TILE_SIZE);
                for(int dy=-1; dy<=b->th; dy++) for(int dx=-1; dx<=b->tw; dx++){
                    int nx=b->tx+dx, ny=b->ty+dy;
                    /* Skip interior tiles */
                    if(nx>=b->tx&&nx<b->tx+b->tw&&ny>=b->ty&&ny<b->ty+b->th) continue;
                    if(!map_in_bounds(nx,ny)) continue;
                    if(!map_is_passable(gs,nx,ny)) continue;
                    int d=(nx-utx)*(nx-utx)+(ny-uty)*(ny-uty);
                    if(d<best_d){best_d=d;bx=nx;by=ny;}
                }
                if(bx<0){tx=b->tx;ty=b->ty;} else {tx=bx;ty=by;}
            }
            int sx=(int)(u->wx/TILE_SIZE),sy=(int)(u->wy/TILE_SIZE);
            u->path_len=pathfind(gs,sx,sy,tx,ty,u->path,ASTAR_PATH_CAP);
            u->path_idx=0;
        }
        unit_step_path(u,dt);
        return;
    }
    u->path_len=0;u->path_idx=0;
    if(u->attack_timer>0) return;
    u->attack_timer=u->attack_cd;
    if(u->target_unit>=0){
        Unit *t=&gs->units[u->target_unit];
        if(u->type == UNIT_MONK){
            if(t->type != UNIT_MONK){
                gs->res[t->player].population--;
                gs->res[u->player].population++;
                t->player = u->player;
                t->target_unit = -1;
                t->target_bld = -1;
                t->state = US_IDLE;
                t->selected = false;
                u->target_unit = -1;
            }
        } else {
            int dmg=u->attack_dmg + bonus_damage_vs_unit(u, t) - t->armor;
            if(dmg<1)dmg=1;
            if(unit_uses_projectiles(u->type)){
                game_spawn_projectile(gs, u->player, PROJ_ARROW,
                                      u->wx, u->wy, t->wx, t->wy,
                                      u->target_unit, -1, dmg,
                                      projectile_duration_for_distance(dist), 26.0f);
            } else if(!game_damage_unit(gs, u->target_unit, dmg)){
                u->target_unit=-1;
            }
        }
    } else {
        Building *b=&gs->buildings[u->target_bld];
        if(u->type == UNIT_MONK){
            u->target_bld = -1;
            u->state = US_IDLE;
            return;
        }
        int dmg = u->attack_dmg + bonus_damage_vs_building(u);
        if(unit_uses_projectiles(u->type)){
            float bx = (b->tx + b->tw * 0.5f) * TILE_SIZE;
            float by = (b->ty + b->th * 0.5f) * TILE_SIZE;
            game_spawn_projectile(gs, u->player, PROJ_ARROW,
                                  u->wx, u->wy, bx, by,
                                  -1, u->target_bld, dmg,
                                  projectile_duration_for_distance(dist), 24.0f);
        } else if(!game_damage_building(gs, u->target_bld, dmg)){
            u->target_bld=-1;
        }
    }
}

/* ─── Main update ─────────────────────────────────────────── */
void unit_update(GameState *gs, Unit *u, float dt){
    if(!u->active) return;

    if(u->state==US_DYING){
        u->death_timer-=dt;
        if(u->death_timer<=0){u->state=US_DEAD;u->active=false;gs->res[u->player].population--;}
        return;
    }
    if(u->state==US_DEAD){u->active=false;return;}

    if(u->state==US_MOVING||u->state==US_RETURNING)
        unit_step_path(u,dt);

    switch(u->state){
        case US_IDLE:
            if(u->type == UNIT_MONK){
                unit_do_monk_support(gs, u, dt);
                break;
            }
            if(!u->stance_manual && u->type!=UNIT_VILLAGER&&u->type!=UNIT_SCOUT){
                int e=auto_find_enemy_unit(gs,u);
                if(e>=0){u->target_unit=e;u->state=US_ATTACKING;}
                else {
                    int b=auto_find_enemy_bld(gs,u);
                    if(b>=0){u->target_bld=b;u->state=US_ATTACKING;}
                }
            }
            break;
        case US_MOVING:
            if(u->path_idx>=u->path_len){
                if(u->gather_tx>=0)           u->state=US_GATHERING;
                else if(u->build_id>=0)       u->state=US_BUILDING;
                else if(u->target_unit>=0||u->target_bld>=0) u->state=US_ATTACKING;
                else                           u->state=US_IDLE;
            }
            break;
        case US_GATHERING: unit_do_gather(gs,u,dt); break;
        case US_RETURNING: unit_do_return(gs,u);    break;
        case US_BUILDING:  unit_do_build(gs,u,dt);  break;
        case US_ATTACKING: unit_do_attack(gs,u,dt); break;
        default: break;
    }
}

void units_update_all(GameState *gs, float dt){
    for(int i=0;i<MAX_UNITS;i++) unit_update(gs,&gs->units[i],dt);
}

int unit_find_idle_villager(GameState *gs, int player){
    for(int i=0;i<MAX_UNITS;i++){
        Unit *u=&gs->units[i];
        if(u->active&&u->player==player&&u->type==UNIT_VILLAGER&&u->state==US_IDLE) return i;
    }
    return -1;
}

int unit_count_military(GameState *gs, int player){
    int c=0;
    for(int i=0;i<MAX_UNITS;i++){
        Unit *u=&gs->units[i];
        if(!u->active||u->player!=player) continue;
        if(u->type!=UNIT_VILLAGER) c++;
    }
    return c;
}
