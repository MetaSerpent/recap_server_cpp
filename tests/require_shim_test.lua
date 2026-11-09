-- Runs the require() shim embedded in darkspore_server/source/game/lua.cpp under
-- LuaJIT against tests/fixtures/require_shim. Usage (from the repo root):
--   luajit tests/require_shim_test.lua
local source = assert(io.open("darkspore_server/source/game/lua.cpp", "rb")):read("*a"):gsub("\r\n", "\n")
local marker = 'LoadBuffer(R"(\npackage.path'
local first = assert(source:find(marker, 1, true), "require shim not found in lua.cpp")
local start = first + #'LoadBuffer(R"(\n'
local finish = assert(source:find('\n)");', start, true), "end of require shim not found")
local shim = source:sub(start, finish)
assert(loadstring(shim, "=require_shim"))()

-- Point the search path at the fixture tree (same folder layout as data/serverdata).
local root = "tests/fixtures/require_shim/"
package.path = root .. "lua/?.lua;" .. root .. "Abilities/?.lua;" .. root .. "Modifiers/?.lua;" .. root .. "behaviors/?.lua"

local failures = 0
local function check(cond, what)
  if not cond then
    failures = failures + 1
    io.stderr:write("FAIL: ", what, "\n")
  end
end

local helper = require("Lua!Helper.lua")
check(helper and helper.name == "Helper", "Lua!name.lua resolves to lua/name.lua")
check(require("Helper") == helper, "prefixed and bare names share one module instance")
check(require("0x3681d755!Helper.lua") == helper, "type-hash group prefix resolves")
check(require("data/serverdata/lua/Helper.lua") == helper, "path-style names still resolve")
check(require("Modifers!mod_x.lua").name == "mod_x", "the Modifers! typo used by game scripts resolves")
check(require("Lua!AttributeUtils.lua").name:find("hashed", 1, true) ~= nil,
  "names missing from the registry resolve to 0x<FNV-1 of lower-case name>.lua")

local ok, err = pcall(require, "Abilities!broken.lua")
check(not ok and tostring(err):find("broken module body", 1, true) ~= nil,
  "errors raised inside a module are not masked as 'module not found'")
ok, err = pcall(require, "Lua!DoesNotExist.lua")
check(not ok and tostring(err):find("DoesNotExist", 1, true) ~= nil, "missing modules report their name")
check(require("string") == string, "standard libraries still load")

if failures > 0 then
  io.stderr:write(failures, " check(s) failed\n")
  os.exit(1)
end
print("require_shim_test: all checks passed")
