-- ==Meta==
-- @name Leading Spaces
-- @name.zh-CN 行首空格
-- @name.zh-TW 行首空格
-- @name.ja 行頭スペース
-- @name.de Führende Leerzeichen
-- @name.fr Espaces en début
-- @name.es Espacios al inicio
-- @name.ru Ведущие пробелы
-- @name.ko 줄 앞쪽 공백
-- @name.pt-BR Espaços iniciais
-- @name.it Spazi iniziali
-- @name.ar مسافات بادئة
-- @name.hi अग्रणी स्थान
-- @name.id Spasi Awal
-- @name.tr Baş Boşlukları
-- @name.vi Khoảng cách đầu dòng
-- @author 邱宗满
-- @date 20260810
-- ==/Meta==

-- Remove leading spaces/tabs of every line.

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
    line = line:gsub("^[ \t]+", "")
    out[#out + 1] = line .. eol
    if not nl then break end
    start = nl + #eol
end
return table.concat(out, "")