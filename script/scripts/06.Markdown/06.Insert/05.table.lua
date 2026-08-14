-- ==Meta==
-- @name Table
-- @name.zh-CN 表格
-- @date 20260812
--!param rows=3: rows
--!pui.rows.zh-CN = 行数
--!param cols=3: columns
--!pui.cols.zh-CN = 列数
-- ==/Meta==

-- 弹出对话框输入行数和列数（默认 3x3），生成 markdown 表格骨架：
-- 生成 rows 行内容；分隔行（|---|---|）总是插在第 1 行之后，
-- 使第 1 行成为表头（GFM 表格必须有表头行 + 分隔行，否则不渲染
-- 为表格）。光标置于第一行第一格。

local function toInt(v, def)
    local n = tonumber(v)
    if not n then n = def end
    n = math.floor(n)
    if n < 1 then n = 1 end
    if n > 50 then n = 50 end
    return n
end

local rows = toInt(params.rows, 3)
local cols = toInt(params.cols, 3)

local function rowCells()
    local cells = {}
    for i = 1, cols do cells[i] = "  " end
    return "|" .. table.concat(cells, "|") .. "|"
end

local lines = {}
for r = 1, rows do
    lines[#lines + 1] = rowCells()
end

-- 分隔行总是插在第 1 行之后：rows=1 时是“内容行 + 分隔行”
-- （单行表格），rows>1 时在第 1 行和第 2 行之间（第 1 行成表头）。
local sep = {}
for i = 1, cols do sep[i] = "---" end
table.insert(lines, 2, "|" .. table.concat(sep, "|") .. "|")

local result = table.concat(lines, "\n")
editor:replace_selection(result)
-- 光标移到第一行第一格（"|" 之后）
editor:set_cursor(editor:get_cursor() - #result + 1)
