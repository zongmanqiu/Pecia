-- ==Meta==
-- @name Unordered List
-- @name.zh-CN 无序列表
-- @name.zh-TW 無序列表
-- @name.ja 箇条書き
-- @name.de Aufzählung
-- @name.fr Liste à puces
-- @name.es Lista con viñetas
-- @name.ru Маркированный список
-- @name.ko 글머리 기호 목록
-- @name.pt-BR Lista com marcadores
-- @name.it Elenco puntato
-- @name.ar قائمة غير مرتبة
-- @name.hi अनक्रमित सूची
-- @name.id Daftar Tidak Berurut
-- @name.tr Sıralı Olmayan Liste
-- @name.vi Danh sách không thứ tự
-- @date 20260812
-- ==/Meta==

-- 选中多行，行首加 "- " 无序列表标记；已有 "- " 的行切换为移除。
-- 无选区时插入 "- " 列表标记，光标置于其后。

if input == "" then
    insert("- ")
    return
end

-- 选区全为空白时视为普通内容，避免被“空行保持”分支跳过
local allBlank = (input:match("^[ \t\r\n]*$") ~= nil)

local crlf = input:find("\r\n") ~= nil
local changed = 0
local out = {}
for line in (input:gsub("\r\n", "\n") .. "\n"):gmatch("(.-)\n") do
    local indent, rest = line:match("^([ \t]*)-[ \t]+(.*)$")
    local outLine = line
    if indent then
        outLine = indent .. rest          -- 已有标记：移除（切换）
        changed = changed + 1
    elseif line:match("^[ \t]*$") and not allBlank then
        -- 文档中的空行保持不变
    else
        outLine = line:gsub("^([ \t]*)", "%1- ")   -- 缩进后加标记（支持多级嵌套）
        changed = changed + 1
    end
    table.insert(out, outLine)
end
local result = table.concat(out, "\n")
if crlf then result = result:gsub("\n", "\r\n") end

return result
