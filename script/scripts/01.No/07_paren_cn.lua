-- ==Meta==
-- @name （一） *
-- @author 邱宗满
-- @date 20260809
-- ==/Meta==


if #input == 0 then
    print("No selection - select the lines to number first.")
    return
end
local CN = {"零","一","二","三","四","五","六","七","八","九"}
local function cn(n)
    if n < 10 then return CN[n + 1]
    elseif n == 10 then return "十"
    elseif n < 20 then return "十" .. CN[n - 10 + 1]
    elseif n % 10 == 0 then return CN[n // 10 + 1] .. "十"
    else return CN[n // 10 + 1] .. "十" .. CN[n % 10 + 1] end
end
local out = {}
local n = 1
local start = 1
while start <= #input do
    local nl = input:find("[\r\n]", start)
    if nl then
        local line = input:sub(start, nl - 1)
        if line:match("^%s*$") then
            out[#out + 1] = line
        else
            out[#out + 1] = "（" .. cn(n) .. "）" .. line
            n = n + 1
        end
        local le = nl
        if input:sub(le, le) == "\r" and input:sub(le + 1, le + 1) == "\n" then le = le + 1 end
        out[#out + 1] = input:sub(nl, le)
        start = le + 1
    else
        local line = input:sub(start)
        if not line:match("^%s*$") then
            out[#out + 1] = "（" .. cn(n) .. "）" .. line
        else
            out[#out + 1] = line
        end
        start = #input + 1
    end
end
return table.concat(out, "")
