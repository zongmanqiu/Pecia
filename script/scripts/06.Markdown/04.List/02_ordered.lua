-- ==Meta==
-- @name Ordered List
-- @name.zh-CN 有序列表
-- @name.zh-TW 有序列表
-- @name.ja 番号付きリスト
-- @name.de Nummerierte Liste
-- @name.fr Liste numérotée
-- @name.es Lista numerada
-- @name.ru Нумерованный список
-- @name.ko 번호 매긴 목록
-- @name.pt-BR Lista numerada
-- @name.it Elenco numerato
-- @name.ar قائمة مرتبة
-- @name.hi क्रमित सूची
-- @name.id Daftar Berurut
-- @name.tr Sıralı Liste
-- @name.vi Danh sách có thứ tự
-- @date 20260812
-- ==/Meta==

-- 选中多行：无编号行添加递增编号 "1. "；所有行都已编号时切换为
-- 移除编号（保留缩进）。混合选区（部分行已编号）重新编号。
-- 无选区时插入 "1. " 列表标记，光标置于其后。

if input == "" then
    insert("1. ")
    return
end

-- 选区全为空白时视为普通内容，避免被“空行保持”分支跳过
local allBlank = (input:match("^[ \t\r\n]*$") ~= nil)

-- 切换方向：所有非空行都已编号 → 移除（切换）；否则 → 添加/重新编号
local remove = true
local hasContent = false
for line in (input:gsub("\r\n", "\n") .. "\n"):gmatch("(.-)\n") do
    if not line:match("^[ \t]*$") then
        hasContent = true
        if not line:match("^[ \t]*%d+%.[ \t]") then
            remove = false
            break
        end
    end
end
if not hasContent then remove = false end

local crlf = input:find("\r\n") ~= nil
local n = 0
local changed = 0
local out = {}
for line in (input:gsub("\r\n", "\n") .. "\n"):gmatch("(.-)\n") do
    local outLine = line
    if line:match("^[ \t]*$") and not allBlank then
        table.insert(out, outLine)      -- 空行保持（编号不递增）
    elseif remove then
        local indent, rest = line:match("^([ \t]*)%d+%.[ \t]*(.*)$")
        if indent then
            outLine = indent .. rest    -- 已有编号：移除（保留缩进）
            changed = changed + 1
        end
        table.insert(out, outLine)
    else
        outLine = line:gsub("^([ \t]*)%d+%.[ \t]*", "%1")   -- 移除旧编号（保留缩进）
        n = n + 1
        table.insert(out, n .. ". " .. outLine)
        changed = changed + 1
    end
end
local result = table.concat(out, "\n")
if crlf then result = result:gsub("\n", "\r\n") end

return result
