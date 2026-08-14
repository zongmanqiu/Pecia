-- ==Meta==
-- @name Footnote
-- @name.zh-CN 脚注
-- @date 20260813
--!param label=1: footnote label (number or text)
--!param text=: footnote text
--!pui.title = Insert Footnote
--!pui.title.zh-CN = 插入脚注
--!pui.label = Label
--!pui.label.zh-CN = 标签
--!pui.text = Text
--!pui.text.zh-CN = 内容
-- ==/Meta==

-- 插入脚注引用 [^label] 与其定义行。md4c 渲染时定义会自动移到文末，
-- 定义写在哪里都行；有选区时选区作为引用处的说明文字保留在原文。

local label = params.label or "1"
local text = params.text or ""
editor:replace_selection("[^" .. label .. "]\n\n[^" .. label .. "]: " .. text)
