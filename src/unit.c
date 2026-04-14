/*=============================================================
 * unit.c  –  Unit spawning, movement, gathering, combat
 *=============================================================*/
#include "game.h"

static ResType tile_to_res(TileType t){
    switch(t){
        case TILE_FOREST:  return RES_WOOD;
        case TILE_GOLD:    return RES_GOLD;
        case TILE_STONE:   return RES_STONE;
        case TILE_BERRIES:
        case TILE_FARM:    return RES_FOOD;
        default:           return RES_FOOD;
    }
}

/* ─── Stat table ─────────────────────────────────────────── */
typedef struct {
    int   hp, attack_dmg, armor;
    float attack_range, vision_range, move_speed, attack_cd;
    int   carry_cap;
} UnitStats;

static const UnitStats STATS[UNIT_COUNT] = {
    /*VILLAGER*/    {25,  3, 0, 1.3f, 4.0f,  80.0f, 1.5f, 10},
    /*SCOUT*/       {45,  3, 0, 1.3f, 7.0f, 130.0f, 2.0f,  0},
    /*MILITIA*/     {40,  4, 0, 1.3f, 4.0f,  95.0f, 2.0f,  0},
    /*MAN_AT_ARMS*/ {45,  6, 2, 1.3f, 4.0f,  95.0f, 2.0f,  0},
    /*ARCHER*/      {30,  5, 0, 5.0f, 6.0f,  90.0f, 2.0f,  0},
    /*KNIGHT*/      {100,10, 3, 1.3f, 4.0f, 115.0f, 2.0f,  0},
};

void unit_init_stats(Unit *u){
    const UnitStats *s = &STATS[u->type];
    u->hp = u->max_hp   = s->hp;
    u->attack_dmg       = s->attack_dmg;
    u->armor            = s->armor;
    u->attack_range     = s->attack_range;
    u->vision_range     = s->vision_range;
    u->move_speed       = s->move_speed;
    u->attack_cd        = s->attack_cd;
    u->attack_timer     = 0.0f;
    u->carry_cap        = s->carry_cap;
    u->carry_amt        = 0;
    u->carry_type       = RES_FOOD;
    u->gather_tx        = -1;
    u->gather_ty        = -1;
    u->build_id         = -1;
    u->target_unit      = -1;
    u->target_bld       = -1;
    u->path_len         = 0;
    u->path_idx         = 0;
    u->death_timer      = 0.8f;
}

int unit_spawn(GameState *gs, int player, UnitType type, float wx, float wy){
    if(gs->res[player].population >= gs->res[player].pop_cap) return -1;
    for(int i=0;i<MAX_UNITS;i++){
        Unit *u=&gs->units[i];
        if(u->active) continue;
        memset(u,0,sizeof(Unit));
        u->active = true;
        u->id     = i;
        u->player = player;
        u->type   = type;
        u->state  = US_IDLE;
        u->wx     = wx; u->wy = wy;
        u->facing = 0.0f;
        unit_init_stats(u);
        gs->res[player].population++;
        if(i >= gs->unit_count) gs->unit_count = i+1;
        return i;
    }
    return -1;
}

/* ─── Adjacency helper ────────────────────────────────────── */
static void find_adjacent_tile(GameState *gs, int bx, int by, int bw, int bh,
                                float ux, float uy, int *ox, int *oy){
    int best=9999; *ox=-1; *oy=-1;
    int utx=(int)(ux/TILE_SIZE), uty=(int)(uy/TILE_SIZE);
    for(int dy=-1;dy<=bh;dy++) for(int dx=-1;dx<=bw;dx++){
        int nx=bx+dx, ny=by+dy;
        /* Skip interior tiles */
        if(nx>=bx&&nx<bx+bw&&ny>=by&&ny<by+bh) continue;
        if(!map_in_bounds(nx,ny)) continue;
        if(!map_is_passable(gs,nx,ny)) continue;
        int d=abs(nx-utx)+abs(ny-uty);
        if(d<best){best=d;*ox=nx;*oy=ny;}
    }
}

/* ─── Orders ──────────────────────────────────────────────── */

void unit_give_move_order(GameState *gs, Unit *u, int tx, int ty){
    int sx=(int)(u->wx/TILE_SIZE), sy=(int)(u->wy/TILE_SIZE);
    u->path_len = pathfind(gs,sx,sy,tx,ty,u->path,MAX_PATH);
    u->path_idx = 0;
    u->state    = (u->path_len>0) ? US_MOVING : US_IDLE;
    u->target_unit=-1; u->target_bld=-1;
    u->build_id=-1; u->gather_tx=-1;
}

void unit_give_gather_order(GameState *gs, Unit *u, int tx, int ty){
    if(u->type!=UNIT_VILLAGER) return;

    /* Discard carried resource if switching to a different type without dropping off */
    if (map_in_bounds(tx, ty)) {
        ResType target_res = tile_to_res(gs->map[ty][tx].type);
        if (u->carry_amt > 0 && u->carry_type != target_res) {
            u->carry_amt = 0; /* Discard */
        }
        u->carry_type = target_res;
    }

    u->gather_tx=tx; u->gather_ty=ty;
    int bx,by;
    find_adjacent_tile(gs,tx,ty,1,1,u->wx,u->wy,&bx,&by);
    if(bx<0){u->state=US_GATHERING;return;}
    int sx=(int)(u->wx/TILE_SIZE),sy=(int)(u->wy/TILE_SIZE);
    u->path_len=pathfind(gs,sx,sy,bx,by,u->path,MAX_PATH);
    u->path_idx=0;
    u->state=(u->path_len>0)?US_MOVING:US_GATHERING;
    u->target_unit=-1; u->target_bld=-1;
}

void unit_give_dropoff_order(GameState *gs, Unit *u, int tx, int ty){
    if(u->type!=UNIT_VILLAGER || u->carry_amt == 0) return;
    int sx=(int)(u->wx/TILE_SIZE),sy=(int)(u->wy/TILE_SIZE);
    
    /* Find adjacent tile to the drop-off building */
    int bx, by;
    find_adjacent_tile(gs,tx,ty,1,1,u->wx,u->wy,&bx,&by);
    
    if(bx<0){
        u->state = US_RETURNING;
        u->path_len = 0;
        return;
    }
    
    u->path_len=pathfind(gs,sx,sy,bx,by,u->path,MAX_PATH);
    u->path_idx=0;
    u->state=(u->path_len>0)?US_RETURNING:US_IDLE;
    u->target_unit=-1; u->target_bld=-1;
    u->gather_tx=-1; /* Cancel gather target on manual drop-off to stay idle */
}

void unit_give_attack_order(GameState *gs, Unit *u, int tunit, int tbld){
    u->target_unit=tunit; u->target_bld=tbld;
    u->gather_tx=-1; u->build_id=-1;
    int tx,ty;
    if(tunit>=0){
        Unit *t=&gs->units[tunit];
        tx=(int)(t->wx/TILE_SIZE); ty=(int)(t->wy/TILE_SIZE);
    } else if(tbld>=0){
        Building *b=&gs->buildings[tbld];
        tx=b->tx+b->tw/2; ty=b->ty+b->th/2;
    } else { u->state=US_IDLE; return; }
    int sx=(int)(u->wx/TILE_SIZE),sy=(int)(u->wy/TILE_SIZE);
    u->path_len=pathfind(gs,sx,sy,tx,ty,u->path,MAX_PATH);
    u->path_idx=0;
    u->state=US_MOVING;
}

void unit_give_build_order(GameState *gs, Unit *u, int bld_id){
    if(u->type!=UNIT_VILLAGER||bld_id<0) return;
    Building *b=&gs->buildings[bld_id];
    u->build_id=bld_id;
    int bx,by;
    find_adjacent_tile(gs,b->tx,b->ty,b->tw,b->th,u->wx,u->wy,&bx,&by);
    if(bx<0){u->state=US_IDLE;return;}
    int sx=(int)(u->wx/TILE_SIZE),sy=(int)(u->wy/TILE_SIZE);
    u->path_len=pathfind(gs,sx,sy,bx,by,u->path,MAX_PATH);
    u->path_idx=0;
    u->state=(u->path_len>0)?US_MOVING:US_BUILDING;
    u->gather_tx=-1; u->target_unit=-1; u->target_bld=-1;
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
        ResType rt=tile_to_res(t->type);
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
    ResType rt=tile_to_res(t->type);
    if(u->carry_amt>0 && u->carry_type!=rt) u->carry_amt=0;
    u->carry_type=rt;
    u->anim_timer += GATHER_RATE[rt]*dt;
    int gained=(int)u->anim_timer;
    if(gained>0){
        u->anim_timer-=gained;
        
        if (fb) {
            /* Gather from farm building */
            if(gained > fb->resource_amt) gained = fb->resource_amt;
            fb->resource_amt -= gained;
            /* Sync tile for visuals if needed (though we'll destroy it soon) */
            t->resource_amt = fb->resource_amt; 
        } else {
            /* Gather from normal tile resource */
            if(gained>t->resource_amt) gained=t->resource_amt;
            t->resource_amt-=gained;
        }
        
        u->carry_amt+=gained;

        /* Resource exhausted logic */
        if(fb && fb->resource_amt <= 0){
            building_destroy(gs, fb->id);
            /* Farm destroyed -> deselect target so unit stops or re-tasks */
            u->gather_tx = -1; u->gather_ty = -1;
        } else if(!fb && t->resource_amt<=0){
            t->resource_amt=0;
            /* Normal tiles revert to grass */
            if(t->type!=TILE_FARM) t->type=TILE_GRASS;
        }
    }
    if(u->carry_amt>=u->carry_cap){
        int dtx,dty;
        if(map_find_dropoff(gs,u->player,rt,u->wx,u->wy,&dtx,&dty)){
            int sx=(int)(u->wx/TILE_SIZE),sy=(int)(u->wy/TILE_SIZE);
            u->path_len=pathfind(gs,sx,sy,dtx,dty,u->path,MAX_PATH);
            u->path_idx=0;
            u->state=(u->path_len>0)?US_RETURNING:US_IDLE;
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
        /* Call the shared completion helper (handles farm tile conversion etc.) */
        extern void building_on_complete(GameState *gs, Building *b);
        building_on_complete(gs, b);
        gs->res[b->player].pop_cap=pop_cap_from_buildings(gs,b->player);
        u->build_id=-1; u->state=US_IDLE;
    }
}

/* ─── Combat ───────────────────────────────────────────────── */
static float dist_to_unit(Unit *a,Unit *b){return dist2f(a->wx,a->wy,b->wx,b->wy)/TILE_SIZE;}
static float dist_to_bld(Unit *u,Building *b){
    return dist2f(u->wx,u->wy,(b->tx+b->tw*0.5f)*TILE_SIZE,(b->ty+b->th*0.5f)*TILE_SIZE)/TILE_SIZE;
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
    /* Validate */
    if(u->target_unit>=0){
        Unit *t=&gs->units[u->target_unit];
        if(!t->active||t->state==US_DEAD||t->state==US_DYING) u->target_unit=-1;
    }
    if(u->target_bld>=0){
        if(!gs->buildings[u->target_bld].active) u->target_bld=-1;
    }
    /* Auto-acquire */
    if(u->target_unit<0&&u->target_bld<0){
        u->target_unit=auto_find_enemy_unit(gs,u);
        if(u->target_unit<0) u->target_bld=auto_find_enemy_bld(gs,u);
        if(u->target_unit<0&&u->target_bld<0){u->state=US_IDLE;return;}
    }
    /* Distance */
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
                tx=b->tx+b->tw/2;ty=b->ty+b->th/2;
            }
            int sx=(int)(u->wx/TILE_SIZE),sy=(int)(u->wy/TILE_SIZE);
            u->path_len=pathfind(gs,sx,sy,tx,ty,u->path,MAX_PATH);
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
        int dmg=u->attack_dmg-t->armor; if(dmg<1)dmg=1;
        t->hp-=dmg;
        if(t->hp<=0){t->state=US_DYING;t->death_timer=0.8f;u->target_unit=-1;}
    } else {
        Building *b=&gs->buildings[u->target_bld];
        b->hp-=u->attack_dmg;
        if(b->hp<=0){
            if(b->type==BLD_TOWN_CENTER && b->player==1) game_set_alert(gs,"VICTORY!");
            if(b->type==BLD_TOWN_CENTER && b->player==0) game_set_alert(gs,"DEFEATED...");
            map_clear_building(gs,b->tx,b->ty,b->tw,b->th);
            b->active=false;
            u->target_bld=-1;
            /* Check win/loss */
            for(int p=0;p<NUM_PLAYERS;p++){
                bool has_tc=false;
                for(int i=0;i<MAX_BUILDINGS;i++){
                    Building *bb=&gs->buildings[i];
                    if(bb->active&&bb->player==p&&bb->type==BLD_TOWN_CENTER){has_tc=true;break;}
                }
                if(!has_tc) gs->phase=(p==0)?PHASE_DEFEAT:PHASE_VICTORY;
            }
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

    /* Path stepping for move/return/attack states */
    if(u->state==US_MOVING||u->state==US_RETURNING)
        unit_step_path(u,dt);

    switch(u->state){
        case US_IDLE:
            if(u->type!=UNIT_VILLAGER&&u->type!=UNIT_SCOUT){
                int e=auto_find_enemy_unit(gs,u);
                if(e>=0){u->target_unit=e;u->state=US_ATTACKING;}
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
        if(u->type!=UNIT_VILLAGER&&u->type!=UNIT_SCOUT) c++;
    }
    return c;
}
