"""Publish only the exact, already successful Windows #290 source artifact.

This separate delivery commit changes no product source. Public QA evidence is
staged as content-addressed Git blobs, never PDFs, credentials or user files.
"""
import argparse
import base64
import hashlib
import json
from pathlib import Path, PurePosixPath
import subprocess
import sys
import zipfile


def gh_json(endpoint, *arguments):
    result = subprocess.run(['gh', 'api', endpoint, *arguments], check=True,
                            capture_output=True, text=True)
    return json.loads(result.stdout)


def verify_assets(release, expected):
    assets = release['assets']
    assert len(assets) == len(expected) == 8
    assert {a['name'] for a in assets} == set(expected)
    for asset in assets:
        assert asset['state'] == 'uploaded'
        assert {k: asset[k] for k in ('size', 'digest')} == expected[asset['name']], asset['name']


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--portable', type=Path)
    parser.add_argument('--verify-only', action='store_true')
    args = parser.parse_args()
    payload = json.loads((Path(__file__).resolve().parents[1]/'release-v290-payload.json').read_text())
    repo = payload['repository']
    assert repo == 'MRYoonn/LUNA-Library-V2'
    prefix = f'repos/{repo}'
    sha, run_id, tag = payload['source_sha'], payload['source_run'], payload['tag']
    assert sha == '418ee1154abdb817b665faca8ddf432ba3c7452f'
    assert run_id == 42152060456 and tag == 'v0.9.0.290'
    actual = subprocess.check_output(['git','rev-parse','HEAD'],cwd=args.source,text=True).strip()
    assert actual == sha
    assert not subprocess.check_output(['git','status','--porcelain','--untracked-files=no'],cwd=args.source,text=True)
    args.output.mkdir(parents=True, exist_ok=True)
    run = gh_json(f'{prefix}/actions/runs/{run_id}')
    assert run['head_sha'] == sha and run['conclusion'] == 'success' and run['status'] == 'completed'
    assert run['path'] == '.github/workflows/build-windows.yml' and run['event'] == 'workflow_dispatch'
    jobs = gh_json(f'{prefix}/actions/runs/{run_id}/jobs?per_page=100')
    assert all(j['conclusion'] in ('success','skipped') for j in jobs['jobs'])
    job = next(j for j in jobs['jobs'] if j['name'] == 'build-windows')
    assert job['conclusion'] == 'success'
    for phrase in (
        'metadata Qt native', 'native Save As dialog', 'Compile source and install DLLs',
        'Ordinary launch visibility', 'direct v261 screenshot comparison',
        'Same-machine PDF open benchmark', 'Independent document windows',
        'Production native tabs', 'Public CC BY research PDF',
        'real public PDF bibliography corpus', '125 and 150 percent',
        'CDB and full page heap', 'Record validation scope',
        'release manifest and file hashes', 'Upload LUNA Windows portable',
    ):
        matches = [s for s in job['steps'] if phrase.casefold() in s['name'].casefold()]
        assert len(matches) == 1 and matches[0]['conclusion'] == 'success', phrase
    (args.output/'source-run.json').write_text(json.dumps(run,indent=2)+'\n')
    (args.output/'source-jobs.json').write_text(json.dumps(jobs,indent=2)+'\n')
    portable = args.portable or args.output/'portable'
    if args.portable is None:
        subprocess.run(['gh','run','download',str(run_id),'--repo',repo,
                        '--name','LUNA-Windows-Portable','--dir',str(portable)],check=True)
    info = json.loads((portable/'build-info.json').read_text(encoding='utf-8-sig'))
    assert info['commit'] == sha and int(info['run']) == run_id
    validation = info['validation']
    assert validation['windowsCpp'] == 'passed' and validation['nativeMetadata'] == 'passed'
    assert validation['pythonTests'] == 195 and validation['nativeQtTests'] == 11
    assert validation['publicResearchPdf'] == 'passed' and validation['publicMainAndSiBibliography'] == 'passed'
    assert hashlib.sha256((portable/'LUNA.exe').read_bytes()).hexdigest() == info['exeSha256']
    dist = args.output/'dist'
    subprocess.run([sys.executable,'-B','-m','tools.prepare_release','--portable',str(portable),
                    '--destination',str(dist),'--tag',tag,'--source-run',str(run_id),'--commit',sha],
                   cwd=args.source,check=True,stdout=subprocess.DEVNULL)
    for name, blob_sha in payload['blobs'].items():
        assert PurePosixPath(name).name == name and ':' not in name and '\\' not in name
        blob = gh_json(f'{prefix}/git/blobs/{blob_sha}')
        assert blob['sha'] == blob_sha and blob['encoding'] == 'base64'
        data = base64.b64decode(blob['content'])
        git_sha = hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()
        assert git_sha == blob_sha
        (dist/name).write_bytes(data)
    for name, expected in payload['expected'].items():
        data = (dist/name).read_bytes()
        assert len(data) == expected['size']
        assert 'sha256:'+hashlib.sha256(data).hexdigest() == expected['digest'], name
    manifest = json.loads((dist/'LUNA-Release.json').read_text())
    assert manifest['schema'] == 2 and manifest['tag'] == tag and manifest['commit'] == sha
    assert manifest['source_run'] == run_id
    with zipfile.ZipFile(dist/'LUNA-Windows-Portable.zip') as archive:
        assert archive.testzip() is None
        names = archive.namelist()
        assert len(names) == len(set(names)) == len(manifest['files']) == 135
        assert set(names) == {f['path'] for f in manifest['files']}
        assert all(not PurePosixPath(n).is_absolute() and '..' not in PurePosixPath(n).parts
                   and ':' not in n and '\\' not in n and not n.lower().endswith(('.pdf','.db','.sqlite')) for n in names)
        for file in manifest['files']:
            data = archive.read(file['path'])
            assert len(data) == file['size'] and hashlib.sha256(data).hexdigest() == file['sha256']
        assert json.loads(archive.read('build-info.json')) == dict(info,version=tag)
        assert hashlib.sha256(archive.read('LUNA.exe')).hexdigest() == info['exeSha256']
    with zipfile.ZipFile(dist/'LUNA-QA-Evidence.zip') as archive:
        assert archive.testzip() is None
        assert len(archive.namelist()) == len(set(archive.namelist()))
        evidence = json.loads(archive.read('evidence-sha256.json'))
        assert set(archive.namelist()) == set(evidence)|{'evidence-sha256.json'}
        assert all(hashlib.sha256(archive.read(n)).hexdigest() == h for n,h in evidence.items())
        assert not any(n.lower().endswith(('.pdf','.exe','.dll')) for n in archive.namelist())
        score = json.loads(archive.read('windows-native-score.json'))
        assert score['documents'] == 31 and score['exact'] == 15
        assert all(v['wrong'] == 0 for v in score['fields'].values())
    if args.verify_only:
        print('Verified exact source/run, all required gates, Portable and staged public QA bytes; no upload or publication.')
        return
    release_id = payload['release_id']
    release = gh_json(f'{prefix}/releases/{release_id}')
    assert release['tag_name'] == tag and release['target_commitish'] == sha
    assert release['draft'] and not release['prerelease']
    assert release['body'].strip() == (dist/'release-notes.md').read_text().strip()
    # Permit a retry after a partial upload, never replace an existing asset.
    existing = {a['name']:a for a in release['assets']}
    assert set(existing) <= set(payload['expected'])
    for name, asset in existing.items():
        assert asset['state'] == 'uploaded'
        assert {k:asset[k] for k in ('size','digest')} == payload['expected'][name]
    pending = [str(dist/n) for n in payload['expected'] if n not in existing]
    if pending:
        subprocess.run(['gh','release','upload',tag,'--repo',repo,*pending],check=True)
    uploaded = gh_json(f'{prefix}/releases/{release_id}')
    verify_assets(uploaded,payload['expected'])
    latest = gh_json(f'{prefix}/releases/latest')
    assert latest['tag_name'] == 'v0.9.0.261'
    assert latest['target_commitish'] == 'c40470847d3f22fbb7c993d2c91dfae6b6b50c4c'
    request = args.output/'publish-request.json'
    request.write_text(json.dumps({'draft':False,'prerelease':False,'make_latest':'true'}))
    published = gh_json(f'{prefix}/releases/{release_id}','--method','PATCH','--input',str(request))
    verify_assets(published,payload['expected'])
    assert not published['draft'] and not published['prerelease']
    assert published['target_commitish'] == sha
    final_latest = gh_json(f'{prefix}/releases/latest')
    assert final_latest['id'] == release_id and final_latest['tag_name'] == tag
    ref = gh_json(f'{prefix}/git/ref/tags/{tag}')
    assert ref['object']['type'] == 'commit' and ref['object']['sha'] == sha
    (args.output/'published-release.json').write_text(json.dumps(published,indent=2)+'\n')
    print('Published verified exact-source release:',published['html_url'])


if __name__ == '__main__':
    main()
