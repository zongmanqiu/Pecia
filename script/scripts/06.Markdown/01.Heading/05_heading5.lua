-- ==Meta==
-- @name ##### Heading 5
-- @name.zh-CN ##### 标题5
-- @name.zh-TW ##### 標題5
-- @name.ja ##### 見出し5
-- @name.de ##### Überschrift 5
-- @name.fr ##### Titre 5
-- @name.es ##### Título 5
-- @name.ru ##### Заголовок 5
-- @name.ko ##### 제목5
-- @name.pt-BR ##### Título 5
-- @name.it ##### Intestazione 5
-- @name.ar ##### عنوان 5
-- @name.hi ##### शीर्षक 5
-- @name.id ##### Judul 5
-- @name.tr ##### Başlık 5
-- @name.vi ##### Tiêu đề 5
-- @date 20260812
-- ==/Meta==

-- 选中多行，行首添加 5 级标题标记（已有标题标记会先移除）。

if input == "" then
    insert("##### ")  -- 无选区：插入标题标记
    return
end

local crlf = input:find("\r\n") ~= nil
local out = {}
for line in (input:gsub("\r\n", "\n") .. "\n"):gmatch("(.-)\n") do
    local hashes = line:match("^[ \t]*(#+)")
    if hashes and #hashes == 5 then
        table.insert(out, line)          -- 已是标题5：保持
    else
        local outLine = line:gsub("^[ \t]*#+[ \t]*", "")
        table.insert(out, "##### " .. outLine)
    end
end
local result = table.concat(out, "\n")
if crlf then result = result:gsub("\n", "\r\n") end

return result
