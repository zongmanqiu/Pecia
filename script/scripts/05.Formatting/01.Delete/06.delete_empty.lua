-- ==Meta==
-- @name Empty Lines
-- @name.zh-CN 空白行
-- @name.zh-TW 空白行
-- @name.ja 空行
-- @name.de Leere Zeilen
-- @name.fr Lignes vides
-- @name.es Líneas vacías
-- @name.ru Пустые строки
-- @name.ko 빈 줄
-- @name.pt-BR Linhas vazias
-- @name.it Righe vuote
-- @name.ar أسطر فارغة
-- @name.hi खाली पंक्तियाँ
-- @name.id Baris Kosong
-- @name.tr Boş Satırlar
-- @name.vi Dòng trống
-- @author 邱宗满
-- @date 20260810
-- ==/Meta==

-- Remove lines that are empty or only whitespace.

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
    if line:match("^%s*$") then
        if not nl then break end
        start = nl + #eol
    else
        out[#out + 1] = line .. eol
        if not nl then break end
        start = nl + #eol
    end
end
return table.concat(out, "")