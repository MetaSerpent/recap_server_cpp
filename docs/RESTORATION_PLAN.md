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
- Reset the old ObjectManager, Lua VM, markers, enemies and objective counters when an eligible player requests the next planet after completing a dungeon; accumulate per-player medal totals across planets and prevent duplicate completion tally. The server currently selects the next campaign level sequentially; actual vote-selection packets need verification. Refuse to reset while another player is in a live or uncommitted state. A new deployment from Spaceship after the previous chain is completed resets chain progression and medals instead of reusing the previous run. Full end-of-mission reward formulas and loot chances still need reconstruction.

## Verification matrix

| Gate | Definition of done | Status |
| --- | --- | --- |
| P0-A | Standalone C++ ability routing tests | Local test passed; CI added |
| P0-B | Windows and Ubuntu server build | Upstream build workflows; inspect run |
| P0-C | Local login, hero selection, stage load | Needs real Darkspore client |
| P0-D | All three heroes cast correct unlocked abilities | Needs client traces |
| P0-E | Finish encounter and complete stage | Needs client traces |
| P1-A | Loot, XP and save/load persist after restart | Not verified |
| P1-B | Chain progression, cashout, replay | Not verified |

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
