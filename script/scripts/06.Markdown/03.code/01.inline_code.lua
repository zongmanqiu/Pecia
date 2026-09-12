-- ==Meta==
-- @name Inline Code
-- @name.zh-CN 行内代码
-- @name.zh-TW 行內代碼
-- @name.ja インラインコード
-- @name.de Inline-Code
-- @name.fr Code en ligne
-- @name.es Código en línea
-- @name.ru Строчный код
-- @name.ko 인라인 코드
-- @name.pt-BR Código inline
-- @name.it Codice inline
-- @name.ar شفرة مضمنة
-- @name.hi इनलाइन कोड
-- @name.id Kode Inline
-- @name.tr Satır İçi Kod
-- @name.vi Mã nội tuyến
-- @date 20260812
-- ==/Meta==

-- 选中文本包裹反引号行内代码（切换：已有反引号包裹则移除）；无选区
-- 时插入成对反引号，光标置于中间。

if input == "" then
    insert("``", -1)
    return
end
if input:sub(1, 1) == "`" and input:sub(-1) == "`" then
    return input:sub(2, -2)  -- 切换：移除
else
    return "`" .. input .. "`"
end
