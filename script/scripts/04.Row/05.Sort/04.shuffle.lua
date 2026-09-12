-- ==Meta==
-- @name Shuffle
-- @name.zh-CN 乱序
-- @name.zh-TW 亂序
-- @name.ja シャッフル
-- @name.de Mischen
-- @name.fr Mélanger
-- @name.es Mezclar
-- @name.ru Перемешать
-- @name.ko 임의 배열
-- @name.pt-BR Embaralhar
-- @name.it Mescola
-- @name.ar خلط
-- @name.hi क्रम बदलें
-- @name.id Acak
-- @name.tr Karıştır
-- @name.vi Xáo trộn
-- @author 邱宗满
-- @date 20260810
-- ==/Meta==

-- Randomize the line order (Fisher-Yates).

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

for i = #lines, 2, -1 do
    local j = math.random(i)
    lines[i], lines[j] = lines[j], lines[i]
    eols[i], eols[j] = eols[j], eols[i]
end

local out = {}
for i = 1, #lines do out[#out + 1] = lines[i] .. eols[i] end
return table.concat(out, "")
