> **🌐 Languages**: [中文](../../README.md) | [English](README.en.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Deutsch](README.de.md) | [Français](README.fr.md) | [Español](README.es.md) | [Русский](README.ru.md) | [Português-BR](README.pt-BR.md) | [Italiano](README.it.md) | [繁體中文](README.zh-TW.md) | [العربية](README.ar.md) | [हिन्दी](README.hi.md) | [Indonesia](README.id.md) | [Türkçe](README.tr.md) | [Tiếng Việt](README.vi.md)

# Pecia — محرر نصوص بسيط للغاية

---

**[⬇️ تنزيل أحدث إصدار (1.1.2) (Gitee)](https://gitee.com/qiuzongman/pecia/releases/download/1.1.2/Pecia_x64_1.1.2.zip)** ｜ **[⬇️ تنزيل أحدث إصدار (1.1.2) (GitHub)](https://github.com/zongmanqiu/Pecia/releases/download/1.1.2/Pecia_x64_1.1.2.zip)** ｜ [مستودع Gitee](https://gitee.com/qiuzongman/pecia) ｜ [مرآة GitHub](https://github.com/zongmanqiu/Pecia) | [عرض فيديو Bilibili](https://www.bilibili.com/video/BV144Yo6oEz9/)

محرر نصوص خفيف الوزن للنظام التشغيلي Windows مبني على C++17 + FLTK. دعم علامات تبويب متعددة، فتح الملفات الكبيرة فوراً، ترميزات متعددة، وامتدادات سكريبت Lua. جديد: لوحة جانبية (شجرة الملفات/المخطط)، مربع حوار اختيار الخط، ظلال النوافذ؛ معاينة Markdown تعرض المستندات الضخمة على شكل أقسام.

> **نظام التشغيل المدعوم**: يدعم فقط **Windows x64 (64 بت)**. لا يوجد نسخة 32 بت، ولا تتم صيانة عبر المنصات
> (Linux/Mac/32 بت). تحتوي سكريبتات البناء على فحص 64 بت مدمج (انظر أعلى CMakeLists)، وسيُظهر خطأ عند تكوين 32 بت عن غير قصد.

## واجهة البرنامج

- معاينة Markdown
  - ![0.MD预览](0.Makrodnw预览.png)
- تبويبات متعددة
  - ![2.多标签页](1.多标签页.png)
- شريط حالة بسيط
  - ![2.极简状态](2.极简状态.png)
- الإعدادات
  - ![3.选项](3.选项.png)
- إعدادات الاختصارات
  - ![4.快捷键](4.快捷键.png)
- محادثة AI
  - ![5.AI对话](5.AI对话.png)
- وحدة تحكم Lua
  - ![6.Lua控制台](6.Lua控制台.gif)
- نصوص Lua
  - ![7.Lua脚本](7.Lua脚本.gif)


## البدء السريع

### متطلبات البيئة (البيئة الأساسية، باستثناء الكود المصدري والمكتبات الخارجية)

- Windows 10/11 x64 (إصدار 1803 أو أحدث، يتضمن أدوات التنزيل curl/tar المدمجة)
- **Visual Studio Build Tools 2022 / VS2022** (يتضمن حملة عمل سطح المكتب C++)
  - تحتوي السكريبت على `build/msvc_env.bat` **لاكتشاف تلقائي** MSVC و Windows SDK (لا يعتمد على
    vcvars/vswhere/موجه أوامر المطور، يعمل بنقرة مزدوجة في cmd العادي)، ويمكن أيضاً استخدام متغيرات البيئة
    `PECIA_MSVC_ROOT` / `PECIA_SDK_ROOT` لتحديد موضع تثبيت مخصص
- **CMake ≥ 3.16** (يجب أن يكون في PATH؛ المولّد هو **NMake Makefiles**، لا حاجة لـ Ninja)
- **سلسلة أدوات Rust** (≥ 1.85، يدعم edition 2024؛ لترجمة مكتبة FFI الخاصة بـ mmdr و RaTeX،
  عبر تثبيتها من https://rustup.rs، يتطلب الاتصال بالإنترنت في البناء الأول لجلب التبعيات من crates.io)
- **اتصال بالإنترنت** (يُطلب فقط في البناء الأول: 2_download.bat لتنزيل التبعيات + `cargo fetch` في 4_build_rust.bat
  لجلب الحزم)
- **git غير مطلوب** (التبعيات تُنزل عبر curl وتُفك عبر tar)

### البناء (الطريقة الموصى بها: نقرة مزدوجة واحدة)

```bat
REM بعد تنزيل الكود المصدري، فقط انقر نقرة مزدوجة على هذا السكريبت.
REM يعمل بالترتيب: 1 فحص سلسلة الأدوات ← 2 تنزيل 6 مكتبات خارجية ← 3 تطبيق الرقع ←
REM 4 cargo fetch + ترجمة مكتبتين Rust FFI ← 5 ترجمة FLTK + CMake ترجمة حزمة Pecia ←
REM تشغيل 26 اختباراً.
REM يعتمد وقت البناء الأول على سرعة الإنترنت والجهاز؛ بعدها يُعاد استخدام الكاش، والترجمة التدريجية + الاختبارات تستغرق حوالي 1-2 دقيقة.
build\full.bat
```

> Step-by-step build scripts, tests and packaging: see `build/build_readme.md`. Release process: see `docs/RELEASE_CHECKLIST.md`. التصميم المعماري (نموذج العمليات/الطبقات/الأنظمة الفرعية): انظر «نظرة عامة على الهندسة» في بداية `开发指南.md`.

## هيكل المجلدات

```
main/
├── CMakeLists.txt      قواعد البناء (مدخل CMake)
├── main.cpp            نقطة دخول البرنامج
├── README.md           هذه الوثيقة (تقديم المشروع/التنزيل/الواجهة/بداية البناء)
├── docs/               إصدارات README بلغات أخرى (README.<lang>.md) + لقطات شاشة الواجهة + intro.pptx
├── 目录结构说明.md      مواصفات المجلدات والوحدات (ضرورية للوارث)
├── 开发指南.md         سير التطوير/الإصدار، نظرة عامة على الهندسة وقائمة فحص ما قبل الالتزام
├── AGENTS.md           اصطلاحات هندسية (المساهمون/قواعد الذكاء الاصطناعي)
├── HISTORY.md          سجل الإصدارات (أضف قسم إصدار جديد في الأعلى عند الإصدار؛ أبِق المحتوى القديم دون تغيير)
├── LICENSE             نص AGPL-3.0 الرسمي الحرفي (UTF-8، بدون BOM؛ لا يجوز تعديل الشروط)
├── THIRD-PARTY-NOTICES.md  إشعارات المكتبات الخارجية (الاسم/الإصدار/الترخيص/عدد التصحيحات)
├── .gitignore          قواعد تجاهل نظام إدارة الإصدارات
├── .gitattributes      قواعد نهايات الأسطر وسمات النص
├── core/               المنطق الأساسي النقي (بدون واجهة مستخدم)
├── editor/             عناصر تحكم المحرر
├── ui/                 النوافذ/مربعات الحوار/شريط الأدوات
├── LuaTool/            أداة PeciaLua المستقلة (نافذة/نقطة دخول/خادم أنبوب)
├── AIChat/             أداة PeciaAIChat المستقلة (نافذة/نقطة دخول)
├── script/             محرك سكريبت Lua
├── DOCS_MANIFEST.md    قائمة الوثائق (للتحقق بعد تغيير الكود، بوابة test_docs؛ للتطوير فقط، لا تُرسل مع exe)
├── image/              موارد الصور (pecia.ico / ai_bot.ico / lua.ico …)
├── test/               كود اختبارات الوحدة
├── lang/               ملفات اللغة (16 × .ini)
├── theme/              ملفات ألوان السمة (light.ini/dark.ini/cream.ini/mint.ini/ice.ini)
├── example/            أمثلة الوثائق (عرض تأثيرات Markdown: مخططات Mermaid، صيغ LaTeX، صور، إلخ)
├── patches/            أرشيف تعديلات مكتبات .thirdparty (تُطبّق تلقائياً أثناء البناء)
├── mdview/             معاينة Markdown (لوحة المعاينة/عرض HTML، تتضمن رؤوس mmdr FFI)
└── build/              سكريبتات البناء وملفات مصدر مساعدة للبناء
```

## المكدس التقني

| المكون | الإصدار | المستودع |
|---|---|---|
| C++ | C++17 | — |
| FLTK | 1.4.5 | https://github.com/fltk/fltk |
| Lua | 5.5.1 | https://github.com/lua/lua |
| PCRE2 | 10.47 | https://github.com/PCRE2Project/pcre2 |
| md4c | 2026-09-11 (10fa4f44) | https://github.com/mity/md4c |
| litehtml | 0.10 | https://github.com/litehtml/litehtml |
| stb | 2026-08-02 (2c980bb5) | https://github.com/nothings/stb |
| mmdr (mermaid-rs-renderer) | 0.3.1 | crates.io (cargo) |
| RaTeX (تنسيق الصيغ، مدمج) | 0.1.14 | crates.io (cargo) |
| Rust | ≥ 1.85 | https://rustup.rs |
| البناء | CMake + NMake Makefiles + MSVC | — |

> ملاحظة: يمكنك إجراء تعديلات محلية على كود المكتبات الخارجية؛ يجب أرشفة الملفات المعدلة بالكامل في
> `main/patches/<اسم المكتبة>/` (تُغطى تلقائياً بواسطة 3_patch.bat أثناء البناء)، لا تُسجل فقط النصوص غير الكاملة.
> عند الترقية، اجلب الإصدار الجديد من المستودعات أعلاه وأعد تطبيق الرقع (انظر build/FLTK_PATCHES.md كمثال).
> يدمج mmdr مكتبة resvg 0.47 (محول SVG الوحيد في المشروع: تتحول مخططات mermaid وصيغ LaTeX إلى PNG بواسطته)؛ تنتج RaTeX نصوص SVG فقط ولا تعتمد على resvg مباشرة.

---

## الترخيص

[GNU AGPL-3.0](../../LICENSE).

تراخيص المكونات الخارجية (FLTK/Lua/PCRE2/md4c/litehtml/stb و حزم Rust)،
وصف التعديلات (الرقع) والتزامات التوزيع موثقة في [THIRD-PARTY-NOTICES.md](../../THIRD-PARTY-NOTICES.md).
