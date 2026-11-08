-- Compiles a fixture with a float32 Lua 5.1.5 build (see README.md).
-- usage: lua51f compile_fixture.lua <source.lua> <output.luac> <chunkname>
local source = assert(io.open(arg[1], "rb")):read("*a")
local chunk = assert(loadstring(source, "=" .. arg[3]))
local out = assert(io.open(arg[2], "wb"))
out:write(string.dump(chunk))
out:close()
