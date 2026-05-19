/*=============================================================
 * pathfinding.c  –  A* pathfinding on the MAP_W × MAP_H grid
 *
 * Uses static workspace arrays (not stack-allocated) to avoid
 * overflowing Android's ~1 MB game-thread stack when the AI
 * issues many pathfind() calls in a single frame.
 *=============================================================*/
#include "game.h"

#define GRID_CELL_COUNT (MAP_W * MAP_H)

/* 8-directional movement: cardinal + diagonal */
static const int DIRECTION_DX[8] = {  0,  1,  0, -1,   1,  1, -1, -1 };
static const int DIRECTION_DY[8] = { -1,  0,  1,  0,  -1,  1,  1, -1 };
static const float DIRECTION_COST[8] = {
    1.0f, 1.0f, 1.0f, 1.0f,                /* cardinal */
    1.414f, 1.414f, 1.414f, 1.414f          /* diagonal */
};
static const int FIRST_DIAGONAL_DIR = 4;

/* Octile-distance heuristic for 8-directional grids.
   Consistent with DIRECTION_COST so A* remains admissible. */
static float octile_heuristic(int ax, int ay, int bx, int by)
{
    int dx = abs(bx - ax);
    int dy = abs(by - ay);
    return (dx > dy)
        ? (0.414f * (float)dy + (float)dx)
        : (0.414f * (float)dx + (float)dy);
}

/*──────────────────────────────────────────────────────────────
 * pathfind  –  find a path from (start_x, start_y) to
 *              (goal_x, goal_y), writing up to max_len waypoints
 *              into `out`.  Returns the number of waypoints, or
 *              0 if no path exists.
 *
 * The goal tile itself does NOT need to be passable — this
 * allows units to pathfind toward gather/attack targets that
 * occupy impassable tiles.
 *──────────────────────────────────────────────────────────────*/
int pathfind(GameState *gs, int start_x, int start_y, int goal_x, int goal_y,
             PathCell *out, int max_len)
{
    if (!map_in_bounds(start_x, start_y) || !map_in_bounds(goal_x, goal_y)) {
        return 0;
    }
    if (start_x == goal_x && start_y == goal_y) {
        return 0;
    }

    /* Static workspace avoids per-call heap allocation and stack overflow. */
    static float  cost_so_far[GRID_CELL_COUNT];
    static int    came_from[GRID_CELL_COUNT];
    static bool   visited[GRID_CELL_COUNT];

    for (int i = 0; i < GRID_CELL_COUNT; i++) {
        cost_so_far[i] = 1e30f;
        came_from[i]    = -1;
        visited[i]      = false;
    }

    /* PriorityQueue is ~64 KB — must be static for the same
       stack-safety reason noted above. */
    static PriorityQueue open_set;
    pq_init(&open_set);

    int start_cell = start_y * MAP_W + start_x;
    int goal_cell  = goal_y  * MAP_W + goal_x;

    cost_so_far[start_cell] = 0.0f;
    pq_push(&open_set, start_cell, 0.0f);

    /* ── Expand nodes ──────────────────────────────────────── */
    while (!pq_empty(&open_set)) {
        PQNode current = pq_pop(&open_set);
        int cell = current.cell;

        if (visited[cell]) continue;
        visited[cell] = true;

        if (cell == goal_cell) break;

        int cell_x = cell % MAP_W;
        int cell_y = cell / MAP_W;

        for (int dir = 0; dir < 8; dir++) {
            int neighbor_x = cell_x + DIRECTION_DX[dir];
            int neighbor_y = cell_y + DIRECTION_DY[dir];

            if (!map_in_bounds(neighbor_x, neighbor_y)) continue;

            int neighbor_cell = neighbor_y * MAP_W + neighbor_x;
            if (visited[neighbor_cell]) continue;

            /* Diagonal moves require both adjacent orthogonal
               tiles to be passable (no corner-cutting). */
            if (dir >= FIRST_DIAGONAL_DIR) {
                bool side_a = map_is_passable(gs, cell_x + DIRECTION_DX[dir], cell_y);
                bool side_b = map_is_passable(gs, cell_x, cell_y + DIRECTION_DY[dir]);
                if (!side_a || !side_b) continue;
            }

            /* Goal tile doesn't need to be passable (attack/gather target). */
            if (neighbor_cell != goal_cell && !map_is_passable(gs, neighbor_x, neighbor_y)) {
                continue;
            }

            float tentative_cost = cost_so_far[cell] + DIRECTION_COST[dir];
            if (tentative_cost < cost_so_far[neighbor_cell]) {
                cost_so_far[neighbor_cell] = tentative_cost;
                came_from[neighbor_cell]   = cell;
                float priority = tentative_cost +
                    octile_heuristic(neighbor_x, neighbor_y, goal_x, goal_y);
                pq_push(&open_set, neighbor_cell, priority);
            }
        }
    }

    /* ── Reconstruct path ──────────────────────────────────── */
    if (came_from[goal_cell] < 0 && goal_cell != start_cell) {
        return 0; /* No path found */
    }

    static int reversed_path[GRID_CELL_COUNT];
    int path_length = 0;

    for (int cell = goal_cell;
         cell != start_cell && cell >= 0 && path_length < GRID_CELL_COUNT;
         cell = came_from[cell])
    {
        reversed_path[path_length++] = cell;
    }

    if (path_length > max_len) {
        path_length = max_len;
    }

    /* Write waypoints in forward order. */
    for (int i = 0; i < path_length; i++) {
        int cell = reversed_path[path_length - 1 - i];
        out[i] = (PathCell){ cell % MAP_W, cell / MAP_W };
    }

    return path_length;
}
