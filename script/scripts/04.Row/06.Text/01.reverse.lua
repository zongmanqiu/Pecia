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

-- Reverse the characters of every line (UTF-8 aware - a Chinese
-- character stays one unit). Line order is preserved.

if #input == 0 then
    if pecia_lang == "zh-CN" then
        print("请先选择要处理的文本。")
    else
        print("No selection - select the lines to process first.")
    end
    return
end

local function charsOf(s)
    local out = {}
    local i = 1
    while i <= #s do
        local b = s:byte(i)
        local clen = 1
        if b >= 0xF0 then clen = 4
        elseif b >= 0xE0 then clen = 3
        elseif b >= 0xC0 then clen = 2
        end
        out[#out + 1] = s:sub(i, i + clen - 1)
        i = i + clen
    end
    return out
end

local out = {}
local start = 1
while start <= #input do
    local nl = input:find("[\r\n]", start)
    local line, eol = "", ""
    if nl then
        line = input:sub(start, nl - 1)
        eol = input:sub(nl, nl)
        if eol == "\r" and input:sub(nl + 1, nl + 1) == "\n" then eol = "\r\n" end
    else
        line = input:sub(start)
    end
    local ch = charsOf(line)
    for i = 1, math.floor(#ch / 2) do
        ch[i], ch[#ch - i + 1] = ch[#ch - i + 1], ch[i]
    end
    out[#out + 1] = table.concat(ch, "") .. eol
    if not nl then break end
    start = nl + #eol
end
return table.concat(out, "")
