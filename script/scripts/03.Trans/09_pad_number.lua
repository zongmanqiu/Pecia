-- ==Meta==
-- @name Pad Numbers
-- @name.zh-CN 数字补0
-- @author 邱宗满
-- @date 20260810
--!param where=before|after
--!param width=4
--!pui.title = Pad Numbers
--!pui.title.zh-CN = 数字补0
--!pui.where = Where to pad
--!pui.where.zh-CN = 补0位置
--!pui.where.before = Integer part
--!pui.where.before.zh-CN = 整数部分
--!pui.where.after = Decimal places
--!pui.where.after.zh-CN = 小数部分
--!pui.width = Target digits
--!pui.width.zh-CN = 目标位数
-- ==/Meta==

-- Zero-pad numbers in the selection.
--
--   where=before: every integer part is left-padded to `width` digits:
--     "1345 and 36" -> "1345 and 0036" (with width=4). Negative numbers
--     keep their sign: -36 -> -0036. Decimal parts are left untouched,
--     so 13.4 -> 0013.4 (integer part padded).
--
--   where=after: decimal places are right-padded to `width` digits:
--     13.4 -> 13.400 (width=3). Numbers without a decimal point are
--     left untouched.
--
-- Known limitation: multi-dot tokens like "1.2.3" (version numbers) are
-- matched as "1.2" then "3", so before-mode pads each part separately.

local sel = editor:get_selection()
if #sel == 0 then
    print("No selection - select the text to convert first.")
    return
end

local where = params.where or "before"
local width = tonumber(params.width)
if where ~= "before" and where ~= "after" then
    print("Invalid where value '" .. where .. "' - use before or after.")
    return
end
if not width or width < 1 or width > 20 then
    print("Invalid width '" .. tostring(params.width) .. "' - use a number between 1 and 20.")
    return
end

local out = {}
local i = 1
local n = #sel
while i <= n do
    local b = sel:byte(i)
    local clen = 1
    if b >= 0xF0 then clen = 4
    elseif b >= 0xE0 then clen = 3
    elseif b >= 0xC0 then clen = 2
    end
    local c = sel:sub(i, i + clen - 1)

    local consumed = false
    if c:match("%d") then
        if where == "before" then
            -- Match the whole number incl. an optional decimal part, but
            -- pad only the integer part: 164.1 (width=4) -> 0164.1.
            local num = sel:match("^%d+%.?%d*", i)
            if num then
                local intPart, decPart = num:match("^(%d+)%.?(%d*)$")
                local padded = string.rep("0", width - #intPart) .. intPart
                if decPart and #decPart > 0 then
                    out[#out + 1] = padded .. "." .. decPart
                else
                    out[#out + 1] = padded
                end
                i = i + #num
                consumed = true
            end
        else
            local num = sel:match("^%d+%.%d+", i)
            if num then
                local intPart, decPart = num:match("^(%d+)%.(%d+)$")
                if #decPart < width then
                    out[#out + 1] = intPart .. "." .. decPart
                        .. string.rep("0", width - #decPart)
                else
                    out[#out + 1] = num
                end
                i = i + #num
                consumed = true
            end
        end
    end
    if not consumed then
        out[#out + 1] = c
        i = i + clen
    end
end
editor:replace_selection(table.concat(out, ""))
