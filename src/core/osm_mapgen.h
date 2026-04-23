#pragma once
#include "game.h"
#include <stdbool.h>

/* Fetch OSM data for a location and generate a playable map.
 * Returns true on success, false on network/parse error.
 * On failure, the map is left in a valid (all-grass) state so the
 * caller can fall back to standard random generation. */
bool osm_generate_map(GameState *gs, const char *location_name,
                      int num_players, int *start_x, int *start_y);
