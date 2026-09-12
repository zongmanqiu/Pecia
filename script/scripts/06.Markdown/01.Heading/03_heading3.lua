-- ==Meta==
-- @name ### Heading 3
-- @name.zh-CN ### 标题3
-- @name.zh-TW ### 標題3
-- @name.ja ### 見出し3
-- @name.de ### Überschrift 3
-- @name.fr ### Titre 3
-- @name.es ### Título 3
-- @name.ru ### Заголовок 3
-- @name.ko ### 제목3
-- @name.pt-BR ### Título 3
-- @name.it ### Intestazione 3
-- @name.ar ### عنوان 3
-- @name.hi ### शीर्षक 3
-- @name.id ### Judul 3
-- @name.tr ### Başlık 3
-- @name.vi ### Tiêu đề 3
-- @date 20260812
-- ==/Meta==

-- 选中多行，行首添加 3 级标题标记（已有标题标记会先移除）。

if input == "" then
    insert("### ")  -- 无选区：插入标题标记
    return
end

local crlf = input:find("\r\n") ~= nil
local out = {}
for line in (input:gsub("\r\n", "\n") .. "\n"):gmatch("(.-)\n") do
    local hashes = line:match("^[ \t]*(#+)")
    if hashes and #hashes == 3 then
        table.insert(out, line)          -- 已是标题3：保持
    else
        local outLine = line:gsub("^[ \t]*#+[ \t]*", "")
        table.insert(out, "### " .. outLine)
    end
end
local result = table.concat(out, "\n")
if crlf then result = result:gsub("\n", "\r\n") end

return result
