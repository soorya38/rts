/*=============================================================
 * map.c  –  Map generation, fog of war, and resource search
 *
 * Procedurally generates a 64×64 tile grid with water, forests,
 * gold, stone, and berry deposits.  Manages per-player fog-of-war
 * visibility and provides spatial queries for resource gathering.
 *=============================================================*/
#include "game.h"
#include "threadpool.h"

/* ── Tile queries ──────────────────────────────────────────── */

bool map_in_bounds(int tile_x, int tile_y)
{
    return tile_x >= 0 && tile_x < MAP_W &&
           tile_y >= 0 && tile_y < MAP_H;
}

static bool tile_is_terrain_blocker(TileType type)
{
    return type == TILE_WATER  || type == TILE_FOREST ||
           type == TILE_GOLD   || type == TILE_STONE  ||
           type == TILE_BERRIES;
}

bool map_is_passable(GameState *gs, int tile_x, int tile_y)
{
    if (!map_in_bounds(tile_x, tile_y)) return false;

    Tile *tile = &gs->map[tile_y][tile_x];

    if (tile_is_terrain_blocker(tile->type)) return false;

    /* Completed buildings block movement, except farms and gates. */
    if (tile->building_id >= 0) {
        Building *bld = &gs->buildings[tile->building_id];
        if (bld->active && bld->complete &&
            bld->type != BLD_FARM && bld->type != BLD_GATE) {
            return false;
        }
    }

    return true;
}

/* True if the tile is open water a ship can occupy. */
bool map_tile_is_water(GameState *gs, int tile_x, int tile_y)
{
    if (!map_in_bounds(tile_x, tile_y)) return false;
    return gs->map[tile_y][tile_x].type == TILE_WATER;
}

/* Domain-aware passability.  Ships (water==true) may only move over
   open water tiles free of buildings; land units keep their normal
   rules.  A dock occupies shore tiles but leaves its water tiles
   open, so ships can pass. */
bool map_is_passable_dom(GameState *gs, int tile_x, int tile_y, bool water)
{
    if (!water) return map_is_passable(gs, tile_x, tile_y);

    if (!map_in_bounds(tile_x, tile_y)) return false;
    Tile *tile = &gs->map[tile_y][tile_x];
    if (tile->type != TILE_WATER) return false;
    if (tile->building_id >= 0) {
        Building *bld = &gs->buildings[tile->building_id];
        /* Docks sit on the shoreline; their footprint never blocks
           the water lane, but guard against any other building. */
        if (bld->active && bld->type != BLD_DOCK) return false;
    }
    return true;
}

/* A passable land tile that is orthogonally adjacent to water —
   where a dock may be built. */
bool map_is_shore(GameState *gs, int tile_x, int tile_y)
{
    if (!map_is_passable(gs, tile_x, tile_y)) return false;
    return map_tile_is_water(gs, tile_x + 1, tile_y) ||
           map_tile_is_water(gs, tile_x - 1, tile_y) ||
           map_tile_is_water(gs, tile_x, tile_y + 1) ||
           map_tile_is_water(gs, tile_x, tile_y - 1);
}

bool map_is_buildable(GameState *gs, int origin_x, int origin_y, int width, int height)
{
    for (int dy = 0; dy < height; dy++) {
        for (int dx = 0; dx < width; dx++) {
            int tx = origin_x + dx;
            int ty = origin_y + dy;
            if (!map_in_bounds(tx, ty)) return false;

            Tile *tile = &gs->map[ty][tx];
            if (tile_is_terrain_blocker(tile->type)) return false;
            if (tile->building_id >= 0) return false;
        }
    }
    return true;
}

/* A building's footprint borders water (needed to place a dock). */
static bool footprint_touches_water(GameState *gs, int ox, int oy, int w, int h)
{
    for (int dy = -1; dy <= h; dy++) {
        for (int dx = -1; dx <= w; dx++) {
            if (dx >= 0 && dx < w && dy >= 0 && dy < h) continue;  /* perimeter only */
            if (map_tile_is_water(gs, ox + dx, oy + dy)) return true;
        }
    }
    return false;
}

/* Type-aware buildability: docks must sit on the shoreline. */
bool map_buildable_for(GameState *gs, BldType type, int ox, int oy, int w, int h)
{
    if (!map_is_buildable(gs, ox, oy, w, h)) return false;
    if (type == BLD_DOCK) return footprint_touches_water(gs, ox, oy, w, h);
    return true;
}

/* ── Building footprint helpers ────────────────────────────── */

void map_place_building(GameState *gs, int tx, int ty, int width, int height, int building_id)
{
    for (int dy = 0; dy < height; dy++) {
        for (int dx = 0; dx < width; dx++) {
            if (map_in_bounds(tx + dx, ty + dy)) {
                gs->map[ty + dy][tx + dx].building_id = building_id;
            }
        }
    }
}

void map_clear_building(GameState *gs, int tx, int ty, int width, int height)
{
    for (int dy = 0; dy < height; dy++) {
        for (int dx = 0; dx < width; dx++) {
            if (map_in_bounds(tx + dx, ty + dy)) {
                gs->map[ty + dy][tx + dx].building_id = -1;
            }
        }
    }
}

/* ── Procedural generation helpers ─────────────────────────── */

static void force_place_patch(GameState *gs, int cx, int cy, TileType type, int radius, int base_amount);

/* 8 compass-direction unit vectors (scaled ×10 for integer math). */
static const int COMPASS_DX[8] = { 10,  7,  0, -7, -10, -7,  0,  7 };
static const int COMPASS_DY[8] = {  0,  7, 10,  7,   0, -7, -10, -7 };

/* Scatter guaranteed resources (3 forests, 2 berries, 1 gold,
   1 stone) around each player's start in randomised directions. */
static void place_start_resources(GameState *gs, int tc_x, int tc_y)
{
    /* Shuffle direction indices (Fisher-Yates). */
    int dir_indices[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    for (int i = 7; i > 0; i--) {
        int j = (int)(rng_next() % (i + 1));
        int tmp = dir_indices[i];
        dir_indices[i] = dir_indices[j];
        dir_indices[j] = tmp;
    }

    static const TileType RESOURCE_TYPES[7] = {
        TILE_FOREST, TILE_FOREST, TILE_FOREST,
        TILE_BERRIES, TILE_BERRIES, TILE_GOLD, TILE_STONE
    };
    static const int PATCH_RADII[7] = { 2, 2, 2, 1, 1, 1, 1 };

    int amounts[7] = {
        140 + (int)(rng_next() % 40), 140 + (int)(rng_next() % 40),
        140 + (int)(rng_next() % 40),
        450 + (int)(rng_next() % 100), 450 + (int)(rng_next() % 100),
        700 + (int)(rng_next() % 100), 600 + (int)(rng_next() % 100)
    };

    for (int i = 0; i < 7; i++) {
        int dir  = dir_indices[i];
        int dist = 9 + (int)(rng_next() % 7);    /* 9-15 tiles from TC */
        int jx   = (int)(rng_next() % 3) - 1;    /* ±1 tile jitter */
        int jy   = (int)(rng_next() % 3) - 1;

        int cx = clampi(tc_x + (COMPASS_DX[dir] * dist) / 10 + jx, 2, MAP_W - 3);
        int cy = clampi(tc_y + (COMPASS_DY[dir] * dist) / 10 + jy, 2, MAP_H - 3);
        force_place_patch(gs, cx, cy, RESOURCE_TYPES[i], PATCH_RADII[i], amounts[i]);
    }
}

/* Place tiles in a circle, only overwriting grass. */
static void place_forest_cluster(GameState *gs, int cx, int cy, int radius)
{
    for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
            if (dx * dx + dy * dy > radius * radius) continue;
            int x = cx + dx, y = cy + dy;
            if (!map_in_bounds(x, y)) continue;
            if (gs->map[y][x].type != TILE_GRASS) continue;
            gs->map[y][x].type = TILE_FOREST;
            gs->map[y][x].resource_amt = 150 + rng_range(0, 100);
        }
    }
}

static void place_resource_patch(GameState *gs, int cx, int cy,
                                 TileType type, int radius, int base_amount)
{
    for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
            if (dx * dx + dy * dy > radius * radius) continue;
            int x = cx + dx, y = cy + dy;
            if (!map_in_bounds(x, y)) continue;
            if (gs->map[y][x].type != TILE_GRASS) continue;
            gs->map[y][x].type = type;
            gs->map[y][x].resource_amt = base_amount + rng_range(0, base_amount / 2);
        }
    }
}

/* Elliptical water body. */
static void place_water_body(GameState *gs, int cx, int cy, int half_w, int half_h)
{
    for (int dy = -half_h; dy <= half_h; dy++) {
        for (int dx = -half_w; dx <= half_w; dx++) {
            float nx = (float)dx / half_w;
            float ny = (float)dy / half_h;
            if (nx * nx + ny * ny > 1.0f) continue;
            int x = cx + dx, y = cy + dy;
            if (!map_in_bounds(x, y)) continue;
            gs->map[y][x].type = TILE_WATER;
            gs->map[y][x].resource_amt = 0;
        }
    }
}

/* Stock fish shoals in open water: fish are TILE_WATER tiles that
   carry a resource_amt (food) which fishing ships harvest. */
static void stock_fish_shoals(GameState *gs, int count)
{
    int placed = 0, guard = 0;
    while (placed < count && guard++ < 6000) {
        int x = rng_range(1, MAP_W - 2);
        int y = rng_range(1, MAP_H - 2);
        if (gs->map[y][x].type != TILE_WATER || gs->map[y][x].resource_amt > 0) continue;
        gs->map[y][x].resource_amt = rng_range(240, 360);
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                int nx = x + dx, ny = y + dy;
                if (!map_in_bounds(nx, ny)) continue;
                if (gs->map[ny][nx].type == TILE_WATER && gs->map[ny][nx].resource_amt == 0)
                    gs->map[ny][nx].resource_amt = rng_range(180, 300);
            }
        }
        placed++;
    }
}

/* Overwrite any terrain (used for guaranteed start-area resources). */
static void force_place_patch(GameState *gs, int cx, int cy,
                              TileType type, int radius, int base_amount)
{
    for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
            if (dx * dx + dy * dy > radius * radius) continue;
            int x = cx + dx, y = cy + dy;
            if (!map_in_bounds(x, y)) continue;
            if (gs->map[y][x].building_id >= 0) continue;
            gs->map[y][x].type = type;
            gs->map[y][x].resource_amt = base_amount + rng_range(0, base_amount / 4);
        }
    }
}

/* Clear a square zone to grass (for starter areas). */
static void clear_zone(GameState *gs, int cx, int cy, int radius)
{
    for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
            int x = cx + dx, y = cy + dy;
            if (map_in_bounds(x, y)) {
                gs->map[y][x].type = TILE_GRASS;
            }
        }
    }
}

static bool point_is_safe_from_starts(int x, int y,
                                      const int *start_x, const int *start_y,
                                      int num_players, int safe_radius)
{
    for (int i = 0; i < num_players; i++) {
        if (abs(x - start_x[i]) <= safe_radius &&
            abs(y - start_y[i]) <= safe_radius) {
            return false;
        }
    }
    return true;
}

/* Pick a random point inside the given map quadrant (0=TL, 1=TR, 2=BL, 3=BR). */
static void pick_quadrant_start(int quadrant, int *out_x, int *out_y)
{
    const int margin = 15;
    const int mid_x  = MAP_W / 2;
    const int mid_y  = MAP_H / 2;

    int min_x = (quadrant == 0 || quadrant == 2) ? margin      : mid_x + 8;
    int max_x = (quadrant == 0 || quadrant == 2) ? mid_x - 8   : MAP_W - margin - 1;
    int min_y = (quadrant == 0 || quadrant == 1) ? margin      : mid_y + 8;
    int max_y = (quadrant == 0 || quadrant == 1) ? mid_y - 8   : MAP_H - margin - 1;

    if (max_x < min_x) max_x = min_x;
    if (max_y < min_y) max_y = min_y;

    *out_x = min_x + (int)(rng_next() % (uint32_t)(max_x - min_x + 1));
    *out_y = min_y + (int)(rng_next() % (uint32_t)(max_y - min_y + 1));
}

/* ── Map initialisation ────────────────────────────────────── */

/* Random int in [lo, hi] using the game's RNG. */
#define rrand(lo, hi) ((int)(rng_next() % ((hi) - (lo) + 1)) + (lo))

void map_init(GameState *gs, int *start_x, int *start_y, int num_players)
{
    /* Initialise every tile to grass with hidden fog. */
    for (int y = 0; y < MAP_H; y++) {
        for (int x = 0; x < MAP_W; x++) {
            gs->map[y][x].type         = TILE_GRASS;
            gs->map[y][x].resource_amt = 0;
            gs->map[y][x].building_id  = -1;
            gs->map[y][x].variant      = (uint8_t)(rng_next() % 4);
            for (int p = 0; p < NUM_PLAYERS; p++) {
                gs->map[y][x].fog[p] = FOG_HIDDEN;
            }
        }
    }

    /* ── Assign player start positions ─────────────────────── */
    if (num_players <= 4) {
        /* One per quadrant, shuffled for variety. */
        int quad_x[4], quad_y[4];
        for (int i = 0; i < 4; i++) {
            pick_quadrant_start(i, &quad_x[i], &quad_y[i]);
        }

        int chosen_quads[4] = {0, 3, 1, 2};

        if (num_players <= 1) {
            chosen_quads[0] = (int)(rng_next() % 4);
        } else if (num_players == 2) {
            /* Diagonal opponents for balanced spacing. */
            if ((rng_next() & 1u) == 0u) {
                chosen_quads[0] = 0; chosen_quads[1] = 3;
            } else {
                chosen_quads[0] = 1; chosen_quads[1] = 2;
            }
            if ((rng_next() & 1u) != 0u) {
                int tmp = chosen_quads[0];
                chosen_quads[0] = chosen_quads[1];
                chosen_quads[1] = tmp;
            }
        } else {
            /* 3-4 players: full shuffle of quadrant assignments. */
            int quads[4] = {0, 1, 2, 3};
            for (int i = 3; i > 0; i--) {
                int j = (int)(rng_next() % (uint32_t)(i + 1));
                int tmp = quads[i]; quads[i] = quads[j]; quads[j] = tmp;
            }
            for (int i = 0; i < num_players; i++) {
                chosen_quads[i] = quads[i];
            }
        }

        for (int i = 0; i < num_players; i++) {
            start_x[i] = quad_x[chosen_quads[i]];
            start_y[i] = quad_y[chosen_quads[i]];
        }
    } else {
        /* 5+ players: evenly spaced around a circle. */
        float angle_offset = rng_frac() * 2.0f * 3.14159265f;
        for (int i = 0; i < num_players; i++) {
            float angle = angle_offset + (2.0f * 3.14159265f * i) / num_players;
            start_x[i] = clampi(32 + (int)(20.0f * det_cosf(angle)), 8, MAP_W - 9);
            start_y[i] = clampi(32 + (int)(20.0f * det_sinf(angle)), 8, MAP_H - 9);
        }
    }

    const int START_SAFE_RADIUS = 10;

    /* ── Scatter natural features ──────────────────────────── */
    int num_lakes = rrand(2, 4);
    for (int i = 0; i < num_lakes; i++) {
        int cx, cy, tries = 0;
        do {
            cx = rrand(12, MAP_W - 13);
            cy = rrand(12, MAP_H - 13);
            tries++;
        } while (!point_is_safe_from_starts(cx, cy, start_x, start_y, num_players, START_SAFE_RADIUS) && tries < 20);
        place_water_body(gs, cx, cy, rrand(3, 7), rrand(2, 5));
    }

    int num_forests = rrand(12, 18);
    for (int i = 0; i < num_forests; i++) {
        int cx, cy, tries = 0;
        do {
            cx = rrand(4, MAP_W - 5);
            cy = rrand(4, MAP_H - 5);
            tries++;
        } while (!point_is_safe_from_starts(cx, cy, start_x, start_y, num_players, START_SAFE_RADIUS) && tries < 20);
        place_forest_cluster(gs, cx, cy, rrand(2, 4));
    }

    int num_gold = rrand(5, 8);
    for (int i = 0; i < num_gold; i++) {
        int cx, cy, tries = 0;
        do {
            cx = rrand(4, MAP_W - 5);
            cy = rrand(4, MAP_H - 5);
            tries++;
        } while (!point_is_safe_from_starts(cx, cy, start_x, start_y, num_players, START_SAFE_RADIUS) && tries < 20);
        place_resource_patch(gs, cx, cy, TILE_GOLD, rrand(1, 2), rrand(600, 900));
    }

    int num_stone = rrand(4, 6);
    for (int i = 0; i < num_stone; i++) {
        int cx, cy, tries = 0;
        do {
            cx = rrand(4, MAP_W - 5);
            cy = rrand(4, MAP_H - 5);
            tries++;
        } while (!point_is_safe_from_starts(cx, cy, start_x, start_y, num_players, START_SAFE_RADIUS) && tries < 20);
        place_resource_patch(gs, cx, cy, TILE_STONE, rrand(1, 2), rrand(500, 800));
    }

    int num_berries = rrand(4, 6);
    for (int i = 0; i < num_berries; i++) {
        int cx, cy, tries = 0;
        do {
            cx = rrand(4, MAP_W - 5);
            cy = rrand(4, MAP_H - 5);
            tries++;
        } while (!point_is_safe_from_starts(cx, cy, start_x, start_y, num_players, START_SAFE_RADIUS) && tries < 20);
        place_resource_patch(gs, cx, cy, TILE_BERRIES, 1, rrand(400, 600));
    }

    /* Guarantee coastal access: a small lake a short sail from each
       base (beyond the cleared start zone) so docks are always
       buildable.  Placed before start clearing so the 5-tile clear
       never eats into it. */
    for (int i = 0; i < num_players; i++) {
        int dirx = (start_x[i] < MAP_W / 2) ? 1 : -1;
        int diry = (start_y[i] < MAP_H / 2) ? 1 : -1;
        int lx = clampi(start_x[i] + dirx * 8, 6, MAP_W - 7);
        int ly = clampi(start_y[i] + diry * 8, 6, MAP_H - 7);
        place_water_body(gs, lx, ly, 4, 3);
    }

    /* Populate all water (natural lakes + guaranteed ones) with fish. */
    stock_fish_shoals(gs, 16);

    /* Clear starter zones and place each player's guaranteed resources. */
    for (int i = 0; i < num_players; i++) {
        clear_zone(gs, start_x[i], start_y[i], 5);
        place_start_resources(gs, start_x[i], start_y[i]);
    }
}

#undef rrand

/* ── Fog of war ────────────────────────────────────────────── */

static const int BUILDING_VISION_RANGE = 4;

/* Mark all tiles within a circular area as visible for `player`. */
static void reveal_circle(GameState *gs, int center_x, int center_y,
                          int radius, int player)
{
    for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
            if (dx * dx + dy * dy > radius * radius) continue;
            int tx = center_x + dx;
            int ty = center_y + dy;
            if (map_in_bounds(tx, ty)) {
                gs->map[ty][tx].fog[player] = FOG_VISIBLE;
            }
        }
    }
}

/* Parallel-for jobs: rows are disjoint between chunks, and the
   reveal pass partitions by player so each thread only touches
   its own fog[p] bytes — results are scheduling-independent. */
static void fog_fill_visible_rows(int y0, int y1, void *ctx)
{
    GameState *gs = ctx;
    for (int y = y0; y < y1; y++)
        for (int x = 0; x < MAP_W; x++)
            for (int p = 0; p < NUM_PLAYERS; p++)
                gs->map[y][x].fog[p] = FOG_VISIBLE;
}

static void fog_demote_rows(int y0, int y1, void *ctx)
{
    GameState *gs = ctx;
    for (int y = y0; y < y1; y++) {
        for (int x = 0; x < MAP_W; x++) {
            for (int p = 0; p < NUM_PLAYERS; p++) {
                if (gs->map[y][x].fog[p] == FOG_VISIBLE) {
                    gs->map[y][x].fog[p] = FOG_EXPLORED;
                }
            }
        }
    }
}

static void fog_reveal_players(int p0, int p1, void *ctx)
{
    GameState *gs = ctx;
    for (int p = p0; p < p1; p++) {
        /* Reveal tiles around each of this player's active units. */
        for (int i = 0; i < MAX_UNITS; i++) {
            Unit *unit = &gs->units[i];
            if (!unit->active || unit->player != p || unit->state == US_DEAD) continue;
            int ux = (int)(unit->wx / TILE_SIZE);
            int uy = (int)(unit->wy / TILE_SIZE);
            reveal_circle(gs, ux, uy, (int)unit->vision_range, p);
        }

        /* Reveal tiles around each of this player's completed buildings. */
        for (int i = 0; i < MAX_BUILDINGS; i++) {
            Building *bld = &gs->buildings[i];
            if (!bld->active || bld->player != p || !bld->complete) continue;
            int bx = bld->tx + bld->tw / 2;
            int by = bld->ty + bld->th / 2;
            reveal_circle(gs, bx, by, BUILDING_VISION_RANGE, p);
        }
    }
}

void map_update_fog(GameState *gs)
{
    /* Sandbox: everything always visible. */
    if (gs->mode == GAME_MODE_SANDBOX) {
        tp_parallel_for(MAP_H, fog_fill_visible_rows, gs);
        return;
    }

    /* Demote VISIBLE → EXPLORED (fog re-covers unseen tiles). */
    tp_parallel_for(MAP_H, fog_demote_rows, gs);

    /* Reveal around units and buildings, one fog channel per job item. */
    tp_parallel_for(NUM_PLAYERS, fog_reveal_players, gs);
}

/* ── Resource search ───────────────────────────────────────── */

int map_find_resource(GameState *gs, int player, ResType res,
                      int near_tx, int near_ty, int *out_x, int *out_y)
{
    (void)player;

    TileType wanted_type;
    switch (res) {
        case RES_FOOD:  wanted_type = TILE_BERRIES; break;
        case RES_WOOD:  wanted_type = TILE_FOREST;  break;
        case RES_GOLD:  wanted_type = TILE_GOLD;    break;
        case RES_STONE: wanted_type = TILE_STONE;   break;
        default: return 0;
    }

    int best_distance = 9999;
    int found = 0;

    for (int y = 0; y < MAP_H; y++) {
        for (int x = 0; x < MAP_W; x++) {
            Tile *tile = &gs->map[y][x];
            bool matches = (tile->type == wanted_type) ||
                           (res == RES_FOOD && tile->type == TILE_FARM);
            if (!matches || tile->resource_amt <= 0) continue;

            int dist = abs(x - near_tx) + abs(y - near_ty);
            if (dist < best_distance) {
                best_distance = dist;
                *out_x = x;
                *out_y = y;
                found = 1;
            }
        }
    }
    return found;
}

int map_find_dropoff(GameState *gs, int player, ResType res,
                     float world_x, float world_y, int *out_tx, int *out_ty)
{
    float best_distance = 1e30f;
    int found = 0;

    for (int i = 0; i < MAX_BUILDINGS; i++) {
        Building *bld = &gs->buildings[i];
        if (!bld->active || !bld->complete || bld->player != player) continue;

        /* Check if this building type accepts this resource. */
        bool accepts = false;
        switch (res) {
            case RES_FOOD:  accepts = (bld->type == BLD_TOWN_CENTER || bld->type == BLD_MILL); break;
            case RES_WOOD:  accepts = (bld->type == BLD_TOWN_CENTER || bld->type == BLD_LUMBER_CAMP); break;
            case RES_GOLD:
            case RES_STONE: accepts = (bld->type == BLD_TOWN_CENTER || bld->type == BLD_MINING_CAMP); break;
            default: break;
        }
        if (!accepts) continue;

        float bld_wx = (bld->tx + bld->tw / 2.0f) * TILE_SIZE;
        float bld_wy = (bld->ty + bld->th / 2.0f) * TILE_SIZE;
        float dist = dist2f(world_x, world_y, bld_wx, bld_wy);

        if (dist < best_distance) {
            best_distance = dist;
            *out_tx = bld->tx + bld->tw / 2;
            *out_ty = bld->ty + bld->th / 2;
            found = 1;
        }
    }
    return found;
}

int map_find_passable_near_dom(GameState *gs, int tx, int ty, bool water,
                               int *out_x, int *out_y)
{
    if (map_is_passable_dom(gs, tx, ty, water)) {
        *out_x = tx;
        *out_y = ty;
        return 1;
    }

    /* Expand outward in rings until a passable tile is found.  Ships
       get a wider search so a click near the shoreline still snaps
       to open water. */
    int max_radius = water ? 6 : 3;
    for (int radius = 1; radius < max_radius; radius++) {
        for (int dy = -radius; dy <= radius; dy++) {
            for (int dx = -radius; dx <= radius; dx++) {
                if (abs(dx) != radius && abs(dy) != radius) continue;
                int nx = tx + dx, ny = ty + dy;
                if (map_is_passable_dom(gs, nx, ny, water)) {
                    *out_x = nx;
                    *out_y = ny;
                    return 1;
                }
            }
        }
    }
    return 0;
}

int map_find_passable_near(GameState *gs, int tx, int ty, int *out_x, int *out_y)
{
    return map_find_passable_near_dom(gs, tx, ty, false, out_x, out_y);
}
