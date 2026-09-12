-- ==Meta==
-- @name Z-A
-- @name.zh-CN Z-A
-- @name.zh-TW Z-A
-- @name.ja Z-A
-- @name.de Z-A
-- @name.fr Z-A
-- @name.es Z-A
-- @name.ru Z-A
-- @name.ko Z-A
-- @name.pt-BR Z-A
-- @name.it Z-A
-- @name.ar Z-A
-- @name.hi Z-A
-- @name.id Z-A
-- @name.tr Z-A
-- @name.vi Z-A
-- @author 邱宗满
-- @date 20260810
-- ==/Meta==

-- Sort the lines alphabetically (Z-A). Chinese sorts by code point,
-- not pinyin.

if #input == 0 then
    if pecia_lang == "zh-CN" then
        print("请先选择要处理的文本。")
    else
        print("No selection - select the lines to process first.")
    end
    return
end

local lines, eols = {}, {}
local start = 1
while start <= #input do
    local nl = input:find("[\r\n]", start)
    if nl then
        lines[#lines + 1] = input:sub(start, nl - 1)
        local eol = input:sub(nl, nl)
        if eol == "\r" and input:sub(nl + 1, nl + 1) == "\n" then eol = "\r\n" end
        eols[#eols + 1] = eol
        start = nl + #eol
    else
        lines[#lines + 1] = input:sub(start)
        eols[#eols + 1] = ""
        break
    end
end

local idx = {}
for i = 1, #lines do idx[i] = i end
table.sort(idx, function(a, b) return lines[a] < lines[b] end)
local nl2, ne = {}, {}
for i = #idx, 1, -1 do nl2[#nl2 + 1] = lines[idx[i]]; ne[#ne + 1] = eols[idx[i]] end
lines, eols = nl2, ne

local out = {}
for i = 1, #lines do out[#out + 1] = lines[i] .. eols[i] end
return table.concat(out, "")
