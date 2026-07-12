# mangos-baker

The offline data baker for MangosOne. It runs one pass over a WoW **2.4.3** client's
MPQ archives and produces the warm caches `mangosd` reads at runtime. It replaces the
old map-extractor + vmap-extractor + Movemap-Generator pipeline — one tool, four
independently selectable components.

## What it produces

| Component  | Output              | Contents |
|------------|---------------------|----------|
| `dbc`      | `<dest>/dbc/`       | The raw `*.dbc` set `DBCStores` loads at startup (locale-patched version wins). |
| `tile`     | `<dest>/tiles/`     | Fused terrain + static WMO/M2 collision tiles (`t_<map>_<tx>_<ty>.tile`, global-WMO `w_<map>.tile`) the runtime `FusedTerrain` reads. Each carries the model's **collidable** faces plus a binned-SAH **BVH built here** over them — Blizzard's authored MOBN/MOBR BSP was measured against it and retired (see `src/game/terrain/Accelerators.hpp`). |
| `gomodels` | `<dest>/gomodels/`  | Per-`displayId` game-object collision models (`go_<displayId>.tile`) for doors, lifts, bridges. |
| `nav`      | `<dest>/mmaps/`     | The Detour navmesh (`<map>.mmap` + `<map><y><x>.mmtile`) the server's MMAP layer loads. |

A `tile` fuses what used to be two separate files — the GridMap terrain heightfield/
liquid/area and the vmap static collision — into one, so the runtime has nothing to
reconcile and the old "use vmaps" switch is moot. Loading a tile decompresses and parses
the client data once and write-throughs the `.tile` in the exact `TileSerializer` payload
(magic `"TBCX"`) the runtime reads.

`nav` reads the **baked tiles, not the MPQs**, so the surface the pathfinder walks on is
exactly the surface `FusedTerrain` collides against — pathfinding cannot disagree with
collision. It therefore requires `tile` to have run at least once, and re-runs without
touching the client data.

## Usage

```
mangos-baker [components] [options]
```

**Components** — name any of `dbc`, `tile`, `gomodels`, `nav`, or `all`. If you name
none, every component runs (the old all-in-one behaviour).

**Options**

| Flag            | Default | Meaning |
|-----------------|---------|---------|
| `--src <dir>`   | `Data`  | The client `Data/` dir (`common.MPQ`, `expansion.MPQ`, locale MPQs, …). |
| `--dest <dir>`  | `cache` | Output root; each component writes its own subdir under it. |
| `--locale <loc>`| `enGB`  | Client locale folder to resolve locale-patched archives. |
| `--map <id>`    | all     | Bake only this map id (from `Map.dbc`). Applies to `tile` and `nav`. |
| `-h`, `--help`  |         | Print usage and exit. |

### Examples

```sh
# Bake everything with defaults (client at ./Data, output to ./cache)
mangos-baker

# Only extract the DBCs
mangos-baker dbc

# Rebuild just the terrain tiles for Azeroth, then its navmesh
mangos-baker tile --map 0
mangos-baker nav --map 0

# Point at a client elsewhere and a custom output dir
mangos-baker tile --src "C:/Games/WoW 2.4.3/Data" --dest ./serverData
```

Extraction (`dbc`, `tile`, `gomodels`) is single-threaded: the client-data reader is
single-owner and not thread-safe, and the work is I/O-bound rather than CPU-bound.

`nav` **is** multi-threaded — it touches no MPQ, and each tile reads one `.tile` and writes
one `.mmtile` independently, so it runs one tile per core by default (`NavConfig::threads`).
Tiles are handed to workers one at a time rather than partitioned up front, because tile
cost varies by an order of magnitude between a dense city and an empty coastline.

### Progress

Extraction stages print a single carriage-return-updated status line so a long bake shows
motion rather than a dead cursor — a DBC file counter, a per-map `col N/64 (T terrain)`
line while loading, and a GO-model counter.

The `nav` stage cannot use a status line: its workers finish out of order. It prints one
`*` per ten completed tiles instead, trailing the map's header line. A single character
cannot tear, so the marks need no lock and their interleaving is harmless.

## The probes

Four read-only tools ship beside the baker. They link the **runtime** query path
(`FusedTerrain` + `src/game/terrain`), not the baker's extraction half, so an answer here is
exactly what `mangosd` would compute for the same point — and a bug they clear is a bug the
server does not have. They read baked tiles and never touch the client MPQs.

| tool | invocation | what it drives |
|------|-----------|----------------|
| `mangos-tiletest` | `<tileDir> <map> <x> <y> [z]` | One point: floor Z, liquid, area id, the WMO triple (root/adt/group) + MOGP flags, indoor/outdoor. |
| `mangos-height-check` | `<tileDir> <probes.csv>` | Every row of `probes.csv` through `GetHeightStatic`, scored PASS/FAIL against an expected floor. |
| `mangos-accelbench` | `<tileDir> <map> [rays]` | BVH / BIH / kd-tree against **brute force over the same triangle soup**, for correctness first and speed second. |
| `mangos-losbench` | `<tileDir> <probes.csv> [--lift L] [--range R] [--max N]` | Segment queries: pairs real spawns into sightlines and scores line-of-sight. |
| `mangos-bvhdebug` | `<tileDir> <map> [rays]` | Bisects an `accelbench` mismatch down to the node that causes it. |

### Which one answers your question

**The first three are all DOWNWARD COLUMN queries and are structurally blind to
line-of-sight.** `tiletest` and `height-check` probe straight down; `accelbench` benches the
acceleration structures. None of them ever calls `SegmentHitFrac`. A change to the LoS or
`GetHitPosition` ray can therefore break every sightline in the game while all three report
"no change" — which is exactly how a stray one-yard lift on the static LoS ray survived a
full engine port. If you touched a *segment* query, `losbench` is the only tool that can see it.

- **A spawn is in the wrong place / falling through the floor** → `tiletest` for the one point,
  then `height-check` for the cohort. Read the `probes.csv` caveats below before you believe a
  pass rate.
- **You changed an acceleration structure** → `accelbench`. It must print `mismatch 0`. Brute
  force is the only arbiter that assumes nothing; never score one tree against another.
- **`accelbench` reports a mismatch** → `bvhdebug`. `accelbench` tells you *that* the tree is
  wrong; `bvhdebug` tells you *why*, by naming the node. See below.
- **You changed LoS, `GetHitPosition`, or anything a segment ray touches** → `losbench`.
- **You changed the height/liquid/area path and believe it is behaviour-neutral** →
  `height-check` against a control binary. The per-probe output must be *identical*, not merely
  similar.

### How `bvhdebug` bisects a mismatch

Brute force is the oracle, so it can name not just the ray but the exact triangle it hit.
`bvhdebug` locates the leaf owning that triangle, reconstructs the root→leaf path, and asks two
*independent* questions at every node on it:

1. does this node's box actually **contain** the hit point? — if not, the **build** is wrong (a
   box failing to bound its own triangles);
2. does `intersectsRay()` **accept** this node for this ray? — if not, the **slab test** is wrong.

The first node answering *yes* to (1) and *no* to (2) is the bug, and the tool then replays that
node's slab arithmetic per axis at full float precision. Keeping the two questions apart is the
whole value: "the tree is built wrong" and "the tree is queried wrong" are indistinguishable from
the outside and have nothing in common as bugs.

That is how the padding in `Aabb::intersectsRay` was found: a downward floor probe whose x sat
**0.141 mm** outside a node's x-slab with `dx == 0`, rejected by an exact box test even though
`rayTri`'s 1e-5 barycentric slop happily accepted the triangle inside it. The broadphase was
*tighter* than the narrowphase, so the traversal skipped the leaf, missed the nearest floor and
returned the surface below it. **A broadphase must never be tighter than the narrowphase it
feeds.**

### Reading `probes.csv`

It is one row per `creature` row — `guid,map,x,y,z,expectedFloor`, where `expectedFloor` is
just `spawnZ + 0.15`. That assumes every spawn stands exactly on the floor, which is false:
air-capable spawns (`creature_template.InhabitType` 4) fail at 50–91% by design, and even the
ground-only cohort holds invisible mid-air markers and DB spawn sets inserted at a constant Z
offset. **A raw pass rate over all rows is close to meaningless — score the ground-only cohort,
and diff against a control rather than chasing an absolute number.**

### How `losbench` avoids needing a control build

The engine takes the caller's segment verbatim, so any candidate ray can be reproduced by
transforming the endpoints before asking. `losbench` exploits that: it computes *both* the
current ray and the alternative (`--lift L`, the retired engine-side lift) in **one binary
against one set of tiles**, and reports how many sightlines change answer. There is no second
build to go stale and no second tree to disagree with. Endpoints are real creature spawns
lifted by the same `+2.0` yards `WorldObject::IsWithinLOS` applies, so a pair it scores is a
sightline the server actually evaluates — not a synthetic ray through empty air.

Generalise the same trick for the next segment-ray question: express the old behaviour as a
transform of the endpoints, run both, count the flips.

## Build

The baker is built as part of the normal CMake tree when `-DBUILD_TOOLS=1` is set. It
shares — does not copy — the runtime's terrain collision engine (`src/game/terrain`), so
the writer and the reader can never disagree on the `.tile` format. DBCs are parsed with
the *server's* reader (`src/shared/DataStores/DBCFileLoader`) driven by the server's format
strings (`src/game/Server/DBCfmt.h`); that one `.cpp` is compiled in rather than linked
from the `shared` library, which exports ACE, MySQL and OpenSSL as PUBLIC dependencies.
It links `stormlib` (MPQ) plus Recast and Detour (navmesh). The installed binary lands in
`<install>/bin/tools/mangos-baker`.

Because that reader is compiled into an ACE-less target, `DataStores/DBCFileLoader.h` and
`Utilities/ByteConverter.h` must stay free of `Platform/Define.h`. Keep them that way.

## Notes

- Compatibility target is **2.4.3 (TBC) only**.
- The fused terrain `.tile` uses magic `"TBCX"` and a version byte; a version bump makes
  the runtime treat existing caches as a miss, so re-run `tile` (and then `nav`).
- `nav` does not emit off-mesh connections (jump links); the retired Movemap-Generator read
  them from an `offmesh.txt`.
- Transport-deck collision for *moving* boats/elevators is not implemented. `bakeGoModel`'s
  `bakeCollisionModelTile` is the reusable one-instance bake it would build on.

### Liquid

ADT terrain liquid is typed by the MCNK chunk flags (`0x04` river, `0x08` ocean, `0x10`
magma, `0x20` slime) — `LiquidType.dbc` is not involved, because in the MCLQ era the flags
*are* the type. It only becomes load-bearing for terrain with MH2O, which is WotLK+.

WMO interior liquid is different. Its identity comes from `MOGP.groupLiquid` (offset
`0x34`) — **not** from MLIQ's trailing `uint16`, which is a `materialId`. When the root
`MOHD.flags & 0x4` is set, `groupLiquid` is a raw `LiquidType.dbc` row id and the baker
resolves it through that DBC; otherwise it is a legacy code and `(id - 1) & 3` selects
water/ocean/magma/slime. Two TBC instances override the row by WMO path (Serpentshrine
water, Naxxramas slime). All of this is resolved at bake time and stored in the `.tile`,
so the runtime never re-derives a liquid category.

Note that 2.4.3's `LiquidType.dbc` `Type` column encodes `0 = magma, 2 = slime, 3 = water`
and cannot distinguish ocean from water — which is exactly why the id, not the column,
classifies rows below 21.
