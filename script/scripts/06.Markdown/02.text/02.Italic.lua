-- ==Meta==
-- @name Italic
-- @name.zh-CN 斜体
-- @name.zh-TW 斜體
-- @name.ja 斜体
-- @name.de Kursiv
-- @name.fr Italique
-- @name.es Cursiva
-- @name.ru Курсив
-- @name.ko 기울임꼴
-- @name.pt-BR Itálico
-- @name.it Corsivo
-- @name.ar مائل
-- @name.hi तिरछा
-- @name.id Miring
-- @name.tr İtalik
-- @name.vi Nghiêng
-- @date 20260812
-- ==/Meta==

-- 选中文本包裹 * 斜体（切换：已有单星包裹则移除；** 加粗文本再执行
-- 本脚本会变为加粗斜体 ***）；无选区时插入成对标记，光标置于中间。

if input == "" then
    insert("**", -1)
    return
end
if input:sub(1, 1) == "*" and input:sub(2, 2) ~= "*" and
   input:sub(-1) == "*" and input:sub(-2, -2) ~= "*" then
    return input:sub(2, -2)  -- 切换：移除
else
    return "*" .. input .. "*"
end
