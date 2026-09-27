> **🌐 Languages**: [中文](../README.md) | [English](README.en.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Deutsch](README.de.md) | [Français](README.fr.md) | [Español](README.es.md) | [Русский](README.ru.md) | [Português-BR](README.pt-BR.md) | [Italiano](README.it.md) | [繁體中文](README.zh-TW.md) | [العربية](README.ar.md) | [हिन्दी](README.hi.md) | [Indonesia](README.id.md) | [Türkçe](README.tr.md) | [Tiếng Việt](README.vi.md)

# Pecia — محرر نصوص بسيط للغاية

---

**[⬇️ تنزيل أحدث إصدار (1.0.4)](https://gitee.com/qiuzongman/pecia/releases/download/1.0.4/Pecia_x64_1.0.4.zip)**

محرر نصوص خفيف الوزن للنظام التشغيلي Windows مبني على C++17 + FLTK. دعم علامات تبويب متعددة، فتح الملفات الكبيرة فوراً، ترميزات متعددة، وامتدادات سكريبت Lua.

> **نظام التشغيل المدعوم**: يدعم فقط **Windows x64 (64 بت)**. لا يوجد نسخة 32 بت، ولا يُooter upkeep عبر المنصات
> (Linux/Mac/32 بت). تحتوي سكريبتات البناء على فحص 64 بت مدمج (انظر أعلى CMakeLists)، وسيُظهر خطأ عند تكوين 32 بت عن غير قصد.

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
REM ي executing بالترتيب: 1 فحص سلسلة الأدوات ← 2 تنزيل 6 مكتبات خارجية ← 3 تطبيق الرقع ←
REM 4 cargo fetch + ترجمة مكتبتين Rust FFI ← 5 ترجمة FLTK + CMake ترجمة حزمة Pecia ←
REM تشغيل 14 اختباراً.
REM يعتمد وقت البناء الأول على سرعة الإنترنت والجهاز؛ بعدها يُعاد استخدام الكاش، والترجمة التدريجية + الاختبارات تستغرق حوالي 1-2 دقيقة.
build\full.bat
```

### البناء (خطوة بخطوة، خمسة سكريبتات مرقمة)

السكريبتات المرقمة في `main\build\` لها مهمة واحدة فقط، ويمكن إعادة تشغيل أي خطوة بشكل منفصل:

```bat
REM ① فحص للقراءة فقط: يتحقق من cmake/cargo/cl/nmake/rc/tar/curl، يجب أن يكون Rust host هو msvc
build\1_check_env.bat

REM ② تنزيل + فك ضغط المكتبات الخارجية إلى .thirdparty/ (幂等 — يتجاوز إذا كان موجوداً بالفعل)
build\2_download.bat

REM ③ تطبيق الرقع بشكل اتجاهي من main/patches/ إلى .thirdparty/ (幂等)
build\3_patch.bat

REM ④ cargo fetch لجلب تبعيات Rust + cargo build --release لترجمة مكتبتين FFI ثابتتين
REM   (هذه الخطوة dễ bị نسيان: Rust أيضاً يحتاج ترجمة، لا يُستخدم كما هو)
build\4_build_rust.bat

REM ⑤ ترجمة FLTK + حزمة Pecia الثلاثية، وتشغيل ctest
build\5_build_pecia.bat

REM ★ المدخل الرئيسي = ①→②→③→④→⑤ (يعادل النقر المزدوج على full.bat)
build\full.bat
```

> عند تغيير الكود فقط وإعادة الترجمة، فقط انقر نقرة مزدوجة على `5_build_pecia.bat` — سيقوم أولاً بمزامنة الرقع
> بنفسه، وحدد حسب الحاجة ما إذا كان يجب استدعاء `4_build_rust.bat`.

> المكتبات الخارجية (FLTK/Lua/PCRE2/md4c/litehtml/stb) تُنزل تلقائياً عبر `2_download.bat`
> حسب العناوين في `build/deps.txt` إلى `.thirdparty/` في جذر المشروع؛
> يمكن تعديل العناوين يدوياً (deps.txt). مكتبات Rust (mmdr/RaTeX/resvg وغيرها) تُجلب تلقائياً عبر cargo
> من crates.io (الإصدارات مقفلة في Cargo.lock الخاص بـ FFI crate).
> تعديلات غلاف FFI التي كتبناها محفوظة في `main/patches/` وترفق مع الكود المصدري.

### تشغيل الاختبارات

سكريبت البناء يشغل اختبارات الوحدة تلقائياً (ctest، بما في ذلك فحsv اتساق الوثائق test_docs). التشغيل المنفصل:

```bat
REM مجلد البناء في جذر المشروع temp\cmake_build\، الملفات التنفيذية في جذر المشروع build\
ctest --test-dir ..\..\temp\cmake_build --output-on-failure
```

> يمكنك أيضاً تشغيل `build\5_build_pecia.bat` مباشرة، فإنه بعد الانتهاء من الترجمة يشغل ctest تلقائياً.
> **الاختبارات هي بوابة البناء**: أي اختبار فاشل ← السكريبت `exit /b 1`، و`full.bat` يتوقف فوراً،
> ولن يظهر "اكتمل الكل". إذا أردت الترجمة السريعة بدون اختبارات ضع `PECIA_SKIP_TESTS=1`.
> (السكريبتات عبارة عن ملفات دفع Windows، يجب حفظها بسطور **CRLF**، وإلا سيفشل cmd في التحليل.)

### التعبئة والإصدار

```bat
build\pack.bat
```

يُخرج إلى جذر المشروع `release\` (مجلد `release\<الإصدار>\` وملف `Pecia-<الإصدار>.zip`،
مرفق مع LICENSE و THIRD-PARTY-NOTICES.md)، لا يتم تخزين أي منتجات إصدار داخل `main/`.

---

## هيكل المجلدات

```
main/
├── CMakeLists.txt      قواعد البناء (مدخل CMake)
├── main.cpp            نقطة دخول البرنامج
├── README.md           هذه الوثيقة (تتضمن نظرة عامة على البنية/stack التقني/تعليمات البناء)
├── docs/               إصدارات README بلغات أخرى (README.<lang>.md) + intro.pptx
├── 目录结构说明.md      مواصفات المجلدات والوحدات (ضرورية للوارث)
├── 开发指南.md         سير التطوير/الإصدار وقائمة فحص ما قبل الالتزام
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
├── image/              موارد الصور (icon/ أيقونة التطبيق)
├── test/               كود اختبارات الوحدة
├── lang/               ملفات اللغة (en.ini/zh-CN.ini)
├── theme/              ملفات ألوان السمة (light.ini/dark.ini/cream.ini/mint.ini/ice.ini)
├── example/            أمثلة الوثائق (عرض تأثيرات Markdown: مخططات Mermaid، صيغ LaTeX، صور، إلخ)
├── patches/            أرشيف تعديلات مكتبات .thirdparty (تُطبّق تلقائياً أثناء البناء)
├── mdview/             معاينة Markdown (لوحة المعاينة/عرض HTML، تتضمن رؤوس mmdr FFI)
└── build/              سكريبتات البناء وملفات مصدر مساعدة للبناء
```

## stack التقني

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

---

## نظرة عامة على البنية

> يصف هذا القسم البنية والتدفق الحقيقي **للكود المصدري الحالي** (C++17 + FLTK 1.4.5).
> مواصفات المجلدات والوحدات موثقة في《目录结构说明.md》.

### أولاً: البرامج التنفيذية ونموذج العمليات

يتم إصدار المنتج كـ **برنامج رئيسي + أداة مستقلة**، يتقاسم الثلاثة كود core/ui الكبير:

| البرنامج التنفيذي | نقطة الدخول | المهمة | مثيل واحد |
|---|---|---|---|
| `Pecia.exe` | `main.cpp` | محرر النصوص الرئيسي | يمكن تشغيل عدة نسخ (`PECIA_POS` متسلسل) |
| `PeciaLua.exe` | `LuaTool/main_lua.cpp` | وحدة تحكم Lua المستقلة، تشغيل السكريبتات على المستند الرئيسي | نعم (`MUTEX_LUA_TOOL`) |
| `PeciaAIChat.exe` | `AIChat/main_ai.cpp` | نافذة محادثة AI المستقلة، قراءة تحديد المستند الرئيسي | نعم (`MUTEX_AI_CHAT`) |

يتواصل الثلاثة عبر **أنابيب مسماة** (شكل الإطار في `core/PipeProtocol.h`: `4 bytes tag + u32 little-endian length + حمولة`). البرنامج الرئيسي يشغّل `LuaPipeServer`، و`listenLoop()` يستمع في خلفية الخيط على الأنبوب لكل معرف عملية `\\.\pipe\pecia-lua-<pid>`؛ عند استلام طلب يوزّعه عبر `Fl::awake` إلى خيط الواجهة للتنفيذ، ثم يكتب إطار OK/ERR. `PeciaLua` / `PeciaAIChat` يُشغّلان بواسطة `launchTool()` في البرنامج الرئيسي، يتمرر اسم الأنبوب واللغة عبر سطر الأوامر.

### ثانياً: الطبقات واتجاه الاعتماد

اتجاه الاعتماد الإلزامي (محدد أيضاً في《目录结构说明.md》):

```
core  ←  editor  ←  ui
core  ←  script  ←  ui
test  يمكن أن تعتمد على أي طبقة، لكنها تختبر المنطق النقي فقط (بدون اعتماد على GUI)
```

- **`core/` (منطق نقي، ممنوع FLTK/UI)**: Config، CrashReport، I18n، Theme، SearchCore، EncodingCore، FileManager، ShortcutCore، OpLog، AiApiClient (خيط HTTP في الخلفية)، PipeClient، UiBridge (واجهة عكس الاعتماد).
- **`editor/`**: Document (النموذج، تغليف Fl_Text_Buffer + ملف IO + ترميز)، Editor (امتداد Fl_Text_Editor: تمييز الأسطر/روابط URL/المسافات الفارغة/المسافة التلقائية).
- **`ui/` (الطبقة الوحيدة التي يمكنها new Fl_Window / فتح مربعات الحوار)**: MainWindow والملفات `MainWindow_*.cpp` المقسمة حسب الوظيفة، مربعات الحوار، جداول القوائم، اختصارات لوحة المفاتيح، توصيل لوحة المعاينة.
- **`script/`**: LuaEngine (ليس صندوقاً محكماً — المكتبة القياسية مفتوحة بالكامل، `lua_api.txt` يصرح بوضوح بنموذج الثقة هذا؛ مع buffer عمل + rex/win API)، ScriptManager (مسح/معلومات وصفية/folder.ini)، LuaParamParser (تصريح --!param).
- **`mdview/`**: معاينة Markdown (md4c→HTML → عرض litehtml؛ mermaid/LaTeX تُعرض داخلية عبر Rust FFI)؛ `mmdr_ffi` / `ratex_ffi` ملفات بناء ثابتة.

**`UiBridge`**: الاتصال الوحيد بين core والواجهة — `core/UiBridge.h` يحدد الدوال المجردة `message()/confirm()`؛ `ui/UiBridge.cpp` يوفر التنفيذ الفعلي ويُحقن عبر `setUiBridge()`. هكذا `Document`/`FileManager` (core) عند طلب شيء من المستخدم يتصل فقط بـ `uiBridge()->confirm(...)`, لا يعتمد على أي فئة محاور FLTK، مما يجعل النواحة قابلة للاختبار.

### ثالثاً: الإعدادات / التدويل / معالجة الأعطال

- **الإعدادات**: `settings.ini` (بجانب ملف exe)، `core/Config.cpp` للقراءة والكتابة؛ آلياً لائحة تصاريح الكتابة ل.protected المفاتيح الحساسة في إعدادات AI. `recent file.ini` لسجل الملفات الأخيرة.
- **التدويل**: `lang/en.ini`، `lang/zh-CN.ini`، `core/I18n.cpp` للتحميل؛ يجب أن تتطابق مفاتيح الملفين (فحص `test_docs`).
- **معالجة الأعطال**: `core/CrashReport.cpp` مشترك بين التنفيذات الثلاثة؛ يكتب إلى `temp/` بجانب exe (dmp + سجل الاستacks + آخر أسطر من سجل العمليات)، يحتفظ بآخر 3 ملفات dmp، يُنظف بعد 7 أيام.

### رابعاً: نقاط مهمة لنظامي التحرير / السكريبت / المعاينة

- **تحرير متعدد العلامات**: `MainWindow` يدير `m_tabsList` (علامة تبويب = مجموعة editor+doc)، عند التبديل/الإغلاق أولاً `buffer(nullptr)` ثم حذف doc، لتجنب وصول استدعاء FLTK إلى buffer تم تدميره.
- **تنفيذ السكريبتات**: `LuaEngine` يشغّل السكريبتات على **لقطة المستند** (ليس صندوقاً محكماً، المكتبة القياسية مفتوحة بالكامل؛ يمكن الوصول عبر الأنبوب المسماة، والذي محدود بصلاحيات المستخدم الحالي DACL)، النتائج تُكتب عبر `applyDocumentSnapshot`. قائمة السكريبتات من `script/scripts/**` (تتضمن `.lua` + ترجمة أسماء `folder.ini`)، تُنسخ إلى `build/script/` أثناء البناء.
- **معاينة Markdown**: التحرير ← `ui/MainWindow_preview.cpp`؛ `mdview/preprocess` يحوّل مخرجات md4c إلى HTML، يستخرج صيغ ```` ```math ```` ويرسلها إلى RaTeX (SVG)، وكتل mermaid يرسلها إلى mmdr (PNG)؛ `mdview/container` هو م扩散 واجهة `document_container` لـ litehtml؛ `PreviewPanel` مسؤول عن سلسلة العرض. التحديث التلقائي حسب الإعداد (1 ثانية/5 ثوانٍ/10 ثوانٍ/30 ثانية).

### خامساً: البناء والاختبارات

- **خمسة سكريبتات مرقمة + مدخل رئيسي** (`main/build/`، مهمة واحدة فقط، يمكن إعادة تشغيل أي خطوة بشكل منفصل):
  - `full.bat`：**المدخل الرئيسي** (نقرة مزدوجة جاهزة)، ي瓶子 بالترتيب ①→②→③→④→⑤.
  - `1_check_env.bat`：فحص سلسلة الأدوات للقراءة فقط (cmake/cargo/cl/nmake/rc/tar/curl；
    يجب أن يكون `rustc -vV` host هو `pc-windows-msvc`، وإلا لن يتمكن من ربط `.lib`).
  - `2_download.bat`：تنزيل + فك ضغط المكتبات الخارجية حسب `deps.txt` إلى `.thirdparty/` (幂等).
  - `3_patch.bat`：مزامنة الرقع (`main/patches/ → .thirdparty/` **اتجاه واحد** تغطية قوية).
  - `4_build_rust.bat`：`cargo fetch` لجلب تبعيات Rust ← `cargo build --release`
    لترجمة `mermaid_ffi.lib` / `ratex_ffi.lib` (**Rust أيضاً يحتاج ترجمة**، لا تفوت هذه الخطوة).
  - `5_build_pecia.bat`：يحدد حسب الطوابع الزمنية ما إذا كان FLTL / مكتبتا Rust FFI تحتاج إعادة ترجمة ← إعداد NMake و
    **ترجمة تدريجية** (يُعاد استخدام كاش `temp/cmake_build`؛ فقط إذا أنشأه مولّد آخر يتم مسحه وإعادة البناء)
    ← تشغيل ctest تلقائياً.
  - `_common.bat`：متغيرات المسار المشتركة بين السكريبتات، مسارات `tar`/`curl` المطلقة، تحميل بيئة MSVC،
    وروتين `:fetch_rust_deps`.
- `build/msvc_env.bat`：اكتشاف MSVC و Windows SDK وضبط `PATH`/`INCLUDE`/`LIB`
    (يستخدم `vswhere.exe` أولاً لتحديد أي مسار تثبيت؛ مشترك بين جميع سكريبتات البناء).
- `build/pack.bat`：حزم محتويات `build/` إلى `release/<الإصدار>/`.
- `build/deps.txt`：قائمة تنزيل المكتبات الخارجية (`اسم_المكتبة=URL|ملف اختبار`، ASCII فقط — التعليقات العربية ستبتلع الأسطر التالية عند تحليل `for /f` بترميز GBK).
- **اختبارات الوحدة** (`test/`، تختبر النواحة المنطقية فقط)：search/encoding/config/document/match_highlight/param/docs/lua_engine (بما في ذلك rex تعبيرات منتظمة)/undo/script_manager/pipe/shortcut/ui_smoke/ui_tool_smoke إجمالي **14** هدفاً، ctest أخضر بالكامل + بوابة اتساق الوثائق `test_docs`.

> جميع التعديلات المحلية على كود المكتبات الخارجية تُحفظ في `main/patches/` وتُغطى تلقائياً على `.thirdparty/` أثناء البناء؛ لا تُغطى بالعكس (انظر AGENTS.md). تحذيرات ترجمة المكتبات الخارجية لا تُحتسب في بوابة "تحذيرات صفرية /W4" للبرنامج الرئيسي (البوابة ت规束 فقط كود `main/` من الدرجة الأولى).

## الترخيص

[GNU AGPL-3.0](../LICENSE).

تراخيص المكونات الخارجية (FLTK/Lua/PCRE2/md4c/litehtml/stb و حزم Rust)،
وصف التعديلات (الرقع) والتزامات التوزيع موثقة في [THIRD-PARTY-NOTICES.md](../THIRD-PARTY-NOTICES.md).
