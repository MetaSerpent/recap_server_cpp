# Restoration milestones and test gates

This fork extends the GPLv3 **Resurrection Capsule server**, not the
proprietary Darkspore game client. It cannot run without a compatible
Darkspore installation and launcher. Do not commit EA executable or assets.

## Completed code changes

- Guard invalid hero indexes and unavailable squad characters.
- Route character abilities from the selected hero, not always hero zero.
- Reject invalid ability indexes before narrowing client-provided numbers.
- Handle missing objectives and startup arguments without out-of-range access.
- Ignore stale/disconnected clients, malformed RakNet timestamps, invalid trigger iterators and actions targeting non-owned actors.
- Add standalone C++ slot-selection tests and a lightweight CI workflow.
- Implement Lua-exposed cooldown scaling with deterministic remaining-time math and saturation tests.
- Correct active cooldown extension/reduction to operate on remaining time and saturate uint32 client durations.
- Join the data loading worker before server shutdown and close every instantiated service.
- Normalize an empty STORAGE_PATH to the existing storage/ default.
- Initialize each dungeon entry only once per client instead of repeating spawn and hero swaps on every debug ping.
- Record actual enemy spawns and unique deaths for the DefeatAllMonsters mission objective; match the bundled Lua 95/75/50% medal thresholds. Actual client behavior remains to be verified.
- Count distinct InteractWithObelisk actions, broadcast objective and interactable changes, and score from original Lua thresholds (3/2/1). Other scripted interactions still need reconstruction.
- Track successful player-to-enemy damage per player and score DoDamageOften from Lua difficulty-scaled hit counts; score HugeDamage from Lua hit-count thresholds with provisional 1x health scaling until that multiplier is implemented.
- Build objective snapshots per player for init/update/completion packets, so multiplayer damage counters cannot bleed across clients. Count actual medals from the same player snapshot.
- Stop granting the unverified flat 50-DNA cash-out reward. Equipment loot remains saved on pickup; DNA drops and mission bonus rules still require reconstruction.
- Credit the DNA amount already stored in server-owned loot packets when picked up; validate the float-to-integer conversion, saturate account balances and atomically persist DNA alongside any attached equipment. Failed saves roll back without consuming the loot. Actual spawning/drop chances and difficulty scaling are still pending.
- Bind the missing `nGameObject.DNAPickup` callback used by bundled `DNA_Follow.lua`, validating the live player-controlled hero and server-created DNA noun before consuming the resource. The rest of the original Lua homing/trigger chain and real-client behavior need verification.
- Restore the documented build-103 Cash Out packet fields/offsets: integer DNA, starting/final experience, medal counts, upper/lower rarity boundaries, daily bonus flags and four four-item rows. Medal-weighted rarity chances use the independently recovered (bronze + 3 silver + 6 gold)/planets policy; Purified begins at index 17. Item records and XP remain zero until actual reward generation and settlement exist, so this must not be interpreted as a completed rewards feature. Source: https://github.com/darkspinnet/darkspin/blob/main/server/zone/result/reward.go and server/raknet/application.go.
- Fix the packet subcommand for the 712-byte cash-out result: client build 103 expects subtype 0, not 1. Resolve Lua object/player/trigger handles by their actual userdata or numeric ID types to prevent invalid conversions in original Lua scripts.
- Handle the native cash-out receipt resend request (chain player command 4) without mutating rewards or account files; ignore premature dungeon-start status (0x08) until the client is truly in PreDungeon. Source for command 4: https://github.com/darkspinnet/darkspin/blob/main/server/raknet/gameplay.go.
- Commit bounded (up to four) cash-out item awards with a random per-run receipt token, persisted with all parts and DNA in one account XML save. Receipt retries are idempotent and failed saves roll back without exposing phantom item cards. Reward counts, level progression, roll and rarity-band mathematics follow the recovered build-103 implementation; the rigblock ID fallback samples from the supported local ID range, but is provisional until original class/element tables are decoded. XP and daily bonuses are not awarded without source tuning.
- Correct empty prefix/suffix asset hashes to zero (not the FNV hash of an empty string) and zero-initialize the entire 712-byte cash-out payload before writing fields; unused item/padding data must never contain uninitialized bytes.
- Reset the old ObjectManager, Lua VM, markers, enemies and objective counters when an eligible player requests the next planet after completing a dungeon; accumulate per-player medal totals across planets and prevent duplicate completion tally. The server currently selects the next campaign level sequentially; actual vote-selection packets need verification. Refuse to reset while another player is in a live or uncommitted state. A new deployment from Spaceship after the previous chain is completed resets chain progression and medals instead of reusing the previous run. Full end-of-mission reward formulas and loot chances still need reconstruction.
- Implement header-only `Lua51Transpiler` (`darkspore_server/source/game/lua51_transpiler.h`), a one-to-one translation of Lua 5.1 bytecode (`\x1bLua\x51`, Darkspore's float32 variant) into LuaJIT source using flat locals and `goto`. It replaces unluac for every script, including the two whose unluac output fails to parse (`ability_citadel_boss_launch.lua`, `template_ability_cone.lua`). See "Lua asset pipeline verification" below for what was measured.
- Integrate automated transpilation and directory merging into `Installer::LoadDarksporeData` (`installer.cpp`) for `lua`, `Abilities`, `Modifiers`, and `behaviors`.
- Fix critical coroutine lifecycle bug in `LuaThread` (`lua.h` / `lua.cpp`): clear Lua coroutine stack with `lua_settop(L, 0)` on normal exit and errors, returning threads cleanly to `mThreadPool` instead of remaining dead/stuck in `mYieldedThreads`. Fix `Ability::Tick` and `nThread.Create` to check thread yield status so subsequent casts are not permanently blocked.
- Wire active ability casting context (`mActiveAgent`, `mActiveTarget`, `mActiveCursorPosition`, `mActiveRank`, `PayCooldownAndMana`) into `Game::Ability` and ensure cooldown and mana are paid upon cast completion.
- Reconstruct complete native engine script bindings in `luafunctions.cpp` across namespaces required by the 1,029 transpiled Darkspore scripts: `nBit`, `nMathUtil` (vector/arc/line math), `nUtil` (SPID, ToGUID, GetAsset hash resolution), `nGameObject` (position, facing, velocity, orientation, stats, targeting, animations, combat, damage, DNA pickup), `nObjectManager` (vector and coordinate object/trigger creation, spatial queries), `nThread` / `nThreadData` (coroutines, sleep, projectile tracing), `nAttribute`, `nEvent`, `nModifier`, `nPlayer`, `nPhysics`, `nGameSimulator`, and `nGameDirector`.
- Route applied damage in `LuaFunction::Object::TakeDamage` to `Game::Instance::OnDamageDealt` so mission objectives (DefeatAllMonsters, DoDamageOften, HugeDamage) receive credit when combat is driven by Lua abilities.

## Verification matrix

| Gate | Definition of done | Status |
| --- | --- | --- |
| P0-A | Standalone C++ ability routing tests | Local test passed; CI added |
| P0-B | Windows and Ubuntu server build | Upstream build workflows; MSVC 2022 verified |
| P0-Lua | Ability & script asset loading (481 scripts) | Transpiler verified offline against all 1,029 original scripts (see below); not yet loaded by a server build with this pipeline. The earlier "481 loaded, 0 failed" came from hand-repaired unluac output on the `f00045a` server, not from this pipeline |
| P0-C | Local login, hero selection, stage load | Login/account load verified on `f00045a`; client crash at 0x006ec495 (null read at +8) on that build, not yet retested with the Lua pipeline |
| P0-D | All three heroes cast correct unlocked abilities | Implemented & unit-tested; needs client trace |
| P0-E | Finish encounter and complete stage | Implemented & unit-tested; needs client trace |
| P1-A | Loot, XP and save/load persist after restart | Implemented & unit-tested; needs client verification |
| P1-B | Chain progression, cashout, replay | Implemented & unit-tested; needs client verification |

A passing isolated test or C++ server build does not prove a playable game.

The Windows/Ubuntu build workflows now smoke-test the installed server's
`--version`, `--help`, and invalid `--darkspore-path` behavior before uploading
artifacts. The headless smoke test also restarts the real server on the same
temporary user directory, checks both accounts remain loadable with their
original IDs, and rejects duplicate registration without changing files.
These checks require no proprietary game assets.

## Reverse-engineering priorities

1. Capture sanitized RakNet packets for each ability and verify cooldown,
   confirmation and error response formats against the original client.
2. The squad-ability handler now executes the Lua ability instead of
   faking a boss completion event. Verify the acknowledgement, cooldown and
   rank semantics against real client captures before calling it complete.
3. Verify damage, target deaths, AI scheduling, objective transitions.
4. Reconstruct loot drops, inventory updates, persistence and mission rewards.
5. Add version-specific client compatibility tests using independently held
   copies of the game; avoid redistributing proprietary game content.
6. Record reliable Windows/Wine install-to-first-mission reproduction steps.

Upstream launch instructions:
https://github.com/Resurrection-Capsule/Resurrection-Capsule/blob/master/HOW_TO_PLAY.md

For testing, use throwaway local account credentials because Darkspore uses
outdated connection protocols. Sanitize logs before sharing.

## Headless shutdown regression

A no-client Linux smoke test of the compiled ReCap binary with bundled
`data/` and a deliberately absent game installation reproduced a SIGSEGV in
`RoomCategory::~RoomCategory` during `SporeNet::Instance` teardown. The
room/view/category maps own their objects: destructors must not call back into
`RoomManager::Remove*` while those maps are being destroyed. Deletion must
be initiated explicitly by the manager. Verify by rerunning the headless
server with local ports, checking all exposed listeners, and stopping with
SIGTERM; no game assets are necessary for this shutdown test.


## M0: Build restoration after Lua pipeline integration (2026-11-08)

The first Lua integration build on commit `475b4e4` failed on both Windows and
Ubuntu. This M0 patch corrects the compiler-reported `string_view`/optional
mismatches, invalid player/cooldown calls, sol2 table getter ambiguity, private
loot API usage, nonexistent objective completion API, and incomplete Lua type
usage. Cooldown operations now target the controlled Object, and Lua's
completion callback uses the guarded BeamOut path. The gameplay behavior of
these callbacks remains unverified with the retail client.

The server prints `MetaSerpent <git SHA>` in `--version` and startup logs
to distinguish builds. The SHA is `unknown` for source archives without Git
metadata. The next gates are: successful Linux and Windows C++ CI builds,
successful headless regression tests, and verified downloadable artifacts from
the same commit. Follow-on real-client work is M1/M2, not proof of M0.

Windows M0 compile follow-up: MSVC Release failed with C1128 (too many COFF
sections in the expanded Lua bindings translation unit). Enable `/bigobj` in
**all** MSVC configurations; it was previously set only for Debug. This change
requires another Windows CI compilation before M0 is accepted.

**M0 result:** accepted at `ba77786`. Windows, Ubuntu and guardrail CI passed;
the Windows job ran the headless smoke test against the built binary, which
reported `0.7.2 (MetaSerpent ba7778688577)`, and uploaded `recap-server-windows-x64`.

## Lua asset pipeline verification (2026-11-09)

**Findings on `ba77786`.** The transpiler was never run on real scripts before
this entry. Measured against the 1,029 Lua 5.1 bytecode files in a local copy
of the Steam demo's `ServerData.package` (build 5.3.0.103; analysis output kept
outside the repository):

- Every file has Darkspore's header (`int`/`size_t`/instruction = 4 bytes,
  `lua_Number` = **float32**). Stock Lua cannot load them.
- Multiple results were rebuilt with `{f()}` and `unpack(t)`, which drops
  values after a `nil` (`return 1, nil, nil, 4` arrived as `1`). This affects
  621 call sites in the corpus and changed observable behavior in real scripts.
- `OP_CLOSE` was silently ignored. The corpus contains no `OP_CLOSE` (3,558
  closures, 395 register captures, none detached), so this never fired on
  build-103 data; the translation is still unsound for such input.
- Reconstructed copies of `ability_citadel_boss_launch` and
  `template_ability_cone` were embedded in the server source and written over
  the user's scripts on load failure. Their constants match the originals,
  but they are proprietary script content, which AGENTS.md forbids committing,
  and the generic translation already handles both files.

**Changes.** Multiple results and the 5.1 `arg` table now carry an explicit
count (`__lua51_pack`/`__lua51_unpack`). Chunks with `OP_CLOSE` are refused
(the installer falls back to unluac and logs it). The embedded scripts and the
load-time overwrite are removed: the loader now reports the failure and asks
for regeneration from `ServerData.package`. In-place conversion writes a
temporary file and renames it, and source files are no longer rewritten on
every start. The installer prints how many scripts were transpiled vs decompiled.

**Verified (offline, not with the server or client):**

- Unit/CI: `tests/lua51_transpiler_test.cpp` with redistributable fixtures in
  `tests/fixtures/lua51` (independent code compiled with a float32 Lua 5.1.5
  build that reproduces Darkspore's header). CI runs the transpiled fixture on
  LuaJIT and requires output identical to the original bytecode on float32
  Lua 5.1. The old transpiler fails it (8 of 21 lines differ). Truncated
  input is rejected for every prefix length; `OP_CLOSE` input is refused.
- Real corpus (local, needs the game files): all 1,029 scripts transpile and
  parse with LuaJIT at the pinned commit `ea9eb7e`. A differential harness ran
  each original bytecode file on float32 Lua 5.1.5 and its translation on
  LuaJIT under identical recording stubs. It compared top-level execution,
  every engine call with arguments, resulting globals, and a call of every
  function the script defines (183,876 trace lines). **1,027/1,029 traces are
  identical** (the old transpiler: 1,026). The two differences are VM
  properties, not translation errors: LuaJIT's `math.rad` honors `__mul` on a
  stub table (identical once `math.rad` behaves the same in both), and
  float32 vs double rounding (`0.40938` vs `0.40937`).

**Limitations.** The server runs scripts on LuaJIT, so numbers are doubles
instead of the original float32, and `pairs()` order is VM-specific. Stubs
cannot follow branches that depend on real engine values. Ability names are
still found by scanning source text (`ExtractRegisteredAbilityNames`). The
removed script copies remain in Git history (`475b4e4`..`ba77786`); removing
them from history needs a rewrite and force-push, which is the maintainers'
decision.

**Regenerating an existing install** (MergeDirectories never overwrites, so
old unluac output stays until removed): stop the game and server; back up
`Server/data/serverdata/{lua,Abilities,Modifiers,behaviors}` outside `data/`;
delete those four folders; start the server with `--darkspore-path`. Because
`version_bin.txt` exists and `Modifiers` is missing, it re-extracts
`ServerData.package` and logs `Lua scripts converted from bytecode: N,
decompiled with unluac: M`. Expect `M = 0` for build 103.

**Next failing gates:** load the regenerated scripts with a server built from
this commit (expect 481 loaded / 0 failed in `Abilities`); then retest the
client crash at `0x006ec495`, last seen on `f00045a` before any Lua change,
and capture the server console log for the same session.

### Server run on a copy of the install (2026-11-09, artifact `164cd1a`)

Ran the CI artifact (`0.7.2 (MetaSerpent 164cd1a87723)`) from a copy of the
demo's `Server` folder without the four script folders, with stdout
redirected to a file (possible since `164cd1a`). Results:
`Lua scripts converted from bytecode: 1029, decompiled with unluac: 0` and
**`Loading ability scripts... done. (loaded: 481, failed: 0)`**. This gate is
met for loading; it only proves the files parse.

The same run exposed a pre-existing runtime blocker: `lua/global.lua` failed
on `require("0x3681d755!GlobalDefinitions.lua")`. Game scripts name modules
`Group!name.lua` (`Lua!` 222×, `Abilities!` 305×, `Modifiers!` 183×,
`Modifers!` 3× (sic), `0x3681d755!` 6×, `Objectives!` 1×), and the server's
`require` shim only stripped path prefixes, so every such `require` failed.
The live install shows the same forms (only `global.lua` was hand-edited),
so abilities requiring helpers such as `Lua!TargetUtils.lua` failed when
first executed, independently of the transpiler. The shim now strips any
`Group!` prefix, falls back to the hashed file name (`0x` + FNV-1 of the
lower-case name, used for scripts absent from `reg_file.txt`, e.g.
`AttributeUtils`, `NavigationUtils`, `modifier_shocked`), and no longer
masks errors raised inside a module as "module not found". Base names are
unique across the four folders. Tested by `tests/require_shim_test.lua`
(runs the shim text from `lua.cpp` on LuaJIT; the old shim fails it).

Of 122 distinct modules required by build-103 scripts, 9 do not exist in
`ServerData.package` under either name and remain unresolvable, as they would
be in the original engine: `template_ability_Firebomb`,
`template_ability_ThornNova`, `modifier_soulravager_soulbag`,
`fireravager_energyvulnerability`, `modifier_chain_ability_counter`,
`modifier_energysentinel_attackDebuff`, `modifier_nomad_pets_pet_tracker`,
`modifier_tcshieldedsentinel_support_snare`,
`template_modifier_BinarySentinel_Knockback` (abilities requiring them fail
when executed; check whether any demo hero uses them).

The Windows installer now runs its tools as `.\tool.exe`; bare names failed
when `cmd.exe` does not search the current directory
(`NoDefaultCurrentDirectoryInExePath`), which aborts the installer.

### Client crash 0x006ec495 (build 103), static analysis

From the exception report of the `f00045a` session (local only, not
committed): `ACCESS_VIOLATION reading 0x00000008` at `0x006ec495`,
`mov eax, [eax+8]` where `eax = [arg2+0x20] = NULL`. The function
(`0x006ec480`) is a queued job callback invoked through a generic job runner
(return address `0x007d0036`, `call eax` on a job record whose `+8` holds
`0x006ec480`). It then walks 152-byte records at `+0x98/+0x9c` of the loaded
object. The surrounding code (`0x6e9000`–`0x6ef000`) allocates under the
`SP_Skinner` and `SP_Skinner/MeshAORender` tags: it is the client's creature
skin-mesh builder. Working hypothesis (unverified): a creature/hero reached
skinning without its model or rig data, e.g. because creature data sent by
the server referenced parts or assets the client could not resolve. To
confirm, reproduce with the server log captured and note the screen where it
crashes (squad selection, editor, spaceship or level load).

### First real-client session on `378acc3` (2026-11-09)

Server run from a copy of the install, client started with
`RecapLauncher.exe --no-server` (by default the launcher starts the old
`Server/recap_server.exe`). Login, persona, rooms and game creation worked;
the client connected to the RakNet game server and received
`GamePrepareForStart`. The player selected campaign **7-1** and the loading
screen never finished.

Cause: the client sent `SelectedDifficulty = 25`. The built-in `levelNames`
table (written for 5.3.0.127) covers only indices 0-24, so the bounds check in
`SetLevelByIndex` silently kept index 0, the tutorial
`Darkspore_Tutorial_cryos_1_v2`. That level does not exist in the 5.3.0.103
demo (`Level: Could not find ...`), and its hash was sent to the client.

Fix (implemented, unit-tested; needs the client rerun): campaign order now
comes from the installed `chainlevels/ChainLevels.ChainLevels.xml`, which in
the demo defines 72 levels (18 tiers × 4; index 25 = `cryos_1`; entries 21/22
are `scaldron_2`/`scaldron_1`, swapped relative to the built-in table). The
built-in table remains the fallback when the data is missing. The tutorial is
`_v2` only if that level exists, otherwise `Darkspore_Tutorial_cryos_1`.
Out-of-range selections are logged instead of falling back silently.
Next-planet deployment uses the data's level count.

Also from this session: Blaze packet dumps printed the login password, e-mail
and session tokens. `Blaze::Log` now redacts `PASS`, `MAIL`, `TOKN`, `AUTH`,
`SKEY`, `SESS` and `KEY` string values (tested by
`tests/blaze_log_redaction_test.cpp`; the old code leaks all five secrets).
`--timestamps` also enables per-packet QoS logging, which floods the log
(~20k lines in minutes); run without it when collecting session logs.
