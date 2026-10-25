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

## 개인정보 보호

공개 저장소에는 사용자 PDF, 실제 논문 기록, 메모, 썸네일, SQLite DB, API 키, MYBOX 인증정보, 로컬 사용자 경로, 기존 비공개 LUNA/Recall 소스를 **절대 업로드하지 않는다**. 의심이 생기면 빌드 및 공개 작업을 중단한다. 자세한 내용은 [SECURITY_POLICY.md](SECURITY_POLICY.md).

## 저작권

PDF4QT 원본: [JakubMelka/PDF4QT](https://github.com/JakubMelka/PDF4QT), MIT licensed. 재배포되는 바이너리에는 PDF4QT 원본과 제3자 라이선스 고지를 포함한다.

## 상태 (2026-10-26)

공개 소스와 수동 Windows 빌드 설정 등록 완료. **Windows 네이티브 컴파일 및 GUI 검증 미실시.** 기존 비공개 LUNA 저장소 변경 없음.
