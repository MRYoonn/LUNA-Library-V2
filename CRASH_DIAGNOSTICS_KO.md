# 실제 Windows 10 크래시 진단 — P0 미해결

`b5aa0fb`에서 실제 PC의 `C0000005` 접근 위반이 확인됐습니다. 이 키트는 EXE나 PDF 엔진을 바꾸지 않으며, 원인 수정본이 아닙니다.

1. 다른 LUNA 창의 변경 사항을 저장하고 정상 종료합니다. 키트의 파일들을 `b5aa0fb` 포터블의 `LUNA.exe` 옆에 풉니다.
2. `Start-Luna-Crash-Diagnostics.cmd`를 더블클릭합니다. Microsoft 서명으로 검증한 ProcDump를 사용자 캐시에 내려받습니다. 관리자 권한은 필요하지 않습니다. Microsoft Sysinternals 사용 조건이 적용됩니다.
3. CMD에 **Attached to LUNA PID …**가 표시된 뒤 기존과 같은 PDF 열기·Ctrl+W 순서로 재현합니다. 이 창을 실행 중에는 닫지 마세요. 충돌하지 않으면 문서를 저장하고 LUNA를 정상 종료합니다.
4. 바탕화면의 일반 `LUNA-Diagnostics-날짜시간.zip`에는 기존처럼 빌드·lifecycle·외부 프로세스 상태·종료 코드만 포함합니다. 별도의 `…-PRIVATE-CRASH` 폴더에 미니덤프와 ProcDump 로그를 남깁니다. 덤프를 만들지 못했다면 재현 성공이나 해결 완료로 판정하지 않습니다.
5. `Analyze-Luna-Crash.cmd`를 더블클릭하면 가장 최근의 해당 덤프를 로컬에서 분석합니다. CDB가 없으면 Microsoft Debugging Tools만 설치하며, Windows가 이 SDK 구성 요소 설치에 UAC 승인을 요구할 수 있습니다. 거부해도 기존 덤프는 보존됩니다. Microsoft 심벌을 내려받을 수 있으나 덤프를 전송하지 않습니다.
6. `stack-summary.txt`에는 예외 코드·모듈·심벌·호출 프레임을 추립니다. 내용을 검토한 뒤 이 텍스트와 일반 진단 ZIP의 종료 정보를 비공개로 전달하세요. 심벌이 없으면 모듈+오프셋까지만 나올 수 있으며, 그 자체를 원인 확정으로 간주하지 않습니다.

**`.dmp`, `analysis-PRIVATE.txt`, ProcDump 원본 로그에는 PDF·주석·개인정보·로컬 경로가 포함될 수 있습니다. 공개 GitHub·Issue·PR·Actions Artifact에 올리지 마세요. 자동 업로드 기능은 없습니다.** 이 파일들은 일반 진단 ZIP에 포함하지 않습니다. 분석 후 필요 없으면 사용자가 해당 PRIVATE 폴더를 삭제할 수 있습니다.

이미 다른 디버거가 붙어 있거나 회사 정책이 ProcDump를 차단하면 연결에 실패할 수 있습니다. 기존 문서를 강제 종료하지 않으므로 저장 후 정상 종료하고 남은 로컬 로그를 확인하세요. Windows 이벤트 로그가 비어 있어도 ProcDump가 연결된 뒤 발생한 미처리 예외를 직접 포착하는 방식입니다.

실제 호출 스택을 근거로 수정하고 Windows 회귀 검사 및 실제 PC 재확인을 마치기 전까지 P0는 미해결로 유지합니다.
