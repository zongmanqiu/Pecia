-- ==Meta==
-- @name Split
-- @name.zh-CN 拆分
-- @name.zh-TW 拆分
-- @name.ja 分割
-- @name.de Teilen
-- @name.fr Séparer
-- @name.es Dividir
-- @name.ru Разделить
-- @name.ko 분할
-- @name.pt-BR Dividir
-- @name.it Dividi
-- @name.ar تقسيم
-- @name.hi विभाजित
-- @name.id Pisah
-- @name.tr Ayır
-- @name.vi Tách
-- @author 邱宗满
-- @date 20260810
--!param sep=, : separator to split on
--!pui.title = Split by Separator
--!pui.title.zh-CN = 按分隔符拆分
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

-- Split the selection into multiple lines at every occurrence of the
-- separator (e.g. CSV columns). The separator is treated as plain text,
-- not a pattern, so chars like "." or "*" are safe.

if #input == 0 then
    if pecia_lang == "zh-CN" then
        print("请先选择要处理的文本。")
    else
        print("No selection - select the text to process first.")
    end
    return
end

local sep = params.sep or ","
if sep == "" then
    if pecia_lang == "zh-CN" then
        print("请填写分隔符。")
    else
        print("Separator is empty - fill in the split character(s).")
    end
    return
end

-- Escape Lua pattern metacharacters so the separator is literal.
local esc = sep:gsub("([%^%$%(%)%%%.%*%+%-%?%[%]%{%}])", "%%%1")
local result = input:gsub(esc, "\n")
return result
