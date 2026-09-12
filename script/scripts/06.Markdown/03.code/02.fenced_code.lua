-- ==Meta==
-- @name Fenced Code Block
-- @name.zh-CN 围栏代码块
-- @name.zh-TW 圍欄代碼塊
-- @name.ja フェンスコードブロック
-- @name.de Eingefasster Codeblock
-- @name.fr Bloc de code délimité
-- @name.es Bloque de código delimitado
-- @name.ru Огороженный блок кода
-- @name.ko 펜스 코드 블록
-- @name.pt-BR Bloco de código delimitado
-- @name.it Blocco codice delimitato
-- @name.ar بلوك شفرة محاط
-- @name.hi एनक्लोज़्ड कोड ब्लॉक
-- @name.id Blok Kode Terpagar
-- @name.tr Çitli Kod Bloğu
-- @name.vi Khối mã hàng rào
-- @date 20260812
-- ==/Meta==

-- 选中文本用 ``` 围栏包裹（首行/尾行加 ```，不指定语言）；
-- 无选区时插入空围栏，光标置于中间等待输入代码。

if input == "" then
    insert("```\n\n```", -3)
    return
end

local crlf = input:find("\r\n") ~= nil
local body = input:gsub("\r\n", "\n")
if not body:match("^[ \t]*\n") then body = "\n" .. body end
if not body:match("\n[ \t]*$") then body = body .. "\n" end
local result = "```" .. body .. "```"
if crlf then result = result:gsub("\n", "\r\n") end

return result
