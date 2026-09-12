-- ==Meta==
-- @name All Spaces
-- @name.zh-CN 所有空格
-- @name.zh-TW 所有空格
-- @name.ja 全スペース
-- @name.de Alle Leerzeichen
-- @name.fr Tous les espaces
-- @name.es Todos los espacios
-- @name.ru Все пробелы
-- @name.ko 모든 공백
-- @name.pt-BR Todos os espaços
-- @name.it Tutti gli spazi
-- @name.ar جميع المسافات
-- @name.hi सभी स्थान
-- @name.id Semua Spasi
-- @name.tr Tüm Boşluklar
-- @name.vi Tất cả khoảng cách
-- ==/Meta==

-- 删除选中文本中的全部空格类字符（ASCII 空格、制表符、全角空格）。
-- 注意：换行符不会被删除（避免把多行文本合并成一行）。

if #input == 0 then
    print("No selection - select the text to process first.")
    return
end
-- 全角空格用字面匹配（放入字符类会按字节匹配，误伤含 0x80 字节的汉字）
local out = input:gsub("[ \t]", ""):gsub("\u{3000}", "")
return out
