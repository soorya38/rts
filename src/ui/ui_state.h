#pragma once
#include "game.h"
#include "raylib.h"

#define TREE_VARIANT_COUNT 5

/* Helper to bridge domain Vec2 to Raylib Vector2 */
static inline Vector2 to_rvec2(Vec2 v) { return (Vector2){v.x, v.y}; }

static inline Vec2 to_vec2(Vector2 v) { return (Vec2){v.x, v.y}; }

typedef struct {
  Camera2D camera;
  Camera3D hero_camera;
  Camera2D hero_saved_camera;
  bool hero_has_saved_camera;
  HeroPossessionPhase hero_seen_phase;

  /* Selection state (belongs to UI) */
  bool box_selecting;
  Vector2 box_start;

  int sel_units[MAX_UNITS];
  int sel_count;
  int sel_building;           /* -1 = none */
  int sel_tile_x, sel_tile_y; /* -1 = none; last clicked resource tile */

  /* Hover state */
  int hover_unit;                 /* -1 = none */
  int hover_building;             /* -1 = none */
  int hover_tile_x, hover_tile_y; /* -1 = none */

  /* UI State overlays */
  bool build_panel_open;
  bool menu_start_hover;
  bool rally_mode;
  bool move_marker_active;
  float move_marker_tx;
  float move_marker_ty;
  float move_marker_start;

  /* Multiplayer lobby */
  char net_ip[64];    /* IP address the client will connect to */
  bool net_ip_active; /* IP text-box has keyboard focus */

  /* OSM map generator */
  char osm_location[128];   /* location name for OSM map gen */
  bool osm_location_active; /* location text-box has keyboard focus */

  /* Assets */
  Texture2D tex_buildings[BLD_COUNT];
  Texture2D tex_town_centers[4];
  Texture2D tex_houses[HOUSE_VARIANT_COUNT];
  Texture2D tex_mills[4];
  Texture2D tex_lumber_camps[4];
  Texture2D tex_barracks[4];
  Texture2D tex_archery_ranges[4];
  Texture2D tex_stables[4];
  Texture2D tex_blacksmiths[4];
  Texture2D tex_markets[4];
  Texture2D tex_mining_camps[4];
  Texture2D tex_watch_towers[4];
  Texture2D tex_monasteries[4];
  Texture2D tex_castles[4];
  Texture2D tex_land_grass[4];

  /* 3D Assets */
  Model mdl_town_centers[4];
  Model mdl_houses[HOUSE_VARIANT_COUNT];
  Model mdl_hero_house;
  Model mdl_hero_town_center;
  Model mdl_hero_stable;
  Model mdl_hero_watch_tower;
  Model mdl_hero_castle;
  Model mdl_mills[4];
  Model mdl_lumber_camps[4];
  Model mdl_barracks[4];
  Model mdl_archery_ranges[4];
  Model mdl_stables[4];
  Model mdl_blacksmiths[4];
  Model mdl_markets[4];
  Model mdl_mining_camps[4];
  Model mdl_watch_towers[4];
  Model mdl_monasteries[4];
  Model mdl_castles[4];
  Texture2D tex_hero_house_diffuse;
  Texture2D tex_hero_town_center_diffuse;
  Texture2D tex_hero_stable_diffuse;
  Texture2D tex_hero_watch_tower_diffuse;
  Texture2D tex_hero_castle_diffuse;
  Texture2D tex_units[UNIT_COUNT];
  Texture2D tex_env_trees[TREE_VARIANT_COUNT];
  Texture2D tex_env_gold;
  Texture2D tex_env_stone;
  Texture2D tex_env_berries;
  Texture2D tex_ui_food;
  Texture2D tex_ui_wood;
  Texture2D tex_ui_gold;
  Texture2D tex_ui_stone;
  Texture2D tex_ui_pop;
  Sound snd_hero_enter;
  Sound snd_hero_attack;
  Sound snd_hero_exit;
  Sound snd_hero_block;
  bool hero_audio_ready;

  /* OSM map overlay */
  Texture2D tex_osm_tiles[4][4]; /* [row][col] */
  int osm_tiles_loaded;          /* count of loaded tiles, 0 = not loaded */
} UIState;

void ui_state_init(UIState *ui, GameState *gs);
void ui_state_deinit(UIState *ui);
void ui_center_on_tc(UIState *ui, GameState *gs);
void ui_sync_hero_possession(UIState *ui, GameState *gs);
void ui_play_hero_attack(UIState *ui);
void ui_play_hero_block(UIState *ui);
Texture2D ui_get_building_texture(const UIState *ui, BldType type, int age);
Texture2D ui_get_house_texture(const UIState *ui, uint8_t variant);
Model ui_get_building_model(const UIState *ui, BldType type, int age,
                            uint8_t variant);
Model ui_get_hero_house_model(const UIState *ui);
Model ui_get_hero_town_center_model(const UIState *ui);
Model ui_get_hero_stable_model(const UIState *ui);
Model ui_get_hero_watch_tower_model(const UIState *ui);
Model ui_get_hero_castle_model(const UIState *ui);
/* Returns a scale factor relative to 720p so HUD elements are legible on any
   screen size, including high-DPI phones.  Range [1.0, 2.5]. */
float hud_scale(void);

/* Abstracted prototypes that now take UIState for rendering / input */
void renderer_draw_world(GameState *gs, UIState *ui);
void renderer_draw_hero_possession(GameState *gs, UIState *ui);
void hud_draw(GameState *gs, UIState *ui);
void input_update(GameState *gs, UIState *ui);
