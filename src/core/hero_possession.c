/*=============================================================
 * hero_possession.c  –  First-person hero control mode lifecycle
 *
 * Manages the enter/active/exit state machine for the temporary
 * first-person unit control mode.  While active, the game clock
 * runs at reduced speed during transitions and the possessed
 * unit's AI is suppressed.
 *=============================================================*/
#include "game.h"

/* Decay rates for visual effects (per second). */
static const float SHAKE_DECAY_RATE  = 1.7f;
static const float BLUR_DECAY_RATE   = 1.25f;

/* Slow-motion time scale during enter/exit transitions. */
static const float TRANSITION_TIME_SCALE = 0.42f;

/* ── Internal helpers ──────────────────────────────────────── */

static bool is_unit_alive(const GameState *gs, int unit_id)
{
    if (!gs || unit_id < 0 || unit_id >= MAX_UNITS) return false;
    const Unit *unit = &gs->units[unit_id];
    return unit->active && unit->state != US_DEAD &&
           unit->state != US_DYING && unit->hp > 0;
}

/* Tick a timer toward zero, clamping at 0. */
static void decay_timer(float *timer, float dt)
{
    if (*timer > 0.0f) {
        *timer -= dt;
        if (*timer < 0.0f) *timer = 0.0f;
    }
}

/* Reset all hero state to the "off" baseline. */
static void reset_hero_state(HeroPossession *hero)
{
    hero->phase          = HERO_POSSESSION_OFF;
    hero->unit_id        = -1;
    hero->transition_timer = 0.0f;
    hero->timer          = 0.0f;
    hero->attack_timer   = 0.0f;
    hero->dodge_timer    = 0.0f;
    hero->dodge_cooldown = 0.0f;
    hero->block_timer    = 0.0f;
    hero->shake          = 0.0f;
    hero->blur           = 0.0f;
    hero->impact_timer   = 0.0f;
}

/* ── Public API ────────────────────────────────────────────── */

bool hero_possession_is_unit_eligible(const GameState *gs, int unit_id, int player)
{
    if (!is_unit_alive(gs, unit_id)) return false;
    if (gs->units[unit_id].player != player) return false;

    /* Only combat-capable units can be possessed. */
    switch (gs->units[unit_id].type) {
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

bool hero_possession_can_start(const GameState *gs, int unit_id, int player)
{
    if (!gs) return false;
    if (gs->phase != PHASE_PLAYING)               return false;
    if (gs->hero.phase != HERO_POSSESSION_OFF)     return false;
    if (gs->hero.cooldown_timer > 0.0f)            return false;
    return hero_possession_is_unit_eligible(gs, unit_id, player);
}

bool hero_possession_start(GameState *gs, int unit_id, int player)
{
    if (!hero_possession_can_start(gs, unit_id, player)) return false;

    Unit *unit = &gs->units[unit_id];

    /* Initialise hero possession state. */
    gs->hero.phase           = HERO_POSSESSION_ENTERING;
    gs->hero.unit_id         = unit_id;
    gs->hero.duration        = HERO_POSSESSION_DURATION;
    gs->hero.timer           = HERO_POSSESSION_DURATION;
    gs->hero.transition_timer = 0.0f;
    gs->hero.transition_time = HERO_POSSESSION_TRANSITION_TIME;
    gs->hero.yaw             = unit->facing;
    gs->hero.pitch           = 0.0f;
    gs->hero.stamina         = 100.0f;
    gs->hero.attack_timer    = 0.0f;
    gs->hero.dodge_timer     = 0.0f;
    gs->hero.dodge_cooldown  = 0.0f;
    gs->hero.block_timer     = 0.0f;
    gs->hero.shake           = 0.35f;
    gs->hero.blur            = 0.65f;
    gs->hero.impact_timer    = 0.0f;

    /* Suppress the unit's normal AI while possessed. */
    unit->path_len     = 0;
    unit->path_idx     = 0;
    unit->target_unit  = -1;
    unit->target_bld   = -1;
    unit->state        = US_IDLE;
    unit->stance_manual = true;

    game_set_alert(gs, "Hero Possession: entering the warrior's view.");
    return true;
}

void hero_possession_request_exit(GameState *gs, const char *alert)
{
    if (!gs) return;
    if (gs->hero.phase == HERO_POSSESSION_OFF ||
        gs->hero.phase == HERO_POSSESSION_EXITING) return;

    gs->hero.phase           = HERO_POSSESSION_EXITING;
    gs->hero.transition_timer = 0.0f;
    gs->hero.cooldown_timer  = HERO_POSSESSION_COOLDOWN;
    gs->hero.shake           = 0.25f;
    gs->hero.blur            = 0.5f;

    if (alert && alert[0]) {
        game_set_alert(gs, alert);
    }
}

void hero_possession_update(GameState *gs, float dt)
{
    if (!gs) return;

    decay_timer(&gs->hero.cooldown_timer, dt);

    if (gs->hero.phase == HERO_POSSESSION_OFF) return;

    /* Decay visual effects. */
    decay_timer(&gs->hero.shake,         dt * SHAKE_DECAY_RATE);
    decay_timer(&gs->hero.blur,          dt * BLUR_DECAY_RATE);
    decay_timer(&gs->hero.impact_timer,  dt);
    decay_timer(&gs->hero.attack_timer,  dt);
    decay_timer(&gs->hero.dodge_timer,   dt);
    decay_timer(&gs->hero.dodge_cooldown, dt);
    decay_timer(&gs->hero.block_timer,   dt);

    /* Auto-exit if the possessed unit dies. */
    if (!is_unit_alive(gs, gs->hero.unit_id)) {
        hero_possession_request_exit(gs, "Hero Possession ended: the unit has fallen.");
    }

    /* State machine transitions. */
    switch (gs->hero.phase) {
        case HERO_POSSESSION_ENTERING:
            gs->hero.transition_timer += dt;
            if (gs->hero.transition_timer >= gs->hero.transition_time) {
                gs->hero.phase = HERO_POSSESSION_ACTIVE;
                gs->hero.transition_timer = 0.0f;
                game_set_alert(gs, "Hero Possession active.");
            }
            break;

        case HERO_POSSESSION_ACTIVE:
            gs->hero.timer -= dt;
            if (gs->hero.timer <= 0.0f) {
                gs->hero.timer = 0.0f;
                hero_possession_request_exit(gs,
                    "Hero Possession spent: returning to command view.");
            }
            break;

        case HERO_POSSESSION_EXITING:
            gs->hero.transition_timer += dt;
            if (gs->hero.transition_timer >= gs->hero.transition_time) {
                reset_hero_state(&gs->hero);
                game_set_alert(gs, "Strategic control restored.");
            }
            break;

        default:
            break;
    }
}

float hero_possession_time_scale(const GameState *gs)
{
    if (!gs) return 1.0f;
    if (gs->hero.phase == HERO_POSSESSION_ENTERING ||
        gs->hero.phase == HERO_POSSESSION_EXITING) {
        return TRANSITION_TIME_SCALE;
    }
    return 1.0f;
}

bool hero_possession_controls_unit(const GameState *gs, int unit_id)
{
    if (!gs || unit_id < 0) return false;
    if (gs->hero.unit_id != unit_id) return false;

    return gs->hero.phase == HERO_POSSESSION_ENTERING ||
           gs->hero.phase == HERO_POSSESSION_ACTIVE   ||
           gs->hero.phase == HERO_POSSESSION_EXITING;
}
