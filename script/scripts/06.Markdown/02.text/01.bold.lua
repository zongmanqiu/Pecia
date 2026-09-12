-- ==Meta==
-- @name Bold
-- @name.zh-CN 加粗
-- @name.zh-TW 加粗
-- @name.ja 太字
-- @name.de Fett
-- @name.fr Gras
-- @name.es Negrita
-- @name.ru Жирный
-- @name.ko 굵게
-- @name.pt-BR Negrito
-- @name.it Grassetto
-- @name.ar عريض
-- @name.hi मोटा
-- @name.id Tebal
-- @name.tr Kalın
-- @name.vi Đậm
-- @date 20260812
-- ==/Meta==

-- 选中文本包裹 ** 加粗（切换：已有 ** 包裹则移除）；无选区时插入成对
-- 标记，光标置于中间。

if input == "" then
    insert("****", -2)
    return
end
if input:sub(1, 2) == "**" and input:sub(-2) == "**" then
    return input:sub(3, -3)  -- 切换：移除
else
    return "**" .. input .. "**"
end
