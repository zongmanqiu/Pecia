-- ==Meta==
-- @name To Uppercase
-- @name.zh-CN 转大写
-- @name.zh-TW 轉大寫
-- @name.ja 大文字に変換
-- @name.de In Großbuchstaben
-- @name.fr En majuscules
-- @name.es A mayúsculas
-- @name.ru В прописные
-- @name.ko 대문자로 변환
-- @name.pt-BR Para maiúsculas
-- @name.it In maiuscolo
-- @name.ar تحويل إلى أحرف كبيرة
-- @name.hi बड़े अक्षरों में बदलें
-- @name.id Ke Huruf Besar
-- @name.tr Büyük Harfe
-- @name.vi Viết hoa
-- @author 邱宗满
-- @date 20260810
-- ==/Meta==

-- Convert the selection to all-uppercase ASCII letters.
-- (Chinese text is unaffected; string.upper only touches ASCII.)

if #input == 0 then
    print("No selection - select the text to convert first.")
    return
end
return input:upper()
