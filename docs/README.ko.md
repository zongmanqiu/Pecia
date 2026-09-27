> **🌐 Languages**: [中文](../README.md) | [English](README.en.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Deutsch](README.de.md) | [Français](README.fr.md) | [Español](README.es.md) | [Русский](README.ru.md) | [Português-BR](README.pt-BR.md) | [Italiano](README.it.md) | [繁體中文](README.zh-TW.md) | [العربية](README.ar.md) | [हिन्दी](README.hi.md) | [Indonesia](README.id.md) | [Türkçe](README.tr.md) | [Tiếng Việt](README.vi.md)

# Pecia — 극소 텍스트 편집기

---

**[⬇️ 최신 버전 다운로드 (1.0.3)](https://gitee.com/qiuzongman/pecia/releases/download/1.0.3/Pecia_x64_1.0.3.zip)**

C++17 + FLTK 기반 Windows 경량 텍스트 편집기. 멀티 탭, 대용량 파일 즉시 열기, 다중 인코딩 지원, Lua 스크립트 확장.

> **플랫폼 목표**: **Windows x64(64비트)**만 지원합니다. 32비트 버전은 제공하지 않으며, 크로스 플랫폼(Linux/mac/32비트) 지원도 더 이상 유지하지 않습니다. 빌드 스크립트에 64비트 검증이 내장되어 있습니다(CMakeLists 상단 참조). 32비트로 잘못 설정하면 즉시 오류가 발생합니다.

## 빠른 시작

### 환경 요구사항(소스 코드 및 서드파티 라이브러리 외)

- Windows 10/11 x64(1803 이상, 내장 curl/tar 다운로드 도구)
- **Visual Studio Build Tools 2022 / VS2022**(C++ 데스크톱 워크로드 포함)
  - 스크립트에 `build/msvc_env.bat`이 포함되어 있어 MSVC와 Windows SDK를 **자동 탐지**합니다(vcvars/vswhere/개발자 명령 프롬프트에 의존하지 않으므로 일반 cmd에서 더블클릭 가능). `PECIA_MSVC_ROOT` / `PECIA_SDK_ROOT` 환경 변수로 사용자 지정 설치 위치도 지정할 수 있습니다.
- **CMake ≥ 3.16**(PATH에 있어야 하며, 생성기는 **NMake Makefiles** 사용. Ninja는 불필요)
- **Rust 도구 체인**(≥ 1.85, edition 2024 지원; mmdr와 RaTeX의 FFI 렌더링 라이브러리 컴파일에 사용. https://rustup.rs 에서 설치. 최초 빌드 시 crates.io에서 의존성을 가져오기 위해 네트워크 연결 필요)
- **네트워크 연결**(최초 빌드에만 필요: 2_download.bat으로 의존성 다운로드 + 4_build_rust.bat의 `cargo fetch`로 crate 가져오기)
- **git 불필요**(의존성은 curl로 다운로드 + tar로 압축 해제)

### 빌드(권장: 더블클릭 한 번으로)

```bat
REM 소스 코드를 다운로드한 후 이 스크립트 하나만 더블클릭하면 됩니다.
REM 순서대로 실행됩니다: 1 도구 체인 검사 → 2 서드파티 라이브러리 6개 다운로드 → 3 패치 적용 →
REM 4 cargo fetch + Rust FFI 2개 컴파일 → 5 FLTK 컴파일 + CMake로 Pecia 3종 세트 컴파일 →
REM 단위 테스트 14개 실행.
REM 최초 소요 시간은 네트워크와 컴퓨터 사양에 따라 다릅니다; 이후 캐시를 재사용하여
REM 증분 컴파일 + 테스트 시 약 1-2분 소요.
build\full.bat
```

### 빌드(단계별, 5개 번호 스크립트)

`main\build\` 아래의 번호 스크립트는 단일 역할을 가지며 각 단계를 개별적으로 다시 실행할 수 있습니다:

```bat
REM ① 읽기 전용 검사: cmake/cargo/cl/nmake/rc/tar/curl 확인, Rust host는 mscv여야 함
build\1_check_env.bat

REM ② 서드파티 라이브러리를 .thirdparty/에 다운로드 + 압축 해제(멱등성, 이미 존재하면 건너뜀)
build\2_download.bat

REM ③ main/patches/를 .thirdparty/에 일방향으로 적용(멱등성)
build\3_patch.bat

REM ④ cargo fetch로 Rust 의존성 가져오기 + cargo build --release로 FFI 정적 라이브러리 2개 컴파일
REM   (이 단계를 놓치기 쉬움: Rust도 컴파일이 필요하며 바로 사용할 수 없음)
build\4_build_rust.bat

REM ⑤ FLTK + Pecia 3종 세트 컴파일 및 ctest 실행
build\5_build_pecia.bat

REM ★ 전체 진입점 = ①→②→③→④→⑤(더블클릭 full.bat과 동일)
build\full.bat
```

> 코드를 수정하여 다시 컴파일해야 할 때는 `5_build_pecia.bat`을 더블클릭하면 됩니다——
> 자체적으로 먼저 패치를 동기화하고, 필요에 따라 `4_build_rust.bat` 호출 여부를 결정합니다.

> 서드파티 라이브러리(FLTK/Lua/PCRE2/md4c/litehtml/stb)는 `2_download.bat`에 의해
> `build/deps.txt`의 주소를 기준으로 프로젝트 루트 `.thirdparty/`에 자동 다운로드됩니다.
> 주소는 수동으로 편집할 수 있습니다(deps.txt). Rust 라이브러리(mmdr/RaTeX/resvg 등)는
> cargo에 의해 crates.io에서 자동으로 가져옵니다(버전은 FFI crate의 Cargo.lock에 고정).
> 수정한 FFI 셸 등은 `main/patches/`에 보관되며 소스 코드와 함께 업로드됩니다.

### 테스트 실행

빌드 스크립트가 자동으로 단위 테스트를 실행합니다(ctest, 문서 일관성 검사 test_docs 포함). 개별 실행:

```bat
REM 빌드 디렉토리는 프로젝트 루트 temp\cmake_build\, 실행 파일은 프로젝트 루트 build\
ctest --test-dir ..\..\temp\cmake_build --output-on-failure
```

> `build\5_build_pecia.bat`을 직접 실행할 수도 있으며, 컴파일 완료 후 자동으로 ctest를 실행합니다.
> **테스트는 빌드 게이트입니다**: 테스트 실패 시 → 스크립트 `exit /b 1`, `full.bat`이 즉시 중지되며
> "모두 완료" 메시지가 표시되지 않습니다. 빠른 컴파일만 원하고 테스트를 건너뛰려면 `PECIA_SKIP_TESTS=1`을 설정하세요.
> (스크립트는 Windows 배치 파일로, **CRLF** 줄 바꿈으로 저장해야 합니다. 그렇지 않으면 cmd에서 파싱 오류가 발생합니다.)

### 패키징 및 배포

```bat
build\pack.bat
```

프로젝트 루트 `release/`에 출력됩니다(`release/<버전>/` 디렉토리와 `Pecia-<버전>.zip`,
LICENSE 및 THIRD-PARTY-NOTICES.md 포함). `main/` 내에는 배포 산물을 저장하지 않습니다.

---

## 디렉토리 구조

```
main/
├── CMakeLists.txt      빌드 규칙(CMake 진입점)
├── main.cpp            프로그램 진입점
├── README.md           본 문서(아키텍처 개요/기술 스택/빌드 설명 포함)
├── docs/               다른 언어 README(README.<lang>.md) + intro.pptx
├── 目录结构说明.md      디렉토리 및 모듈 사양(인수 인계 시 필독)
├── 开发指南.md         개발/릴리스 절차와 커밋 전 체크리스트
├── AGENTS.md           엔지니어링 규약(협업자/AI 규칙)
├── HISTORY.md          버전 기록(릴리스 시 상단에 새 버전 섹션 추가, 기존 내용은 변경하지 않음)
├── LICENSE             AGPL-3.0 공식 원문 그대로(UTF-8, BOM 없음, 조항 수정 금지)
├── THIRD-PARTY-NOTICES.md  서드파티 라이브러리 고지(이름/버전/라이선스/패치 수)
├── .gitignore          버전 관리 무시 규칙
├── .gitattributes      줄 끝과 텍스트 속성 규칙
├── core/               핵심 순수 로직(UI 없음)
├── editor/             에디터 컨트롤
├── ui/                 창/다이얼로그/도구 모음
├── LuaTool/            PeciaLua 독립 도구(창/진입점/파이프 서버)
├── AIChat/             PeciaAIChat 독립 도구(창/진입점)
├── script/             Lua 스크립트 엔진
├── DOCS_MANIFEST.md    문서 목록(코드 수정 후 항목별 확인, test_docs 게이트; 개발 전용, exe에 포함되지 않음)
├── image/              이미지 리소스(icon/ 앱 아이콘)
├── test/               단위 테스트 소스 코드
├── lang/               언어 파일(en.ini/zh-CN.ini)
├── theme/              테마 색상 파일(light.ini/dark.ini/cream.ini/mint.ini/ice.ini)
├── example/            문서 예시(Markdown 렌더링 효과 표시: Mermaid 차트, LaTeX 수식, 이미지 등)
├── patches/            .thirdparty 라이브러리 소스 코드 수정 보관소(빌드 시 자동 덮어쓰기 적용)
├── mdview/             Markdown 미리보기(미리보기 패널/HTML 렌더링, mmdr FFI 헤더 포함)
└── build/              빌드 스크립트 및 빌드 보조 소스 코드
```

## 기술 스택

| 구성 요소 | 버전 | 저장소 |
|---|---|---|
| C++ | C++17 | — |
| FLTK | 1.4.5 | https://github.com/fltk/fltk |
| Lua | 5.5.1 | https://github.com/lua/lua |
| PCRE2 | 10.47 | https://github.com/PCRE2Project/pcre2 |
| md4c | 2026-09-11 (10fa4f44) | https://github.com/mity/md4c |
| litehtml | 0.10 | https://github.com/litehtml/litehtml |
| stb | 2026-08-02 (2c980bb5) | https://github.com/nothings/stb |
| mmdr (mermaid-rs-renderer) | 0.3.1 | crates.io (cargo) |
| RaTeX (수식 렌더링, 내장) | 0.1.14 | crates.io (cargo) |
| Rust | ≥ 1.85 | https://rustup.rs |
| 빌드 | CMake + NMake Makefiles + MSVC | — |

> 참고: 서드파티 라이브러리 소스 코드를 로컬에서 수정할 수 있습니다; 수정된 완전한 파일은
> `main/patches/<라이브러리명>/`에 보관해야 합니다(빌드 시 3_patch.bat에 의해 자동 덮어쓰기).
> 텍스트만 기록하는 것은 불완전합니다. 업그레이드 시 위의 저장소에서 새 버전을 가져온 후
> 패치를 다시 적용합니다(build/FLTK_PATCHES.md의 예시 참조).

---

## 아키텍처 개요

> 본 문서는 **현재 소스 코드**(C++17 + FLTK 1.4.5)의 실제 아키텍처와 데이터 흐름을 설명합니다.
> 디렉토리 및 모듈 사양은 《目录结构说明.md》을 기준으로 합니다.

### 1. 실행 프로그램 및 프로세스 모델

본 제품은 **메인 프로그램 1개 + 독립 도구 2개**로 배포되며, 세 프로그램 모두 core/ui 소스 코드를 공유합니다:

| 실행 프로그램 | 진입점 | 역할 | 단일 인스턴스 |
|---|---|---|---|
| `Pecia.exe` | `main.cpp` | 메인 텍스트 편집기 | 다중 실행 가능(`PECIA_POS` 캐스케이드) |
| `PeciaLua.exe` | `LuaTool/main_lua.cpp` | 독립 Lua 콘솔, 메인 문서에 스크립트 실행 | 예(`MUTEX_LUA_TOOL`) |
| `PeciaAIChat.exe` | `AIChat/main_ai.cpp` | 독립 AI 채팅 창, 메인 문서 선택 영역 읽기 | 예(`MUTEX_AI_CHAT`) |

세 프로그램은 **네임드 파이프**로 통신합니다(프레임 형식은 `core/PipeProtocol.h` 참조: `4바이트 태그 + u32 리틀엔디안 길이 + 페이로드`). 메인 프로그램이 `LuaPipeServer`를 시작하면, `listenLoop()`가 백그라운드 스레드에서 per-pid 파이프 `\\.\pipe\pecia-lua-<pid>`를 수신합니다. 요청을 받으면 `Fl::awake`로 UI 스레드에 디스패치하여 실행한 후 OK/ERR 프레임을 다시 작성합니다. `PeciaLua` / `PeciaAIChat`은 메인 프로그램의 `launchTool()`에 의해 시작되며, 명령줄로 파이프 이름과 언어가 전달됩니다.

### 2. 계층 및 의존성 방향

강제 의존성 방향(《目录结构说明.md》에서도 규정):

```
core  ←  editor  ←  ui
core  ←  script  ←  ui
test  모든 계층에 의존 가능하나 순수 로직만 테스트(GUI 의존 없음)
```

- **`core/` (순수 로직, FLTK/UI 사용 금지)**: Config, CrashReport, I18n, Theme, SearchCore, EncodingCore, FileManager, ShortcutCore, OpLog, AiApiClient(HTTP 백그라운드 스레드), PipeClient, UiBridge(의존성 역전 인터페이스).
- **`editor/`**: Document(모델, Fl_Text_Editor 래퍼 + 파일 IO + 인코딩), Editor(Fl_Text_Editor 확장: 행 하이라이트/URL/공백 문자/자동 들여쓰기).
- **`ui/` (Fl_Window 생성 / 다이얼로그 팝업이 가능한 유일한 계층)**: MainWindow 및 기능별로 분할된 `MainWindow_*.cpp`, 각 다이얼로그, 메뉴 테이블, 단축키, 미리보기 패널 배선.
- **`script/`**: LuaEngine(샌드박스가 아님——표준 라이브러리 전체 개방, `lua_api.txt`에 이 신뢰 모델이 명시; scratch buffer + rex/win API 포함), ScriptManager(스캔/메타정보/folder.ini), LuaParamParser(--!param 선언).
- **`mdview/`**: Markdown 미리보기(md4c→HTML → litehtml 렌더링; mermaid/LaTeX는 Rust FFI를 통해 프로세스 내 렌더링); `mmdr_ffi` / `ratex_ffi` 정적 링크 빌드 산물.

**`UiBridge`**: core와 UI의 유일한 연결 지점——`core/UiBridge.h`에 순수 가상 `message()/confirm()` 정의; `ui/UiBridge.cpp`에서 구체적 구현을 제공하고 `setUiBridge()`로 주입됩니다. 이렇게 하여 `Document`/`FileManager`(core)가 사용자에게 알릴 때 `uiBridge()->confirm(...)`만 호출하고 FLTK 다이얼로그 클래스에 의존하지 않으므로 핵심 로직을 테스트할 수 있습니다.

### 3. 설정 / 국제화 / 크래시 처리

- **설정**: `settings.ini`(exe와 같은 디렉토리), `core/Config.cpp`에서 읽기/쓰기; AI 설정 관련 민감한 키에 대한 쓰기 화이트리스트 메커니즘. `recent file.ini`에 최근 파일 기록.
- **국제화**: `lang/en.ini`, `lang/zh-CN.ini`, `core/I18n.cpp`에서 로드; 두 파일의 키 집합이 동일해야 합니다(`test_docs` 게이트에서 검증).
- **크래시 처리**: `core/CrashReport.cpp`를 3개 exe가 공유; exe와 같은 디렉토리의 `temp/`에 기록(dmp + 스택 로그 + 최근 작업 로그 끝부분). dmp는 최근 3개를 보관하며 7일 후 삭제.

### 4. 편집 / 스크립트 / 미리보기 하위 시스템 요점

- **멀티 탭 편집**: `MainWindow`가 `m_tabsList`(탭=에디터+문서 조합)를 관리합니다. 전환/닫을 때 먼저 `buffer(nullptr)` 설정 후 delete doc을 수행하여 FLTK 콜백이 이미 해제된 버퍼에 접근하는 것을 방지합니다.
- **스크립트 실행**: `LuaEngine`이 **문서 스냅샷**에 대해 스크립트를 실행합니다(샌드박스가 아니며 표준 라이브러리 전체 개방; 진입점은 네임드 파이프를 통해 접근 가능, 파이프는 현재 사용자 DACL로 제한). 결과는 `applyDocumentSnapshot`으로 다시 기록됩니다. 스크립트 메뉴는 `script/scripts/**`(`.lua` + `folder.ini` 이름 번역 포함)에서 가져오며, 빌드 시 `build/script/`에 복사됩니다.
- **Markdown 미리보기**: 편집기→`ui/MainWindow_preview.cpp`; `mdview/preprocess`가 md4c 출력을 HTML로 변환하고 ```` ```math ```` 수식을 RaTeX(SVG)에, mermaid 블록을 mmdr(PNG)에 전달합니다; `mdview/container`는 litehtml의 document_container 어댑터; `PreviewPanel`이 렌더링 파이프라인을 담당합니다. 미리보기 자동 새로고침은 단계별(1초/5초/10초/30초)로 작동합니다.

### 5. 빌드 및 테스트

- **5개 번호 스크립트 + 전체 진입점** (`main/build/`, 단일 역할, 각 단계를 개별적으로 다시 실행 가능):
  - `full.bat`: **전체 진입점**(더블클릭으로 사용), 순서대로 ①→②→③→④→⑤ 호출.
  - `1_check_env.bat`: 읽기 전용 도구 체인 검사(cmake/cargo/cl/nmake/rc/tar/curl; `rustc -vV`의 host가 `pc-windows-msvc`여야 하며, 그렇지 않으면 링크 가능한 `.lib`를 생성할 수 없음).
  - `2_download.bat`: `deps.txt` 기준으로 서드파티 라이브러리를 `.thirdparty/`에 다운로드 + 압축 해제(멱등성).
  - `3_patch.bat`: 패치 동기화(`main/patches/ → .thirdparty/` **일방향** 강제 덮어쓰기).
  - `4_build_rust.bat`: `cargo fetch`로 Rust 의존성 가져오기 → `cargo build --release`로 `mermaid_ffi.lib` / `ratex_ffi.lib` 컴파일(**Rust도 컴파일이 필요**, 이 단계를 놓치지 마세요).
  - `5_build_pecia.bat`: 타임스탬프로 FLTK / 두 개의 Rust FFI 재컴파일 필요 여부 판단 → NMake 구성 및 **증분** 컴파일(`temp/cmake_build` 캐시 재사용; 다른 생성기로 생성된 캐시인 경우에만 삭제 후 재구성) → 자동으로 ctest 실행.
  - `_common.bat`: 각 스크립트가 공유하는 경로 변수, `tar`/`curl` 절대 경로 해석, MSVC 환경 로드 및 `:fetch_rust_deps` 서브루틴.
- `build/msvc_env.bat`: MSVC와 Windows SDK를 탐지하여 `PATH`/`INCLUDE`/`LIB` 설정(`vswhere.exe`로 임의 설치 경로를 우선 탐지; 각 빌드 스크립트가 공유).
- `build/pack.bat`: `build/` 내용을 `release/<버전>/`에 패키징.
- `build/deps.txt`: 서드파티 라이브러리 다운로드 목록(`라이브러리명=URL|프로브 파일`, 순수 ASCII——한국어 주석은 `for /f`가 GBK로 파싱할 때 후속 라인을 잘라냄).
- **단위 테스트** (`test/`, 핵심 순수 로직만 테스트): search/encoding/config/document/match_highlight/param/docs/lua_engine( rex 정규식 포함)/undo/script_manager/pipe/shortcut/ui_smoke/ui_tool_smoke 총 **14**개 타겟, ctest 전부 통과 + `test_docs` 문서 일관성 게이트.

> 서드파티 라이브러리 소스 코드의 로컬 수정은 모두 `main/patches/`에 보관되며, 빌드 시 자동으로 `.thirdparty/`에 덮어씌워집니다. 반대 방향 덮어쓰기는 절대 하지 마세요(AGENTS.md 참조). 서드파티 라이브러리의 컴파일 경고는 메인 프로그램의 "경고 제로 /W4" 게이트에 포함되지 않습니다(게이트는 `main/`의 1차 소스 코드에만 적용).

## 라이선스

[GNU AGPL-3.0](../LICENSE).

서드파티 구성 요소(FLTK/Lua/PCRE2/md4c/litehtml/stb 및 Rust crate)의 라이선스,
수정(patch) 설명 및 배포 의무는 [THIRD-PARTY-NOTICES.md](../THIRD-PARTY-NOTICES.md)를 참조하세요.
