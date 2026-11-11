"""Prepare exact verified v305 assets and a draft release; public visibility is a separate final step."""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess
import sys
import zipfile


def api(endpoint):
    return json.loads(subprocess.check_output(['gh','api',endpoint],text=True))


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def archive_tree(root,destination):
    files=sorted(p for p in root.rglob('*') if p.is_file())
    with zipfile.ZipFile(destination,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for p in files:
            item=zipfile.ZipInfo(p.relative_to(root).as_posix(),date_time=(2026,1,1,0,0,0))
            item.external_attr=0o100644<<16
            z.writestr(item,p.read_bytes(),compress_type=zipfile.ZIP_DEFLATED,compresslevel=6)


def main():
    payload=json.loads((Path(__file__).resolve().parents[1]/'grid-release-payload.json').read_text())
    repo=payload['repository'];prefix='repos/'+repo
    source=Path(sys.argv[1]).resolve();output=Path(sys.argv[2]).resolve();output.mkdir(parents=True)
    sha=payload['source_sha'];run_id=payload['source_run']
    assert payload['private_original_windows_user_confirmed'] is True
    acceptance=payload['user_acceptance']
    assert acceptance['source_sha']==sha and acceptance['source_run']==run_id and acceptance['scales']==[100,150,200]
    pr=api(f'{prefix}/pulls/{payload["pr"]}')
    assert pr['merged'] and pr['head']['sha']==sha
    assert api(f'{prefix}/git/commits/{pr["merge_commit_sha"]}')['tree']['sha']==api(f'{prefix}/git/commits/{sha}')['tree']['sha']
    assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=source,text=True).strip()==sha
    run=api(f'{prefix}/actions/runs/{run_id}')
    assert run['head_sha']==sha and run['conclusion']=='success' and run['status']=='completed'
    assert run['path']=='.github/workflows/build-windows.yml' and run['event']=='workflow_dispatch'
    jobs=api(f'{prefix}/actions/runs/{run_id}/jobs?per_page=100')
    assert all(j['conclusion'] in ('success','skipped') for j in jobs['jobs'])
    job=next(j for j in jobs['jobs'] if j['name']=='build-windows')
    assert job['conclusion']=='success'
    for phrase in ('metadata Qt native','native Save As dialog','Compile source and install DLLs',
                   'six actual Windows grid','exact v304 failure','Production native tabs',
                   'Public CC BY research','real public PDF bibliography corpus',
                   '125 and 150 percent','CDB and full page heap','validation scope',
                   'release manifest and file hashes','Upload LUNA Windows portable'):
        matches=[s for s in job['steps'] if phrase.casefold() in s['name'].casefold()]
        assert len(matches)==1 and matches[0]['conclusion']=='success',(phrase,matches)
    latest=api(f'{prefix}/releases/latest')
    assert latest['tag_name']==payload['base_tag'] and latest['target_commitish']==payload['base_sha']
    for name,folder in [('LUNA-Windows-Portable','portable'),('LUNA-UI-Synthetic','ui'),('LUNA-Build-Diagnostics','diagnostics')]:
        subprocess.run(['gh','run','download',str(run_id),'--repo',repo,'--name',name,'--dir',str(output/folder)],check=True)
    portable=output/'portable';ui=output/'ui'
    info=json.loads((portable/'build-info.json').read_text(encoding='utf-8-sig'))
    assert info['commit']==sha and int(info['run'])==run_id
    v=info['validation'];assert v['windowsCpp']=='passed' and v['nativeMetadata']=='passed'
    assert v['pythonTests']==236 and v['nativeQtTests']==12
    assert v['publicResearchPdf']=='passed' and v['publicMainAndSiBibliography']=='passed'
    assert v['privateOriginalPdfWindows'].startswith('not verified')
    assert digest(portable/'LUNA.exe')==info['exeSha256']==acceptance['exe_sha256']
    sys.path.insert(0,str(source));from tools.verify_portable import check
    assert not check(portable)
    after=json.loads((ui/'vector-grid/grid-matrix.json').read_text())['cases']
    before=json.loads((ui/'vector-grid-before/original-v304-exe/grid-matrix.json').read_text())['cases']
    rows=json.loads((ui/'vector-grid/native-grid-matrix.json').read_text())
    old_rows=json.loads((ui/'vector-grid-before/v304-renderer-with-readonly-probe/native-grid-matrix.json').read_text())
    assert len(after)==len(before)==6 and set(after)==set(before)
    assert len(rows)==len(old_rows)==36 and all(r['passed'] for r in rows)
    assert all(r['geometry']['blendBackend'] and r['geometry']['nativeWindowCapture'] for r in rows)
    for row in rows:
        if row['zoom']==2:assert all(v['passed'] for v in row['engineComparisons'].values())
    for row in old_rows:
        if row['zoom']==2:
            assert row['engineComparisons']['direct-qt']['passed'] and row['engineComparisons']['compiled-qt']['passed']
    for value in after.values():
        p=value['pixel_metrics'];assert p['icc_gray_pixels']>=400 and p['device_gray_pixels']>=800
    # Publish the exact tested binaries; only packaging/validation metadata changes.
    test_input=output/'release-input';shutil.copytree(portable,test_input)
    release_validation=dict(v,privateOriginalPdfWindows='User confirmed privately at 100/150/200% on Windows; not a public-CI test',privateOriginalPdfCi='not run; original stayed private',privateOriginalPdfUserAcceptanceDate=acceptance['date_kst'])
    test_info=dict(info,status='validated release',validation=release_validation)
    (test_input/'build-info.json').write_text(json.dumps(test_info,ensure_ascii=False,sort_keys=True,indent=2)+'\n')
    package=output/'release-assets';package.mkdir()
    subprocess.run([sys.executable,'-B','-m','tools.prepare_release','--portable',str(test_input),
        '--destination',str(package),'--tag',payload['release_tag'],'--source-run',str(run_id),'--commit',sha],
        cwd=source,stdout=subprocess.DEVNULL,check=True)
    manifest=json.loads((package/'LUNA-Release.json').read_text())
    assert digest(package/'LUNA-Windows-Portable.zip')==manifest['sha256']
    assert manifest['commit']==sha and manifest['source_run']==run_id and manifest['schema']==2
    with zipfile.ZipFile(package/'LUNA-Windows-Portable.zip') as z:
        assert z.testzip() is None and len(z.namelist())==len(set(z.namelist()))==len(manifest['files'])
        assert set(z.namelist())=={f['path'] for f in manifest['files']}
        for f in manifest['files']:
            data=z.read(f['path']);assert len(data)==f['size'] and hashlib.sha256(data).hexdigest()==f['sha256']
        assert hashlib.sha256(z.read('LUNA.exe')).hexdigest()==info['exeSha256']
        assert json.loads(z.read('build-info.json'))==dict(test_info,version=payload['release_tag'])
        assert set(z.namelist())=={p.relative_to(portable).as_posix() for p in portable.rglob('*')
                                  if p.is_file() and p.name!='Luna-Crash-Tools.ps1'}
        transformed={'build-info.json','Analyze-Luna-Crash.ps1','Start-Luna-Crash-Diagnostics.ps1'}
        assert all(z.read(n)==(portable/n).read_bytes() for n in set(z.namelist())-transformed)
    qa=output/'qa';qa.mkdir()
    backend_proof={}
    for name in after:
        trace=[json.loads(line) for line in (ui/'vector-grid'/name/'render-trace.jsonl').read_text(encoding='utf-8-sig').splitlines()]
        changed=[e for e in trace if e['stage']=='backend.path.cull' and e['strokeActive'] and e['centerlineClipMode']==1 and e['clipMode']==2]
        backend_proof[name]={'compiledVectorStrokePaths':sum(e['stage']=='compile.path' and e['stroke'] and not e['text'] for e in trace if e['stage']=='compile.path'),
            'storedClips':sum(e['stage']=='stored.clip' for e in trace),'reclassifiedPaintEvents':len(changed),
            'actualClippedOutlinePaintEvents':sum(e['stage']=='backend.clippedStroke.called' for e in trace),
            'clipVertexCounts':sorted({e['clipPath']['count'] for e in changed}),
            'penWidths':sorted({e['pen']['width'] for e in changed}),
            'allReclassifiedPenOpaque':all(e['pen']['rgba'][3]==1 for e in changed),
            'example':changed[0] if changed else None}
    assert backend_proof['short-icc-clip-only']['reclassifiedPaintEvents']>0
    assert backend_proof['short-icc-clip-only']['actualClippedOutlinePaintEvents']>0
    (qa/'backend-proof-summary.json').write_text(json.dumps(backend_proof,indent=2)+'\n')
    (qa/'windows-run.json').write_text(json.dumps(run,indent=2)+'\n')
    (qa/'windows-jobs.json').write_text(json.dumps(jobs,indent=2)+'\n')
    (qa/'source-build-info.json').write_text(json.dumps(info,indent=2)+'\n')
    job_log='\n'.join(p.read_text(errors='replace') for p in ui.rglob('*.log'))
    printer={'nativeDialogOpenedAndCancelled':'[PASS] Native Ctrl+P dispatched once; print dialog opened and cancelled' in job_log,
             'runnerReportedNoPrinter':'No system printer on runner' in job_log,
             'physicalPaperOutputVerified':False,'privateOriginalWindowsUserConfirmed':True,'originalUsedByPublicCI':False}
    (qa/'printer-observation.json').write_text(json.dumps(printer,indent=2)+'\n')
    for name in ('vector-grid','vector-grid-before'):
        for p in (ui/name).rglob('*'):
            if not p.is_file() or 'settings' in p.parts or p.suffix.lower() not in ('.json','.jsonl','.png','.log','.txt'):continue
            target=qa/p.relative_to(ui);target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,target)
    for name in ('summary.json','edit.json','restore.json'):
        for p in ui.rglob(name):
            target=qa/'regression'/p.relative_to(ui);target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,target)
    for p in (output/'diagnostics').rglob('*'):
        if p.is_file() and p.suffix in ('.json','.md'):
            target=qa/'diagnostics'/p.relative_to(output/'diagnostics');target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,target)
    shutil.copyfile(source/'BUILD_DIAGNOSTICS.md',qa/'BUILD_DIAGNOSTICS.md')
    shutil.copyfile(source/'BUILD_DIAGNOSTICS.md',qa/'source-BUILD_DIAGNOSTICS.md')
    with (qa/'BUILD_DIAGNOSTICS.md').open('a') as record:
        record.write(f'\n\n## Post-build revalidation (generated from completed Actions evidence)\n\n'
            f'- Product source `{sha}`, Windows run `{run_id}`: completed/success. Original source document above is preserved verbatim separately as source-BUILD_DIAGNOSTICS.md.\n'
            f'- Python{v["pythonTests"]}, metadata1 plus native Qt{v["nativeQtTests"]} CTests; all original six visual gates, 36 native positive/negative clip captures and all four renderer comparisons passed.\n'
            '- Immutable v304 baseline reproduced missing Blend strokes while direct/compiled Qt succeeded. Required tabs, library/file management, annotations, public-paper/SI/corpus, DPI, CDB/full page heap and package hashes succeeded; per-step API outcomes are in windows-jobs.json.\n'
            f'- Owner confirmed the private original at100/150/200% on Windows on {acceptance["date_kst"]}; source EXE hash is recorded in user-acceptance.json. Public CI never received the original. PR119 is merged.\n'
            '- First QA packaging run43053218029 failed only because a newer gh CLI refused raw ANSI job-log output. Read the already retained child log files instead; product binaries and all Windows validation remain unchanged.\n')
    shutil.copyfile(source/'tools/probe_synthetic_grid.py',qa/'probe_synthetic_grid.py')
    (qa/'user-acceptance.json').write_text(json.dumps(acceptance,indent=2)+'\n')
    lines=['# LUNA P0 PDF 격자선 — Windows 검증 및 v305 정식 배포','',
        '**합성 Windows 전체 검증 성공과 사용자 원본의 Windows100/150/200% 정상 확인을 완료했습니다. 원본 확인은 사용자가 비공개 PC에서 수행했으며 공개 CI 결과와 구분합니다.**','',
        f"기준 {payload['base_tag']} / `{payload['base_sha']}`. 검증 소스 `{sha}`.",
        f"[전체 Windows #{run['run_number']}](https://github.com/{repo}/actions/runs/{run_id}) 성공. [병합된 PR119](https://github.com/{repo}/pull/119), [Issue106](https://github.com/{repo}/issues/106).",'',
        '## 정확한 원인과 수정','',
        '정상 clip 교집합을 Qt가 네 점의 implicit-closed 경로로 반환하면 Blend2D의 다섯 점 사각형 최적화를 벗어난다. 일반 경로 가시성 검사가 선의 실제 두께를 만들기 전에 중심선의 bounding rectangle을 검사한다. 수직/수평 중심선은 폭/높이가 0이므로 QRectF::intersects가 false를 반환해 NotVisible로 버린다.','',
        'PDF 연산자·컴파일·저장된 clip과 CTM/불투명 pen은 정상이며 stroke는 QPainter까지 도달했다. 백엔드 계측에서 old centerline NotVisible=1 → stroked coverage NeedsResolve=2와 최종 clipped outline paint를 직접 확인했다. 보정은 한 함수의 조기 탈락 판정만 같은 기존 QPainterPathStroker 외곽으로 다시 확인한다. clip·CTM·선 두께·RGBA는 변경하지 않았고, 클리핑 생략/엔진 교체/PDF 수정은 하지 않았다.','',
        '## 실제 백엔드 계측 요약','',
        '| 조건 | 컴파일된 vector stroke 경로 | 저장 clip | old NotVisible → NeedsResolve paint events | 실제 clipped outline paint events |','|---|---:|---:|---:|---:|']
    for name,p in backend_proof.items():
        lines.append(f"| {name} | {p['compiledVectorStrokePaths']} | {p['storedClips']} | {p['reclassifiedPaintEvents']} | {p['actualClippedOutlinePaintEvents']} |")
    example=backend_proof['short-icc-clip-only']['example']
    lines += ['',f"대표 device-space 중심선 bounds `{example['centerPath']['bounds']}` → 실제 stroke coverage `{example['strokePath']['bounds']}`. clip 요소 {example['clipPath']['count']}개, empty={example['clipPath']['empty']}, pen alpha={example['pen']['rgba'][3]}, width={example['pen']['width']}pt. paint event는 재그리기를 포함하며 고유 PDF 경로 수와 구분한다.",'',
        '## 원래 6종 실제 LUNA 화면 픽셀','',
        '| 조건 | 정식 v304 원본 EXE ICC / Device | 수정 EXE ICC / Device |','|---|---:|---:|']
    for name in after:
        a=after[name]['pixel_metrics'];b=before[name]['pixel_metrics']
        lines.append(f"| {name} | {b['icc_gray_pixels']} / {b['device_gray_pixels']} | {a['icc_gray_pixels']} / {a['device_gray_pixels']} |")
    lines += ['', '기존 입력 바이트 및 ICC>=400 / Device>=800 판정 유지. 이전 Device182는 문자 안티앨리어싱 혼입이며 격자 복구가 아니다.','',
        '## 확장된 실제 native 창 검사','',
        '12개 입력 × 100/150/200% = 36개 top-level Windows LUNA 창. 각 PDF→화면 행렬로 격자 위치를 측정하고 실제 native 창 및 데스크톱 PNG를 보존했다. 중첩/tight clip, even-odd 구멍, 삼각형, 빈 교집합, save/restore 표시, alpha를 포함한다. 보여야 할 선과 숨겨야 할 영역 모두 통과했다. 200%에서 direct/compiled × Qt/Blend48개 추가 이미지 판정도 통과했다.','',
        '별도 통제 비교는 고정 v304 ZIP/manifest/전체 파일 해시를 검증했다. original-v304-exe는 실제 배포 EXE 자체의 6종 캡처다. v304-renderer-with-readonly-probe는 새 read-only 캡처 EXE와 변경하지 않은 v304 renderer DLL을 결합한 진단 전용 비교이며 배포본 EXE라고 부르지 않는다. 원래 Qt direct/compiled는 정상, 원래 Blend는 격자 소실, 수정 Blend는 정상임을 비교했다. 원본 DLL/EXE/새 probe 해시는 baseline-provenance.json에 있다.','',
        '| 실제 native 200% | 수정 전 v304 렌더러 | 수정 후 |','|---|---|---|',
        '| clip-only | ![before](vector-grid-before/v304-renderer-with-readonly-probe/native-matrix/short-icc-clip-only/2.0/render.json.native.png) | ![after](vector-grid/native-matrix/short-icc-clip-only/2.0/render.json.native.png) |',
        '| even-odd 구멍 | ![before](vector-grid-before/v304-renderer-with-readonly-probe/native-matrix/hole-evenodd/2.0/render.json.native.png) | ![after](vector-grid/native-matrix/hole-evenodd/2.0/render.json.native.png) |','',
        '## 전체 회귀와 실제 소스','',
        f"Python {v['pythonTests']}개, native metadata1 + native Qt{v['nativeQtTests']} CTests, Windows C++/Portable, 탭/배율 복원·주석/형광펜/기존 인쇄 회귀·라이브러리 파일 관리·공개 논문/SI·31PDF·CDB/Full Page Heap 및 manifest/파일 해시 통과. 선택 installer 등은 windows-jobs.json의 skipped로 별도 기록했다. 실물 인쇄 출력은 수행하지 않았습니다. 사용자 원본 Windows 표시는 사용자 확인이며 공개 CI가 원본을 검사했다고 주장하지 않습니다. 실제 Ctrl+P 대화상자 확인 범위는 printer-observation.json과 원본 job 로그를 따른다.",'',
        '주요 변경: tools/patch_blend_clip_strokes.py(고정 upstream pdfblpainter.cpp의 조기 cull 수정), src/lunarendertrace.h 및 tools/patch_pdf_render_trace.py(명시적 계측), src/lunagridprobe.h 및 tools/apply_overlay.py(실제 창 캡처·4경로 비교), tools/probe_synthetic_grid.py / probe_vector_grid_windows.py / probe_grid_baseline_windows.py(입력·픽셀·고정 baseline 검증), Windows workflow/preflight/BUILD_DIAGNOSTICS.md(필수 게이트·기록).','',
        '## 원본 보호와 배포 여부','',
        '사용자 원본은 로컬에서 읽기 전용으로 분석했으며 원본/원본 화면/내용 로그는 저장소·CI·이 QA ZIP에 전송하지 않았다. 사용자가 제공한 성공한 CI 테스트 EXE로 원본을 Windows에서100/150/200% 모두 정상 확인했다고 이 대화에서 명시적으로 보고했다. 동일 EXE 해시를 유지해 v305를 배포하며 PR119를 병합했다. v304 기존 자산은 그대로 보존한다.','',
        '정식 식별은 v0.9.0.305/source commit/run 및 build-info를 사용한다. 기존 자동업데이트 자산 이름과 manifest schema2를 유지하며 새 v305 자산을 생성한다. v304 파일을 덮어쓰지 않는다.','',
        f"v305 Portable ZIP SHA256: `{manifest['sha256']}`. 파일별 크기/해시는 LUNA-Release.json. EXE는 성공한 CI 원본과 바이트가 같다. 소스/실행/EXE/ZIP CRC/모든 파일 집합과 SHA256을 재검증했다."]
    lines += ['', '## 합성 입력별 native 창 결과', '',
              '| 입력 | v304 100/150/200% | 수정 100/150/200% |', '|---|---|---|']
    for name in dict.fromkeys(r['case'] for r in rows):
        old=' / '.join('PASS' if r['passed'] else 'FAIL' for r in old_rows if r['case']==name)
        new=' / '.join('PASS' if r['passed'] else 'FAIL' for r in rows if r['case']==name)
        lines.append(f'| {name} | {old} | {new} |')
    report='\n'.join(lines)+'\n';(qa/'QA-REPORT.md').write_text(report)
    asset_base=f'https://github.com/{repo}/releases/download/{payload["release_tag"]}/'
    image_sources={
        'grid-before-clip-only-200.png': 'vector-grid-before/v304-renderer-with-readonly-probe/native-matrix/short-icc-clip-only/2.0/render.json.native.png',
        'grid-after-clip-only-200.png': 'vector-grid/native-matrix/short-icc-clip-only/2.0/render.json.native.png',
        'grid-before-evenodd-200.png': 'vector-grid-before/v304-renderer-with-readonly-probe/native-matrix/hole-evenodd/2.0/render.json.native.png',
        'grid-after-evenodd-200.png': 'vector-grid/native-matrix/hole-evenodd/2.0/render.json.native.png',
    }
    for name,relative in image_sources.items():
        shutil.copyfile(ui/relative,package/name)
        report=report.replace(']('+relative+')',']('+asset_base+name+')')
    (package/'LUNA-QA-Report.md').write_text(report)
    assert not any(p.suffix.lower() in ('.pdf','.exe','.dll') for p in qa.rglob('*') if p.is_file())
    checks={p.relative_to(qa).as_posix():digest(p) for p in sorted(qa.rglob('*')) if p.is_file()}
    (qa/'evidence-sha256.json').write_text(json.dumps(checks,indent=2)+'\n')
    archive_tree(qa,package/'LUNA-QA-Evidence.zip')
    (package/'SHA256SUMS.txt').write_text('\n'.join(digest(p)+'  '+p.name for p in sorted(package.iterdir()) if p.is_file())+'\n')
    final=api(f'{prefix}/releases/latest')
    assert final['id']==latest['id'] and {a['name']:a['digest'] for a in final['assets']}=={a['name']:a['digest'] for a in latest['assets']}
    assert {a['name']:a['digest'] for a in latest['assets']}==payload['base_asset_digests']
    expected_names={'LUNA-Windows-Portable.zip','LUNA-Release.json','LUNA-QA-Evidence.zip','LUNA-QA-Report.md',*image_sources}
    expected={p.name:{'size':p.stat().st_size,'digest':'sha256:'+digest(p)}
              for p in package.iterdir() if p.name in expected_names}
    assert set(expected)==expected_names
    (package/'expected-assets.json').write_text(json.dumps(expected,indent=2)+'\n')
    notes=f'''HWP/Quartz PDF에서 표 격자선이 클리핑 뒤 사라지던 P0 오류를 수정했습니다. 사용자가 성공한 Windows 빌드로 비공개 원본을 100%·150%·200%에서 정상 확인했습니다.

원인: PDF4QT Blend2D 백엔드가 일반 clip 경로에서 stroke 외곽을 만들기 전에 폭 또는 높이가 0인 중심선 bounds를 검사해 유효한 선을 NotVisible로 버렸습니다. 같은 pen/CTM의 실제 stroke coverage로 조기 탈락만 재검사하고 정상 clip 교집합을 유지합니다. PDF·clip·선 두께·색상·렌더링 엔진을 변경하지 않습니다.

검증: 기존 6종 픽셀 게이트, 12입력×3배율=36개 실제 Windows LUNA 창, 48개 direct/compiled Qt/Blend 비교 통과. Python236 및 native metadata1+Qt12 CTests, Windows C++ 빌드, 탭/배율·주석/형광펜·기존 인쇄 회귀·파일 관리·공개 PDF31종·CDB/Full Page Heap·Portable/업데이트 해시 검증을 통과했습니다. 실물 인쇄는 검사하지 않았습니다. 비공개 원본 확인은 사용자 결과이며 원본을 공개 CI에 보내지 않았습니다.

- [전체 Windows 검증](https://github.com/{repo}/actions/runs/{run_id}) / [병합 PR #119](https://github.com/{repo}/pull/119) / [Issue #106](https://github.com/{repo}/issues/106)
- [기술 원인·조건별 측정 보고서]({asset_base}LUNA-QA-Report.md) / [전후 화면·측정값·진단 로그 ZIP]({asset_base}LUNA-QA-Evidence.zip)
- 검증·태그 소스: `{sha}`. 원본 확인한 EXE SHA256: `{info['exeSha256']}`.
- Portable ZIP SHA256: `{manifest['sha256']}`. 자동 업데이트 manifest schema2 및 파일별 SHA256 유지. 기존 v304 자산은 보존했습니다.

아래 비교는 원본 연구노트가 아닌 공개 합성 입력의 실제 Windows native 창입니다. 수정 전은 읽기 전용 캡처 EXE와 변경하지 않은 v304 renderer DLL의 통제 비교이며, 정식 v304 EXE 자체의 원래 6종 캡처도 QA ZIP에 포함합니다.

| 200% 실제 화면 | 수정 전 v304 렌더러 | 수정 후 |
|---|---|---|
| clip-only | ![before]({asset_base}grid-before-clip-only-200.png) | ![after]({asset_base}grid-after-clip-only-200.png) |
| even-odd hole | ![before]({asset_base}grid-before-evenodd-200.png) | ![after]({asset_base}grid-after-evenodd-200.png) |
'''
    notes_path=package/'RELEASE-NOTES.md';notes_path.write_text(notes)
    # The job stages a draft only. Publication follows independent asset review.
    releases=api(f'{prefix}/releases?per_page=100')
    existing=[r for r in releases if r['tag_name']==payload['release_tag']]
    assert len(existing)<=1
    if not existing:
        subprocess.run(['gh','release','create',payload['release_tag'],'--repo',repo,
                        '--draft','--target',sha,'--title','LUNA Library v0.9.0.305 — PDF clipping grid fix',
                        '--notes-file',str(notes_path)],check=True)
        existing=[r for r in api(f'{prefix}/releases?per_page=100') if r['tag_name']==payload['release_tag']]
    release=existing[0]
    assert release['draft'] and not release['prerelease'] and release['target_commitish']==sha
    remote={a['name']:a for a in release['assets']}
    assert set(remote)<=expected_names
    for name in sorted(expected):
        if name in remote:
            assert remote[name]['digest']==expected[name]['digest'] and remote[name]['size']==expected[name]['size']
        else:
            subprocess.run(['gh','release','upload',payload['release_tag'],str(package/name),'--repo',repo],check=True)
    release=api(f'{prefix}/releases/{release["id"]}')
    assert release['draft'] and release['target_commitish']==sha
    assert len(release['assets'])==8 and {a['name'] for a in release['assets']}==expected_names
    for asset in release['assets']:
        assert asset['state']=='uploaded'
        assert asset['digest']==expected[asset['name']]['digest'] and asset['size']==expected[asset['name']]['size']
    (package/'draft-release.json').write_text(json.dumps(release,indent=2)+'\n')
    final=api(f'{prefix}/releases/latest')
    assert final['id']==latest['id'] and {a['name']:a['digest'] for a in final['assets']}==payload['base_asset_digests']
    print(json.dumps(dict(source=sha,run=run_id,tag=payload['release_tag'],privateOriginalWindowsUserConfirmed=True,zipSha256=manifest['sha256'],files=len(manifest['files'])),indent=2))


if __name__=='__main__':main()
