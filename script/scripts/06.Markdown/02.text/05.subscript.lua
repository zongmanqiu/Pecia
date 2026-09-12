-- ==Meta==
-- @name Subscript
-- @name.zh-CN 下标
-- @name.zh-TW 下標
-- @name.ja 下付き
-- @name.de Tiefgestellt
-- @name.fr Indice
-- @name.es Subíndice
-- @name.ru Подстрочный
-- @name.ko 아래 첨자
-- @name.pt-BR Subscrito
-- @name.it Pedice
-- @name.ar منخفض
-- @name.hi अधोलेख
-- @name.id Subskrip
-- @name.tr Alt Simge
-- @name.vi Chữ viết dưới
-- @date 20260813
-- ==/Meta==

-- 选中文本包裹 ~ 下标（切换：已有 ~ 包裹则移除）；无选区时插入成对
-- 标记，光标置于中间。注意：与删除线 ~~ 区分，不要与删除线混用。

if input == "" then
    insert("~~", -1)
    return
end
if input:sub(1, 1) == "~" and input:sub(-1) == "~" then
    return input:sub(2, -2)  -- 切换：移除
else
    return "~" .. input .. "~"
end
