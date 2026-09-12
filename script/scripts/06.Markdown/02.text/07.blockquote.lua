-- ==Meta==
-- @name Blockquote
-- @name.zh-CN 引用块
-- @name.zh-TW 引用塊
-- @name.ja 引用ブロック
-- @name.de Zitatblock
-- @name.fr Bloc de citation
-- @name.es Bloque de cita
-- @name.ru Блок цитаты
-- @name.ko 인용 블록
-- @name.pt-BR Bloco de citação
-- @name.it Blocco citazione
-- @name.ar اقتباس
-- @name.hi उद्धरण ब्लॉक
-- @name.id Blok Kutipan
-- @name.tr Alıntı Bloğu
-- @name.vi Trích dẫn
-- @date 20260812
-- ==/Meta==

-- 选中多行，行首加 "> " 引用标记；已有 "> " 的行切换为移除。
-- 无选区时插入 "> " 引用符号，光标置于其后。

if input == "" then
    insert("> ")
    return
end

-- 选区全为空白（空格/制表/换行）时视为普通内容，避免被
-- 下方“空行保持”分支跳过（选中空格点击脚本却无反应）。
local allBlank = (input:match("^[ \t\r\n]*$") ~= nil)

local crlf = input:find("\r\n") ~= nil
local changed = 0
local out = {}
for line in (input:gsub("\r\n", "\n") .. "\n"):gmatch("(.-)\n") do
    local indent, rest = line:match("^([ \t]*)>[ \t]?(.*)$")
    local outLine = line
    if indent then
        outLine = indent .. rest          -- 已有引用标记：移除（切换）
        changed = changed + 1
    elseif line:match("^[ \t]*$") and not allBlank then
        -- 文档中的空行保持（引用块段落分隔）
    else
        outLine = "> " .. line
        changed = changed + 1
    end
    table.insert(out, outLine)
end
local result = table.concat(out, "\n")
if crlf then result = result:gsub("\n", "\r\n") end

return result
