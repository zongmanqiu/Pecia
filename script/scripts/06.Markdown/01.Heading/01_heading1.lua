-- ==Meta==
-- @name # Heading 1
-- @name.zh-CN # 标题1
-- @name.zh-TW # 標題1
-- @name.ja # 見出し1
-- @name.de # Überschrift 1
-- @name.fr # Titre 1
-- @name.es # Título 1
-- @name.ru # Заголовок 1
-- @name.ko # 제목1
-- @name.pt-BR # Título 1
-- @name.it # Intestazione 1
-- @name.ar # عنوان 1
-- @name.hi # शीर्षक 1
-- @name.id # Judul 1
-- @name.tr # Başlık 1
-- @name.vi # Tiêu đề 1
-- @date 20260812
-- ==/Meta==

-- 选中多行，行首添加 1 级标题标记（已有标题标记会先移除）。

if input == "" then
    insert("# ")  -- 无选区：插入标题标记
    return
end

local crlf = input:find("\r\n") ~= nil
local out = {}
for line in (input:gsub("\r\n", "\n") .. "\n"):gmatch("(.-)\n") do
    local hashes = line:match("^[ \t]*(#+)")
    if hashes and #hashes == 1 then
        table.insert(out, line)          -- 已是标题1：保持
    else
        local outLine = line:gsub("^[ \t]*#+[ \t]*", "")
        table.insert(out, "# " .. outLine)
    end
end
local result = table.concat(out, "\n")
if crlf then result = result:gsub("\n", "\r\n") end

return result
