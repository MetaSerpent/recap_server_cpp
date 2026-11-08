# Lua 5.1 transpiler fixtures

Independent test code for `Game::Lua51Transpiler`
(`darkspore_server/source/game/lua51_transpiler.h`). Nothing here is taken from
Darkspore; the files are GPLv3 like the rest of this repository.

| File | Purpose |
| --- | --- |
| `transpiler_fixture.lua` | Source exercising multiple results with `nil` holes, varargs, the 5.1 `arg` table, loops, logic, constants, methods, upvalues, `SETLIST` blocks and coroutines. |
| `transpiler_fixture.luac` | Its Lua 5.1 bytecode with Darkspore's header (`int`/`size_t`/instruction = 4 bytes, `lua_Number` = float32). |
| `transpiler_fixture.expected` | Output of the **bytecode** on a float32 Lua 5.1.5 interpreter: the reference semantics. |
| `close_fixture.lua` / `.luac` | A closure capturing a loop local (emits `OP_CLOSE`); the transpiler must refuse it. |
| `run_fixture.lua` | Runs a chunk and prints its result. |
| `compile_fixture.lua` | Compiles a fixture with a neutral chunk name (no local paths in the bytecode). |

CI (`gameplay-guardrails.yml`) transpiles the bytecode, runs the result on
LuaJIT and requires output identical to `transpiler_fixture.expected`.

## Regenerating

Build Lua 5.1.5 as a 32-bit binary with `LUA_NUMBER float`, `LUA_NUMBER_SCAN "%f"`
and `LUA_NUMBER_FMT "%.7g"` in `luaconf.h` (call it `lua51f`). Its
`string.dump` header must match `1b 4c 75 61 51 00 01 04 04 04 04 00`.

```sh
lua51f compile_fixture.lua transpiler_fixture.lua transpiler_fixture.luac transpiler_fixture
lua51f compile_fixture.lua close_fixture.lua close_fixture.luac close_fixture
lua51f run_fixture.lua transpiler_fixture.luac > transpiler_fixture.expected
```

Use only float32-exact numbers in fixtures so float32 Lua 5.1 and LuaJIT print
the same values. `.gitattributes` keeps every file byte-exact (no EOL conversion).
