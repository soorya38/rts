# Sangora RTS

Sangora is a high-performance, single-threaded real-time strategy (RTS) game engine written in C11. Built on top of Raylib for cross-platform rendering and ENet for multiplayer networking, Sangora features a stateful simulation model, a deterministic lockstep command replication layer, procedural map generation, a 6-mission scripted campaign, and a unique first-person Hero Possession mode.

The engine has been architected and optimized to meet a target of 100 FPS on legacy mobile hardware through custom spatial partitioning, system throttling, and static memory allocation strategy.

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

## Optimization Blueprint

To achieve 100 FPS performance on legacy devices, the engine enforces these paradigms:
1. Zero Runtime Allocation: Memory is statically allocated in pool arrays at start. No malloc/free inside update loops prevents heap fragmentation.
2. Spatial Separation Grid: Limits collision scans to adjacent grids only, turning a bottleneck O(N^2) process into an O(N) calculation.
3. System Throttling: High-cost calculations (e.g. Fog-of-War recalculation, AI path-refresh polling) are throttled to run at 5Hz to 10Hz rather than every frame.
4. View Frustum Culling: Bypasses sprite and geometry render submissions for any objects outside the screen viewport.
