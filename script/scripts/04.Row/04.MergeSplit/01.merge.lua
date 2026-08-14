-- ==Meta==
-- @name Merge
-- @name.zh-CN 合并
-- @author 邱宗满
-- @date 20260810
--!param sep= : separator between lines (default: space)
--!pui.title = Merge Lines
--!pui.title.zh-CN = 合并行
--!pui.sep = Separator
--!pui.sep.zh-CN = 分隔符
-- ==/Meta==

-- Join all lines of the selection into one line, separated by the
-- given separator (default: a single space).

local sel = editor:get_selection()
if #sel == 0 then
    if pecia_lang == "zh-CN" then
        print("请先选择要处理的文本。")
    else
        print("No selection - select the lines to process first.")
    end
    return
end

local sep = params.sep
if sep == nil then sep = " " end  -- empty string = no separator

local parts = {}
local start = 1
while start <= #sel do
    local nl = sel:find("[\r\n]", start)
    local line = nl and sel:sub(start, nl - 1) or sel:sub(start)
    parts[#parts + 1] = line
    if not nl then break end
    start = nl + 1
    if sel:sub(start, start) == "\n" and sel:sub(start - 1, start - 1) == "\r" then
        start = start + 1
    end
end
editor:replace_selection(table.concat(parts, sep))
