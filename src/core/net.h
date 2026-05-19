#pragma once
/*=============================================================
 * net.h  –  Multiplayer networking interface (ENet-based)
 *
 * Provides a thin wrapper over ENet for lockstep-style
 * multiplayer.  Commands are serialised as NetPacket structs
 * and broadcast to all peers reliably.
 *=============================================================*/

#include <stdbool.h>
#include <stdint.h>

/* ── Windows compatibility ─────────────────────────────────────
 * ENet pulls in winsock2.h → windows.h, which declares symbols
 * that collide with Raylib (Rectangle, CloseWindow, DrawText).
 * WIN32_LEAN_AND_MEAN + NOGDI + NOUSER prevent those headers
 * from being included.  These are no-ops on other platforms.
 * ────────────────────────────────────────────────────────────── */
#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #define NOGDI
  #define NOUSER
#endif

#include "enet.h"

#ifdef _WIN32
  #undef WIN32_LEAN_AND_MEAN
  #undef NOGDI
  #undef NOUSER
  /* winmm's PlaySound macro collides with Raylib's PlaySound(). */
  #ifdef PlaySound
    #undef PlaySound
  #endif
#endif

/* ── Multiplayer state ─────────────────────────────────────── */

extern bool g_net_active;          /* True when hosting or connected    */
extern int  g_local_player_id;     /* 0 = host, 1-7 = client, -1 = N/A */
extern bool g_net_connected;       /* True once handshake is complete   */

static inline int net_get_local_player(void)
{
    return (g_local_player_id < 0) ? 0 : g_local_player_id;
}

int net_get_peer_count(void);
int net_get_max_players(void);

/* ── Packet types ──────────────────────────────────────────── */

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

/* ── Packet structure (wire format) ────────────────────────── */

#define NET_MAX_UNITS_PER_PACKET 64

#pragma pack(push, 1)
typedef struct {
    uint8_t  type;           /* PacketType discriminator        */
    uint8_t  player;         /* Issuing player ID               */
    uint16_t unit_count;     /* Number of valid entries in units */
    uint16_t units[NET_MAX_UNITS_PER_PACKET];
    int32_t  tx;             /* Target tile X / map coordinate  */
    int32_t  ty;             /* Target tile Y / map coordinate  */
    int32_t  target_id;      /* Target unit or building ID      */
    int32_t  extra;          /* Overloaded: BldType, UnitType, formation, etc. */
} NetPacket;
#pragma pack(pop)

#include "game.h"

/* ── Lifecycle ─────────────────────────────────────────────── */

bool net_init(void);
void net_deinit(void);
bool net_host_create(int port);
bool net_join(const char *ip, int port);
void net_update(GameState *gs);
void net_send_packet(NetPacket *pkt);
void net_dispatch_packet(GameState *gs, NetPacket *pkt);
void net_disconnect(void);
