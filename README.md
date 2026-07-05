# Sangora RTS

Sangora is a high-performance, real-time strategy (RTS) game engine written in C11. Built on top of Raylib for cross-platform rendering and ENet for multiplayer networking, Sangora features a stateful simulation model, a deterministic lockstep command replication layer, procedural map generation, a 6-mission scripted campaign, and a unique first-person Hero Possession mode.

The engine has been architected and optimized to meet a target of 100 FPS on legacy mobile hardware through custom spatial partitioning, system throttling, and static memory allocation strategy.

---

## Table of Contents

- [Engine Architecture & Technical Details](#engine-architecture--technical-details)
- [Algorithms & Data Structures Reference](#algorithms--data-structures-reference)
- [Rendering Pipelines](#rendering-pipelines)
- [Multiplayer & Networking](#multiplayer--networking)
- [AI Architecture](#ai-architecture)
- [Gameplay Systems](#gameplay-systems)
- [Map Generation](#map-generation)
- [Hero Possession Mode](#hero-possession-mode)
- [Campaign System](#campaign-system)
- [Technology & Upgrade System](#technology--upgrade-system)
- [Build & Platform Architecture](#build--platform-architecture)
- [Codebase Map](#codebase-map)
- [Getting Started](#getting-started)
- [Mobile Build (Android)](#mobile-build-android)
- [Game Controls](#game-controls)

---

## Engine Architecture & Technical Details

### High-Level Architecture

The codebase is organized into seven distinct layers:

| Layer | Files | Responsibility |
|-------|-------|----------------|
| **Entry/Runtime** | `main.c` | Window, audio, main loop (`Input → Net → Update → Render`) |
| **Core Simulation** | `game.c`, `game.h` | `GameState`, entities, phases, campaign, projectiles |
| **World/Rules** | `map.c`, `resources.c`, `building.c`, `unit_orders.c`, `unit_ai.c`, `hero_possession.c` | Gameplay mechanics |
| **AI** | `ai.c` | Computer-controlled opponent |
| **Networking** | `net.h`, `net.c` | ENet wrapper, command replication |
| **Content/MapGen** | `osm_mapgen.c`, `osm_mapgen.h` | OpenStreetMap-derived map generation |
| **UI/Input/Render** | `ui_state.*`, `ui/input/*`, `ui/hud/*`, `ui/renderer/*` | Selection, commands, HUD, rendering |

The project uses a **stateful, array-of-objects, immediate-mode** architecture — no ECS, no job system, no external engine framework. All mechanics are encoded directly in plain C logic with `GameState` as the single source of truth.

### Central Simulation Loop and Determinism

- **Single-Threaded Gameplay Logic**: The gameplay simulation runs entirely on a single thread to guarantee deterministic execution across platforms — eliminating race conditions, lock overhead, and scheduling discrepancies that would cause multiplayer desyncs.
- **Worker Thread Pool for Data-Parallel Scans**: A persistent `pthread`-based thread pool (`threadpool.c`) accelerates O(N²) simulation scans (fog-of-war rasterization, unit/tower target acquisition) when the live unit count exceeds a `TP_UNIT_SCAN_THRESHOLD` of 64. Workers claim chunks via a **lock-free atomic CAS cursor** packed as `(generation << 32 | next_index)` into a single 64-bit atomic — mismatched generations cause automatic backoff. Workers spin briefly (`TP_SPIN_BUDGET = 20000` iterations) before parking on a condvar, optimized for many small dispatches per 60 Hz frame. The calling thread participates in every dispatch, yielding N+1 lanes. Jobs must write only to per-index disjoint output slots to preserve determinism.
- **Fixed-Delta Clamping**: The simulation time-delta (`dt`) is strictly clamped to a ceiling of `0.05f` seconds, protecting physics and mechanics from the "spiral of death" during intense lag or frame-drops.
- **Precise Update Sequencing**: Each frame's operations execute in strict chronological order:
  1. Alert timers tick down
  2. Campaign briefing early-out check
  3. Hero possession lifecycle updates
  4. Simulation time scale computed
  5. Population caps recomputed
  6. Age advancement timers update
  7. Units update (movement, gathering, combat, AI state machines)
  8. Buildings update (construction, production, research, defensive fire)
  9. Projectiles update (travel, impact, splash)
  10. AI updates (if applicable)
  11. Campaign scripting updates (if applicable)
  12. Fog-of-war recalculation

### Memory Model — Zero Runtime Allocations

The engine performs **zero dynamic memory allocations** (`malloc`/`free`) during the runtime game loop. All major entities reside in preallocated, contiguous static arrays inside `GameState`:

| Entity | Max Count | Storage |
|--------|-----------|---------|
| Units | 256 | `Unit units[MAX_UNITS]` |
| Buildings | 128 | `Building buildings[MAX_BUILDINGS]` |
| Projectiles | 128 | `Projectile projectiles[MAX_PROJECTILES]` |
| A* Path Nodes per Unit | 300 | `PathCell path[ASTAR_PATH_CAP]` |
| Map Tiles | 64×64 | `Tile map[MAP_H][MAP_W]` |
| Players | 8 | `PlayerRes res[NUM_PLAYERS]` |

Active flags determine slot occupancy. This eliminates heap fragmentation, prevents cache misses, and guarantees predictable performance on constrained devices.

### Throttled Systems

Non-critical operations are deferred to reduced frequencies:

| System | Update Rate | Savings |
|--------|------------|---------|
| Fog-of-War rasterization | `FOG_UPDATE_INTERVAL = 0.2f` (5 Hz) | ~83% CPU reduction vs. per-frame |
| AI think cycle | Every 0.5 seconds | Avoids per-frame overhead |
| AI major attack cooldown | Every 60 seconds | Rate-limited attack waves |

### Deterministic Math

#### 32-bit Xorshift PRNG
```c
_rng ^= _rng << 13;
_rng ^= _rng >> 17;
_rng ^= _rng <<  5;
```
The host seeds and synchronizes this generator to all clients via `PKT_SYNC_SEED` to ensure parallel random choices. Helper functions provide range integers (`rng_range`) and normalized floats (`rng_frac`).

#### Platform-Deterministic Trigonometry
Standard `libm` functions (`sinf`, `cosf`, `atan2f`) are **not bit-identical** across platforms (macOS libm vs. Android bionic). The engine replaces them with pure-float approximations:

- **`det_sinf`**: Parabolic approximation with one refinement pass. Range-reduced to `[-π, π]` via integer truncation. Accuracy: ~1e-3 rad.
- **`det_cosf`**: Phase-shifted `det_sinf(x + π/2)`.
- **`det_atan2f`**: Minimax polynomial (`0.0776x⁴ − 0.2874x² + 0.9952x`) with quadrant correction.

Compiled with **`-ffp-contract=off`** and without `-ffast-math` to ensure IEEE-exact `fabsf`/`sqrtf` and no FMA contraction differences across compilers.

#### Floating-Point Alignment
Coordinate systems are strictly normalized, and speed variables are constrained to prevent precision accumulation drift. All workspace arrays (`cost_so_far`, `came_from`, `visited`) are `_Thread_local static` to be safe for the worker pool.

---

## Algorithms & Data Structures Reference

### 1. Pathfinding — A* Search

**Implementation**: `src/core/pathfinding.c`

| Property | Value |
|----------|-------|
| Grid | 64 × 64 tiles |
| Directions | 8 (cardinal + diagonal) |
| Orthogonal cost | 1.0 |
| Diagonal cost | 1.414 |
| Heuristic | **Octile distance** |
| Priority queue | **Binary min-heap** (static, `PQ_CAP = 8192`) |

**Octile Heuristic**:
```c
float octile_heuristic(int ax, int ay, int bx, int by) {
    int dx = abs(bx - ax), dy = abs(by - ay);
    return (dx > dy) ? (0.414f * dy + dx) : (0.414f * dx + dy);
}
```
This is admissible and consistent with 8-directional movement costs, guaranteeing optimal paths.

**Key Strategies**:
- **Diagonal Blocker Checking**: Diagonal moves are blocked unless both adjacent orthogonal tiles are passable — preventing units from slicing through solid tile corners.
- **Non-Passable Goal Tolerance**: The goal tile itself may be impassable (for attack/gather targets toward blocked resources/buildings). Only intermediate tiles are checked.
- **Static Workspace**: All arrays (`cost_so_far[4096]`, `came_from[4096]`, `visited[4096]`) plus the priority queue (~64 KB) are `_Thread_local static` — avoiding stack overflow on Android's ~1 MB game-thread stack and eliminating per-call heap allocations.
- **Path Reconstruction**: Backwards trace via `came_from[]` into a reversed path buffer, then written in forward order.

### 2. Priority Queue — Binary Min-Heap

**Implementation**: Inline in `src/core/game.h`

- Capacity: 8192 nodes
- Operations: `pq_push` (O(log N) sift-up), `pq_pop` (O(log N) sift-down)
- Static allocation, zero dynamic memory
- Stores `{cell, f}` pairs where `f` is the estimated total cost

### 3. Adjacent-Tile Search

For buildings and resource gathering, path targets are often blocked tiles. Three complementary helpers find navigable perimeter tiles:

- **`find_adjacent_tile()`**: Searches the 8 neighbors of a target tile for the nearest passable one.
- **`map_find_passable_near()`**: Expanding ring search outward from a center tile.
- **`unit_find_free_tile_near()`**: Finds unoccupied passable tiles near buildings, avoiding collision with other units already pathfinding there.

### 4. Formation Target Allocation

**Implementation**: `unit_compute_formation_targets()` in `unit_orders.c`

When issuing move orders to multiple units:
- Grid-based arrangement: `width ≈ sqrt(unit_count)`, capped at 5
- Each target tile is jittered to nearby free passable tiles via `map_find_passable_near()`
- Prevents mass-move clustering on a single tile

### 5. Spatial Partitioning & Collision Resolution

**Implementation**: `unit_apply_separation()` in `unit_ai.c`

#### Spatial Grid (O(N) Collision)
The map is subdivided into a `64×64` coarse grid. Each frame:
1. Units are assigned to grid cells based on their world position
2. Collision checks only examine the 3×3 neighborhood of adjacent cells
3. Reduces collision complexity from **O(N²) to O(N)** — ~98% CPU savings

```c
#define SEP_CELL_CAP 12  // max units tracked per cell
static int sep_grid[MAP_H][MAP_W][SEP_CELL_CAP];
```

#### Multi-Pass Iterative Separation
**2 passes** of position resolution per tick, resolving overlaps gradually to minimize computational snapping artifacts.

#### Orthogonal Vector Sliding
When two units collide head-on, a perpendicular "spin" force is added:
```c
float ortho_x = -dy * push * 0.2f;
float ortho_y =  dx * push * 0.2f;
```
This lateral vector allows units to slide smoothly past each other instead of jamming in chokepoints.

#### Deterministic Nudge for Co-located Units
When two units land on the exact same pixel position (`dist < 0.001`), a deterministic angle is derived from their IDs:
```c
float angle = (float)(((a->id * 37 + b->id * 17) % 360) * (PI / 180.0f));
```
This avoids non-deterministic tie-breaking in multiplayer.

#### Passability Validation
Push vectors are validated against tile passability — units are never pushed into water or other impassable terrain.

### 6. Coordinate System Transforms

| Space | Representation | Use |
|-------|---------------|-----|
| **Tile Space** | Integer `(tx, ty)` | Pathfinding, building footprints, fog, resources |
| **World Space** | Float `(wx, wy)` in pixels | Continuous unit movement |
| **Isometric Screen** | 2:1 projection | Visual rendering |
| **3D Hero Space** | Raylib 3D camera | First-person possession mode |

**Isometric Projection** (2:1):
```c
ScreenX = WorldX - WorldY
ScreenY = (WorldX + WorldY) * 0.5f
```

**Inverse**:
```c
WorldX = ScreenY + ScreenX * 0.5f
WorldY = ScreenY - ScreenX * 0.5f
```

### 7. View-Frustum Culling

The render layer computes bounding circles for isometric tiles, buildings, and units, discarding render passes for entities positioned outside the active screen viewport.

### 8. Depth Sorting

Isometric rendering uses world-position-based depth sorting to ensure correct visual layering of overlapping sprites.

---

## Rendering Pipelines

### Hybrid 2D/3D Billboarding Pipeline

#### 2D Isometric Renderer
Standard RTS viewpoint featuring:
- 2:1 isometric projection with automatic tile variation
- Wall-connectivity linking (procedural sprite selection based on neighbor state)
- Dynamic depth sorting
- View-frustum culling
- Fog overlay rendering (three fog states: hidden/explored/visible)
- Building sprites with per-building scaling heuristics
- Rally point previews and selection outlines

#### 3D Hero Possession Renderer
A separate rendering path over the same simulation state:
- Tiles rendered as cubes/planes in 3D
- **Billboard sprites**: 2D unit/building assets rendered in 3D space as camera-facing billboards
  ```c
  float dx = camera.position.x - sprite.position.x;
  float dz = camera.position.z - sprite.position.z;
  float angle = atan2f(dx, dz) * RAD2DEG;
  ```
- FPS-style weapon overlay
- 3D projectile arcs
- Crosshair spread mechanics
- Screen-space impact/blur effects
- Simple 3D representations for units and imported 3D models for structures

### HUD System

Modular HUD provides:
- Top resource bar and age status display
- Bottom unit/building/action panel
- Minimap with real-time unit tracking
- Alert notifications
- Build menu with placement ghost
- Campaign briefing overlays
- End-screen (victory/defeat) overlays
- Side-by-side OSM and game map overview in sandbox/OSM flows

---

## Multiplayer & Networking

### Protocol: Deterministic Lockstep Command Replication

**Implementation**: `src/core/net.c`, `src/core/net.h` using bundled single-header ENet (`src/core/enet/enet.h`)

Rather than sending large state snapshots, the engine transmits compact, intent-based **command packets** (`NetPacket`):

```c
#pragma pack(push, 1)
typedef struct {
    uint8_t  type;
    uint8_t  player;
    uint16_t unit_count;
    uint16_t units[64];
    int32_t  tx, ty;
    int32_t  target_id;
    int32_t  extra;
} NetPacket;
#pragma pack(pop)
```

### Topology

- **Star topology**: Host (player 0) connects up to 3 client peers
- Host: `enet_host_create(&address, 3, 2, 0, 0)` — 3 peers, 2 channels
- Client: `enet_host_create(NULL, 1, 2, 0, 0)` — 1 outgoing peer

### Connection Flow

1. Host creates ENet server, sets `g_local_player_id = 0`
2. Client connects via `net_join(ip, port)`, `g_local_player_id = -1` (pending)
3. On connect, host sends `PKT_ID_ASSIGN` (assigns player ID) and `PKT_SYNC_SEED` (synchronizes RNG)
4. Host broadcasts `PKT_LOBBY_SYNC` with peer count

### Dual Dispatch Architecture

```
Local input → Fill NetPacket → net_dispatch_packet()
                                    ├─→ net_send_packet()     [ENet reliable broadcast]
                                    └─→ apply_packet()        [Immediate local application]
```

Local commands are applied immediately for responsiveness. Remote machines receive the same packet and call `apply_packet()` on their local `GameState`.

### Packet Types

| Packet | `player` | `units[]` | `tx,ty` | `target_id` | `extra` |
|--------|----------|-----------|---------|-------------|---------|
| `PKT_SYNC_SEED` | — | — | — | — | RNG seed |
| `PKT_START_GAME` | — | — | — | — | Player count |
| `PKT_MOVE` | Issuer | Units to move | Destination tile | — | — |
| `PKT_GATHER` | Issuer | Villagers | Resource tile | — | — |
| `PKT_ATTACK` | Issuer | Attackers | — | Target ID | 0=unit, 1=building |
| `PKT_BUILD` | Issuer | Builders | — | Building ID | — |
| `PKT_PLACE_BLD` | Issuer | Builders | Placement tile | — | Building type |
| `PKT_TRAIN_UNIT` | Issuer | — | — | Building ID | Unit type |
| `PKT_DELETE_BLD` | Issuer | — | — | Building ID | — |
| `PKT_AGE_ADVANCE` | Issuer | — | — | — | — |
| `PKT_STANCE` | Issuer | Affected units | — | — | Manual flag |
| `PKT_RESEARCH` | Issuer | — | — | Building ID | Tech type |
| `PKT_SET_RALLY` | Issuer | — | Rally tile | Building ID | — |

### Reliability Model

- All packets sent with `ENET_PACKET_FLAG_RELIABLE`
- No state snapshots, checksums, rollback, or resync mechanisms
- Correct operation depends on all peers starting from the same seed and receiving the same commands

---

## AI Architecture

**Implementation**: `src/ai/ai.c`

The AI is a **rule-based** system (no machine learning) controlling player 1 in single-player mode.

### Runtime Cadence

| Parameter | Value |
|-----------|-------|
| Think interval | 0.5 seconds |
| Major attack cooldown | 60 seconds |

### AI Phases

The AI uses coarse descriptive phases (`gather` → `build` → `military` → `attack`), but these are labels — not a blocking state machine. Multiple behaviors can run simultaneously.

### Economic Management Strategy

- **Idle Villager Recovery**: Detects and reassigns stuck/idle gatherers
- **Dynamic Worker Allocation**: Target counts by resource type, prioritizing food and wood in Dark Age, adding gold/stone later
- **Age-Up Saving**: Reserves resources when near age advancement thresholds
- **Target villager counts**: ~22 in Dark Age, ~24 in Feudal

### Build Order Heuristics

**Dark Age priorities** (in order):
1. Lumber Camp → 2. Mill → 3. Mining Camp (once eco stable) → 4. Barracks (once vils + wood threshold met)

**Feudal/Castle/Imperial** expand into: Archery Range, Stable, Blacksmith, Market, Monastery, Siege Workshop, University, Watch Tower

### Just-in-Time Housing Scheduler

Computes current population headroom (factoring active queue targets) and dynamically issues house placement orders with:
- Free-pop threshold check
- Maximum 2 houses pending simultaneously
- Prevents training blockages

### Scout Exploration Heuristic

1. Prefer hidden tiles near town center (with extra priority for hidden resource tiles)
2. Roam among landmark set: corners, edge midpoints, map center
3. Lightweight proximity-biased exploration, not a frontier-search algorithm

### Research Logic

Gated by age, resources, army composition, villager count, and building existence. Tries one research per think pass, stops after first success.

### Attack Behavior

**Age-based minimum army threshold**:

| Age | Min Military |
|-----|-------------|
| Dark | 4 |
| Feudal | 6 |
| Castle | 8 |
| Imperial | 10 |

**Targeting order**:
1. Enemy Town Center (if found)
2. Nearest enemy complete building
3. Nearest enemy units (when close)

Groups idle military near rally point until threshold is met, then launches waves every 60 seconds.

---

## Gameplay Systems

### Economy

**Four resources**: Food, Wood, Gold, Stone

**Villager Gather Rates** (per second):

| Resource | Base Rate | Tech Multipliers |
|----------|-----------|------------------|
| Food | 0.4 | Hand Mill +15%, Irrigation +15% |
| Wood | 0.5 | Double-Bit Axe +15%, Bow Saw +15%, Two-Man Saw +20% |
| Gold | 0.33 | — |
| Stone | 0.28 | — |

**Gathering Algorithm** (`unit_do_gather()` in `unit_ai.c`):
1. Villager must be adjacent to target
2. Accumulate `anim_timer += rate * multiplier * dt`
3. Extract integer amount when `anim_timer >= 1`
4. Subtract from tile/farm resource
5. Increment carry amount
6. When carry cap reached → compute drop-off path

**Carry Capacity**: Base 10, modified by tech (Granary Baskets +1, Reaping +2 for food; Log Straps +1, Hardwood Carts +2 for wood)

**Drop-off Buildings**:

| Resource | Valid Drop-offs |
|----------|----------------|
| Food | Town Center, Mill |
| Wood | Town Center, Lumber Camp |
| Gold/Stone | Town Center, Mining Camp |

### Farming System

- Farm footprint becomes `TILE_FARM` (remains passable)
- Base farm food: 400
- Crop Rotation: +75 food; Fertilizer: +125 food
- Farm food tracked in both building and tile resource amounts

### Population System

- Base pop cap: 5
- Each completed house: +5
- Hard ceiling: 200
- Queued units count toward pop-cap checks
- Sandbox forces cap to 200

### Combat Model

**Attack Flow** (`unit_do_attack()`):
1. Validate target (alive, active)
2. Auto-acquire if target lost and not in manual stance
3. If out of range: periodically repath with cooldown via `anim_timer`, spread around targets using slot heuristics, allow "extended melee" fallback when pathing blocked near target
4. If in range: stop pathing, attack when cooldown elapsed

**Damage Formula**:
- Unit vs Unit: `damage = attack + bonuses - armor` (minimum 1)
- Unit vs Building: `damage = attack + anti-building bonus`

**Bonus Damage System**:

| Attacker | Target | Bonus |
|----------|--------|-------|
| Spearman | Cavalry/Cav Archer | +8 |
| Skirmisher | Archer/Cav Archer | +7 |
| Scout | Monk | +10 |
| Archer | Spearman | +2 |
| Knight | Archer/Skirmisher/Monk | +2 |
| Cavalry Archer | Spearman | +3 |
| Scorpion | Ranged units | +4 |
| Battering Ram | Buildings | +28 |
| Mangonel | Buildings | +8 |
| Bombard Cannon | Buildings | +18 |
| Militia/Man-at-Arms | Buildings | +2 |
| Knight | Buildings | +1 |

### Projectile System

Explicit projectile simulation (not hitscan):
- **Types**: `PROJ_ARROW`, `PROJ_BOLT`, `PROJ_STONE`
- Travel time: distance-based
- Stone projectiles: splash damage in radius on impact
- Arc height for visual parabolic trajectories

**Projectile-firing units**: Archer, Skirmisher, Cavalry Archer, Mangonel, Scorpion, Bombard Cannon
**Projectile-firing buildings**: Town Center, Watch Tower, Castle

### Monk Mechanics

- **Idle**: Auto-heal nearby damaged allied units
- **Attack order**: Converts enemy unit instead of damaging
  - Transfers team, decrements old population, increments new
  - Resets converted unit to idle
- Monks cannot attack buildings

### Death System

When `hp <= 0`:
1. State → `US_DYING`, `death_timer = 0.8f`
2. Timer expires → `US_DEAD`, `active = false`, population decremented

### Unit Roster (14 types)

| Unit | HP | Atk | Armor | Range | Vision | Speed | CD | Carry |
|------|---:|----:|------:|------:|-------:|------:|---:|------:|
| Villager | 25 | 3 | 0 | 1.3 | 4.0 | 80 | 1.5 | 10 |
| Scout | 45 | 3 | 0 | 1.3 | 7.0 | 130 | 2.0 | 0 |
| Militia | 40 | 4 | 0 | 1.3 | 4.0 | 95 | 2.0 | 0 |
| Man-at-Arms | 45 | 6 | 2 | 1.3 | 4.0 | 95 | 2.0 | 0 |
| Spearman | 45 | 3 | 0 | 1.3 | 4.0 | 90 | 2.0 | 0 |
| Archer | 30 | 5 | 0 | 5.0 | 6.0 | 90 | 2.0 | 0 |
| Skirmisher | 35 | 3 | 1 | 4.5 | 6.0 | 92 | 1.9 | 0 |
| Cavalry Archer | 50 | 6 | 0 | 5.0 | 7.0 | 110 | 2.2 | 0 |
| Knight | 100 | 10 | 3 | 1.3 | 4.0 | 115 | 2.0 | 0 |
| Monk | 30 | 0 | 0 | 5.0 | 8.0 | 80 | 6.0 | 0 |
| Battering Ram | 175 | 2 | 4 | 1.2 | 4.5 | 52 | 2.8 | 0 |
| Mangonel | 60 | 35 | 0 | 7.0 | 8.0 | 58 | 4.5 | 0 |
| Scorpion | 55 | 16 | 1 | 6.0 | 8.0 | 62 | 3.1 | 0 |
| Bombard Cannon | 75 | 42 | 1 | 8.5 | 9.0 | 56 | 5.0 | 0 |

### Building System

**18 building types** with footprints from 1×1 (Wall, Gate) to 4×4 (Town Center, Stable, Castle).

**Placement Algorithm** (`building_place()`):
1. Check tile buildability across full footprint
2. Check age requirement
3. Check affordability
4. Find free building slot in static array
5. Deduct resources
6. Initialize incomplete foundation
7. Mark map footprint as occupied immediately

**Construction**: Base rate `0.035/s` (×1.5 with Treadmill Crane). **Repair**: `34 HP/s` (×1.35 with Treadmill Crane).

**Production Queue**: Capacity 5, only complete buildings can queue, cannot train while researching. Queued population counts toward pop-cap.

**Defensive Buildings** (auto-acquire nearest enemy in range):

| Building | Damage | Range |
|----------|-------:|------:|
| Town Center | 5 | 6 |
| Watch Tower | 6 | 8 |
| Castle | 8 | 8 |

**Building Sell**: Refunds 95% of each cost component.

### Age Progression

| Transition | Cost | Duration |
|------------|------|----------|
| Dark → Feudal | 400 Food | 20s |
| Feudal → Castle | 500 Food, 100 Wood | 16s |
| Castle → Imperial | 600 Food, 400 Gold | 12s |

Duration formula: `20.0f - age * 4.0f`

---

## Map Generation

### Standard Random Map (`map.c`)

**Pipeline** (`map_init()`):
1. Fill map with grass
2. Choose one start quadrant per player (opposite for 2P, shuffled for 3-4P)
3. Place random lakes (elliptical placement)
4. Place random forest clusters (circular cluster painting)
5. Place random gold, stone, berry clusters
6. Clear starter safe zones
7. Place guaranteed starting resources around each Town Center

**Start Placement Strategy**:
- Map split into quadrants
- Random starts sampled inside quadrants (not hardcoded corners)
- 2-player: opposite quadrants; 3-4 player: shuffled quadrants

**Guaranteed Starting Resources** (`place_start_resources()`):
- 3 forest patches, 2 berry patches, 1 gold patch, 1 stone patch
- Directions shuffled using **Fisher-Yates** over 8 compass directions
- Distances randomized and jittered

**Terrain Passability**:
- Impassable: Water, Forest, Gold, Stone, Berries
- Passable: Grass, Desert, Road, Farm

### OSM-Derived Map Generation (`osm_mapgen.c`)

**Pipeline**:
1. **Geocoding**: `geocode(location_name)` via Nominatim API → GPS bounding box
2. **Feature Fetch**: Overpass API query → roads, water, forests, rivers, sand, residential zones, buildings
3. **Rasterization**: Vector features → 64×64 tile grid
4. **Gameplay Placement**: Starting positions, neutral buildings, guaranteed resources
5. **Tile Download**: OSM raster tiles for minimap/overview display

**Algorithms used**:

| Algorithm | Purpose | Implementation |
|-----------|---------|----------------|
| **Point-in-Polygon (Ray Casting)** | Rasterize closed polygons (lakes, residential zones) | `point_in_way()` — casts horizontal rays, counts boundary edge crossings |
| **Point-to-Line-Segment Distance** | Rasterize linear features (roads, rivers) | `point_line_dist()` — parametric projection with clamped `t ∈ [0,1]` |
| **Cellular-Automata Forest Growth** | Organic forest clusters | `grow_forest_blob()` — recursive generation with `count_forest_neighbors()` |
| **Forest Shape Smoothing** | Remove isolated spikes | `smooth_forest_shapes()` — neighbor-count cellular automata pass |
| **Neutral Building Placement** | Urban landmarks along roads | Road-adjacency scoring, intersection-bias heuristic, breathing-room validation |

### Fog of War

**Three states**: `FOG_HIDDEN`, `FOG_EXPLORED`, `FOG_VISIBLE`

**Update rules** (`map_update_fog()`):
1. Sandbox forces all tiles visible
2. Existing `VISIBLE` tiles demoted to `EXPLORED`
3. Units reveal circular radius based on `vision_range`
4. Complete buildings reveal fixed 4-tile radius from footprint center

---

## Hero Possession Mode

**Implementation**: `src/core/hero_possession.c`

A temporary first-person control mode for one unit in solo play.

### Eligible Units
- ✅ Scout, Militia, Man-at-Arms, Spearman, Archer, Skirmisher, Cavalry Archer, Knight
- ❌ Villager, Monk, Siege units

### Lifecycle

| Phase | Duration |
|-------|----------|
| Entering | 0.65s (transition) |
| Active | 22s |
| Exiting | 0.65s (transition) |
| Cooldown | 45s |

### Time Scaling
- During entering/exiting transitions: simulation at **0.42×** speed
- During active possession: normal speed

### Mechanics
- RTS pathing and targets cleared on possessed unit
- Direct WASD movement in world space
- Amplified damage for first-person attacks
- **Dodge**: Heavy damage reduction, consumes stamina, grants brief invulnerability frames
- **Block**: Moderate damage reduction for front-facing melee
- **Interact**: Break resources or damage buildings
- Mode ends on: timer expiry, unit death, or player cancel

### 3D Camera System
- Mouse-look yaw/pitch control
- Camera shake, blur, and impact timers for feedback
- Stamina system for run and dodge

---

## Campaign System

**6 scripted missions**, hand-authored in C:

| # | Mission | Objective |
|---|---------|-----------|
| 1 | Landfall | Gather 120 food and 80 wood |
| 2 | A Home In The Vale | Build 2 houses, mill, lumber camp, reach 6 villagers |
| 3 | Iron In The Hills | Build mining camp, barracks, 2 farms, advance to Feudal |
| 4 | The Broken Watch | Build archery range, train troops, destroy enemy barracks (scripted raid wave) |
| 5 | Hold The Ford | Survive 3 timed waves protecting the town |
| 6 | Break The Ashen Banner | Destroy enemy TC (with allied reinforcement wave) |

- Missions 0-2: Solo economy focused
- Missions 3-5: Include enemy player
- Mission 5: First mission where the general AI loop runs
- Briefing overlay with story/objective text, dismissal pauses gameplay

---

## Technology & Upgrade System

### Upgrade Application Model

Upgrades are **not applied incrementally** as ad-hoc modifiers. Instead:
1. Unit/building stats reset to base values
2. Category-specific bonuses re-applied from unlocked boolean tech flags

This makes the system robust and deterministic — no floating-point accumulation drift from cascading multipliers.

### Technology Categories

**63 total technologies** across research buildings:

| Building | Research Categories |
|----------|-------------------|
| Town Center | Loom, Wheelbarrow, Hand Cart |
| Mill | Food economy (Hand Mill, Irrigation, etc.) |
| Lumber Camp | Wood economy (Double-Bit Axe, Bow Saw, etc.) |
| Barracks | Infantry (Iron Weaponry, Squires, etc.) |
| Archery Range | Archery (Composite Bows, Thumb Ring, etc.) |
| Stable | Cavalry (Mounted Armor, Husbandry, etc.) |
| Monastery | Monk (Sanctity, Devotion, Fervor, etc.) |
| Siege Workshop | Siege (Reinforced Ram, Siege Engineers, etc.) |
| Blacksmith | Armor/Melee/Arrow (Scale Armor, Blast Furnace, etc.) |
| University | Structure (Masonry, Architecture, Fortified Wall, etc.) |

### Notable Upgrade Effects
- Masonry/Architecture: Percentage HP boosts to buildings
- Fortified Wall: +600 HP to walls and gates
- Hoardings: +500 HP to Town Centers and Castles
- Guard Tower/Keep/Missile Guidance: Modify tower attack and range
- Heated Shot: Anti-siege bonus to defensive structures
- Chemistry: Modifies tower damage calculations
- Cannon Emplacements: Gates Bombard Cannon training

---

## Build & Platform Architecture

### Desktop Build (macOS)

```
Compiler: gcc (C11)
Flags: -std=c11 -Wall -Wextra -O3 -ffp-contract=off -g -pthread
Defines: -DRTS_HAS_CURL=1
Dependencies: raylib (Homebrew), curl (Homebrew)
Frameworks: OpenGL, Cocoa, IOKit, CoreFoundation, CoreVideo, CoreAudio, AudioToolbox
```

### Cross-Platform CI (GitHub Actions)

| Platform | Architecture | Notes |
|----------|-------------|-------|
| macOS | arm64 | Native Homebrew build |
| Linux | amd64 | Raylib built from source |
| Linux | arm64 | Cross-compiled, Raylib from source |
| Windows | x86_64 | LLVM-MinGW cross-compiler |
| Windows | aarch64 | LLVM-MinGW cross-compiler |
| Android | arm64 | Gradle + CMake + NDK |

### Android Build

- Gradle wrapper with CMake and Android NDK (version 25.2.9519653+)
- Raylib fetched via CMake `FetchContent`
- Game compiled as shared library `librts.so`
- Linker option `-u ANativeActivity_onCreate` prevents NativeActivity entrypoint stripping
- `InitWindow(0, 0, "Sangora")` adopts device resolution

### Platform-Specific Notes

- Windows build includes header compatibility fix in `net.h` to avoid collisions between Windows APIs and raylib symbols
- Required Windows link libraries: `raylib`, `winmm`, `gdi32`, `opengl32`, `ws2_32`, `iphlpapi`
- OSM generation disabled when `RTS_HAS_CURL` is not defined

---

## Codebase Map

| File | Responsibility |
|------|---------------|
| `src/main.c` | Application entry point, main loop coordinator |
| `src/core/game.h` | Master header: `GameState`, all types, constants, inline functions |
| `src/core/game.c` | Core updating, projectile math, campaign scripting, initialization |
| `src/core/pathfinding.c` | A* pathfinding with static heap workspace |
| `src/core/map.c` | Procedural landscape, resource distribution, fog-of-war |
| `src/core/building.c` | Placement, construction, production queues, resource drop-off, defensive fire |
| `src/core/resources.c` | Economy checking, age advancement, technology tree application |
| `src/core/hero_possession.c` | Hero state machine, decay timers, physical properties |
| `src/core/unit/unit_orders.c` | Unit command dispatching, formation computing, spawn parameters |
| `src/core/unit/unit_ai.c` | AI state machines for gathering, building, movement, combat, separation |
| `src/core/net.c` | Network event processing, packet construction, connection handling |
| `src/core/net.h` | Packet types, network API declarations |
| `src/core/osm_mapgen.c` | OSM feature extraction, geocoding, rasterization, neutral city placement |
| `src/core/threadpool.c` | Lock-free worker pool with atomic CAS cursor for parallel scans |
| `src/core/threadpool.h` | Thread pool interface and determinism rules |
| `src/core/cJSON.c/h` | JSON parser for OSM API responses |
| `src/ai/ai.c` | Computer player: economy, construction, military, attack AI |
| `src/ui/ui_state.c` | Asset loading, lifecycle management, camera interpolation |
| `src/ui/ui_state.h` | `UIState` structure (camera, selection, textures, models, sounds) |
| `src/ui/input/` | Camera navigation, click-box selections, context order creation |
| `src/ui/renderer/` | Isometric sprites, ground tiles, 3D possession rendering |
| `src/ui/hud/` | Selection panels, tech grids, resource counters, minimaps, overlays |

---

## Getting Started

### Prerequisites

#### macOS
Ensure you have Homebrew installed, then fetch the system dependencies:
```bash
brew install raylib curl enet
```

#### Linux (Debian/Ubuntu)
Install standard build essentials and development libraries:
```bash
sudo apt-get update
sudo apt-get install build-essential libraylib-dev libcurl4-openssl-dev libenet-dev
```

### Native Build & Run

To build the executable and execute the game locally:
```bash
make clean
make -j$(sysctl -n hw.ncpu)
make run
```

---

## Mobile Build (Android)

Sangora compiles natively to Android, utilizing Gradle, CMake, and the Android NDK to produce a high-performance APK.

### Android Prerequisites
Ensure the following environment configurations are set:
1. Android SDK Command-line Tools installed.
2. Android NDK (specifically version 25.2.9519653 or newer).
3. ANDROID_HOME set in your environment paths.

### Compile APK
Build the project for Android ARM targets:
```bash
make android
```

### Deploy and Run
To automatically clean, compile, install to a USB-connected device, and stream trace logs:
```bash
make android-deploy
```

---

## Game Controls

### 2D RTS Mode
- Pan Camera: WASD or Middle Mouse Click + Drag
- Zoom Camera: Mouse Scroll Wheel
- Select Unit: Left Click (Hold and drag for Box Selection)
- Action/Move Order: Right Click on ground, resource tile, or building
- Place Building: Select building button on the bottom HUD, drag ghost overlay, Left Click to build
- Cancel Placement: Escape or Right Click
- Clear Selection: Escape

### 3D Hero Possession Mode
- Possess Selected Unit: Select an individual military or civilian unit, then click the Possess Button on the bottom HUD.
- Look: Mouse Movement
- Move: WASD
- Run: Left Shift (Consumes Stamina)
- Primary Attack: Left Click (Ranged units feature crosshair spread and projectile leading)
- Raise Shield / Block: Right Click (Blocks incoming front-facing melee attacks)
- Roll / Dodge: Spacebar (Consumes Stamina, grants brief invulnerability frames)
- Exit Possession: Escape or E

---

## Known Implementation Notes

1. Networking is command-replicated, not full-state synchronized — no rollback, checksum, or state correction path exists.
2. Multiplayer manual resource drop-off reuses `PKT_MOVE` rather than a dedicated packet type.
3. The AI only controls player 1 in single-player.
4. Campaign behaviors are fully scripted in C, not data-driven from external files.
5. Hero possession is explicitly disabled in multiplayer.
6. Sandbox forces full visibility and population cap 200.
7. OSM generation depends on curl-enabled builds and external services (Nominatim, Overpass API).
8. `ESC` is not bound to raylib's default quit behavior; the game handles it manually.

---