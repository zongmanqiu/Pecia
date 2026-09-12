-- ==Meta==
-- @name Strikethrough
-- @name.zh-CN 删除线
-- @name.zh-TW 刪除線
-- @name.ja 取り消し線
-- @name.de Durchgestrichen
-- @name.fr Barré
-- @name.es Tachado
-- @name.ru Зачёркнутый
-- @name.ko 취소선
-- @name.pt-BR Tachado
-- @name.it Barrato
-- @name.ar يتوسطه خط
-- @name.hi आरपार की रेखा
-- @name.id Coret
-- @name.tr Üstü Çizili
-- @name.vi Gạch ngang
-- @date 20260812
-- ==/Meta==

-- 选中文本包裹 ~~ 删除线（切换：已有 ~~ 包裹则移除）；无选区时插入
-- 成对标记，光标置于中间。

if input == "" then
    insert("~~~~", -2)
    return
end
if input:sub(1, 2) == "~~" and input:sub(-2) == "~~" then
    return input:sub(3, -3)  -- 切换：移除
else
    return "~~" .. input .. "~~"
end
