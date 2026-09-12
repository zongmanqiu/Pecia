-- ==Meta==
-- @name Task List
-- @name.zh-CN 任务列表
-- @name.zh-TW 任務列表
-- @name.ja タスクリスト
-- @name.de Aufgabenliste
-- @name.fr Liste de tâches
-- @name.es Lista de tareas
-- @name.ru Список задач
-- @name.ko 작업 목록
-- @name.pt-BR Lista de tarefas
-- @name.it Elenco attività
-- @name.ar قائمة المهام
-- @name.hi कार्य सूची
-- @name.id Daftar Tugas
-- @name.tr Görev Listesi
-- @name.vi Danh sách công việc
-- @date 20260812
-- ==/Meta==

-- 选中多行：无任务标记的行加 "- [ ] "；已有 "- [ ]" 切换为 "- [x]"；
-- 已有 "- [x]" 切换回 "- [ ]"。无选区时插入 "- [ ] "，光标置于其后。

if input == "" then
    insert("- [ ] ")
    return
end

-- 选区全为空白时视为普通内容，避免被“空行保持”分支跳过
local allBlank = (input:match("^[ \t\r\n]*$") ~= nil)

local crlf = input:find("\r\n") ~= nil
local changed = 0
local out = {}
for line in (input:gsub("\r\n", "\n") .. "\n"):gmatch("(.-)\n") do
    local outLine = line
    if line:match("^[ \t]*$") and not allBlank then
        table.insert(out, outLine)
    else
        local indent, box, rest = line:match("^([ \t]*)- %[([ xX])%][ \t]*(.*)$")
        if indent then
            if box == " " then
                outLine = indent .. "- [x] " .. rest
            else
                outLine = indent .. "- [ ] " .. rest
            end
            changed = changed + 1
        else
            outLine = "- [ ] " .. line
            changed = changed + 1
        end
    end
    table.insert(out, outLine)
end
local result = table.concat(out, "\n")
if crlf then result = result:gsub("\n", "\r\n") end

return result
