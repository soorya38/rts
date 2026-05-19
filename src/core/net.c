/*=============================================================
 * net.c  –  ENet multiplayer implementation
 *
 * Handles hosting, joining, connection lifecycle, and packet
 * dispatch.  The host (player 0) assigns IDs to connecting
 * clients and broadcasts lobby state.
 *=============================================================*/
#define ENET_IMPLEMENTATION
#include "net.h"
#include <stdio.h>
#include <string.h>

/* ── Global multiplayer state ──────────────────────────────── */

bool g_net_active       = false;
int  g_local_player_id  = -1;
bool g_net_connected    = false;

static ENetHost *host_handle   = NULL;
static ENetPeer *peers[NUM_PLAYERS - 1];
static int       peer_count    = 0;

/* ── Lifecycle ─────────────────────────────────────────────── */

bool net_init(void)
{
    if (enet_initialize() != 0) {
        fprintf(stderr, "ENet: failed to initialise.\n");
        return false;
    }
    return true;
}

void net_deinit(void)
{
    net_disconnect();
    if (host_handle) {
        enet_host_destroy(host_handle);
        host_handle = NULL;
    }
    enet_deinitialize();
}

bool net_host_create(int port)
{
    ENetAddress address;
    address.host = ENET_HOST_ANY;
    address.port = (uint16_t)port;

    /* Allow up to (NUM_PLAYERS - 1) clients, 2 channels. */
    host_handle = enet_host_create(&address, NUM_PLAYERS - 1, 2, 0, 0);
    for (int i = 0; i < NUM_PLAYERS - 1; i++) peers[i] = NULL;
    peer_count = 0;

    if (!host_handle) {
        fprintf(stderr, "ENet: failed to create server host.\n");
        return false;
    }

    g_net_active       = true;
    g_local_player_id  = 0;    /* Host is always player 0. */
    g_net_connected    = false;
    return true;
}

bool net_join(const char *ip, int port)
{
    host_handle = enet_host_create(NULL, 1, 2, 0, 0);
    for (int i = 0; i < NUM_PLAYERS - 1; i++) peers[i] = NULL;
    peer_count = 0;

    if (!host_handle) {
        fprintf(stderr, "ENet: failed to create client host.\n");
        return false;
    }

    ENetAddress address;
    enet_address_set_host(&address, ip);
    address.port = (uint16_t)port;

    ENetPeer *server_peer = enet_host_connect(host_handle, &address, 2, 0);
    if (!server_peer) {
        fprintf(stderr, "ENet: no available peers for connection.\n");
        return false;
    }

    peers[0] = server_peer;
    g_net_active       = true;
    g_local_player_id  = -1;   /* Wait for server to assign an ID. */
    g_net_connected    = false;
    return true;
}

void net_disconnect(void)
{
    for (int i = 0; i < NUM_PLAYERS - 1; i++) {
        if (peers[i]) enet_peer_disconnect(peers[i], 0);
        peers[i] = NULL;
    }
    g_net_active      = false;
    g_net_connected   = false;
    g_local_player_id = -1;
}

/* ── Packet send ───────────────────────────────────────────── */

void net_send_packet(NetPacket *pkt)
{
    if (!g_net_connected || !host_handle) return;

    ENetPacket *enet_pkt = enet_packet_create(
        pkt, sizeof(NetPacket), ENET_PACKET_FLAG_RELIABLE);
    enet_host_broadcast(host_handle, 0, enet_pkt);
    enet_host_flush(host_handle);
}

/* ── Packet application (execute a command on the game state) ─ */

static void apply_packet(GameState *gs, NetPacket *pkt)
{
    int capped_count = (pkt->unit_count < NET_MAX_UNITS_PER_PACKET)
                     ?  pkt->unit_count : NET_MAX_UNITS_PER_PACKET;

    switch ((PacketType)pkt->type) {

    case PKT_SYNC_SEED: {
        extern uint32_t _rng;
        _rng = (uint32_t)pkt->extra;
        break;
    }

    case PKT_START_GAME: {
        extern uint32_t _rng;
        int num_players = pkt->extra;
        if (num_players < 1) num_players = 2;
        game_init_started_game(gs, _rng, num_players);
        break;
    }

    case PKT_MOVE: {
        PathCell formation_targets[NET_MAX_UNITS_PER_PACKET];
        FormationType formation = (FormationType)pkt->extra;
        if (formation < 0 || formation >= FORMATION_COUNT) {
            formation = FORMATION_BOX;
        }
        unit_compute_formation_targets(gs, pkt->tx, pkt->ty,
                                       capped_count, formation,
                                       formation_targets);
        for (int i = 0; i < capped_count; i++) {
            if (pkt->units[i] < MAX_UNITS) {
                unit_give_move_order(gs, &gs->units[pkt->units[i]],
                                    formation_targets[i].x,
                                    formation_targets[i].y);
            }
        }
        break;
    }

    case PKT_GATHER:
        for (int i = 0; i < capped_count; i++) {
            if (pkt->units[i] < MAX_UNITS) {
                unit_give_gather_order(gs, &gs->units[pkt->units[i]],
                                       pkt->tx, pkt->ty);
            }
        }
        break;

    case PKT_ATTACK:
        for (int i = 0; i < capped_count; i++) {
            if (pkt->units[i] < MAX_UNITS) {
                int target_unit = (pkt->extra == 0) ? pkt->target_id : -1;
                int target_bld  = (pkt->extra == 1) ? pkt->target_id : -1;
                unit_give_attack_order(gs, &gs->units[pkt->units[i]],
                                       target_unit, target_bld);
            }
        }
        break;

    case PKT_BUILD:
        for (int i = 0; i < capped_count; i++) {
            if (pkt->units[i] < MAX_UNITS) {
                unit_give_build_order(gs, &gs->units[pkt->units[i]],
                                      pkt->target_id);
            }
        }
        break;

    case PKT_PLACE_BLD: {
        int building_id = building_place(gs, pkt->player,
                                         (BldType)pkt->extra,
                                         pkt->tx, pkt->ty);
        if (building_id >= 0) {
            for (int i = 0; i < capped_count; i++) {
                if (pkt->units[i] < MAX_UNITS) {
                    unit_give_build_order(gs, &gs->units[pkt->units[i]],
                                          building_id);
                }
            }
        }
        break;
    }

    case PKT_TRAIN_UNIT:
        if (pkt->target_id >= 0 && pkt->target_id < MAX_BUILDINGS) {
            building_enqueue_unit(gs, &gs->buildings[pkt->target_id],
                                  (UnitType)pkt->extra);
        }
        break;

    case PKT_DELETE_BLD:
        building_sell(gs, pkt->target_id);
        break;

    case PKT_AGE_ADVANCE:
        res_try_advance_age(gs, pkt->player);
        break;

    case PKT_STANCE:
        for (int i = 0; i < capped_count; i++) {
            if (pkt->units[i] < MAX_UNITS) {
                gs->units[pkt->units[i]].stance_manual = (pkt->extra != 0);
            }
        }
        break;

    case PKT_ID_ASSIGN:
        g_local_player_id = (int)pkt->extra;
        printf("Assigned Player ID: %d\n", g_local_player_id);
        break;

    case PKT_LOBBY_SYNC:
        if (g_local_player_id != 0) {
            peer_count = pkt->extra;
        }
        break;

    case PKT_RESEARCH:
        if (pkt->target_id >= 0 && pkt->target_id < MAX_BUILDINGS) {
            building_start_tech(gs, &gs->buildings[pkt->target_id],
                                (TechType)pkt->extra);
        }
        break;

    case PKT_SET_RALLY:
        if (pkt->target_id >= 0 && pkt->target_id < MAX_BUILDINGS) {
            Building *bld = &gs->buildings[pkt->target_id];
            if (bld->active) {
                bld->rally_tx = pkt->tx;
                bld->rally_ty = pkt->ty;
            }
        }
        break;
    }
}

/* ── Network event loop ────────────────────────────────────── */

static void handle_new_connection(ENetEvent *event)
{
    if (g_local_player_id != 0) return; /* Only the host assigns IDs. */

    int assigned_id = ++peer_count;
    if (assigned_id >= NUM_PLAYERS) return;

    peers[assigned_id - 1] = event->peer;

    /* Tell the new client which player slot it has. */
    NetPacket id_pkt = {0};
    id_pkt.type  = PKT_ID_ASSIGN;
    id_pkt.extra = assigned_id;
    ENetPacket *ep = enet_packet_create(&id_pkt, sizeof(NetPacket),
                                        ENET_PACKET_FLAG_RELIABLE);
    enet_peer_send(event->peer, 0, ep);

    /* Send the current RNG seed so the client is in sync. */
    extern uint32_t _rng;
    NetPacket seed_pkt = {0};
    seed_pkt.type  = PKT_SYNC_SEED;
    seed_pkt.extra = _rng;
    ep = enet_packet_create(&seed_pkt, sizeof(NetPacket),
                            ENET_PACKET_FLAG_RELIABLE);
    enet_peer_send(event->peer, 0, ep);

    /* Broadcast updated lobby state to all peers. */
    NetPacket lobby_pkt = {0};
    lobby_pkt.type  = PKT_LOBBY_SYNC;
    lobby_pkt.extra = peer_count;
    net_send_packet(&lobby_pkt);
}

static void handle_disconnect(ENetEvent *event)
{
    printf("ENet disconnected (type=%d).\n", event->type);

    if (g_local_player_id == 0) {
        /* Host: find and remove the disconnected peer. */
        for (int i = 0; i < NUM_PLAYERS - 1; i++) {
            if (peers[i] == event->peer) {
                peers[i] = NULL;
                peer_count--;
                break;
            }
        }

        NetPacket lobby_pkt = {0};
        lobby_pkt.type  = PKT_LOBBY_SYNC;
        lobby_pkt.extra = peer_count;
        net_send_packet(&lobby_pkt);
    }

    if (peer_count == 0 && g_local_player_id != 0) {
        g_net_connected = false;
    }
}

void net_update(GameState *gs)
{
    if (!g_net_active || !host_handle) return;

    ENetEvent event;
    while (enet_host_service(host_handle, &event, 0) > 0) {
        switch (event.type) {
            case ENET_EVENT_TYPE_CONNECT:
                g_net_connected = true;
                printf("ENet connected!\n");
                handle_new_connection(&event);
                break;

            case ENET_EVENT_TYPE_RECEIVE:
                if (event.packet->dataLength == sizeof(NetPacket)) {
                    apply_packet(gs, (NetPacket *)event.packet->data);
                }
                enet_packet_destroy(event.packet);
                break;

            case ENET_EVENT_TYPE_DISCONNECT:
            case ENET_EVENT_TYPE_DISCONNECT_TIMEOUT:
                handle_disconnect(&event);
                break;

            case ENET_EVENT_TYPE_NONE:
                break;
        }
    }
}

/* ── Dispatch: apply locally + broadcast to network ────────── */

void net_dispatch_packet(GameState *gs, NetPacket *pkt)
{
    if (g_net_active) net_send_packet(pkt);
    apply_packet(gs, pkt);
}

int net_get_peer_count(void)   { return peer_count;  }
int net_get_max_players(void)  { return NUM_PLAYERS;  }
