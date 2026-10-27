# LUNA Library V2

**PDF4QT 기반 Windows용 LUNA Library** — 2열 인터페이스 (왼쪽 폴더/PDF 목록, 오른쪽 PDF 본문)

This is a **public source-only build repository**. It does not store, synchronize or receive your PDF files, annotations, research notes, cloud credentials or local databases.

## 현재 Beta 개발 상태

Draft PR #7에서 HTML v0.9를 최종 UI 기준으로 통합 중입니다. 독립 문서 창은
임시 구조이며 최종 목표는 한 LUNA 창 안의 PDF 탭입니다. 별도
[`experiments/tab_host`](experiments/tab_host/README.md)는 Qt 컨테이너/Win32
연결을 비교하는 미승인 실험이며 포터블 프로그램에 포함하지 않습니다.

최근 기록 검사 실패는 Windows 레지스트리와 테스트 INI의 설정 형식 혼용을
수정한 뒤 통과했습니다. run 40447039043은 C++/네이티브 CTest 4개 통과 후
크롭 취소의 지연 삭제 검사에서 실패했습니다. 중첩 이벤트 루프에서 취소를
보내도록 검사를 수정했으며, 이후 v0.9 UI와 함께 Windows 재검증 중입니다.
아래의 과거 성공 기록이 현재 HEAD 전체의 성공을 뜻하지 않습니다.
베타 완료·탭 POC 통과·물리 Windows DPI 검증을 선언하지 않습니다.

## Windows EXE 만들기

1. [Actions → LUNA Library V2 - PDF4QT Windows Portable](https://github.com/MRYoonn/LUNA-Library-V2/actions/workflows/build-windows.yml) 페이지 열기.
2. 오른쪽 **Run workflow** → Branch **feature/windows-beta-v0.1** → 초록색 **Run workflow** 누르기.
3. 실행 기록에서 각 단계의 성공/실패를 확인하기. 실패하면 정확한 오류 메시지를 확인하고 수정한다.
4. 성공하면 해당 실행 기록 맨 아래 **Artifacts → LUNA-Windows-Portable** 다운로드.
5. ZIP 전체를 풀고 **LUNA.exe** 더블클릭. 사용자 문서 화질과 배율은 자신의 Windows에서 확인한다.

개발 변경은 아직 Draft PR이며 `main`에 병합하지 않았다. 이미 검증된 빌드를 받으려면
[읽기 툴바 포터블 ZIP](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/40282150131/artifacts/12634015827)을 다운로드하면 된다.
기존 [Editor 빌드](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/40258840696/artifacts/12623708384)도 보존했다.

초기 빌드는 PDF4QT의 C++/Qt/vcpkg 의존성 설치와 컴파일 때문에 오래 걸릴 수 있다. 실패 원인을 확인하고 수정한 뒤 필요한 검증만 다시 실행한다. 실제 PDF의 화질과 상호작용은 자동 시작 검사와 별도로 확인한다.

Windows 기본 포터블 빌드는 이후 검증을 통과했다.
[P0 검증 실행](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/40230666645)에서
컴파일·패키징·DLL 검사·실제 창 시작·합성 PDF 로딩이 통과했고,
[포터블 Artifact](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/40230666645/artifacts/12614056454)가 생성됐다.
이 산출물의 커밋은 `31a65be0fd2fc72636356044560faefc55eb5a68`이다.
첨부 디자인에 따른 후속 UI 변경과 주석 기능까지 검증됐다는 뜻은 아니다.

Editor 통합 빌드도 [Windows 실행40258840696](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/40258840696)에서
통과했다. 검증한 C++ 소스 커밋은 `574050a567e382a4738d9a38b657c1fbac2db659`이다.
네이티브 창/PDF 열기, 실제 Ctrl+K 검색 포커스, 라이브러리 회귀 검사,
형광펜·메모 생성, 다른 이름으로 저장, 닫기·디스크 재열기를 통과했다.
원본 합성 PDF 바이트와 저장한 주석 내용·기본 형광펜 색상도 확인했다.
Python 테스트는 로컬·Windows 각각 **31 PASS / 0 FAIL / 0 SKIP**,
Windows 네이티브 Qt CTest는 **1 PASS / 0 FAIL / 0 SKIP**이다.

[Qt 레이아웃 캡처](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/40258840696/artifacts/12624227683)는
실제 PNG 크기 1366×768·1920×1080을 자동 확인했다. 이후 접근 가능한 환경에서
ZIP의 공식 SHA256을 대조하고 이미지를 직접 검토했다. 이 이전 빌드에는 복잡한
플러그인 툴바가 노출되어 있으며, 아래 읽기 툴바 작업의 비교 기준으로 사용했다.
물리 모니터 및 Windows125%/150% 배율 검증을 대신하지 않는다.

## 동작 구조

- **독립 실행 파일:** `LUNA.exe` (별도 target)
- **PDF 엔진:** 고정 버전의 공개 PDF4QT 원본 (수정 금지)
- **LUNA UI:** `src/lunalibrary.cpp` / `src/lunalibrary.h`
- **적용:** `tools/apply_overlay.py` (업스트림 원본 소스 해시 확인 후 새로운 타깃만 추가)
- **보안:** `tools/preflight_public.py` 및 `tests/test_public_gate.py`
- **빌드:** `.github/workflows/build-windows.yml` (수동 실행, 읽기 전용)
- **집↔연구실 PDF 동기화:** 향후 로컬 컴퓨터와 별도 동기화 서비스만 사용하며 GitHub는 관여하지 않음.

## 개발 및 릴리스 검증

네이티브 UI 브랜치는 탐색 패널·논문 목록·PDF4QT Editor의 문서 위젯을 가로로 배치한다.
각 라이브러리 패널 접기, 집중 모드, `Ctrl+K` 파일명 검색, 실제 열람 기록과 즐겨찾기,
원본을 유지하는 PDF 복사 추가를 제공한다. 같은 이름의 파일은 덮어쓰지 않는다.
개인 경로·최근 열람·즐겨찾기·창 크기·패널 너비는 로컬 QSettings에만 저장한다.
스타 표시 또는 문서 목록의 컨텍스트 메뉴에서 즐겨찾기를 변경할 수 있다.
목록의 종이 아이콘은 일반 PDF 아이콘이며 실제 페이지 썸네일이라고 표시하지 않는다.
DOI·태그·페이지 보기·휴지통·다중 문서 탭은 아직 제공하지 않는다.

색상 토큰은 `src/lunalibrary.cpp`에서 네이티브 카드와
`src/lunalibrary.qss` 템플릿에 함께 적용한다. PDF 렌더러는 교체하지 않는다.
후속 Windows UI 검사에서는 임시 합성 라이브러리만 사용하며,
실제 Ctrl+K 키 입력·파일명 검색·즐겨찾기·최근 열람·패널 접기·목록을 통한 PDF 재열기·연속 보기 200%를 검사한다.
PDF4QT 환경설정의 기존 Ctrl+K 충돌을 해소했으며 환경설정은 Ctrl+Alt+O로 연다.
실제 Windows/QPA 창에서 기능 검사를 마친 뒤 같은 위젯 트리를 숨겨진 렌더 호스트에
옮겨 1366×768·1920×1080 크기로 캡처한다. PNG의 실제 크기도 검사하며,
CI 데스크톱에 의해 줄어든 이미지는 통과시키지 않는다.
이 Qt 레이아웃 캡처는 별도 `LUNA-UI-Synthetic` Artifact로 저장한다.
물리 디스플레이 해상도나 OS 배율 검사와는 구분한다.
P1 UI 기준 커밋 `7b55fd09ba50bcd38667a8205c2367fc29538344`는
[Windows 실행](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/40241381173)에서
컴파일·패키징·네이티브 기능·정확한 크기의 Qt 캡처 검사를 통과했다.
이 결과는 후속 Editor 변경의 검증을 대신하지 않는다.

Python 표준 라이브러리만 사용한다. 공개 소스 검사에서 생성 파일을 차단하므로
저장소 안에 PDF4QT 소스, 빌드 결과 또는 Python 바이트코드를 만들지 않는다.

```bash
python3 -B tools/preflight_public.py
python3 -B -m unittest discover -s tests -p 'test_*.py' -v
```

Windows 워크플로는 전체 PDF4QT 컴파일 전에 `tests/native_dialog`를 먼저 빌드하여
실제 Qt 파일 대화상자 CTest를 실행한다. 포커스된 파일명 입력, Windows 경로 대소문자,
같은 디렉터리의 식별 및 기존 파일 덮어쓰기 거부를 검사한다.
`src/lunasavedialog.h`를 앱의 CI 검사와 공유하므로 같은 경로 검사를 사용한다.
사용자 설정이나 PDF를 사용하지 않는 작은 네이티브 테스트다.

포터블 패키징 후에는 다음 검사를 수행한다.

- `tools/verify_portable.py`: x64 PE 헤더, 필수 Qt/PDF4QT DLL,
  `platforms/qwindows.dll`, MSVC 런타임 및 라이선스 파일 확인.
  개인 데이터 파일과 추가 실행 파일은 차단한다. 전체 DLL import 해석 검사는 아니다.
  MSVC 런타임 DLL은 Visual Studio 2022의 공식 x64 CRT 재배포 폴더에서
  앱 옆으로 복사하며, 별도 런타임 설치 프로그램 실행에 의존하지 않는다.
- `tools/smoke_windows.py`: 개발용 Qt 경로를 제외한 환경에서 `--version` 실행 후,
  실제 메인 창을 시작하고 임시 합성 2페이지 PDF를 연다.
  PDF4QT Editor의 실제 선택·형광펜 드래그·메모 입력·다른 이름으로 저장을 수행한다.
  복사본을 닫고 다시 열어 하이라이트 수·색상과 메모 내용을 확인한다.
  원본 합성 PDF가 바이트 단위로 동일하고 별도 PDF가 저장되어야 통과한다.
  이 검사와 기존 UI 검사를 모두 통과한 경우에만 성공 마커를 기록하고 종료한다.
  타임아웃, 비정상 종료 또는 마커 누락은 실패다. 테스트 PDF는 패키지에 포함하지 않는다.

위 검사의 추가 자체는 Windows 검증 성공을 뜻하지 않는다. 새 커밋의 Windows 실행 결과를
별도로 확인해야 한다. 아래 읽기 툴바 빌드에서는 합성 문서의 메모 편집·삭제와
색상 선택 UI도 검사했다. 실제 두 단 논문 선택 정확도, 렌더링 품질·성능,
물리 모니터의 OS 배율 변경과 실제 출력물 품질은 별도 검증 대상이다.

## 간결한 읽기 툴바 작업

`feature/native-reading-toolbar`는 기본 PDF 툴바를 열기·저장, 페이지 이동/번호,
확대·축소/비율, 텍스트 선택, 형광펜, 메모, 주석 삭제, 본문 검색, 인쇄,
라이브러리 접기로 제한한다. 기존 QWidgetAction의 페이지/비율 입력 위젯을 재사용하고
플러그인 툴바는 숨긴다. 원래 QAction과 연결은 유지하며 고급 기능은 메뉴에 남긴다.
메인 툴바는 약42 논리 픽셀 높이이며 같은 선형 아이콘과 한국어 툴팁을 사용한다.
좁은 창에서는 Qt의 한 줄 오버플로와 맨 앞의 라이브러리 접기를 사용한다.

PDF를 연 뒤 기본 상태는 텍스트 선택이다. 주석을 만든 뒤 텍스트 선택으로 돌아오며,
메모 아이콘 클릭은 PDF4QT의 팝업을 연다. 팝업의 메모 편집 버튼은 기존 주석 편집기로
연결한다. 지우개 클릭은 선택한 주석에 기존 주석 관리자의 삭제 기능을 적용하며,
본문이나 원본 PDF 객체를 지우는 기능을 추가하지 않는다.

새 검증은 합성2페이지 PDF로 페이지/확대 조작, 실제 키 입력, 드래그 복사,
형광펜 색상 선택, 메모 확인/편집, 주석 삭제/복구 및 저장 후 재열기를 검사한다.
별도 레이아웃 모드는 Windows Qt의 프로세스 배율1.25/1.5에서 유효 화면 크기를 줄여
겹침·잘림과 패널 접기를 검사한다. 이는 실제 모니터의 OS 배율 변경 검사와 구분한다.
[Windows 실행40282150131](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/40282150131)에서
C++ 컴파일, 패키징, 위 네이티브 기능 검사와 배율별 배치 검사가 모두 통과했다.
검증한 앱 소스는 `96c66f747fc4ff59f351ba6b5efe00241445bbc1`이다.
Python **34 PASS / 0 FAIL / 0 SKIP**, 네이티브 Qt CTest **1 PASS / 0 FAIL / 0 SKIP**이다.
이후 커밋은 별도 인쇄 검사·워크플로·문서 변경이며 앱 C++ 소스는 동일하다.

[합성 UI 캡처](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/40282150131/artifacts/12633601824)의
6개 PNG를 다운로드해 직접 검토했다. 기본 1366×768·1920×1080 및 배율별 캡처에서
플러그인 도구 줄이 제거됐고 툴바 높이는 모두42 논리 픽셀이다.
좁은1366 화면의125%/150%에서는 오버플로를 사용하며, 라이브러리 접기 후 모든 필수
도구가 보이는 것을 자동 검사했다.150%의1366 캡처는 정수 논리 좌표 반올림으로
실제1367×768이며 나머지는 요청한 픽셀 크기와 같다. 실제 OS 모니터 배율 검사는 아니다.

포터블·캡처 ZIP 모두 GitHub가 제공한 SHA256과 로컬 다운로드를 대조했다.

- 포터블: `44c39d6f32246cc00c1be6d85d3ecf4463fa6b57efde06dd9c34729eef6c4368`
- UI 캡처: `0a5a6c97e723010472c4371797ab7531bc4e22a61658f85f79899be838b4952f`

[별도 Windows 인쇄 검사40285739442](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/40285739442)도 통과했다.
`tools/probe_print_windows.ps1`은 위 검증된 EXE 해시를 확인한 뒤 실제 전경 창에 Ctrl+P를
보내고, 같은 프로세스의 Windows 인쇄 창이 열리는지 확인해 취소한다.
원본 합성 PDF 바이트도 유지됐다. 실제 인쇄 작업은 보내지 않았다.
동일 검사를 재실행하려면 수동 워크플로에서 `packaging_preflight_only`와
`verify_print_portable`을 함께 선택한다. 이 모드는 위 기존 산출물을 사용하며 앱을 재컴파일하지 않는다.

## PDF 주석과 저장 정책

Editor 통합 브랜치는 PR #2의 Editor 기반 구성을 재사용하되 기존 포터블 검사와
라이브러리 UI를 유지한다. 원본 렌더러와 주석 도구를 수정하지 않는다.
형광펜을 선택하고 본문을 드래그하면 하이라이트가 생성되며,
PDF4QT 기본 스타일 창에서 색상을 선택할 수 있다. 메모 도구는 클릭한 위치에
텍스트 메모를 추가한다. 페이지 북마크는 PDF4QT 기본 기능이다.
자동 검사는 실제 글자 좌표에 기반한 영역 드래그를 사용한다. 시작과 끝의 높이가
완전히 같은 드래그는 상위 엔진의 사각형 선택에서 빈 영역으로 처리되는 제한이 있다.
북마크는 하이라이트·메모와 달리 PDF 주석으로 저장되지 않으며,
기본 북마크 내보내기/불러오기 메뉴에서 별도 JSON을 사용한다.
읽기 툴바 빌드에서 합성 문서의 색상 선택과 메모 편집·삭제는 네이티브 검사를 통과했다.
실제 논문에서의 실사용과 북마크 JSON 왕복은 별도 검증 대상이다.

**현재 PDF 저장 / Ctrl+S는 현재 열린 PDF에 변경 내용을 기록한다.**
원본을 보존하려면 먼저 **다른 이름으로 저장**으로 복사본을 만든다.
이후 저장 대상은 그 복사본이다. 라이브러리의 PDF 추가는 원본을 이동하지 않고
복사하며, 주석을 자동 저장하지 않는다. 다른 PDF를 열거나 프로그램을 닫을 때
PDF4QT의 저장 확인을 유지한다. 동기화 폴더에서는 두 PC가 같은 PDF를 동시에
편집하지 않아야 한다. 개인 설정 QSettings를 공유 SQLite DB로 바꾸지 않는다.

## MYBOX 로컬 폴더 동기화 준비

파일 구조는 같은 폴더의 `paper.pdf`와 `paper.luna-thumb.png`이다.
하이라이트·메모는 사용자가 저장한 PDF 안에 들어간다. 베타 개발 브랜치에서는
논문 목록의 우클릭 메뉴로 현재 PDF 화면을 크롭해 썸네일을 지정하고,
PDF와 썸네일을 같은 드라이브의 다른 폴더로 함께 이동할 수 있다.
대상 이름 충돌 시 덮어쓰지 않으며, 다른 드라이브로의 이동은 지원하지 않는다.
부분 실패 시 되돌리기를 시도하고, 복원이 불가능한 썸네일의 보존 위치를 알린다.
열린 PDF 이동은 기존 저장 확인 후 문서를 닫고 새 경로로 다시 연다.
이 기능들의 Windows 통합 검증은 진행 중이며, 베타 완료를 뜻하지 않는다.
외부 탐색기에서 PDF만 이동했을 때 썸네일을 자동 추적하지 않는다.
북마크 JSON은 사용자가 명시적으로 내보낸 별도 파일이다.
창 크기·최근 열람·즐겨찾기 등 QSettings는 PC별로 유지하며 공유 SQLite를 만들지 않는다.

두 PC에서 동일 PDF를 동시에 편집하지 않는다. 다른 PC에서 열기 전 MYBOX 동기화
완료를 확인하고, 편집·저장·프로그램 종료 후 다시 동기화 완료를 확인한다.
로컬 파일의 원자적 저장은 두 PC 사이의 충돌 해결 기능이 아니다.
충돌 복사본이 생기면 양쪽 파일을 보존하고 다른 이름으로 저장한 사본에서
주석을 비교한다. 자동 병합·자동 충돌 파일 삭제는 제공하지 않는다.
MYBOX API·로그인·토큰 처리나 GitHub를 이용한 PDF 동기화는 추가하지 않는다.

## 개인정보 보호

공개 저장소에는 사용자 PDF, 실제 논문 기록, 메모, 썸네일, SQLite DB, API 키, MYBOX 인증정보, 로컬 사용자 경로, 기존 비공개 LUNA/Recall 소스를 **절대 업로드하지 않는다**. 의심이 생기면 빌드 및 공개 작업을 중단한다. 자세한 내용은 [SECURITY_POLICY.md](SECURITY_POLICY.md).

## 저작권

PDF4QT 원본: [JakubMelka/PDF4QT](https://github.com/JakubMelka/PDF4QT), MIT licensed. 재배포되는 바이너리에는 PDF4QT 원본과 제3자 라이선스 고지를 포함한다.

## 상태 (2026-10-26)

**기존 P0 포터블·P1 읽기 툴바·P2 Editor 합성 주석 저장/재열기 Windows 검사 결과는 보존한다.**
PR #7은 베타 통합 개발 중이다. 썸네일 크롭·비동기 캐시·호버·동반 이동과 공식 달 아이콘은
구현했으며, 최신 Windows 통합 검증은 별도 진행 중이다. 캐시와 파일 이동의 Linux Qt C++
검사는 통과했지만 Windows 실행 검사를 대체하지 않는다.
다중 문서는 **탭 대신 문서별 독립 프로세스·창 방식**을 사용한다. Ctrl+O에서 여러 PDF를
선택하거나 목록·드래그 앤 드롭으로 열 수 있다. 같은 PDF는 기존 창을 활성화하고,
각 창은 자체 PDF4QT Controller·주석·저장 상태를 가진다. 다른 창에서 열린 PDF는
그 창으로 전환한 뒤 이동해야 한다. 창별 미저장 변경 확인은 PDF4QT 기능을 유지한다.
이 방식의 Windows 2·5·10개 문서 및 주석 분리 검증은 진행 중이다.
원본 `PDFProgramController::loadPlugins()`는 `QPluginLoader::instance()`의 플러그인에
`setDataExchangeInterface(this)`와 `setWidget(m_pdfWidget)`를 설정한다. 같은 프로세스에
여러 Editor를 넣으면 공유 플러그인의 참조가 바뀌므로, 엔진을 수정하지 않는 베타에서는
프로세스를 분리한다. 작동하는 탭 UI를 제공한다고 표시하지 않는다.
실제 학술 PDF, 물리 모니터 OS 배율, 장시간 사용 검증 및 최종 Beta ZIP은 남아 있다.
태그와 DOI 추출 등 제외 기능은 개발하지 않는다. main 자동 병합·공개 Release 게시 없음.
