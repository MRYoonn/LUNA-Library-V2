# LUNA Library V2 — Coding agent operating contract

**먼저 [BUILD_HANDOFF_KO.md](BUILD_HANDOFF_KO.md)를 읽는다.** 이 저장소에서 '수정해', '이어서 해', '빌드해'라는 요청은 코딩 에이전트가 스스로 **코드 수정부터 성공 Windows 포터블 ZIP 전달까지 수행**하라는 뜻이다.

## 반드시 직접 수행할 일

1. 최신 GitHub `main`, 작업 브랜치, 열려 있는 Draft PR, 의존 PR, 성공/실패 CI 및 실제 소스 코드를 확인한다.
2. 사용자가 지적한 문제를 재현할 수 있는 근거를 찾고 **실제 코드를 수정하며 회귀 테스트를 추가**한다. 분석 보고서만 쓰고 끝내지 않는다.
3. GitHub에 변경사항을 커밋한다. 적절한 GitHub 연동/CLI/API가 있으면 에이전트가 **직접** `build-windows.yml` workflow_dispatch를 실행한다. 연결된 앱에 dispatch 기능이 없다면 브랜치/파일/권한을 좁게 제한한 승인된 Actions 브리지로 직접 요청한다(예: `.github/workflows/manager8-dispatch-once.yml`). **사용자에게 Run workflow 버튼을 누르라고 하지 않는다.**
4. 생성된 실제 Windows CI run의 `head_sha`와 구현 커밋을 대조한다. 공개 사전 검사, MSVC, Qt 네이티브 UI, 주석·인쇄, 다중 탭, DPI, CDB/Full Page Heap, 패키징 단계가 모두 성공해야 한다.
5. 실패하면 반드시 로그와 진단 Artifact를 읽고 원인을 파악해 `BUILD_DIAGNOSTICS.md`에 기록한다. `failure-analysis.md`와 `build-diagnostics.json`을 보존한다. 테스트를 삭제하거나 체크를 건너뛰어 초록불만 만드는 행위 금지. 수정 뒤 **새 커밋 SHA로 새 전체 Windows 빌드**를 직접 실행한다.
6. 전체 CI `completed/success` 후 `LUNA-Windows-Portable` Artifact를 직접 다운로드한다. ZIP 손상 검사, `build-info.json`의 run/commit, `LUNA.exe` SHA-256을 검증하고 사용자에게 다운로드 링크와 검증 결과를 전달한다.
7. 사용자에게는 완료된 수정, 정확한 run/SHA, 검사 결과와 미검증 사항을 보고한다. **코드 작성, 컴파일, 전체 Windows 통과, 실제 사용자 PC 검증, 정식 Release를 혼동하지 않는다.**

## 실제로 했던 자율 개발·빌드 사례

- [Windows Build #237](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41562960971), [#238](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41564257499): 네이티브 크래시 발생, CDB/체크포인트/윈도우 파일 감시 경합을 분석하고 `BUILD_DIAGNOSTICS.md`에 기록.
- 에이전트가 직접 수정→재빌드하여 [#239](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41565593079) 및 [#240](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41567734959) **Windows 전체 성공** 확인. #240의 검증 SHA는 `a56e1f9f36a4f797fe247039870c9e0fb140a8e4`; 성공한 ZIP Artifact까지 검사.
- 이 기록은 당시 기능 브랜치에 한정된다. **새 커밋/브랜치가 이 빌드를 통과했다는 뜻이 아니다.**

## 제한 및 예외

- 공개 저장소에 사용자 PDF, 썸네일, 연구 노트, 개인 파일 경로, API 키, DB 또는 인증 정보를 올리지 않는다. `tools/preflight_public.py` 공개 보안 게이트를 우회하지 않는다.
- 외부 도구 권한이 없다면 실제로 가능한 동작까지 해보고 **막힌 단계 및 근거**를 설명한다. 실행하지 않은 빌드를 실행했다고 말하지 않는다.
- 기능 개발용 CI와 main 병합, 정식 Release 게시, 자동 업데이트 배포는 별개다. 명시적 승인 없는 main 기능 병합·Release 발행·사용자 파일 변경을 하지 않는다.
- 테스트/문서만 수정했거나 코드 빌드가 필요 없는 작업이면 불필요하게 무거운 Windows 빌드를 돌리지 않는다.

**업무 완료 기준:** 에이전트가 **직접** 구현, 커밋, 빌드 실행, 실패 수정·재빌드, 최종 ZIP 검증, 사용자 보고까지 끝낸다. [전체 인수인계](BUILD_HANDOFF_KO.md)에 더 자세한 CLI·브리지·진단 절차가 있다.
