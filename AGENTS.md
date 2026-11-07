# AGENTS.md — Darkspore / ReCap restoration (MetaSerpent)

This file is the working contract for coding agents operating on this repository in VS Code, Codex, Copilot-compatible workflows, or similar assistants. Read it completely **before editing code**, then read `README.md` and `docs/RESTORATION_PLAN.md`. Prefer the repository state and tests over assumptions in this document when they conflict.

## Mission and scope

Restore a **locally playable, reproducible Darkspore 5.3.0.103 experience**, using our GPLv3 fork of the C++ Resurrection Capsule (ReCap) server. Prioritize actual game flow, combat, missions, rewards, and persistence over speculative features or unrelated refactors. The primary current target is the **Steam demo, Darkspore 5.3.0.103**; 5.3.0.127 is a later compatibility target.

This repo contains the **server**, not the complete proprietary game, official launcher, game assets, or a reconstructed native client. The original ReCap v0.7.2 upstream describes local login/registration as implemented and hero editing and level entry as WIP. Our additions extend server mechanisms, but end-to-end gameplay is still **not verified**. Never announce that Darkspore is fully restored without client-side proof.

Repositories:
- Working fork: https://github.com/MetaSerpent/recap_server_cpp
- Upstream server: https://github.com/Resurrection-Capsule/recap_server_cpp
- Upstream project / launcher instructions: https://github.com/Resurrection-Capsule/Resurrection-Capsule
- Source-of-truth progress and remaining verification gates: `docs/RESTORATION_PLAN.md`.

## Project map

- `darkspore_server/source/`: HTTP / SporeNet / Blaze / RakNet handlers, gameplay, Lua bindings, save/game state.
- `darkspore_server/res/data/`: source-controlled, redistributable server resources and configurations.
- `darkspore_server/CMakeLists.txt`: CMake 3.20.4+, C++23 server target and pinned third-party dependencies.
- `tests/`: Python source/contract/regression tests, C++ isolated math/ability tests, headless server smoke test.
- `.github/workflows/build-windows.yml` and other workflows: reproducible platform builds and published artifacts.
- `README.md`, `BUILD.md`, `docs/RESTORATION_PLAN.md`: implementation notes, build details, verification matrix.

A typical developer workstation may have:
- Repo (user-local): `D:\Documentos\.Code\DarkSpore`
- Steam demo: `D:\SteamLibrary\steamapps\common\Darkspore\DarksporeBin`
- Real extracted Lua scripts: `DarksporeBin\Server\data\serverdata\Abilities\`
These paths are **examples**, not portable hardcoded requirements; detect or configure locations rather than embedding them in production code.

## Mandatory workflow for each coding task

1. Inspect `git status --short`, branch, recent commits, relevant source, upstream history, and tests. Preserve unrelated local changes (including `.vscode/settings.json`); never discard or overwrite them without permission.
2. Choose one **small, testable vertical slice** from the priority list below. State a hypothesis, the intended observable behavior and which tests can falsify it.
3. Trace the complete source-to-client flow (packet -> authoritative server state -> Lua/native handler -> response -> persistence). Do not infer protocol bytes, opcode meanings, item layouts or objective rules without evidence.
4. Make minimal, focused changes. Add regression tests that fail for the reported defect and pass after the fix. Prefer deterministic fixtures, isolated test doubles, and full server headless tests before requesting human E2E checks.
5. Run relevant local tests and build if tools are installed; use CI when local toolchains are missing. If tests cannot be run, say precisely which were not run. Never manufacture PASS results.
6. Review diffs for security, resource ownership, serialization compatibility, client packet compatibility, and unintended gameplay changes. Commit a coherent change with a descriptive message to the permitted working branch. Push only when authorized by the user's repository workflow; do not force-push.
7. Update `docs/RESTORATION_PLAN.md` with implemented changes, **what was actually verified**, and the next failing gate. Continue to the next highest-priority self-contained task if time and access permit; do not stop at a plan when code and tests can be written.

Label status accurately: **implemented / unit-tested / CI-built / headless-tested / verified with Darkspore client**. These are distinct.

## Immediate priorities (in order)

### P0 — Real-client blocker and Lua asset pipeline

The first verified local Windows launch, using Darkspore `5.3.0.103` and a server built from commit `f00045a7968aa4da4ca0c67da484e02e001af8f9`, reported:

- `ability_citadel_boss_launch.lua`: Lua parser error at line 109 (`end` expected to close the `if` at line 98, near `else`).
- `template_ability_cone.lua`: parser error at line 139 (`end` expected to close the `if` at line 133, near `elseif`).
- 479 ability scripts loaded; 2 failed.

The affected files are **generated/extracted locally**, normally under `DarksporeBin/Server/data/serverdata/Abilities/`; they are not necessarily repository source files. Investigate unpacking, decompilation, script generation and server loading first. Preserve original bytecode / inputs where legally and technically available; build a minimal, redistributable regression fixture instead of committing original Darkspore assets. A user may have tried ad hoc local repairs. **Do not assume those repairs preserve gameplay semantics.**

For every Lua fix: preserve a backup, verify matching script fingerprint/version, syntax-check with a compatible Lua/LuaJIT parser, run loader tests, and verify the ability actually functions in client. A target of 481 loaded / 0 failed is useful **only for script loading**, not correctness.

### P1 — First reliable mission: verified client flow

Reproduce and instrument: launcher readiness -> local account login -> squad/hero selection -> spaceship -> dungeon entry -> enemy spawns -> three hero ability casts -> valid damage/death -> objective completion -> next screen. On client failures, collect sanitized timestamped logs, server diagnostics, build version and precise reproduction steps. Protect against duplicate init, stale players, malformed packets and client desynchronization. Make server failures diagnosable without flooding logs.

### P2 — Durable victory, loot, and chain progression

Verify client-compatible cash-out packet subtype/size/fields; receipt retries must be idempotent. Confirm awarded parts and DNA persist after restart and rollback cleanly on failed saves. Test medals, rarity selection, multi-planet transition, completion replay and new chain reset. Do not invent XP awards, daily bonuses, drop odds, equipment tables or unknown original rules. Treat provisional rules as such and tie every recovered value to a source or test.

### P3 — Combat/gameplay completeness

Audit selected-hero slot mapping, abilities for all three heroes, targeting/ownership, cooldown reduction and extension, Lua bindings, terrain/trigger interactions, AI damage, mission objectives, and disconnect/reconnect. Expand replay-based tests for packets and deterministic outcomes. Confirm gameplay using the actual 5.3.0.103 client when available.

### P4 — Compatibility, tooling, maintainability

Improve deterministic offline tests, startup/shutdown robustness, reliable Windows installation/deployment, asset pipeline diagnostics, version/build identification, CI coverage and 5.3.0.127 compatibility. Fix proven correctness problems before cosmetic refactors. Do not silently change protocol compatibility for one client to accommodate another.

## Tests and build commands

From repo root with Python 3:

```powershell
py -3 -m unittest discover -s tests -p "test_*.py" -v
```

On Linux or a compatible C++23 toolchain, see `README.md` for isolated C++ tests in `tests/ability_selection_test.cpp` and `tests/cooldown_math_test.cpp`. Avoid assuming GNU-only commands work in Windows PowerShell.

Windows full server build follows `.github/workflows/build-windows.yml` (Visual Studio 2022, CMake, MSYS2 UCRT64, Perl, NASM, pinned dependencies):

```powershell
cmake -S darkspore_server -B darkspore_server/build -G "Visual Studio 17 2022"
cmake --build darkspore_server/build --config Release --parallel 2
```

The configure/build may require the extra environment/tool setup documented in CI; inspect those steps before attributing a dependency failure to our code. The official Windows artifact is called `recap-server-windows-x64`. Its binary is at `dist/bin/recap_server.exe` within the uploaded build tree. Do not overwrite the user's installed server until the build and package contents are verified and the old server is stopped.

For asset-free server integration, use `tests/headless_server_smoke.py` with the built binary and `darkspore_server/res/data`, e.g.:

```powershell
py -3 tests/headless_server_smoke.py --server darkspore_server/dist/bin/recap_server.exe --data darkspore_server/res/data
```

Use the correct path to the executable **after installing the build**; the command will fail if there is no installed `dist/bin` yet. The smoke test is designed to use temporary storage and loopback ports; do not point it at the user's real account storage.

## Evidence and acceptance criteria

At minimum each completed fix should provide: file/function changed, observed original failure, repro or regression test, actual test output, remaining limitations, and new `docs/RESTORATION_PLAN.md` entry.

The **first playable mission** gate is complete only after the real client logs in, selects a hero, enters a level, displays correctly routed combat, registers kills/objectives, reaches completion and returns to the intended next screen without crash or state corruption. A full playable loop further requires verified rewards and restart persistence.

Unit tests, simulated packets, green CI and correct `--version` output do **not** by themselves satisfy these gates. The server's upstream version remains `0.7.2` unless intentionally changed, so identify our builds by Git commit SHA / artifact / binary hash, not that version string alone.

## Safety, reproducibility and source boundaries

- Never check in or upload EA game executables, proprietary packed/extracted assets, player accounts, personal credentials, real email addresses, tokens, or unsanitized client packet captures.
- Respect upstream GPLv3 notices and preserve attribution. Keep independent fixtures/code distinct from original proprietary assets.
- Test on **copies** of installed game data. Back up any file before changing a user's `DarksporeBin` tree; do not edit the live game folder while ReCap/game processes are running.
- Account registration and transport use outdated protocols. Use throwaway local credentials and sanitized diagnostics.
- Never remove tests to make CI green, silently downgrade assertions, bypass errors with unconditional success responses, or fake mission/loot completion.
- Avoid destructive git commands (`reset --hard`, `clean -fdx`, force push) unless explicitly authorized. Do not mutate unrelated repos or private files.
- When a step requires human client interaction, minimize it: package automated self-checks first and ask only for the specific screen, action or trace needed.

## Start here on a new coding-agent session

1. Read this file, `README.md`, `docs/RESTORATION_PLAN.md`, `.github/workflows/build-windows.yml`.
2. Examine working tree, latest commits, current CI, and current issues.
3. Begin with the two failed Lua scripts and asset-loading pipeline, or the highest newly evidenced real-client blocker.
4. Implement and run the maximum automated test coverage available.
5. Commit coherent, verified changes; update the verification matrix; proceed to the next blocker.

**End goal:** robust offline Darkspore gameplay on the original compatible client, with tests and honest verification—not merely a booting launcher.
