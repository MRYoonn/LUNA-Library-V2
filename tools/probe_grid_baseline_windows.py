#!/usr/bin/env python3
"""Capture immutable v304 and compare its renderer against the same probe EXE.

Only generated, non-sensitive PDFs are used. The official original EXE supplies
the unchanged six-case screenshots. A separately labelled diagnostic directory
then uses the new read-only probe EXE with the untouched v304 renderer DLLs to
compare Qt/Blend2D and direct/compiled paths at all three zooms. Never distribute
that mixed diagnostic directory or describe its EXE as the original product.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import tempfile
import zipfile

REPO = 'MRYoonn/LUNA-Library-V2'
SOURCE = '97fe534fa96971ced318998e3cad7811240e83da'
ZIP_SHA = '3e925756a02b33073b0f00187eec6aceee58b8b6a435fd96ff015e34ecdc83cc'
MANIFEST_SHA = '869088a2a6d4dd95eec7ed9b13810fdf3558e8c7fdaedada17170f12363a9fb0'


def run(candidate,output,temporary):
    from probe_vector_grid_windows import main as original_six, native_zoom_matrix
    output.mkdir(parents=True)
    download=temporary/'release';download.mkdir()
    subprocess.run(['gh','release','download','v0.9.0.304','--repo',REPO,
        '--pattern','LUNA-Windows-Portable.zip','--pattern','LUNA-Release.json','--dir',str(download)],check=True)
    archive=download/'LUNA-Windows-Portable.zip';manifest=download/'LUNA-Release.json'
    assert hashlib.sha256(archive.read_bytes()).hexdigest()==ZIP_SHA
    assert hashlib.sha256(manifest.read_bytes()).hexdigest()==MANIFEST_SHA
    metadata=json.loads(manifest.read_text())
    assert metadata['commit']==SOURCE and metadata['tag']=='v0.9.0.304'
    package=temporary/'baseline-diagnostic-package'
    with zipfile.ZipFile(archive) as z:
        assert z.testzip() is None
        assert len(z.namelist())==len(set(z.namelist()))==len(metadata['files'])
        assert set(z.namelist())=={v['path'] for v in metadata['files']}
        assert all(not PurePosixPath(n).is_absolute() and '..' not in PurePosixPath(n).parts
                   and ':' not in n and '\\' not in n for n in z.namelist())
        assert all(len(z.read(v['path']))==v['size'] and hashlib.sha256(z.read(v['path'])).hexdigest()==v['sha256'] for v in metadata['files'])
        info=json.loads(z.read('build-info.json'))
        assert info['commit']==SOURCE and info['validation']['windowsCpp']=='passed'
        assert hashlib.sha256(z.read('LUNA.exe')).hexdigest()==info['exeSha256']
        z.extractall(package)
    old=os.environ.pop('LUNA_REQUIRE_GRID_VISIBILITY',None)
    try: original_six(package/'LUNA.exe',output/'original-v304-exe')
    finally:
        if old is not None:os.environ['LUNA_REQUIRE_GRID_VISIBILITY']=old
    original=json.loads((output/'original-v304-exe/grid-matrix.json').read_text())['cases']
    for name in ('short-icc-no-clip','long-icc-no-clip','short-icc-ctm-only'):
        p=original[name]['pixel_metrics'];assert p['icc_gray_pixels']>=400 and p['device_gray_pixels']>=800
    for name in ('short-icc-clip-only','short-icc-nested-clip','long-icc-nested-clip'):
        p=original[name]['pixel_metrics'];assert p['icc_gray_pixels']<400 and p['device_gray_pixels']<800
    # Record exact identities before replacing only this disposable EXE. All
    # renderer/Qt dependencies remain byte-identical to the official release.
    provenance={'officialTag':'v0.9.0.304','officialSource':SOURCE,'officialZipSha256':ZIP_SHA,
        'officialExeSha256':info['exeSha256'],'probeExeSha256':hashlib.sha256(candidate.read_bytes()).hexdigest(),
        'diagnosticOnly':'new read-only capture hook with untouched v304 engine; never a distributable package',
        'rendererFiles':{v['path']:v['sha256'] for v in metadata['files'] if v['path'].endswith('.dll')}}
    (output/'baseline-provenance.json').write_text(json.dumps(provenance,indent=2)+'\n')
    shutil.copyfile(candidate,package/'LUNA.exe')
    env=dict(os.environ,QT_QPA_PLATFORM='windows',QT_SCALE_FACTOR='1',LUNA_METADATA_OFFLINE='1',
        QT_PLUGIN_PATH=str(package),QT_QPA_PLATFORM_PLUGIN_PATH=str(package/'platforms'))
    env['PATH']=os.pathsep.join([str(package),str(Path(os.environ['SystemRoot'])/'System32'),os.environ['SystemRoot']])
    diagnostics=output/'v304-renderer-with-readonly-probe'
    try:
        native_zoom_matrix(package/'LUNA.exe',diagnostics,env)
    except RuntimeError as error:
        if str(error)!='Actual native grid/clip-hole/empty-clip/restore/alpha pixel assertions failed':raise
    else:
        raise AssertionError('Baseline did not reproduce the reported clipping regression')
    rows=json.loads((diagnostics/'native-grid-matrix.json').read_text())
    assert len(rows)==36 and any(not r['passed'] for r in rows)
    for row in rows:
        if row['zoom']==2.0:
            assert row['engineComparisons']['direct-qt']['passed'],row['case']
            assert row['engineComparisons']['compiled-qt']['passed'],row['case']
    assert any(not r['engineComparisons']['direct-blend']['passed'] for r in rows if r['zoom']==2.0)
    assert any(not r['engineComparisons']['compiled-blend']['passed'] for r in rows if r['zoom']==2.0)
    print('[OBSERVED BASELINE FAILURE] Same PDF/geometry succeeds in Qt direct+compiled and fails in v304 Blend2D; source/EXE/DLL hashes retained')
    # Public evidence contains generated PDFs and images, never binary bundles.
    shutil.rmtree(package);shutil.rmtree(download)


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('candidate',type=Path);p.add_argument('output',type=Path)
    a=p.parse_args()
    with tempfile.TemporaryDirectory(prefix='luna-public-v304-baseline-') as temp:
        run(a.candidate.resolve(),a.output.resolve(),Path(temp))
