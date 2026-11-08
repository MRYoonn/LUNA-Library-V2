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
from urllib.parse import quote, urlsplit
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
        # Only actual bibliographic evidence can imply a successfully read
        # article. Diagnostic-only keys (e.g. _titleAudit) are not extracted
        # metadata; counting them incorrectly turns abstained image-only SI
        # covers into false-positive 'article' classifications.
        has_bibliography = any(
            actual.get(key) for key in (*FIELDS, 'article_number')
            if key != 'documentKind'
        )
        if present and has_bibliography and not actual.get('evaluationError'):
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


def download_public_fixture(request, *, attempts=3, timeout=120):
    # The same pinned fixture must succeed; a transient service failure is
    # neither an extraction result nor permission to skip a goldset member.
    for attempt in range(attempts):
        delay = 2 ** attempt
        try:
            with urlopen(request, timeout=timeout) as response:
                return response.read(100_000_001)
        except HTTPError as error:
            if error.code not in (408, 429, 500, 502, 503, 504) or attempt == attempts-1:
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
            if attempt == attempts-1:
                raise
        time.sleep(delay)



def public_sources(item):
    """Explicit public archival mirrors. Each candidate must match manifest SHA256."""
    primary = item['pdf_url']
    sources = [('manifest', primary, 'zip' if 'archive_member' in item else 'pdf')]
    identifier = item['id'].split('-SI', 1)[0]
    # Do not redirect arbitrary manifest entries to untrusted locations.
    if not re.fullmatch(r'PMC[1-9]\d*', identifier):
        return sources
    if 'archive_member' in item:
        member = item['archive_member']
        if member != Path(member).name or not re.fullmatch(r'[A-Za-z0-9_.-]+\.pdf', member):
            raise ValueError('Unsafe pinned supplementary filename')
        sources.extend([
            ('ncbi-supplement', f'https://pmc.ncbi.nlm.nih.gov/articles/{identifier}/bin/{quote(member)}', 'pdf'),
            ('europepmc-supplement', f'https://europepmc.org/articles/{identifier}/bin/{quote(member)}', 'pdf'),
        ])
    elif primary.startswith('https://europepmc.org/api/getPdf?pmcid='):
        numeric = int(identifier[3:])
        batch = (numeric + 9999) // 10000
        sources.extend([
            ('ebi-oa-archive', f'https://ftp.ebi.ac.uk/pub/databases/pmc/pdf/OA/PMCxxxx{batch:03d}/{identifier}.zip', 'zip'),
            ('ncbi-pdf', f'https://pmc.ncbi.nlm.nih.gov/articles/{identifier}/pdf/', 'pdf'),
            ('europepmc-render', f'https://europepmc.org/articles/{identifier}?pdf=render', 'pdf'),
        ])
    return sources


def verified_pdf(data, item, kind):
    expected = item['sha256']
    if len(data) > 100_000_000:
        raise ValueError('Public fixture exceeds download bound')
    if kind == 'zip':
        with zipfile.ZipFile(io.BytesIO(data)) as archive:
            if 'archive_member' in item:
                members = [item['archive_member']]
            else:
                members = [name for name in archive.namelist()
                           if name.lower().endswith('.pdf') and name == Path(name).name]
            for name in members:
                if archive.namelist().count(name) != 1:
                    continue
                info = archive.getinfo(name)
                if info.file_size > 30_000_000 or info.file_size < 32:
                    continue
                candidate = archive.read(info)
                if candidate.startswith(b'%PDF-') and hashlib.sha256(candidate).hexdigest() == expected:
                    return candidate
    elif kind == 'pdf' and data.startswith(b'%PDF-') and hashlib.sha256(data).hexdigest() == expected:
        return data
    raise ValueError('Pinned PDF SHA256 mismatch or invalid archive')



def aws_openaccess_pdf(item):
    """Read publisher-authorized PMC S3 article metadata, then exact SHA256 media.

    August 2026 moved PMC dataset distribution from legacy FTP packages to
    versioned S3 article objects. Never trust arbitrary URLs in S3 metadata.
    """
    identifier = item['id'].split('-SI',1)[0]
    for version in (1,2,3):
        folder = f'{identifier}.{version}/'
        meta_url = f'https://pmc-oa-opendata.s3.amazonaws.com/metadata/{identifier}.{version}.json'
        try:
            with urlopen(Request(meta_url,headers={'User-Agent':'LUNA-public-fixture-validation'}),timeout=25) as reply:
                info = json.loads(reply.read(100_000))
            if info.get('pmcid') != identifier or info.get('version') != version:
                continue
            if not info.get('is_pmc_openaccess'):
                continue
            media = list(info.get('media_urls') or []) if 'archive_member' in item else [info.get('pdf_url')]
            for uri in media:
                if not isinstance(uri,str):
                    continue
                parsed = urlsplit(uri)
                if parsed.scheme != 's3' or parsed.netloc != 'pmc-oa-opendata':
                    continue
                if not parsed.path.lstrip('/').startswith(folder):
                    continue
                if 'archive_member' in item and parsed.path.rsplit('/',1)[-1] != item['archive_member']:
                    continue
                if 'archive_member' not in item and not parsed.path.endswith('.pdf'):
                    continue
                public_url = f'https://pmc-oa-opendata.s3.amazonaws.com{parsed.path}'
                try:
                    raw = download_public_fixture(Request(public_url,
                        headers={'User-Agent':'LUNA-public-fixture-validation'}), attempts=1, timeout=35)
                    return verified_pdf(raw,item,'pdf')
                except (HTTPError,URLError,TimeoutError,ValueError):
                    continue
        except (HTTPError,URLError,TimeoutError,ValueError,json.JSONDecodeError):
            continue
    raise ValueError('No exact SHA256-matching open-access PMC cloud object available')


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
        attempt_errors = []
        resolved = False
        for label, url, kind in public_sources(item):
            try:
                request = Request(url, headers={'User-Agent': 'LUNA-public-fixture-validation'})
                data = download_public_fixture(request, attempts=3 if label=='manifest' else 1,
                                               timeout=120 if label=='manifest' else 30)
                verified = verified_pdf(data, item, kind)
                path.write_bytes(verified)
                resolved = True
                break
            except Exception as error:
                fault = dict(source=label, error=type(error).__name__)
                if isinstance(error, HTTPError):
                    fault['httpStatus'] = error.code
                    retry_after = (error.headers or {}).get('Retry-After', '')
                    if retry_after:
                        fault['retryAfter'] = str(retry_after)[:80]
                attempt_errors.append(fault)
                # A rate limit must be respected; do not switch routes to
                # circumvent a provider's explicit Retry-After instruction.
                if label == 'manifest' and isinstance(error, HTTPError) and error.code == 429:
                    break
                # Try the official August-2026 PMC AWS dataset first, avoiding
                # obsolete FTP packages and browser-facing PMC pages when possible.
                if label == 'manifest' and item['pdf_url'].startswith(
                        ('https://europepmc.org/', 'https://www.ebi.ac.uk/')):
                    try:
                        path.write_bytes(aws_openaccess_pdf(item))
                        resolved=True
                        break
                    except ValueError as aws_error:
                        attempt_errors.append(dict(source='official-pmc-aws',
                                                   error=type(aws_error).__name__))
        if not resolved:
            fault = dict(id=identifier, **{k: v for k, v in attempt_errors[0].items() if k != 'source'})
            if len(attempt_errors) > 1:
                fault['attempts'] = attempt_errors
            failures.append(fault)
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
