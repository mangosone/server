# CLAUDE.md

Context for AI assistants — the Claude GitHub App (`@claude`) and contributors using Claude — working in
this repo. Humans: also read [`doc/CodingStandard.md`](doc/CodingStandard.md).

## Project

**MangosOne** — The Burning Crusade World of Warcraft **2.4.3** server (C++, MySQL/MariaDB). Compatibility target is
**2.4.3 only**; do **not** introduce 2.5+/TBC or later-expansion assumptions.

- **Database changes go in the separate `mangosone/database` repo**, not here — as transactional, idempotent
  `Rel##_##_###_*.sql` migrations that chain via `db_version`.
- Clone/update **recursively**: `dep`, `src/realmd`, `src/modules/{SD3,Eluna}` and `win` are submodules —
  those four and no others. Never shallow-update a submodule to a non-tip pinned SHA.
- Less-obvious locations: scripting lives in `src/modules/` — Eluna (Lua) and SD3 (C++) are submodules;
  Bots (playerbots) is in-tree. The AuctionHouseBot is in `src/game/AuctionHouseBot/`. The client-data
  baker is **in-tree** at `src/tools/extractor/` (the old `Extractor_projects` submodule is gone).

## Build & test

**C++17** — strict (`-std=c++17`, GNU extensions off); C code is C11. CMake ≥ 3.18; GCC/Clang
(Linux/macOS/BSD) or MSVC ≥ 2015 (Windows). The exact flags CI builds with:

```sh
git clone --recursive https://github.com/mangosone/server.git && cd server
sudo apt-get install -y git cmake ninja-build ccache \
  libssl-dev libbz2-dev default-libmysqlclient-dev libreadline-dev   # Debian/Ubuntu deps
cmake -S . -B ../build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=../build/install \
  -DBUILD_TOOLS=1 -DBUILD_MANGOSD=1 -DBUILD_REALMD=1 \
  -DWITH_TESTS=1 -DWITH_NET_TESTS=0 -DSOAP=1 \
  -DSCRIPT_LIB_ELUNA=1 -DSCRIPT_LIB_SD3=1 -DPLAYERBOTS=1 \
  -DPCH=0
cmake --build ../build --parallel && cmake --install ../build
```

Build **out of source, outside the working tree**. A build directory inside the repo can make a stale
relative `#include "../../dep/foo.h"` resolve by accident, so the build passes and the broken path
survives; building outside is what exposes it.

Every `-D` above is a real option (see the `option(...)` block in `CMakeLists.txt`). CMake **silently ignores
an unknown `-D`** — it only mutters "Manually-specified variables were not used" at the end — so a misspelt
flag looks like it works while configuring nothing. Check the name against `CMakeLists.txt` before adding one.

**`BUILD_TOOLS` no longer gates the baker.** `src/CMakeLists.txt` adds `tools/extractor` unconditionally,
and says why: the fused `.tile` files it produces are what `src/shared/terrain` reads **at runtime**, so it
is not optional the way the old map/vmap extractors were. The unit tests also link its client-data parsers
directly. `BUILD_TOOLS` now only decides whether the extractor **executable** is built; CI passes
`-DBUILD_TOOLS=1`, so extractor changes are compiled by every CI run. Navmesh generation lives inside it
(`src/tools/extractor/nav/`) rather than in a separate Movemap generator.

Two runtime couplings follow, and both bite at deploy time:

- The tile format carries a **version**, and a bake from an older extractor is refused outright — no height,
  no liquid, no collision for that tile. Rebuild the extractor and re-run it **before** serving a branch that
  bumped the version; the old binary writes the old format.
- The extractor links `dataintegrity` and is what **writes** `data.manifest`, the SHA-256 listing that
  `World::VerifyDataIntegrity` checks at start-up (`DataIntegrityCheck`: 0 off, 1 report, 2 refuse). A data
  set with no manifest is reported and accepted, never refused — so "no data.manifest; unverified" at boot
  means the bake predates the manifest, not that anything is wrong.

Windows: use the EasyBuild helper in `win/`. **A PR MUST keep CI green.** CI is GitHub Actions
(`.github/workflows/`): `core_linux_build.yml` builds with **both GCC and Clang**, `core_windows_build.yml`
builds with MSVC, `core_codestyle.yml` checks the style rules below, and `docker_build.yml` builds the images.

The GCC/Clang split matters more than it looks: the two standard libraries do not leak the same headers, so
a missing `#include` can pass on one and fail on the other. **Include what you use** — if a file names
`std::vector`, it includes `<vector>` itself rather than inheriting it from some header four levels up.

## Code style

Source of truth: [`doc/CodingStandard.md`](doc/CodingStandard.md). Non-default rules:

- **4-space indent, never tabs**; ~80-column lines.
- **Allman braces**, and **YOU MUST brace single-statement blocks** — even one-line `if`/`for`/`while`. Do
  not de-brace existing ones.
- **One space before `(`, none inside**: `if (x)`, not `if( x )`.
- Doxygen: `///` above a member, `///<` trailing, `/** ... */` multi-line.

## Repository etiquette

- **Branches**: `type/kebab-description` (`feature/…`, `fix/…`, `docs/…`). Never push straight to `master`.
- **Keep PRs small and single-purpose.** Large multi-commit PRs are hard to review and can exceed the
  `@claude` reviewer's budget (it reacts 👀 but may never post). For a big change, ask `@claude` to review one
  subsystem/file range at a time rather than the whole branch.

## Architecture note

`mangosd` is the **sole authority** over game state. The world runs as a single heartbeat loop on the main
thread (`Master::WorldLoop`); everything else — the console, remote access, SOAP, the freeze watchdog, the
DB delay threads — is auxiliary and must not mutate world state directly. Anything arriving from another
thread is queued to the world thread (see `World::QueueCliCommand`) rather than applied where it landed.

The AuctionHouseBot (`src/game/AuctionHouseBot/`) runs **in-process**.

## Logging

Console output is rendered on a dedicated off-thread writer (`src/shared/Log/ConsoleLogWriter`) so the
world/map-update threads never block on console I/O. Two rules follow:

- **Never write to stdout directly** (`printf`/`fprintf`, progress bars, ad-hoc notices) for console output —
  route it through `Log::ConsoleEmitRaw` so stdout has a single owner and lines can't tear against, or
  overtake, the writer's output.
- **Gate high-volume runtime debug** with `DEBUG_FILTER_LOG(LOG_FILTER_*, …)` (or `DETAIL_`/`BASIC_`),
  reusing an existing `LogFilters` bit where one fits. All filters ship **default-on (suppressed)**; set a
  `LogFilter_*` key to `0` to see a category. **Never filter `outError`/`outErrorDb`** — errors must always show.

Recommended runtime mode: `LogLevel=1` (quiet console) + `LogFileLevel=3` (buffered full file).

## Review focus (for `@claude`)

Prioritise: **(1)** correctness/safety in `src/game/` handlers and anything touching live world or DB state,
especially cross-thread state (the shutdown signal, the CLI command queue, the DB delay threads);
**(2)** coding-standard conformance above; **(3)** build/CI impact — GCC *and* Clang *and* MSVC, including
missing `#include`s that only one standard library exposes; **(4)** DB-migration correctness (use the
`mangosone/database` pattern). Keep feedback concrete and minimal-diff; flag correctness/standard issues, not
style preferences the standard doesn't cover.
