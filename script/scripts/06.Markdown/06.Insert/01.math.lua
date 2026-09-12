-- ==Meta==
-- @name LaTeX Math
-- @name.zh-CN LaTeX 公式
-- @name.zh-TW LaTeX 公式
-- @name.ja LaTeX 数式
-- @name.de LaTeX-Mathematik
-- @name.fr Mathématiques LaTeX
-- @name.es Matemáticas LaTeX
-- @name.ru Математика LaTeX
-- @name.ko LaTeX 수식
-- @name.pt-BR Matemática LaTeX
-- @name.it Matematica LaTeX
-- @name.ar رياضيات LaTeX
-- @name.hi LaTeX गणित
-- @name.id Matematika LaTeX
-- @name.tr LaTeX Matematik
-- @name.vi Toán LaTeX
-- @date 20260813
-- ==/Meta==

-- 选中文本包裹 $$ 行间公式（预览渲染为 PNG 图片）；
-- 无选区时插入成对 $$，光标置于中间。

if input == "" then
    insert("$$$$", -2)
    return
end
if input:sub(1, 2) == "$$" and input:sub(-2) == "$$" then
    return input:sub(3, -3)  -- 切换：移除
else
    return "$$\n" .. input .. "\n$$"
end
