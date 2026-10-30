"""Audit ordinary empty/file launches on a real Windows desktop."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from smoke_windows import synthetic_pdf


def run(package, output):
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='luna-chrome-') as temp:
        documents = []
        for i in range(3):
            path = Path(temp) / f'Public synthetic {i}.pdf'
            path.write_bytes(synthetic_pdf(3))
            documents.append(str(path))
        for scale in ('1', '1.25', '1.5'):
            for mode in ('empty', 'three-pdfs', 'restart'):
                report = output / f'{scale}-{mode}.json'
                env = os.environ.copy()
                env.update(QT_SCALE_FACTOR=scale, LUNA_UI_AUDIT=str(report), LUNA_PERF='1', QT_FORCE_STDERR_LOGGING='1')
                with report.with_suffix('.log').open('w') as log:
                    subprocess.run([str(package / 'LUNA.exe'), *(documents if mode != 'empty' else [])],
                                   env=env, cwd=package, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=120)
                data = json.loads(report.read_text(encoding='utf-8-sig'))
                assert data['accepted'], report
    print('[PASS] Ordinary empty, three-PDF and persisted restart chrome at 100/125/150 percent')


if __name__ == '__main__':
    run(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve())
