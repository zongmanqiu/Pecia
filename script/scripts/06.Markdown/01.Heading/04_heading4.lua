-- ==Meta==
-- @name #### Heading 4
-- @name.zh-CN #### 标题4
-- @name.zh-TW #### 標題4
-- @name.ja #### 見出し4
-- @name.de #### Überschrift 4
-- @name.fr #### Titre 4
-- @name.es #### Título 4
-- @name.ru #### Заголовок 4
-- @name.ko #### 제목4
-- @name.pt-BR #### Título 4
-- @name.it #### Intestazione 4
-- @name.ar #### عنوان 4
-- @name.hi #### शीर्षक 4
-- @name.id #### Judul 4
-- @name.tr #### Başlık 4
-- @name.vi #### Tiêu đề 4
-- @date 20260812
-- ==/Meta==

-- 选中多行，行首添加 4 级标题标记（已有标题标记会先移除）。

if input == "" then
    insert("#### ")  -- 无选区：插入标题标记
    return
end

local crlf = input:find("\r\n") ~= nil
local out = {}
for line in (input:gsub("\r\n", "\n") .. "\n"):gmatch("(.-)\n") do
    local hashes = line:match("^[ \t]*(#+)")
    if hashes and #hashes == 4 then
        table.insert(out, line)          -- 已是标题4：保持
    else
        local outLine = line:gsub("^[ \t]*#+[ \t]*", "")
        table.insert(out, "#### " .. outLine)
    end
end
local result = table.concat(out, "\n")
if crlf then result = result:gsub("\n", "\r\n") end

return result
