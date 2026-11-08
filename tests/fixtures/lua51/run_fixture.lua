-- Runs a compiled or transpiled fixture chunk and writes its result.
-- Usage: <lua> run_fixture.lua <chunk> <output>
-- The output is opened in binary mode so it is byte-identical on every OS.
local chunk = assert(loadfile(arg[1]))
local out = assert(io.open(arg[2], "wb"))
out:write(tostring(chunk()), "\n")
out:close()
