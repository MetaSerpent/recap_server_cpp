-- Runs a compiled or transpiled fixture chunk and prints its result.
-- Usage: luajit run_fixture.lua <chunk>   (or a float32 Lua 5.1 for bytecode)
local chunk = assert(loadfile(arg[1]))
io.write(tostring(chunk()), "\n")
