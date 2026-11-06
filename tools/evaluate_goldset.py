#!/usr/bin/env python3
"""Explicit public-PDF evaluation. Never discovers or uploads user documents.

Only the versioned manifest is public. Downloaded PDFs, JATS, text snapshots and
local paths stay outside the repository and never enter the portable package.
"""
import argparse
from email.utils import parsedate_to_datetime
import hashlib
import io
import json
from pathlib import Path
import re
import subprocess
import time
import unicodedata
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen
import zipfile

FIELDS = ('title', 'authors', 'journal', 'year', 'volume', 'page', 'doi', 'documentKind')
DEFAULT_MANIFEST = Path(__file__).resolve().parents[1] / 'tests/goldset/public-papers.json'


def canonical(value):
    return ''.join(c for c in unicodedata.normalize('NFKC', str(value)).casefold() if c.isalnum())


def field_value(key, value):
    text = unicodedata.normalize('NFKC', str(value)).strip().casefold()
    if key == 'doi':
        # DOI punctuation is identity, not presentation. A missing slash or
        # changed decimal point must never score as the exact registered DOI.
        return text
    if key in ('year', 'volume', 'documentKind'):
        return text
    if key == 'page':
        for dash in ('–', '—', '−'):
            text = text.replace(dash, '-')
        return ''.join(text.split())
    return canonical(text)


def score(manifest, predictions):
    """Lexical exactness, official journal aliases, no fuzzy substring matching.

    Punctuation/spacing/case are ignored; words/numbers are never dropped. SI
    empty fields require abstention. Article numbers and ranges share the page
    metric, but remain separate fields in production storage. Missing records
    are failures, including document type (never silently assume article).
    """
    counts = {k: dict(correct=0, missing=0, wrong=0, total=0) for k in FIELDS}
    rows = []
    for item in manifest:
        present = item['id'] in predictions
        actual = dict(predictions.get(item['id'], {}))
        # Production treats a successfully read, non-SI document as an article.
        if present and actual and not actual.get('evaluationError'):
            actual.setdefault('documentKind', 'article')
        actual['page'] = actual.get('page') or actual.get('article_number', '')
        statuses = {}
        for key in FIELDS:
            expected = item['expected'][key]
            accepted = expected if isinstance(expected, list) else [expected]
            value = actual.get(key, '')
            if present and field_value(key, value) in {field_value(key, x) for x in accepted}:
                status = 'correct'
            elif not value:
                status = 'missing'
            else:
                status = 'wrong'
            counts[key][status] += 1
            counts[key]['total'] += 1
            statuses[key] = status
        rows.append(dict(id=item['id'], publisher=item['publisher'], fields=statuses,
                         exact=all(x == 'correct' for x in statuses.values())))
    return dict(documents=len(rows), exact=sum(x['exact'] for x in rows), fields=counts, rows=rows,
                normalization='Title/author/journal: Unicode NFKC, case, punctuation and whitespace; official journal aliases. DOI retains punctuation; year/volume/type exact; page dash/whitespace normalization. No fuzzy matching.')


def download_public_fixture(request):
    # The same pinned fixture must succeed; a transient service failure is
    # neither an extraction result nor permission to skip a goldset member.
    for attempt in range(3):
        delay = 2 ** attempt
        try:
            with urlopen(request, timeout=120) as response:
                return response.read(100_000_001)
        except HTTPError as error:
            if error.code not in (408, 429, 500, 502, 503, 504) or attempt == 2:
                raise
            delay = 30 * (2 ** attempt) if error.code == 429 else delay
            retry_after = (error.headers or {}).get('Retry-After', '')
            if retry_after:
                try:
                    delay = max(0, int(retry_after))
                except ValueError:
                    try:
                        delay = max(0, parsedate_to_datetime(retry_after).timestamp() - time.time())
                    except (TypeError, ValueError, OverflowError):
                        pass
                # A longer service cooldown is a failure for this attempt;
                # never retry earlier than the server allows to fit CI time.
                if delay > 300:
                    raise
        except (URLError, TimeoutError):
            if attempt == 2:
                raise
        time.sleep(delay)


def fetch(manifest, destination):
    destination.mkdir(parents=True, exist_ok=True)
    failures = []
    for item in manifest:
        identifier = item['id']
        if not re.fullmatch(r'PMC\d+(?:-SI\d+)?', identifier):
            raise ValueError('Invalid public fixture identifier')
        path = destination / (identifier + '.pdf')
        if path.exists() and hashlib.sha256(path.read_bytes()).hexdigest() == item['sha256']:
            continue
        try:
            request = Request(item['pdf_url'], headers={'User-Agent': 'LUNA-public-fixture-validation'})
            data = download_public_fixture(request)
            if len(data) > 100_000_000:
                raise ValueError('Fixture exceeds download bound')
            if 'archive_member' in item:
                with zipfile.ZipFile(io.BytesIO(data)) as archive:
                    member = item['archive_member']
                    if archive.namelist().count(member) != 1 or archive.getinfo(member).file_size > 30_000_000:
                        raise ValueError('Supplement member changed or exceeds bound')
                    data = archive.read(member)
            if not data.startswith(b'%PDF-') or hashlib.sha256(data).hexdigest() != item['sha256']:
                raise ValueError('Fixture checksum mismatch')
            path.write_bytes(data)
        except Exception as error:
            failure = dict(id=identifier, error=type(error).__name__)
            if isinstance(error, HTTPError):
                failure['httpStatus'] = error.code
                retry_after = (error.headers or {}).get('Retry-After', '')
                if retry_after:
                    failure['retryAfter'] = str(retry_after)[:80]
            failures.append(failure)
    return failures


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument('--predictions', type=Path)
    parser.add_argument('--fetch', type=Path)
    parser.add_argument('--exe', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text(encoding='utf-8'))
    failures = fetch(manifest, args.fetch) if args.fetch else []
    if args.exe:
        if not args.fetch or failures:
            args.output.write_text(json.dumps(dict(downloadFailures=failures)), encoding='utf-8')
            raise SystemExit('Native evaluation blocked: complete checksum-pinned corpus required')
        if not args.predictions:
            parser.error('--exe requires --predictions')
        subprocess.run([str(args.exe.resolve()), '--luna-corpus-report', str(args.predictions.resolve()),
                        str(args.fetch.resolve())], check=True, timeout=600)
    if args.predictions:
        result = score(manifest, json.loads(args.predictions.read_text(encoding='utf-8-sig')))
        result['downloadFailures'] = failures
    else:
        result = dict(downloadFailures=failures, requested=len(manifest))
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    if failures:
        raise SystemExit('Some public fixtures unavailable; not a successful evaluation')


if __name__ == '__main__':
    main()
