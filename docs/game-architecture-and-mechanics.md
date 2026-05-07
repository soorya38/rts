# RTS Game Architecture and Mechanics Reference


 RTS Game Architecture and Mechanics Reference

This document describes the game exactly as it is implemented in this repository. It is intended to be a full technical and gameplay reference for anyone who needs to understand:

- how the game is built
- how the gameplay loop works
- which mechanics exist and how they are implemented
- which algorithms and heuristics are used
- how the networking model works
- how the AI works
- how rendering, input, and platform builds are organized

## Beginner-friendly reading guide

If you are new to game development, C programming, or RTS games, read this document in two passes.

First pass:

- Read the plain-English notes in each section.
- Focus on the "what" and "why".
- Do not worry if function names, file names, or algorithms feel unfamiliar.

Second pass:

- Re-read the technical sections.
- Open the linked source files only when you want to see the exact implementation.
- Treat each source file as one department of the game, such as "movement", "buildings", "AI", or "rendering".

In simple terms, this project is a strategy game where the code keeps one big record of the whole match, updates that record every frame, and then draws the current result on screen.

The most useful mental model is:

1. The player gives commands.
2. The game stores those commands as changes to `GameState`.
3. The simulation updates units, buildings, projectiles, resources, AI, and fog.
4. The renderer draws the newest state.
5. The same cycle repeats many times per second.

### Very short glossary

- RTS: Real-time strategy game. Players control units and buildings while time keeps moving.
- Frame: One pass through input, update, and drawing. Games usually run many frames per second.
- Simulation: The rules that decide what happens next, such as movement, gathering, attacking, and training.
- State: The current data of the game, such as where units are and how many resources each player has.
- Tile: One square of the map grid.
- World space: Pixel-based coordinates used for smooth unit movement.
- Isometric: A tilted 2D view that makes the map look partly 3D.
- Entity: A game object such as a unit, building, or projectile.
- AI: Computer-controlled player logic.
- Pathfinding: The process of finding a walkable route from one place to another.
- Packet: A small message sent over the network.
- Lockstep-style: A multiplayer style where players send commands and each machine applies those commands to its own copy of the game.
- Heuristic: A practical rule of thumb that works well enough, even if it is not a perfect solution.

The project is a single-process, single-threaded C RTS built on raylib, with ENet-based multiplayer, a deterministic-leaning lockstep-style command replication layer, an optional OpenStreetMap-derived map generator, a scripted campaign mode, a sandbox mode, and a temporary first-person "Hero Possession" mode.

This document follows the implementation in:

- [src/main.c](/Users/sooryaakilesh/test/rts/src/main.c)
- [src/core/game.h](/Users/sooryaakilesh/test/rts/src/core/game.h)
- [src/core/game.c](/Users/sooryaakilesh/test/rts/src/core/game.c)
- [src/core/map.c](/Users/sooryaakilesh/test/rts/src/core/map.c)
- [src/core/pathfinding.c](/Users/sooryaakilesh/test/rts/src/core/pathfinding.c)
- [src/core/resources.c](/Users/sooryaakilesh/test/rts/src/core/resources.c)
- [src/core/building.c](/Users/sooryaakilesh/test/rts/src/core/building.c)
- [src/core/unit/unit_orders.c](/Users/sooryaakilesh/test/rts/src/core/unit/unit_orders.c)
- [src/core/unit/unit_ai.c](/Users/sooryaakilesh/test/rts/src/core/unit/unit_ai.c)
- [src/ai/ai.c](/Users/sooryaakilesh/test/rts/src/ai/ai.c)
- [src/core/net.h](/Users/sooryaakilesh/test/rts/src/core/net.h)
- [src/core/net.c](/Users/sooryaakilesh/test/rts/src/core/net.c)
- [src/core/hero_possession.c](/Users/sooryaakilesh/test/rts/src/core/hero_possession.c)
- [src/core/osm_mapgen.c](/Users/sooryaakilesh/test/rts/src/core/osm_mapgen.c)
- the UI and renderer files under `src/ui/`
- the build files in `Makefile`, `.github/workflows/ci.yml`, and `android/app/src/main/cpp/CMakeLists.txt`

## 1. High-level architecture

The game is organized into a few clear layers:

1. Entry/runtime layer
   - `main.c`
   - owns the window, audio device, main loop, and top-level order of operations

2. Core simulation layer
   - `game.c`, `game.h`
   - defines `GameState`, entities, match modes, phase progression, campaign flow, projectile simulation

3. World/rules layer
   - `map.c`
   - `resources.c`
   - `building.c`
   - `unit_orders.c`
   - `unit_ai.c`
   - `hero_possession.c`
   - these files define the actual mechanics

4. AI layer
   - `ai.c`
   - controls the non-player RTS opponent in single-player

5. Networking layer
   - `net.h`, `net.c`
   - wraps ENet and replicates commands as packets

6. Content-derived map generation layer
   - `osm_mapgen.c`, `osm_mapgen.h`
   - optionally builds a playable RTS map from OpenStreetMap data

7. UI/input/render layer
   - `ui_state.*`
   - `ui/input/*`
   - `ui/hud/*`
   - `ui/renderer/*`
   - handles selection, commands, HUD, 2D isometric rendering, and the 3D hero camera

The project is not using an ECS, a job system, or a separate engine framework. It is a direct stateful C codebase built around arrays of structs inside one `GameState`.

## 2. Main loop and runtime model

The top-level runtime is in [src/main.c](/Users/sooryaakilesh/test/rts/src/main.c).

The loop order each frame is:

1. Poll input through `input_update(gs, ui)`
2. Poll and apply network events through `net_update(gs)`
3. Advance simulation through `game_update(gs, dt)`
4. Render:
   - draw world in 2D isometric mode unless hero possession is exclusively rendering the 3D scene
   - draw first-person hero scene when hero possession is active
   - draw HUD on top

Important runtime details:

- Desktop initializes a resizable raylib window with MSAA.
- Android uses `InitWindow(0, 0, "RTS Game")` so raylib adopts the device resolution.
- Audio is initialized once at startup.
- `ESC` is not bound to raylib's default quit behavior; the game handles it manually.
- `dt` is capped at `0.05f` to avoid a spiral-of-death on slow frames.
- The game is strictly single-threaded from gameplay's point of view.

## 3. Core state model

The core state is defined in [src/core/game.h](/Users/sooryaakilesh/test/rts/src/core/game.h).

### 3.1 Global constants

- Map size: `64 x 64`
- Tile size: `32` pixels
- Max units: `256`
- Max buildings: `128`
- Max projectiles: `128`
- Players supported: up to `4`
- Population cap hard ceiling: `200`
- A* path storage per unit: `300` nodes

### 3.2 Primary modes

`GameMode`

- `GAME_MODE_STANDARD`
- `GAME_MODE_SANDBOX`
- `GAME_MODE_CAMPAIGN`

`GamePhase`

- `PHASE_MENU`
- `PHASE_PLAYING`
- `PHASE_PAUSED`
- `PHASE_VICTORY`
- `PHASE_DEFEAT`

### 3.3 Master game state

`GameState` owns:

- current mode and phase
- accumulated `game_time`
- full tile grid
- full unit array
- full building array
- projectile array
- resource state for each player
- build ghost state
- hero possession state
- AI phase/timers
- campaign metadata and timers
- OSM metadata and downloaded tile-grid coordinates

The codebase uses fixed-size arrays rather than dynamically growing containers. "Active" flags determine whether a unit/building slot is in use.

## 4. Coordinate systems and spatial representation

The game uses several coordinate spaces at once.

### 4.1 Tile space

- integer coordinates `(tx, ty)`
- used for pathfinding, building footprint placement, fog, and resources

### 4.2 World space

- float coordinates `(wx, wy)` in pixels
- units move continuously in world space
- converting tile center to world:
  - `wx = (tx + 0.5f) * TILE_SIZE`
  - `wy = (ty + 0.5f) * TILE_SIZE`

### 4.3 Isometric screen space

Defined by:

- `world_to_iso(wx, wy) -> (wx - wy, (wx + wy) * 0.5f)`
- `iso_to_world(ix, iy) -> (iy + ix * 0.5f, iy - ix * 0.5f)`

This is a 2:1 isometric projection.

### 4.4 3D hero view space

Hero possession uses a separate 3D raylib camera.

- `world3(wx, wy, h)` maps 2D world coordinates into a simple 3D grid space
- x and z are based on tile/world position
- y is height above the ground plane

## 5. Match initialization

### 5.1 Standard match

`game_init_started_game()`:

- zeroes and initializes the match through `game_init_match()`
- seeds the RNG
- gives each player starting resources:
  - 200 food
  - 200 wood
  - 100 gold
  - 0 stone
- starts all players in Dark Age
- calls `game_setup_random_starts()`

Each player starts with:

- 1 prebuilt town center
- 3 villagers
- 1 scout

### 5.2 Campaign

`game_init_campaign()` sets up mission-specific state.

Campaign properties:

- 6 missions total
- missions 0-2 are solo-economy focused
- missions 3-5 introduce or require an enemy player
- each mission overrides resources, age, initial buildings, initial units, timers, and mission scripting conditions

### 5.3 Sandbox

`game_init_sandbox()` creates a handcrafted test map:

- 2 players
- Imperial Age start
- full population cap
- massive resources
- prebuilt "ancient city" layouts for both sides
- preview blocks of many unit types

This mode is explicitly for systems testing: economy, combat, tech, siege, healing, and building behavior.

### 5.4 OSM map mode

`game_init_osm()`:

- puts the game in sandbox-like conditions
- gives both players large resources and full cap
- calls `osm_generate_map()`
- falls back to random map generation if OSM fetch fails
- places town centers and starting units on generated start positions

## 6. Map generation architecture

The game has two map generation pipelines:

1. standard random map generation in `map.c`
2. real-world feature-driven generation in `osm_mapgen.c`

### 6.1 Standard random map generation

`map_init()` performs:

1. fill the map with grass
2. choose one start quadrant per player
3. place random lakes
4. place random forest clusters
5. place random gold clusters
6. place random stone clusters
7. place random berry clusters
8. clear starter safe zones
9. place guaranteed starting resources around each TC

#### Start placement strategy

- map is split into quadrants
- random starts are sampled inside quadrants, not hardcoded corners
- 2-player matches choose opposite quadrants
- 3- and 4-player matches shuffle quadrants

#### Resource placement strategy

Guaranteed starting resources are placed by `place_start_resources()`:

- 3 forest patches
- 2 berry patches
- 1 gold patch
- 1 stone patch

Directions are shuffled using Fisher-Yates over 8 compass directions. Distances are randomized and jittered.

#### Terrain blockers

The following tile types are impassable:

- water
- forest
- gold
- stone
- berries

`TILE_DESERT` and `TILE_ROAD` are passable.

#### Buildability

Buildings cannot be placed on:

- water
- forest
- gold
- stone
- berries
- any occupied building footprint

Desert and road are buildable.

### 6.2 OSM-derived generation

The OSM generator is wrapped in [src/core/osm_mapgen.h](/Users/sooryaakilesh/test/rts/src/core/osm_mapgen.h).

Pipeline:

1. `geocode(location_name)` using Nominatim
2. fetch geographic features from Overpass API
3. rasterize features into the 64x64 RTS tile grid
4. place gameplay starts and neutral buildings
5. download OSM raster tiles for a side-by-side map overview in the HUD

The code stores:

- bounding box coordinates
- chosen zoom level
- tile-grid origin and size
- downloaded tile images `osm_tile_<col>_<row>.png`

If the curl-backed build flag `RTS_HAS_CURL` is missing, OSM generation is disabled and returns false.

### 6.3 Fog of war

Fog is updated every simulation frame by `map_update_fog()`.

Rules:

- sandbox forces all tiles visible for all players
- otherwise, existing `VISIBLE` becomes `EXPLORED`
- units reveal a circular radius based on `vision_range`
- complete buildings reveal a fixed radius of 4 tiles centered on the footprint

Fog states:

- `FOG_HIDDEN`
- `FOG_EXPLORED`
- `FOG_VISIBLE`

## 7. Pathfinding and movement algorithms

Pathfinding is implemented in [src/core/pathfinding.c](/Users/sooryaakilesh/test/rts/src/core/pathfinding.c).

### 7.1 Algorithm

- A* over the `64 x 64` tile grid
- 8-directional movement
- diagonal movement cost: `1.414`
- orthogonal movement cost: `1`
- heuristic: octile distance

### 7.2 Constraints

- diagonal movement is blocked unless both orthogonal neighbor tiles are passable
- the destination tile itself may be non-passable in some cases, because attack/gather logic often wants pathfinding toward a blocked resource/building boundary and then uses adjacent-tile logic externally

### 7.3 Data structures

- `PriorityQueue` is a binary min-heap defined inline in `game.h`
- workspace arrays `g`, `from`, and `closed` are static
- queue is static to avoid stack pressure, especially on Android

### 7.4 Reconstruction

The resulting path is reconstructed backwards from `from[]` and copied into the unit's per-unit path array.

### 7.5 Adjacent-tile search

For buildings and resource gathering, the path target is often not the target tile itself but a free passable tile adjacent to it. This is handled with:

- `find_adjacent_tile()`
- `map_find_passable_near()`
- `unit_find_free_tile_near()`

These helpers are central to making building, resource, and combat interactions work on a blocked tile map.

## 8. Entity model

### 8.1 Units

Each `Unit` stores:

- identity: `active`, `id`, `player`, `type`, `state`
- world position
- path buffer and index
- combat stats
- villager hauling state
- combat targets
- selection state
- animation / facing / death timer

### 8.2 Buildings

Each `Building` stores:

- identity and ownership
- tile footprint and dimensions
- HP and max HP
- construction progress
- complete/incomplete state
- unit production queue
- rally point
- farm resource amount
- active tech research
- defensive attack stats
- visual variant

### 8.3 Projectiles

Projectile simulation is explicit, not hitscan-only.

A `Projectile` stores:

- owner player
- projectile type: `ARROW`, `BOLT`, `STONE`
- target unit or building
- damage
- start/end coordinates
- elapsed time
- duration
- arc height

### 8.4 Hero possession

Hero possession is tracked separately from the unit:

- phase
- controlled unit id
- remaining duration
- cooldown timer
- transition timer
- yaw/pitch
- stamina
- attack / dodge / block timers
- camera shake, blur, impact timers

## 9. Units: stats, categories, and mechanics

Base unit stats are defined in [src/core/unit/unit_orders.c](/Users/sooryaakilesh/test/rts/src/core/unit/unit_orders.c).

### 9.1 Unit roster

Economy / utility:

- Villager
- Scout

Infantry:

- Militia
- Man-at-Arms
- Spearman

Ranged:

- Archer
- Skirmisher
- Cavalry Archer

Cavalry:

- Knight

Support:

- Monk

Siege:

- Battering Ram
- Mangonel
- Scorpion
- Bombard Cannon

### 9.2 Base stats table

| Unit | HP | Atk | Armor | Range | Vision | Speed | Atk CD | Carry |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
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

### 9.3 Unit states

- `US_IDLE`
- `US_MOVING`
- `US_GATHERING`
- `US_RETURNING`
- `US_BUILDING`
- `US_ATTACKING`
- `US_DYING`
- `US_DEAD`

### 9.4 Movement

Movement is path-driven:

- units hold a path buffer
- `unit_step_path()` advances them toward the current waypoint
- last waypoint has a tighter acceptance threshold than intermediate waypoints

Collision resolution:

- after all unit updates, `unit_apply_separation()` runs 4 passes
- pairwise overlap resolution pushes units apart
- includes a small orthogonal "spin" term so units slide off one another instead of staying stuck head-on

### 9.5 Selection and formations

When issuing a move order to multiple units, the game computes formation positions with `unit_compute_formation_targets()`:

- grid-like arrangement
- width approximates `sqrt(unit_count)`
- capped width of 5
- each target tile is adjusted to a nearby free passable tile

This means mass move commands are not all directed to a single tile.

## 10. Economy mechanics

### 10.1 Resources

Resource types:

- food
- wood
- gold
- stone

### 10.2 Affordability and deduction

Resource checks are straightforward:

- `res_can_afford()`
- `res_deduct()`
- `res_add()`

### 10.3 Villager gathering

Gather logic is in `unit_do_gather()` inside [src/core/unit/unit_ai.c](/Users/sooryaakilesh/test/rts/src/core/unit/unit_ai.c).

Base gather rates per second:

- food: `0.4`
- wood: `0.5`
- gold: `0.33`
- stone: `0.28`

Process:

1. villager must be adjacent to target
2. villager accumulates `anim_timer += rate * dt`
3. integer amount extracted whenever `anim_timer >= 1`
4. resource is subtracted from tile or farm building
5. villager carry amount increases
6. once carry cap is reached, a drop-off path is computed

### 10.4 Carry capacity

Villagers have a base carry capacity of 10, modified by upgrades.

Food-specific carry modifiers:

- Granary Baskets: `+1`
- Reaping: `+2`

Wood-specific carry modifiers:

- Log Straps: `+1`
- Hardwood Carts: `+2`

### 10.5 Drop-off rules

Nearest valid drop-off building by resource:

- food: Town Center or Mill
- wood: Town Center or Lumber Camp
- gold: Town Center or Mining Camp
- stone: Town Center or Mining Camp

### 10.6 Farms

Farms are special because:

- the building footprint becomes `TILE_FARM`
- farms remain passable
- farm food is tracked both in the building and synced into the tile resource amounts

Base farm food:

- 400

New-farm bonuses:

- Crop Rotation: `+75`
- Fertilizer: `+125`

### 10.7 Population

Population is tracked in `PlayerRes`.

Rules:

- each spawned unit increments population
- each actual death decrements population
- base cap is 5
- each completed house adds 5
- hard cap is 200
- sandbox forces cap to 200

The game also counts queued units when deciding whether to allow more training.

## 11. Building system

### 11.1 Building roster

- Town Center
- House
- Barracks
- Archery Range
- Stable
- Blacksmith
- Market
- Mill
- Lumber Camp
- Mining Camp
- Farm
- Watch Tower
- Monastery
- Siege Workshop
- University
- Wall
- Gate
- Castle

### 11.2 Costs

| Building | Food | Wood | Gold | Stone |
| --- | ---: | ---: | ---: | ---: |
| Town Center | 0 | 0 | 0 | 0 |
| House | 0 | 25 | 0 | 0 |
| Barracks | 0 | 175 | 0 | 0 |
| Archery Range | 0 | 175 | 0 | 0 |
| Stable | 0 | 175 | 0 | 0 |
| Blacksmith | 0 | 150 | 0 | 0 |
| Market | 0 | 175 | 0 | 0 |
| Mill | 0 | 100 | 0 | 0 |
| Lumber Camp | 0 | 100 | 0 | 0 |
| Mining Camp | 0 | 100 | 0 | 0 |
| Farm | 0 | 60 | 0 | 0 |
| Watch Tower | 0 | 125 | 0 | 125 |
| Monastery | 0 | 175 | 0 | 0 |
| Siege Workshop | 0 | 200 | 0 | 0 |
| University | 0 | 200 | 0 | 0 |
| Wall | 0 | 20 | 0 | 0 |
| Gate | 0 | 35 | 0 | 15 |
| Castle | 0 | 0 | 0 | 650 |

### 11.3 Footprint sizes

| Building | Size |
| --- | ---: |
| Town Center | 4x4 |
| Stable | 4x4 |
| Castle | 4x4 |
| Barracks | 3x3 |
| Archery Range | 3x3 |
| Blacksmith | 3x3 |
| Market | 3x3 |
| Farm | 3x3 |
| Monastery | 3x3 |
| Siege Workshop | 3x3 |
| University | 3x3 |
| House | 2x2 |
| Mill | 2x2 |
| Lumber Camp | 2x2 |
| Mining Camp | 2x2 |
| Watch Tower | 2x2 |
| Wall | 1x1 |
| Gate | 1x1 |

### 11.4 HP table

| Building | HP |
| --- | ---: |
| Town Center | 2400 |
| House | 550 |
| Barracks | 1200 |
| Archery Range | 1300 |
| Stable | 1400 |
| Blacksmith | 1200 |
| Market | 800 |
| Mill | 1000 |
| Lumber Camp | 600 |
| Mining Camp | 600 |
| Farm | 400 |
| Watch Tower | 1020 |
| Monastery | 900 |
| Siege Workshop | 1500 |
| University | 1200 |
| Wall | 900 |
| Gate | 1100 |
| Castle | 4800 |

### 11.5 Placement logic

`building_place()`:

- checks buildability
- checks age requirement
- checks affordability
- finds a free building slot
- deducts resources
- initializes an incomplete foundation
- marks the map footprint as occupied immediately

### 11.6 Construction and repair

Villagers build via `unit_do_build()`.

Construction:

- base progress rate: `0.035` per second
- Treadmill Crane increases this by `1.5x`

Repair:

- base HP repair rate: `34` HP/sec
- Treadmill Crane multiplies this by `1.35x`

When a building completes:

- it is marked complete
- HP is set to max
- farm conversion happens if it is a farm
- player's pop cap is recomputed

### 11.7 Production queue

- queue capacity: 5
- only complete buildings can queue units
- building cannot train units while researching
- queued population counts toward pop-cap checks
- spawned units appear near the building, then move to the rally point if needed

### 11.8 Rally points

Rally-capable buildings:

- Town Center
- Barracks
- Archery Range
- Stable
- Monastery
- Siege Workshop
- Castle

### 11.9 Selling

`building_sell()` refunds 95% of each cost component, then destroys the building.

### 11.10 Defensive buildings

Combat-capable defensive buildings:

- Town Center
- Watch Tower
- Castle

Base defensive stats:

- Town Center: 5 damage, 6 range
- Watch Tower: 6 damage, 8 range
- Castle: 8 damage, 8 range

They auto-acquire the nearest enemy unit within range.

## 12. Unit production and age gating

### 12.1 Unit production buildings

- Town Center: Villager, Scout
- Barracks: Militia, Man-at-Arms, Spearman
- Archery Range: Archer, Skirmisher, Cavalry Archer
- Stable: Knight
- Monastery: Monk
- Siege Workshop: Ram, Mangonel, Scorpion, Bombard Cannon

### 12.2 Unit costs

| Unit | Food | Wood | Gold | Stone |
| --- | ---: | ---: | ---: | ---: |
| Villager | 50 | 0 | 0 | 0 |
| Scout | 80 | 0 | 0 | 0 |
| Militia | 60 | 0 | 20 | 0 |
| Man-at-Arms | 60 | 0 | 20 | 0 |
| Spearman | 35 | 25 | 0 | 0 |
| Archer | 0 | 25 | 45 | 0 |
| Skirmisher | 25 | 35 | 0 | 0 |
| Cavalry Archer | 40 | 0 | 70 | 0 |
| Knight | 60 | 0 | 75 | 0 |
| Monk | 0 | 0 | 100 | 0 |
| Battering Ram | 0 | 160 | 75 | 0 |
| Mangonel | 0 | 160 | 135 | 0 |
| Scorpion | 0 | 75 | 75 | 0 |
| Bombard Cannon | 0 | 225 | 225 | 0 |

### 12.3 Train times

| Unit | Seconds |
| --- | ---: |
| Villager | 25 |
| Scout | 20 |
| Militia | 21 |
| Man-at-Arms | 21 |
| Spearman | 22 |
| Archer | 35 |
| Skirmisher | 22 |
| Cavalry Archer | 34 |
| Knight | 30 |
| Monk | 45 |
| Battering Ram | 36 |
| Mangonel | 46 |
| Scorpion | 34 |
| Bombard Cannon | 56 |

### 12.4 Age requirements

Units:

- Dark Age:
  - Villager
  - Scout
  - Militia
  - Archer
- Feudal:
  - Man-at-Arms
  - Spearman
  - Skirmisher
- Castle:
  - Knight
  - Cavalry Archer
  - Monk
  - Ram
  - Mangonel
  - Scorpion
- Imperial:
  - Bombard Cannon

Buildings:

- Feudal:
  - Archery Range
  - Stable
  - Blacksmith
  - Market
  - Watch Tower
- Castle:
  - Monastery
  - Siege Workshop
  - University
  - Castle

## 13. Combat model

### 13.1 Basic attack flow

Combat is handled in `unit_do_attack()`.

Process:

1. validate target
2. if target lost and unit is not manually locked, auto-acquire a new target
3. if target out of range:
   - periodically repath
   - spread around units/buildings using slot heuristics
   - allow a small "extended melee" behavior if pathing is impossible but distance is close
4. if in range:
   - stop pathing
   - attack if cooldown elapsed

### 13.2 Auto-acquisition

If `stance_manual` is false:

- non-villager, non-scout military units auto-search for enemies while idle
- attack state also reacquires if original target disappears

### 13.3 Armor and minimum damage

Unit-vs-unit and building-vs-unit damage:

- `damage = attack + bonuses - armor`
- minimum final damage is 1

Unit-vs-building damage:

- `damage = attack + anti-building bonus`

### 13.4 Bonus damage

Implemented matchup bonuses include:

- Spearman vs cavalry/cavalry archer: `+8`
- Skirmisher vs archer/cavalry archer: `+7`
- Scout vs monk: `+10`
- Archer vs spearman: `+2`
- Knight vs archer/skirmisher/monk: `+2`
- Cavalry Archer vs spearman: `+3`
- Scorpion vs ranged units: `+4`

Anti-building:

- Militia / Man-at-Arms: `+2`
- Knight: `+1`
- Battering Ram: `+28`
- Mangonel: `+8`
- Bombard Cannon: `+18`

### 13.5 Projectiles

Projectile-using units:

- Archer
- Skirmisher
- Cavalry Archer
- Mangonel
- Scorpion
- Bombard Cannon

Projectile-using buildings:

- Town Center
- Watch Tower
- Castle

Projectile travel time is distance-based.

Projectile types:

- `PROJ_ARROW`
- `PROJ_BOLT`
- `PROJ_STONE`

Stone projectiles can deal splash damage in a radius on impact.

### 13.6 Monks

Monk behavior is asymmetric:

- when idle, monks automatically heal nearby damaged allied units
- when given an attack command against an enemy unit, they convert it instead of damaging it
- conversion:
  - moves the unit to the monk's player
  - decrements old player's population
  - increments new player's population
  - resets the converted unit to idle
- monks do not attack buildings

### 13.7 Death

When a unit reaches `hp <= 0`:

- state becomes `US_DYING`
- `death_timer = 0.8`
- when timer expires:
  - state becomes `US_DEAD`
  - `active` becomes false
  - population is decremented

### 13.8 Town center defeat logic

If a Town Center is destroyed outside sandbox:

- local player losing their TC causes defeat
- if all enemy TCs are gone, local player wins
- campaign missions 3 and 4 override this with mission-specific messaging instead of immediate final victory

## 14. Technology system

Research is handled per building with:

- `active_tech`
- `tech_timer`
- `building_start_tech()`

Completion logic:

1. mark tech unlocked for the owning player
2. clear the building's active tech
3. refresh every owned unit's stats
4. refresh every owned building's stats

### 14.1 Research buildings by category

Town Center:

- Loom
- Wheelbarrow
- Hand Cart

Mill:

- food economy techs

Lumber Camp:

- wood economy techs

Barracks:

- infantry techs

Archery Range:

- archery techs

Stable:

- cavalry techs

Monastery:

- monk techs

Siege Workshop:

- siege techs

Blacksmith:

- armor / melee / arrow techs

University:

- structural and tower techs

### 14.2 Upgrade application model

Upgrades are not applied incrementally as ad hoc modifiers every frame. Instead:

- unit and building stats are reset to base
- then category-specific upgrade bonuses are re-applied from unlocked tech flags

This makes the system robust and deterministic within a single simulation.

### 14.3 Important upgrade effects

Economy:

- food and wood gather multipliers
- villager carry-cap changes
- villager HP/armor/speed changes
- farm capacity bonuses

Military:

- infantry HP/attack/armor/speed
- archery attack/range/vision/attack cooldown
- cavalry HP/armor/speed/attack
- monk HP/range/speed/support cadence
- siege HP/attack/range/speed

Structures:

- Masonry / Architecture percentage HP boosts
- Fortified Wall adds 600 HP to walls and gates
- Hoardings adds 500 HP to town centers and castles
- Guard Tower / Keep / Missile Guidance / Murder Holes / Chemistry modify tower-class attack and range
- Heated Shot adds anti-siege bonus to defensive structures

Special unlock:

- Bombard Cannon training is gated by `TECH_CANNON_EMPLACEMENTS`

## 15. Age progression

Age progression is managed in [src/core/resources.c](/Users/sooryaakilesh/test/rts/src/core/resources.c).

Age costs:

- Dark -> Feudal: 400 food
- Feudal -> Castle: 500 food, 100 wood
- Castle -> Imperial: 600 food, 400 gold

Age-up durations:

- Dark -> Feudal: 20 sec
- Feudal -> Castle: 16 sec
- Castle -> Imperial: 12 sec

The formula used is:

- `20.0f - pr->age * 4.0f`

Rules:

- cannot advance past Imperial
- cannot advance while already advancing
- cost is paid immediately
- completion increments `age` and clears `advancing`

## 16. AI architecture

AI is in [src/ai/ai.c](/Users/sooryaakilesh/test/rts/src/ai/ai.c).

The AI controls player index 1 and only runs:

- in single-player
- not in sandbox
- not in campaign missions 0-4

Campaign mission 5 is the first campaign mission where the general RTS AI loop is allowed to run.

### 16.1 AI runtime cadence

- think interval: every 0.5 seconds
- major attack cooldown: 60 seconds

### 16.2 AI phase labels

Internal coarse phases:

- gather
- build
- military
- attack

These are descriptive state markers, not a full state-machine that blocks actions.

### 16.3 Economic management

The AI:

- reassigns idle villagers
- detects stuck gatherers and resets them
- chooses desired worker counts by resource
- prioritizes food and wood heavily in Dark Age
- adds gold/stone targets later
- saves for age-up when near thresholds

Dark Age villager target:

- 22 villagers approximately

Feudal economy target:

- 24 villagers approximately

### 16.4 Build order heuristics

Dark Age priorities:

1. Lumber Camp
2. Mill
3. Mining Camp once the eco is stable
4. Barracks once villagers and wood threshold are met

Feudal/Castle/Imperial expand into:

- Archery Range
- Stable
- Blacksmith
- Market
- Monastery
- Siege Workshop
- University
- Watch Tower

Housing is built just-in-time with a free-pop threshold and up to 2 houses pending.

### 16.5 Scout exploration

The AI scout:

- first prefers hidden tiles near its town center
- gives extra priority to hidden resource tiles
- then roams among a landmark set:
  - corners
  - edge midpoints
  - map center

This is a lightweight exploration heuristic, not a frontier-search algorithm.

### 16.6 Research logic

The AI research order is gated by:

- age
- resources
- current army composition
- number of villagers
- building existence

It tries one research at a time on each think pass and stops after the first successful one.

### 16.7 Training logic

The AI chooses composition based on:

- villager targets by age
- whether it is saving for age-up
- current counts of infantry / archers / cavalry / monks / siege
- building availability

### 16.8 Attack behavior

The AI:

- defines an age-based minimum military size before attacking
  - Dark: 4
  - Feudal: 6
  - Castle: 8
  - Imperial: 10
- groups idle military near a rally point before threshold
- once threshold is met:
  - redirects idle military to attack
  - periodically launches larger waves every 60 seconds

Targeting order:

1. enemy Town Center if found
2. nearest enemy complete building
3. nearest enemy units when close enough

## 17. Hero Possession mode

Hero possession is a temporary first-person control mode for one eligible unit in solo play.

### 17.1 Eligible units

Allowed:

- Scout
- Militia
- Man-at-Arms
- Spearman
- Archer
- Skirmisher
- Cavalry Archer
- Knight

Disallowed:

- Villager
- Monk
- Siege units

### 17.2 Activation rules

Can only start if:

- phase is `PLAYING`
- hero mode is currently off
- cooldown has expired
- selected unit belongs to the player
- selected unit type is eligible
- not in multiplayer

### 17.3 Lifecycle

Phases:

- off
- entering
- active
- exiting

Durations:

- active duration: 22 sec
- cooldown: 45 sec
- transition: 0.65 sec

### 17.4 Time scaling

During entering and exiting:

- simulation time is scaled to `0.42x`

During active possession:

- simulation is normal speed

### 17.5 Controls

While active:

- mouse look adjusts yaw/pitch
- `WASD` or arrows move
- left mouse or `F` attacks
- `E` interacts
- right mouse or `Q` blocks
- `Space` or `Shift` dodges
- `ESC` exits

### 17.6 Mechanics

The possessed unit:

- has its RTS pathing and targets cleared
- is controlled directly in world space
- can block and dodge
- uses amplified damage for first-person attacks
- can break resources or damage buildings with interact

Incoming damage while possessed is modified:

- dodge reduces damage heavily
- block reduces damage moderately

The mode is ended if:

- timer expires
- unit dies
- player cancels

## 18. Networking architecture

Networking is in [src/core/net.c](/Users/sooryaakilesh/test/rts/src/core/net.c) and uses the bundled single-header ENet implementation in [src/core/enet/enet.h](/Users/sooryaakilesh/test/rts/src/core/enet/enet.h).

### 18.1 Model

The networking model is command replication:

- local player actions are encoded into `NetPacket`
- sent reliably through ENet
- applied locally as well through `net_dispatch_packet()`
- remote peers receive the same packet and call `apply_packet()`

This is closer to a lockstep / replicated-command model than to snapshot replication.

It does not transmit the full world state every frame.

### 18.2 Topology

- Host is always player 0
- Clients connect to host
- Host can accept up to 3 clients total
- Host uses one ENet host object with up to 3 peers
- Client uses one ENet host object and one peer pointing to the server

### 18.3 Connection flow

Host:

- `net_host_create(port)`
- creates server ENet host
- sets local player id to 0

Client:

- `net_join(ip, port)`
- creates client ENet host
- connects to server
- local player id remains `-1` until assigned

On connect:

- host assigns a player id via `PKT_ID_ASSIGN`
- host sends current random seed via `PKT_SYNC_SEED`
- host broadcasts lobby peer count via `PKT_LOBBY_SYNC`

### 18.4 Packet structure

`NetPacket` fields:

- `type`
- `player`
- `unit_count`
- `units[64]`
- `tx`
- `ty`
- `target_id`
- `extra`

This packet is intentionally generic and overloaded by packet type.

### 18.5 Packet types

- `PKT_SYNC_SEED`
- `PKT_START_GAME`
- `PKT_MOVE`
- `PKT_GATHER`
- `PKT_ATTACK`
- `PKT_BUILD`
- `PKT_PLACE_BLD`
- `PKT_TRAIN_UNIT`
- `PKT_DELETE_BLD`
- `PKT_AGE_ADVANCE`
- `PKT_ID_ASSIGN`
- `PKT_STANCE`
- `PKT_LOBBY_SYNC`
- `PKT_RESEARCH`
- `PKT_SET_RALLY`

### 18.6 Per-packet semantics

`PKT_SYNC_SEED`

- updates `_rng`

`PKT_START_GAME`

- starts a standard game with the agreed player count

`PKT_MOVE`

- computes formation targets on the receiver
- then issues move orders per unit

`PKT_GATHER`

- issues villager gather orders to a specific resource tile

`PKT_ATTACK`

- attacks unit or building
- `extra == 0` means unit target
- `extra == 1` means building target

`PKT_BUILD`

- assigns selected villagers to an existing building foundation

`PKT_PLACE_BLD`

- creates a new building
- then assigns villagers listed in `units[]` as builders

`PKT_TRAIN_UNIT`

- queues unit production at a building

`PKT_DELETE_BLD`

- sells a building

`PKT_AGE_ADVANCE`

- triggers age advancement

`PKT_STANCE`

- changes `stance_manual`

`PKT_ID_ASSIGN`

- sets client's player id

`PKT_LOBBY_SYNC`

- communicates current peer count for clients

`PKT_RESEARCH`

- starts a technology at a building

`PKT_SET_RALLY`

- changes a building's rally point

### 18.7 Command dispatch behavior

`net_dispatch_packet(gs, pkt)` does two things:

1. send over the network if networking is active
2. immediately apply locally through `apply_packet()`

This keeps local player input responsive.

### 18.8 Reliability model

Packets are sent as reliable ENet packets.

The system assumes all peers begin from the same starting state and then apply the same commands.

### 18.9 Important caveats in the current networking implementation

The current architecture is practical and simple, but it is not a full deterministic lockstep engine with rollback or checksums.

Notable caveats:

- The game does not exchange periodic world-state hashes or resync snapshots.
- Some simulation behavior depends on local scanning order and current state, which may diverge if peers ever desync.
- The input layer notes that manual villager drop-off in multiplayer currently reuses `PKT_MOVE` rather than having a dedicated drop-off packet, so this interaction is weaker than the single-player local path.
- `peer_count` is used both as a lobby concept and a live connection count, which is adequate but minimal.

### 18.10 Networking explained from zero

This networking system is custom at the game-protocol level.

That means:

- ENet provides the low-level transport.
- The RTS game defines its own packet structure.
- The RTS game defines what each packet type means.
- The RTS game decides when packets are created.
- The RTS game decides how received packets change `GameState`.

ENet is not deciding how villagers gather, how units attack, how buildings are placed, or how the match starts. ENet only helps move bytes between machines reliably.

The custom part is the meaning of those bytes.

### 18.11 Why this architecture is used

The game uses command replication because it is simpler and lighter than sending the entire world every frame.

The game world contains many things:

- all units
- all buildings
- all projectiles
- all resource amounts
- all map tiles
- all player resources
- all unit paths
- all timers
- all combat state

Sending all of that every frame would be expensive and complicated.

Instead, the game sends player intentions.

Examples of player intentions:

- "Player 1 ordered these units to move to tile 20, 30."
- "Player 0 ordered this building to train a villager."
- "Player 2 placed a barracks at tile 12, 16."
- "Player 1 started researching this technology."

Every machine already has its own copy of the game simulation. If every machine starts from the same initial state and receives the same commands in the same practical order, then every machine should reach the same result.

That is the main idea behind this implementation.

### 18.12 What ENet does and does not do here

ENet does:

- open host/client network connections
- accept peers
- send byte packets
- receive byte packets
- provide reliable packet delivery when `ENET_PACKET_FLAG_RELIABLE` is used
- report connect, receive, and disconnect events

ENet does not:

- know what `GameState` is
- know what a unit is
- know what a building is
- know what a move order is
- know what a technology is
- automatically synchronize the world
- automatically fix desyncs

The game code gives ENet a block of bytes that happens to contain `NetPacket`. The game code later receives a block of bytes and interprets it as `NetPacket`.

### 18.13 The main networking globals

The networking layer uses three global flags/values:

- `g_net_active`
- `g_local_player_id`
- `g_net_connected`

`g_net_active` means the game is currently using networking.

`g_local_player_id` means which player this machine controls:

- host uses player `0`
- clients are assigned player `1`, `2`, or `3`
- `-1` means no player id has been assigned yet

`g_net_connected` means ENet has reported that a connection exists.

The helper `net_get_local_player()` returns player `0` if no id has been assigned yet. This gives the game a safe fallback outside multiplayer.

### 18.14 Host and client roles

There are two roles:

- host
- client

The host is the machine that creates the lobby/server.

The client is a machine that joins the host.

The host always controls player `0`.

Clients do not choose their own player id. The host assigns ids.

This matters because the packet's `player` field is used to say which player is performing an action. For example, if player `2` places a building, the packet must say player `2`, otherwise the building could be created for the wrong player.

### 18.15 Host startup flow

Host startup happens through `net_host_create(port)`.

Step by step:

1. The game creates an `ENetAddress`.
2. The address uses `ENET_HOST_ANY`, meaning it listens on available local network interfaces.
3. The address stores the chosen port.
4. The game calls `enet_host_create(&address, 3, 2, 0, 0)`.
5. The `3` means the host accepts up to 3 clients.
6. The `2` means ENet can use 2 channels.
7. The code clears the `peers[]` array.
8. The code sets `peer_count = 0`.
9. The code sets `g_net_active = true`.
10. The code sets `g_local_player_id = 0`.
11. The code sets `g_net_connected = false` until a client actually connects.

The host does not need to connect to itself. It already exists as player `0`.

### 18.16 Client join flow

Client join happens through `net_join(ip, port)`.

Step by step:

1. The client creates an ENet host with `enet_host_create(NULL, 1, 2, 0, 0)`.
2. Passing `NULL` means this is not binding as a server address.
3. The `1` means the client needs one outgoing peer connection.
4. The client clears the `peers[]` array.
5. The client converts the host IP string into an `ENetAddress`.
6. The client stores the host port.
7. The client calls `enet_host_connect(client_or_server, &address, 2, 0)`.
8. The returned peer is stored in `peers[0]`.
9. The code sets `g_net_active = true`.
10. The code sets `g_local_player_id = -1`.
11. The client waits for the host to assign the real player id.

On a client, `peers[0]` means "the server/host".

On the host, `peers[0]`, `peers[1]`, and `peers[2]` mean connected clients for player ids `1`, `2`, and `3`.

### 18.17 Connection event flow

Network events are processed by `net_update(gs)`.

`net_update(gs)` calls:

- `enet_host_service(client_or_server, &event, 0)`

This asks ENet whether anything network-related happened.

Possible event types handled by the game:

- `ENET_EVENT_TYPE_CONNECT`
- `ENET_EVENT_TYPE_RECEIVE`
- `ENET_EVENT_TYPE_DISCONNECT`
- `ENET_EVENT_TYPE_NONE`

When a client connects to the host:

1. ENet reports `ENET_EVENT_TYPE_CONNECT`.
2. The game sets `g_net_connected = true`.
3. The host increments `peer_count`.
4. The host chooses the client's player id from that count.
5. The host stores the ENet peer pointer in `peers[assigned_id - 1]`.
6. The host sends `PKT_ID_ASSIGN` directly to that new peer.
7. The host sends `PKT_SYNC_SEED` directly to that new peer.
8. The host broadcasts `PKT_LOBBY_SYNC` so everyone can update lobby player count.

The id assignment packet is direct because only the new client needs to learn its assigned id.

The lobby sync packet is broadcast because everyone should know how many players are connected.

### 18.18 The custom packet format

Every gameplay network message uses this struct:

```c
typedef struct {
    uint8_t  type;
    uint8_t  player;
    uint16_t unit_count;
    uint16_t units[64];
    int32_t  tx;
    int32_t  ty;
    int32_t  target_id;
    int32_t  extra;
} NetPacket;
```

The struct is wrapped in:

```c
#pragma pack(push, 1)
...
#pragma pack(pop)
```

This tells the compiler not to insert padding bytes between fields. That matters because the code sends the raw memory bytes of this struct over the network.

The packet fields mean:

- `type`: which kind of command this is
- `player`: which player issued the command
- `unit_count`: how many entries in `units[]` are valid
- `units[64]`: unit ids involved in the command
- `tx`: target tile x coordinate or map x coordinate
- `ty`: target tile y coordinate or map y coordinate
- `target_id`: target unit id, building id, or selected building id depending on packet type
- `extra`: extra meaning that depends on packet type

The packet is generic. It does not have separate structs for move, attack, build, train, and research. Instead, each packet type reuses these same fields differently.

This is compact, but it means the documentation must define the meaning of each field for each packet type.

### 18.19 Why `extra` exists

`extra` is a reusable integer field.

It means different things for different packets.

Examples:

- in `PKT_SYNC_SEED`, `extra` is the random seed
- in `PKT_START_GAME`, `extra` is the total player count
- in `PKT_ATTACK`, `extra` says whether the target is a unit or building
- in `PKT_PLACE_BLD`, `extra` is the building type
- in `PKT_TRAIN_UNIT`, `extra` is the unit type
- in `PKT_STANCE`, `extra` is whether manual stance is on
- in `PKT_RESEARCH`, `extra` is the technology type
- in `PKT_LOBBY_SYNC`, `extra` is peer count
- in `PKT_ID_ASSIGN`, `extra` is the assigned player id

Because `extra` is overloaded, a reader should never interpret it alone. Always read it together with `type`.

### 18.20 How a packet is sent

All normal command sending goes through:

- `net_dispatch_packet(gs, pkt)`

That function does two things:

1. If networking is active, send the packet over the network.
2. Apply the packet locally immediately.

The code is:

```c
void net_dispatch_packet(GameState *gs, NetPacket *pkt) {
    if (g_net_active) net_send_packet(pkt);
    apply_packet(gs, pkt);
}
```

This is important.

It means the local player's action happens right away on their own machine instead of waiting for the packet to travel out and come back.

This keeps controls responsive.

### 18.21 How `net_send_packet()` works

`net_send_packet(pkt)` is the function that actually gives bytes to ENet.

Step by step:

1. If the game is not connected, return and send nothing.
2. If there is no ENet host/client object, return and send nothing.
3. Create an `ENetPacket` from the bytes of `NetPacket`.
4. Use `ENET_PACKET_FLAG_RELIABLE`.
5. Broadcast the packet on ENet channel `0`.
6. Flush the ENet host so the packet is pushed out promptly.

The key line is conceptually:

```c
enet_packet_create(pkt, sizeof(NetPacket), ENET_PACKET_FLAG_RELIABLE);
```

That means the exact bytes of `pkt` are copied into an ENet packet.

Then:

```c
enet_host_broadcast(client_or_server, 0, enet_pkt);
```

This sends the packet to all connected peers for that local ENet host object.

On a client, broadcasting from the client's ENet host effectively sends to its connected host peer.

On the host, broadcasting sends to connected clients.

### 18.22 How a packet is received

Packets are received in `net_update(gs)`.

Step by step:

1. `net_update(gs)` asks ENet for pending events.
2. ENet returns a receive event.
3. The game checks `event.packet->dataLength`.
4. The packet is accepted only if the length equals `sizeof(NetPacket)`.
5. The packet bytes are cast to `NetPacket *`.
6. The game calls `apply_packet(gs, pkt)`.
7. The ENet packet is destroyed after use.

The game does not parse JSON, text, or a variable-length command format.

The packet is binary.

The receiver expects exactly the same `NetPacket` memory layout.

### 18.23 How a move command travels through the system

Example: player selects units and right-clicks empty ground.

On the machine where the click happened:

1. Input code detects the click.
2. Input code decides this is a move command.
3. It creates `NetPacket pkt = {0}`.
4. It sets `pkt.type = PKT_MOVE`.
5. It sets `pkt.player = g_local_player_id`.
6. It stores the target tile in `pkt.tx` and `pkt.ty`.
7. It fills `pkt.units[]` with selected unit ids.
8. It sets `pkt.unit_count`.
9. It calls `net_dispatch_packet(gs, &pkt)`.

Inside `net_dispatch_packet()`:

1. The packet is sent over ENet if networking is active.
2. The packet is applied locally immediately.

On a remote machine:

1. ENet receives the packet.
2. `net_update(gs)` sees `ENET_EVENT_TYPE_RECEIVE`.
3. The bytes are interpreted as `NetPacket`.
4. `apply_packet(gs, pkt)` handles `PKT_MOVE`.
5. The receiver computes formation targets.
6. The receiver calls `unit_give_move_order()` for each listed unit.

The packet does not contain every future unit position. It only contains the move order. Each machine then simulates movement locally.

### 18.24 How a gather command travels through the system

Example: player selects villagers and right-clicks a forest tile.

Packet fields:

- `type = PKT_GATHER`
- `player = local player id`
- `units[] = selected villager ids`
- `unit_count = number of selected villagers`
- `tx = resource tile x`
- `ty = resource tile y`

When received:

- `apply_packet()` loops over the listed units
- it checks each id is below `MAX_UNITS`
- it calls `unit_give_gather_order(gs, unit, tx, ty)`

Only villagers should be included for gather commands. The input layer filters this before sending.

### 18.25 How an attack command travels through the system

Example: player selects military units and clicks an enemy.

Packet fields:

- `type = PKT_ATTACK`
- `player = local player id`
- `units[] = selected attacker ids`
- `unit_count = number of attackers`
- `target_id = enemy unit id or enemy building id`
- `extra = 0` if target is a unit
- `extra = 1` if target is a building

When received:

- `apply_packet()` loops over the listed units
- if `extra == 0`, the target is passed as a unit target
- if `extra == 1`, the target is passed as a building target
- each unit receives `unit_give_attack_order()`

The packet does not send damage. It sends the order to attack. Damage happens later through the normal combat simulation.

### 18.26 How building placement travels through the system

Example: player places a Barracks foundation.

Packet fields:

- `type = PKT_PLACE_BLD`
- `player = local player id`
- `extra = building type`
- `tx = building tile x`
- `ty = building tile y`
- `units[] = villager ids assigned to build`
- `unit_count = number of assigned builders`

When received:

1. `apply_packet()` calls `building_place(gs, player, building_type, tx, ty)`.
2. `building_place()` checks placement rules and affordability on that machine.
3. If placement succeeds, it returns a building id.
4. `apply_packet()` loops over builder ids.
5. Each builder receives `unit_give_build_order()` targeting the new building id.

Important detail:

The packet sends the requested building type and position, not a pre-created building object. Each machine creates the building locally from the same command.

### 18.27 How training a unit travels through the system

Example: player clicks "Villager" at the Town Center.

Packet fields:

- `type = PKT_TRAIN_UNIT`
- `player = local player id`
- `target_id = selected building id`
- `extra = unit type`

When received:

- `apply_packet()` checks `target_id` is a valid building index
- it calls `building_enqueue_unit(gs, building, unit_type)`

The packet does not spawn the unit instantly unless the building logic would do that. It queues the unit, and the normal building update completes training over time.

### 18.28 How research travels through the system

Example: player starts a technology at a building.

Packet fields:

- `type = PKT_RESEARCH`
- `player = local player id`
- `target_id = selected building id`
- `extra = technology type`

When received:

- `apply_packet()` checks the building id
- it calls `building_start_tech(gs, building, tech_type)`

The packet starts research. The actual timer and completion happen through normal building update logic.

### 18.29 How age advancement travels through the system

Example: player clicks advance age.

Packet fields:

- `type = PKT_AGE_ADVANCE`
- `player = local player id`

When received:

- `apply_packet()` calls `res_try_advance_age(gs, player)`

The resource check and age-up timer are handled by the normal resource system.

### 18.30 How match start travels through the system

The host starts the match from the lobby.

When the host clicks start:

1. The host generates a seed using `time(NULL)`.
2. The host sends `PKT_SYNC_SEED` with `extra = seed`.
3. The host sends `PKT_START_GAME` with `extra = total player count`.

When `PKT_SYNC_SEED` is applied:

- the global `_rng` is set to the seed

When `PKT_START_GAME` is applied:

- `game_init_started_game(gs, _rng, np)` is called
- `np` comes from the packet's `extra`

This is how all machines are supposed to create the same starting match.

The seed matters because random map generation and random starts must be based on the same random sequence.

### 18.31 Complete packet field meaning table

| Packet type | `player` | `units[]` / `unit_count` | `tx`, `ty` | `target_id` | `extra` |
| --- | --- | --- | --- | --- | --- |
| `PKT_SYNC_SEED` | unused | unused | unused | unused | random seed |
| `PKT_START_GAME` | unused | unused | unused | unused | total player count |
| `PKT_MOVE` | issuing player | units to move | destination tile | unused | unused |
| `PKT_GATHER` | issuing player | villagers to gather | resource tile | unused | unused |
| `PKT_ATTACK` | issuing player | attackers | unused | target unit/building id | `0` for unit, `1` for building |
| `PKT_BUILD` | issuing player | builders | unused | building id | unused |
| `PKT_PLACE_BLD` | issuing player | builders | placement tile | unused before placement | building type |
| `PKT_TRAIN_UNIT` | issuing player | unused | unused | trainer building id | unit type |
| `PKT_DELETE_BLD` | issuing player | unused | unused | building id | unused |
| `PKT_AGE_ADVANCE` | issuing player | unused | unused | unused | unused |
| `PKT_ID_ASSIGN` | unused | unused | unused | unused | assigned player id |
| `PKT_STANCE` | issuing player | affected units | unused | unused | manual stance flag |
| `PKT_LOBBY_SYNC` | unused | unused | unused | unused | connected peer count |
| `PKT_RESEARCH` | issuing player | unused | unused | research building id | technology type |
| `PKT_SET_RALLY` | issuing player | unused | rally tile | building id | unused |

### 18.32 Why local application and network sending both happen

When the local player gives a command, the game applies it immediately and sends it to others.

This has one big benefit:

- local input feels responsive

Without this, a player might click move and see nothing happen until the packet travels through the network and is processed.

The tradeoff is:

- the local machine trusts its own command immediately
- the architecture assumes the same command will also be accepted by the other machines

There is no server authority rollback step that later says, "that local action was invalid, undo it."

### 18.33 Who is authoritative

This implementation is not a strict server-authoritative simulation.

The host has special authority for:

- accepting clients
- assigning player ids
- sharing the startup seed
- broadcasting lobby peer count
- starting the match from the lobby UI

But once gameplay commands are flowing, every machine applies commands to its own local simulation.

The host is not sending full corrected world snapshots.

The host is not validating every command and then sending approved state.

The game is closer to peer command replication, with the host acting as lobby owner and connection hub.

### 18.34 What "reliable" means here

Packets are created with:

- `ENET_PACKET_FLAG_RELIABLE`

In practical terms, this asks ENet to deliver the packet reliably instead of treating it as disposable.

This is important because gameplay commands should not randomly disappear.

For example:

- a build command should not be lost
- an attack command should not be lost
- a research command should not be lost
- a match start command should not be lost

Reliable does not mean the game can never desync. It only means ENet is trying to deliver the packet reliably at the transport layer.

### 18.35 What is not sent over the network

The game does not send:

- every unit position every frame
- every unit HP every frame
- every building HP every frame
- every projectile every frame
- every resource amount every frame
- full map state after match start
- full player state snapshots
- checksums
- rollback data
- deterministic frame numbers
- input delay buffers
- replay logs

Those values are expected to evolve locally from the same starting state and the same command stream.

### 18.36 Why the same starting seed matters

The starting seed controls random generation.

If two machines use different seeds, they may create different maps, different starting positions, or different resource placements.

After that, the same command could mean different things on each machine.

Example:

- Player sends "move to tile 20, 20".
- On machine A, tile 20,20 is grass.
- On machine B, tile 20,20 is water.
- The unit behavior can diverge.

This is why the host sends `PKT_SYNC_SEED` before `PKT_START_GAME`.

### 18.37 Why unit ids and building ids matter

Packets refer to units and buildings by array index/id.

For example:

- unit id `17`
- building id `4`

This only works if all machines agree that id `17` means the same unit and id `4` means the same building.

That is another reason all machines must start the same way and apply commands consistently.

If one machine creates an extra unit or building that another machine does not create, ids can stop matching.

After ids stop matching, commands may affect the wrong object.

### 18.38 Packet validation level

The receive path has minimal validation.

It checks:

- received packet size equals `sizeof(NetPacket)`
- unit ids are below `MAX_UNITS` before unit commands are applied
- building ids are within `MAX_BUILDINGS` for several building commands

It does not deeply verify every gameplay rule at the network boundary.

Instead, the called gameplay functions perform their usual checks where available, such as affordability, placement, age requirements, or queue validity.

This means networking is mostly a command transport layer, not a security or anti-cheat layer.

### 18.39 Disconnect behavior

When a disconnect event happens:

1. The game prints `"ENet disconnected."`.
2. If this machine is host, it searches `peers[]` for the disconnected peer.
3. The host clears that peer slot.
4. The host decrements `peer_count`.
5. The host broadcasts `PKT_LOBBY_SYNC`.
6. If this machine is a client and `peer_count == 0`, it marks `g_net_connected = false`.

The implementation does not include advanced reconnect, migration, pause-on-disconnect, or state recovery.

### 18.40 Practical architecture diagram

```text
Local player input
        |
        v
Input/HUD code decides command type
        |
        v
Fill NetPacket fields
        |
        v
net_dispatch_packet()
        |
        +----------------------+
        |                      |
        v                      v
net_send_packet()          apply_packet()
        |                      |
        v                      v
ENet reliable packet       local GameState changes now
        |
        v
Remote machine receives packet in net_update()
        |
        v
Remote apply_packet()
        |
        v
Remote GameState changes in the same logical way
```

### 18.41 Important beginner takeaway

The networking code does not synchronize "what the world looks like".

It synchronizes "what commands players gave".

That is the central idea.

Everything else follows from that:

- the same seed is needed so the starting world matches
- ids must match so commands affect the same units/buildings
- packet meanings must be explicit because the packet is custom
- reliable delivery is used because commands are important
- desync correction is not present because full world snapshots are not sent

## 19. Campaign architecture

Campaign logic is centralized in `game.c`.

### 19.1 Campaign structure

There are 6 missions:

1. Landfall
   - gather 120 food and 80 wood

2. A Home In The Vale
   - build 2 houses, a mill, a lumber camp, and reach 6 villagers

3. Iron In The Hills
   - build mining camp, barracks, 2 farms, and advance to Feudal

4. The Broken Watch
   - build an archery range, train archers and barracks troops, destroy enemy barracks
   - includes a scripted raid wave

5. Hold The Ford
   - survive 3 timed waves while protecting the town

6. Break The Ashen Banner
   - destroy the enemy town center
   - includes allied reinforcement wave

### 19.2 Scripting model

Campaign scripting uses:

- `campaign_mission`
- `campaign_event_stage`
- `campaign_event_timer`
- mission-specific resource overrides
- mission-specific prebuilt structures and units

The scripting is hand-authored in C, not data-driven from external files.

### 19.3 Briefing system

Campaign missions open with a blocking briefing overlay:

- story text
- objective text
- briefing text
- dismissal through enter/space/click/tap

While briefing is open, gameplay simulation is effectively paused at the input layer.

## 20. Sandbox architecture

Sandbox is a systems validation mode, not just a cheat mode.

Features:

- full-tech and full-city setup
- quick actions:
  - add resources
  - advance age
  - spawn allied wave
  - spawn enemy wave
  - restore selected or all friendly units/buildings
- keyboard shortcuts:
  - `F1` resources
  - `F2` next age
  - `F3` ally wave
  - `F4` enemy wave
  - `F5` restore

## 21. Input and command architecture

Input is split across:

- [src/ui/input/input_commands.c](/Users/sooryaakilesh/test/rts/src/ui/input/input_commands.c)
- [src/ui/input/input_selection.c](/Users/sooryaakilesh/test/rts/src/ui/input/input_selection.c)
- `input_camera.c`

### 21.1 Core philosophy

Selection and commands are context-sensitive:

- clicking friendly unit: select
- clicking friendly building: select
- clicking resource with villager(s): gather
- clicking unfinished or damaged friendly building with villager(s): build or repair
- clicking enemy: attack
- clicking empty ground: move
- clicking drop-off with carrying villager(s): return resources

### 21.2 Box selection

Desktop:

- left drag selects units
- threshold is based on screen-space drag size

Android:

- taps are point selection only
- dragging is reserved for camera / touch behavior

### 21.3 Build mode

Villagers enter build mode through:

- `B` build panel
- direct building hotkeys

Build mode supports:

- normal single placement for most buildings
- drag-line placement for walls and gates using Bresenham-like line point generation
- `Shift` to keep placing after one structure
- `ESC` or right-click to cancel

### 21.4 Rally mode

- `G` enters rally placement for selected rally-capable building
- click map to set rally
- right-click or `ESC` cancels

### 21.5 Pause / end-screen controls

- `P` toggles pause
- `R` restarts or retries
- `Q` quits to menu or exits window depending on mode
- `N` advances campaign mission on victory

### 21.6 Hero possession trigger

- `E` on a selected eligible unit in solo play starts hero possession
- clicking the same eligible selected unit can also retrigger the attempt

## 22. Rendering architecture

### 22.1 2D world renderer

The normal RTS view is rendered in isometric 2D.

Renderer responsibilities:

- terrain diamonds
- resource overlays
- building sprites or procedural shapes
- wall connectivity rendering
- units
- projectiles
- selection outlines
- fog overlay
- rally previews

### 22.2 Sprite strategy

Buildings are mostly rendered from textures with custom per-building scaling heuristics so source art of different dimensions still lands correctly on the map footprint.

Walls and gates are procedural and connectivity-aware.

### 22.3 3D hero renderer

Hero possession uses a separate renderer:

- draws tiles as cubes/planes in 3D
- renders simple 3D representations for units and many structures
- uses imported 3D models where available for several hero-view structures
- draws an FPS-style weapon overlay
- shows projectiles in 3D

This is not the same scene graph as the 2D RTS renderer; it is a parallel rendering path over the same simulation state.

### 22.4 HUD

HUD modules provide:

- top resource bar and age status
- bottom unit/building/action panel
- minimap
- alerts
- build menu
- campaign panel
- campaign briefing
- end screens
- map overview with side-by-side OSM and game map in sandbox/OSM flows

## 23. UI state model

`UIState` stores:

- camera state
- hero camera state
- selection and hover state
- build/rally/menu panel toggles
- multiplayer IP entry state
- OSM location entry state
- loaded textures/models/sounds
- cached downloaded OSM tile textures

This separation means gameplay state remains mostly in `GameState`, while view/transient interaction state lives in `UIState`.

## 24. Build and platform architecture

### 24.1 Desktop macOS build

From `Makefile`:

- compiler: `gcc`
- C standard: C11
- uses Homebrew `raylib` and `curl`
- output: `rts`

Core flags:

- `-std=c11`
- `-Wall -Wextra -O2 -g`
- `-DRTS_HAS_CURL=1`

### 24.2 Linux CI build

GitHub Actions builds:

- native `amd64`
- cross-compiled `arm64`

Raylib is built from source in CI, then the game is compiled against that build.

### 24.3 Windows CI build

Windows uses:

- LLVM-MinGW cross compilers
- x86_64 and aarch64 targets
- static raylib build from source

Required link libraries:

- `raylib`
- `winmm`
- `gdi32`
- `opengl32`
- `ws2_32`
- `iphlpapi`

The repo also contains a Windows header compatibility fix in `net.h` to avoid collisions between Windows APIs and raylib symbols.

### 24.4 Android build

Android build path:

- Gradle wrapper
- Android SDK / NDK
- CMake
- raylib fetched through `FetchContent`
- game compiled as shared library `rts`

Important Android-specific note:

- linker option `-u ANativeActivity_onCreate` is forced so the NativeActivity entrypoint is not stripped

### 24.5 Release workflow

The CI workflow:

1. builds each target platform
2. uploads artifacts
3. optionally creates a prerelease on pushes to `main` or `master`

Targets:

- macOS arm64
- Linux amd64
- Linux arm64
- Windows amd64
- Windows arm64
- Android arm64 APK

## 25. System architecture summary by responsibility

### 25.1 Simulation order

During gameplay:

1. alerts tick down
2. campaign briefing early-out if open
3. hero possession lifecycle updates
4. simulation time scale is computed
5. pop caps are recomputed
6. age advancement timers update
7. units update
8. buildings update
9. projectiles update
10. AI updates if applicable
11. campaign scripting updates if applicable
12. fog updates

This ordering matters:

- units move before buildings produce/project
- buildings act before projectile impacts resolve in the same frame's projectile update
- fog reflects post-update positions

### 25.2 Architecture style

The game is primarily:

- stateful
- array-of-objects
- immediate-mode update/render
- simulation-first, not event-sourced

There is no formal component registry, no scripting VM, and no resource database beyond the hardcoded tables.

## 26. Algorithms and heuristics inventory

This section is a concise checklist of the main implemented algorithms and strategies.

### 26.1 Deterministic / pseudo-deterministic primitives

- xorshift-style RNG in `rng_next()`
- fixed-size arrays for all entities
- integer tile addressing for pathfinding and world structure

### 26.2 Spatial algorithms

- A* pathfinding on 8-neighbor grid
- octile heuristic
- ring-based nearest-passable search
- nearest-adjacent-perimeter search for building interactions
- pairwise separation resolution with tangential slide

### 26.3 Map generation algorithms

- quadrant-based randomized start distribution
- Fisher-Yates direction shuffle for starter resources
- elliptical lake placement
- circular resource/forest cluster painting
- OSM feature rasterization onto tile grid

### 26.4 Combat heuristics

- nearest target auto-acquisition
- building perimeter attack spread based on attacker id
- attack repath cooldown using `anim_timer`
- extended melee fallback when pathfinding is blocked near target
- projectile travel arcs with splash for stone projectiles

### 26.5 AI heuristics

- desired worker-count balancing by resource type
- just-in-time housing
- age-up resource saving thresholds
- rule-based build order
- rule-based composition training
- scout proximity-biased exploration
- periodic mass-attack trigger

### 26.6 Networking strategy

- reliable ENet command replication
- local immediate application through dual dispatch
- host-assigned player IDs
- shared startup seed synchronization

## 27. Known implementation caveats and important as-built notes

These are not external critiques; they are important facts about the implementation as it currently exists.

1. Networking is command-replicated, not full-state synchronized.
2. There is no rollback, checksum sync, or state correction path.
3. Multiplayer manual resource drop-off does not have a dedicated packet type and is weaker than the local single-player path.
4. The AI only controls player 1.
5. Many campaign behaviors are fully scripted in C rather than data-driven.
6. Hero possession is explicitly disabled in multiplayer.
7. Sandbox forces full visibility and population cap behavior that differs from standard play.
8. OSM generation depends on curl-enabled builds and external services.

## 28. File map: where to look for what

- Core data model: [src/core/game.h](/Users/sooryaakilesh/test/rts/src/core/game.h)
- Match flow / campaign / sandbox / projectiles: [src/core/game.c](/Users/sooryaakilesh/test/rts/src/core/game.c)
- Random map generation / fog / resource search: [src/core/map.c](/Users/sooryaakilesh/test/rts/src/core/map.c)
- Pathfinding: [src/core/pathfinding.c](/Users/sooryaakilesh/test/rts/src/core/pathfinding.c)
- Costs / ages / resource economy: [src/core/resources.c](/Users/sooryaakilesh/test/rts/src/core/resources.c)
- Building placement / production / research / tower behavior: [src/core/building.c](/Users/sooryaakilesh/test/rts/src/core/building.c)
- Unit stats and order issuance: [src/core/unit/unit_orders.c](/Users/sooryaakilesh/test/rts/src/core/unit/unit_orders.c)
- Unit runtime behavior: [src/core/unit/unit_ai.c](/Users/sooryaakilesh/test/rts/src/core/unit/unit_ai.c)
- RTS AI: [src/ai/ai.c](/Users/sooryaakilesh/test/rts/src/ai/ai.c)
- Networking protocol and host/client logic: [src/core/net.h](/Users/sooryaakilesh/test/rts/src/core/net.h), [src/core/net.c](/Users/sooryaakilesh/test/rts/src/core/net.c)
- Hero possession: [src/core/hero_possession.c](/Users/sooryaakilesh/test/rts/src/core/hero_possession.c)
- OSM map generator: [src/core/osm_mapgen.c](/Users/sooryaakilesh/test/rts/src/core/osm_mapgen.c)
- Input and commands: [src/ui/input/input_commands.c](/Users/sooryaakilesh/test/rts/src/ui/input/input_commands.c), [src/ui/input/input_selection.c](/Users/sooryaakilesh/test/rts/src/ui/input/input_selection.c)
- HUD: [src/ui/hud/hud_bars.c](/Users/sooryaakilesh/test/rts/src/ui/hud/hud_bars.c), [src/ui/hud/hud_menus.c](/Users/sooryaakilesh/test/rts/src/ui/hud/hud_menus.c), [src/ui/hud/hud_overlays.c](/Users/sooryaakilesh/test/rts/src/ui/hud/hud_overlays.c)
- Rendering: [src/ui/renderer/renderer_world.c](/Users/sooryaakilesh/test/rts/src/ui/renderer/renderer_world.c), [src/ui/renderer/renderer_tiles.c](/Users/sooryaakilesh/test/rts/src/ui/renderer/renderer_tiles.c), [src/ui/renderer/renderer_hero.c](/Users/sooryaakilesh/test/rts/src/ui/renderer/renderer_hero.c)
- Desktop build: [Makefile](/Users/sooryaakilesh/test/rts/Makefile)
- CI build matrix: [.github/workflows/ci.yml](/Users/sooryaakilesh/test/rts/.github/workflows/ci.yml)
- Android native build: [android/app/src/main/cpp/CMakeLists.txt](/Users/sooryaakilesh/test/rts/android/app/src/main/cpp/CMakeLists.txt)

## 29. Final interpretation

As implemented, this project is a handcrafted C RTS with:

- tile-based economy and city-building
- unit production, combat, and upgrades
- 2D isometric strategy presentation
- a temporary 3D first-person control mode
- rule-based single-player AI
- ENet command replication for multiplayer
- scripted campaign progression
- a sandbox validation environment
- an unusual but functional real-world OSM map ingestion path

The most important architectural idea is that nearly all mechanics are encoded directly in plain C logic and small data tables, with `GameState` as the single source of truth. Understanding `GameState`, the frame update order, the unit/building update routines, and the packet application path is enough to understand nearly the entire game.

## 30. Plain-English companion guide

This section repeats the most important ideas in simpler language. It is meant for junior developers, designers, producers, testers, and anyone who wants to understand the game without reading C code first.

### 30.1 What this game is, in one paragraph

This is a real-time strategy game. The player gathers resources, builds a base, trains units, researches upgrades, fights enemies, and tries to win the match. The game is written mostly as direct C code. That means the rules are not hidden inside a separate engine or scripting system. Most gameplay behavior can be found by reading the source files listed in this document.

### 30.2 The big picture

Think of the game as a board game that updates very quickly.

- The map is the board.
- Units and buildings are the pieces.
- Resources are the currency.
- Commands are the player's instructions.
- The rules decide what each piece is allowed to do.
- Rendering is the part that shows the board and pieces on screen.

The game does not create a new world every frame. Instead, it keeps updating the same `GameState` object. `GameState` is like the master notebook for the match.

### 30.3 What happens every frame

Every frame, the game follows the same basic routine:

1. Read what the player is doing.
2. Read any multiplayer messages.
3. Update the match rules.
4. Draw the result.

For example, if a player right-clicks a tree with villagers selected:

1. Input code understands the click as a gather command.
2. The command is applied to the selected villagers.
3. The villagers find a path to the tree.
4. The simulation moves them closer each frame.
5. Once close enough, they gather wood.
6. When full, they walk to a drop-off building.
7. The player receives wood.
8. The renderer draws each visible step.

### 30.4 Why `GameState` matters so much

`GameState` is the central place where the match lives.

It contains things like:

- which game mode is active
- whether the game is in menu, playing, paused, victory, or defeat
- every map tile
- every unit
- every building
- every projectile
- each player's resources
- campaign progress
- AI timers
- fog of war
- hero possession state

If you want to know what is currently true in the game, look for it in `GameState` or in data connected directly to it.

### 30.5 Why the code uses fixed-size arrays

The game stores units, buildings, and projectiles in fixed-size arrays.

That means the game reserves space for a maximum number of objects, such as 256 units or 128 buildings. Each slot has an `active` flag that says whether that slot is currently being used.

Simple analogy:

- Imagine a parking lot with 256 spaces for units.
- A unit exists when a parking space is marked active.
- When a unit dies, that parking space becomes available again.

This is common in C games because it is predictable, fast, and simple to reason about.

### 30.6 How the map works

The map is a 64 by 64 grid of tiles.

Each tile can represent terrain or resources, such as:

- grass
- water
- forest
- gold
- stone
- berries
- road
- desert
- farm

Some tiles can be walked on. Some cannot. For example, water and forest block movement, while grass and roads are passable.

The game also uses the tile grid to decide where buildings can be placed. A building usually occupies more than one tile, so placement checks the full footprint.

### 30.7 Why there are multiple coordinate systems

The game uses different coordinate systems because different jobs need different measurements.

Tile coordinates are good for map rules:

- "This unit is near tile 10, 12."
- "Can a building fit here?"
- "Is this tile blocked?"

World coordinates are good for smooth movement:

- "This unit is moving from pixel position A to pixel position B."
- "Move a little closer this frame."

Isometric screen coordinates are good for drawing:

- "Show the flat world as a tilted RTS view."

Hero 3D coordinates are good for first-person mode:

- "Place the same unit and building data into a simple 3D scene."

### 30.8 How units move

When a unit receives a move order, it usually does not walk in a straight line blindly. The game asks the pathfinding system to find a route through passable tiles.

The pathfinding algorithm is A*.

Beginner explanation:

- A* looks at nearby tiles.
- It avoids blocked tiles.
- It estimates which route is likely shortest.
- It builds a list of waypoints.
- The unit follows those waypoints one by one.

If many units are selected, the game gives them formation targets instead of sending every unit to the exact same spot. This helps prevent a crowd from stacking on one tile.

### 30.9 How resources work

The economy has four resources:

- food
- wood
- gold
- stone

Villagers gather resources from map tiles or farms. They carry a limited amount. Once full, they return to the correct drop-off building.

Simple examples:

- Wood can go to a Town Center or Lumber Camp.
- Food can go to a Town Center or Mill.
- Gold and stone can go to a Town Center or Mining Camp.

The game checks affordability before building, training, researching, or advancing age. If the player cannot afford something, the action is rejected.

### 30.10 How buildings work

Buildings have three main roles:

- unlock actions, such as training units or researching upgrades
- support the economy, such as drop-off buildings and farms
- defend or control space, such as towers, walls, gates, and castles

Most buildings start as incomplete foundations. Villagers must construct them. Once complete, the building can perform its full role.

Example:

1. Player places a Barracks.
2. Resources are deducted.
3. A foundation appears.
4. Villagers build it over time.
5. Once complete, the Barracks can train infantry.

### 30.11 How unit training works

Training means creating new units from a building.

The building must be complete, must support that unit type, and must not be busy researching. The player also needs enough resources and population space.

Example:

- Town Center trains Villagers and Scouts.
- Barracks trains infantry.
- Archery Range trains ranged units.
- Stable trains cavalry.
- Siege Workshop trains siege units.

The production queue has limited space, so a building cannot queue unlimited units.

### 30.12 How combat works

Combat follows a clear flow:

1. A unit gets a target.
2. The unit checks whether the target is valid.
3. If too far away, the unit moves closer.
4. If close enough, the unit attacks after its cooldown is ready.
5. Damage is calculated.
6. If HP reaches zero, the target begins dying or is destroyed.

Some units have bonus damage against certain targets. This creates counterplay.

Examples:

- Spearmen are strong against cavalry.
- Skirmishers are strong against archers.
- Siege units are strong against buildings.
- Scouts are strong against monks.

Ranged attacks use projectiles. A projectile travels toward the target and applies damage when it arrives.

### 30.13 How upgrades work

Technologies improve units, buildings, or economy behavior.

The important idea is that upgrades are stored as unlocked flags for each player. When an upgrade finishes, the game refreshes stats for that player's units and buildings.

This keeps the logic easier to manage:

- Start with base stats.
- Check which upgrades are unlocked.
- Apply the matching bonuses.

### 30.14 How ages work

Ages are progression stages.

The game starts in Dark Age. Players can advance through:

- Feudal
- Castle
- Imperial

Advancing age costs resources and takes time. Higher ages unlock more buildings, units, and technologies.

Beginner analogy:

- Dark Age is the early-game toolbox.
- Feudal adds basic military and economy expansion.
- Castle adds stronger units and advanced buildings.
- Imperial unlocks the late-game power options.

### 30.15 How the AI works

The AI is rule-based. It does not use machine learning.

It follows practical rules such as:

- keep villagers working
- gather the resources it needs most
- build houses before population blocks production
- build military buildings as the game progresses
- train an army
- attack when the army is large enough

This makes the AI understandable and predictable. If the AI behaves strangely, the cause is usually in one of its rules or thresholds.

### 30.16 How campaign missions work

Campaign missions are scripted directly in C.

That means each mission can set up:

- starting resources
- starting units
- starting buildings
- mission objective
- enemy behavior
- timed events
- victory or defeat conditions

This is flexible for a small project, but changing campaign content usually requires editing code.

### 30.17 How sandbox mode should be understood

Sandbox mode is not only for casual play. It is also a testing environment.

It gives the player many resources, high age, large population cap, and many available systems. This makes it useful for testing:

- combat
- buildings
- upgrades
- healing
- siege
- restoration actions
- large unit groups

If a developer wants to quickly check whether a mechanic works, sandbox is usually the fastest place to try it.

### 30.18 How hero possession fits into the RTS

Hero possession temporarily changes the player's viewpoint and control style.

Normal RTS mode:

- player selects units
- player gives commands
- units follow simulation rules

Hero possession:

- player directly controls one eligible unit
- camera switches to a first-person style
- movement, attack, block, and dodge become direct actions

The same match still exists underneath. The mode is a temporary alternate control layer over the same simulation state.

### 30.19 How multiplayer works

Multiplayer sends commands, not the full game world.

For example, when a player orders units to move, the network sends a message like:

- player id
- command type
- selected unit ids
- target tile

Each computer receives the command and applies it to its own local copy of the game.

This is lighter than sending every unit position every frame, but it depends on each computer staying in sync. The current implementation is practical, but it does not yet include advanced safety systems like rollback, snapshots, or checksum-based correction.

The most important beginner-friendly sentence is:

- multiplayer sends "what the player asked for", not "the full result of what happened".

Example:

- The packet does not say "unit 5 is now at pixel 650,700".
- The packet says "unit 5 was ordered to move to tile 20,22".
- Each machine then moves unit 5 using its own local simulation.

This is why the machines must agree on the starting map, unit ids, building ids, and command order.

If those do not match, the same packet can produce different results on different machines.

In this project, ENet is the delivery tool, while `NetPacket` is the game's custom message format. ENet carries the message; the RTS code decides what the message means.

For the full explicit breakdown, read sections 18.10 through 18.41.

### 30.20 How rendering should be understood

Rendering means drawing the game.

The renderer does not decide the gameplay rules. It reads the current game state and turns it into visuals.

There are two main rendering paths:

- normal 2D isometric RTS rendering
- separate 3D rendering for hero possession

This means a bug in how something looks may be in renderer code, while a bug in how something behaves is usually in core simulation code.

### 30.21 How input becomes gameplay

Input code translates player actions into game commands.

Examples:

- click a friendly unit: select it
- right-click ground: move selected units
- right-click tree with villagers: gather wood
- click enemy: attack
- press build hotkey: enter build mode
- place a building: create foundation and assign builders

This layer is important because the same mouse click can mean different things depending on what is selected and what was clicked.

### 30.22 How to debug common issues

If a unit does not move:

- check whether the destination tile is passable
- check whether pathfinding found a path
- check whether the unit has a valid order
- check whether another system is clearing the path

If a villager does not gather:

- check whether the target resource exists
- check whether the villager can reach an adjacent tile
- check whether the villager's carry amount is already full
- check whether a valid drop-off building exists

If a building cannot be placed:

- check whether the tile footprint is buildable
- check whether another building already occupies the space
- check whether the player has enough resources
- check whether the building requires a later age

If multiplayer behaves differently on two machines:

- check whether both sides started from the same seed
- check whether both sides received the same commands
- check whether a local-only action changed simulation state
- check whether the behavior depends on timing or scanning order

If the AI seems stuck:

- check its resource counts
- check whether villagers are idle or blocked
- check whether it is saving for age-up
- check whether it has the required buildings
- check whether population cap is blocking training

### 30.23 Suggested reading order for new contributors

For a new developer, the easiest reading order is:

1. Read this plain-English companion guide.
2. Read sections 1, 2, and 3 to understand the architecture and `GameState`.
3. Read section 25 to understand frame update order.
4. Read section 28 to learn where each feature lives.
5. Pick one gameplay system, such as resources or combat.
6. Read that section and then open the matching source file.

For a non-technical reader, the most useful sections are:

- Beginner-friendly reading guide
- 30.1 through 30.4 for the main idea
- 30.9 through 30.18 for gameplay systems
- 30.19 for multiplayer expectations
- 30.22 for common debugging conversations

### 30.24 Safe way to change the game

When changing this project, make small changes and test one behavior at a time.

Good examples:

- Change one unit's cost, then test training that unit.
- Change one gather rate, then test villager gathering.
- Add one building rule, then test placement and construction.
- Add one packet type, then test host and client behavior.

Avoid changing many systems at once unless you already understand how they connect. In this codebase, a gameplay feature often touches multiple layers: input, simulation, UI, rendering, AI, and networking.

### 30.25 One-sentence summary for each major area

- `main.c`: starts the game and runs the frame loop.
- `game.h`: defines the main data structures.
- `game.c`: controls match setup, campaign flow, projectiles, and the simulation order.
- `map.c`: creates and updates the tile map, resources, passability, and fog.
- `pathfinding.c`: finds walkable routes.
- `resources.c`: handles player resources, age advancement, and affordability.
- `building.c`: handles placement, construction, production, research, and defensive buildings.
- `unit_orders.c`: defines unit stats and assigns orders.
- `unit_ai.c`: updates unit behavior every frame.
- `ai.c`: controls the computer opponent.
- `net.c`: sends and receives multiplayer commands.
- `hero_possession.c`: controls the temporary first-person mode.
- `osm_mapgen.c`: converts real-world map data into playable tiles.
- `src/ui/input/`: turns clicks and keys into commands.
- `src/ui/hud/`: draws menus, panels, resources, and overlays.
- `src/ui/renderer/`: draws the world, units, buildings, effects, and hero view.

### 30.26 Final beginner takeaway

You do not need to understand the whole codebase at once. The game is built from understandable parts: input, state, rules, AI, networking, and rendering. Most bugs or features can be approached by asking three questions:

1. Where is the data stored?
2. Which function changes that data?
3. Which renderer or UI code shows that data to the player?

Answering those three questions will usually point you to the right file and the right system.
