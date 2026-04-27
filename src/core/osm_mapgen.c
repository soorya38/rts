/*=============================================================
 * osm_mapgen.c – Procedural RTS map from OpenStreetMap data
 *=============================================================*/
#include "game.h"
#include "osm_mapgen.h"
#include <stdio.h>

#if defined(RTS_HAS_CURL)
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>
#include <curl/curl.h>
#include <math.h>

/* ─── HTTP response buffer ─────────────────────────────────── */
typedef struct { char *data; size_t len; } HttpBuf;

static size_t http_write_cb(void *ptr, size_t size, size_t nmemb, void *ud) {
    size_t total = size * nmemb;
    HttpBuf *buf = (HttpBuf *)ud;
    char *tmp = realloc(buf->data, buf->len + total + 1);
    if (!tmp) return 0;
    buf->data = tmp;
    memcpy(buf->data + buf->len, ptr, total);
    buf->len += total;
    buf->data[buf->len] = '\0';
    return total;
}

static char *http_get(const char *url) {
    CURL *c = curl_easy_init();
    if (!c) return NULL;
    HttpBuf buf = {NULL, 0};
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, http_write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "RTS-MapGen/1.0");
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    CURLcode res = curl_easy_perform(c);
    curl_easy_cleanup(c);
    if (res != CURLE_OK) { free(buf.data); return NULL; }
    return buf.data;
}

static char *http_post(const char *url, const char *postdata) {
    CURL *c = curl_easy_init();
    if (!c) return NULL;
    HttpBuf buf = {NULL, 0};
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, postdata);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, http_write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "RTS-MapGen/1.0");
    CURLcode res = curl_easy_perform(c);
    curl_easy_cleanup(c);
    if (res != CURLE_OK) { free(buf.data); return NULL; }
    return buf.data;
}

/* ─── Geo helpers ──────────────────────────────────────────── */
typedef struct { double south, west, north, east; } BBox;
typedef struct { double lat, lon; } GeoPoint;

#define MAX_WAY_NODES 512
#define MAX_WAYS      512
typedef struct {
    GeoPoint nodes[MAX_WAY_NODES];
    int count;
    int kind; /* 0=water, 1=forest, 2=road, 3=river, 4=sand, 5=farm, 6=residential, 7=building */
} Way;

typedef struct {
    Way ways[MAX_WAYS];
    int way_count;
    BBox bbox;
} OsmData;

/* ─── Stage 1: Geocode location ────────────────────────────── */
static bool geocode(const char *name, double *lat, double *lon, BBox *bbox) {
    char url[512];
    char encoded[256];
    /* Simple URL encoding for spaces */
    int ei = 0;
    for (int i = 0; name[i] && ei < 250; i++) {
        if (name[i] == ' ') { encoded[ei++] = '+'; }
        else { encoded[ei++] = name[i]; }
    }
    encoded[ei] = '\0';

    snprintf(url, sizeof(url),
        "https://nominatim.openstreetmap.org/search?q=%s&format=json&limit=1",
        encoded);

    printf("OSM MapGen: Geocoding '%s'...\n", name);
    char *resp = http_get(url);
    if (!resp) { printf("OSM MapGen: Geocode HTTP failed\n"); return false; }

    cJSON *arr = cJSON_Parse(resp);
    free(resp);
    if (!arr || !cJSON_IsArray(arr) || cJSON_GetArraySize(arr) == 0) {
        printf("OSM MapGen: No results for '%s'\n", name);
        cJSON_Delete(arr);
        return false;
    }

    cJSON *item = cJSON_GetArrayItem(arr, 0);
    cJSON *jlat = cJSON_GetObjectItem(item, "lat");
    cJSON *jlon = cJSON_GetObjectItem(item, "lon");
    cJSON *jbb  = cJSON_GetObjectItem(item, "boundingbox");

    if (!jlat || !jlon) { cJSON_Delete(arr); return false; }
    *lat = atof(jlat->valuestring);
    *lon = atof(jlon->valuestring);

    if (jbb && cJSON_GetArraySize(jbb) == 4) {
        bbox->south = atof(cJSON_GetArrayItem(jbb, 0)->valuestring);
        bbox->north = atof(cJSON_GetArrayItem(jbb, 1)->valuestring);
        bbox->west  = atof(cJSON_GetArrayItem(jbb, 2)->valuestring);
        bbox->east  = atof(cJSON_GetArrayItem(jbb, 3)->valuestring);
    } else {
        /* Fallback: ~2km square */
        bbox->south = *lat - 0.01;
        bbox->north = *lat + 0.01;
        bbox->west  = *lon - 0.015;
        bbox->east  = *lon + 0.015;
    }

    /* Clamp to small region */
    double dlat = bbox->north - bbox->south;
    double dlon = bbox->east - bbox->west;
    if (dlat > 0.04) { bbox->south = *lat - 0.02; bbox->north = *lat + 0.02; }
    if (dlon > 0.06) { bbox->west = *lon - 0.03; bbox->east = *lon + 0.03; }

    printf("OSM MapGen: Found at %.4f,%.4f  bbox=[%.4f,%.4f,%.4f,%.4f]\n",
           *lat, *lon, bbox->south, bbox->west, bbox->north, bbox->east);
    cJSON_Delete(arr);
    return true;
}

/* ─── Stage 2: Fetch OSM features ──────────────────────────── */
static void parse_way_geometry(cJSON *geom, Way *w) {
    w->count = 0;
    if (!geom || !cJSON_IsArray(geom)) return;
    int n = cJSON_GetArraySize(geom);
    for (int i = 0; i < n && w->count < MAX_WAY_NODES; i++) {
        cJSON *pt = cJSON_GetArrayItem(geom, i);
        cJSON *jlat = cJSON_GetObjectItem(pt, "lat");
        cJSON *jlon = cJSON_GetObjectItem(pt, "lon");
        if (jlat && jlon) {
            w->nodes[w->count].lat = jlat->valuedouble;
            w->nodes[w->count].lon = jlon->valuedouble;
            w->count++;
        }
    }
}

static bool fetch_features(BBox *bbox, OsmData *data) {
    char query[2048];
    snprintf(query, sizeof(query),
        "data=[out:json][timeout:25];"
        "("
        "way[\"waterway\"](%.6f,%.6f,%.6f,%.6f);"
        "way[\"natural\"=\"water\"](%.6f,%.6f,%.6f,%.6f);"
        "way[\"natural\"=\"wood\"](%.6f,%.6f,%.6f,%.6f);"
        "way[\"landuse\"=\"forest\"](%.6f,%.6f,%.6f,%.6f);"
        "way[\"highway\"](%.6f,%.6f,%.6f,%.6f);"
        "way[\"natural\"=\"sand\"](%.6f,%.6f,%.6f,%.6f);"
        "way[\"natural\"=\"beach\"](%.6f,%.6f,%.6f,%.6f);"
        "way[\"landuse\"=\"farmland\"](%.6f,%.6f,%.6f,%.6f);"
        "way[\"landuse\"=\"residential\"](%.6f,%.6f,%.6f,%.6f);"
        ");out geom;",
        bbox->south, bbox->west, bbox->north, bbox->east,
        bbox->south, bbox->west, bbox->north, bbox->east,
        bbox->south, bbox->west, bbox->north, bbox->east,
        bbox->south, bbox->west, bbox->north, bbox->east,
        bbox->south, bbox->west, bbox->north, bbox->east,
        bbox->south, bbox->west, bbox->north, bbox->east,
        bbox->south, bbox->west, bbox->north, bbox->east,
        bbox->south, bbox->west, bbox->north, bbox->east,
        bbox->south, bbox->west, bbox->north, bbox->east);

    printf("OSM MapGen: Fetching features from Overpass...\n");
    char *resp = http_post("https://overpass-api.de/api/interpreter", query);
    if (!resp) { printf("OSM MapGen: Overpass HTTP failed\n"); return false; }

    cJSON *root = cJSON_Parse(resp);
    free(resp);
    if (!root) { printf("OSM MapGen: JSON parse failed\n"); return false; }

    cJSON *elements = cJSON_GetObjectItem(root, "elements");
    if (!elements) { cJSON_Delete(root); return false; }

    data->way_count = 0;
    data->bbox = *bbox;
    int n = cJSON_GetArraySize(elements);
    for (int i = 0; i < n && data->way_count < MAX_WAYS; i++) {
        cJSON *el = cJSON_GetArrayItem(elements, i);
        cJSON *tags = cJSON_GetObjectItem(el, "tags");
        cJSON *geom = cJSON_GetObjectItem(el, "geometry");
        if (!tags || !geom) continue;

        Way *w = &data->ways[data->way_count];
        memset(w, 0, sizeof(Way));

        if (cJSON_GetObjectItem(tags, "waterway")) w->kind = 3;
        else if (cJSON_GetObjectItem(tags, "natural")) {
            cJSON *nat = cJSON_GetObjectItem(tags, "natural");
            if (nat && nat->valuestring) {
                if (strcmp(nat->valuestring, "water") == 0) w->kind = 0;
                else if (strcmp(nat->valuestring, "wood") == 0) w->kind = 1;
                else if (strcmp(nat->valuestring, "sand") == 0 ||
                         strcmp(nat->valuestring, "beach") == 0) w->kind = 4;
                else continue;
            } else continue;
        }
        else if (cJSON_GetObjectItem(tags, "landuse")) {
            cJSON *lu = cJSON_GetObjectItem(tags, "landuse");
            if (lu && lu->valuestring) {
                if (strcmp(lu->valuestring, "forest") == 0) w->kind = 1;
                else if (strcmp(lu->valuestring, "farmland") == 0) w->kind = 5;
                else if (strcmp(lu->valuestring, "residential") == 0) w->kind = 6;
                else continue;
            } else continue;
        }
        else if (cJSON_GetObjectItem(tags, "highway")) w->kind = 2;
        else continue;

        parse_way_geometry(geom, w);
        if (w->count >= 2) data->way_count++;
    }

    printf("OSM MapGen: Parsed %d features\n", data->way_count);
    cJSON_Delete(root);
    return true;
}

/* ─── Stage 3: Rasterize to tile grid ──────────────────────── */

/* Point-in-polygon (ray casting) for closed ways */
static bool point_in_way(Way *w, double lat, double lon) {
    if (w->count < 3) return false;
    bool inside = false;
    int j = w->count - 1;
    for (int i = 0; i < w->count; i++) {
        double yi = w->nodes[i].lat, xi = w->nodes[i].lon;
        double yj = w->nodes[j].lat, xj = w->nodes[j].lon;
        if (((yi > lat) != (yj > lat)) &&
            (lon < (xj - xi) * (lat - yi) / (yj - yi) + xi))
            inside = !inside;
        j = i;
    }
    return inside;
}

/* Distance from point to line segment (in geo coords, approx) */
static double point_line_dist(double px, double py, double ax, double ay, double bx, double by) {
    double dx = bx - ax, dy = by - ay;
    double len2 = dx*dx + dy*dy;
    if (len2 < 1e-12) return sqrt((px-ax)*(px-ax) + (py-ay)*(py-ay));
    double t = ((px-ax)*dx + (py-ay)*dy) / len2;
    if (t < 0) t = 0; if (t > 1) t = 1;
    double cx = ax + t*dx, cy = ay + t*dy;
    return sqrt((px-cx)*(px-cx) + (py-cy)*(py-cy));
}

static bool point_near_way(Way *w, double lat, double lon, double threshold) {
    for (int i = 0; i < w->count - 1; i++) {
        double d = point_line_dist(lon, lat,
            w->nodes[i].lon, w->nodes[i].lat,
            w->nodes[i+1].lon, w->nodes[i+1].lat);
        if (d < threshold) return true;
    }
    return false;
}

static bool tile_can_be_forest_filled(TileType t) {
    return t == TILE_GRASS || t == TILE_DESERT;
}

static int count_forest_neighbors(GameState *gs, int x, int y, int radius) {
    int count = 0;
    for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
            if (dx == 0 && dy == 0) continue;
            int nx = x + dx;
            int ny = y + dy;
            if (map_in_bounds(nx, ny) && gs->map[ny][nx].type == TILE_FOREST)
                count++;
        }
    }
    return count;
}

static void set_forest_tile(GameState *gs, int x, int y, int *added) {
    if (!map_in_bounds(x, y)) return;
    if (!tile_can_be_forest_filled(gs->map[y][x].type)) return;
    gs->map[y][x].type = TILE_FOREST;
    gs->map[y][x].resource_amt = 150 + rng_range(0, 100);
    if (added) (*added)++;
}

static void grow_forest_blob(GameState *gs, int sx, int sy, int budget, int *added) {
    int cx = sx;
    int cy = sy;
    int remaining = budget;
    int stall_steps = 0;
    int max_steps = budget * 12 + 32;

    while (remaining > 0 && max_steps-- > 0) {
        int before_remaining = remaining;
        int radius = 2 + (int)(rng_next() % 3);
        for (int dy = -radius; dy <= radius && remaining > 0; dy++) {
            for (int dx = -radius; dx <= radius && remaining > 0; dx++) {
                int x = cx + dx;
                int y = cy + dy;
                if (!map_in_bounds(x, y)) continue;
                if (!tile_can_be_forest_filled(gs->map[y][x].type)) continue;

                float fx = (float)dx / (float)radius;
                float fy = (float)dy / (float)radius;
                float dist = fx * fx + fy * fy;
                if (dist > 1.15f) continue;

                int nearby = count_forest_neighbors(gs, x, y, 1);
                unsigned hash = (unsigned)(x * 1103515245u + y * 12345u + remaining * 97u);
                int threshold = nearby >= 4 ? 92 : nearby >= 2 ? 75 : 58;
                if ((int)(hash % 100u) > threshold) continue;

                bool was_fillable = tile_can_be_forest_filled(gs->map[y][x].type);
                set_forest_tile(gs, x, y, added);
                if (was_fillable)
                    remaining--;
            }
        }

        if (remaining == before_remaining) {
            stall_steps++;
            if (stall_steps >= 6) break;
        } else {
            stall_steps = 0;
        }

        cx += (int)(rng_next() % 5) - 2;
        cy += (int)(rng_next() % 5) - 2;
        cx = clampi(cx, 2, MAP_W - 3);
        cy = clampi(cy, 2, MAP_H - 3);

        if (!tile_can_be_forest_filled(gs->map[cy][cx].type)) {
            for (int r = 1; r <= 6; r++) {
                bool found = false;
                for (int dy = -r; dy <= r && !found; dy++) {
                    for (int dx = -r; dx <= r; dx++) {
                        int nx = cx + dx;
                        int ny = cy + dy;
                        if (!map_in_bounds(nx, ny)) continue;
                        if (!tile_can_be_forest_filled(gs->map[ny][nx].type)) continue;
                        cx = nx;
                        cy = ny;
                        found = true;
                        break;
                    }
                }
                if (found) break;
            }
        }
    }
}

static void smooth_forest_shapes(GameState *gs) {
    TileType next_types[MAP_H][MAP_W];
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++)
            next_types[y][x] = gs->map[y][x].type;

    for (int y = 1; y < MAP_H - 1; y++) {
        for (int x = 1; x < MAP_W - 1; x++) {
            TileType t = gs->map[y][x].type;
            int neighbors = count_forest_neighbors(gs, x, y, 1);

            if (t == TILE_FOREST && neighbors <= 1) {
                next_types[y][x] = TILE_GRASS;
            } else if (tile_can_be_forest_filled(t) && neighbors >= 5) {
                next_types[y][x] = TILE_FOREST;
            }
        }
    }

    for (int y = 1; y < MAP_H - 1; y++) {
        for (int x = 1; x < MAP_W - 1; x++) {
            if (next_types[y][x] == TILE_FOREST && gs->map[y][x].type != TILE_FOREST) {
                gs->map[y][x].type = TILE_FOREST;
                gs->map[y][x].resource_amt = 150 + rng_range(0, 100);
            } else if (next_types[y][x] != TILE_FOREST && gs->map[y][x].type == TILE_FOREST) {
                gs->map[y][x].type = next_types[y][x];
                gs->map[y][x].resource_amt = 0;
            }
        }
    }
}

static void fill_forest_cover(GameState *gs, int target_percent) {
    int current_forest = 0;
    int coverable_tiles = 0;

    for (int y = 0; y < MAP_H; y++) {
        for (int x = 0; x < MAP_W; x++) {
            TileType t = gs->map[y][x].type;
            if (t == TILE_FOREST) {
                current_forest++;
                coverable_tiles++;
            } else if (tile_can_be_forest_filled(t)) {
                coverable_tiles++;
            }
        }
    }

    if (coverable_tiles == 0) return;

    int target_forest = (coverable_tiles * target_percent + 50) / 100;
    int remaining = target_forest - current_forest;
    if (remaining <= 0) return;

    int blob_attempts = 10 + remaining / 50;
    for (int attempt = 0; attempt < blob_attempts && remaining > 0; attempt++) {
        int sx = (int)(rng_next() % MAP_W);
        int sy = (int)(rng_next() % MAP_H);

        if (!tile_can_be_forest_filled(gs->map[sy][sx].type)) {
            bool found = false;
            for (int r = 1; r <= 8 && !found; r++) {
                for (int dy = -r; dy <= r && !found; dy++) {
                    for (int dx = -r; dx <= r; dx++) {
                        int nx = sx + dx;
                        int ny = sy + dy;
                        if (!map_in_bounds(nx, ny)) continue;
                        if (!tile_can_be_forest_filled(gs->map[ny][nx].type)) continue;
                        sx = nx;
                        sy = ny;
                        found = true;
                        break;
                    }
                }
            }
            if (!found) continue;
        }

        int blob_budget = 10 + (int)(rng_next() % 28);
        if (blob_budget > remaining) blob_budget = remaining;
        int added = 0;
        grow_forest_blob(gs, sx, sy, blob_budget, &added);
        remaining -= added;
    }

    smooth_forest_shapes(gs);

    if (remaining > 0) {
        for (int y = 0; y < MAP_H && remaining > 0; y++) {
            for (int x = 0; x < MAP_W && remaining > 0; x++) {
                if (!tile_can_be_forest_filled(gs->map[y][x].type)) continue;
                if (count_forest_neighbors(gs, x, y, 1) < 2) continue;
                set_forest_tile(gs, x, y, NULL);
                remaining--;
            }
        }
    }
}

static void rasterize(GameState *gs, OsmData *data) {
    BBox *bb = &data->bbox;
    double tile_w = (bb->east - bb->west) / MAP_W;
    double tile_h = (bb->north - bb->south) / MAP_H;
    double road_thresh = tile_w * 0.8;
    double river_thresh = tile_w * 1.5;

    /* For each tile, check all features */
    for (int y = 0; y < MAP_H; y++) {
        for (int x = 0; x < MAP_W; x++) {
            double lon = bb->west + (x + 0.5) * tile_w;
            double lat = bb->north - (y + 0.5) * tile_h;
            TileType best = TILE_GRASS;
            int priority = 0;

            for (int w = 0; w < data->way_count; w++) {
                Way *way = &data->ways[w];
                bool hit = false;
                int pri = 0;

                switch (way->kind) {
                    case 0: /* water polygon */
                        hit = point_in_way(way, lat, lon);
                        pri = 10;
                        if (hit) best = TILE_WATER;
                        break;
                    case 1: /* forest */
                        hit = point_in_way(way, lat, lon);
                        pri = 5;
                        if (hit && pri > priority) { best = TILE_FOREST; priority = pri; }
                        break;
                    case 2: /* road */
                        hit = point_near_way(way, lat, lon, road_thresh);
                        pri = 3;
                        if (hit && pri > priority) {
                            best = TILE_ROAD;
                            priority = pri;
                        }
                        break;
                    case 3: /* river */
                        hit = point_near_way(way, lat, lon, river_thresh);
                        pri = 9;
                        if (hit) best = TILE_WATER;
                        break;
                    case 4: /* sand */
                        hit = point_in_way(way, lat, lon);
                        pri = 4;
                        if (hit && pri > priority) { best = TILE_DESERT; priority = pri; }
                        break;
                    case 5: /* farmland - keep as grass with berries marker */
                        hit = point_in_way(way, lat, lon);
                        pri = 2;
                        if (hit && pri > priority) { priority = pri; } /* stays grass */
                        break;
                    case 6: /* residential */
                        hit = point_in_way(way, lat, lon);
                        pri = 1;
                        break;
                    default: break;
                }
                if (best == TILE_WATER && pri >= 9) break; /* water wins */
            }

            if (best != TILE_GRASS) {
                gs->map[y][x].type = best;
                if (best == TILE_FOREST)
                    gs->map[y][x].resource_amt = 150 + rng_range(0, 100);
            }
        }
    }

    /* Smoothing: remove isolated single-tile water */
    for (int y = 1; y < MAP_H-1; y++) {
        for (int x = 1; x < MAP_W-1; x++) {
            if (gs->map[y][x].type == TILE_WATER) {
                int water_neighbors = 0;
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++)
                        if ((dx||dy) && gs->map[y+dy][x+dx].type == TILE_WATER)
                            water_neighbors++;
                if (water_neighbors < 2)
                    gs->map[y][x].type = TILE_GRASS;
            }
        }
    }

    /* Cover roughly 60% of open land with trees instead of limiting tree placement to roads. */
    fill_forest_cover(gs, 60);

    /* Ensure at least 40% passable */
    int passable = 0;
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++) {
            TileType t = gs->map[y][x].type;
            if (t == TILE_GRASS || t == TILE_DESERT || t == TILE_ROAD || t == TILE_FARM)
                passable++;
        }
    if (passable < (MAP_W * MAP_H * 40 / 100)) {
        /* Trim excess water/forest from edges */
        for (int y = 0; y < MAP_H && passable < (MAP_W*MAP_H*45/100); y++)
            for (int x = 0; x < MAP_W && passable < (MAP_W*MAP_H*45/100); x++) {
                if ((gs->map[y][x].type == TILE_WATER || gs->map[y][x].type == TILE_FOREST)
                    && (rng_next() % 3 == 0)) {
                    gs->map[y][x].type = TILE_GRASS;
                    passable++;
                }
            }
    }
}

/* ─── Stage 4: Gameplay layer ──────────────────────────────── */

/* Try to place a building near a target tile, searching outward */
static int try_place_near(GameState *gs, int player, BldType type, int cx, int cy, int search_r) {
    int tw = building_tw(type), th = building_th(type);
    for (int r = 0; r <= search_r; r++) {
        for (int dy = -r; dy <= r; dy++) {
            for (int dx = -r; dx <= r; dx++) {
                if (abs(dx) != r && abs(dy) != r) continue;
                int tx = cx + dx, ty = cy + dy;
                if (!map_in_bounds(tx, ty) || !map_in_bounds(tx+tw-1, ty+th-1)) continue;
                if (map_is_buildable(gs, tx, ty, tw, th)) {
                    return building_place_ready(gs, player, type, tx, ty);
                }
            }
        }
    }
    return -1;
}

static void place_gameplay(GameState *gs, int num_players, int *start_x, int *start_y) {
    /* Pick spawn points in opposing corners, ensuring passable terrain */
    int corners[4][2] = {{8,8}, {MAP_W-8,8}, {8,MAP_H-8}, {MAP_W-8,MAP_H-8}};
    int chosen[4] = {0, 3, 1, 2};

    for (int p = 0; p < num_players && p < 4; p++) {
        int cx = corners[chosen[p]][0];
        int cy = corners[chosen[p]][1];
        for (int r = 0; r < 15; r++) {
            for (int dy = -r; dy <= r; dy++) {
                for (int dx = -r; dx <= r; dx++) {
                    int tx = cx+dx, ty = cy+dy;
                    if (!map_in_bounds(tx, ty)) continue;
                    TileType t = gs->map[ty][tx].type;
                    if (t == TILE_GRASS || t == TILE_DESERT || t == TILE_ROAD) {
                        start_x[p] = tx; start_y[p] = ty;
                        goto found;
                    }
                }
            }
        }
        start_x[p] = cx; start_y[p] = cy;
        found:;
    }

    /* Clear spawn zones */
    for (int p = 0; p < num_players; p++) {
        for (int dy = -6; dy <= 6; dy++)
            for (int dx = -6; dx <= 6; dx++) {
                int tx = start_x[p]+dx, ty = start_y[p]+dy;
                if (map_in_bounds(tx, ty)) {
                    gs->map[ty][tx].type = TILE_GRASS;
                    gs->map[ty][tx].resource_amt = 0;
                }
            }
    }

    /* Place starting resources */
    static const int DX8[8] = { 10,  7,  0, -7, -10, -7,  0,  7 };
    static const int DY8[8] = {  0,  7, 10,  7,   0, -7, -10, -7 };
    TileType res_types[7] = {TILE_FOREST, TILE_FOREST, TILE_FOREST,
                             TILE_BERRIES, TILE_BERRIES, TILE_GOLD, TILE_STONE};
    int res_amts[7] = {150, 150, 150, 450, 450, 700, 600};
    int res_radii[7] = {2, 2, 2, 1, 1, 1, 1};

    for (int p = 0; p < num_players; p++) {
        int idx[8] = {0,1,2,3,4,5,6,7};
        for (int i = 7; i > 0; i--) {
            int j = (int)(rng_next() % (unsigned)(i+1));
            int tmp = idx[i]; idx[i] = idx[j]; idx[j] = tmp;
        }
        for (int i = 0; i < 7; i++) {
            int d = idx[i];
            int dist = 10 + (int)(rng_next() % 5);
            int rcx = start_x[p] + (DX8[d] * dist) / 10;
            int rcy = start_y[p] + (DY8[d] * dist) / 10;
            rcx = clampi(rcx, 2, MAP_W-3);
            rcy = clampi(rcy, 2, MAP_H-3);
            int rr = res_radii[i];
            for (int dy2 = -rr; dy2 <= rr; dy2++)
                for (int dx2 = -rr; dx2 <= rr; dx2++) {
                    if (dx2*dx2+dy2*dy2 > rr*rr) continue;
                    int tx = rcx+dx2, ty = rcy+dy2;
                    if (!map_in_bounds(tx, ty)) continue;
                    if (gs->map[ty][tx].building_id >= 0) continue;
                    gs->map[ty][tx].type = res_types[i];
                    gs->map[ty][tx].resource_amt = res_amts[i] + rng_range(0, 50);
                }
        }
    }

    /* ── Place buildings around each player's base ── */
    for (int p = 0; p < num_players; p++) {
        int sx = start_x[p], sy = start_y[p];

        /* Houses (4 around the TC) */
        try_place_near(gs, p, BLD_HOUSE, sx - 4, sy - 2, 4);
        try_place_near(gs, p, BLD_HOUSE, sx + 4, sy - 2, 4);
        try_place_near(gs, p, BLD_HOUSE, sx - 4, sy + 3, 4);
        try_place_near(gs, p, BLD_HOUSE, sx + 4, sy + 3, 4);

        /* Military district (offset from TC) */
        try_place_near(gs, p, BLD_BARRACKS,      sx - 6, sy + 5, 5);
        try_place_near(gs, p, BLD_ARCHERY_RANGE,  sx - 2, sy + 6, 5);
        try_place_near(gs, p, BLD_STABLE,          sx + 3, sy + 6, 5);

        /* Economy */
        try_place_near(gs, p, BLD_MILL,        sx + 5, sy - 4, 5);
        try_place_near(gs, p, BLD_LUMBER_CAMP, sx - 6, sy - 4, 5);
        try_place_near(gs, p, BLD_MINING_CAMP, sx + 6, sy + 1, 5);

        /* Advanced buildings */
        try_place_near(gs, p, BLD_BLACKSMITH,  sx - 3, sy - 5, 5);
        try_place_near(gs, p, BLD_MARKET,      sx + 2, sy - 5, 5);
        try_place_near(gs, p, BLD_MONASTERY,   sx + 6, sy - 2, 6);

        /* Defenses */
        try_place_near(gs, p, BLD_WATCH_TOWER, sx - 7, sy, 4);
        try_place_near(gs, p, BLD_WATCH_TOWER, sx + 7, sy, 4);

        /* Farms (near mill) */
        try_place_near(gs, p, BLD_FARM, sx + 5, sy - 2, 4);
        try_place_near(gs, p, BLD_FARM, sx + 7, sy - 4, 4);

        /* Siege Workshop & University */
        try_place_near(gs, p, BLD_SIEGE_WORKSHOP, sx - 5, sy + 8, 6);
        try_place_near(gs, p, BLD_UNIVERSITY,     sx + 1, sy + 8, 6);
    }

    /* Scatter extra gold/stone in map center */
    for (int i = 0; i < 4; i++) {
        int ccx = MAP_W/4 + (int)(rng_next() % (unsigned)(MAP_W/2));
        int ccy = MAP_H/4 + (int)(rng_next() % (unsigned)(MAP_H/2));
        if (!map_in_bounds(ccx, ccy)) continue;
        TileType rt = (i < 2) ? TILE_GOLD : TILE_STONE;
        int amt = (rt == TILE_GOLD) ? 700 : 600;
        for (int dy2 = -1; dy2 <= 1; dy2++)
            for (int dx2 = -1; dx2 <= 1; dx2++) {
                int tx = ccx+dx2, ty = ccy+dy2;
                if (map_in_bounds(tx, ty) && gs->map[ty][tx].type == TILE_GRASS) {
                    gs->map[ty][tx].type = rt;
                    gs->map[ty][tx].resource_amt = amt + rng_range(0, 100);
                }
            }
    }
}

static bool near_player_town_center(GameState *gs, int tx, int ty, int radius) {
    for (int i = 0; i < gs->bld_count; i++) {
        if (!gs->buildings[i].active || gs->buildings[i].type != BLD_TOWN_CENTER) continue;
        int dx = gs->buildings[i].tx - tx;
        int dy = gs->buildings[i].ty - ty;
        if (dx * dx + dy * dy < radius * radius) return true;
    }
    return false;
}

static int count_adjacent_roads(GameState *gs, int tx, int ty, int w, int h) {
    int roads = 0;
    for (int x = tx - 1; x <= tx + w; x++) {
        if (map_in_bounds(x, ty - 1) && gs->map[ty - 1][x].type == TILE_ROAD) roads++;
        if (map_in_bounds(x, ty + h) && gs->map[ty + h][x].type == TILE_ROAD) roads++;
    }
    for (int y = ty; y < ty + h; y++) {
        if (map_in_bounds(tx - 1, y) && gs->map[y][tx - 1].type == TILE_ROAD) roads++;
        if (map_in_bounds(tx + w, y) && gs->map[y][tx + w].type == TILE_ROAD) roads++;
    }
    return roads;
}

static bool footprint_overlaps_road(GameState *gs, int tx, int ty, int w, int h) {
    for (int dy = 0; dy < h; dy++)
        for (int dx = 0; dx < w; dx++)
            if (gs->map[ty + dy][tx + dx].type == TILE_ROAD)
                return true;
    return false;
}

static bool lot_has_breathing_room(GameState *gs, int tx, int ty, int w, int h, int pad) {
    for (int y = ty - pad; y < ty + h + pad; y++) {
        for (int x = tx - pad; x < tx + w + pad; x++) {
            if (!map_in_bounds(x, y)) continue;
            if (x >= tx && x < tx + w && y >= ty && y < ty + h) continue;
            int bid = gs->map[y][x].building_id;
            if (bid < 0) continue;
            Building *b = &gs->buildings[bid];
            if (b->active) return false;
        }
    }
    return true;
}

static int place_neutral_building_at(GameState *gs, int neutral_player, BldType type, int tx, int ty) {
    int w = building_tw(type);
    int h = building_th(type);
    if (!map_in_bounds(tx, ty) || !map_in_bounds(tx + w - 1, ty + h - 1)) return -1;
    if (!map_is_buildable(gs, tx, ty, w, h)) return -1;
    if (footprint_overlaps_road(gs, tx, ty, w, h)) return -1;

    int slot = -1;
    for (int i = 0; i < MAX_BUILDINGS; i++) {
        if (!gs->buildings[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return -1;

    Building *b = &gs->buildings[slot];
    memset(b, 0, sizeof(Building));
    b->active = true;
    b->id = slot;
    b->player = neutral_player;
    b->type = type;
    b->tx = tx;
    b->ty = ty;
    b->tw = w;
    b->th = h;
    b->max_hp = building_max_hp(type);
    b->hp = b->max_hp;
    b->construction = 1.0f;
    b->complete = true;
    b->variant = (type == BLD_HOUSE) ? (rng_next() % 4) : 0;
    map_place_building(gs, tx, ty, w, h, slot);
    if (slot >= gs->bld_count) gs->bld_count = slot + 1;
    return slot;
}

static BldType choose_neutral_building_type(int road_contacts, bool intersection_bias) {
    int roll = (int)(rng_next() % 100);
    if (intersection_bias || road_contacts >= 4) {
        if (roll < 18) return BLD_MARKET;
        if (roll < 32) return BLD_MONASTERY;
        if (roll < 46) return BLD_BLACKSMITH;
        if (roll < 58) return BLD_WATCH_TOWER;
        if (roll < 68) return BLD_BARRACKS;
        if (roll < 76) return BLD_ARCHERY_RANGE;
        if (roll < 84) return BLD_STABLE;
        return BLD_HOUSE;
    }

    if (roll < 72) return BLD_HOUSE;
    if (roll < 81) return BLD_BLACKSMITH;
    if (roll < 88) return BLD_BARRACKS;
    if (roll < 93) return BLD_ARCHERY_RANGE;
    if (roll < 97) return BLD_STABLE;
    return BLD_MARKET;
}

static void place_neutral_city_buildings(GameState *gs) {
    int neutral_player = 3; /* Uncontrollable neutral player */
    /* Upgrade neutral player to Castle Age so their markets/blacksmiths look grander */
    gs->res[neutral_player].age = 2;
    const int town_center_buffer = 26;
    int road_sites[MAP_W * MAP_H][2];
    int road_site_count = 0;

    for (int y = 2; y < MAP_H - 2; y++) {
        for (int x = 2; x < MAP_W - 2; x++) {
            if (gs->map[y][x].type != TILE_ROAD) continue;
            if (near_player_town_center(gs, x, y, town_center_buffer)) continue;
            road_sites[road_site_count][0] = x;
            road_sites[road_site_count][1] = y;
            road_site_count++;
        }
    }

    for (int i = road_site_count - 1; i > 0; i--) {
        int j = (int)(rng_next() % (unsigned)(i + 1));
        int tmpx = road_sites[i][0], tmpy = road_sites[i][1];
        road_sites[i][0] = road_sites[j][0];
        road_sites[i][1] = road_sites[j][1];
        road_sites[j][0] = tmpx;
        road_sites[j][1] = tmpy;
    }

    int buildings_placed = 0;
    int building_cap = MAP_W * MAP_H / 22;

    for (int i = 0; i < road_site_count && buildings_placed < building_cap; i++) {
        int rx = road_sites[i][0];
        int ry = road_sites[i][1];
        bool intersection_bias = false;

        int road_neighbors = 0;
        static const int dirs[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
        for (int d = 0; d < 4; d++) {
            int nx = rx + dirs[d][0];
            int ny = ry + dirs[d][1];
            if (map_in_bounds(nx, ny) && gs->map[ny][nx].type == TILE_ROAD)
                road_neighbors++;
        }
        if (road_neighbors >= 3) intersection_bias = true;

        BldType type = choose_neutral_building_type(road_neighbors, intersection_bias);
        bool placed_here = false;

        for (int side = 0; side < 4 && !placed_here; side++) {
            int tx = rx;
            int ty = ry;
            int w = building_tw(type);
            int h = building_th(type);

            if (side == 0) { tx = rx - w / 2; ty = ry - h; }
            else if (side == 1) { tx = rx - w / 2; ty = ry + 1; }
            else if (side == 2) { tx = rx - w; ty = ry - h / 2; }
            else { tx = rx + 1; ty = ry - h / 2; }

            if (!map_in_bounds(tx, ty) || !map_in_bounds(tx + w - 1, ty + h - 1)) continue;
            if (near_player_town_center(gs, tx, ty, town_center_buffer)) continue;
            if (!lot_has_breathing_room(gs, tx, ty, w, h, type == BLD_HOUSE ? 0 : 1)) continue;

            int road_contacts = count_adjacent_roads(gs, tx, ty, w, h);
            if (road_contacts == 0) continue;

            if (type != BLD_HOUSE && road_contacts < 2 && (rng_next() % 100) < 60) continue;
            if (type == BLD_HOUSE && road_contacts < 1) continue;

            if (place_neutral_building_at(gs, neutral_player, type, tx, ty) >= 0) {
                buildings_placed++;
                placed_here = true;
            } else if (type != BLD_HOUSE) {
                int hw = building_tw(BLD_HOUSE);
                int hh = building_th(BLD_HOUSE);
                int htx = tx;
                int hty = ty;
                if (side == 0) { htx = rx - hw / 2; hty = ry - hh; }
                else if (side == 1) { htx = rx - hw / 2; hty = ry + 1; }
                else if (side == 2) { htx = rx - hw; hty = ry - hh / 2; }
                else { htx = rx + 1; hty = ry - hh / 2; }

                if (map_in_bounds(htx, hty) && map_in_bounds(htx + hw - 1, hty + hh - 1) &&
                    !near_player_town_center(gs, htx, hty, town_center_buffer) &&
                    lot_has_breathing_room(gs, htx, hty, hw, hh, 0) &&
                    count_adjacent_roads(gs, htx, hty, hw, hh) > 0 &&
                    place_neutral_building_at(gs, neutral_player, BLD_HOUSE, htx, hty) >= 0) {
                    buildings_placed++;
                    placed_here = true;
                }
            }
        }
    }
}

/* ─── Download OSM tile grid matching the bbox ─────────────── */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Convert lat/lon to tile coordinates at given zoom */
static int lon_to_tile_x(double lon, int z) {
    return (int)((lon + 180.0) / 360.0 * (1 << z));
}
static int lat_to_tile_y(double lat, int z) {
    double r = lat * M_PI / 180.0;
    return (int)((1.0 - log(tan(r) + 1.0/cos(r)) / M_PI) / 2.0 * (1 << z));
}

static bool download_single_tile(int z, int tx, int ty, const char *path) {
    char url[256];
    snprintf(url, sizeof(url),
        "https://tile.openstreetmap.org/%d/%d/%d.png", z, tx, ty);
    CURL *c = curl_easy_init();
    if (!c) return false;
    FILE *fp = fopen(path, "wb");
    if (!fp) { curl_easy_cleanup(c); return false; }
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "RTS-MapGen/1.0");
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    CURLcode res = curl_easy_perform(c);
    curl_easy_cleanup(c);
    fclose(fp);
    if (res != CURLE_OK) { remove(path); return false; }
    return true;
}

static bool download_osm_bbox_tiles(GameState *gs, BBox *bbox) {
    /* Choose zoom level: try to fit the bbox in 2-4 tiles per axis */
    int zoom = 15;
    int x0, y0, x1, y1;

    /* Adjust zoom so the bbox fits in at most 4 tiles per axis */
    for (zoom = 16; zoom >= 12; zoom--) {
        x0 = lon_to_tile_x(bbox->west, zoom);
        x1 = lon_to_tile_x(bbox->east, zoom);
        y0 = lat_to_tile_y(bbox->north, zoom); /* north = smaller Y */
        y1 = lat_to_tile_y(bbox->south, zoom); /* south = larger Y */
        int cols = x1 - x0 + 1;
        int rows = y1 - y0 + 1;
        if (cols <= 4 && rows <= 4) break;
    }

    int cols = x1 - x0 + 1;
    int rows = y1 - y0 + 1;
    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;
    if (cols > 4) cols = 4; /* safety cap */
    if (rows > 4) rows = 4;

    gs->osm_tile_z = zoom;
    gs->osm_tile_x0 = x0;
    gs->osm_tile_y0 = y0;
    gs->osm_tile_cols = cols;
    gs->osm_tile_rows = rows;

    printf("OSM MapGen: Downloading %dx%d tile grid at zoom %d (tiles %d,%d to %d,%d)...\n",
           cols, rows, zoom, x0, y0, x0+cols-1, y0+rows-1);

    bool any_ok = false;
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            char path[64];
            snprintf(path, sizeof(path), "osm_tile_%d_%d.png", c, r);
            if (download_single_tile(zoom, x0 + c, y0 + r, path))
                any_ok = true;
        }
    }
    return any_ok;
}

/* ─── Public API ───────────────────────────────────────────── */
bool osm_generate_map(GameState *gs, const char *location_name,
                      int num_players, int *start_x, int *start_y) {
    /* Initialize map to grass */
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++) {
            gs->map[y][x].type = TILE_GRASS;
            gs->map[y][x].resource_amt = 0;
            gs->map[y][x].building_id = -1;
            gs->map[y][x].variant = (uint8_t)(rng_next() % 4);
            for (int p = 0; p < NUM_PLAYERS; p++)
                gs->map[y][x].fog[p] = FOG_HIDDEN;
        }

    curl_global_init(CURL_GLOBAL_DEFAULT);

    double lat, lon;
    BBox bbox;
    if (!geocode(location_name, &lat, &lon, &bbox)) {
        curl_global_cleanup();
        return false;
    }

    OsmData *data = calloc(1, sizeof(OsmData));
    if (!data) { curl_global_cleanup(); return false; }

    bool ok = fetch_features(&bbox, data);

    /* Download OSM tile grid matching the exact bbox */
    gs->osm_map_available = download_osm_bbox_tiles(gs, &bbox);
    gs->osm_bbox_west = bbox.west;
    gs->osm_bbox_east = bbox.east;
    gs->osm_bbox_north = bbox.north;
    gs->osm_bbox_south = bbox.south;
    snprintf(gs->osm_location_name, sizeof(gs->osm_location_name), "%s", location_name);

    curl_global_cleanup();

    if (!ok || data->way_count == 0) {
        printf("OSM MapGen: No features found, map will be mostly grass\n");
    }

    if (data->way_count > 0)
        rasterize(gs, data);

    place_gameplay(gs, num_players, start_x, start_y);
    place_neutral_city_buildings(gs);
    free(data);

    printf("OSM MapGen: Map generation complete for '%s'\n", location_name);
    return true;
}

#else

bool osm_generate_map(GameState *gs, const char *location_name,
                      int num_players, int *start_x, int *start_y) {
    (void)gs;
    (void)location_name;
    (void)num_players;
    (void)start_x;
    (void)start_y;
    printf("OSM MapGen: disabled in this build (RTS_HAS_CURL not set)\n");
    return false;
}

#endif
