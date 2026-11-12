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

### Second real-client session on `0b3da19` (2026-11-09)

Campaign data loaded (`ChainLevels: 72 campaign levels from game data`).
The player chose 1-1 (`SelectedDifficulty = 1`, `zelems_1`): the level loaded
on the server with no errors and the client got past the previous blocker
(`GamePrepareForStart` → `GameStart`, `DirectorState`, `QuickGameMsgs`,
`ObjectivesInitForLevel`, 346 `ObjectCreate`, then per-agent
`CombatantDataUpdate`/`AttributeDataUpdate`/`AgentBlackboardUpdate`/
`ObjectTeleport`). The client crashed before sending any action.

Crash (local exception report, not committed): `ACCESS_VIOLATION reading
0x00000034` at `0x009c8e75`, `cmp [esi+0x34], 0` with `esi = NULL`, in a
function (`0x009c8e70`) of the client's asset catalog code (string refs
`AssetCatalog/AssetGlobals`, `catalog_%d.bin`). The stack shows argument
`0` and return address `0x0041fa02`, inside `0x0041f8a0`, which passes a
resource id from `[obj+0xcc]` to a catalog lookup (`0x009eca10`) during HUD
setup (neighbouring code: `HUD_ObjectivesPopup.swf`, `Helix_PIP.swf`,
`HUD_PlayerDeck.swf`). Working hypothesis (unverified): a resource id sent
by the server during world setup (objective, noun/creature, or HUD asset) is
missing from the 5.3.0.103 client's catalog, e.g. data that only exists in
5.3.0.127. Next: identify the object type behind `[obj+0xcc]` and diff the
ids the server sends at mission start (objectives, nouns, creature parts)
against the demo's own `AssetData_Binary`/`assets_catalog`.

### "Undefined" heroes and the HUD crash: squads held template ids

The player reported that the mission loading screen showed the enemies but
three blank hero slots named "undefined". The account's squads contained
`[1614078369, 0, 0]`, `[2530835539, 0, 0]`, `[3618328019, 0, 0]`: the
creature *template* ids of Arborus Gamma, Zrin Beta and Vex Alpha, while the
owned creatures have ids 1..N. Registration (upstream `c73300b`/`101954f`)
called `SetCreatureId(0, templates[slot]->GetNoun())`, so `Player::SetSquad`
found no creature and sent noun 0. This matches the client crash in the
asset catalog during HUD setup (a lookup of an asset the hero does not have).

Fix (implemented, unit-tested; needs the client rerun): registration assigns
the ids returned by `UnlockCreature`, three creatures per squad;
`User::Load` repairs squads stored in the old layout (template noun in slot
0, other slots empty → default 3-creature squad; other template nouns map to
an owned creature of that template); `Player::SetSquad` logs squad members
the account does not own. Logic in `sporenet/squad_repair.h`, tested by
`tests/squad_repair_test.cpp`.

### Third real-client session on `dec0a4b` (2026-11-09): first playable mission start

Squads were repaired on login, the player deployed with three heroes in 1-1,
moved around and swapped heroes (14 `PlayerCharacterDeploy`); 59
`UseCharacterAbility` commands arrived. No crash; the player quit normally.
No damage, kills or objective updates happened. Two combat blockers:

1. **Every cast was dropped:** all 100 entries of the shipped
   `creature_templates.json` have ability ids 0, so `Player::GetAbilityId`
   returned 0 and the handler silently ignored the command. The installed game
   data has the abilities: hero noun (`PC_LF_Tank_v2.Noun` = Arborus Gamma,
   id = the template id) → `playerClassData` → `PlayerClass`. Its fields, by
   name pattern, are `<Hero>Basic`, `<Hero>Active`, `<Hero>Random`/variant,
   `<Hero>Support` (the field recap_parser labels `basicAbility` is a numeric
   id that names no script). Template slots now fall back to those fields
   (basic → Basic, special_1 → Active, special_2/squad → Support, random →
   variant). Provisional mapping until casts are verified in the client.
   In the demo data 68 of 100 `<Hero>Basic` scripts do not exist (e.g.
   `PlasmaSentinelBasic`, `TimeRavagerBasic`); those basic attacks remain
   unavailable and are logged.
2. **No game ability could register:** scripts call
   `nAbility.RegisterAbility("Name", table)` but the binding took the table
   first (upstream `Ability.Register(table)`), raising "attempt to index a
   string value" (seen for `ability_firstaggro_*`). The binding now accepts
   both forms (`game/ability_registration.h`), tested in a real sol2 3.3.1 +
   LuaJIT state (`tests/ability_registration_test.cpp`).

Casts that still fail now log once per run why (no ability id for a hero slot,
or no registered script for an ability id).

### Fourth client session on `05ef33e` (2026-11-10): abilities execute

Hero casts now resolve and run Lua: the basic attack executed
`template_ability_melee`. Failures observed, fixed in the next commit:

- Every melee hit called `nAbility.DebugDrawDamageArc` → unbound
  `nDebug.IsAbilityDebugEnabled` → Lua error. Added server-side stubs whose
  semantics are unambiguous (debug drawing disabled, `nClient` reticles no-op,
  `GetMyPlayerObjectId` 0, `nScenarioManager.GetIsClient` false).
- After any script error the pooled Lua thread stayed in the error state and
  every later cast failed with "cannot resume non-suspended coroutine".
  `ReturnThreadToPool` now replaces errored threads
  (`tests/lua_thread_recovery_test.cpp` reproduces both on LuaJIT).
- `modifier_timeravager_basic` (and 6 others) use `AttributeUtils` globals
  without requiring the module; 1 script does the same with
  `NavigationUtils`. Both are preloaded in each game Lua state.

Static binding audit (members used by the 1,029 scripts but neither bound by
the server nor defined by scripts): 218 members / 856 call sites overall; for
the session's heroes' ability closure 27 / 36, mostly `nLocomotion` path
queries (`GetClosestSpawnPosition`, `IsPositionReachable`,
`FindBallRollPosition`, `FindGoodMeleePosition`, `DistanceToObstacle`),
`nTimeManager.GetSimTimeDt`, and engine enum values (`nAttributeType.AoERadius`,
`nModifierPriorities.*`, `nDebuffDescriptors.*`, `nAbilityPrimaryStat.*`,
`nAllowsMovement.Always`, `nProjectileFlags.kHoming`). Enum values must come
from the original engine (e.g. the client's own Lua registration), not be
invented.

### Fifth client session on `e1bf517` (2026-11-10): server crash on melee hit

Abilities ran without Lua errors and three basic attacks completed the server
pipeline (`=== End UseCharacterAbility ===`), then the server crashed
(`0xc0000005`, Windows Error Reporting dump, not committed). Analysis of the
dump without symbols: faulting instruction in `Attributes::GetValue`, called
from `Object::TakeDamage` with `this->mAttributes` holding garbage
(`0x8000046f8a135d34`), called from the `nGameObject.TakeDamage` binding
inside a resumed ability coroutine (`LuaThread::resume`).

Root cause: the binding's argument order did not match the game. All 1,029
scripts call `TakeDamage(snapshot, target, damage, damageType, damageSource,
coefficient, descriptors[, multiplier[, dirX, dirY, dirZ]])` (first argument
traced to `Get*AttributeSnapshot` in ~104 of ~124 calls, `kInvalidGUID`
otherwise; 128 calls use 7 arguments, 10 use 11) and
`HealDamage(snapshot, target, heal, coefficient[, descriptors[, critical[,
canResurrect]]])`. The bindings expected `(object, attacker, snapshot, ...)`,
so a number reached a `sol::table` parameter that is then read with
`raw_get`: LuaJIT reinterprets the number as a table pointer (undefined
behaviour, memory corruption). The exact path from that read to the garbage
`this` cannot be proven from a symbol-less dump; the mismatch itself is
proven. Note: lines 86-1618 of `lua.cpp` (a duplicate `LuaFunction` set) are
compiled out (`#ifdef UNUSED_STUFF`).

Fix (implemented, unit-tested in sol2 + LuaJIT, needs client rerun):
`game/combat_call.h` parses both calls in the game order with every argument
type-checked; the attacker is the snapshot's owner object. Release builds now
produce and ship `recap_server.pdb` so future dumps resolve to source lines.

### Sixth client session on `e81710e` (2026-11-10): first damage dealt

No server crash and no client crash; the player quit normally. The server
sent 23 `CombatEvent` packets and one death (`SetObjectGfxState` "dead" plus
`SetAnimationState`); no objective update (1 kill of ~340 rounds to 0 %).
What the player saw on screen is not yet confirmed. One Lua error, three
times: `ability_entangling_rush.lua:209: sol: no matching function call`.

Cause: `nObjectManager.GetObjectsInRadius(x, y, z, radius, kType_Creature)`.
Scripts pass the type filter as a single number (60 calls) or a table of types
(48 calls) or omit it; the coordinate bindings only accepted a table. Fixed
with `game/lua_type_filter.h` for `GetObjectsInRadius`, both sorted variants
and `GetObjectsAlongLine`.

**sol2 safety in Release (root cause class of the session-5 crash).** sol2
3.3.1 enables its checks only in debug builds. CI ships Release, where a bound
function declared with `sol::table`/`sol::function` accepts any Lua value and
reads memory through it; measured with the server's flags: `TakesTable(5)`
runs its body with a bogus table. `-DSOL_SAFE_FUNCTION=1` in CMake is not a
macro this sol2 version defines and never had an effect. Now
`-DSOL_SAFE_FUNCTION_CALLS=1 -DSOL_SAFE_USERTYPE=1`: argument and `self` type
mismatches raise Lua errors (logged, the cast is dropped, later casts still
run). Behaviour change to expect: a script passing `nil` for a number now
errors instead of silently becoming 0. `SOL_SAFE_NUMERICS` stays off.
`tests/lua_type_filter_test.cpp` is built like Release and fails with the old
defines on exactly the number-as-table case.

**Correction to the binding audit.** The earlier audit missed definitions made
with constant bracket keys (`t["Name"] = value`), which is how
`GlobalDefinitions.lua` builds its enum tables. Engine enums such as
`nAttributeType`, `nModifierPriorities`, `nDebuffDescriptors` are defined by
the game's own scripts with the engine's values; nothing needs inventing.
Corrected totals: 134 members / 319 call sites not provided. Engine functions
among them: `nBehaviorTree.GetMyObjectID` (61), `nAgent.GetBestTarget` (40),
`nLocomotion.FindBallRollPosition` (15), `GetClosestSpawnPosition` (11),
`IsPositionReachable` (10), `DistanceToObstacle` (7), `GetTerrainHeight` (5),
`FindGoodMeleePosition` (3), `nTimeManager.GetSimTimeDt` (8),
`nAgent.HasTargetsOnAggroList`/`GetTargetsOnAggroList` (11), `nTuning.*`,
`nAffix.*`. The rest are optional script fields that are nil by design.

**Attribute numbering differs from build 103 (not fixed yet).** The game's
`nAttributeType` (115 entries, executed with LuaJIT) and the server's
`AttributeType` enum agree for 0-73 and differ for 39 attributes from 74 on:
the game has `ImmuneToShocked` 74 (unnamed in Lua), `ImmuneToSleep` 75,
`ImmuneToTaunted` 76, `ImmuneToTerrified` 77, ...; the server has
`ImmuneToSleep` 74 and moves `ImmuneToShocked`/`ImmuneToTaunted` after
`ClientCount`. The attribute-name table in the 5.3.0.103 `Darkspore.exe`
(read in order: ..., Surefooted, ImmuneToStunned, ImmuneToShocked,
ImmuneToSleep, ImmuneToTaunted, ImmuneToTerrified, ...) matches the game
scripts. Effect: scripts and `AttributeDataUpdate` packets address the wrong
attribute for indices 74-111 (immunities, DoT/aggro modifiers); 0-73 (health,
mana, damage, crit, speed, `AoERadius` 62) are unaffected. The server enum may
be correct for 5.3.0.127, so this needs a version-aware fix with its own tests
rather than a reorder. Next slice after combat is confirmed on screen.

### Seventh client session on `a5d8bdb` (2026-11-11): what the player sees

Player report: heroes "cannot use their abilities"; occasional hits land with
no visual feedback; basic attacks and abilities feel blocked. Server log for
the same session: 44 casts, 15 `CombatEvent`, **0** `SetAnimationState`,
2 `ActionCommandResponse`, 29 casts aborted at
`template_ability_melee.lua:141` and 3 at `template_ability_instantcast.lua:172`.

Sources used below: the user's 5.3.0.103 scripts, and darkspin
(https://github.com/darkspinnet/darkspin, MIT), an independent server with
build-103 packet encoders (`server/raknet/application.go`,
`server/sim/raknet103/encoder.go`, `server/zone/action/raknet103/response.go`).

Fixed in this commit (implemented, unit/contract-tested; **client effect not
verified yet**):

- **Calls rejected by the new argument checks.** `tools/lua_audit/arity_audit.py`
  compares script calls with binding signatures: 10 functions / 303 call sites
  passed fewer arguments than required and 6 parameters had the wrong type.
  `nAbility.PlayAnimationSequence()` (no arguments), `nThreadData` slots keyed
  by number (`SetGUID(0, id)`), `CreatePrivateTable()` per script thread,
  `nPlayer.*` without a player, `CreateObject(noun)`. Forms whose meaning is
  unknown (`RemoveCooldownTime(x)`, `ScaleCooldownTime(obj, scale)`,
  `SetOrientation(obj, x)`; 5 call sites) are accepted and logged once.
- **Attribute modifiers.** Scripts use `handle = AddAttributeModifier(obj,
  attr, value)` and `RemoveAttributeModifier(handle)` (79 sites). The binding
  returned nothing, so buffs/debuffs were never removed. Now a handle registry.
- **Animations never played.** `nAbility.PreloadAnimation(name, ...)` returned
  nil, so every `animationstate` was nil; it now returns the animation id
  (resource hash of the name, as darkspin sends). `GetAnimationSequenceIndex`
  returned a constant 1; the game's `GetTimeToReleaseLua` reads
  `animationSequence[index + 1]`, so it is zero-based and now cycles per actor
  (`game/animation_sequence.h`). `PlayAnimationSequence()` plays the current
  entry. `SetAnimationState`'s last field is 0, not a copy of the state.
- **Hits without feedback.** `CombatEvent` wrote its flags as 32 bits; build 103
  reflects 16, so delta, target and source were shifted by two bytes. Events
  now also carry the attacker and the flags darkspin uses (0x0001 hit, 0x0004
  killing blow, 0x0008 critical).

**Next slice: action responses (the "blocked" feeling).** Upstream disabled
`ActionCommandResponse` for hero abilities in `08d2633` ("crashes the game"),
so the client never learns that a cast was accepted or refused. darkspin's
build-103 layout (56 bytes): `[0]` sync stamp = byte 1 of the client's action
command (`ActionCommandCommonData::unk[0]`; upstream sent 0), `[1]` type
(1 accepted, 2 rejected, 4 released, 8 pursuit, 16 clear user data), `[4]`
ability hash (object id for a rejection), `[8]` ability index, `[16]/[24]/[32]`
start / commit / end in server-clock milliseconds, `[40]` global cooldown,
`[52]` user data `0xFFFFFFFF`. A basic attack sends Accepted at cast time with
commit = start + hit and end = start + release, then Released at the release
time, and Rejected for refused casts. The hit/release times are in the game's
own data: `animationSequence[i].hit` / `.release` (or `timetohit` /
`timetorelease`). Implement as: encoder in a tested header, Accepted after a
successful `UseAbility`, Rejected otherwise, Released scheduled at `release`.
Crash risk exists (upstream's attempt crashed); keep it isolated in one commit.

### Eighth client session on `2b1d1ee` (2026-11-11): animations and hits

Server log: 174 casts, 74 accepted, 79 `SetAnimationState`, 44 `CombatEvent`,
2 deaths, the first `ObjectiveUpdate`, no Lua errors, no client or server
crash. What the player saw is not reported yet. 100 casts were refused with
no answer to the client, and many accepted attacks animated without damage.

Fixed in the following commit (implemented, unit/contract-tested; client
effect not verified):

- **Target validation used the wrong argument.** Scripts call
  `nGameObject.ValidateHostileTarget(team, target)` (160 of 175 calls pass the
  result of `GetTeam`; `ValidateFriendlyTarget`: 48 of 49). The binding looked
  up an *object* whose id equals the team number and compared against that
  object's team. A number is now a team (`game/team_validation.h`).
- **Cast responses** (design in the previous entry): `raknet/action_response.h`
  encodes the 56-byte build-103 payload (byte-exact tests). The hero ability
  handler sends Rejected on every refusal, Accepted with
  commit = start + hit and end = start + release, and schedules Released at
  the release time. `Ability::GetActiveCastTiming` reads
  `animationSequence[i].hit/.release`, else `timetohit`/`timetorelease`;
  abilities without timing get no response and are logged once. Times are
  not scaled by attack speed yet. The squad-ability path still uses the old
  upstream layout. **Crash risk:** upstream's version of this packet crashed
  the client; if a crash follows a cast, this commit is the first suspect.

### Ninth client session on `32938fd` (2026-11-12): cast responses are stable

621 casts, 911 `ActionCommandResponse` packets (accepted / released /
rejected), 350 animations, 250 hits, 10 kills, 1 objective update; no client
or server crash over a long session. The build-103 response layout therefore
does not reproduce the crash that made upstream disable it (`08d2633`).
Refused casts alternate with accepted ones while a basic attack is held: the
client re-sends the attack during the swing and the hero's script is busy.
darkspin queues a repeat at max(cooldown, release); not implemented yet.

Remaining errors were fixed in `4814f1e` (deployed after this session):
`PreloadAsset`/`PreloadModifier` returned nil (effect/modifier ids used by
1,267 call sites), and `math.mod`, a Lua 5.1 compatibility alias that LuaJIT
lacks. That commit broke the require-shim guardrail (the compatibility line
was inserted above the `package.path` marker the test anchors on); moved.

Also fixed after this session:
- **Kills were rare (about 25 hits per kill).** Every hero's weapon damage was
  hard-coded to 1-5 (`object.cpp`). The class data defines it for all 100
  hero classes (Arborus 6-12, Zrin 9-14, Vex 4-8) and the server already parsed
  it; now used (`game/hero_stats.h`). Enemy health and difficulty scaling are
  not yet checked against the game data.
- `nTimeManager.GetSimTimeDt` returned nothing; projectile, nova and several
  modifier scripts loop `elapsed += GetSimTimeDt(); coroutine.yield()`. It now
  returns the duration of the current Lua update in seconds.
