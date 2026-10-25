# LUNA Library V2

**PDF4QT 기반 Windows용 LUNA Library** — 2열 인터페이스 (왼쪽 폴더/PDF 목록, 오른쪽 PDF 본문)

This is a **public source-only build repository**. It does not store, synchronize or receive your PDF files, annotations, research notes, cloud credentials or local databases.

## Windows EXE 만들기

1. [Actions → LUNA Library V2 - PDF4QT Windows Portable](https://github.com/MRYoonn/LUNA-Library-V2/actions/workflows/build-windows.yml) 페이지 열기.
2. 오른쪽 **Run workflow** → Branch **main** → 초록색 **Run workflow** 누르기.
3. 실행 기록에서 각 단계의 성공/실패를 확인하기. 실패하면 정확한 오류 메시지를 확인하고 수정한다.
4. 성공하면 해당 실행 기록 맨 아래 **Artifacts → LUNA-Windows-Portable** 다운로드.
5. ZIP 압축을 풀고 **LUNA.exe** 더블클릭. PDF 로딩·선택·화질은 실제 Windows에서 별도 검증.

**주의:** 아직 실제 Windows EXE 빌드 성공이나 GUI 동작이 검증되지 않았다. 초기 빌드는 PDF4QT의 C++/Qt/vcpkg 의존성을 설치하기 때문에 오래 걸리거나 외부 패키지 문제로 실패할 수 있다. 승인 없이 자동 재시도하지 않는다.

## 동작 구조

- **독립 실행 파일:** `LUNA.exe` (별도 target)
- **PDF 엔진:** 고정 버전의 공개 PDF4QT 원본 (수정 금지)
- **LUNA UI:** `src/lunalibrary.cpp` / `src/lunalibrary.h`
- **적용:** `tools/apply_overlay.py` (업스트림 원본 소스 해시 확인 후 새로운 타깃만 추가)
- **보안:** `tools/preflight_public.py` 및 `tests/test_public_gate.py`
- **빌드:** `.github/workflows/build-windows.yml` (수동 실행, 읽기 전용)
- **집↔연구실 PDF 동기화:** 향후 로컬 컴퓨터와 별도 동기화 서비스만 사용하며 GitHub는 관여하지 않음.

## 개발 및 릴리스 검증

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
  PDF4QT가 문서를 로딩하고 창이 표시된 경우에만 성공 마커를 기록하고 종료한다.
  타임아웃, 비정상 종료 또는 마커 누락은 실패다. 테스트 PDF는 패키지에 포함하지 않는다.

위 검사의 추가 자체는 Windows 검증 성공을 뜻하지 않는다. 새 커밋의 Windows 실행 결과를
별도로 확인해야 한다. 화면 렌더링 품질, 텍스트 선택, 주석 저장·재열기,
Windows 배율별 UI 검증은 여전히 별도 테스트가 필요하다.

## 개인정보 보호

공개 저장소에는 사용자 PDF, 실제 논문 기록, 메모, 썸네일, SQLite DB, API 키, MYBOX 인증정보, 로컬 사용자 경로, 기존 비공개 LUNA/Recall 소스를 **절대 업로드하지 않는다**. 의심이 생기면 빌드 및 공개 작업을 중단한다. 자세한 내용은 [SECURITY_POLICY.md](SECURITY_POLICY.md).

## 저작권

PDF4QT 원본: [JakubMelka/PDF4QT](https://github.com/JakubMelka/PDF4QT), MIT licensed. 재배포되는 바이너리에는 PDF4QT 원본과 제3자 라이선스 고지를 포함한다.

## 상태 (2026-10-26)

공개 소스와 수동 Windows 빌드 설정 등록 완료. **Windows 네이티브 컴파일 및 GUI 검증 미실시.** 기존 비공개 LUNA 저장소 변경 없음.
