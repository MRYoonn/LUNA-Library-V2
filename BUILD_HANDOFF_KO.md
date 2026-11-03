# LUNA Library V2 — Windows 빌드·오류 분석 인수인계

> **다음 매니저 필독.** 실제 소스 확인 → 최소 수정 → 회귀 테스트 → **정확한 커밋의 Windows 전체 빌드** → 네이티브 GUI 검증 → ZIP 무결성 확인 → 원인 분석 보존의 순서를 지킨다.
>
> 기준일: 2026-11-03. 이 문서는 *빌드 운영 규칙*이다. 이력에 기록된 성공 커밋을 현재 브랜치의 성공으로 잘못 적용하지 않는다.

## 0. 코드와 검증 상태부터 확인

- 저장소: https://github.com/MRYoonn/LUNA-Library-V2
- Windows 전체 빌드: [Actions / build-windows.yml](https://github.com/MRYoonn/LUNA-Library-V2/actions/workflows/build-windows.yml) (기본적으로 **수동 workflow_dispatch**).
- 코드 검토 대상: `main`의 실제 HEAD, 현재 작업 브랜치의 HEAD, 열려 있는 Draft PR과 **상위 의존 PR**, 최신 커밋 및 Actions 로그.
- 2026-11-03의 기능 개발은 `main`에 병합되지 않은 Draft PR #22(`manager8/folder-count-verifier-20261103`, 상위 `feature/title-extraction-v2`/PR #19)에 있다. **main을 빌드하고 최신 기능을 검증했다고 주장하지 않는다.**
- 실제로 검증된 기준선: [Windows Build #240](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41567734959), **run ID `41567734959`**, SHA **`a56e1f9f36a4f797fe247039870c9e0fb140a8e4`**, 전체 결론 `success`. 이전 통과 기준선: [#239](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41565593079) SHA `d36786a4afa44ea826100520275f0c422d695a89`.
- #240에서 `windows-packaging-preflight`와 `build-windows` 모두 통과했고, Portable Artifact가 생성되었다. `build-info.json` 기준 Python **155**, 네이티브 Qt **8**, Windows C++·합성 PDF/주석·독립 탭·메모 복원·Qt 100/125/150%·CDB/Full Page Heap 종료 안정성 검사가 통과했다.
- **미검증**: 물리 모니터 DPI, 장시간 사용자 PC 세션, 선택적 실제 공개 논문 검증, 정식 Release 사이 자동 업데이트, 최종 베타 완료. `betaComplete=false`, `status=development verification`. 포터블 개발 빌드는 정식 배포가 아니다.

## 1. 개발·검증 표준 절차

1. **기준 고정:** 작업할 브랜치, 현재 40자리 HEAD SHA, 관련 PR의 base/head, 사용자 증상, 재현 단계를 기록한다. 다른 작업자가 같은 브랜치를 갱신했는지 확인한다.
2. **원인 추적:** 실제 C++/Qt 클래스, QSettings 키, 이벤트/시그널, 모델 수명, 비동기 작업 및 관련 테스트를 읽는다. 코드를 보지 않고 원인을 확정하지 않는다.
3. **최소 수정:** 문제를 직접 해결하고 재현 방지 테스트를 추가한다. 이미 성공한 부분을 무분별하게 재작성하지 않는다. 테스트가 새 요구사항과 충돌하면 **테스트의 계약을 정확히 수정**한다. 단지 초록불을 만들기 위해 테스트·크래시 관찰·보안 게이트를 우회하지 않는다.
4. **공개 소스 게이트:** `tools/preflight_public.py`, `tests/test_public_gate.py`와 관련 Python 회귀 검사를 확인한다. 새 공개 텍스트 파일은 명시적 허용 목록에 추가하고, 실제 필요한 경우에만 근거 있는 파일 크기 한도를 조정한다. 사용자 PDF, 원본 메모, 경로, 인증정보, DB는 커밋하지 않는다.
5. **정확한 브랜치로 실행:** GitHub → Actions → **LUNA Library V2 - Windows Portable** → **Run workflow** → 해당 변경사항이 있는 브랜치를 선택한다. 현재 HEAD와 워크플로 실행 `head_sha`가 정확히 일치하는지 확인한다. main/오래된 기능 브랜치로 대신 실행하지 않는다.
6. **전체 관문 검증:** Preflight → native Qt/Save As → MSVC 컴파일·DLL → 포터블 구성 → 실제 창 시작 → PDF 열기 성능 → 네이티브 문서/인쇄·주석 왕복 → 프로덕션 다중 탭·라이브러리·새 프로세스 메모 복원 → Qt 배율 → CDB/Full Page Heap 종료 → ZIP 해시/Artifact 업로드 순으로 결과를 기록한다. 요구사항과 관련된 UI 동작을 실제 Qt 이벤트로 테스트하는 것이 우선이다.
7. **최종 성공 판정:** 전체 Workflow `completed/success`, 필수 Job `success`, 실패 Step 없음, `LUNA-Windows-Portable` Artifact 존재를 확인한다. ZIP 다운로드 후 손상 여부를 검사하고 `build-info.json`의 `commit`/`run`이 CI 실행과 일치하는지, `LUNA.exe` SHA-256이 `exeSha256`과 일치하는지 비교한다. **성공 컴파일 ≠ 전체 빌드 성공 ≠ 제품 정식 출시**.
8. **최종 보고:** 변경 내용, 실패 원인, 회귀 범위, 빌드 번호·run ID·정확한 SHA·Job 결과, Artifact, 검증하지 못한 항목을 PR에 남긴다. 본진에 병합할지는 **Draft PR 의존성**을 확인한 후 별도 결정한다.

### 터미널로 ZIP 내용을 검증할 때(선택)

```python
from pathlib import Path
from zipfile import ZipFile
from hashlib import sha256
import json

p = Path("LUNA-Windows-Portable.zip")
with ZipFile(p) as z:
    assert z.testzip() is None, "Corrupted ZIP member"
    names = z.namelist()
    assert "LUNA.exe" in names and "build-info.json" in names
    info = json.loads(z.read("build-info.json"))
    assert sha256(z.read("LUNA.exe")).hexdigest() == info["exeSha256"]
    print("run", info["run"], "commit", info["commit"], "files", len(names))
# 반드시 GitHub Actions run_id/head_sha와 출력값을 별도로 대조할 것.
```

## 2. 빌드 실패 시 필수 대응

**파일을 남기지 않은 빌드 실패는 완료된 분석이 아니다.**

1. 먼저 실패한 **최초의 필수 Step**과 GitHub Actions 원문 로그를 찾는다. 실패를 컴파일, 보안/사전 검사, 실제 UI, 주석 저장, 패키징, DPI, CDB/프로세스 종료 등으로 구분한다.
2. **모든 CI 실행**에서 `tools/build_diagnostics.py`로 생성하는 `failure-analysis.md`와 `build-diagnostics.json`을 보존한다. `LUNA-Build-Diagnostics` 및 `LUNA-Preflight-Diagnostics` Artifact 보존은 90일로 설정돼 있다. **실패 시 GitHub Issue에도 원인 분석 보고를 보존**하는 단계가 있으나 Issue 기록은 권한/네트워크 오류로 실패할 수 있으므로 성공 여부까지 확인한다. 일반 포터블 Artifact와 UI 스크린샷의 보존은 통상 7일이므로 장기 보관이 필요하면 별도 승인된 경로에 보관한다.
3. 네이티브 오류는 `LUNA-UI-Synthetic` 진단, 마지막 `*.stage.txt` 체크포인트, `luna-native-failure.json`, 가능한 경우 CDB 스택·스크린샷을 확인한다. 합성 데이터/허용 필드만 기록하며 실제 PDF나 개인 경로는 로그/Issue에 올리지 않는다.
4. **확인된 사실과 가설을 구분**한다. 디버거로 다시 돌린 *replay가 성공*했다면 원본 크래시의 원인을 증명하지 못한 것이다. 크래시의 exit code가 0이 아니면 코드 컴파일이 성공해도 전체 빌드는 **실패**로 남긴다.
5. 재현 조건을 고정하고 원인에 대응하는 최소 수정+회귀 검사를 만든 뒤, **변경 후 새 SHA**로 전체 빌드를 다시 실행한다. 이전 성공 실행/Artifact로 새로운 커밋을 성공 처리하지 않는다.
6. 실패 보고서를 `BUILD_DIAGNOSTICS.md`에도 누적하여 **재발 원인·실행 ID·SHA·실패 Step·실제 증거·해결 여부**를 기록한다. 이미 기록된 과거 실패를 지우지 않는다.

### 실제 실패와 수정에서 얻은 교훈

| 실행 | 실패 또는 확인된 문제 | 수정/판정 |
|---|---|---|
| [#233](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41550167904) ~ [#235](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41556468643) | 네이티브 인쇄 대화상자 미관찰. PDF 로드 완료 상태와 비동기 `actionPrint` 준비 상태가 달랐음 | Action 활성화를 유한 시간 내 실제 이벤트 처리로 확인하고 Ctrl+P 전달 및 Windows 대화상자 관찰을 각각 검증. [#236 성공](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41557070156). |
| [#237](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41562960971) | `0xC0000005` / `count-recovered` 부근. CDB 재현 스택에 `QListWidget::visualItemRect` | 폴더 변경 시 `QListWidgetItem*`가 무효화됨. 테스트에서 이전 항목 포인터 재사용 금지, 비파괴적인 테스트 순서 보장. |
| [#238](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41564257499) | 같은 체크포인트 크래시; 원본 로그에 삭제된 임시 폴더의 `QFileSystemWatcher` 경고. CDB 재실행은 정상 종료 | 파일 감시 race는 **가설**로 기록. 임시 폴더의 수명을 GUI 테스트 종료까지 연장하고 해당 PDF만 삭제하며 상세 체크포인트 추가. [#239 성공](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41565593079), [#240 성공](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41567734959). |
| Shift 다중삭제·폴더 강조·패널 크기 | 우클릭 시 선택 소실/클릭된 한 파일로 대체, 루트와 하위 폴더의 체크 혼선, 논문 목록 단독 모드 300px 폭 제한 | 컨텍스트 메뉴 열기 전 선택 경로 스냅샷, 실제 폴더만 선택 표시, 패널 최대 850px 및 사용자 너비 기억. **합성 UI 테스트 통과가 실제 모든 사용자 시나리오 보증은 아님**. |

## 3. 작업 분리·자동화·릴리스 정책

- 브랜치/PR 의존성이 쌓여 있으므로 독립 모듈을 개발할 때 기존 Draft PR 또는 main과 충돌/중복을 조사한다. **새 코드의 직접 반영·main 병합·Release 게시를 자동으로 해서는 안 된다.**
- `.github/workflows/build-windows.yml`은 수동 실행이 원칙이다. Manager 8에서만 사용한 `.github/workflows/manager8-dispatch-once.yml`은 특정 브랜치/파일 변경으로 수동 빌드 요청을 이어주는 **한시적 브리지**다. 일반 빌드 자동화의 표준이나 모든 브랜치용 트리거로 복제하지 않는다.
- `.github/workflows/draft-release.yml`은 별도 수동 검증을 거쳐 **Draft Release만** 만든다. 정식 Release 게시, 실제 자동 업데이트 교체, 설치 프로그램 배포는 각각 별도 검증·승인이 필요하다.
- 진단 Artifact는 만료될 수 있다. 완료된 증거는 커밋 이력·`BUILD_DIAGNOSTICS.md`·PR/Issue·run 링크에 남겨야 한다.
- 개발 실행 결과는 반드시 구분한다: **코드 작성**, **Python/소스 검사**, **C++ 컴파일**, **전체 Windows CI**, **물리 Windows 사용자 검증**, **정식 배포**. 뒤 단계를 앞 단계의 결과로 대신 보고하지 않는다.

## 4. 다음 매니저의 매 작업 종료 보고 형식

```text
대상 PR / 브랜치:
수정한 기능과 실제 재현 조건:
원인 (확인됨 / 가설):
변경 파일 및 회귀 테스트:
검증 SHA (40자):
GitHub Actions run URL / run ID:
Preflight / MSVC / native UI / DPI / CDB / packaging 결과:
Artifact 이름과 ZIP·EXE 무결성 확인:
failure-analysis.md / BUILD_DIAGNOSTICS.md / Issue 보존 여부:
남은 미검증 항목 / main 병합 여부:
```

**최종 원칙:** 실패하면 원인 파일을 남기고 다시 수정한다. **정확한 SHA의 Windows 전체 빌드가 통과하고 검증된 포터블 ZIP이 있어야만 “빌드 성공”이라고 말한다.**
