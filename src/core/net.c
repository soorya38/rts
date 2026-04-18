#define ENET_IMPLEMENTATION
#include "net.h"
#include <stdio.h>
#include <string.h>

bool g_net_active = false;
int  g_local_player_id = -1;
bool g_net_connected = false;

static ENetHost *client_or_server = NULL;
static ENetPeer *peers[NUM_PLAYERS-1]; // Track peers (indexed by ID-1)
static int       peer_count = 0;

bool net_init(void) {
    if (enet_initialize() != 0) {
        fprintf(stderr, "An error occurred while initializing ENet.\n");
        return false;
    }
    return true;
}

void net_deinit(void) {
    net_disconnect();
    if (client_or_server) {
        enet_host_destroy(client_or_server);
        client_or_server = NULL;
    }
    enet_deinitialize();
}

bool net_host_create(int port) {
    ENetAddress address;
    address.host = ENET_HOST_ANY;
    address.port = port;
    client_or_server = enet_host_create(&address, 3, 2, 0, 0); // Up to 3 clients 
    for(int i=0; i<NUM_PLAYERS-1; i++) peers[i] = NULL;
    peer_count = 0;
    if (client_or_server == NULL) {
        fprintf(stderr, "An error occurred while trying to create an ENet server host.\n");
        return false;
    }
    g_net_active = true;
    g_local_player_id = 0; // Host is Player 0
    g_net_connected = false;
    return true;
}

bool net_join(const char *ip, int port) {
    client_or_server = enet_host_create(NULL, 1, 2, 0, 0);
    for(int i=0; i<NUM_PLAYERS-1; i++) peers[i] = NULL;
    peer_count = 0;
    if (client_or_server == NULL) {
        fprintf(stderr, "An error occurred while trying to create an ENet client host.\n");
        return false;
    }
    ENetAddress address;
    enet_address_set_host(&address, ip);
    address.port = port;
    peer_count = 0;
    ENetPeer *p = enet_host_connect(client_or_server, &address, 2, 0);
    if (p == NULL) {
        fprintf(stderr, "No available peers for initiating an ENet connection.\n");
        return false;
    }
    peers[0] = p; // For client, peers[0] is the server
    g_net_active = true;
    g_local_player_id = -1; // Wait for server to assign
    g_net_connected = false;
    return true;
}

void net_disconnect(void) {
    for(int i=0; i<NUM_PLAYERS-1; i++) {
        if (peers[i]) enet_peer_disconnect(peers[i], 0);
        peers[i] = NULL;
    }
    g_net_active = false;
    g_net_connected = false;
    g_local_player_id = -1;
}

void net_send_packet(NetPacket *pkt) {
    if (!g_net_connected || !client_or_server) return;

    ENetPacket *enet_pkt = enet_packet_create(pkt, sizeof(NetPacket), ENET_PACKET_FLAG_RELIABLE);
    enet_host_broadcast(client_or_server, 0, enet_pkt);
    enet_host_flush(client_or_server);
}

static void apply_packet(GameState *gs, NetPacket *pkt) {
    if (pkt->type == PKT_SYNC_SEED) {
        // Just store seed? Or init immediately? 
        // In this architecture, we stay in lobby until START_GAME
        extern uint32_t _rng;
        _rng = (uint32_t)pkt->extra;
    }
    else if (pkt->type == PKT_START_GAME) {
        extern uint32_t _rng;
        int np = pkt->extra;
        if (np < 1) np = 2; // Default to 2 if not specified (legacy or solo)
        game_init_started_game(gs, _rng, np);
    }
    else if (pkt->type == PKT_MOVE) {
        for (int i=0; i<pkt->unit_count && i<64; i++) {
            if (pkt->units[i] < MAX_UNITS)
                unit_give_move_order(gs, &gs->units[pkt->units[i]], pkt->tx, pkt->ty);
        }
    }
    else if (pkt->type == PKT_GATHER) {
        for (int i=0; i<pkt->unit_count && i<64; i++) {
            if (pkt->units[i] < MAX_UNITS)
                unit_give_gather_order(gs, &gs->units[pkt->units[i]], pkt->tx, pkt->ty);
        }
    }
    else if (pkt->type == PKT_ATTACK) {
        for (int i=0; i<pkt->unit_count && i<64; i++) {
            if (pkt->units[i] < MAX_UNITS)
                unit_give_attack_order(gs, &gs->units[pkt->units[i]], 
                                       (pkt->extra == 0) ? pkt->target_id : -1,
                                       (pkt->extra == 1) ? pkt->target_id : -1);
        }
    }
    else if (pkt->type == PKT_BUILD) {
        for (int i=0; i<pkt->unit_count && i<64; i++) {
            if (pkt->units[i] < MAX_UNITS)
                unit_give_build_order(gs, &gs->units[pkt->units[i]], pkt->target_id);
        }
    }
    else if (pkt->type == PKT_PLACE_BLD) {
        // We place the building.
        // Wait, for farms/etc there might be prerequisite logic locally, but if it reaches here, we assume valid
        int bid = building_place(gs, pkt->player, (BldType)pkt->extra, pkt->tx, pkt->ty);
        if (bid >= 0) {
            // Assign builders
             for (int i=0; i<pkt->unit_count && i<64; i++) {
                if (pkt->units[i] < MAX_UNITS)
                    unit_give_build_order(gs, &gs->units[pkt->units[i]], bid);
             }
        }
    }
    else if (pkt->type == PKT_TRAIN_UNIT) {
        if (pkt->target_id >= 0 && pkt->target_id < MAX_BUILDINGS)
            building_enqueue_unit(gs, &gs->buildings[pkt->target_id], (UnitType)pkt->extra);
    }
    else if (pkt->type == PKT_DELETE_BLD) {
        building_sell(gs, pkt->target_id);
    }
    else if (pkt->type == PKT_AGE_ADVANCE) {
        res_try_advance_age(gs, pkt->player);
    }
    else if (pkt->type == PKT_STANCE) {
        for (int i=0; i<pkt->unit_count && i<64; i++) {
            if (pkt->units[i] < MAX_UNITS)
                gs->units[pkt->units[i]].stance_manual = (pkt->extra != 0);
        }
    }
    else if (pkt->type == PKT_ID_ASSIGN) {
        g_local_player_id = (int)pkt->extra;
        printf("Assigned Player ID: %d\n", g_local_player_id);
    }
    else if (pkt->type == PKT_LOBBY_SYNC) {
        if (g_local_player_id != 0) {
            peer_count = pkt->extra;
        }
    }
    else if (pkt->type == PKT_RESEARCH) {
        if (pkt->target_id >= 0 && pkt->target_id < MAX_BUILDINGS)
            building_start_tech(gs, &gs->buildings[pkt->target_id], (TechType)pkt->extra);
    }
    else if (pkt->type == PKT_SET_RALLY) {
        if (pkt->target_id >= 0 && pkt->target_id < MAX_BUILDINGS) {
            Building *b = &gs->buildings[pkt->target_id];
            if (b->active) {
                b->rally_tx = pkt->tx;
                b->rally_ty = pkt->ty;
            }
        }
    }
}

void net_update(GameState *gs) {
    if (!g_net_active || !client_or_server) return;

    ENetEvent event;
    while (enet_host_service(client_or_server, &event, 0) > 0) {
        switch (event.type) {
            case ENET_EVENT_TYPE_CONNECT:
                g_net_connected = true;
                printf("ENet connected!\n");
                
                if (g_local_player_id == 0) { // We are host
                    int assigned_id = ++peer_count;
                    if (assigned_id < NUM_PLAYERS) {
                        peers[assigned_id-1] = event.peer;
                        
                        NetPacket idp = {0};
                        idp.type = PKT_ID_ASSIGN;
                        idp.extra = assigned_id;
                        ENetPacket *ep = enet_packet_create(&idp, sizeof(NetPacket), ENET_PACKET_FLAG_RELIABLE);
                        enet_peer_send(event.peer, 0, ep);
                        
                        // Also send current seed to the NEW peer
                        extern uint32_t _rng;
                        NetPacket sp = {0};
                        sp.type = PKT_SYNC_SEED;
                        sp.extra = _rng;
                        ep = enet_packet_create(&sp, sizeof(NetPacket), ENET_PACKET_FLAG_RELIABLE);
                        enet_peer_send(event.peer, 0, ep);
                        
                        NetPacket lsp = {0};
                        lsp.type = PKT_LOBBY_SYNC;
                        lsp.extra = peer_count;
                        net_send_packet(&lsp);
                    }
                }
                break;
            case ENET_EVENT_TYPE_RECEIVE:
                if (event.packet->dataLength == sizeof(NetPacket)) {
                    NetPacket *pkt = (NetPacket *)event.packet->data;
                    apply_packet(gs, pkt);
                }
                enet_packet_destroy(event.packet);
                break;
            case ENET_EVENT_TYPE_DISCONNECT:
                printf("ENet disconnected.\n");
                // If Host, find which peer disconnected
                if (g_local_player_id == 0) {
                    for(int i=0; i<NUM_PLAYERS-1; i++) {
                        if (peers[i] == event.peer) { peers[i] = NULL; peer_count--; break; }
                    }
                    NetPacket lsp = {0};
                    lsp.type = PKT_LOBBY_SYNC;
                    lsp.extra = peer_count;
                    net_send_packet(&lsp);
                }
                if (peer_count == 0 && g_local_player_id != 0) g_net_connected = false;
                enet_packet_destroy(event.packet); // wait, event.packet might be null on disconnect? ENet docs say so
                break;
            case ENET_EVENT_TYPE_NONE:
                break;
        }
    }
}

void net_dispatch_packet(GameState *gs, NetPacket *pkt) {
    if (g_net_active) net_send_packet(pkt);
    apply_packet(gs, pkt);
}

int net_get_peer_count(void) { return peer_count; }
int net_get_max_players(void) { return NUM_PLAYERS; }
