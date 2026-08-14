-- ==Meta==
-- @name (1) *
-- @author 邱宗满
-- @date 20260809
-- ==/Meta==

local sel = editor:get_selection()
if #sel == 0 then
    print("No selection - select the lines to number first.")
    return
end

local out = {}
local n = 1
local start = 1
while start <= #sel do
    local nl = sel:find("[\r\n]", start)
    if nl then
        local line = sel:sub(start, nl - 1)
        if line:match("^%s*$") then
            out[#out + 1] = line
        else
            out[#out + 1] = "(" .. n .. ") " .. line
            n = n + 1
        end
        local le = nl
        if sel:sub(le, le) == "\r" and sel:sub(le + 1, le + 1) == "\n" then le = le + 1 end
        out[#out + 1] = sel:sub(nl, le)
        start = le + 1
    else
        local line = sel:sub(start)
        if not line:match("^%s*$") then
            out[#out + 1] = "(" .. n .. ") " .. line
        else
            out[#out + 1] = line
        end
        start = #sel + 1
    end
end
editor:replace_selection(table.concat(out, ""))
