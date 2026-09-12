-- ==Meta==
-- @name Highlight
-- @name.zh-CN 高亮
-- @name.zh-TW 高亮
-- @name.ja ハイライト
-- @name.de Hervorheben
-- @name.fr Surligner
-- @name.es Resaltar
-- @name.ru Выделение
-- @name.ko 강조
-- @name.pt-BR Destaque
-- @name.it Evidenzia
-- @name.ar تمييز
-- @name.hi हाइलाइट
-- @name.id Sorot
-- @name.tr Vurgula
-- @name.vi Đánh dấu
-- @date 20260813
-- ==/Meta==

-- 选中文本包裹 == 高亮（切换：已有 == 包裹则移除）；无选区时插入成对
-- 标记，光标置于中间。

if input == "" then
    insert("====", -2)
    return
end
if input:sub(1, 2) == "==" and input:sub(-2) == "==" then
    return input:sub(3, -3)  -- 切换：移除
else
    return "==" .. input .. "=="
end
