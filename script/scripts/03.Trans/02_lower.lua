-- ==Meta==
-- @name To Lowercase
-- @name.zh-CN 转小写
-- @name.zh-TW 轉小寫
-- @name.ja 小文字に変換
-- @name.de In Kleinbuchstaben
-- @name.fr En minuscules
-- @name.es A minúsculas
-- @name.ru В строчные
-- @name.ko 소문자로 변환
-- @name.pt-BR Para minúsculas
-- @name.it In minuscolo
-- @name.ar تحويل إلى أحرف صغيرة
-- @name.hi छोटे अक्षरों में बदलें
-- @name.id Ke Huruf Kecil
-- @name.tr Küçük Harfe
-- @name.vi Viết thường
-- @author 邱宗满
-- @date 20260810
-- ==/Meta==

-- Convert the selection to all-lowercase ASCII letters.
-- (Chinese text is unaffected; string.lower only touches ASCII.)

if #input == 0 then
    print("No selection - select the text to convert first.")
    return
end
return input:lower()
