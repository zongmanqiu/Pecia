-- ==Meta==
-- @name Merge x
-- @name.zh-CN x 行合 1
-- @name.zh-TW x 行合 1
-- @name.ja x 行を1行に
-- @name.de x Zeilen zusammen
-- @name.fr Fusionner x lignes
-- @name.es Unir x líneas
-- @name.ru Объединить x строк
-- @name.ko x줄 합치기
-- @name.pt-BR Mesclar x linhas
-- @name.it Unisci x righe
-- @name.ar دمج x أسطر
-- @name.hi x पंक्तियाँ जोड़ें
-- @name.id Gabung x Baris
-- @name.tr x Satırı Birleştir
-- @name.vi Gộp x dòng
-- @author 邱宗满
-- @date 20260810
--!param n=2: lines per group
--!param sep= : separator within a group (default: space)
--!pui.title = Merge x Lines
--!pui.title.zh-CN = x 行合并 1 行
--!pui.title.zh-TW = 標題
--!pui.title.ja = タイトル
--!pui.title.de = Titel
--!pui.title.fr = Titre
--!pui.title.es = Título
--!pui.title.ru = Заголовок
--!pui.title.ko = 제목
--!pui.title.pt-BR = Título
--!pui.title.it = Titolo
--!pui.title.ar = العنوان
--!pui.title.hi = शीर्षक
--!pui.title.id = Judul
--!pui.title.tr = Başlık
--!pui.title.vi = Tiêu đề
--!pui.n = Lines per group
--!pui.n.zh-CN = 每组行数
--!pui.n.zh-TW = 每組行數
--!pui.n.ja = グループ行数
--!pui.n.de = Zeilen pro Gruppe
--!pui.n.fr = Lignes par groupe
--!pui.n.es = Líneas por grupo
--!pui.n.ru = Строк на группу
--!pui.n.ko = 그룹별 행 수
--!pui.n.pt-BR = Linhas por grupo
--!pui.n.it = Righe per gruppo
--!pui.n.ar = أسطر لكل مجموعة
--!pui.n.hi = प्रति समूह पंक्तियाँ
--!pui.n.id = Baris per Grup
--!pui.n.tr = Grup Başına Satır
--!pui.n.vi = Dòng mỗi nhóm
--!pui.sep = Separator
--!pui.sep.zh-CN = 分隔符
--!pui.sep.zh-TW = 分隔符
--!pui.sep.ja = 区切り文字
--!pui.sep.de = Trennzeichen
--!pui.sep.fr = Séparateur
--!pui.sep.es = Separador
--!pui.sep.ru = Разделитель
--!pui.sep.ko = 구분 기호
--!pui.sep.pt-BR = Separador
--!pui.sep.it = Separatore
--!pui.sep.ar = فاصل
--!pui.sep.hi = विभाजक
--!pui.sep.id = Pemisah
--!pui.sep.tr = Ayraç
--!pui.sep.vi = Dấu phân cách
-- ==/Meta==

-- Merge every N lines into one line (groups stay separate lines).
-- E.g. n=2: "a b c d" -> "a b" / "c d" (with space separator).

if #input == 0 then
    if pecia_lang == "zh-CN" then
        print("请先选择要处理的文本。")
    else
        print("No selection - select the lines to process first.")
    end
    return
end

local n = tonumber(params.n) or 2
if n < 1 then n = 1 end
local sep = params.sep
if sep == nil then sep = " " end  -- empty string = no separator

local parts = {}
local start = 1
while start <= #input do
    local nl = input:find("[\r\n]", start)
    parts[#parts + 1] = nl and input:sub(start, nl - 1) or input:sub(start)
    if not nl then break end
    start = nl + 1
    if input:sub(start, start) == "\n" and input:sub(start - 1, start - 1) == "\r" then
        start = start + 1
    end
end

local out = {}
for i = 1, #parts, n do
    local group = {}
    for k = i, math.min(i + n - 1, #parts) do
        group[#group + 1] = parts[k]
    end
    out[#out + 1] = table.concat(group, sep)
end
return table.concat(out, "\n")
