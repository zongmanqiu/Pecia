-- ==Meta==
-- @name Reverse
-- @name.zh-CN 逆序
-- @name.zh-TW 逆序
-- @name.ja 逆順
-- @name.de Umkehren
-- @name.fr Inverser
-- @name.es Invertir
-- @name.ru Обратный порядок
-- @name.ko 역순
-- @name.pt-BR Inverter
-- @name.it Inverti
-- @name.ar عكس
-- @name.hi विलोम
-- @name.id Balik
-- @name.tr Ters Sırala
-- @name.vi Đảo ngược
-- @author 邱宗满
-- @date 20260810
-- ==/Meta==

-- Reverse the original line order.

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

for i = 1, math.floor(#lines / 2) do
    lines[i], lines[#lines - i + 1] = lines[#lines - i + 1], lines[i]
    eols[i], eols[#eols - i + 1] = eols[#eols - i + 1], eols[i]
end

local out = {}
for i = 1, #lines do out[#out + 1] = lines[i] .. eols[i] end
return table.concat(out, "")
