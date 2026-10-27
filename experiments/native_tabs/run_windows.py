#!/usr/bin/env python3
"""Only disposable synthetic PDFs are passed to the native architecture proof."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
from smoke_windows import synthetic_pdf


def run(executable, output):
    if sys.platform != 'win32':
        raise RuntimeError('Actual Windows required; no offscreen acceptance')
    output.mkdir(parents=True, exist_ok=True)
    all_results = []
    for scale in ('1', '1.25', '1.5'):
        folder = output / ('scale-' + scale)
        folder.mkdir()
        with tempfile.TemporaryDirectory(prefix='luna-native-synthetic-') as tmp:
            paths = [Path(tmp) / 'A.pdf', Path(tmp) / 'B.pdf']
            # Same-length replacement preserves stream length and xref offsets.
            for path, word in zip(paths, (b'alpha', b'bravo')):
                path.write_bytes(synthetic_pdf(2).replace(b'smoke', word))
            env = os.environ.copy()
            env.update(QT_QPA_PLATFORM='windows', QT_SCALE_FACTOR=scale,
                       QT_FORCE_STDERR_LOGGING='1')
            report = folder / 'result.json'
            with (folder / 'native.log').open('w', encoding='utf-8') as log:
                try:
                    completed = subprocess.run([str(executable), *map(str, paths), str(report)],
                        env=env, stdout=log, stderr=subprocess.STDOUT, timeout=180, check=False)
                    code = completed.returncode
                except subprocess.TimeoutExpired:
                    code = -1
            result = json.loads(report.read_text()) if report.exists() else {'accepted': False, 'error': 'No report'}
            result.update(exitCode=code, qtScale=scale,
                          syntheticOutputHashes=[hashlib.sha256(path.read_bytes()).hexdigest() for path in paths])
            report.write_text(json.dumps(result, indent=2), encoding='utf-8')
            all_results.append(result)
            if code != 0 or result.get('accepted') is not True:
                break
    accepted = len(all_results) == 3 and all(r.get('accepted') and r['exitCode'] == 0 for r in all_results)
    (output / 'summary.json').write_text(json.dumps({'accepted': accepted, 'runs': all_results,
        'scope': 'native PDF4QT sessions; production library integration and physical-monitor DPI unverified'}, indent=2), encoding='utf-8')
    if not accepted:
        raise RuntimeError('Native architecture proof failed; inspect exact phase/error')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('executable', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    run(args.executable.resolve(strict=True), args.output.resolve())
