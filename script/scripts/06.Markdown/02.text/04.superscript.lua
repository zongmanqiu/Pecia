-- ==Meta==
-- @name Superscript
-- @name.zh-CN 上标
-- @name.zh-TW 上標
-- @name.ja 上付き
-- @name.de Hochgestellt
-- @name.fr Exposant
-- @name.es Superíndice
-- @name.ru Надстрочный
-- @name.ko 위 첨자
-- @name.pt-BR Sobrescrito
-- @name.it Apice
-- @name.ar مرفوع
-- @name.hi अधोलेख
-- @name.id Superskrip
-- @name.tr Üst Simge
-- @name.vi Chữ viết trên
-- @date 20260813
-- ==/Meta==

-- 选中文本包裹 ^ 上标（切换：已有 ^ 包裹则移除）；无选区时插入成对
-- 标记，光标置于中间。

if input == "" then
    insert("^^", -1)
    return
end
if input:sub(1, 1) == "^" and input:sub(-1) == "^" then
    return input:sub(2, -2)  -- 切换：移除
else
    return "^" .. input .. "^"
end
