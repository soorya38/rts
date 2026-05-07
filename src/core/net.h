#pragma once

#include <stdbool.h>
#include <stdint.h>

/* ── Windows compatibility ──────────────────────────────────────────────────
 * ENet includes winsock2.h → windows.h, which in turn includes:
 *   wingdi.h   → declares Rectangle() ← conflicts with Raylib's Rectangle typedef
 *   winuser.h  → declares CloseWindow(), ShowCursor(), DrawText() ← conflict
 *   winscard.h → pulls in ole2.h → oleidl.h → needs LPMSG (from winuser.h)
 *
 * Fix:
 *   WIN32_LEAN_AND_MEAN  → tells windows.h to skip winscard.h/OLE/COM entirely
 *   NOGDI                → also skip wingdi.h (Rectangle conflict)
 *   NOUSER               → also skip winuser.h (CloseWindow etc.)
 *   With WIN32_LEAN_AND_MEAN the NOUSER/LPMSG cascade never starts.
 *
 * These defines are Windows-only no-ops elsewhere.
 */
#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN   /* skip OLE/COM/winscard cascade */
  #define NOGDI                 /* skip wingdi.h  → no Rectangle() conflict */
  #define NOUSER                /* skip winuser.h → no CloseWindow/ShowCursor/DrawText */
#endif
#include "enet.h"
#ifdef _WIN32
  #undef WIN32_LEAN_AND_MEAN
  #undef NOGDI
  #undef NOUSER
  /* winmm's PlaySound macro collides with raylib's PlaySound() API. */
  #ifdef PlaySound
    #undef PlaySound
  #endif
#endif

/* Multiplayer states */
extern bool g_net_active;
extern int  g_local_player_id; // 0 for Host, 1-3 for Clients, -1 for none
extern bool g_net_connected;

static inline int net_get_local_player() {
    return (g_local_player_id < 0) ? 0 : g_local_player_id;
}

int net_get_peer_count(void);
int net_get_max_players(void);

typedef enum {
    PKT_SYNC_SEED = 0,
    PKT_START_GAME,
    PKT_MOVE,
    PKT_GATHER,
    PKT_ATTACK,
    PKT_BUILD,
    PKT_PLACE_BLD,
    PKT_TRAIN_UNIT,
    PKT_DELETE_BLD,
    PKT_AGE_ADVANCE,
    PKT_ID_ASSIGN,
    PKT_STANCE,
    PKT_LOBBY_SYNC,
    PKT_RESEARCH,
    PKT_SET_RALLY
} PacketType;

#pragma pack(push, 1)
typedef struct {
    uint8_t  type;
    uint8_t  player;
    uint16_t unit_count;     // How many units involved
    uint16_t units[64];      // Array of unit IDs (max 64 per order)
    int32_t  tx;             // Target X or Map X
    int32_t  ty;             // Target Y or Map Y
    int32_t  target_id;      // Target unit/bld ID
    int32_t  extra;          // Use for BldType, UnitType, etc.
} NetPacket;
#pragma pack(pop)

#include "game.h"

bool net_init(void);
void net_deinit(void);
bool net_host_create(int port);
bool net_join(const char *ip, int port);
void net_update(GameState *gs);
void net_send_packet(NetPacket *pkt);
void net_dispatch_packet(GameState *gs, NetPacket *pkt);
void net_disconnect(void);
