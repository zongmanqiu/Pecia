> **🌐 Languages**: [中文](../../README.md) | [English](README.en.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Deutsch](README.de.md) | [Français](README.fr.md) | [Español](README.es.md) | [Русский](README.ru.md) | [Português-BR](README.pt-BR.md) | [Italiano](README.it.md) | [繁體中文](README.zh-TW.md) | [العربية](README.ar.md) | [हिन्दी](README.hi.md) | [Indonesia](README.id.md) | [Türkçe](README.tr.md) | [Tiếng Việt](README.vi.md)

# Pecia — अत्यंत सरल पाठ संपादक

---

**[⬇️ नवीनतम संस्करण डाउनलोड करें (1.1.2) (Gitee)](https://gitee.com/qiuzongman/pecia/releases/download/1.1.2/Pecia_x64_1.1.2.zip)** ｜ **[⬇️ नवीनतम संस्करण डाउनलोड करें (1.1.2) (GitHub)](https://github.com/zongmanqiu/Pecia/releases/download/1.1.2/Pecia_x64_1.1.2.zip)** ｜ [Gitee रिपॉज़िटरी](https://gitee.com/qiuzongman/pecia) ｜ [GitHub मिरर](https://github.com/zongmanqiu/Pecia) | [Bilibili डेमो](https://www.bilibili.com/video/BV144Yo6oEz9/)

C++17 + FLTK आधारित Windows हल्का पाठ संपादक। मल्टी-टैब, बड़ी फ़ाइलें तुरंत खुलती हैं, बहु-एन्कोडिंग समर्थन, Lua स्क्रिप्ट एक्सटेंशन। नया: साइड पैनल (फ़ाइल ट्री/रूपरेखा), फ़ॉन्ट चयन संवाद, विंडो छाया; Markdown पूर्वावलोकन बहुत बड़े दस्तावेज़ों को खंडों में प्रस्तुत करता है।

> **प्लेटफ़ॉर्म स्थिति**: केवल **Windows x64 (64-बिट)** समर्थित। 32-बिट संस्करण उपलब्ध नहीं है, और क्रॉस-प्लेटफ़ॉर्म (Linux/Mac/32-बिट) रखरखाव नहीं किया जाता। बिल्ड स्क्रिप्ट में 64-बिट जाँच अंतर्निहित है (CMakeLists के शीर्ष पर देखें), 32-बिट गलती से कॉन्फ़िगर करने पर सीधे त्रुटि आएगी।

## सॉफ़्टवेयर इंटरफ़ेस

- Markdown पूर्वावलोकन
  - ![0.MD预览](0.Makrodnw预览.png)
- मल्टी-टैब
  - ![2.多标签页](1.多标签页.png)
- न्यूनतम स्थिति पट्टी
  - ![2.极简状态](2.极简状态.png)
- विकल्प
  - ![3.选项](3.选项.png)
- शॉर्टकट सेटिंग्स
  - ![4.快捷键](4.快捷键.png)
- AI चैट
  - ![5.AI对话](5.AI对话.png)
- Lua कंसोल
  - ![6.Lua控制台](6.Lua控制台.gif)
- Lua स्क्रिप्ट
  - ![7.Lua脚本](7.Lua脚本.gif)


## त्वरित आरंभ

### वातावरण आवश्यकताएँ (मूल वातावरण, स्रोत कोड और तृतीय-पक्ष लाइब्रेरी के अतिरिक्त)

- Windows 10/11 x64 (1803+ या उससे अधिक, अंतर्निहित curl/tar डाउनलोड उपकरण)
- **Visual Studio Build Tools 2022 / VS2022** (C++ डेस्कटॉप वर्कलोड सहित)
  - स्क्रिप्ट में `build/msvc_env.bat` है जो MSVC और Windows SDK **स्वचालित रूप से खोजता** है (vcvars/vswhere/डेवलपर कमांड प्रॉम्प्ट पर निर्भर नहीं, साधारण cmd में डबल-क्लिक करें), या कस्टम इंस्टॉलेशन स्थान निर्दिष्ट करने के लिए वातावरण चर `PECIA_MSVC_ROOT` / `PECIA_SDK_ROOT` का उपयोग करें
- **CMake ≥ 3.16** (PATH में होना चाहिए; जेनरेटर **NMake Makefiles** है, Ninja की आवश्यकता नहीं)
- **Rust टूलचेन** (≥ 1.85, edition 2024 समर्थित; mmdr और RaTeX FFI रेंडरिंग लाइब्रेरी को कंपाइल करने के लिए,
  https://rustup.rs से इंस्टॉल करें, पहली बार बिल्ड करते समय crates.io से निर्भरताएँ खींचने के लिए इंटरनेट कनेक्शन आवश्यक)
- **इंटरनेट कनेक्शन** (केवल पहली बार बिल्ड के लिए आवश्यक: 2_download.bat निर्भरताएँ डाउनलोड करता है + 4_build_rust.bat में `cargo fetch` क्रेट खींचता है)
- **git आवश्यक नहीं** (निर्भरताएँ curl से डाउनलोड और tar से एक्सट्रैक्ट होती हैं)

### बिल्ड (अनुशंसित: डबल-क्लिक से एक कदम)

```bat
REM स्रोत कोड डाउनलोड करने के बाद, बस इस एक स्क्रिप्ट पर डबल-क्लिक करें।
REM या क्रम में चलाता है: 1 टूलचेन जाँच → 2 छह तृतीय-पक्ष लाइब्रेरी डाउनलोड → 3 पैच लागू →
REM 4 cargo fetch + दो Rust FFI कंपाइल → 5 FLTK + CMake Pecia तीन पैकेज कंपाइल →
REM 26 यूनिट टेस्ट चलाएँ।
REM पहली बार का समय इंटरनेट और मशीन पर निर्भर; बाद में कैश पुनः उपयोग, इंक्रीमेंटल कंपाइल + टेस्ट लगभग 1-2 मिनट।
build\full.bat
```

> Step-by-step build scripts, tests and packaging: see `build/build_readme.md`. Release process: see `docs/RELEASE_CHECKLIST.md`. आर्किटेक्चर डिज़ाइन (प्रोसेस मॉडल/लेयरिंग/सबसिस्टम): `开发指南.md` के आरंभ में «आर्किटेक्चर अवलोकन» देखें।

## निर्देशिका संरचना

```
main/
├── CMakeLists.txt      बिल्ड नियम (CMake प्रवेश बिंदु)
├── main.cpp            प्रोग्राम प्रवेश बिंदु
├── README.md           यह दस्तावेड़ (परियोजना परिचय/डाउनलोड/इंटरफ़ेस/बिल्ड प्रवेश)
├── docs/               अन्य भाषाओं में README संस्करण (README.<lang>.md) + इंटरफ़ेस स्क्रीनशॉट + intro.pptx
├── 目录结构说明.md      निर्देशिका और मॉड्यूल विनिर्देश (विरासतकर्ता के लिए आवश्यक पठन)
├── 开发指南.md         विकास/रिलीज़ प्रक्रिया, आर्किटेक्चर अवलोकन और प्री-कमिट चेकलिस्ट
├── AGENTS.md           इंजीनियरिंग नियम (सहयोगी/AI नियम)
├── HISTORY.md          संस्करण इतिहास (रिलीज़ पर शीर्ष पर नया संस्करण अनुभाग जोड़ें; पुरानी सामग्री अपरिवर्तित रखें)
├── LICENSE             AGPL-3.0 का शब्दशः आधिकारिक पाठ (UTF-8, BOM रहित; शर्तों को संशोधित नहीं किया जा सकता)
├── THIRD-PARTY-NOTICES.md  तृतीय-पक्ष लाइब्रेरी सूचनाएँ (नाम/संस्करण/लाइसेंस/पैच संख्या)
├── .gitignore          संस्करण-नियंत्रण अनदेखा नियम
├── .gitattributes      पंक्ति-अंत और पाठ विशेषता नियम
├── core/               शुद्ध मूल तर्क (UI रहित)
├── editor/             संपादक नियंत्रण
├── ui/                 विंडो/डायलॉग/टूलबार
├── LuaTool/            PeciaLua स्वतंत्र उपकरण (विंडो/प्रवेश/पाइप सर्वर)
├── AIChat/             PeciaAIChat स्वतंत्र उपकरण (विंडो/प्रवेश)
├── script/             Lua स्क्रिप्ट इंजन
├── DOCS_MANIFEST.md    दस्तावेज़ सूची (कोड बदलने के बाद प्रत्येक आइटम जाँचें, test_docs गेटकीपर; केवल विकास के लिए, exe के साथ नहीं भेजा जाता)
├── image/              छवि संसाधन (pecia.ico / ai_bot.ico / lua.ico …)
├── test/               यूनिट टेस्ट स्रोत कोड
├── lang/               भाषा फ़ाइलें (16 × .ini)
├── theme/              थीम रंग फ़ाइलें (light.ini/dark.ini/cream.ini/mint.ini/ice.ini)
├── example/            दस्तावेज़ उदाहरण (Markdown रेंडरिंग प्रभाव दिखाते हैं: Mermaid डायग्राम, LaTeX फ़ॉर्मूला, छवियाँ आदि)
├── patches/            .thirdparty लाइब्रेरी स्रोत पर संशोधन संग्रह (बिल्ड के दौरान स्वचालित रूप से लागू)
├── mdview/             Markdown पूर्वावलोकन (पूर्वावलोकन पैनल/HTML रेंडरिंग, mmdr FFI हेडर सहित)
└── build/              बिल्ड स्क्रिप्ट और बिल्ड सहायक स्रोत कोड
```

## तकनीकी स्टैक

| घटक | संस्करण | रिपॉज़िटरी |
|---|---|---|
| C++ | C++17 | — |
| FLTK | 1.4.5 | https://github.com/fltk/fltk |
| Lua | 5.5.1 | https://github.com/lua/lua |
| PCRE2 | 10.47 | https://github.com/PCRE2Project/pcre2 |
| md4c | 2026-09-11 (10fa4f44) | https://github.com/mity/md4c |
| litehtml | 0.10 | https://github.com/litehtml/litehtml |
| stb | 2026-08-02 (2c980bb5) | https://github.com/nothings/stb |
| mmdr (mermaid-rs-renderer) | 0.3.1 | crates.io (cargo) |
| RaTeX (फ़ॉर्मूला रेंडरिंग, अंतर्निहित) | 0.1.14 | crates.io (cargo) |
| Rust | ≥ 1.85 | https://rustup.rs |
| बिल्ड | CMake + NMake Makefiles + MSVC | — |

> सुझाव: तृतीय-पक्ष लाइब्रेरी स्रोत पर स्थानीय संशोधन किए जा सकते हैं; संशोधित पूर्ण फ़ाइलें
> `main/patches/<लाइब्रेरी-नाम>/` में संग्रहीत होनी चाहिए (बिल्ड के दौरान 3_patch.bat द्वारा स्वचालित रूप से ओवरराइट), केवल अपूर्ण टेक्स्ट रिकॉर्ड न करें।
> अपग्रेड करते समय ऊपर दी गई रिपॉज़िटरी से नया संस्करण खींचें और पैच पुनः लागू करें (build/FLTK_PATCHES.md में उदाहरण देखें)।
> mmdr में resvg 0.47 सम्मिलित है (परियोजना का एकमात्र SVG रैस्टराइज़र: mermaid आरेख और LaTeX सूत्र SVG दोनों को इसी द्वारा PNG में बदला जाता है); RaTeX केवल SVG स्ट्रिंग उत्पन्न करता है और सीधे resvg पर निर्भर नहीं है।

---

## लाइसेंस

[GNU AGPL-3.0](../../LICENSE)।

तृतीय-पक्ष घटक (FLTK/Lua/PCRE2/md4c/litehtml/stb और Rust crates) के लाइसेंस,
संशोधन (पैच) विवरण और वितरण दायित्व [THIRD-PARTY-NOTICES.md](../../THIRD-PARTY-NOTICES.md) में देखें।
