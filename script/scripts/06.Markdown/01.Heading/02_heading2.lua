-- ==Meta==
-- @name ## Heading 2
-- @name.zh-CN ## 标题2
-- @name.zh-TW ## 標題2
-- @name.ja ## 見出し2
-- @name.de ## Überschrift 2
-- @name.fr ## Titre 2
-- @name.es ## Título 2
-- @name.ru ## Заголовок 2
-- @name.ko ## 제목2
-- @name.pt-BR ## Título 2
-- @name.it ## Intestazione 2
-- @name.ar ## عنوان 2
-- @name.hi ## शीर्षक 2
-- @name.id ## Judul 2
-- @name.tr ## Başlık 2
-- @name.vi ## Tiêu đề 2
-- @date 20260812
-- ==/Meta==

-- 选中多行，行首添加 2 级标题标记（已有标题标记会先移除）。

if input == "" then
    insert("## ")  -- 无选区：插入标题标记
    return
end

local crlf = input:find("\r\n") ~= nil
local body = input:gsub("\r\n", "\n")
local trailingNL = body:sub(-1) == "\n"
if not trailingNL then body = body .. "\n" end
local out = {}
for line in body:gmatch("(.-)\n") do
    local hashes = line:match("^[ \t]*(#+)")
    if hashes and #hashes == 2 then
        table.insert(out, line)          -- 已是标题2：保持
    else
        local outLine = line:gsub("^[ \t]*#+[ \t]*", "")
        table.insert(out, "## " .. outLine)
    end
end
local result = table.concat(out, "\n")
if trailingNL then result = result .. "\n" end
if crlf then result = result:gsub("\n", "\r\n") end

return result
