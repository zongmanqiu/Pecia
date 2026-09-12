-- ==Meta==
-- @name Merge
-- @name.zh-CN 合并
-- @name.zh-TW 合併
-- @name.ja 結合
-- @name.de Zusammenführen
-- @name.fr Fusionner
-- @name.es Unir
-- @name.ru Объединить
-- @name.ko 병합
-- @name.pt-BR Mesclar
-- @name.it Unisci
-- @name.ar دمج
-- @name.hi विलय
-- @name.id Gabung
-- @name.tr Birleştir
-- @name.vi Gộp
-- @author 邱宗满
-- @date 20260810
--!param sep= : separator between lines (default: space)
--!pui.title = Merge Lines
--!pui.title.zh-CN = 合并行
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

-- Join all lines of the selection into one line, separated by the
-- given separator (default: a single space).

if #input == 0 then
    if pecia_lang == "zh-CN" then
        print("请先选择要处理的文本。")
    else
        print("No selection - select the lines to process first.")
    end
    return
end

local sep = params.sep
if sep == nil then sep = " " end  -- empty string = no separator

local parts = {}
local start = 1
while start <= #input do
    local nl = input:find("[\r\n]", start)
    local line = nl and input:sub(start, nl - 1) or input:sub(start)
    parts[#parts + 1] = line
    if not nl then break end
    start = nl + 1
    if input:sub(start, start) == "\n" and input:sub(start - 1, start - 1) == "\r" then
        start = start + 1
    end
end
return table.concat(parts, sep)
