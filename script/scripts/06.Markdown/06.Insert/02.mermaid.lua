-- ==Meta==
-- @name Mermaid
-- @name.zh-CN Mermaid 图
-- @name.zh-TW Mermaid 圖
-- @name.ja Mermaid ダイアグラム
-- @name.de Mermaid-Diagramm
-- @name.fr Diagramme Mermaid
-- @name.es Diagrama Mermaid
-- @name.ru Диаграмма Mermaid
-- @name.ko Mermaid 다이어그램
-- @name.pt-BR Diagrama Mermaid
-- @name.it Diagramma Mermaid
-- @name.ar مخطط Mermaid
-- @name.hi Mermaid चित्र
-- @name.id Diagram Mermaid
-- @name.tr Mermaid Diyagramı
-- @name.vi Sơ đồ Mermaid
-- @date 20260813
-- ==/Meta==

-- 插入 mermaid 图表代码块（预览渲染为 PNG 图片）。
-- 无选区时插入 flowchart 模板，光标置于内容处；
-- 有选区时把选区包进 mermaid 围栏。

if input == "" then
    insert("```mermaid\nflowchart TD\n    A[Start] --> B[End]\n```", -4)
    return
end
return "```mermaid\n" .. input .. "\n```"
