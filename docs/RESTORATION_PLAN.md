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
artifacts. These entry-point checks require no proprietary game assets.

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
