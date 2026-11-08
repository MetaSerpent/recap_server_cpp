-- Redistributable fixture: a closure capturing a loop local makes Lua 5.1 emit
-- OP_CLOSE so each iteration gets a fresh upvalue. A flat-locals translation
-- cannot reproduce that, so Game::Lua51Transpiler must refuse this chunk.
local fns = {}
for i = 1, 3 do
  fns[i] = function() return i end
end
return fns[1]() + fns[2]() + fns[3]()
