/*=============================================================
 * hero_possession.c - Core lifecycle for the temporary
 *                     first-person hero control mode.
 *=============================================================*/
#include "game.h"

static bool hero_possession_unit_alive(const GameState *gs, int unit_id){
    if(!gs || unit_id < 0 || unit_id >= MAX_UNITS) return false;
    const Unit *u = &gs->units[unit_id];
    return u->active && u->state != US_DEAD && u->state != US_DYING && u->hp > 0;
}

bool hero_possession_is_unit_eligible(const GameState *gs, int unit_id, int player){
    if(!hero_possession_unit_alive(gs, unit_id)) return false;

    const Unit *u = &gs->units[unit_id];
    if(u->player != player) return false;

    switch(u->type){
        case UNIT_SCOUT:
        case UNIT_MILITIA:
        case UNIT_MAN_AT_ARMS:
        case UNIT_SPEARMAN:
        case UNIT_ARCHER:
        case UNIT_SKIRMISHER:
        case UNIT_CAVALRY_ARCHER:
        case UNIT_KNIGHT:
            return true;
        default:
            return false;
    }
}

bool hero_possession_can_start(const GameState *gs, int unit_id, int player){
    if(!gs) return false;
    if(gs->phase != PHASE_PLAYING) return false;
    if(gs->hero.phase != HERO_POSSESSION_OFF) return false;
    if(gs->hero.cooldown_timer > 0.0f) return false;
    return hero_possession_is_unit_eligible(gs, unit_id, player);
}

bool hero_possession_start(GameState *gs, int unit_id, int player){
    if(!hero_possession_can_start(gs, unit_id, player)) return false;

    Unit *u = &gs->units[unit_id];
    gs->hero.phase = HERO_POSSESSION_ENTERING;
    gs->hero.unit_id = unit_id;
    gs->hero.duration = HERO_POSSESSION_DURATION;
    gs->hero.timer = HERO_POSSESSION_DURATION;
    gs->hero.transition_timer = 0.0f;
    gs->hero.transition_time = HERO_POSSESSION_TRANSITION_TIME;
    gs->hero.yaw = u->facing;
    gs->hero.pitch = 0.0f;
    gs->hero.stamina = 100.0f;
    gs->hero.attack_timer = 0.0f;
    gs->hero.dodge_timer = 0.0f;
    gs->hero.dodge_cooldown = 0.0f;
    gs->hero.block_timer = 0.0f;
    gs->hero.shake = 0.35f;
    gs->hero.blur = 0.65f;
    gs->hero.impact_timer = 0.0f;

    u->path_len = 0;
    u->path_idx = 0;
    u->target_unit = -1;
    u->target_bld = -1;
    u->state = US_IDLE;
    u->stance_manual = true;

    game_set_alert(gs, "Hero Possession: entering the warrior's view.");
    return true;
}

void hero_possession_request_exit(GameState *gs, const char *alert){
    if(!gs) return;
    if(gs->hero.phase == HERO_POSSESSION_OFF ||
       gs->hero.phase == HERO_POSSESSION_EXITING) return;

    gs->hero.phase = HERO_POSSESSION_EXITING;
    gs->hero.transition_timer = 0.0f;
    gs->hero.cooldown_timer = HERO_POSSESSION_COOLDOWN;
    gs->hero.shake = 0.25f;
    gs->hero.blur = 0.5f;
    if(alert && alert[0]) game_set_alert(gs, alert);
}

void hero_possession_update(GameState *gs, float dt){
    if(!gs) return;

    if(gs->hero.cooldown_timer > 0.0f){
        gs->hero.cooldown_timer -= dt;
        if(gs->hero.cooldown_timer < 0.0f) gs->hero.cooldown_timer = 0.0f;
    }

    if(gs->hero.phase == HERO_POSSESSION_OFF) return;

    if(gs->hero.shake > 0.0f){
        gs->hero.shake -= dt * 1.7f;
        if(gs->hero.shake < 0.0f) gs->hero.shake = 0.0f;
    }
    if(gs->hero.blur > 0.0f){
        gs->hero.blur -= dt * 1.25f;
        if(gs->hero.blur < 0.0f) gs->hero.blur = 0.0f;
    }
    if(gs->hero.impact_timer > 0.0f){
        gs->hero.impact_timer -= dt;
        if(gs->hero.impact_timer < 0.0f) gs->hero.impact_timer = 0.0f;
    }
    if(gs->hero.attack_timer > 0.0f){
        gs->hero.attack_timer -= dt;
        if(gs->hero.attack_timer < 0.0f) gs->hero.attack_timer = 0.0f;
    }
    if(gs->hero.dodge_timer > 0.0f){
        gs->hero.dodge_timer -= dt;
        if(gs->hero.dodge_timer < 0.0f) gs->hero.dodge_timer = 0.0f;
    }
    if(gs->hero.dodge_cooldown > 0.0f){
        gs->hero.dodge_cooldown -= dt;
        if(gs->hero.dodge_cooldown < 0.0f) gs->hero.dodge_cooldown = 0.0f;
    }
    if(gs->hero.block_timer > 0.0f){
        gs->hero.block_timer -= dt;
        if(gs->hero.block_timer < 0.0f) gs->hero.block_timer = 0.0f;
    }

    if(!hero_possession_unit_alive(gs, gs->hero.unit_id)){
        hero_possession_request_exit(gs, "Hero Possession ended: the unit has fallen.");
    }

    if(gs->hero.phase == HERO_POSSESSION_ENTERING){
        gs->hero.transition_timer += dt;
        if(gs->hero.transition_timer >= gs->hero.transition_time){
            gs->hero.phase = HERO_POSSESSION_ACTIVE;
            gs->hero.transition_timer = 0.0f;
            game_set_alert(gs, "Hero Possession active.");
        }
        return;
    }

    if(gs->hero.phase == HERO_POSSESSION_ACTIVE){
        gs->hero.timer -= dt;
        if(gs->hero.timer <= 0.0f){
            gs->hero.timer = 0.0f;
            hero_possession_request_exit(gs, "Hero Possession spent: returning to command view.");
        }
        return;
    }

    if(gs->hero.phase == HERO_POSSESSION_EXITING){
        gs->hero.transition_timer += dt;
        if(gs->hero.transition_timer >= gs->hero.transition_time){
            gs->hero.phase = HERO_POSSESSION_OFF;
            gs->hero.unit_id = -1;
            gs->hero.transition_timer = 0.0f;
            gs->hero.timer = 0.0f;
            gs->hero.attack_timer = 0.0f;
            gs->hero.dodge_timer = 0.0f;
            gs->hero.dodge_cooldown = 0.0f;
            gs->hero.block_timer = 0.0f;
            gs->hero.shake = 0.0f;
            gs->hero.blur = 0.0f;
            gs->hero.impact_timer = 0.0f;
            game_set_alert(gs, "Strategic control restored.");
        }
    }
}

float hero_possession_time_scale(const GameState *gs){
    if(!gs) return 1.0f;
    if(gs->hero.phase == HERO_POSSESSION_ENTERING ||
       gs->hero.phase == HERO_POSSESSION_EXITING){
        return 0.42f;
    }
    return 1.0f;
}

bool hero_possession_controls_unit(const GameState *gs, int unit_id){
    if(!gs || unit_id < 0) return false;
    if(gs->hero.unit_id != unit_id) return false;
    return gs->hero.phase == HERO_POSSESSION_ENTERING ||
           gs->hero.phase == HERO_POSSESSION_ACTIVE ||
           gs->hero.phase == HERO_POSSESSION_EXITING;
}
