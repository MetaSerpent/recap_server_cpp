# Lua binding audits

Static checks of the server's sol2 bindings against the scripts the installer
converts from the game's `ServerData.package` (`data/serverdata/{lua,Abilities,
Modifiers,behaviors}`). They read the user's local game data and contain none
of it. Run them after changing `luafunctions.cpp` or the transpiler.

```sh
# engine members used by scripts that the server neither binds nor the scripts define
python tools/lua_audit/binding_gaps.py <Server>/data/serverdata darkspore_server/source 60

# calls passing fewer arguments than a binding requires, and literal type mismatches
python tools/lua_audit/arity_audit.py <Server>/data/serverdata darkspore_server/source/game/luafunctions.cpp
```

Why they exist: sol2 type-checks binding arguments (`SOL_SAFE_FUNCTION_CALLS`),
so a script calling `nAbility.PlayAnimationSequence()` against a binding that
declares two parameters is a Lua error that aborts the ability. On build
5.3.0.103 the first run found 10 functions (303 call sites) called with too
few arguments and 6 parameters with the wrong type (`nThreadData` slots are
numbers). Both tools work on transpiler output (`R<depth>_<n>` registers) and
are heuristics: they follow constants and namespaces within straight-line code.
