# LUNA Library V2

**PDF4QT 기반 Windows용 LUNA Library** — 2열 인터페이스 (왼쪽 폴더/PDF 목록, 오른쪽 PDF 본문)

This is a **public source-only build repository**. It does not store, synchronize or receive your PDF files, annotations, research notes, cloud credentials or local databases.

## Windows EXE 만들기

1. [Actions → LUNA Library V2 - PDF4QT Windows Portable](https://github.com/MRYoonn/LUNA-Library-V2/actions/workflows/build-windows.yml) 페이지 열기.
2. 오른쪽 **Run workflow** → Branch **main** → 초록색 **Run workflow** 누르기.
3. 실행 기록에서 각 단계의 성공/실패를 확인하기. 실패하면 정확한 오류 메시지를 확인하고 수정한다.
4. 성공하면 해당 실행 기록 맨 아래 **Artifacts → LUNA-Windows-Portable** 다운로드.
5. ZIP 압축을 풀고 **LUNA.exe** 더블클릭. PDF 로딩·선택·화질은 실제 Windows에서 별도 검증.

초기 빌드는 PDF4QT의 C++/Qt/vcpkg 의존성 설치와 컴파일 때문에 오래 걸릴 수 있다. 실패 원인을 확인하고 수정한 뒤 필요한 검증만 다시 실행한다. 실제 PDF의 화질과 상호작용은 자동 시작 검사와 별도로 확인한다.

Windows 기본 포터블 빌드는 이후 검증을 통과했다.
[P0 검증 실행](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/40230666645)에서
컴파일·패키징·DLL 검사·실제 창 시작·합성 PDF 로딩이 통과했고,
[포터블 Artifact](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/40230666645/artifacts/12614056454)가 생성됐다.
이 산출물의 커밋은 `31a65be0fd2fc72636356044560faefc55eb5a68`이다.
첨부 디자인에 따른 후속 UI 변경과 주석 기능까지 검증됐다는 뜻은 아니다.

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
검색·즐겨찾기·최근 열람·패널 접기·목록을 통한 PDF 재열기·연속 보기 200%를 검사한다.
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

Windows 워크플로는 포터블 패키징 후 다음 검사를 수행한다.

- `tools/verify_portable.py`: x64 PE 헤더, 필수 Qt/PDF4QT DLL,
  `platforms/qwindows.dll`, MSVC 런타임 및 라이선스 파일 확인.
  개인 데이터 파일과 추가 실행 파일은 차단한다. 전체 DLL import 해석 검사는 아니다.
  MSVC 런타임 DLL은 Visual Studio 2022의 공식 x64 CRT 재배포 폴더에서
  앱 옆으로 복사하며, 별도 런타임 설치 프로그램 실행에 의존하지 않는다.
- `tools/smoke_windows.py`: 개발용 Qt 경로를 제외한 환경에서 `--version` 실행 후,
  실제 메인 창을 시작하고 임시 합성 1페이지 PDF를 연다.
  PDF4QT Editor의 실제 선택·형광펜 드래그·메모 입력·다른 이름으로 저장을 수행한다.
  복사본을 닫고 다시 열어 하이라이트 수·색상과 메모 내용을 확인한다.
  원본 합성 PDF가 바이트 단위로 동일하고 별도 PDF가 저장되어야 통과한다.
  이 검사와 기존 UI 검사를 모두 통과한 경우에만 성공 마커를 기록하고 종료한다.
  타임아웃, 비정상 종료 또는 마커 누락은 실패다. 테스트 PDF는 패키지에 포함하지 않는다.

위 검사의 추가 자체는 Windows 검증 성공을 뜻하지 않는다. 새 커밋의 Windows 실행 결과를
별도로 확인해야 한다. 화면 렌더링 품질, 두 단 논문 선택 정확도, 메모 편집·삭제,
색상 선택 UI 및 Windows 배율별 검증은 별도 테스트가 필요하다.

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
색상 선택, 메모 편집·삭제 및 북마크 JSON 왕복은 후속 실사용 검증 대상이다.

**현재 PDF 저장 / Ctrl+S는 현재 열린 PDF에 변경 내용을 기록한다.**
원본을 보존하려면 먼저 **다른 이름으로 저장**으로 복사본을 만든다.
이후 저장 대상은 그 복사본이다. 라이브러리의 PDF 추가는 원본을 이동하지 않고
복사하며, 주석을 자동 저장하지 않는다. 다른 PDF를 열거나 프로그램을 닫을 때
PDF4QT의 저장 확인을 유지한다. 동기화 폴더에서는 두 PC가 같은 PDF를 동시에
편집하지 않아야 한다. 개인 설정 QSettings를 공유 SQLite DB로 바꾸지 않는다.

## MYBOX 로컬 폴더 동기화 준비

예정 파일 구조는 같은 폴더의 `paper.pdf`와 `paper.luna-thumb.png`이다.
하이라이트·메모는 사용자가 저장한 PDF 안에 들어간다. 썸네일 생성·묶음 이동은
후속 구현 대상이며, 현재 썸네일 자동 추적을 제공한다고 표시하지 않는다.
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

**P0 포터블 및 P1 네이티브 UI Windows 자동 검사 통과.** Editor 통합과 합성 주석 저장·재열기 검사를 추가했으며 이 변경의 Windows 결과는 아직 미검증이다. 실제 사용자 문서와 Windows 배율 검증은 미실시. 기존 비공개 저장소 변경 없음.
