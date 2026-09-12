-- ==Meta==
-- @name Number Headings
-- @name.zh-CN 标题编号
-- @name.zh-TW 標題編號
-- @name.ja 見出し番号
-- @name.de Überschriften nummerieren
-- @name.fr Numéroter les titres
-- @name.es Numerar títulos
-- @name.ru Нумеровать заголовки
-- @name.ko 제목 번호 매기기
-- @name.pt-BR Numerar títulos
-- @name.it Numera intestazioni
-- @name.ar ترقيم العناوين
-- @name.hi शीर्षक क्रमांकन
-- @name.id Nomori Judul
-- @name.tr Başlık Numaralandır
-- @name.vi Đánh số tiêu đề
-- @date 20260812
-- ==/Meta==

-- 选中多行，为各级标题添加数字编号：1. / 1.1 / 1.1.1（按层级递增，
-- 子级在父级变化时重置）。已有编号会被替换；非标题行保持不变。

if input == "" then
    print("No selection - select the heading lines first.")
    return
end

local crlf = input:find("\r\n") ~= nil
local counts = {}
local out = {}
for line in (input:gsub("\r\n", "\n") .. "\n"):gmatch("(.-)\n") do
    local level, text = line:match("^(#+)[ \t]*(.*)$")
    if level then
        local lv = #level
        counts[lv] = (counts[lv] or 0) + 1
        for i = lv + 1, 6 do counts[i] = nil end
        text = text:gsub("^%d+%.%d*[ \t]*", "")   -- 移除旧编号
        local num = {}
        for i = 1, lv do table.insert(num, counts[i] or 0) end
        table.insert(out, string.rep("#", lv) .. " " ..
                           table.concat(num, ".") .. " " .. text)
    else
        table.insert(out, line)
    end
end
local result = table.concat(out, "\n")
if crlf then result = result:gsub("\n", "\r\n") end

return result
