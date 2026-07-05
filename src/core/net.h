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
    PKT_SET_RALLY,
    PKT_ATTACK_MOVE,   /* move + auto-engage (was local-only, desynced) */
    PKT_TURN_EXEC,     /* host → all: turn N's command batch is complete */
    PKT_TURN_ACK,      /* client → host: finished simulating turn N      */
    PKT_CHECKSUM,      /* client → host: state checksum at turn N        */
    PKT_DESYNC,        /* host → all: checksum mismatch detected         */
    PKT_STATE_SYNC,    /* host → reconnecting client: full state snapshot */
    PKT_TYPE_COUNT
} PacketType;

/* ── Packet structure (wire format) ────────────────────────── */

#define NET_MAX_UNITS_PER_PACKET 64

#define NET_PROTO_MAGIC   0x5254u  /* "RT" */
#define NET_PROTO_VERSION 3u

#pragma pack(push, 1)
typedef struct {
    uint16_t magic;          /* NET_PROTO_MAGIC — reject foreign data */
    uint8_t  version;        /* NET_PROTO_VERSION — reject old builds */
    uint8_t  type;           /* PacketType discriminator        */
    uint8_t  player;         /* Issuing player ID (host-stamped) */
    uint8_t  _pad;
    uint16_t unit_count;     /* Number of valid entries in units */
    uint32_t turn;           /* Lockstep execution turn (host-stamped) */
    uint16_t units[NET_MAX_UNITS_PER_PACKET];
    int32_t  tx;             /* Target tile X / map coordinate  */
    int32_t  ty;             /* Target tile Y / map coordinate  */
    int32_t  target_id;      /* Target unit or building ID      */
    int32_t  extra;          /* Overloaded: BldType, UnitType, formation, etc. */
} NetPacket;
#pragma pack(pop)

/* Header of a PKT_STATE_SYNC datagram: the header is followed by
   sizeof(GameState) raw state bytes (ENet fragments large
   reliable packets automatically).  state_size guards against
   mismatched builds/struct layouts. */
#pragma pack(push, 1)
typedef struct {
    uint16_t magic;
    uint8_t  version;
    uint8_t  type;          /* PKT_STATE_SYNC */
    uint32_t turn;          /* the snapshot is the state at this turn boundary */
    uint32_t rng;           /* RNG register (lives outside GameState) */
    uint32_t state_size;    /* must equal sizeof(GameState) */
} StateSyncHeader;
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

/* ── Lockstep simulation (see net.c header comment) ────────── */

/* True while a lockstep match is running; main.c then drives the
   sim through net_lockstep_pump instead of calling game_update. */
bool net_lockstep_active(void);

/* Advance the fixed-timestep lockstep sim by up to real_dt worth
   of ticks, gated on turn batches from the host. */
void net_lockstep_pump(GameState *gs, float real_dt);

/* True when the sim is blocked waiting on the network (shown to
   the user; commands are still collected while stalled). */
bool net_lockstep_stalled(void);

uint32_t net_lockstep_turn(void);
