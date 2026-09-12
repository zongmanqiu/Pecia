-- ==Meta==
-- @name Regex Replace
-- @name.zh-CN 正则替换
-- @name.zh-TW 正則替換
-- @name.ja 正規表現置換
-- @name.de Regex ersetzen
-- @name.fr Remplacement Regex
-- @name.es Reemplazo Regex
-- @name.ru Замена по регулярному выражению
-- @name.ko 정규식 치환
-- @name.pt-BR Substituição Regex
-- @name.it Sostituzione Regex
-- @name.ar استبدال Regex
-- @name.hi रेगेक्स प्रतिस्थापन
-- @name.id Penggantian Regex
-- @name.tr Regex Değiştirme
-- @name.vi Thay thế Regex
-- @author 邱宗满
-- @date 20260810
--!param pattern=: regex pattern (PCRE2 syntax)
--!param replacement=: replacement text ($1 = first capture group)
--!pui.title = Regex Replace
--!pui.title.zh-CN = 正则替换
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
--!pui.pattern = Find
--!pui.pattern.zh-CN = 查找
--!pui.pattern.zh-TW = 正則表達式
--!pui.pattern.ja = 正規表現
--!pui.pattern.de = Muster
--!pui.pattern.fr = Motif
--!pui.pattern.es = Patrón
--!pui.pattern.ru = Шаблон
--!pui.pattern.ko = 패턴
--!pui.pattern.pt-BR = Padrão
--!pui.pattern.it = Modello
--!pui.pattern.ar = النمط
--!pui.pattern.hi = पैटर्न
--!pui.pattern.id = Pola
--!pui.pattern.tr = Desen
--!pui.pattern.vi = Mẫu
--!pui.replacement = Replacement
--!pui.replacement.zh-CN = 替换为
--!pui.replacement.zh-TW = 替換文字
--!pui.replacement.ja = 置換文字
--!pui.replacement.de = Ersetzung
--!pui.replacement.fr = Remplacement
--!pui.replacement.es = Reemplazo
--!pui.replacement.ru = Замена
--!pui.replacement.ko = 치환 문자
--!pui.replacement.pt-BR = Substituição
--!pui.replacement.it = Sostituzione
--!pui.replacement.ar = الاستبدال
--!pui.replacement.hi = प्रतिस्थापन
--!pui.replacement.id = Penggantian
--!pui.replacement.tr = Değiştirme
--!pui.replacement.vi = Thay thế
-- ==/Meta==

-- Replace every match of a PCRE2 regex in the selection. The engine is
-- rex (PCRE2, statically linked).
--
-- PCRE2 syntax (not Lua patterns):
--   \d+        digits        [0-9]{2,4}   bounded repeat
--   (?i)foo    case-insensitive
--   (a|b)      alternation   $1           capture group in replacement
--   \p{Han}+   Chinese chars
-- Case sensitivity follows standard regex rules (sensitive by default);
-- prepend (?i) to the pattern to ignore case.

if #input == 0 then
    if pecia_lang == "zh-CN" then
        print("请先选择要处理的文本。")
    else
        print("No selection - select the text to process first.")
    end
    return
end

local pattern = params.pattern or ""
local replacement = params.replacement or ""
if pattern == "" then
    if pecia_lang == "zh-CN" then
        print("请填写正则模式。")
    else
        print("Pattern is empty - fill in a regex pattern first.")
    end
    return
end

local r, err = rex.gsub(input, pattern, replacement)
if r == nil then
    if pecia_lang == "zh-CN" then
        print("正则错误: " .. tostring(err))
    else
        print("Regex error: " .. tostring(err))
    end
    return
end
if r ~= input then
    return r
else
    if pecia_lang == "zh-CN" then
        print("无匹配。")
    else
        print("No matches.")
    end
end
