#include "ui_state.h"
#include "net.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

static const char *asset_path_without_prefix(const char *path) {
  return (path && strncmp(path, "assets/", 7) == 0) ? path + 7 : NULL;
}

static Texture2D load_game_texture(const char *path) {
  Texture2D tex = {0};
  if (!path || !path[0])
    return tex;

  tex = LoadTexture(path);
  if (tex.id != 0) {
    SetTextureFilter(tex, TEXTURE_FILTER_POINT);
    return tex;
  }

  const char *trimmed = asset_path_without_prefix(path);
  if (trimmed) {
    tex = LoadTexture(trimmed);
    if (tex.id != 0) {
      SetTextureFilter(tex, TEXTURE_FILTER_POINT);
      return tex;
    }
  }

  const char *app_dir = GetApplicationDirectory();
  if (app_dir && app_dir[0]) {
    char full_path[1024];

    snprintf(full_path, sizeof(full_path), "%s%s", app_dir, path);
    tex = LoadTexture(full_path);
    if (tex.id != 0) {
      SetTextureFilter(tex, TEXTURE_FILTER_POINT);
      return tex;
    }

    snprintf(full_path, sizeof(full_path), "%s../%s", app_dir, path);
    tex = LoadTexture(full_path);
    if (tex.id != 0) {
      SetTextureFilter(tex, TEXTURE_FILTER_POINT);
      return tex;
    }

    if (trimmed) {
      snprintf(full_path, sizeof(full_path), "%s%s", app_dir, trimmed);
      tex = LoadTexture(full_path);
      if (tex.id != 0) {
        SetTextureFilter(tex, TEXTURE_FILTER_POINT);
        return tex;
      }

      snprintf(full_path, sizeof(full_path), "%s../%s", app_dir, trimmed);
      tex = LoadTexture(full_path);
      if (tex.id != 0) {
        SetTextureFilter(tex, TEXTURE_FILTER_POINT);
        return tex;
      }
    }
  }

  TraceLog(LOG_WARNING, "RTS >> failed to load texture: %s", path);
  return tex;
}

static Model load_game_model(const char *path) {
  Model mdl = {0};
  if (!path || !path[0])
    return mdl;

  mdl = LoadModel(path);
  if (mdl.meshCount > 0)
    return mdl;

  const char *trimmed = asset_path_without_prefix(path);
  if (trimmed) {
    mdl = LoadModel(trimmed);
    if (mdl.meshCount > 0)
      return mdl;
  }

  const char *app_dir = GetApplicationDirectory();
  if (app_dir && app_dir[0]) {
    char full_path[1024];

    snprintf(full_path, sizeof(full_path), "%s%s", app_dir, path);
    mdl = LoadModel(full_path);
    if (mdl.meshCount > 0)
      return mdl;

    snprintf(full_path, sizeof(full_path), "%s../%s", app_dir, path);
    mdl = LoadModel(full_path);
    if (mdl.meshCount > 0)
      return mdl;

    if (trimmed) {
      snprintf(full_path, sizeof(full_path), "%s%s", app_dir, trimmed);
      mdl = LoadModel(full_path);
      if (mdl.meshCount > 0)
        return mdl;

      snprintf(full_path, sizeof(full_path), "%s../%s", app_dir, trimmed);
      mdl = LoadModel(full_path);
      if (mdl.meshCount > 0)
        return mdl;
    }
  }

  TraceLog(LOG_WARNING, "RTS >> failed to load model: %s", path);
  return mdl;
}

static Sound make_hero_tone(float f0, float f1, float duration, float volume) {
  Sound sound = {0};
  if (!IsAudioDeviceReady() || duration <= 0.0f)
    return sound;

  const int sample_rate = 22050;
  int frame_count = (int)(duration * (float)sample_rate);
  if (frame_count < 1)
    frame_count = 1;

  short *samples = (short *)malloc((size_t)frame_count * sizeof(short));
  if (!samples)
    return sound;

  float phase = 0.0f;
  for (int i = 0; i < frame_count; i++) {
    float t = (float)i / (float)(frame_count - 1 > 0 ? frame_count - 1 : 1);
    float freq = f0 + (f1 - f0) * t;
    float env = sinf(t * PI);
    if (duration > 0.18f)
      env *= (1.0f - t * 0.25f);
    phase += (2.0f * PI * freq) / (float)sample_rate;
    float v = sinf(phase) * env * volume;
    samples[i] = (short)(clampf(v, -1.0f, 1.0f) * 32767.0f);
  }

  Wave wave = {0};
  wave.frameCount = (unsigned int)frame_count;
  wave.sampleRate = sample_rate;
  wave.sampleSize = 16;
  wave.channels = 1;
  wave.data = samples;
  sound = LoadSoundFromWave(wave);
  free(samples);
  return sound;
}

static void ui_load_hero_audio(UIState *ui) {
  ui->hero_audio_ready = IsAudioDeviceReady();
  if (!ui->hero_audio_ready)
    return;

  ui->snd_hero_enter = make_hero_tone(120.0f, 420.0f, 0.48f, 0.22f);
  ui->snd_hero_attack = make_hero_tone(520.0f, 180.0f, 0.12f, 0.30f);
  ui->snd_hero_exit = make_hero_tone(360.0f, 90.0f, 0.42f, 0.20f);
  ui->snd_hero_block = make_hero_tone(180.0f, 120.0f, 0.14f, 0.24f);
}

static void play_ui_sound(Sound s) {
  if (IsAudioDeviceReady() && s.frameCount > 0)
    PlaySound(s);
}

static void load_house_variant_textures(UIState *ui) {
  if (!ui)
    return;

  /* Load the 4 new Tamil cultured house variants */
  ui->tex_houses[0] = load_game_texture("assets/buildings/tamil_house_0.png");
  ui->tex_houses[1] = load_game_texture("assets/buildings/tamil_house_1.png");
  ui->tex_houses[2] = load_game_texture("assets/buildings/tamil_house_2.png");
  ui->tex_houses[3] = load_game_texture("assets/buildings/tamil_house_3.png");

  /* Fallbacks if the above fail */
  if (ui->tex_houses[0].id == 0)
    ui->tex_houses[0] = load_game_texture("assets/buildings/house_dark.png");
  if (ui->tex_houses[1].id == 0)
    ui->tex_houses[1] = load_game_texture("assets/buildings/house_feudal.png");
  if (ui->tex_houses[2].id == 0)
    ui->tex_houses[2] = load_game_texture("assets/buildings/house_castle.png");
  if (ui->tex_houses[3].id == 0)
    ui->tex_houses[3] =
        load_game_texture("assets/buildings/house_imperial.png");

  ui->mdl_houses[0] = load_game_model("assets/buildings/tamil_house_0.obj");
  ui->mdl_houses[1] = load_game_model("assets/buildings/tamil_house_1.obj");
  ui->mdl_houses[2] = load_game_model("assets/buildings/tamil_house_2.obj");
  ui->mdl_houses[3] = load_game_model("assets/buildings/tamil_house_3.obj");
  ui->mdl_hero_house = load_game_model("assets/buildings/hero_house/model.obj");
  ui->mdl_hero_town_center =
      load_game_model("assets/buildings/hero_town_center/model.obj");
  ui->mdl_hero_stable =
      load_game_model("assets/buildings/hero_stable/model.obj");
  ui->mdl_hero_watch_tower =
      load_game_model("assets/buildings/hero_watch_tower/model.obj");
  ui->mdl_hero_castle =
      load_game_model("assets/buildings/hero_castle/model.obj");
  ui->tex_hero_house_diffuse =
      load_game_texture("assets/buildings/hero_house/diffuse_0.png");
  ui->tex_hero_town_center_diffuse =
      load_game_texture("assets/buildings/hero_town_center/diffuse_0.png");
  ui->tex_hero_stable_diffuse =
      load_game_texture("assets/buildings/hero_stable/diffuse_0.png");
  ui->tex_hero_watch_tower_diffuse =
      load_game_texture("assets/buildings/hero_watch_tower/diffuse_0.png");
  ui->tex_hero_castle_diffuse =
      load_game_texture("assets/buildings/hero_castle/diffuse_0.png");
}

static void load_unit_textures(UIState *ui) {
  if (!ui)
    return;

  ui->tex_units[UNIT_VILLAGER] =
      load_game_texture("assets/units/villager_m.png");
  ui->tex_units[UNIT_SCOUT] = load_game_texture("assets/units/scout.png");
  ui->tex_units[UNIT_MILITIA] = load_game_texture("assets/units/militia.png");
  ui->tex_units[UNIT_MAN_AT_ARMS] =
      load_game_texture("assets/units/man_at_arms.png");
  ui->tex_units[UNIT_SPEARMAN] =
      load_game_texture("assets/units/spearman.png");
  ui->tex_units[UNIT_ARCHER] = load_game_texture("assets/units/archer.png");
  ui->tex_units[UNIT_SKIRMISHER] =
      load_game_texture("assets/units/skirmisher.png");
  ui->tex_units[UNIT_CAVALRY_ARCHER] =
      load_game_texture("assets/units/cavalry_archer.png");
  ui->tex_units[UNIT_KNIGHT] = load_game_texture("assets/units/knight.png");
  ui->tex_units[UNIT_MONK] = load_game_texture("assets/units/monk.png");
  ui->tex_units[UNIT_BATTERING_RAM] =
      load_game_texture("assets/units/battering_ram.png");
  ui->tex_units[UNIT_MANGONEL] =
      load_game_texture("assets/units/mangonel.png");
  ui->tex_units[UNIT_SCORPION] =
      load_game_texture("assets/units/scorpion.png");
  ui->tex_units[UNIT_BOMBARD_CANNON] =
      load_game_texture("assets/units/bombard_cannon.png");
}

void ui_state_init(UIState *ui, GameState *gs) {
  memset(ui, 0, sizeof(UIState));

  ui->sel_building = -1;
  ui->sel_tile_x = -1;
  ui->sel_tile_y = -1;
  ui->hover_unit = -1;
  ui->hover_building = -1;
  ui->hover_tile_x = -1;
  ui->hover_tile_y = -1;
  ui->rally_mode = false;

  /* Default placeholder IP — user overwrites this to join */
  strncpy(ui->net_ip, "192.168.1.1", sizeof(ui->net_ip) - 1);
  ui->net_ip_active = false;

  ui->camera.zoom = 1.0f;
  ui->camera.offset =
      (Vector2){GetScreenWidth() * 0.5f, GetScreenHeight() * 0.5f};
  ui->camera.rotation = 0.0f;
  ui->hero_camera = (Camera3D){0};
  ui->hero_camera.up = (Vector3){0.0f, 1.0f, 0.0f};
  ui->hero_camera.fovy = 72.0f;
  ui->hero_camera.projection = CAMERA_PERSPECTIVE;
  ui->hero_seen_phase = HERO_POSSESSION_OFF;
  ui->hero_has_saved_camera = false;

  ui_center_on_tc(ui, gs);

  /* Keep the box-only renderer everywhere else, but allow age-specific
     town center art. */
  ui->tex_town_centers[0] =
      load_game_texture("assets/buildings/tamil_town_center.png");
  ui->tex_town_centers[1] =
      load_game_texture("assets/buildings/tamil_town_center.png");
  ui->tex_town_centers[2] =
      load_game_texture("assets/buildings/tamil_town_center.png");
  ui->tex_town_centers[3] =
      load_game_texture("assets/buildings/tamil_town_center.png");
  load_house_variant_textures(ui);
  ui->tex_mills[0] = load_game_texture("assets/buildings/tamil_mill.png");
  ui->tex_mills[1] = load_game_texture("assets/buildings/tamil_mill.png");
  ui->tex_mills[2] = load_game_texture("assets/buildings/tamil_mill.png");
  ui->tex_mills[3] = load_game_texture("assets/buildings/tamil_mill.png");
  ui->tex_lumber_camps[0] =
      load_game_texture("assets/buildings/tamil_lumber_camp.png");
  ui->tex_lumber_camps[1] =
      load_game_texture("assets/buildings/tamil_lumber_camp.png");
  ui->tex_lumber_camps[2] =
      load_game_texture("assets/buildings/tamil_lumber_camp.png");
  ui->tex_lumber_camps[3] =
      load_game_texture("assets/buildings/tamil_lumber_camp.png");
  ui->tex_barracks[0] =
      load_game_texture("assets/buildings/tamil_barracks.png");
  ui->tex_barracks[1] =
      load_game_texture("assets/buildings/tamil_barracks.png");
  ui->tex_barracks[2] =
      load_game_texture("assets/buildings/tamil_barracks.png");
  ui->tex_barracks[3] =
      load_game_texture("assets/buildings/tamil_barracks.png");
  ui->tex_archery_ranges[0] =
      load_game_texture("assets/buildings/tamil_archery_range.png");
  ui->tex_archery_ranges[1] =
      load_game_texture("assets/buildings/tamil_archery_range.png");
  ui->tex_archery_ranges[2] =
      load_game_texture("assets/buildings/tamil_archery_range.png");
  ui->tex_archery_ranges[3] =
      load_game_texture("assets/buildings/tamil_archery_range.png");
  ui->tex_stables[0] = load_game_texture("assets/buildings/tamil_stable.png");
  ui->tex_stables[1] = load_game_texture("assets/buildings/tamil_stable.png");
  ui->tex_stables[2] = load_game_texture("assets/buildings/tamil_stable.png");
  ui->tex_stables[3] = load_game_texture("assets/buildings/tamil_stable.png");
  ui->tex_blacksmiths[0] =
      load_game_texture("assets/buildings/tamil_blacksmith.png");
  ui->tex_blacksmiths[1] =
      load_game_texture("assets/buildings/tamil_blacksmith.png");
  ui->tex_blacksmiths[2] =
      load_game_texture("assets/buildings/tamil_blacksmith.png");
  ui->tex_blacksmiths[3] =
      load_game_texture("assets/buildings/tamil_blacksmith.png");
  ui->tex_markets[0] = load_game_texture("assets/buildings/tamil_market.png");
  ui->tex_markets[1] = load_game_texture("assets/buildings/tamil_market.png");
  ui->tex_markets[2] = load_game_texture("assets/buildings/tamil_market.png");
  ui->tex_markets[3] = load_game_texture("assets/buildings/tamil_market.png");
  ui->tex_mining_camps[0] =
      load_game_texture("assets/buildings/tamil_mining_camp.png");
  ui->tex_mining_camps[1] =
      load_game_texture("assets/buildings/tamil_mining_camp.png");
  ui->tex_mining_camps[2] =
      load_game_texture("assets/buildings/tamil_mining_camp.png");
  ui->tex_mining_camps[3] =
      load_game_texture("assets/buildings/tamil_mining_camp.png");
  ui->tex_watch_towers[0] =
      load_game_texture("assets/buildings/tamil_watch_tower.png");
  ui->tex_watch_towers[1] =
      load_game_texture("assets/buildings/tamil_watch_tower.png");
  ui->tex_watch_towers[2] =
      load_game_texture("assets/buildings/tamil_watch_tower.png");
  ui->tex_watch_towers[3] =
      load_game_texture("assets/buildings/tamil_watch_tower.png");
  ui->tex_monasteries[0] =
      load_game_texture("assets/buildings/tamil_monastery.png");
  ui->tex_monasteries[1] =
      load_game_texture("assets/buildings/tamil_monastery.png");
  ui->tex_monasteries[2] =
      load_game_texture("assets/buildings/tamil_monastery.png");
  ui->tex_monasteries[3] =
      load_game_texture("assets/buildings/tamil_monastery.png");
  ui->tex_castles[0] = load_game_texture("assets/buildings/tamil_castle.png");
  ui->tex_castles[1] = load_game_texture("assets/buildings/tamil_castle.png");
  ui->tex_castles[2] = load_game_texture("assets/buildings/tamil_castle.png");
  ui->tex_castles[3] = load_game_texture("assets/buildings/tamil_castle.png");

  ui->mdl_town_centers[0] =
      load_game_model("assets/buildings/tamil_town_center.obj");
  ui->mdl_town_centers[1] =
      load_game_model("assets/buildings/tamil_town_center.obj");
  ui->mdl_town_centers[2] =
      load_game_model("assets/buildings/tamil_town_center.obj");
  ui->mdl_town_centers[3] =
      load_game_model("assets/buildings/tamil_town_center.obj");
  ui->mdl_mills[0] = load_game_model("assets/buildings/tamil_mill.obj");
  ui->mdl_mills[1] = load_game_model("assets/buildings/tamil_mill.obj");
  ui->mdl_mills[2] = load_game_model("assets/buildings/tamil_mill.obj");
  ui->mdl_mills[3] = load_game_model("assets/buildings/tamil_mill.obj");
  ui->mdl_lumber_camps[0] =
      load_game_model("assets/buildings/tamil_lumber_camp.obj");
  ui->mdl_lumber_camps[1] =
      load_game_model("assets/buildings/tamil_lumber_camp.obj");
  ui->mdl_lumber_camps[2] =
      load_game_model("assets/buildings/tamil_lumber_camp.obj");
  ui->mdl_lumber_camps[3] =
      load_game_model("assets/buildings/tamil_lumber_camp.obj");
  ui->mdl_barracks[0] = load_game_model("assets/buildings/tamil_barracks.obj");
  ui->mdl_barracks[1] = load_game_model("assets/buildings/tamil_barracks.obj");
  ui->mdl_barracks[2] = load_game_model("assets/buildings/tamil_barracks.obj");
  ui->mdl_barracks[3] = load_game_model("assets/buildings/tamil_barracks.obj");
  ui->mdl_archery_ranges[0] =
      load_game_model("assets/buildings/tamil_archery_range.obj");
  ui->mdl_archery_ranges[1] =
      load_game_model("assets/buildings/tamil_archery_range.obj");
  ui->mdl_archery_ranges[2] =
      load_game_model("assets/buildings/tamil_archery_range.obj");
  ui->mdl_archery_ranges[3] =
      load_game_model("assets/buildings/tamil_archery_range.obj");
  ui->mdl_stables[0] = load_game_model("assets/buildings/tamil_stable.obj");
  ui->mdl_stables[1] = load_game_model("assets/buildings/tamil_stable.obj");
  ui->mdl_stables[2] = load_game_model("assets/buildings/tamil_stable.obj");
  ui->mdl_stables[3] = load_game_model("assets/buildings/tamil_stable.obj");
  ui->mdl_blacksmiths[0] =
      load_game_model("assets/buildings/tamil_blacksmith.obj");
  ui->mdl_blacksmiths[1] =
      load_game_model("assets/buildings/tamil_blacksmith.obj");
  ui->mdl_blacksmiths[2] =
      load_game_model("assets/buildings/tamil_blacksmith.obj");
  ui->mdl_blacksmiths[3] =
      load_game_model("assets/buildings/tamil_blacksmith.obj");
  ui->mdl_markets[0] = load_game_model("assets/buildings/tamil_market.obj");
  ui->mdl_markets[1] = load_game_model("assets/buildings/tamil_market.obj");
  ui->mdl_markets[2] = load_game_model("assets/buildings/tamil_market.obj");
  ui->mdl_markets[3] = load_game_model("assets/buildings/tamil_market.obj");
  ui->mdl_mining_camps[0] =
      load_game_model("assets/buildings/tamil_mining_camp.obj");
  ui->mdl_mining_camps[1] =
      load_game_model("assets/buildings/tamil_mining_camp.obj");
  ui->mdl_mining_camps[2] =
      load_game_model("assets/buildings/tamil_mining_camp.obj");
  ui->mdl_mining_camps[3] =
      load_game_model("assets/buildings/tamil_mining_camp.obj");
  ui->mdl_watch_towers[0] =
      load_game_model("assets/buildings/tamil_watch_tower.obj");
  ui->mdl_watch_towers[1] =
      load_game_model("assets/buildings/tamil_watch_tower.obj");
  ui->mdl_watch_towers[2] =
      load_game_model("assets/buildings/tamil_watch_tower.obj");
  ui->mdl_watch_towers[3] =
      load_game_model("assets/buildings/tamil_watch_tower.obj");
  ui->mdl_monasteries[0] =
      load_game_model("assets/buildings/tamil_monastery.obj");
  ui->mdl_monasteries[1] =
      load_game_model("assets/buildings/tamil_monastery.obj");
  ui->mdl_monasteries[2] =
      load_game_model("assets/buildings/tamil_monastery.obj");
  ui->mdl_monasteries[3] =
      load_game_model("assets/buildings/tamil_monastery.obj");
  ui->mdl_castles[0] = load_game_model("assets/buildings/tamil_castle.obj");
  ui->mdl_castles[1] = load_game_model("assets/buildings/tamil_castle.obj");
  ui->mdl_castles[2] = load_game_model("assets/buildings/tamil_castle.obj");
  ui->mdl_castles[3] = load_game_model("assets/buildings/tamil_castle.obj");
  ui->tex_land_grass[0] = load_game_texture("assets/ui/land_grass_0.png");
  ui->tex_land_grass[1] = load_game_texture("assets/ui/land_grass_1.png");
  ui->tex_land_grass[2] = load_game_texture("assets/ui/land_grass_2.png");
  ui->tex_land_grass[3] = load_game_texture("assets/ui/land_grass_3.png");
  load_unit_textures(ui);

  ui->tex_env_trees[0] = load_game_texture("assets/buildings/tamil_tree_0.png");
  ui->tex_env_trees[1] = load_game_texture("assets/buildings/tamil_tree_1.png");
  ui->tex_env_trees[2] = load_game_texture("assets/buildings/tamil_tree_2.png");
  ui->tex_env_trees[3] = load_game_texture("assets/buildings/tamil_tree_3.png");
  ui->tex_env_trees[4] = load_game_texture("assets/env/tree.png");

  ui_load_hero_audio(ui);
}

void ui_state_deinit(UIState *ui) {
  for (int i = 0; i < BLD_COUNT; i++)
    UnloadTexture(ui->tex_buildings[i]);
  for (int i = 0; i < 4; i++)
    UnloadTexture(ui->tex_town_centers[i]);
  for (int i = 0; i < HOUSE_VARIANT_COUNT; i++)
    UnloadTexture(ui->tex_houses[i]);
  for (int i = 0; i < 4; i++)
    UnloadTexture(ui->tex_mills[i]);
  for (int i = 0; i < 4; i++)
    UnloadTexture(ui->tex_lumber_camps[i]);
  for (int i = 0; i < 4; i++)
    UnloadTexture(ui->tex_barracks[i]);
  for (int i = 0; i < 4; i++)
    UnloadTexture(ui->tex_archery_ranges[i]);
  for (int i = 0; i < 4; i++)
    UnloadTexture(ui->tex_stables[i]);
  for (int i = 0; i < 4; i++)
    UnloadTexture(ui->tex_blacksmiths[i]);
  for (int i = 0; i < 4; i++)
    UnloadTexture(ui->tex_markets[i]);
  for (int i = 0; i < 4; i++)
    UnloadTexture(ui->tex_mining_camps[i]);
  for (int i = 0; i < 4; i++)
    UnloadTexture(ui->tex_watch_towers[i]);
  for (int i = 0; i < 4; i++)
    UnloadTexture(ui->tex_monasteries[i]);
  for (int i = 0; i < 4; i++)
    UnloadTexture(ui->tex_castles[i]);
  UnloadTexture(ui->tex_hero_house_diffuse);
  UnloadTexture(ui->tex_hero_town_center_diffuse);
  UnloadTexture(ui->tex_hero_stable_diffuse);
  UnloadTexture(ui->tex_hero_watch_tower_diffuse);
  UnloadTexture(ui->tex_hero_castle_diffuse);

  for (int i = 0; i < 4; i++)
    UnloadModel(ui->mdl_town_centers[i]);
  for (int i = 0; i < HOUSE_VARIANT_COUNT; i++)
    UnloadModel(ui->mdl_houses[i]);
  UnloadModel(ui->mdl_hero_house);
  UnloadModel(ui->mdl_hero_town_center);
  UnloadModel(ui->mdl_hero_stable);
  UnloadModel(ui->mdl_hero_watch_tower);
  UnloadModel(ui->mdl_hero_castle);
  for (int i = 0; i < 4; i++)
    UnloadModel(ui->mdl_mills[i]);
  for (int i = 0; i < 4; i++)
    UnloadModel(ui->mdl_lumber_camps[i]);
  for (int i = 0; i < 4; i++)
    UnloadModel(ui->mdl_barracks[i]);
  for (int i = 0; i < 4; i++)
    UnloadModel(ui->mdl_archery_ranges[i]);
  for (int i = 0; i < 4; i++)
    UnloadModel(ui->mdl_stables[i]);
  for (int i = 0; i < 4; i++)
    UnloadModel(ui->mdl_blacksmiths[i]);
  for (int i = 0; i < 4; i++)
    UnloadModel(ui->mdl_markets[i]);
  for (int i = 0; i < 4; i++)
    UnloadModel(ui->mdl_mining_camps[i]);
  for (int i = 0; i < 4; i++)
    UnloadModel(ui->mdl_watch_towers[i]);
  for (int i = 0; i < 4; i++)
    UnloadModel(ui->mdl_monasteries[i]);
  for (int i = 0; i < 4; i++)
    UnloadModel(ui->mdl_castles[i]);
  for (int i = 0; i < 4; i++)
    UnloadTexture(ui->tex_land_grass[i]);
  for (int i = 0; i < UNIT_COUNT; i++)
    UnloadTexture(ui->tex_units[i]);
  for (int i = 0; i < TREE_VARIANT_COUNT; i++)
    UnloadTexture(ui->tex_env_trees[i]);
  UnloadTexture(ui->tex_env_gold);
  UnloadTexture(ui->tex_env_stone);
  UnloadTexture(ui->tex_env_berries);
  UnloadTexture(ui->tex_ui_food);
  UnloadTexture(ui->tex_ui_wood);
  UnloadTexture(ui->tex_ui_gold);
  UnloadTexture(ui->tex_ui_stone);
  UnloadTexture(ui->tex_ui_pop);
  if (ui->snd_hero_enter.frameCount > 0)
    UnloadSound(ui->snd_hero_enter);
  if (ui->snd_hero_attack.frameCount > 0)
    UnloadSound(ui->snd_hero_attack);
  if (ui->snd_hero_exit.frameCount > 0)
    UnloadSound(ui->snd_hero_exit);
  if (ui->snd_hero_block.frameCount > 0)
    UnloadSound(ui->snd_hero_block);
}

void ui_center_on_tc(UIState *ui, GameState *gs) {
  int lp = net_get_local_player();
  float target_wx = (MAP_W / 2) * TILE_SIZE;
  float target_wy = (MAP_H / 2) * TILE_SIZE;
  if (gs) {
    for (int i = 0; i < MAX_BUILDINGS; i++) {
      Building *b = &gs->buildings[i];
      if (b->active && b->player == lp && b->type == BLD_TOWN_CENTER) {
        target_wx = (b->tx + b->tw / 2.0f) * TILE_SIZE;
        target_wy = (b->ty + b->th / 2.0f) * TILE_SIZE;
        break;
      }
    }
  }
  Vec2 iso_target = world_to_iso(target_wx, target_wy);
  ui->camera.target = to_rvec2(iso_target);
}

void ui_play_hero_attack(UIState *ui) {
  if (!ui)
    return;
  play_ui_sound(ui->snd_hero_attack);
}

void ui_play_hero_block(UIState *ui) {
  if (!ui)
    return;
  play_ui_sound(ui->snd_hero_block);
}

void ui_sync_hero_possession(UIState *ui, GameState *gs) {
  if (!ui || !gs)
    return;

  HeroPossessionPhase phase = gs->hero.phase;
  if (phase != ui->hero_seen_phase) {
    if (phase == HERO_POSSESSION_ENTERING) {
      ui->hero_saved_camera = ui->camera;
      ui->hero_has_saved_camera = true;
      play_ui_sound(ui->snd_hero_enter);
#if !defined(PLATFORM_ANDROID) && !defined(ANDROID)
      DisableCursor();
#endif
    } else if (phase == HERO_POSSESSION_EXITING) {
      play_ui_sound(ui->snd_hero_exit);
    } else if (phase == HERO_POSSESSION_OFF && ui->hero_has_saved_camera) {
      ui->camera = ui->hero_saved_camera;
      ui->hero_has_saved_camera = false;
#if !defined(PLATFORM_ANDROID) && !defined(ANDROID)
      EnableCursor();
#endif
    }
    ui->hero_seen_phase = phase;
  }

  if (phase == HERO_POSSESSION_OFF || gs->hero.unit_id < 0 ||
      gs->hero.unit_id >= MAX_UNITS)
    return;

  Unit *u = &gs->units[gs->hero.unit_id];
  if (!u->active)
    return;

  Vec2 target = world_to_iso(u->wx, u->wy);
  float t = 1.0f;
  if (gs->hero.transition_time > 0.0f &&
      (phase == HERO_POSSESSION_ENTERING || phase == HERO_POSSESSION_EXITING)) {
    t = clampf(gs->hero.transition_timer / gs->hero.transition_time, 0.0f,
               1.0f);
    if (phase == HERO_POSSESSION_EXITING)
      t = 1.0f - t;
  }

  float saved_zoom =
      ui->hero_has_saved_camera ? ui->hero_saved_camera.zoom : ui->camera.zoom;
  ui->camera.target.x = lerpf(ui->camera.target.x, target.x, 0.25f + 0.55f * t);
  ui->camera.target.y =
      lerpf(ui->camera.target.y, target.y - 34.0f, 0.25f + 0.55f * t);
  ui->camera.zoom = lerpf(saved_zoom, 3.1f, t);
  ui->camera.offset =
      (Vector2){GetScreenWidth() * 0.5f, GetScreenHeight() * 0.5f};
}

Texture2D ui_get_building_texture(const UIState *ui, BldType type, int age) {
  if (!ui)
    return (Texture2D){0};

  if (age < 0)
    age = 0;
  if (age > 3)
    age = 3;

  Texture2D tex = ui->tex_buildings[type];
  switch (type) {
  case BLD_TOWN_CENTER:
    if (ui->tex_town_centers[age].id != 0)
      tex = ui->tex_town_centers[age];
    break;
  case BLD_HOUSE:
    if (ui->tex_houses[0].id != 0)
      tex = ui->tex_houses[0];
    break;
  case BLD_MILL:
    if (ui->tex_mills[age].id != 0)
      tex = ui->tex_mills[age];
    break;
  case BLD_LUMBER_CAMP:
    if (ui->tex_lumber_camps[age].id != 0)
      tex = ui->tex_lumber_camps[age];
    break;
  case BLD_BARRACKS:
    if (ui->tex_barracks[age].id != 0)
      tex = ui->tex_barracks[age];
    break;
  case BLD_ARCHERY_RANGE:
    if (ui->tex_archery_ranges[age].id != 0)
      tex = ui->tex_archery_ranges[age];
    break;
  case BLD_STABLE:
    if (ui->tex_stables[age].id != 0)
      tex = ui->tex_stables[age];
    break;
  case BLD_BLACKSMITH:
    if (ui->tex_blacksmiths[age].id != 0)
      tex = ui->tex_blacksmiths[age];
    break;
  case BLD_MARKET:
    if (ui->tex_markets[age].id != 0)
      tex = ui->tex_markets[age];
    break;
  case BLD_MINING_CAMP:
    if (ui->tex_mining_camps[age].id != 0)
      tex = ui->tex_mining_camps[age];
    break;
  case BLD_WATCH_TOWER:
    if (ui->tex_watch_towers[age].id != 0)
      tex = ui->tex_watch_towers[age];
    break;
  case BLD_MONASTERY:
    if (ui->tex_monasteries[age].id != 0)
      tex = ui->tex_monasteries[age];
    break;
  case BLD_CASTLE:
    if (ui->tex_castles[age].id != 0)
      tex = ui->tex_castles[age];
    break;
  default:
    break;
  }
  return tex;
}

Texture2D ui_get_house_texture(const UIState *ui, uint8_t variant) {
  if (!ui)
    return (Texture2D){0};

  Texture2D tex = ui->tex_houses[variant % HOUSE_VARIANT_COUNT];
  if (tex.id != 0)
    return tex;
  for (int i = 0; i < HOUSE_VARIANT_COUNT; i++) {
    if (ui->tex_houses[i].id != 0)
      return ui->tex_houses[i];
  }
  return (Texture2D){0};
}

Model ui_get_building_model(const UIState *ui, BldType type, int age,
                            uint8_t variant) {
  if (!ui)
    return (Model){0};

  if (age < 0)
    age = 0;
  if (age > 3)
    age = 3;

  Model mdl = {0};
  switch (type) {
  case BLD_TOWN_CENTER:
    if (ui->mdl_town_centers[age].meshCount > 0)
      mdl = ui->mdl_town_centers[age];
    break;
  case BLD_HOUSE:
    mdl = ui->mdl_houses[variant % HOUSE_VARIANT_COUNT];
    if (mdl.meshCount == 0) {
      for (int i = 0; i < HOUSE_VARIANT_COUNT; i++) {
        if (ui->mdl_houses[i].meshCount > 0) {
          mdl = ui->mdl_houses[i];
          break;
        }
      }
    }
    break;
  case BLD_MILL:
    if (ui->mdl_mills[age].meshCount > 0)
      mdl = ui->mdl_mills[age];
    break;
  case BLD_LUMBER_CAMP:
    if (ui->mdl_lumber_camps[age].meshCount > 0)
      mdl = ui->mdl_lumber_camps[age];
    break;
  case BLD_BARRACKS:
    if (ui->mdl_barracks[age].meshCount > 0)
      mdl = ui->mdl_barracks[age];
    break;
  case BLD_ARCHERY_RANGE:
    if (ui->mdl_archery_ranges[age].meshCount > 0)
      mdl = ui->mdl_archery_ranges[age];
    break;
  case BLD_STABLE:
    if (ui->mdl_stables[age].meshCount > 0)
      mdl = ui->mdl_stables[age];
    break;
  case BLD_BLACKSMITH:
    if (ui->mdl_blacksmiths[age].meshCount > 0)
      mdl = ui->mdl_blacksmiths[age];
    break;
  case BLD_MARKET:
    if (ui->mdl_markets[age].meshCount > 0)
      mdl = ui->mdl_markets[age];
    break;
  case BLD_MINING_CAMP:
    if (ui->mdl_mining_camps[age].meshCount > 0)
      mdl = ui->mdl_mining_camps[age];
    break;
  case BLD_WATCH_TOWER:
    if (ui->mdl_watch_towers[age].meshCount > 0)
      mdl = ui->mdl_watch_towers[age];
    break;
  case BLD_MONASTERY:
    if (ui->mdl_monasteries[age].meshCount > 0)
      mdl = ui->mdl_monasteries[age];
    break;
  case BLD_CASTLE:
    if (ui->mdl_castles[age].meshCount > 0)
      mdl = ui->mdl_castles[age];
    break;
  default:
    break;
  }

  if (mdl.meshCount > 0) {
    Texture2D tex = ui_get_building_texture(ui, type, age);
    if (tex.id != 0 && mdl.materials != NULL) {
      mdl.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = tex;
    }
  }

  return mdl;
}

Model ui_get_hero_house_model(const UIState *ui) {
  if (!ui)
    return (Model){0};

  Model mdl = ui->mdl_hero_house;
  if (mdl.meshCount > 0 && ui->tex_hero_house_diffuse.id != 0 &&
      mdl.materials != NULL) {
    mdl.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture =
        ui->tex_hero_house_diffuse;
  }
  return mdl;
}

Model ui_get_hero_town_center_model(const UIState *ui) {
  if (!ui)
    return (Model){0};

  Model mdl = ui->mdl_hero_town_center;
  if (mdl.meshCount > 0 && ui->tex_hero_town_center_diffuse.id != 0 &&
      mdl.materials != NULL) {
    mdl.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture =
        ui->tex_hero_town_center_diffuse;
  }
  return mdl;
}

Model ui_get_hero_stable_model(const UIState *ui) {
  if (!ui)
    return (Model){0};

  Model mdl = ui->mdl_hero_stable;
  if (mdl.meshCount > 0 && ui->tex_hero_stable_diffuse.id != 0 &&
      mdl.materials != NULL) {
    mdl.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture =
        ui->tex_hero_stable_diffuse;
  }
  return mdl;
}

Model ui_get_hero_watch_tower_model(const UIState *ui) {
  if (!ui)
    return (Model){0};

  Model mdl = ui->mdl_hero_watch_tower;
  if (mdl.meshCount > 0 && ui->tex_hero_watch_tower_diffuse.id != 0 &&
      mdl.materials != NULL) {
    mdl.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture =
        ui->tex_hero_watch_tower_diffuse;
  }
  return mdl;
}

Model ui_get_hero_castle_model(const UIState *ui) {
  if (!ui)
    return (Model){0};

  Model mdl = ui->mdl_hero_castle;
  if (mdl.meshCount > 0 && ui->tex_hero_castle_diffuse.id != 0 &&
      mdl.materials != NULL) {
    mdl.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture =
        ui->tex_hero_castle_diffuse;
  }
  return mdl;
}

/* Scale factor relative to 720p so all HUD elements are readable on phones */
float hud_scale(void) {
  float s = GetScreenHeight() / 720.0f;
  if (s < 1.0f)
    s = 1.0f;
  if (s > 2.5f)
    s = 2.5f;
  return s;
}
