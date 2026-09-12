-- ==Meta==
-- @name 「*」
-- @author 邱宗满
-- @date 20260809
-- ==/Meta==

local left, right = "「", "」"
if #input > 0 then
    return left .. input .. right
else
    insert(left .. right, #left)
end