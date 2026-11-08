-- Redistributable regression fixture for Game::Lua51Transpiler.
-- Independent test code (GPLv3, this repository); it contains no Darkspore
-- script content. It is compiled to Lua 5.1 bytecode with the same header as
-- Darkspore's server scripts (int=4, size_t=4, lua_Number=float32), see README.md.
-- Only float32-exact numbers are used so float32 Lua 5.1 and LuaJIT agree.
local log = {}
local function emit(...)
  local n = select("#", ...)
  local parts = {}
  for i = 1, n do
    parts[i] = tostring((select(i, ...)))
  end
  log[#log + 1] = n .. ":" .. table.concat(parts, ",")
end

-- Multiple results containing nil holes (CALL C=0 feeding CALL/RETURN/SETLIST B=0).
local function holes() return 1, nil, nil, 4 end
local function trailing() return 7, nil end
local function none() end
local function passthrough(...) return ... end
local function tail(...) return passthrough(...) end
local function count(...) return select("#", ...) end

emit(holes())
emit(trailing())
emit(none())
emit("x", holes())
emit(passthrough(nil, 2, nil))
emit(tail(nil, nil, 3))
emit(count(holes()), count(trailing()), count(none()), count(passthrough(nil, nil)))
local packed = { "head", holes() }
emit(packed[1], packed[2], packed[3], packed[4], packed[5])

-- Lua 5.1 compatibility 'arg' table for vararg functions that never use '...'.
local function oldstyle(...) return arg.n, arg[1], arg[3] end
emit(oldstyle(5, nil, 6))

-- Numeric and generic loops.
local acc = 0
for i = 10, 1, -3 do acc = acc + i end
for _ = 1, 3 do acc = acc * 2 end
emit(acc)
local keys = {}
for _, v in ipairs({ "a", "b", "c" }) do keys[#keys + 1] = v end
emit(table.concat(keys))
local w = 0
while w < 5 do w = w + 2 end
repeat w = w - 1 until w <= 3
emit(w)

-- Logical operators, comparisons and boolean materialisation.
local a, b = nil, false
emit(a or "dflt", b or 0, a and 1, 1 and 2, not a, 1 < 2, 2 <= 1, "a" == "a", 3 ~= 3)
local c = (a == nil) and "yes" or "no"
emit(c)

-- Arithmetic, length, concatenation and float32-exact constants.
emit(7 % 3, -7 % 3, 2 ^ 10, 10 / 4, -(3), #"hello", "con" .. "cat" .. 1)
emit(0.5, 1.25, 3 * 0.25, -0.125)

-- Methods (SELF) and closures with shared upvalues (no loop capture).
local obj = { n = 5 }
function obj:get(k) return self.n + k end
emit(obj:get(2))
local function counter()
  local value = 0
  return function() value = value + 1; return value end
end
local nextValue = counter()
nextValue(); nextValue()
emit(nextValue())

-- String constants that need escaping.
emit("tab\tq\"uote\\n", "\0" .. "1" == "\0001", #"\0\1\2")

-- Table constructor spanning several SETLIST blocks.
local big = {
  1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
  21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
  41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60,
}
emit(#big, big[50], big[51], big[60])

-- Coroutines yielding through translated code.
local co = coroutine.wrap(function(x)
  local y = coroutine.yield(x + 1)
  return y * 2
end)
emit(co(1), co(10))

return table.concat(log, "\n")
