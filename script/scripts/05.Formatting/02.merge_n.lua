-- ==Meta==
-- @name Merge x
-- @name.zh-CN x 行合 1
-- @author 邱宗满
-- @date 20260810
--!param n=2: lines per group
--!param sep= : separator within a group (default: space)
--!pui.title = Merge x Lines
--!pui.title.zh-CN = x 行合并 1 行
--!pui.n = Lines per group
--!pui.n.zh-CN = 每组行数
--!pui.sep = Separator
--!pui.sep.zh-CN = 分隔符
-- ==/Meta==

-- Merge every N lines into one line (groups stay separate lines).
-- E.g. n=2: "a b c d" -> "a b" / "c d" (with space separator).

local sel = editor:get_selection()
if #sel == 0 then
    if pecia_lang == "zh-CN" then
        print("请先选择要处理的文本。")
    else
        print("No selection - select the lines to process first.")
    end
    return
end

local n = tonumber(params.n) or 2
if n < 1 then n = 1 end
local sep = params.sep
if sep == nil then sep = " " end  -- empty string = no separator

local parts = {}
local start = 1
while start <= #sel do
    local nl = sel:find("[\r\n]", start)
    parts[#parts + 1] = nl and sel:sub(start, nl - 1) or sel:sub(start)
    if not nl then break end
    start = nl + 1
    if sel:sub(start, start) == "\n" and sel:sub(start - 1, start - 1) == "\r" then
        start = start + 1
    end
end

local out = {}
for i = 1, #parts, n do
    local group = {}
    for k = i, math.min(i + n - 1, #parts) do
        group[#group + 1] = parts[k]
    end
    out[#out + 1] = table.concat(group, sep)
end
editor:replace_selection(table.concat(out, "\n"))
