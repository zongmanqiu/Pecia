> **🌐 Languages**: [中文](../../README.md) | [English](README.en.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Deutsch](README.de.md) | [Français](README.fr.md) | [Español](README.es.md) | [Русский](README.ru.md) | [Português-BR](README.pt-BR.md) | [Italiano](README.it.md) | [繁體中文](README.zh-TW.md) | [العربية](README.ar.md) | [हिन्दी](README.hi.md) | [Indonesia](README.id.md) | [Türkçe](README.tr.md) | [Tiếng Việt](README.vi.md)

# Pecia — 극소 텍스트 편집기

---

**[⬇️ 최신 버전 다운로드 (1.1.1) (Gitee)](https://gitee.com/qiuzongman/pecia/releases/download/1.1.1/Pecia_x64_1.1.1.zip)** ｜ **[⬇️ 최신 버전 다운로드 (1.1.1) (GitHub)](https://github.com/zongmanqiu/Pecia/releases/download/1.1.1/Pecia_x64_1.1.1.zip)** ｜ [Gitee 저장소](https://gitee.com/qiuzongman/pecia) ｜ [GitHub 미러](https://github.com/zongmanqiu/Pecia) | [Bilibili 데모](https://www.bilibili.com/video/BV144Yo6oEz9/)

C++17 + FLTK 기반 Windows 경량 텍스트 편집기. 멀티 탭, 대용량 파일 즉시 열기, 다중 인코딩 지원, Lua 스크립트 확장. 새 기능: 사이드 패널(파일 트리/개요), 글꼴 선택 대화상자, 창 그림자. Markdown 미리보기는 초대형 문서를 분할 렌더링합니다.

> **플랫폼 목표**: **Windows x64(64비트)**만 지원합니다. 32비트 버전은 제공하지 않으며, 크로스 플랫폼(Linux/mac/32비트) 지원도 더 이상 유지하지 않습니다. 빌드 스크립트에 64비트 검증이 내장되어 있습니다(CMakeLists 상단 참조). 32비트로 잘못 설정하면 즉시 오류가 발생합니다.

## 소프트웨어 화면

- Markdown 미리보기
  - ![0.MD预览](0.Makrodnw预览.png)
- 멀티 탭
  - ![2.多标签页](1.多标签页.png)
- 미니멀 상태 표시줄
  - ![2.极简状态](2.极简状态.png)
- 옵션 설정
  - ![3.选项](3.选项.png)
- 단축키 설정
  - ![4.快捷键](4.快捷键.png)
- AI 채팅
  - ![5.AI对话](5.AI对话.png)
- Lua 콘솔
  - ![6.Lua控制台](6.Lua控制台.gif)
- Lua 스크립트
  - ![7.Lua脚本](7.Lua脚本.gif)


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
REM 단위 테스트 26개 실행.
REM 최초 소요 시간은 네트워크와 컴퓨터 사양에 따라 다릅니다; 이후 캐시를 재사용하여
REM 증분 컴파일 + 테스트 시 약 1-2분 소요.
build\full.bat
```

> Step-by-step build scripts, tests and packaging: see `build/build_readme.md`. Release process: see `docs/RELEASE_CHECKLIST.md`. 아키텍처 설계(프로세스 모델/레이어링/서브시스템)는 `开发指南.md`의 시작 부분「아키텍처 개요」를 참조하세요.

## 디렉토리 구조

```
main/
├── CMakeLists.txt      빌드 규칙(CMake 진입점)
├── main.cpp            프로그램 진입점
├── README.md           본 문서(프로젝트 소개/다운로드/인터페이스/빌드 입문)
├── docs/               다른 언어 README(README.<lang>.md) + 인터페이스 스크린샷 + intro.pptx
├── 目录结构说明.md      디렉토리 및 모듈 사양(인수 인계 시 필독)
├── 开发指南.md         개발/릴리스 절차, 아키텍처 개요 및 커밋 전 체크리스트
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
├── image/              이미지 리소스(pecia.ico / ai_bot.ico / lua.ico …)
├── test/               단위 테스트 소스 코드
├── lang/               언어 파일(16 × .ini)
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
> mmdr은 resvg 0.47을 내장하고 있습니다(프로젝트 유일의 SVG 래스터라이저: mermaid 다이어그램과 LaTeX 수식 SVG 모두 이로썸 PNG로 래스터화됩니다); RaTeX는 SVG 문자열만 생성하며 resvg에 직접 의존하지 않습니다.

---

## 라이선스

[GNU AGPL-3.0](../../LICENSE).

서드파티 구성 요소(FLTK/Lua/PCRE2/md4c/litehtml/stb 및 Rust crate)의 라이선스,
수정(patch) 설명 및 배포 의무는 [THIRD-PARTY-NOTICES.md](../../THIRD-PARTY-NOTICES.md)를 참조하세요.
