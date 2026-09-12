-- ==Meta==
-- @name Duplicated Lines
-- @name.zh-CN 重复行
-- @name.zh-TW 重複行
-- @name.ja 重複行
-- @name.de Doppelte Zeilen
-- @name.fr Lignes dupliquées
-- @name.es Líneas duplicadas
-- @name.ru Дублирующие строки
-- @name.ko 중복 줄
-- @name.pt-BR Linhas duplicadas
-- @name.it Righe duplicate
-- @name.ar أسطر مكررة
-- @name.hi दोहरी पंक्तियाँ
-- @name.id Baris Duplikat
-- @name.tr Tekrarlanan Satırlar
-- @name.vi Dòng trùng lặp
-- @author 邱宗满
-- @date 20260810
-- ==/Meta==

-- Remove duplicate lines from the selection: the FIRST occurrence of
-- every line is kept, later duplicates are dropped (order preserved).

if #input == 0 then
    if pecia_lang == "zh-CN" then
        print("请先选择要处理的文本。")
    else
        print("No selection - select the lines to process first.")
    end
    return
end

local seen = {}
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
    if not seen[line] then
        seen[line] = true
        out[#out + 1] = line .. eol
    end
    if not nl then break end
    start = nl + #eol
end
return table.concat(out, "")
