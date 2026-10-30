"""Audit ordinary empty/file launches on a real Windows desktop."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import hashlib
from urllib.request import Request, urlopen
from smoke_windows import synthetic_pdf


def run(package, output, public_paper=False):
    output.mkdir(parents=True, exist_ok=True)
    data = synthetic_pdf(3)
    if public_paper:
        from probe_research_windows import URL, SHA256, CREDIT
        with urlopen(Request(URL, headers={'User-Agent': 'LUNA-public-fixture-validation'}), timeout=30) as response:
            data = response.read(2_000_001)
        if hashlib.sha256(data).hexdigest() != SHA256:
            raise RuntimeError('Pinned public paper checksum mismatch')
        (output / 'PUBLIC_PAPER_CREDIT.txt').write_text(CREDIT, encoding='utf-8')
    with tempfile.TemporaryDirectory(prefix='luna-chrome-') as temp:
        documents = []
        for i in range(3):
            name = 'Public research paper' if public_paper else 'Public synthetic'
            path = Path(temp) / f'{name} {i}.pdf'
            path.write_bytes(data)
            documents.append(str(path))
        for scale in ('1', '1.25', '1.5'):
            for mode in ('empty', 'three-pdfs', 'restart'):
                report = output / f'{scale}-{mode}.json'
                env = os.environ.copy()
                env.update(QT_SCALE_FACTOR=scale, LUNA_UI_AUDIT=str(report), LUNA_AUDIT_ROOT=temp, LUNA_PERF='1', QT_FORCE_STDERR_LOGGING='1')
                env.update(QT_QPA_PLATFORM='windows', QT_PLUGIN_PATH=str(package), QT_QPA_PLATFORM_PLUGIN_PATH=str(package / 'platforms'))
                with report.with_suffix('.log').open('w') as log:
                    subprocess.run([str(package / 'LUNA.exe'), *(documents if mode != 'empty' else [])],
                                   env=env, cwd=package, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=120)
                data = json.loads(report.read_text(encoding='utf-8-sig'))
                assert data['accepted'], report
    print('[PASS] Ordinary empty, three-PDF and persisted restart chrome at 100/125/150 percent')


if __name__ == '__main__':
    run(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve(), '--public-paper' in sys.argv[3:])
