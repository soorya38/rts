/*=============================================================
 * renderer_hero.c - Cinematic first-person Hero Possession view
 *=============================================================*/
#include "game.h"
#include "hud_common.h"
#include "net.h"
#include "renderer.h"
#include "ui_state.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static Vector3 world3(float wx, float wy, float h) {
  return (Vector3){wx / TILE_SIZE, h, wy / TILE_SIZE};
}

static Color tile_color_3d(Tile *t) {
  switch (t->type) {
  case TILE_WATER:
    return CLITERAL(Color){36, 92, 162, 255};
  case TILE_FOREST:
    return CLITERAL(Color){34, 92, 42, 255};
  case TILE_GOLD:
    return CLITERAL(Color){132, 110, 38, 255};
  case TILE_STONE:
    return CLITERAL(Color){118, 112, 104, 255};
  case TILE_BERRIES:
    return CLITERAL(Color){88, 118, 52, 255};
  case TILE_FARM:
    return CLITERAL(Color){138, 108, 50, 255};
  case TILE_DESERT:
    return CLITERAL(Color){190, 166, 116, 255};
  case TILE_ROAD:
    return CLITERAL(Color){116, 96, 68, 255};
  default: {
    static const Color grass[4] = {
        CLITERAL(Color){66, 124, 52, 255},
        CLITERAL(Color){58, 112, 46, 255},
        CLITERAL(Color){74, 134, 56, 255},
        CLITERAL(Color){52, 104, 42, 255},
    };
    return grass[t->variant % 4];
  }
  }
}

static void draw_tile_3d(GameState *gs, int x, int y) {
  Tile *t = &gs->map[y][x];
  Color c = tile_color_3d(t);
  if (t->type == TILE_WATER) {
    float wave =
        sinf(gs->game_time * 2.0f + (float)(x + y) * 0.7f) * 0.5f + 0.5f;
    c.b = (unsigned char)clampi((int)c.b + (int)(wave * 34.0f), 0, 255);
  }

  DrawCubeV((Vector3){x + 0.5f, -0.035f, y + 0.5f},
            (Vector3){1.02f, 0.07f, 1.02f}, c);

  if (t->type == TILE_FOREST) {
    DrawCubeV((Vector3){x + 0.5f, 0.28f, y + 0.5f},
              (Vector3){0.16f, 0.56f, 0.16f}, CLITERAL(Color){82, 50, 30, 255});
    DrawSphere((Vector3){x + 0.5f, 0.82f, y + 0.5f}, 0.34f,
               CLITERAL(Color){28, 104, 36, 255});
  } else if (t->type == TILE_GOLD) {
    DrawCubeV((Vector3){x + 0.5f, 0.14f, y + 0.5f},
              (Vector3){0.44f, 0.28f, 0.44f},
              CLITERAL(Color){218, 178, 42, 255});
  } else if (t->type == TILE_STONE) {
    DrawCubeV((Vector3){x + 0.5f, 0.12f, y + 0.5f},
              (Vector3){0.46f, 0.24f, 0.46f},
              CLITERAL(Color){162, 158, 148, 255});
  } else if (t->type == TILE_BERRIES) {
    DrawSphere((Vector3){x + 0.5f, 0.24f, y + 0.5f}, 0.23f,
               CLITERAL(Color){158, 42, 52, 255});
  }
}

static float building_height_3d(BldType type) {
  switch (type) {
  case BLD_TOWN_CENTER:
    return 2.2f;
  case BLD_WATCH_TOWER:
    return 3.0f;
  case BLD_MONASTERY:
    return 2.3f;
  case BLD_MARKET:
    return 1.25f;
  case BLD_HOUSE:
    return 1.05f;
  case BLD_WALL:
  case BLD_GATE:
    return 0.85f;
  default:
    return 1.55f;
  }
}

static bool draw_hero_house_model(UIState *ui, Building *b, Vector3 pos,
                                  Color tint) {
  Model mdl = ui_get_hero_house_model(ui);
  if (mdl.meshCount == 0)
    return false;

  const float model_min_y = -0.279456f;
  float footprint = fminf((float)b->tw, (float)b->th);
  float scale = footprint * 1.45f;
  DrawModelEx(mdl, (Vector3){pos.x, -model_min_y * scale, pos.z},
              (Vector3){0.0f, 1.0f, 0.0f}, 90.0f,
              (Vector3){scale, scale, scale}, tint);
  return true;
}

static bool draw_hero_town_center_model(UIState *ui, Building *b, Vector3 pos,
                                        Color tint) {
  Model mdl = ui_get_hero_town_center_model(ui);
  if (mdl.meshCount == 0)
    return false;

  const float model_min_y = -0.378787f;
  float footprint = fminf((float)b->tw, (float)b->th);
  float scale = footprint * 1.05f;
  DrawModelEx(mdl, (Vector3){pos.x, -model_min_y * scale, pos.z},
              (Vector3){0.0f, 1.0f, 0.0f}, 0.0f, (Vector3){scale, scale, scale},
              tint);
  return true;
}

static bool draw_hero_stable_model(UIState *ui, Building *b, Vector3 pos,
                                   Color tint) {
  Model mdl = ui_get_hero_stable_model(ui);
  if (mdl.meshCount == 0)
    return false;

  const float model_min_y = -0.427576f;
  float footprint = fminf((float)b->tw, (float)b->th);
  float scale = footprint * 0.9f;
  DrawModelEx(mdl, (Vector3){pos.x, -model_min_y * scale, pos.z},
              (Vector3){0.0f, 1.0f, 0.0f}, 0.0f, (Vector3){scale, scale, scale},
              tint);
  return true;
}

static bool draw_hero_watch_tower_model(UIState *ui, Building *b, Vector3 pos,
                                        Color tint) {
  Model mdl = ui_get_hero_watch_tower_model(ui);
  if (mdl.meshCount == 0)
    return false;

  const float model_min_y = -0.5f;
  float footprint = fminf((float)b->tw, (float)b->th);
  float scale = footprint * 1.15f;
  DrawModelEx(mdl, (Vector3){pos.x, -model_min_y * scale, pos.z},
              (Vector3){0.0f, 1.0f, 0.0f}, 0.0f, (Vector3){scale, scale, scale},
              tint);
  return true;
}

static bool draw_hero_castle_model(UIState *ui, Building *b, Vector3 pos,
                                   Color tint) {
  Model mdl = ui_get_hero_castle_model(ui);
  if (mdl.meshCount == 0)
    return false;

  const float model_min_y = -0.271237f;
  float footprint = fminf((float)b->tw, (float)b->th);
  float scale = footprint * 1.15f;
  DrawModelEx(mdl, (Vector3){pos.x, -model_min_y * scale, pos.z},
              (Vector3){0.0f, 1.0f, 0.0f}, 0.0f, (Vector3){scale, scale, scale},
              tint);
  return true;
}

static void draw_building_3d(GameState *gs, UIState *ui, Building *b) {
  float h = building_height_3d(b->type);
  Color c = player_color(b->player);
  Color dc = player_color_dark(b->player);
  if (!b->complete) {
    c.a = 150;
    h *= clampf(b->construction, 0.15f, 1.0f);
  }

  Vector3 pos = {b->tx + b->tw * 0.5f, h * 0.5f, b->ty + b->th * 0.5f};
  Vector3 size = {(float)b->tw * 0.92f, h, (float)b->th * 0.92f};

  int age = 0;
  if (b->player >= 0 && b->player < NUM_PLAYERS) {
    age = gs->res[b->player].age;
  }

  Model mdl = ui_get_building_model(ui, b->type, age, b->variant);
  Color tint = WHITE;
  if (!b->complete)
    tint.a = 150;

  if (b->type == BLD_HOUSE && draw_hero_house_model(ui, b, pos, tint)) {
    return;
  }

  if (b->type == BLD_TOWN_CENTER &&
      draw_hero_town_center_model(ui, b, pos, tint)) {
    return;
  }

  if (b->type == BLD_STABLE && draw_hero_stable_model(ui, b, pos, tint)) {
    return;
  }

  if (b->type == BLD_WATCH_TOWER &&
      draw_hero_watch_tower_model(ui, b, pos, tint)) {
    return;
  }

  if (b->type == BLD_CASTLE && draw_hero_castle_model(ui, b, pos, tint)) {
    return;
  }

  if (mdl.meshCount > 0) {
    Texture2D tex = ui_get_building_texture(ui, b->type, age);
    if (b->type == BLD_HOUSE)
      tex = ui_get_house_texture(ui, b->variant);

    float aspect = 1.0f;
    if (tex.id != 0 && tex.height > 0) {
      aspect = (float)tex.width / (float)tex.height;
    }

    // Preserve perfect aspect ratio of the 2D sprite
    float scale_y = h * 2.0f;
    float scale_x = scale_y * aspect;
    float scale_z = scale_x;

    // Calculate angle to camera for billboarding
    float dx = ui->hero_camera.position.x - pos.x;
    float dz = ui->hero_camera.position.z - pos.z;
    float angle = atan2f(dx, dz) * RAD2DEG;

    // Face the model directly towards the camera
    DrawModelEx(mdl, (Vector3){pos.x, 0.0f, pos.z}, (Vector3){0, 1, 0}, angle,
                (Vector3){scale_x, scale_y, scale_z}, tint);
  } else {
    DrawCubeV(pos, size, c);
    DrawCubeWiresV(pos, size, dc);

    if (b->type == BLD_WATCH_TOWER) {
      DrawCubeV((Vector3){pos.x, h + 0.18f, pos.z},
                (Vector3){0.82f, 0.36f, 0.82f}, dc);
    }
  }
}

static void draw_unit_3d(Unit *u, bool possessed) {
  if (u->state == US_DEAD || possessed)
    return;

  Color c = player_color(u->player);
  Color dc = player_color_dark(u->player);
  float scale =
      (u->type == UNIT_KNIGHT || u->type == UNIT_SCOUT) ? 1.25f : 1.0f;
  if (u->type == UNIT_BATTERING_RAM || u->type == UNIT_MANGONEL ||
      u->type == UNIT_SCORPION || u->type == UNIT_BOMBARD_CANNON)
    scale = 1.6f;

  Vector3 feet = world3(u->wx, u->wy, 0.0f);
  DrawCubeV((Vector3){feet.x, 0.38f * scale, feet.z},
            (Vector3){0.28f * scale, 0.62f * scale, 0.24f * scale}, c);
  DrawSphere((Vector3){feet.x, 0.82f * scale, feet.z}, 0.14f * scale,
             CLITERAL(Color){220, 184, 142, 255});

  float fx = cosf(u->facing);
  float fy = sinf(u->facing);
  DrawCubeV((Vector3){feet.x + fx * 0.18f, 0.55f * scale, feet.z + fy * 0.18f},
            (Vector3){0.08f, 0.08f, 0.38f * scale}, dc);

  if (u->hp < u->max_hp) {
    float frac = clampf((float)u->hp / (float)u->max_hp, 0.0f, 1.0f);
    DrawCubeV((Vector3){feet.x, 1.12f * scale, feet.z},
              (Vector3){0.42f * frac, 0.035f, 0.035f},
              frac > 0.45f ? C_HP_GREEN : CLITERAL(Color){210, 50, 40, 255});
  }
}

static void draw_projectiles_3d(GameState *gs) {
  for (int i = 0; i < MAX_PROJECTILES; i++) {
    Projectile *p = &gs->projectiles[i];
    if (!p->active || p->duration <= 0.0f)
      continue;
    float t = clampf(p->elapsed / p->duration, 0.0f, 1.0f);
    float wx = lerpf(p->sx, p->ex, t);
    float wy = lerpf(p->sy, p->ey, t);
    float arc = 4.0f * t * (1.0f - t) * p->arc_height / TILE_SIZE;

    if (p->type == PROJ_ARROW) {
      // Draw a thick cylinder for the arrow to ensure visibility
      float tail_t = t - 0.15f;
      float pwx = p->sx + (p->ex - p->sx) * tail_t;
      float pwy = p->sy + (p->ey - p->sy) * tail_t;
      float parc = 4.0f * tail_t * (1.0f - tail_t) * p->arc_height / TILE_SIZE;

      Vector3 startP = world3(pwx, pwy, 0.95f + parc);
      Vector3 endP = world3(wx, wy, 0.95f + arc);

      // Bright colored wooden shaft
      DrawCylinderEx(startP, endP, 0.05f, 0.05f, 6,
                     CLITERAL(Color){255, 200, 120, 255});
    } else {
      Color c = p->type == PROJ_STONE ? CLITERAL(Color){180, 170, 150, 255}
                                      : CLITERAL(Color){230, 210, 120, 255};
      DrawSphere(world3(wx, wy, 0.95f + arc),
                 p->type == PROJ_STONE ? 0.16f : 0.06f, c);
    }
  }
}

static void draw_first_person_weapon(GameState *gs, Unit *u) {
  int sw = GetScreenWidth();
  int sh = GetScreenHeight();
  float swing =
      gs->hero.attack_timer > 0.0f ? sinf(gs->hero.attack_timer * 16.0f) : 0.0f;
  float dodge = gs->hero.dodge_timer > 0.0f ? 1.0f : 0.0f;
  Color steel = CLITERAL(Color){205, 212, 218, 235};
  Color leather = CLITERAL(Color){92, 54, 30, 235};
  Color wood = CLITERAL(Color){120, 74, 34, 240};

  if (unit_uses_projectiles(u->type)) {
    int bx = sw - (int)(210 * hud_scale());
    int by = sh - (int)(230 * hud_scale());
    DrawLineEx((Vector2){(float)bx, (float)(by + 150)},
               (Vector2){(float)(bx + 58), (float)(by + 30 + swing * 10.0f)},
               7.0f, wood);
    DrawLineEx((Vector2){(float)(bx + 58), (float)(by + 30 + swing * 10.0f)},
               (Vector2){(float)(bx + 92), (float)(by + 150)}, 4.0f,
               CLITERAL(Color){230, 220, 180, 230});
    DrawLineEx((Vector2){(float)(bx + 56), (float)(by + 34)},
               (Vector2){(float)(bx + 122), (float)(by + 98)}, 3.0f, steel);
  } else {
    Rectangle blade = {sw - 150.0f + swing * 36.0f, sh - 260.0f + dodge * 22.0f,
                       22.0f, 220.0f};
    DrawRectanglePro(blade, (Vector2){11.0f, 210.0f}, -18.0f + swing * 22.0f,
                     steel);
    DrawRectanglePro((Rectangle){sw - 110.0f, sh - 78.0f, 70.0f, 18.0f},
                     (Vector2){35.0f, 9.0f}, -18.0f, leather);
  }

  if (gs->hero.block_timer > 0.0f) {
    DrawCircleGradient((int)(sw * 0.18f), (int)(sh * 0.72f), 90.0f,
                       CLITERAL(Color){120, 90, 54, 155},
                       CLITERAL(Color){32, 22, 12, 20});
  }
}

static void draw_archer_crosshair(GameState *gs, Unit *u) {
  if (!unit_uses_projectiles(u->type))
    return;

  int sw = GetScreenWidth();
  int sh = GetScreenHeight();
  float sc = hud_scale();
  float cx = sw * 0.5f;
  float cy = sh * 0.5f;

  bool ready = gs->hero.attack_timer <= 0.0f;
  float cooldown =
      clampf(gs->hero.attack_timer / clampf(u->attack_cd * 0.55f, 0.35f, 1.1f),
             0.0f, 1.0f);
  float spread = (ready ? 13.0f : 20.0f + cooldown * 10.0f) * sc;
  float tick = 9.0f * sc;
  float gap = 5.0f * sc;
  Color main = ready ? CLITERAL(Color){238, 226, 172, 235}
                     : CLITERAL(Color){170, 150, 110, 175};
  Color shadow = CLITERAL(Color){0, 0, 0, 150};

  DrawCircleLines((int)cx, (int)cy, spread * 0.72f, shadow);
  DrawCircleLines((int)cx, (int)cy, spread * 0.72f, main);
  DrawCircleV((Vector2){cx, cy}, ready ? 2.2f * sc : 1.6f * sc, main);

  DrawLineEx((Vector2){cx - spread - tick, cy},
             (Vector2){cx - spread - gap, cy}, 3.5f * sc, shadow);
  DrawLineEx((Vector2){cx + spread + gap, cy},
             (Vector2){cx + spread + tick, cy}, 3.5f * sc, shadow);
  DrawLineEx((Vector2){cx, cy - spread - tick},
             (Vector2){cx, cy - spread - gap}, 3.5f * sc, shadow);
  DrawLineEx((Vector2){cx, cy + spread + gap},
             (Vector2){cx, cy + spread + tick}, 3.5f * sc, shadow);

  DrawLineEx((Vector2){cx - spread - tick, cy},
             (Vector2){cx - spread - gap, cy}, 1.5f * sc, main);
  DrawLineEx((Vector2){cx + spread + gap, cy},
             (Vector2){cx + spread + tick, cy}, 1.5f * sc, main);
  DrawLineEx((Vector2){cx, cy - spread - tick},
             (Vector2){cx, cy - spread - gap}, 1.5f * sc, main);
  DrawLineEx((Vector2){cx, cy + spread + gap},
             (Vector2){cx, cy + spread + tick}, 1.5f * sc, main);

  if (!ready) {
    float arc = 360.0f * (1.0f - cooldown);
    DrawRing((Vector2){cx, cy}, spread + 5.0f * sc, spread + 7.0f * sc, -90.0f,
             -90.0f + arc, 28, CLITERAL(Color){82, 190, 220, 210});
  }
}

static void draw_hero_overlay(GameState *gs, Unit *u) {
  int sw = GetScreenWidth();
  int sh = GetScreenHeight();
  float sc = hud_scale();

  float transition = 1.0f;
  if (gs->hero.transition_time > 0.0f &&
      (gs->hero.phase == HERO_POSSESSION_ENTERING ||
       gs->hero.phase == HERO_POSSESSION_EXITING)) {
    transition = clampf(gs->hero.transition_timer / gs->hero.transition_time,
                        0.0f, 1.0f);
    if (gs->hero.phase == HERO_POSSESSION_EXITING)
      transition = 1.0f - transition;
  }

  int bar_h = (int)(42.0f * sc);
  DrawRectangle(0, 0, sw, bar_h, CLITERAL(Color){0, 0, 0, 215});
  DrawRectangle(0, sh - bar_h, sw, bar_h, CLITERAL(Color){0, 0, 0, 215});

  unsigned char blur_a =
      (unsigned char)clampi((int)(gs->hero.blur * 120.0f), 0, 150);
  if (blur_a > 0) {
    DrawRectangle(0, 0, (int)(sw * 0.18f), sh,
                  CLITERAL(Color){210, 220, 255, blur_a / 2});
    DrawRectangle(sw - (int)(sw * 0.18f), 0, (int)(sw * 0.18f), sh,
                  CLITERAL(Color){210, 220, 255, blur_a / 2});
    DrawRectangle(0, 0, sw, sh, CLITERAL(Color){255, 245, 220, blur_a / 5});
  }

  if (gs->hero.impact_timer > 0.0f) {
    unsigned char a =
        (unsigned char)(clampf(gs->hero.impact_timer / 0.35f, 0.0f, 1.0f) *
                        160.0f);
    DrawRectangle(0, 0, sw, sh, CLITERAL(Color){200, 15, 10, a});
  }

  unsigned char fade_a = (unsigned char)((1.0f - transition) * 215.0f);
  if (fade_a > 0)
    DrawRectangle(0, 0, sw, sh, CLITERAL(Color){0, 0, 0, fade_a});

  char title[96];
  snprintf(title, sizeof(title), "HERO POSSESSION  %s", unit_name(u->type));
  DrawText(title, (int)(18 * sc), (int)(12 * sc), (int)(16 * sc),
           CLITERAL(Color){238, 220, 168, 255});

  char timer[64];
  snprintf(timer, sizeof(timer), "Time %.0fs", gs->hero.timer);
  int timer_fs = (int)(16 * sc);
  DrawText(timer, sw - MeasureText(timer, timer_fs) - (int)(18 * sc),
           (int)(12 * sc), timer_fs, CLITERAL(Color){238, 220, 168, 255});

  int meter_w = (int)(220 * sc);
  int meter_h = (int)(8 * sc);
  int meter_x = (int)(18 * sc);
  int meter_y = sh - bar_h + (int)(17 * sc);
  DrawRectangle(meter_x, meter_y, meter_w, meter_h,
                CLITERAL(Color){28, 22, 12, 255});
  DrawRectangle(meter_x, meter_y,
                (int)(meter_w * clampf(gs->hero.stamina / 100.0f, 0.0f, 1.0f)),
                meter_h, CLITERAL(Color){82, 190, 220, 255});
  DrawRectangleLinesEx((Rectangle){(float)meter_x, (float)meter_y,
                                   (float)meter_w, (float)meter_h},
                       1.0f, CLITERAL(Color){160, 142, 92, 220});

  char hp[64];
  snprintf(hp, sizeof(hp), "HP %d/%d", u->hp, u->max_hp);
  DrawText(hp, meter_x + meter_w + (int)(16 * sc), sh - bar_h + (int)(10 * sc),
           (int)(13 * sc), CLITERAL(Color){220, 205, 168, 235});

  draw_archer_crosshair(gs, u);
  draw_first_person_weapon(gs, u);
}

void renderer_draw_hero_possession(GameState *gs, UIState *ui) {
  if (!gs || !ui || gs->hero.phase == HERO_POSSESSION_OFF)
    return;
  if (gs->hero.unit_id < 0 || gs->hero.unit_id >= MAX_UNITS)
    return;

  Unit *hero = &gs->units[gs->hero.unit_id];
  if (!hero->active)
    return;

  float yaw = gs->hero.yaw;
  float pitch = gs->hero.pitch;
  float shake = gs->hero.shake;

  float sx = sinf(gs->game_time * 58.0f) * shake * 0.25f;
  float sy = sinf(gs->game_time * 43.0f + 1.8f) * shake * 0.22f;
  float sz = cosf(gs->game_time * 51.0f) * shake * 0.25f;

  float pitch_shake = sinf(gs->game_time * 75.0f) * shake * 0.15f;
  float yaw_shake = cosf(gs->game_time * 65.0f) * shake * 0.15f;

  float fx = cosf(yaw + yaw_shake);
  float fz = sinf(yaw + yaw_shake);

  Vector3 eye = world3(hero->wx, hero->wy, 0.92f + sy);
  eye.x += sx;
  eye.z += sz;
  ui->hero_camera.position =
      (Vector3){eye.x - fx * 0.08f, eye.y, eye.z - fz * 0.08f};
  ui->hero_camera.target =
      (Vector3){eye.x + fx, eye.y + sinf(pitch + pitch_shake), eye.z + fz};
  ui->hero_camera.up = (Vector3){0.0f, 1.0f, 0.0f};
  ui->hero_camera.fovy = 72.0f + gs->hero.blur * 5.0f;
  ui->hero_camera.projection = CAMERA_PERSPECTIVE;

  int lp = net_get_local_player();
  int hx = (int)(hero->wx / TILE_SIZE);
  int hy = (int)(hero->wy / TILE_SIZE);
  int radius = 22;

  BeginMode3D(ui->hero_camera);
  DrawPlane((Vector3){MAP_W * 0.5f, -0.08f, MAP_H * 0.5f},
            (Vector2){MAP_W, MAP_H}, CLITERAL(Color){38, 72, 38, 255});

  int x0 = clampi(hx - radius, 0, MAP_W - 1);
  int x1 = clampi(hx + radius, 0, MAP_W - 1);
  int y0 = clampi(hy - radius, 0, MAP_H - 1);
  int y1 = clampi(hy + radius, 0, MAP_H - 1);
  for (int y = y0; y <= y1; y++) {
    for (int x = x0; x <= x1; x++) {
      if (gs->map[y][x].fog[lp] == FOG_HIDDEN)
        continue;
      draw_tile_3d(gs, x, y);
    }
  }

  for (int i = 0; i < MAX_BUILDINGS; i++) {
    Building *b = &gs->buildings[i];
    if (!b->active)
      continue;
    float cx = b->tx + b->tw * 0.5f;
    float cy = b->ty + b->th * 0.5f;
    if (fabsf(cx - hx) > radius || fabsf(cy - hy) > radius)
      continue;
    int fog_x = clampi((int)cx, 0, MAP_W - 1);
    int fog_y = clampi((int)cy, 0, MAP_H - 1);
    if (gs->map[fog_y][fog_x].fog[lp] == FOG_HIDDEN && b->player != lp)
      continue;
    draw_building_3d(gs, ui, b);
  }

  for (int i = 0; i < MAX_UNITS; i++) {
    Unit *u = &gs->units[i];
    if (!u->active || u->state == US_DEAD)
      continue;
    int ux = (int)(u->wx / TILE_SIZE);
    int uy = (int)(u->wy / TILE_SIZE);
    if (abs(ux - hx) > radius || abs(uy - hy) > radius)
      continue;
    if (gs->map[clampi(uy, 0, MAP_H - 1)][clampi(ux, 0, MAP_W - 1)].fog[lp] ==
            FOG_HIDDEN &&
        u->player != lp)
      continue;
    draw_unit_3d(u, i == gs->hero.unit_id);
  }

  draw_projectiles_3d(gs);
  EndMode3D();

  draw_hero_overlay(gs, hero);
}
