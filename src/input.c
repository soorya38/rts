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
#include "ui_state.h"
#include <math.h>
#include <stdio.h>

#define MINI_SIZE 180
#define CAM_SPEED 280.0f
#define CAM_EDGE 12
#define ZOOM_SPEED 0.12f
#define ZOOM_MIN 0.35f
#define ZOOM_MAX 2.8f

/* ─── Camera ─────────────────────────────────────────────── */
static void update_camera(GameState *gs, UIState *ui, float dt) {
  Camera2D *cam = &ui->camera;
  Vector2 mp = GetMousePosition();
  float speed = CAM_SPEED / cam->zoom;

  if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP))
    cam->target.y -= speed * dt;
  if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN))
    cam->target.y += speed * dt;
  if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT))
    cam->target.x -= speed * dt;
  if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT))
    cam->target.x += speed * dt;

  /* Edge scrolling (only in normal mode) */
  if (!gs->build_mode.active && !ui->build_panel_open) {
    if (mp.x < CAM_EDGE)
      cam->target.x -= speed * dt;
    if (mp.x > SCREEN_W - CAM_EDGE)
      cam->target.x += speed * dt;
    if (mp.y < CAM_EDGE)
      cam->target.y -= speed * dt;
    if (mp.y > SCREEN_H - CAM_EDGE)
      cam->target.y += speed * dt;
  }

  float wheel = GetMouseWheelMove();
  if (wheel != 0.0f) {
    Vector2 mouseWorldBeforeZoom = GetScreenToWorld2D(mp, *cam);
    cam->zoom += wheel * ZOOM_SPEED * cam->zoom;
    cam->zoom = clampf(cam->zoom, ZOOM_MIN, ZOOM_MAX);
    Vector2 mouseWorldAfterZoom = GetScreenToWorld2D(mp, *cam);
    cam->target.x += (mouseWorldBeforeZoom.x - mouseWorldAfterZoom.x);
    cam->target.y += (mouseWorldBeforeZoom.y - mouseWorldAfterZoom.y);
  }

  /* Smooth sliding bounds (prevents snapping/fighting zoom) */
  float hw = (SCREEN_W * 0.5f) / cam->zoom, hh = (SCREEN_H * 0.5f) / cam->zoom;

  float min_cam_x = -(float)(MAP_H * TILE_SIZE);
  float max_cam_x = (float)(MAP_W * TILE_SIZE);
  float min_cam_y = -30.0f; // Bit of padding for the top
  float max_cam_y = (float)(MAP_W + MAP_H) * TILE_SIZE * 0.5f + 100.0f;

  float min_x = min_cam_x + hw;
  float max_x = max_cam_x - hw;
  if (max_x < min_x) {
    float mid = (min_x + max_x) * 0.5f;
    min_x = max_x = mid;
  }
  cam->target.x = clampf(cam->target.x, min_x, max_x);

  float min_y = min_cam_y + hh;
  float max_y = max_cam_y - hh;
  if (max_y < min_y) {
    float mid = (min_y + max_y) * 0.5f;
    min_y = max_y = mid;
  }
  cam->target.y = clampf(cam->target.y, min_y, max_y);
}

/* ─── Selection helpers ───────────────────────────────────── */
static bool point_in_unit(Unit *u, Vector2 wp) {
  Vector2 p = to_rvec2(world_to_iso(u->wx, u->wy));
  return fabsf(p.x - wp.x) < 15 && fabsf((p.y - 10) - wp.y) < 15;
}
static bool rect_intersects_unit(Unit *u, float x0, float y0, float x1,
                                 float y1) {
  Vector2 p = to_rvec2(world_to_iso(u->wx, u->wy));
  return p.x >= x0 && p.x <= x1 && p.y >= y0 && p.y <= y1;
}
static void clear_selection(GameState *gs, UIState *ui) {
  for (int i = 0; i < ui->sel_count; i++)
    gs->units[ui->sel_units[i]].selected = false;
  ui->sel_count = 0;
  if (ui->sel_building >= 0) {
    gs->buildings[ui->sel_building].selected = false;
    ui->sel_building = -1;
  }
}
static void select_unit(GameState *gs, UIState *ui, int uid) {
  if (ui->sel_count >= MAX_UNITS)
    return;
  gs->units[uid].selected = true;
  ui->sel_units[ui->sel_count++] = uid;
}

/* ─── World-hit testers ───────────────────────────────────── */
static bool hit_building_iso(Building *b, Vector2 wp) {
  float bx = (float)b->tx * TILE_SIZE, by = (float)b->ty * TILE_SIZE;
  float bw = (float)b->tw * TILE_SIZE, bh = (float)b->th * TILE_SIZE;

  Vec2 c = iso_to_world(wp.x, wp.y);
  if (c.x >= bx && c.x <= bx + bw && c.y >= by && c.y <= by + bh) {
    return true;
  }

  Vector2 p = to_rvec2(world_to_iso(bx + bw * 0.5f, by + bh * 0.5f));
  float hit_w = (bw + bh) * 0.7f;
  float hit_h = (bw + bh) * 0.5f + 30; // Vertical extrusion
  return (wp.x >= p.x - hit_w / 2 && wp.x <= p.x + hit_w / 2 &&
          wp.y >= p.y - hit_h && wp.y <= p.y + hit_h / 4);
}

static int find_friendly_unit_at(GameState *gs, Vector2 wp) {
  for (int i = 0; i < MAX_UNITS; i++) {
    Unit *u = &gs->units[i];
    if (!u->active || u->player != 0 || u->state == US_DEAD)
      continue;
    if (point_in_unit(u, wp))
      return i;
  }
  return -1;
}
static int find_friendly_building_at(GameState *gs, Vector2 wp) {
  for (int i = 0; i < MAX_BUILDINGS; i++) {
    Building *b = &gs->buildings[i];
    if (!b->active || b->player != 0)
      continue;
    if (hit_building_iso(b, wp))
      return i;
  }
  return -1;
}
static int find_enemy_unit_at(GameState *gs, Vector2 wp) {
  for (int i = 0; i < MAX_UNITS; i++) {
    Unit *u = &gs->units[i];
    if (!u->active || u->player != 1 || u->state == US_DEAD)
      continue;
    /* Must be visible */
    int utx = (int)(u->wx / TILE_SIZE), uty = (int)(u->wy / TILE_SIZE);
    if (!map_in_bounds(utx, uty))
      continue;
    if (gs->map[uty][utx].fog[0] != FOG_VISIBLE)
      continue;
    if (point_in_unit(u, wp))
      return i;
  }
  return -1;
}
static int find_enemy_building_at(GameState *gs, Vector2 wp) {
  for (int i = 0; i < MAX_BUILDINGS; i++) {
    Building *b = &gs->buildings[i];
    if (!b->active || b->player != 1)
      continue;
    int bmx = clampi(b->tx, 0, MAP_W - 1), bmy = clampi(b->ty, 0, MAP_H - 1);
    if (gs->map[bmy][bmx].fog[0] == FOG_HIDDEN)
      continue;
    if (hit_building_iso(b, wp))
      return i;
  }
  return -1;
}
static int find_unfinished_building_at(GameState *gs, Vector2 wp) {
  for (int i = 0; i < MAX_BUILDINGS; i++) {
    Building *b = &gs->buildings[i];
    if (!b->active || b->player != 0 || b->complete)
      continue;
    if (hit_building_iso(b, wp))
      return i;
  }
  return -1;
}
static int find_friendly_dropoff_at(GameState *gs, Vector2 wp) {
  int fb = find_friendly_building_at(gs, wp);
  if (fb >= 0 && gs->buildings[fb].complete) {
    int t = gs->buildings[fb].type;
    if (t == BLD_TOWN_CENTER || t == BLD_MILL || t == BLD_LUMBER_CAMP ||
        t == BLD_MINING_CAMP) {
      return fb;
    }
  }
  return -1;
}

/* ─── Issue context command to all selected units ─────────── */
static void issue_command_at(GameState *gs, UIState *ui, Vector2 world) {
  Vector2 cart = to_rvec2(iso_to_world(world.x, world.y));
  int tx = (int)(cart.x / TILE_SIZE), ty = (int)(cart.y / TILE_SIZE);
  if (!map_in_bounds(tx, ty))
    return;

  int eu = find_enemy_unit_at(gs, world);
  int eb = (eu < 0) ? find_enemy_building_at(gs, world) : -1;
  int ub = find_unfinished_building_at(gs, world);
  int dropoff = find_friendly_dropoff_at(gs, world);

  TileType tt = gs->map[ty][tx].type;
  bool is_resource =
      (tt == TILE_FOREST || tt == TILE_GOLD || tt == TILE_STONE ||
       tt == TILE_BERRIES || tt == TILE_FARM);

  int ftx = tx, fty = ty;
  bool must_be_passable =
      (!is_resource && eu < 0 && eb < 0 && ub < 0 && dropoff < 0);
  if (must_be_passable) {
    if (!map_find_passable_near(gs, tx, ty, &ftx, &fty))
      return;
  }

  /* Formation constants */
  int width = ui->sel_count < 5 ? ui->sel_count : 5;
  if (width < 1)
    width = 1;
  int height = (ui->sel_count + width - 1) / width;

  for (int i = 0; i < ui->sel_count; i++) {
    Unit *u = &gs->units[ui->sel_units[i]];
    if (!u->active || u->player != 0)
      continue;

    if (eu >= 0 || eb >= 0) {
      unit_give_attack_order(gs, u, eu, eb);
    } else if (ub >= 0 && u->type == UNIT_VILLAGER) {
      unit_give_build_order(gs, u, ub);
    } else if (dropoff >= 0 && u->type == UNIT_VILLAGER && u->carry_amt > 0) {
      unit_give_dropoff_order(gs, u, gs->buildings[dropoff].tx,
                              gs->buildings[dropoff].ty);
    } else if (is_resource && u->type == UNIT_VILLAGER) {
      unit_give_gather_order(gs, u, tx, ty);
    } else {
      /* Spread into a loose formation centered on target */
      int col = i % width;
      int row = i / width;
      int ox = col - (width / 2);
      int oy = row - (height / 2);
      int ntx = clampi(ftx + ox, 0, MAP_W - 1);
      int nty = clampi(fty + oy, 0, MAP_H - 1);
      unit_give_move_order(gs, u, ntx, nty);
    }
  }
}

/* ─── Build ghost placement ───────────────────────────────── */
static void update_build_mode(GameState *gs, UIState *ui) {
  if (!gs->build_mode.active)
    return;
  Vector2 mp = GetMousePosition();
  if (IsKeyPressed(KEY_ESCAPE)) {
    gs->build_mode.active = false;
    return;
  }
  if (mp.y < 42 || mp.y > SCREEN_H - 130)
    return;

  Vector2 wp = GetScreenToWorld2D(mp, ui->camera);
  Vector2 cart = to_rvec2(iso_to_world(wp.x, wp.y));
  int tx = (int)(cart.x / TILE_SIZE), ty = (int)(cart.y / TILE_SIZE);
  int tw = building_tw(gs->build_mode.type),
      th = building_th(gs->build_mode.type);
  tx -= tw / 2;
  ty -= th / 2;
  gs->build_mode.ghost_tx = tx;
  gs->build_mode.ghost_ty = ty;
  gs->build_mode.valid =
      map_is_buildable(gs, tx, ty, tw, th) &&
      res_can_afford(&gs->res[0], building_cost(gs->build_mode.type));

  if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && gs->build_mode.valid) {
    int bid = building_place(gs, 0, gs->build_mode.type, tx, ty);
    if (bid >= 0) {
      /* Assign SELECTED villager(s) first – that's who the player picked */
      bool any = false;
      for (int i = 0; i < ui->sel_count; i++) {
        Unit *u = &gs->units[ui->sel_units[i]];
        if (u->active && u->player == 0 && u->type == UNIT_VILLAGER) {
          unit_give_build_order(gs, u, bid);
          any = true;
        }
      }
      /* No villager was selected → fall back to nearest idle one */
      if (!any) {
        int vid = unit_find_idle_villager(gs, 0);
        if (vid >= 0)
          unit_give_build_order(gs, &gs->units[vid], bid);
      }
      if (!IsKeyDown(KEY_LEFT_SHIFT))
        gs->build_mode.active = false;
    }
  }
  /* ESC/right-click cancel (on platforms that have it) */
  if (IsMouseButtonPressed(MOUSE_RIGHT_BUTTON))
    gs->build_mode.active = false;
}

/* ─── Hotkeys ─────────────────────────────────────────────── */
static void update_hotkeys(GameState *gs, UIState *ui) {
  /* ESC: cancel build ghost → close picker → deselect */
  if (IsKeyPressed(KEY_ESCAPE)) {
    if (gs->build_mode.active) {
      gs->build_mode.active = false;
      return;
    }
    if (ui->build_panel_open) {
      ui->build_panel_open = false;
      return;
    }
    clear_selection(gs, ui);
  }
  /* B: toggle build picker (need villager) */
  if (IsKeyPressed(KEY_B)) {
    bool vil = false;
    for (int i = 0; i < ui->sel_count; i++)
      if (gs->units[ui->sel_units[i]].type == UNIT_VILLAGER) {
        vil = true;
        break;
      }
    if (vil) {
      bool any = ui->build_panel_open || gs->build_mode.active;
      ui->build_panel_open = any ? false : true;
      gs->build_mode.active = false;
    }
  }
  /* Quick-build hotkeys → straight to ghost placement */
  bool vil = false;
  for (int i = 0; i < ui->sel_count; i++)
    if (gs->units[ui->sel_units[i]].type == UNIT_VILLAGER) {
      vil = true;
      break;
    }
  if (vil) {
    BldType qt = BLD_COUNT;
    if (IsKeyPressed(KEY_H))
      qt = BLD_HOUSE;
    if (IsKeyPressed(KEY_R))
      qt = BLD_BARRACKS;
    if (IsKeyPressed(KEY_A))
      qt = BLD_ARCHERY_RANGE;
    if (IsKeyPressed(KEY_M))
      qt = BLD_MILL;
    if (IsKeyPressed(KEY_F))
      qt = BLD_FARM;
    if (qt != BLD_COUNT && res_can_afford(&gs->res[0], building_cost(qt))) {
      gs->build_mode.type = qt;
      gs->build_mode.active = true;
      ui->build_panel_open = false;
      static const char *BN[BLD_COUNT] = {
          "Town Center",   "House",       "Barracks",
          "Archery Range", "Stable",      "Mill",
          "Lumber Camp",   "Mining Camp", "Farm"};
      char msg[48];
      snprintf(msg, sizeof(msg), "Placing: %s", BN[qt]);
      game_set_alert(gs, msg);
    }
  }
  if (IsKeyPressed(KEY_P) && gs->phase == PHASE_PLAYING)
    gs->phase = PHASE_PAUSED;
  else if (IsKeyPressed(KEY_P) && gs->phase == PHASE_PAUSED)
    gs->phase = PHASE_PLAYING;
  if (IsKeyPressed(KEY_DELETE) && ui->sel_building >= 0) {
    Building *b = &gs->buildings[ui->sel_building];
    if (b->player == 0) {
      map_clear_building(gs, b->tx, b->ty, b->tw, b->th);
      b->active = false;
      ui->sel_building = -1;
    }
  }
  if (IsKeyPressed(KEY_S) && IsKeyDown(KEY_LEFT_SHIFT)) {
    for (int i = 0; i < ui->sel_count; i++) {
      Unit *u = &gs->units[ui->sel_units[i]];
      u->state = US_IDLE;
      u->path_len = 0;
    }
  }
}

/* ─── Left-click start (box-select anchor) ────────────────── */
static void handle_left_down(GameState *gs, UIState *ui) {
  Vector2 mp = GetMousePosition();
  bool over_hud =
      mp.y < 42 || mp.y > SCREEN_H - 130 ||
      (mp.x > SCREEN_W - MINI_SIZE - 16 && mp.y > SCREEN_H - 130 - 8);
  if (over_hud)
    return;
  if (gs->build_mode.active || ui->build_panel_open)
    return;
  ui->box_selecting = true;
  ui->box_start = mp;
}

/* ─── Left-click release (main logic) ────────────────────── */
static void handle_left_up(GameState *gs, UIState *ui) {
  if (!ui->box_selecting)
    return;
  ui->box_selecting = false;

  Vector2 mp = GetMousePosition();
  Vector2 ws = GetScreenToWorld2D(ui->box_start, ui->camera);
  Vector2 we = GetScreenToWorld2D(mp, ui->camera);

  /* HUD guard on release too */
  bool over_hud =
      mp.y < 42 || mp.y > SCREEN_H - 130 ||
      (mp.x > SCREEN_W - MINI_SIZE - 16 && mp.y > SCREEN_H - 130 - 8);
  if (over_hud) {
    return;
  }

  float dx = fabsf(we.x - ws.x), dy = fabsf(we.y - ws.y);
  bool is_box = (dx > 10 || dy > 10);

  /* ── BOX DRAG: always selects friendly units ── */
  if (is_box) {
    bool shift = IsKeyDown(KEY_LEFT_SHIFT);
    if (!shift)
      clear_selection(gs, ui);
    float x0 = ws.x < we.x ? ws.x : we.x, x1 = ws.x > we.x ? ws.x : we.x;
    float y0 = ws.y < we.y ? ws.y : we.y, y1 = ws.y > we.y ? ws.y : we.y;
    for (int i = 0; i < MAX_UNITS; i++) {
      Unit *u = &gs->units[i];
      if (!u->active || u->player != 0 || u->state == US_DEAD)
        continue;
      if (rect_intersects_unit(u, x0, y0, x1, y1))
        select_unit(gs, ui, i);
    }
    return;
  }

  /* ── SINGLE CLICK ── */
  bool shift = IsKeyDown(KEY_LEFT_SHIFT);
  int fu = find_friendly_unit_at(gs, we);
  int fb = find_friendly_building_at(gs, we);
  int dropoff = find_friendly_dropoff_at(gs, we);

  bool is_villager_carrying = false;
  bool has_villagers = false;
  for (int i = 0; i < ui->sel_count; i++) {
    if (gs->units[ui->sel_units[i]].type == UNIT_VILLAGER) {
      has_villagers = true;
      if (gs->units[ui->sel_units[i]].carry_amt > 0) {
        is_villager_carrying = true;
      }
    }
  }

  if (fu >= 0) {
    /* Clicked a friendly unit → select it */
    if (!shift)
      clear_selection(gs, ui);
    select_unit(gs, ui, fu);
    ui->sel_tile_x = -1;
    ui->sel_tile_y = -1;
  } else if (is_villager_carrying && dropoff >= 0) {
    issue_command_at(gs, ui, we);
  } else if (fb >= 0 && gs->buildings[fb].complete) {
    if (has_villagers && gs->buildings[fb].type == BLD_FARM) {
      /* Clicking a farm with villagers selected = gather command */
      issue_command_at(gs, ui, we);
    } else {
      /* Clicked a complete friendly building → select it */
      clear_selection(gs, ui);
      ui->sel_building = fb;
      gs->buildings[fb].selected = true;
      ui->sel_tile_x = -1;
      ui->sel_tile_y = -1;
    }
  } else if (ui->sel_count > 0) {
    /* Units already selected, clicked on world → context command */
    issue_command_at(gs, ui, we);
    /* Also record the tile they were commanded to (for gather info) */
    Vector2 cart = to_rvec2(iso_to_world(we.x, we.y));
    int tx = (int)(cart.x / TILE_SIZE), ty = (int)(cart.y / TILE_SIZE);
    if (map_in_bounds(tx, ty)) {
      TileType tt = gs->map[ty][tx].type;
      if (tt == TILE_FOREST || tt == TILE_GOLD || tt == TILE_STONE ||
          tt == TILE_BERRIES || tt == TILE_FARM) {
        ui->sel_tile_x = tx;
        ui->sel_tile_y = ty;
      } else {
        ui->sel_tile_x = -1;
        ui->sel_tile_y = -1;
      }
    }
  } else {
    /* Nothing selected → inspect the clicked tile */
    Vector2 cart = to_rvec2(iso_to_world(we.x, we.y));
    int tx = (int)(cart.x / TILE_SIZE), ty = (int)(cart.y / TILE_SIZE);
    if (map_in_bounds(tx, ty)) {
      TileType tt = gs->map[ty][tx].type;
      if (tt == TILE_FOREST || tt == TILE_GOLD || tt == TILE_STONE ||
          tt == TILE_BERRIES || tt == TILE_FARM) {
        ui->sel_tile_x = tx;
        ui->sel_tile_y = ty;
      } else {
        ui->sel_tile_x = -1;
        ui->sel_tile_y = -1;
        clear_selection(gs, ui);
      }
    }
  }
}

/* ─── Hover detection ─────────────────────────────────────── */
static void update_hover(GameState *gs, UIState *ui) {
  Vector2 mp = GetMousePosition();
  bool over_hud =
      mp.y < 42 || mp.y > SCREEN_H - 130 ||
      (mp.x > SCREEN_W - MINI_SIZE - 16 && mp.y > SCREEN_H - 130 - 8);

  ui->hover_unit = -1;
  ui->hover_building = -1;
  ui->hover_tile_x = -1;
  ui->hover_tile_y = -1;

  if (over_hud || gs->phase != PHASE_PLAYING)
    return;

  Vector2 wp = GetScreenToWorld2D(mp, ui->camera);

  /* 1. Units */
  int u = find_friendly_unit_at(gs, wp);
  if (u < 0)
    u = find_enemy_unit_at(gs, wp);
  if (u >= 0) {
    ui->hover_unit = u;
    return;
  }

  /* 2. Buildings */
  int b = find_friendly_building_at(gs, wp);
  if (b < 0)
    b = find_enemy_building_at(gs, wp);
  if (b >= 0) {
    ui->hover_building = b;
    return;
  }

  /* 3. Tiles (only interested in resources for hover highlighting usually) */
  Vector2 cart = to_rvec2(iso_to_world(wp.x, wp.y));
  int tx = (int)(cart.x / TILE_SIZE), ty = (int)(cart.y / TILE_SIZE);
  if (map_in_bounds(tx, ty) && gs->map[ty][tx].fog[0] == FOG_VISIBLE) {
    TileType tt = gs->map[ty][tx].type;
    if (tt == TILE_FOREST || tt == TILE_GOLD || tt == TILE_STONE ||
        tt == TILE_BERRIES || tt == TILE_FARM) {
      ui->hover_tile_x = tx;
      ui->hover_tile_y = ty;
    }
  }
}

/* ─── Master input update ─────────────────────────────────── */
void input_update(GameState *gs, UIState *ui) {
  float dt = GetFrameTime();
  update_hover(gs, ui);
  update_camera(gs, ui, dt);

  /* Build ghost takes over left-click entirely while active */
  if (gs->build_mode.active) {
    update_build_mode(gs, ui);
    update_hotkeys(gs, ui);
    return;
  }

  update_hotkeys(gs, ui);

  if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
    handle_left_down(gs, ui);
  if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON))
    handle_left_up(gs, ui);

  /* Minimap click → pan (handled inside hud_draw) */

  /* End screen */
  if (gs->phase == PHASE_VICTORY || gs->phase == PHASE_DEFEAT) {
    if (IsKeyPressed(KEY_Q))
      CloseWindow();
    if (IsKeyPressed(KEY_R))
      game_init(gs);
  }
}
