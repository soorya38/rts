#include "game.h"
#include "osm_mapgen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main() {
    GameState gs;
    memset(&gs, 0, sizeof(gs));
    int sx[2], sy[2];
    bool ok = osm_generate_map(&gs, "coimbatore", 2, sx, sy);
    printf("Success: %d\n", ok);
    return 0;
}
