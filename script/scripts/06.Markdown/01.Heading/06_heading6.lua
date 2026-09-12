-- ==Meta==
-- @name ###### Heading 6
-- @name.zh-CN ###### 标题6
-- @name.zh-TW ###### 標題6
-- @name.ja ###### 見出し6
-- @name.de ###### Überschrift 6
-- @name.fr ###### Titre 6
-- @name.es ###### Título 6
-- @name.ru ###### Заголовок 6
-- @name.ko ###### 제목6
-- @name.pt-BR ###### Título 6
-- @name.it ###### Intestazione 6
-- @name.ar ###### عنوان 6
-- @name.hi ###### शीर्षक 6
-- @name.id ###### Judul 6
-- @name.tr ###### Başlık 6
-- @name.vi ###### Tiêu đề 6
-- @date 20260812
-- ==/Meta==

-- 选中多行，行首添加 6 级标题标记（已有标题标记会先移除）。

if input == "" then
    insert("###### ")  -- 无选区：插入标题标记
    return
end

local crlf = input:find("\r\n") ~= nil
local out = {}
for line in (input:gsub("\r\n", "\n") .. "\n"):gmatch("(.-)\n") do
    local hashes = line:match("^[ \t]*(#+)")
    if hashes and #hashes == 6 then
        table.insert(out, line)          -- 已是标题6：保持
    else
        local outLine = line:gsub("^[ \t]*#+[ \t]*", "")
        table.insert(out, "###### " .. outLine)
    end
end
local result = table.concat(out, "\n")
if crlf then result = result:gsub("\n", "\r\n") end

return result
