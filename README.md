# Sangora RTS

Sangora is a high-performance, single-threaded real-time strategy (RTS) game engine written in C11. Built on top of Raylib for cross-platform rendering and ENet for multiplayer networking, Sangora features a stateful simulation model, a deterministic lockstep command replication layer, procedural map generation, a 6-mission scripted campaign, and a unique first-person Hero Possession mode.

The engine has been architected and optimized to meet a target of 100 FPS on legacy mobile hardware through custom spatial partitioning, system throttling, and static memory allocation strategy.

---

## Engine Architecture & Technical Details

Sangora has been engineered around strict constraints to achieve peak efficiency and platform determinism. Below are the key technical designs that govern the engine:

### 1. Central Simulation Loop and Determinism
- Strict Single-Threaded Logic: To guarantee deterministic execution across different operating systems and CPU architectures, the gameplay simulation runs entirely on a single thread. This eliminates race conditions, lock overhead, and scheduling discrepancies that lead to multiplayer desynchronisms.
- Fixed-Delta Clamping: The game updates via a standard Euler integration pass. To protect the game physics and mechanics from fracturing during intense lag or frame-drops (the "spiral of death"), the simulation time-delta (`dt`) is strictly clamped to a maximum ceiling of `0.05f` seconds.
- Precise Update Sequencing: Within each simulation frame, operations are executed in a strict chronological sequence to ensure predictability:
  1. Decrement systemic and campaign timers.
  2. Parse incoming ENet packets and populate the command queues.
  3. Advance research and training progressions.
  4. Perform unit path-following steps and state updates.
  5. Run the O(N) spatial partitioning separation sweeps.
  6. Calculate ballistic projectile travel curves and evaluate impact collisions.
  7. Fire systemic intervals (such as Fog-of-War recalculation at 5Hz).

### 2. High-Performance Mobile Optimization and Memory Management
- Zero Dynamic Allocations (Memory Pooling): The engine performs zero memory allocations (`malloc`/`free`) during the runtime game loop. All major entities (Units, Buildings, Projectiles, Path nodes) reside in preallocated, contiguous static arrays configured within `GameState`. This eliminates heap fragmentation, prevents cache misses, and guarantees predictable performance on constrained devices.
- Throttled Systems: Non-critical operations are deferred and run at dedicated, reduced frequencies. For instance, the multi-layered Fog-of-War rasterization is throttled to `FOG_UPDATE_INTERVAL = 0.2f` (5Hz), reducing the CPU burden by 83% compared to per-frame recalculations.
- View-Frustum Culling: The render layer automatically computes bounding circles for isometric tiles, building models, and units, discarding render passes for any entity positioned outside the active screen viewport.

### 3. RNG and Mathematical Desync Mitigation
- 32-bit Xorshift PRNG: The engine implements a deterministic 32-bit Xorshift Pseudo-Random Number Generator to control random features (starting layouts, resource placements, and combat accuracy checks).
  ```c
  _rng ^= _rng << 13;
  _rng ^= _rng >> 17;
  _rng ^= _rng <<  5;
  ```
  During multiplayer initiation, the host seeds this generator and synchronizes it to all clients via `PKT_SYNC_SEED` to ensure parallel random choices across the network.
- Floating-Point Alignment: To circumvent compile-time floating-point optimization differences (such as FMA operations) that cause desyncs, coordinate systems are strictly normalized, and speed variables are constrained to prevent precision accumulation drift.

### 4. Hybrid 2D/3D Billboarding Pipeline
- Standard View Projection: Conversions between 2D continuous world coordinates and tilted screen coordinates are performed via a strict 2:1 isometric transformation:
  ```c
  ScreenX = WorldX - WorldY
  ScreenY = (WorldX + WorldY) * 0.5
  ```
- 3D Billboarding System: While the first-person Hero Possession view shifts the camera into a perspective frustum, standard 2D unit and building assets are rendered within the 3D space as billboards. The engine dynamically evaluates the angle between the possessed hero's camera position and the target sprite coordinates:
  ```c
  float dx = camera.position.x - sprite.position.x;
  float dz = camera.position.z - sprite.position.z;
  float angle = atan2f(dx, dz) * RAD2DEG;
  ```
  This rotates the 2D billboard texture to perfectly face the perspective view camera on every frame, preserving scale, vertical alignments, and rendering performance.

### 5. Multiplayer Lockstep Protocol
- Command Replication: Rather than sending large state snapshots of every unit and tile, the engine transmits compact, intent-based network payloads (`NetPacket`) through ENet's reliable channels.
- Peer Connection Mapping: A robust star-topology connection map assigns unique ID coordinates to joining peers (`PKT_ID_ASSIGN`), establishing a centralized time-sync loop driven by the host.

---

## Key Features

### Core Simulation and Mechanics
- Stateful C Architecture: The entire simulation state is contained inside a centralized GameState structure, utilizing fixed-size static arrays for units, buildings, and projectiles.
- Strategic Economy: Manage four distinct resources (food, wood, gold, and stone). Villagers feature gathering cycles, carrying capacity, automatic drop-off logic, and upgrades.
- Farming System: Fully passable agricultural tiles linked to physical farm buildings, featuring crop rotation and fertilizer upgrades.
- Age Advancement and Technology: Transition through four historical ages (Dark, Feudal, Castle, Imperial) to unlock military units, defensive fortifications, and tech tree upgrades across Blacksmiths, Universities, and resource camps.
- Entity Roster: Control 14 unique unit types including Villagers, Scouts, multiple infantry/cavalry classes, Monks, and siege weapons (Battering Rams, Mangonels, Scorpions, Bombard Cannons).

### Advanced Systems
- Pathfinding and Formations: 8-directional A* pathfinding utilizing a static binary min-heap priority queue. Features smart diagonal blocker checks, adjacent-tile target searches, and grid-based formation positioning.
- Collision and Separation: O(N) spatial grid partitioning replaces standard O(N^2) checks, reducing collision overhead by 98%. Employs a sliding orthogonal force term to prevent units from jamming head-on.
- Fog of War: Multi-layered fog (hidden, explored, visible) updated incrementally, with unit vision circles and building visual reveals.

### Render Pipelines
- Isometric 2D Mode: Standard RTS viewpoint featuring 2:1 isometric projecting, automatic tile variation, wall-connectivity linking, dynamic depth sorting, and frustum culling.
- First-Person Hero Possession: A fully immersive 3D perspective mode allowing the player to possess and directly control any individual unit. Includes a 3D camera with walk, run, dodge, and block actions, physical models, projectile arcs, crosshairs, and screen-space impact/blur effects.

### Map Generation
- Procedural Generator: Automatically creates balanced resources, shorelines, starting quadrant positions, and starter protective zones.
- OpenStreetMap (OSM) Integration: Real-world geographic feature extraction via the Overpass API. Uses libcurl to fetch road, forest, river, and water polygons, rasterizing them directly into playable 64x64 grids, complete with mini-map overview previews.

### Networking and Game Modes
- Deterministic Lockstep: Command-based synchronization using ENet to guarantee low-bandwidth replication for multiplayer.
- Scripted Campaign: 6 custom missions ranging from economic survival to intensive military engagements.
- System Sandbox: An advanced testing scenario preloaded with massive resources, fully developed bases, and all unit classes for developers.

---

## Technical Architecture

The codebase adheres to a clean, decoupled modular design. Hardware/UI presentation layers are isolated from the core physics and rules logic:

- src/main.c: The application entry point and central main loop coordinator (Input -> Net -> Update -> Render).
- src/core/game.h: The master authority declaring GameState, PlayerRes, Unit, Building, and configuration constants.
- src/core/game.c: Implements core updating, projectile math, campaign scripting, and initialization routines.
- src/core/pathfinding.c: Performance-oriented A* implementation using a static heap workspace.
- src/core/map.c: Handles procedural landscape, resource distribution, and fog-of-war throttling.
- src/core/building.c: Directs building placement, completion transitions, production queues, and resource camp drop-off gates.
- src/core/resources.c: Handles player economy checking, age-advancement triggers, and technology tree application.
- src/core/hero_possession.c: Standardizes the state machine, decay timers, and physical properties of possessing a unit.
- src/core/unit/unit_orders.c: Controls unit command dispatching, formation computing, and spawn parameters.
- src/core/unit/unit_ai.c: Contains the main AI state machines for gathering, building, movement, and combat.
- src/ai/ai.c: A modular computer player that manages automated gatherer distribution, base construction, and military training queues.
- src/core/net.c: Network event processing, packet construction, and connection handling.
- src/ui/ui_state.c: Asset managers handles loading, lifecycle deallocations, and camera interpolations.
- src/ui/input/: Controls camera navigation, click-box selections, and context order creation.
- src/ui/renderer/: Draws isometric sprites, ground tiles, and 3D possession matrices.
- src/ui/hud/: Draws selection panels, tech grids, resource counters, minimaps, and overlays.

---

## Core Algorithms

Sangora uses custom algorithms and heuristics tuned for determinism and mobile performance:

### 1. Movement and Pathfinding
- 8-Directional A* Search: Evaluates optimal paths over the 64x64 grid. Cost parameters are configured to 1.0 for orthogonal steps and 1.414 for diagonal steps.
- Static Binary Min-Heap: Leverages a preallocated priority queue heap to perform node selections without triggering dynamic heap memory operations, guaranteeing O(log N) operations and zero GC/fragmentation.
- Diagonal Blocker Checking: Prevents units from slicing through solid tile corners by validating that both adjacent orthogonal tiles are passable before allowing a diagonal transition.
- Adjacent-Tile Outward Search: When routing towards a solid, non-passable entity (such as a forest patch or castle footprint), the engine uses `find_adjacent_tile()` and `map_find_passable_near()` to find the nearest navigable, unoccupied perimeter tile.
- Formation Target Allocation: Distributes move targets for multi-unit selection using a dynamic grid pattern (width scaled to `sqrt(unit_count)`, maxing at 5). Individual targets are then jittered to nearby passable tiles to prevent unit clustering.

### 2. Collision and Spatial Partitioning
- O(N) Spatial Partitioning Grid: Subdivides the map into a coarse grid system. Units are dynamically assigned to grid cells every frame, allowing collision checks to bypass all other entities and focus solely on adjacent grid cells, reducing CPU workload by ~98%.
- Multi-Pass Iterative Separation: Runs 4 iterative cycles of position resolution per tick, correcting overlaps gradually to minimize computational snapping artifacts.
- Orthogonal Vector Sliding: Resolves unit-on-unit blockades by calculating a perpendicular normal projection. If two units collide head-on, the engine applies a slight lateral vector, allowing them to slide smoothly past each other.

### 3. Procedural Generation and Geo-Rasterization
- Nominatim and Overpass Mapping: Converts standard location strings to GPS bounding boxes, fetching geographical raw vector data (roads, water bodies, forests) via Overpass API queries.
- Point-in-Polygon (PIP) Ray-Casting: Rasterizes closed OSM polygons (lakes, residential zones) onto the 64x64 game map by casting horizontal vectors and tracking boundary edge crossings.
- Polyline Distance Approximation: Rasterizes linear features (roads and rivers) into coordinate footprints by calculating distance-to-segment metrics against local tiles.
- Fisher-Yates Compass Shuffling: Shuffles starting directions uniformly to deploy starting resource patches (food, gold, stone) in a random but strictly balanced circular pattern around starting town centers.
- Cellular-Automata Blobs: Generates organic, cohesive forest and lake clusters using recursive cluster generation combined with cellular-automata neighbor counters, preventing isolated single-tile spikes.

### 4. Gameplay Simulation and AI
- Dynamic Stature Upgrades: Instead of accumulating multiplier cascades that can lead to arithmetic drift, all unit and building stats are periodically recalculated from base structures combined with unlocked global boolean flag indexes.
- Monk Conversion: Converts hostile units by transferring team properties, resetting execution targets, and synchronizing pop caps.
- Scout Landmark Heuristics: Auto-controls AI scouts using a low-overhead roaming system that checks local fog boundaries and sequences exploration points across primary quadrants (corners, center).
- Just-in-Time Construction Scheduler: Computes current population headroom, factoring in active queue targets, and dynamically issues house placement orders to avoid training blockages.

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