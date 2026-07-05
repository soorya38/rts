/*=============================================================
 * net.c  –  ENet multiplayer: lobby, lockstep, and validation
 *
 * Topology: star.  Clients connect to the host (player 0); the
 * host assigns player slots and relays everything.
 *
 * Sync model: deterministic lockstep.  While a match runs, the
 * sim advances in fixed ticks (NET_TICK_DT) grouped into turns
 * of NET_TICKS_PER_TURN.  Commands are never applied on receipt:
 *   1. A player's command is sent to the host (the host queues
 *      its own locally).
 *   2. At each turn boundary the host stamps the queued commands
 *      with a future turn number and broadcasts them, followed
 *      by PKT_TURN_EXEC marking the batch complete.
 *   3. Every machine (host included) executes turn N's batch in
 *      identical order at the start of turn N, then simulates
 *      the turn's ticks.  A machine that hasn't received the
 *      batch stalls until it arrives.
 * Clients ack each finished turn; the host stops issuing new
 * turns if a client falls too far behind (flow control).  Every
 * NET_CHECKSUM_INTERVAL turns clients send a state checksum and
 * the host verifies it to detect desyncs.
 *
 * Validation: packets are dropped unless the magic, version,
 * size, and type check out.  The host overwrites the player
 * field of client commands with the sender's assigned slot, and
 * apply_packet enforces per-unit/per-building ownership, so a
 * peer can neither spoof another player nor command units it
 * does not own.  Control packets are only honoured from the
 * host.
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
static ENetPeer *peers[NUM_PLAYERS - 1];   /* slot i ↔ player i+1 */
static bool      enet_ready    = false;

/* Host-side slot lifecycle.  A slot whose player drops mid-match
   is RESERVED (not freed) so the same player id can reconnect
   and receive a state snapshot. */
typedef enum { SLOT_EMPTY = 0, SLOT_CONNECTED, SLOT_RESERVED } SlotState;
static SlotState slot_state[NUM_PLAYERS - 1];
static bool      snapshot_pending[NUM_PLAYERS - 1];

/* Client-side auto-reconnect state. */
#define NET_RECONNECT_RETRY_MS  2000u
#define NET_RECONNECT_LIMIT_MS  30000u
static bool     reconnecting      = false;
static uint32_t reconnect_next_ms = 0;
static uint32_t reconnect_stop_ms = 0;
static char     last_join_ip[64];
static int      last_join_port;

/* ── Lockstep configuration ────────────────────────────────── */

#define NET_TICK_DT           (1.0f / 30.0f)
#define NET_TICKS_PER_TURN    3      /* 100 ms turns */
#define NET_TURN_LATENCY      2      /* commands execute 2 turns out */
#define NET_TURN_WINDOW       32     /* ring of buffered turns */
#define NET_MAX_TURN_CMDS     64     /* commands per turn batch */
#define NET_MAX_CLIENT_LAG    8      /* host stalls if a client is this far behind */
#define NET_CHECKSUM_INTERVAL 30     /* sample state every ~3 s */
#define NET_CATCHUP_MAX_TICKS 8      /* max sim ticks per rendered frame */

typedef struct {
    NetPacket cmds[NET_MAX_TURN_CMDS];
    int       count;
    uint32_t  turn;
    bool      ready;
} TurnSlot;

static bool      lockstep_running = false;
static TurnSlot  turn_ring[NET_TURN_WINDOW];
static uint32_t  exec_turn;        /* turn currently being simulated */
static int       ticks_into_turn;
static float     tick_accum;
static float     stall_time;       /* seconds spent blocked on the network */

/* Host only: commands collected for the next batch, per-client
   turn acks for flow control, and recent checksums for desync
   detection. */
static NetPacket host_pending[NET_MAX_TURN_CMDS];
static int       host_pending_count;
static uint32_t  host_issue_turn;             /* next turn number to broadcast */
static uint32_t  peer_ack_turn[NUM_PLAYERS - 1];
static uint32_t  cksum_ring[NET_TURN_WINDOW]; /* host's own sums, by turn      */
static uint32_t  cksum_ring_turn[NET_TURN_WINDOW];
static bool      desync_reported;

/* A client may reach a checksum turn slightly before the host
   does (it can run up to NET_TURN_LATENCY turns ahead); park its
   sum until the host samples that turn.  One pending sum per
   client is enough given the flow-control lag cap. */
static uint32_t  client_pending_sum[NUM_PLAYERS - 1];
static uint32_t  client_pending_turn[NUM_PLAYERS - 1]; /* UINT32_MAX = none */

/* Client-side lobby size, fed by PKT_LOBBY_SYNC from the host. */
static int       client_lobby_count;

static TurnSlot *ring_slot(uint32_t turn) { return &turn_ring[turn % NET_TURN_WINDOW]; }

static void lockstep_reset(void)
{
    memset(turn_ring, 0, sizeof(turn_ring));
    memset(cksum_ring, 0, sizeof(cksum_ring));
    memset(cksum_ring_turn, 0xFF, sizeof(cksum_ring_turn));
    memset(client_pending_turn, 0xFF, sizeof(client_pending_turn));
    memset(peer_ack_turn, 0, sizeof(peer_ack_turn));
    exec_turn          = 0;
    ticks_into_turn    = 0;
    tick_accum         = 0.0f;
    stall_time         = 0.0f;
    host_pending_count = 0;
    desync_reported    = false;

    /* The first NET_TURN_LATENCY turns have no commands; mark
       them ready so both sides can start simulating at once. */
    for (uint32_t t = 0; t < NET_TURN_LATENCY; t++) {
        TurnSlot *slot = ring_slot(t);
        slot->turn  = t;
        slot->count = 0;
        slot->ready = true;
    }
    host_issue_turn = NET_TURN_LATENCY;
    lockstep_running = true;
}

/* ── Lifecycle ─────────────────────────────────────────────── */

bool net_init(void)
{
    if (enet_ready) return true;
    if (enet_initialize() != 0) {
        fprintf(stderr, "ENet: failed to initialise.\n");
        return false;
    }
    enet_ready = true;
    return true;
}

void net_deinit(void)
{
    net_disconnect();
    if (host_handle) {
        enet_host_flush(host_handle);
        enet_host_destroy(host_handle);
        host_handle = NULL;
    }
    if (enet_ready) {
        enet_deinitialize();
        enet_ready = false;
    }
}

static void net_reset_session(void)
{
    for (int i = 0; i < NUM_PLAYERS - 1; i++) {
        peers[i] = NULL;
        slot_state[i] = SLOT_EMPTY;
        snapshot_pending[i] = false;
    }
    lockstep_running = false;
    reconnecting     = false;
}

bool net_host_create(int port)
{
    ENetAddress address;
    address.host = ENET_HOST_ANY;
    address.port = (uint16_t)port;

    /* Allow up to (NUM_PLAYERS - 1) clients, 2 channels. */
    host_handle = enet_host_create(&address, NUM_PLAYERS - 1, 2, 0, 0);
    net_reset_session();

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
    net_reset_session();

    if (!host_handle) {
        fprintf(stderr, "ENet: failed to create client host.\n");
        return false;
    }

    ENetAddress address;
    if (enet_address_set_host(&address, ip) != 0) {
        fprintf(stderr, "ENet: could not resolve '%s'.\n", ip);
        enet_host_destroy(host_handle);
        host_handle = NULL;
        return false;
    }
    address.port = (uint16_t)port;

    ENetPeer *server_peer = enet_host_connect(host_handle, &address, 2, 0);
    if (!server_peer) {
        fprintf(stderr, "ENet: no available peers for connection.\n");
        enet_host_destroy(host_handle);
        host_handle = NULL;
        return false;
    }

    snprintf(last_join_ip, sizeof(last_join_ip), "%s", ip);
    last_join_port = port;

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
    if (host_handle) enet_host_flush(host_handle);
    g_net_active      = false;
    g_net_connected   = false;
    g_local_player_id = -1;
    lockstep_running  = false;
}

int net_get_peer_count(void)
{
    /* Clients only hold the server peer; the real lobby size
       comes from the host's PKT_LOBBY_SYNC broadcasts. */
    if (g_net_active && g_local_player_id > 0) return client_lobby_count;

    int n = 0;
    for (int i = 0; i < NUM_PLAYERS - 1; i++)
        if (peers[i]) n++;
    return n;
}

int net_get_max_players(void)  { return NUM_PLAYERS; }

/* ── Packet send helpers ───────────────────────────────────── */

static void pkt_finalize(NetPacket *pkt)
{
    pkt->magic   = NET_PROTO_MAGIC;
    pkt->version = NET_PROTO_VERSION;
}

static void send_to_peer(ENetPeer *peer, NetPacket *pkt)
{
    pkt_finalize(pkt);
    ENetPacket *ep = enet_packet_create(pkt, sizeof(NetPacket),
                                        ENET_PACKET_FLAG_RELIABLE);
    enet_peer_send(peer, 0, ep);
}

static void broadcast_to_peers(NetPacket *pkt)
{
    pkt_finalize(pkt);
    if (!host_handle) return;
    ENetPacket *ep = enet_packet_create(pkt, sizeof(NetPacket),
                                        ENET_PACKET_FLAG_RELIABLE);
    enet_host_broadcast(host_handle, 0, ep);
}

/* Public "broadcast" kept for lobby-control use (host → all, or
   client → host, which is the client's only peer). */
void net_send_packet(NetPacket *pkt)
{
    if (!host_handle) return;
    broadcast_to_peers(pkt);
    enet_host_flush(host_handle);
}

/* ── Command validation helpers ────────────────────────────────
 * These run identically on every machine (they depend only on
 * the packet and the synchronized game state), so rejecting a
 * bad command cannot itself cause a desync. */

static bool pkt_player_ok(GameState *gs, NetPacket *pkt)
{
    return pkt->player < gs->num_players;
}

/* The unit referenced by entry i, iff it exists and belongs to
   the issuing player. */
static Unit *pkt_owned_unit(GameState *gs, NetPacket *pkt, int i)
{
    int id = pkt->units[i];
    if (id >= MAX_UNITS) return NULL;
    Unit *u = &gs->units[id];
    if (!u->active || u->player != pkt->player) return NULL;
    return u;
}

static Building *pkt_owned_building(GameState *gs, NetPacket *pkt, int id)
{
    if (id < 0 || id >= MAX_BUILDINGS) return NULL;
    Building *b = &gs->buildings[id];
    if (!b->active || b->player != pkt->player) return NULL;
    return b;
}

/* ── Packet application (execute a command on the game state) ─ */

static void apply_move_like(GameState *gs, NetPacket *pkt, bool attack_move)
{
    if (!map_in_bounds(pkt->tx, pkt->ty)) return;

    int capped_count = (pkt->unit_count < NET_MAX_UNITS_PER_PACKET)
                     ?  pkt->unit_count : NET_MAX_UNITS_PER_PACKET;

    PathCell formation_targets[NET_MAX_UNITS_PER_PACKET];
    FormationType formation = (FormationType)pkt->extra;
    if (pkt->extra < 0 || pkt->extra >= FORMATION_COUNT) {
        formation = FORMATION_BOX;
    }
    unit_compute_formation_targets(gs, pkt->tx, pkt->ty,
                                   capped_count, formation,
                                   formation_targets);
    int batch_ids[NET_MAX_UNITS_PER_PACKET];
    PathCell batch_dests[NET_MAX_UNITS_PER_PACKET];
    int batch_count = 0;
    for (int i = 0; i < capped_count; i++) {
        Unit *u = pkt_owned_unit(gs, pkt, i);
        if (!u) continue;
        if (attack_move) {
            if (u->type == UNIT_VILLAGER) continue;
            u->stance_manual = false;  /* aggressive: engage enemies en route */
            u->attack_move   = true;
        }
        batch_ids[batch_count]   = pkt->units[i];
        batch_dests[batch_count] = formation_targets[i];
        batch_count++;
    }
    unit_give_move_orders_batch(gs, batch_ids, batch_dests, batch_count);
}

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
        int num_players = pkt->extra;
        if (num_players < 1) num_players = 2;
        game_init_started_game(gs, (uint32_t)pkt->target_id, num_players);
        lockstep_reset();
        break;
    }

    case PKT_MOVE:
        if (!pkt_player_ok(gs, pkt)) break;
        apply_move_like(gs, pkt, false);
        break;

    case PKT_ATTACK_MOVE:
        if (!pkt_player_ok(gs, pkt)) break;
        apply_move_like(gs, pkt, true);
        break;

    case PKT_GATHER:
        if (!pkt_player_ok(gs, pkt)) break;
        if (!map_in_bounds(pkt->tx, pkt->ty)) break;
        for (int i = 0; i < capped_count; i++) {
            Unit *u = pkt_owned_unit(gs, pkt, i);
            if (u) unit_give_gather_order(gs, u, pkt->tx, pkt->ty);
        }
        break;

    case PKT_ATTACK: {
        if (!pkt_player_ok(gs, pkt)) break;
        int target_unit = (pkt->extra == 0) ? pkt->target_id : -1;
        int target_bld  = (pkt->extra == 1) ? pkt->target_id : -1;
        if (target_unit >= MAX_UNITS || target_bld >= MAX_BUILDINGS) break;
        if (target_unit < 0 && target_bld < 0) break;
        for (int i = 0; i < capped_count; i++) {
            Unit *u = pkt_owned_unit(gs, pkt, i);
            if (u) unit_give_attack_order(gs, u, target_unit, target_bld);
        }
        break;
    }

    case PKT_BUILD:
        if (!pkt_player_ok(gs, pkt)) break;
        if (!pkt_owned_building(gs, pkt, pkt->target_id)) break;
        for (int i = 0; i < capped_count; i++) {
            Unit *u = pkt_owned_unit(gs, pkt, i);
            if (u) unit_give_build_order(gs, u, pkt->target_id);
        }
        break;

    case PKT_PLACE_BLD: {
        if (!pkt_player_ok(gs, pkt)) break;
        if (pkt->extra < 0 || pkt->extra >= BLD_COUNT) break;
        int building_id = building_place(gs, pkt->player,
                                         (BldType)pkt->extra,
                                         pkt->tx, pkt->ty);
        if (building_id >= 0) {
            for (int i = 0; i < capped_count; i++) {
                Unit *u = pkt_owned_unit(gs, pkt, i);
                if (u) unit_give_build_order(gs, u, building_id);
            }
        }
        break;
    }

    case PKT_TRAIN_UNIT: {
        if (!pkt_player_ok(gs, pkt)) break;
        if (pkt->extra < 0 || pkt->extra >= UNIT_COUNT) break;
        Building *b = pkt_owned_building(gs, pkt, pkt->target_id);
        if (b) building_enqueue_unit(gs, b, (UnitType)pkt->extra);
        break;
    }

    case PKT_DELETE_BLD:
        if (!pkt_player_ok(gs, pkt)) break;
        if (pkt_owned_building(gs, pkt, pkt->target_id)) {
            building_sell(gs, pkt->target_id);
        }
        break;

    case PKT_AGE_ADVANCE:
        if (!pkt_player_ok(gs, pkt)) break;
        res_try_advance_age(gs, pkt->player);
        break;

    case PKT_STANCE:
        if (!pkt_player_ok(gs, pkt)) break;
        for (int i = 0; i < capped_count; i++) {
            Unit *u = pkt_owned_unit(gs, pkt, i);
            if (u) u->stance_manual = (pkt->extra != 0);
        }
        break;

    case PKT_ID_ASSIGN:
        g_local_player_id = (int)pkt->extra;
        printf("Assigned Player ID: %d\n", g_local_player_id);
        break;

    case PKT_LOBBY_SYNC:
        /* Lobby size is informational for clients; the value is
           read back through net_get_peer_count on the host. */
        break;

    case PKT_RESEARCH: {
        if (!pkt_player_ok(gs, pkt)) break;
        if (pkt->extra < 0 || pkt->extra >= TECH_COUNT) break;
        Building *b = pkt_owned_building(gs, pkt, pkt->target_id);
        if (b) building_start_tech(gs, b, (TechType)pkt->extra);
        break;
    }

    case PKT_SET_RALLY: {
        if (!pkt_player_ok(gs, pkt)) break;
        if (!map_in_bounds(pkt->tx, pkt->ty)) break;
        Building *b = pkt_owned_building(gs, pkt, pkt->target_id);
        if (b) {
            b->rally_tx = pkt->tx;
            b->rally_ty = pkt->ty;
        }
        break;
    }

    case PKT_DESYNC:
        game_set_alert(gs, "DESYNC DETECTED - game states have diverged.");
        break;

    default:
        break;
    }
}

/* ── Lockstep: turn queueing and execution ─────────────────── */

static void host_queue_command(NetPacket *pkt)
{
    if (host_pending_count >= NET_MAX_TURN_CMDS) {
        fprintf(stderr, "Net: turn command buffer full, dropping command.\n");
        return;
    }
    host_pending[host_pending_count++] = *pkt;
}

/* Host: broadcast queued commands as the batch for the next
   unissued turn, respecting client flow control. */
static void host_try_issue_turns(void)
{
    if (g_local_player_id != 0 || !lockstep_running) return;

    while (host_issue_turn <= exec_turn + NET_TURN_LATENCY) {
        /* Flow control: don't run away from a lagging client. */
        for (int i = 0; i < NUM_PLAYERS - 1; i++) {
            if (peers[i] && peer_ack_turn[i] + NET_MAX_CLIENT_LAG < exec_turn) {
                return;
            }
        }

        TurnSlot *slot = ring_slot(host_issue_turn);
        slot->turn  = host_issue_turn;
        slot->count = host_pending_count;
        for (int c = 0; c < host_pending_count; c++) {
            host_pending[c].turn = host_issue_turn;
            slot->cmds[c] = host_pending[c];
            broadcast_to_peers(&host_pending[c]);
        }
        host_pending_count = 0;

        NetPacket exec_pkt = {0};
        exec_pkt.type  = PKT_TURN_EXEC;
        exec_pkt.turn  = host_issue_turn;
        exec_pkt.extra = slot->count;
        broadcast_to_peers(&exec_pkt);

        slot->ready = true;
        host_issue_turn++;
    }
    if (host_handle) enet_host_flush(host_handle);
}

/* Host: compare a client's checksum against our own for that turn. */
static void host_report_desync(GameState *gs, uint32_t turn, int player,
                               uint32_t mine, uint32_t theirs)
{
    if (desync_reported) return;
    desync_reported = true;
    fprintf(stderr, "Net: DESYNC at turn %u (host %08x, player %d %08x)\n",
            turn, mine, player, theirs);
    NetPacket d = {0};
    d.type = PKT_DESYNC;
    d.turn = turn;
    broadcast_to_peers(&d);
    apply_packet(gs, &d);  /* alert locally too */
}

static void host_check_client_checksum(GameState *gs, NetPacket *pkt)
{
    if (desync_reported || !lockstep_running) return;
    if (pkt->turn % NET_CHECKSUM_INTERVAL != 0) return;
    int slot = pkt->player - 1;
    if (slot < 0 || slot >= NUM_PLAYERS - 1) return;

    if (cksum_ring_turn[pkt->turn % NET_TURN_WINDOW] == pkt->turn) {
        /* We already sampled this turn — compare now. */
        uint32_t mine = cksum_ring[pkt->turn % NET_TURN_WINDOW];
        if (mine != (uint32_t)pkt->target_id) {
            host_report_desync(gs, pkt->turn, pkt->player,
                               mine, (uint32_t)pkt->target_id);
        }
    } else if (pkt->turn >= exec_turn) {
        /* Client is ahead of us; park the sum until we get there. */
        client_pending_sum[slot]  = (uint32_t)pkt->target_id;
        client_pending_turn[slot] = pkt->turn;
    }
    /* Turns older than the ring window are simply dropped. */
}

/* Host: after sampling our own checksum, settle any parked
   client sums for the same turn. */
static void host_settle_pending_checksums(GameState *gs, uint32_t turn,
                                          uint32_t mine)
{
    for (int i = 0; i < NUM_PLAYERS - 1; i++) {
        if (client_pending_turn[i] != turn) continue;
        client_pending_turn[i] = UINT32_MAX;
        if (mine != client_pending_sum[i]) {
            host_report_desync(gs, turn, i + 1, mine, client_pending_sum[i]);
        }
    }
}

/* Host → one reconnecting peer: the full game state at the
   current turn boundary.  ENet fragments this large reliable
   packet transparently. */
static void host_send_state_snapshot(ENetPeer *peer, GameState *gs)
{
    extern uint32_t _rng;
    size_t total = sizeof(StateSyncHeader) + sizeof(GameState);
    ENetPacket *ep = enet_packet_create(NULL, total, ENET_PACKET_FLAG_RELIABLE);
    if (!ep) return;

    StateSyncHeader hdr;
    hdr.magic      = NET_PROTO_MAGIC;
    hdr.version    = NET_PROTO_VERSION;
    hdr.type       = PKT_STATE_SYNC;
    hdr.turn       = exec_turn;
    hdr.rng        = _rng;
    hdr.state_size = (uint32_t)sizeof(GameState);

    memcpy(ep->data, &hdr, sizeof(hdr));
    memcpy(ep->data + sizeof(hdr), gs, sizeof(GameState));
    enet_peer_send(peer, 0, ep);
}

/* Host: at a turn boundary, deliver a state snapshot to any peer
   that just (re)connected mid-match, then replay the still-in-
   flight turn batches so the client resumes exactly at exec_turn.
   Ordered channel 0 guarantees the snapshot lands before the
   replayed and future batches. */
static void host_send_pending_snapshots(GameState *gs)
{
    if (g_local_player_id != 0) return;

    for (int i = 0; i < NUM_PLAYERS - 1; i++) {
        if (!snapshot_pending[i] || !peers[i]) continue;

        host_send_state_snapshot(peers[i], gs);

        /* Replay batches for turns the client missed while gone:
           [exec_turn, host_issue_turn).  The snapshot clears the
           client's ring, so these repopulate it. */
        for (uint32_t t = exec_turn; t < host_issue_turn; t++) {
            TurnSlot *slot = ring_slot(t);
            if (slot->turn != t) continue;
            for (int c = 0; c < slot->count; c++) {
                send_to_peer(peers[i], &slot->cmds[c]);
            }
            NetPacket exec_pkt = {0};
            exec_pkt.type  = PKT_TURN_EXEC;
            exec_pkt.turn  = t;
            exec_pkt.extra = slot->count;
            send_to_peer(peers[i], &exec_pkt);
        }

        peer_ack_turn[i]    = exec_turn;
        snapshot_pending[i] = false;
    }
    if (host_handle) enet_host_flush(host_handle);
}

/* Try to run one fixed sim tick.  Returns false if blocked
   waiting for the current turn's command batch. */
static bool lockstep_step(GameState *gs)
{
    if (ticks_into_turn == 0) {
        host_try_issue_turns();
        host_send_pending_snapshots(gs);

        TurnSlot *slot = ring_slot(exec_turn);
        if (!slot->ready || slot->turn != exec_turn) {
            return false;  /* batch not here yet — stall */
        }

        /* Sample the state checksum at the turn boundary, before
           this turn's commands run, on the same turn everywhere. */
        if (exec_turn % NET_CHECKSUM_INTERVAL == 0) {
            uint32_t sum = game_state_checksum(gs);
            if (g_local_player_id == 0) {
                cksum_ring[exec_turn % NET_TURN_WINDOW]      = sum;
                cksum_ring_turn[exec_turn % NET_TURN_WINDOW] = exec_turn;
                host_settle_pending_checksums(gs, exec_turn, sum);
            } else if (peers[0]) {
                NetPacket ck = {0};
                ck.type      = PKT_CHECKSUM;
                ck.player    = (uint8_t)net_get_local_player();
                ck.turn      = exec_turn;
                ck.target_id = (int32_t)sum;
                send_to_peer(peers[0], &ck);
            }
        }

        for (int c = 0; c < slot->count; c++) {
            apply_packet(gs, &slot->cmds[c]);
        }
        slot->ready = false;  /* consumed */
    }

    game_update(gs, NET_TICK_DT);
    ticks_into_turn++;

    if (ticks_into_turn >= NET_TICKS_PER_TURN) {
        ticks_into_turn = 0;
        exec_turn++;
        if (g_local_player_id != 0 && peers[0]) {
            NetPacket ack = {0};
            ack.type = PKT_TURN_ACK;
            ack.turn = exec_turn - 1;
            send_to_peer(peers[0], &ack);
        }
        host_try_issue_turns();
    }
    return true;
}

bool net_lockstep_active(void)  { return g_net_active && lockstep_running; }
bool net_lockstep_stalled(void) { return stall_time > 0.5f; }
uint32_t net_lockstep_turn(void){ return exec_turn; }

void net_lockstep_pump(GameState *gs, float real_dt)
{
    if (!net_lockstep_active()) return;
    if (gs->phase != PHASE_PLAYING) return;

    tick_accum += real_dt;
    /* Cap the debt so a long stall doesn't cause a huge burst. */
    float max_accum = NET_TICK_DT * NET_CATCHUP_MAX_TICKS;
    if (tick_accum > max_accum) tick_accum = max_accum;

    int guard = NET_CATCHUP_MAX_TICKS;
    bool advanced = false;
    while (tick_accum >= NET_TICK_DT && guard-- > 0) {
        if (!lockstep_step(gs)) break;
        tick_accum -= NET_TICK_DT;
        advanced = true;
    }

    if (advanced) stall_time = 0.0f;
    else if (tick_accum >= NET_TICK_DT) stall_time += real_dt;

    if (net_lockstep_stalled() && gs->alert_timer <= 0.0f) {
        game_set_alert(gs, "Waiting for network...");
    }
}

/* ── Network event loop ────────────────────────────────────── */

static void handle_new_connection(GameState *gs, ENetEvent *event)
{
    if (g_local_player_id != 0) {
        /* Client side: our connection to the server succeeded. */
        g_net_connected = true;
        if (reconnecting && gs) {
            game_set_alert(gs, "Reconnected - resyncing game state...");
        }
        reconnecting = false;
        return;
    }

    /* Assign a slot.  Mid-match, only slots reserved by a
       dropped player are available (this is a reconnect); in the
       lobby, any empty slot works. */
    int slot = -1;
    SlotState wanted = lockstep_running ? SLOT_RESERVED : SLOT_EMPTY;
    for (int i = 0; i < NUM_PLAYERS - 1; i++) {
        if (!peers[i] && slot_state[i] == wanted) { slot = i; break; }
    }
    if (slot < 0) {
        enet_peer_disconnect(event->peer, 0);  /* full / no reconnect slot */
        return;
    }

    peers[slot] = event->peer;
    slot_state[slot] = SLOT_CONNECTED;
    peer_ack_turn[slot] = lockstep_running ? exec_turn : 0;
    /* Snapshot is sent at the next turn boundary so the state is
       captured at a well-defined point in the turn sequence. */
    snapshot_pending[slot] = lockstep_running;
    event->peer->data = (void *)(intptr_t)(slot + 1);  /* player id */
    g_net_connected = true;

    /* Tell the new client which player slot it has. */
    NetPacket id_pkt = {0};
    id_pkt.type  = PKT_ID_ASSIGN;
    id_pkt.extra = slot + 1;
    send_to_peer(event->peer, &id_pkt);

    if (lockstep_running) {
        if (gs) game_set_alert(gs, "Player reconnected - syncing...");
        return;  /* no lobby traffic mid-match */
    }

    /* Send the current RNG seed so the client menus stay in sync
       (the authoritative seed rides in PKT_START_GAME). */
    extern uint32_t _rng;
    NetPacket seed_pkt = {0};
    seed_pkt.type  = PKT_SYNC_SEED;
    seed_pkt.extra = (int32_t)_rng;
    send_to_peer(event->peer, &seed_pkt);

    /* Broadcast updated lobby state to all peers. */
    NetPacket lobby_pkt = {0};
    lobby_pkt.type  = PKT_LOBBY_SYNC;
    lobby_pkt.extra = net_get_peer_count();
    broadcast_to_peers(&lobby_pkt);
    enet_host_flush(host_handle);

    if (gs) game_set_alert(gs, "A player joined the lobby.");
}

static void handle_disconnect(GameState *gs, ENetEvent *event)
{
    if (g_local_player_id == 0) {
        for (int i = 0; i < NUM_PLAYERS - 1; i++) {
            if (peers[i] == event->peer) {
                peers[i] = NULL;
                snapshot_pending[i] = false;
                peer_ack_turn[i] = 0;
                if (lockstep_running) {
                    /* Mid-match: hold the seat for a reconnect. */
                    slot_state[i] = SLOT_RESERVED;
                    if (gs) game_set_alert(gs, "Player disconnected - they can rejoin.");
                } else {
                    slot_state[i] = SLOT_EMPTY;
                    if (gs) game_set_alert(gs, "A player left the lobby.");
                }
                break;
            }
        }
        if (!lockstep_running) {
            NetPacket lobby_pkt = {0};
            lobby_pkt.type  = PKT_LOBBY_SYNC;
            lobby_pkt.extra = net_get_peer_count();
            broadcast_to_peers(&lobby_pkt);
        }
    } else {
        /* Client: the server is our only peer. */
        peers[0] = NULL;
        g_net_connected = false;

        if (lockstep_running) {
            /* Mid-match: keep the lockstep session alive (the sim
               stalls at the current turn) and retry the last host
               address; a snapshot restores us on success. */
            uint32_t now = (uint32_t)enet_time_get();
            if (!reconnecting) {
                reconnecting      = true;
                reconnect_stop_ms = now + NET_RECONNECT_LIMIT_MS;
                if (gs) game_set_alert(gs, "Connection lost - reconnecting...");
            }
            reconnect_next_ms = now + NET_RECONNECT_RETRY_MS;
        } else {
            g_net_active     = false;
            lockstep_running = false;
            reconnecting     = false;
            if (gs) game_set_alert(gs, "Connection to host lost.");
        }
    }
}

/* Client: periodic reconnect attempts while the sim is stalled. */
static void client_try_reconnect(GameState *gs)
{
    if (!reconnecting || peers[0] || !host_handle) return;

    uint32_t now = (uint32_t)enet_time_get();
    if (now >= reconnect_stop_ms) {
        /* Give up: drop to offline play. */
        reconnecting     = false;
        g_net_active     = false;
        lockstep_running = false;
        if (gs) game_set_alert(gs, "Could not reconnect - continuing offline.");
        return;
    }
    if (now < reconnect_next_ms) return;
    reconnect_next_ms = now + NET_RECONNECT_RETRY_MS;

    ENetAddress address;
    if (enet_address_set_host(&address, last_join_ip) != 0) return;
    address.port = (uint16_t)last_join_port;
    peers[0] = enet_host_connect(host_handle, &address, 2, 0);
    /* Failure surfaces as a DISCONNECT_TIMEOUT event → retry. */
}

/* Sanity-check an incoming datagram before touching its fields. */
static bool packet_wire_valid(ENetPacket *ep)
{
    if (ep->dataLength != sizeof(NetPacket)) return false;
    NetPacket *pkt = (NetPacket *)ep->data;
    if (pkt->magic != NET_PROTO_MAGIC) return false;
    if (pkt->version != NET_PROTO_VERSION) return false;
    if (pkt->type >= PKT_TYPE_COUNT) return false;
    return true;
}

static bool packet_is_control(uint8_t type)
{
    return type == PKT_SYNC_SEED || type == PKT_START_GAME ||
           type == PKT_ID_ASSIGN || type == PKT_LOBBY_SYNC ||
           type == PKT_DESYNC;
}

/* Client: adopt a full-state snapshot from the host after a
   mid-match reconnect and resume lockstep at the snapshot turn. */
static void handle_state_sync(GameState *gs, ENetPacket *ep)
{
    if (g_local_player_id == 0) return;  /* host is authoritative */
    if (ep->dataLength != sizeof(StateSyncHeader) + sizeof(GameState)) return;

    StateSyncHeader hdr;
    memcpy(&hdr, ep->data, sizeof(hdr));
    if (hdr.magic != NET_PROTO_MAGIC)        return;
    if (hdr.version != NET_PROTO_VERSION)    return;
    if (hdr.type != PKT_STATE_SYNC)          return;
    if (hdr.state_size != sizeof(GameState)) return;

    extern uint32_t _rng;
    memcpy(gs, ep->data + sizeof(hdr), sizeof(GameState));
    _rng = hdr.rng;

    /* Restart the lockstep clock at the snapshot boundary; the
       host replays the in-flight batches right after this. */
    memset(turn_ring, 0, sizeof(turn_ring));
    exec_turn        = hdr.turn;
    ticks_into_turn  = 0;
    tick_accum       = 0.0f;
    stall_time       = 0.0f;
    desync_reported  = false;
    lockstep_running = true;

    game_set_alert(gs, "Game state resynced.");
}

static void handle_receive(GameState *gs, ENetEvent *event)
{
    ENetPacket *ep = event->packet;

    /* A state snapshot is a large, variably-sized datagram; detect
       it before the fixed-size wire check rejects it. */
    if (ep->dataLength >= sizeof(StateSyncHeader) &&
        ((const StateSyncHeader *)ep->data)->type == PKT_STATE_SYNC) {
        handle_state_sync(gs, ep);
        return;
    }

    if (!packet_wire_valid(ep)) return;
    NetPacket pkt = *(NetPacket *)event->packet->data;  /* copy: packet freed by caller */

    if (g_local_player_id == 0) {
        /* ── Host receive path ──────────────────────────────── */
        int sender = (int)(intptr_t)event->peer->data;
        if (sender <= 0 || sender >= NUM_PLAYERS) return;

        switch ((PacketType)pkt.type) {
        case PKT_TURN_ACK:
            if (pkt.turn > peer_ack_turn[sender - 1]) {
                peer_ack_turn[sender - 1] = pkt.turn;
            }
            break;
        case PKT_CHECKSUM:
            pkt.player = (uint8_t)sender;
            host_check_client_checksum(gs, &pkt);
            break;
        default:
            /* Clients may only submit game commands; control
               packets from a client are spoof attempts. */
            if (packet_is_control(pkt.type) || pkt.type == PKT_TURN_EXEC) break;
            pkt.player = (uint8_t)sender;  /* enforce identity */
            if (lockstep_running) {
                host_queue_command(&pkt);
            }
            /* Commands before match start are ignored. */
            break;
        }
    } else {
        /* ── Client receive path (everything is from the host) ─ */
        switch ((PacketType)pkt.type) {
        case PKT_TURN_EXEC: {
            TurnSlot *slot = ring_slot(pkt.turn);
            if (slot->turn != pkt.turn) {
                /* No commands arrived for this turn: empty batch. */
                slot->turn  = pkt.turn;
                slot->count = 0;
            }
            if (slot->count != pkt.extra) {
                /* Should be impossible on a reliable ordered channel. */
                fprintf(stderr, "Net: turn %u batch has %d/%d commands\n",
                        pkt.turn, slot->count, pkt.extra);
            }
            slot->ready = true;
            break;
        }
        case PKT_TURN_ACK:
        case PKT_CHECKSUM:
            break;  /* host-bound only */
        default:
            if (packet_is_control(pkt.type)) {
                apply_packet(gs, &pkt);
                if (pkt.type == PKT_LOBBY_SYNC) client_lobby_count = pkt.extra;
            } else if (lockstep_running) {
                /* Turn-stamped command: buffer for its turn. */
                TurnSlot *slot = ring_slot(pkt.turn);
                if (slot->turn != pkt.turn) {
                    slot->turn  = pkt.turn;
                    slot->count = 0;
                    slot->ready = false;
                }
                if (slot->count < NET_MAX_TURN_CMDS) {
                    slot->cmds[slot->count++] = pkt;
                }
            }
            break;
        }
    }
}

void net_update(GameState *gs)
{
    if (!g_net_active || !host_handle) return;

    /* Drive mid-match reconnect attempts (no-op unless stalled). */
    client_try_reconnect(gs);

    ENetEvent event;
    while (enet_host_service(host_handle, &event, 0) > 0) {
        switch (event.type) {
            case ENET_EVENT_TYPE_CONNECT:
                printf("ENet connected!\n");
                handle_new_connection(gs, &event);
                break;

            case ENET_EVENT_TYPE_RECEIVE:
                handle_receive(gs, &event);
                enet_packet_destroy(event.packet);
                break;

            case ENET_EVENT_TYPE_DISCONNECT:
            case ENET_EVENT_TYPE_DISCONNECT_TIMEOUT:
                printf("ENet disconnected.\n");
                handle_disconnect(gs, &event);
                break;

            case ENET_EVENT_TYPE_NONE:
                break;
        }
        if (!g_net_active || !host_handle) return;  /* disconnected mid-loop */
    }
}

/* ── Dispatch: local command entry point ───────────────────────
 * Offline: apply immediately (single-player path, unchanged).
 * In a lockstep match: never apply now — route to the host, who
 * schedules it into a future turn for everyone at once.
 * In the lobby (net active, match not started): host control
 * packets broadcast + apply immediately. */
void net_dispatch_packet(GameState *gs, NetPacket *pkt)
{
    pkt->player = (uint8_t)net_get_local_player();

    if (!g_net_active) {
        apply_packet(gs, pkt);
        return;
    }

    if (lockstep_running && !packet_is_control(pkt->type)) {
        if (g_local_player_id == 0) {
            host_queue_command(pkt);
        } else if (peers[0]) {
            send_to_peer(peers[0], pkt);
            enet_host_flush(host_handle);
        }
        return;
    }

    net_send_packet(pkt);
    apply_packet(gs, pkt);
}
