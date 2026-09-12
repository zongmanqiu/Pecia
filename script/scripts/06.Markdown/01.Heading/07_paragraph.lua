-- ==Meta==
-- @name Paragraph
-- @name.zh-CN 正文
-- @name.zh-TW 正文
-- @name.ja 本文
-- @name.de Absatz
-- @name.fr Paragraphe
-- @name.es Párrafo
-- @name.ru Абзац
-- @name.ko 본문
-- @name.pt-BR Parágrafo
-- @name.it Paragrafo
-- @name.ar فقرة
-- @name.hi अनुच्छेद
-- @name.id Paragraf
-- @name.tr Paragraf
-- @name.vi Đoạn văn
-- @date 20260812
-- ==/Meta==

-- 选中多行，移除行首标题标记（# ），转为正文。非标题行保持不变。

if input == "" then
    print("No selection - select the heading lines first.")
    return
end

local crlf = input:find("\r\n") ~= nil
local changed = 0
local out = {}
for line in (input:gsub("\r\n", "\n") .. "\n"):gmatch("(.-)\n") do
    local content = line:match("^[ \t]*#+[ \t]*(.*)$")
    if content then
        table.insert(out, content)
        changed = changed + 1
    else
        table.insert(out, line)
    end
end
local result = table.concat(out, "\n")
if crlf then result = result:gsub("\n", "\r\n") end

if changed > 0 then
    return result
else
    print("No heading lines found.")
end
