-- ==Meta==
-- @name CJK-Latin Spacing
-- @name.zh-CN 文本混排间隔
-- @name.zh-TW 文本混排間隔
-- @name.ja CJK-ラテン間隔
-- @name.de CJK-Latenz-Abstand
-- @name.fr Espacement CJK-Latin
-- @name.es Espaciado CJK-Latin
-- @name.ru Интервал CJK-Латиница
-- @name.ko CJK-라틴 간격
-- @name.pt-BR Espaçamento CJK-Latin
-- @name.it Spaziatura CJK-Latin
-- @name.ar تباعد CJK-لاتيني
-- @name.hi CJK-लैटिन अंतर
-- @name.id Jarak CJK-Latin
-- @name.tr CJK-Latin Boşluğu
-- @name.vi Khoảng cách CJK-Latin
-- ==/Meta==

-- 在中文（CJK）与英文/数字之间自动添加 1 个空格，提升混排阅读体验。
-- 规则（PCRE2，UCP 模式）：
--   * 汉字后紧跟字母/数字 -> 中间加空格（如：你好world -> 你好 world）
--   * 字母/数字后紧跟汉字 -> 中间加空格（如：Hello世界 -> Hello 世界）
-- 标点（。，、；：！？等全角标点，以及半角 , . ; : ! ?）不会加空格。
-- 全角空格（U+3000）视为正常空格，不会重复添加。

if #input == 0 then
    print("No selection - select the text to process first.")
    return
end

local out, err = rex.gsub(input, "(\\p{Han})(?=[A-Za-z0-9])", "$1 ")
if out == nil then
    print("Regex error: " .. tostring(err))
    return
end
out, err = rex.gsub(out, "([A-Za-z0-9])(?=\\p{Han})", "$1 ")
if out == nil then
    print("Regex error: " .. tostring(err))
    return
end

if out ~= input then
    return out
else
    print("No CJK-Latin boundaries found.")
end
