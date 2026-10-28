> Windows 실증 업데이트: [40584019204](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/40584019204),
> 소스 `a69be17`, 3개 Qt 배율 모두 `accepted=true`, 종료 코드 0.
> 아래 본문은 최초 인수인계 당시의 기록입니다. 제품 라이브러리 통합 검증은 별도입니다.

# LUNA 다중 PDF 탭 — 소스 검토 및 구현 인수인계

**결론: B(같은 프로세스의 문서별 Editor 위젯)를 선택한다. 단, Windows 검증 전이므로 승인된 해법은 아직 아니다.**

기준: LUNA PR #7 b60f5f2529760102887fa660365f037e78fc7aae / PDF4QT 67b6b9903cb4e318803342f2d428f02e1bb124e3.
기존 성공: Windows #40, 11f89f1d54973b327753281dd4e5abaf8f3a5562. PR/기존 브랜치/성공 빌드 변경 없음.

## 1. 실패 원인 — 확인된 사실과 추정 분리

### 외부 Qt 창 수용

- `experiments/tab_host/main.cpp:106–113`: `fromWinId()` + `createWindowContainer()`.
- 같은 파일 `synchronize():130–143`: 컨테이너를 쓰면서도 100ms마다 외부 HWND의 ShowWindow를 직접 호출한다. Qt가 관리하는 표시 상태와 별도 네이티브 표시 상태가 중첩된다.
- `clickDocument():166–178`, `tick():323–337`: 호스트를 foreground로 만든 후 SendInput을 보내지만 자식 Qt 프로세스의 activeWindow/focusWidget/ShortcutOverride/Find action 발화 여부를 기록하지 않는다.
- PDF4QT `pdfprogramcontroller.cpp:1960`: Find 액션 → 해당 컨트롤러의 Find tool 활성화. `Pdf4QtLibWidgets/sources/pdfwidgettool.cpp:433–502`: 검색 UI는 부모 Editor를 가진 `Qt::Popup`이며 독자적인 키보드 포커스가 필요하다.
- **구조적 위험:** 외부 프로세스의 QWidget/활성 창/팝업 관계는 HWND를 수용했다고 호스트 QObject 트리에 합쳐지지 않는다.
- **수정 가능한 별도 오류:** `pdfwidgettool.cpp:494–498`에서 전역 좌표를 부모 로컬 좌표로 바꿔 top-level 팝업의 move()에 전달한다. 전역 좌표 유지로 수정 가능하다. 이것만으로 과거 Ctrl+F 실패가 해결된다고 단정하지 않는다.
- 실제 #38 artifact의 결과는 `Real Ctrl+F did not open this document's search popup`. 검색 액션 미발화인지 팝업 즉시 소멸인지까지는 기존 증거로 확정 불가.

### Win32 연결

- `attach():114–126`: Qt 외부에서 WS_CHILD 및 SetParent를 직접 변경한다.
- `synchronize()`가 숨겨진 surface의 winId 및 자식 표시/크기를 반복 조작한다. 외부 HWND의 수명과 Qt surface의 수명을 동기화하는 프로토콜이 없다.
- `tick():265–274`는 `!handle || !IsWindow(handle)`를 모두 “Document exited”로 기록한다. **HWND 소멸과 프로세스 종료를 구분하지 못하는 진단 결함이 확정**된다.
- #38 artifact에는 종료 코드·HWND 교체 이력·WinIdChange·자식 크래시 덤프가 없다. 실제 프로세스 종료 원인은 미확정이다.
- 추가 구현으로 개선할 수는 있으나, focus/modal/DPI/close ACK/창 재생성까지 전부 책임져야 한다. 동일 실험 반복은 제외한다.

### 여러 Editor를 곧바로 같은 프로세스에 넣는 방식

- `Pdf4QtLibGui/pdfeditormainwindow.cpp:83–103,301`: Editor마다 ActionManager/ProgramController 생성.
- `pdfprogramcontroller.cpp:403–431,445–469,735–760`: 문서·툴·주석·폼·북마크·Undo는 컨트롤러별 객체.
- **분리되지 않은 경계:** `loadPlugins():2686–2742`는 QPluginLoader::instance()의 루트 객체를 그대로 저장하고 `setDataExchangeInterface(this)`, `setWidget(m_pdfWidget)`, `setCMSManager(...)`를 다시 호출한다.
- `Pdf4QtEditorPlugins/EditorPlugin/editorplugin.cpp:60–64`는 `Q_ASSERT(!m_widget)`로 재바인딩을 명시적으로 금지한다. 이어 액션/툴/scene 연결을 만든다. Release에서 assert가 빠져도 안전해지지 않는다.
- `setDocument():2456–2458`도 이 객체에 문서를 덮어쓴다. 따라서 단순 `new Editor` 2개는 승인할 수 없다.

## 2. 선택 구조 및 대안 비교

| 대안 | 현재 코드 호환성·기능 보존 | 상태 충돌 | 수정량 / 유지보수 | 최소 Windows 실증 |
|---|---|---|---|---|
| A. 단일 Editor 컨트롤러를 다중 문서로 확장 | 가능하지만 기존 단일 document·manager 연결을 재설계해야 함 | 전환 중 재바인딩 누락 위험 높음 | 큼 / 높음 | 상태 묶음 추출부터 필요 |
| **B. 문서별 Editor를 Qt 탭에 배치** | **Editor UI·검색·주석·저장 경로 재사용** | **플러그인 별도 생성과 전역 설정 정책 필요** | **비교적 작음 / 중간** | **2 Editor + 9개씩 독립 plugin + 기존 기능 호출** |
| C. 프로세스별 Editor 외부 창 수용 개선 | 기능과 프로세스 격리는 가장 강함 | PDF 상태 충돌은 낮으나 OS 창/포커스 충돌 높음 | 겉보기보다 큼 / 높음 | child mode·IPC·close ACK·HWND/focus 계측 필요 |
| D. 공개 API로 별도 문서 뷰 | 엔진 재사용 가능 | 명시적 문서 소유권 설계 가능 | UI·도구·검색·폼·plugin 호환까지 큼 / 높음 | 렌더링만 빠르게 되지만 요구 기능 보존 비용 큼 |

**B 선택:** Editor의 QObject 트리를 통째로 유지한다. DLL 루트는 factory prototype으로만 사용하고 문서마다 새 플러그인 객체를 생성한다. 전환은 QTabWidget 페이지 변경만 수행하며 openDocument를 호출하지 않는다.

현재 실증은 전체 Editor chrome을 각 탭 안에 둔다. 제품 단계에서는 라이브러리 1개를 유지하고 문서별 toolbar/menu를 먼저 보존한다. 공통 툴바로 합치는 작업은 뒤로 미룬다.

## 3. 실증 상태

| 항목 | 현재 결과 |
|---|---|
| PR HEAD 및 #40 성공 상태 조회 | 통과 |
| #38 실패 artifact 실제 다운로드·로그·스크린샷 확인 | 완료; 두 실험 실패 확인 |
| Python 38개 테스트 / 공개 소스 allowlist | 통과 (Windows 기능 증거 아님) |
| 기존 LUNA overlay → native overlay 적용 / pin·anchor·diff 검사 | 통과 |
| C++ 빌드 | 미실행: 로컬 Linux에 CMake/Qt 빌드 환경 없음 |
| Windows 1창 2문서·전환·Ctrl+F·형광펜·저장·닫기 | **미실행 / 승인 불가** |

Windows 실행의 현재 차단점: 연결된 GitHub 도구에는 workflow_dispatch 쓰기 기능이 없으며, 대체 브라우저는 로그인되지 않았다. 기존 성공 빌드 재실행은 새 코드 검증이 아니므로 하지 않았다.

## 4. 제공 코드 / 수정 경계

- `experiments/native_tabs/prepare.py`: 정확한 upstream pin 확인 후 13개 기존 파일에 작은 변경 적용. 렌더러 변경 없음.
- `experiments/native_tabs/main.cpp`: Tabs::open/closeTab/closeEvent 및 실제 PDF4QT를 사용하는 런타임 검사.
- `experiments/native_tabs/run_windows.py`: alpha/bravo 합성 PDF 생성, 실제 Windows에서 1/1.25/1.5 배율 실행, 실패 시 중단.
- `.github/workflows/build-windows.yml`: 수동 선택 `native_tabs_proof`. 실패 증거 보존. proof는 기존 엔진 캐시에 저장하지 않고 표준 portable로 업로드하지 않는다.
- 생성되는 upstream 변경: 9개 plugin header의 생성자에 Q_INVOKABLE, PDFProgramController::loadPlugins/canClose/askForSaveDocumentBeforeClose/performSave/saveDocument, 새 hasUnsavedChanges/trySave, PDFEditorMainWindow::closeEvent, PDFFindTextTool::setActiveImpl.
- **제품의 `src/lunalibrary.cpp`, 썸네일/메모/파일이동 코드는 변경하지 않았다.**

닫기 시 `canClose()`의 OR를 AND로 바꾸고, dirty 조건에 plugin의 hasUnwrittenChanges를 포함한다. 저장 성공은 dirty 플래그 추정 대신 trySave의 실제 결과로 확인한다. 플러그인은 쓰기 후에도 편집 모드가 활성일 수 있으므로 hasUnwrittenChanges가 즉시 false가 된다고 가정하면 안 된다.

## 5. Codex 실행 순서

1. 이 브랜치에서 수동 Windows workflow를 `native_tabs_proof=true`, 나머지 기본값으로 실행한다. PR #7 브랜치에 먼저 덮어쓰지 않는다.
2. C++ 컴파일 오류는 이 브랜치에서 수정한다. `LUNA-native-tabs-evidence/summary.json`의 accepted와 모든 프로세스 exitCode를 함께 확인한다. `.search-a/.search-b/.survivor` 스크린샷을 검사한다. 통과 기준을 낮추지 않는다.
3. 필수 5조건에 더해 plugin-only pending edit Cancel, 파일 잠금 저장 실패, 탭 삭제 후 생존 문서 검색을 통과시킨다. 메모리/종료 안정성도 재검증한다.
4. 통과 후 `IndependentDocuments::open/chooseFiles/eventFilter`의 프로세스 실행 경로를 문서 registry 호출로 교체한다. 정규화 경로 중복 방지와 기존 DocumentClaim은 유지한다.
5. `LibraryPanel(viewer_)`의 고정 Editor 참조를 active-session accessor로 바꾸되 폴더/메모/썸네일 구현은 그대로 둔다. 페이지 미리보기 연결만 활성 문서 변경에 맞춰 해제·연결한다. 생성자 캡처와 파일 이동 후 reopen 경로도 함께 점검한다.
6. 현재 `widget->window() == &window_` drop filter는 Editor가 자식이 되면 성립하지 않는다. shell/세션 ancestry를 기준으로 한 단일 open router로 바꾼다. recent-file, 링크, Ctrl+O가 기존 문서를 덮어쓰지 않게 한다.
7. Save As 경로 예약→성공 후 identity 변경→기존 claim 해제, 로딩 실패 탭 rollback, 전체 종료 2단계 승인을 추가한다. 기존 라이브러리/메모/파일이동 회귀와 #40 기준 기능을 재실행한다. 그 뒤에만 PR #7에 선별 반영한다.

## 6. 위험과 현실적인 우회

- 공유 설정: PDFExecutionPolicy::setStrategy와 PDFAuthorSettings::setAuthorName은 프로세스 전역이다 (`pdfprogramcontroller.cpp:2125–2126`). theme·author·thread policy·shortcuts는 앱 설정으로 단일화한다. 문서 상태처럼 탭마다 독립이라고 주장하지 않는다.
- plug-in 인스턴스가 분리되어도 모든 plugin 기능이 검증된 것은 아니다. AudioBook/Scanner/Signature 등의 비동기 작업 중 닫기, scene 입력/그리기 인터페이스 해제, 모든 플러그인 파괴 경계를 확인한다. 번들 밖 DLL은 factory 미지원 시 차단한다.
- Editor::showEvent의 taskbar indicator는 embedded windowHandle가 null일 수 있다. shell 하나가 진행 상태를 집계하게 하며 각 탭에서 OS taskbar를 경쟁 갱신하지 않는다.
- 캐시/패키징: 변경 DLL과 플러그인은 동일 빌드로 묶는다. 기존 #40 DLL과 신규 plugin을 섞지 않는다.
- B가 비동기 플러그인 수명 문제로 통과하지 못하면 우선 #40의 독립 창을 유지한다. 이는 단일 창 탭 완료가 아니다. C로 돌아갈 경우 외부 창을 다시 강제 수용하는 대신 명시적인 child-mode/IPC 계약부터 설계한다.

공식 Qt 참고: https://doc.qt.io/qt-6/qpluginloader.html , https://doc.qt.io/qt-6/qwidget.html#createWindowContainer , https://doc.qt.io/qt-6/qmetaobject.html#newInstance . 문서 참조는 런타임 검증을 대체하지 않는다.
