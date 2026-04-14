/*=============================================================
 * pathfinding.c  –  A* on the MAP_W × MAP_H grid
 *=============================================================*/
#include "game.h"

#define CELLS (MAP_W * MAP_H)

/* 8-directional */
static const int DX[8] = { 0, 1, 0,-1,  1, 1,-1,-1};
static const int DY[8] = {-1, 0, 1, 0, -1, 1, 1,-1};
static const float DC[8] = {1,1,1,1, 1.414f,1.414f,1.414f,1.414f};

int pathfind(GameState *gs, int sx, int sy, int ex, int ey,
             PathCell *out, int max_len)
{
    if (!map_in_bounds(sx,sy) || !map_in_bounds(ex,ey)) return 0;
    if (sx==ex && sy==ey) return 0;

    /* Static workspace – fine for single-threaded game */
    static float  g[CELLS];
    static int    from[CELLS];
    static bool   closed[CELLS];

    for(int i=0;i<CELLS;i++){ g[i]=1e30f; from[i]=-1; closed[i]=false; }

    PriorityQueue open;
    pq_init(&open);

    int start = sy*MAP_W + sx;
    int end   = ey*MAP_W + ex;

    g[start] = 0.0f;
    pq_push(&open, start, 0.0f);

    while(!pq_empty(&open)){
        PQNode cur = pq_pop(&open);
        int c = cur.cell;
        if(closed[c]) continue;
        closed[c] = true;
        if(c == end) break;

        int cx=c%MAP_W, cy=c/MAP_W;

        for(int d=0;d<8;d++){
            int nx=cx+DX[d], ny=cy+DY[d];
            if(!map_in_bounds(nx,ny)) continue;
            int nc=ny*MAP_W+nx;
            if(closed[nc]) continue;

            /* Diagonal: both orthogonal neighbours must be passable */
            if(d>=4){
                if(!map_is_passable(gs,cx+DX[d],cy) ||
                   !map_is_passable(gs,cx,cy+DY[d])) continue;
            }

            /* Destination tile doesn't need to be passable (attack/gather) */
            if(nc!=end && !map_is_passable(gs,nx,ny)) continue;

            float ng = g[c] + DC[d];
            if(ng < g[nc]){
                g[nc]    = ng;
                from[nc] = c;
                /* Octile distance heuristic for 8-directional grids */
                int dx = abs(ex - nx);
                int dy = abs(ey - ny);
                float h = (dx > dy) ? (0.414f * dy + dx) : (0.414f * dx + dy);
                pq_push(&open, nc, ng + h);
            }
        }
    }

    if(from[end]<0 && end!=start) return 0;  /* no path */

    /* Reconstruct */
    static int tmp[CELLS];
    int len=0;
    for(int c=end; c!=start && c>=0 && len<CELLS; c=from[c])
        tmp[len++]=c;

    if(len>max_len) len=max_len;
    for(int i=0;i<len;i++){
        int c=tmp[len-1-i];
        out[i]=(PathCell){c%MAP_W, c/MAP_W};
    }
    return len;
}
