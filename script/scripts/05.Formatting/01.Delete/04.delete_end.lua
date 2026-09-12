-- ==Meta==
-- @name Trailing Spaces
-- @name.zh-CN 行尾空格
-- @name.zh-TW 行尾空格
-- @name.ja 行末スペース
-- @name.de Nachgestellte Leerzeichen
-- @name.fr Espaces en fin
-- @name.es Espacios al final
-- @name.ru Завершающие пробелы
-- @name.ko 줄 끝 공백
-- @name.pt-BR Espaços finais
-- @name.it Spazi finali
-- @name.ar مسافات ختامية
-- @name.hi अनुगामी स्थान
-- @name.id Spasi Akhir
-- @name.tr Son Boşlukları
-- @name.vi Khoảng cách cuối dòng
-- @author 邱宗满
-- @date 20260810
-- ==/Meta==

-- Remove trailing spaces/tabs of every line.

if #input == 0 then
    if pecia_lang == "zh-CN" then
        print("请先选择要处理的文本。")
    else
        print("No selection - select the lines to process first.")
    end
    return
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
    line = line:gsub("[ \t]+$", "")
    out[#out + 1] = line .. eol
    if not nl then break end
    start = nl + #eol
end
return table.concat(out, "")