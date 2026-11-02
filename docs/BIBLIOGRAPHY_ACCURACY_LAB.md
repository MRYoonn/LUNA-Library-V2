# LUNA ② — Bibliography Accuracy Lab (independent, offline)

This is an **evaluation harness**, not a new DOI parser, metadata fetcher, or PDF reader. It does not call Crossref/OpenAlex, open PDFs, modify QSettings, or claim that the native Windows LUNA application passed. The `*.synthetic.json` files contain **fabricated examples**, not real publisher measurements.

## Run the synthetic contract

```sh
python -B -m unittest discover -s tests -p 'test_bibliography_accuracy_lab.py' -v
python -B tools/bibliography_accuracy_lab.py \
  --goldset tests/fixtures/bibliography_accuracy_goldset.synthetic.json \
  --predictions tests/fixtures/bibliography_accuracy_predictions.synthetic.json \
  --report ../luna-accuracy-local-score.json
```

The `--require-real` flag rejects any synthetic goldset. The report marks non-synthetic results as `provided_real_goldset_not_independently_verified` until an authorized reviewer verifies evidence and the exact application build. Do **not** commit generated score reports, actual PDFs, PDF filenames, personal metadata, QSettings, or research notes.

## Goldset format and evidence policy

Create a **private, local** JSON file with `schema_version: 1`, `kind: "goldset"`, `dataset_id`, and a nonempty `cases` array. Each case has an opaque `id`, `publisher` (`ACS`, `RSC`, `Wiley`, `Nature`, `Elsevier`, or `Other`), `source.kind` (`public` or `permissioned`), a verified `source.sha256` for the PDF, `expected` field values, and `evidence` strings identifying where each nonempty expected value was independently checked (e.g. `page 1 author line`, `publisher landing page`, `SI cover`). Never infer the final author from PDF Info Author alone. Record the **last listed author**, not automatically the corresponding author; explicit senior-author overrides must be reviewed separately.

Fields: `doi`, `title`, `terminal_author`, `journal`, `journal_abbreviation`, `year`, `volume`, `issue`, `page`, `article_number`, `document_kind` (`main_article` or `supporting_information`). The goldset rejects nonempty `document_kind` labels other than `main_article` or `supporting_information`; a native prediction with an unrecognized nonempty kind is a scored `invalid_document_kind` FP/FN error, not a valid match. This prevents matching typos in both datasets from counting as true positives.

Use `null` to assert that a field **must be absent** (e.g. a reference-only DOI in an SI); **omit** a field when ground truth is not known. Do not label an inaccessible field `null`. For publication metadata, compare the official full journal name and abbreviation separately; no unreviewed abbreviation aliases. Pages normalize en/em dashes; titles use Unicode NFC rather than NFKC so chemical subscripts/superscripts (e.g. CO₂ vs CO2 and Fe³⁺ vs Fe3+) are not silently conflated. Canonically equivalent Unicode accents still match. Chemical formula, stereochemical descriptors and title punctuation remain significant. A wrong nonempty value produces **both** FP and FN; an abstention on known-positive ground truth produces FN; an unexpected value on known-negative ground truth produces FP. Metrics with no positive denominator are `null`, never invented 100%.

A separate private `kind: "predictions"` JSON must use the same `dataset_id` and case IDs, with `observed` fields representing **captured LUNA outputs from a specific build**. Export the actual card/record state from the application using a reviewed read-only adapter; this harness deliberately does not guess what the UI showed. Record the tested commit and build/run identity outside the synthetic fixture. Never claim synthetic snapshots came from the engine.

## Minimum real-paper acceptance matrix (not yet run)

At least 20 **permissioned/public** PDFs, with at least 3 main articles per ACS/RSC/Wiley/Nature/Elsevier (15), at least 3 SI PDFs (including multiple SI and SI with a reference DOI), and at least 2 no-DOI/ambiguous cases. Add explicit cross-provider DOI/title conflicts. Include both page ranges and article numbers, publisher-specific final-author layouts, year-only dates, journal abbreviations, missing metadata, and offline behavior. Every known field needs independent page/publisher evidence; list unknown labels as `unscored`.

Capture read-only before/after snapshots for: stale `QSettings` from PR #13 (both ambiguous legacy `manual` states), explicit user-edited title/author/journal/citation, concurrent edit and refresh, network offline, HTTP 429/404, invalid DOI, reference-only DOI, mismatched Crossref/OpenAlex DOI, restart, PDF rename/move, SI classification, and two simultaneously opened tabs. The optional `regressions` array compares `before` and `after` via `must_preserve`, `must_remain_empty`, and `expected_status`/`actual_status`. Each regression must contain at least one explicit assertion; assertion-free entries, duplicate/contradictory field checks, and an `actual_status` without `expected_status` are rejected rather than counted as PASS. A status-only regression is allowed when `expected_status` is explicit. It checks only **supplied snapshots**; a synthetic PASS does not prove these events occurred in LUNA.

## Follow-up integration boundary

1. Keep this branch stacked on Draft PR #16, not on `main`; do not modify PDF4QT, reader UI, metadata parsers or another developer's PR.
2. Add a **read-only Qt/native snapshot exporter** in a separate, reviewed step, or have the Windows owner export sanitized record snapshots manually. Map the final-author display explicitly and preserve per-field provenance.
3. Run the harness against the 20+ real authorized PDF goldset on Windows at the exact PR #16 commit; report field-wise precision/recall, per-publisher error cases, unknown labels, and reproducible regression failures. Use actual runtime evidence for 429/404 and persisted QSettings, not fabricated pass flags.
4. Only after those gates pass should the owner consider a merge. No auto-merge, Release, credential use, PDF upload, or automatic CI workflow dispatch.

## Follow-up: Qt record snapshot adapter (source-only, not Windows-verified)

The independent `tests/native_accuracy/snapshot_adapter.h` maps an **already loaded** `Bibliography::record(identity)` to a whitelisted `predictions` case with an opaque ID, a last-listed `terminal_author`, the original `documentKind`, and a small provenance audit. It never opens PDFs, QSettings, files or network connections, and never mutates a record. The caller must supply the exact 40-character source commit and a private case-ID mapping; the adapter does not read identities or user files. The returned `observed` values represent the **bibliography record**, not a rendered card screenshot; manual citation/senior-author overrides are only flagged, never misrepresented as terminal-author truth.

Native synthetic regression tests seed **temporary** QSettings and exercise Crossref-style structured last author, ambiguous legacy `manual=true`/Hao Fang suppression, explicit manual author vs stale structured names, SI without invented DOI, and invalid opaque ID/build identity. They do not touch production settings. This is an isolated Qt Core CMake project, **not** wired into the main Windows release workflow.

Run on a Windows/Qt6 machine:
```sh
cmake -S tests/native_accuracy -B build/native_accuracy -DCMAKE_BUILD_TYPE=Release
cmake --build build/native_accuracy --config Release
ctest --test-dir build/native_accuracy -C Release --output-on-failure
python -B -m unittest discover -s tests -p 'test_accuracy_snapshot_source_contract.py' -v
```

**Current evidence:** the three Python source-contract checks passed locally; Qt6 was not installed in the current execution environment, so **native compilation, CTest, full repository preflight, and real Windows GUI behavior remain unverified**. No private snapshots were generated. A future reviewed test driver may call `captureCase(opaqueCaseId, store.record(identity))` inside a locally isolated Windows process and save the returned JSON **outside the repository**, after explicit privacy review. The adapter alone is not a running exporter and cannot prove that a card visibly displayed the same text. Never upload the resulting predictions, PDF filenames, QSettings, author data, or private case-ID map.

Next: compile the native target on Windows; verify that exact SHA; build a reviewed, opt-in local driver that loads actual records without writing QSettings, compare against 20+ permissioned/public PDFs, then separately validate card display and restart/move/network behaviors.

## Follow-up: publisher-by-field accuracy matrix (source implemented; runtime pending)

The offline scorer now returns `by_publisher_fields` alongside the existing
`field_scores` and `by_publisher` keys. Each publisher has its **own**
`tp/fp/fn/tn/scored/unscored/precision/recall` for each of the 11 fields.
A wrong nonempty final author counts as FP+FN **only for that publisher**;
a field omitted from goldset stays `unscored` rather than becoming a false
negative. Undefined precision/recall remains JSON `null`.

The existing synthetic fixtures still cannot establish real extraction
accuracy. Two additional source-level Python test cases cover publisher
isolation, SI negative DOI, and unknown-label denominators. **They must be
executed in the target environment before claiming PASS**:

```sh
python -B -m unittest discover -s tests -p 'test_bibliography_accuracy_lab.py' -v
python -B tools/bibliography_accuracy_lab.py \
  --goldset tests/fixtures/bibliography_accuracy_goldset.synthetic.json \
  --predictions tests/fixtures/bibliography_accuracy_predictions.synthetic.json
```

Follow-up remains: Windows Qt6 compile/CTest of the existing
`tests/native_accuracy` adapter; privacy-reviewed, opt-in capture of real
LUNA record snapshots; 20+ authorized/public PDFs with SHA and independent
field evidence; actual restart/move/QSettings/HTTP 429/404 evidence.
No private PDF, settings, predictions or generated report may be committed.

## Follow-up: stricter real-run provenance gate (2026-11-01)

The `--require-real` mode now **blocks structurally unverified runs** instead of
merely rejecting synthetic `source.kind`. Every prediction document must
declare an exact nonzero 40-hex `engine_commit`, the read-only native adapter
`capture_kind: "qt_record_snapshot_read_only"`, and each case must carry
boolean `audit.manual_citation_active` and
`audit.senior_author_override_active` flags. Each real PDF must have a
nonzero SHA256, and **every labeled field, including explicit null (verified
absence)**, requires a nonblank evidence string. An omitted field remains
unknown/unscored and does not require evidence. This specifically catches SI
DOI-negative cases without a justification.

The default synthetic harness is unchanged. Added two **fabricated
contract-only** regression tests for unverified commit/capture/audit, blank or
missing evidence for a null DOI, and placeholder PDF hash. A 40-hex commit,
hash and text evidence are only **syntactic checks**; they do not authenticate
PDF bytes, prove an application was executed, or prove the evidence was
independently reviewed. A successful real-mode report still says
`provided_real_goldset_not_independently_verified`. Keep real data local.

Next run: execute all Python tests on the exact PR #18 HEAD, compile
`tests/native_accuracy` on Windows with Qt6 and run CTest, then capture
read-only real records at an exact PR #16 commit and independently review
>=20 authorized/public PDFs. The PR #16 branch has advanced; review its
changes before any integration. Do not silently rebase or merge.


## Follow-up: optional 20-PDF coverage gate (2026-11-01)

The existing scorer now supports `--coverage-gate` **together with**
`--require-real`. This is an optional stricter readiness gate; the old
synthetic and small real-pilot scoring behavior is unchanged. It requires:

- At least **20 distinct public/permissioned PDF cases**, counted by the
  supplied SHA256 metadata, with duplicate PDF digests explicitly blocked.
- At least **3 main articles each** from ACS, RSC, Wiley, Nature and Elsevier.
- At least **3 explicitly labeled SI PDFs**, plus **2 separately labeled
  main-article PDFs without a DOI**. An SI's missing DOI does **not** satisfy
  the no-DOI main-article requirement.
- Missing publisher/sample/duplicate coverage is returned as machine-readable
  `coverage_audit.gaps`; the CLI exits 1 and still writes the JSON report.
  Calling `--coverage-gate` without `--require-real` exits 2.

Example on a **local, permissioned, privacy-reviewed** dataset (never commit
the actual PDF, file paths, case-ID map, predictions or report):

```sh
python -B tools/bibliography_accuracy_lab.py --require-real --coverage-gate \
  --goldset LOCAL_GOLDSET.json --predictions LOCAL_PREDICTIONS.json \
  --report LOCAL_COVERAGE_REPORT.json
python -B -m unittest discover -s tests -p 'test_bibliography_accuracy_lab.py' -v
```

The coverage gate checks only the **provided metadata**, not the existence or
contents of any PDF, authenticity of the declared SHA256, or actual Windows
execution. Fabricated public-kind fixtures are used **only** to exercise the
gate and must never be described as real-publisher validation. A passing
`coverage_audit.ready` is a sample-design check, **not** evidence of
extraction accuracy. Native Qt6 CTest, live Crossref/OpenAlex, user-settings
preservation, real Windows restarts/moves, and independently reviewed PDF
evidence are still outstanding. Keep PR #18 Draft; do not merge or release.

## Follow-up: manual-provenance contamination guard (2026-11-01)

The native Qt snapshot adapter reports both field-level provenance and flags
for active pasted citations / senior-author overrides. In `--require-real`
mode the scorer now **blocks** such card overrides, and blocks any scored
field with `manual` provenance (including `authorsSource` for
`terminal_author`). This avoids counting user corrections as successful
**automatic extraction**. Do not treat legacy record-wide `manual=true` as
proof of a human edit: PR #13 explicitly documents machine-filled legacy
records. Use the separate before/after regression checks for manual-value
preservation and UI-card overrides. Synthetic default scoring is unchanged.

Two new synthetic contract tests cover both override flags, title/author
manual provenance, malformed field source maps, and clean read-only records.
They have not been executed on a Windows/Qt6 build. Real publisher accuracy,
actual QSettings persistence, file relocation and network error injection
remain unverified. Run the complete Python tests and native CTest before any
merge; do not upload private snapshots or user PDF files.

## Follow-up: exact engine-SHA gate and SI coverage correction (2026-11-01)

The coverage gate now requires **three** independently labeled SI PDFs,
consistent with the minimum real-paper matrix above. The earlier two-SI
threshold could report sample-design readiness even though the documented
acceptance matrix was incomplete. The new synthetic test holds the other
thresholds constant and proves that two SI are rejected while three are
accepted; synthetic hashes are **not** real PDF evidence.

For any real run, pin the **expected** PR #16 build commit explicitly:

```sh
python -B tools/bibliography_accuracy_lab.py --require-real --coverage-gate \
  --expected-engine-commit ACTUAL_40_HEX_WINDOWS_BUILD_SHA \
  --goldset LOCAL_GOLDSET.json --predictions LOCAL_PREDICTIONS.json \
  --report LOCAL_ACCURACY_REPORT.json
```

The command refuses an invalid expected SHA, a different
`predictions.engine_commit`, or using this option without `--require-real`.
Matching is case-insensitive; the real-mode report records the claimed
`engine_commit` for audit. This guards against scoring stale PR #16
snapshots under a newer build's label. It does **not** prove the SHA was
executed or that evidence/PDF digests are authentic. Independently compare
the SHA with the actual Windows build-info and Actions run, and keep all
captured records local.

New synthetic contract tests cover the three-SI threshold, missing/invalid/
stale engine identity, and the real-mode CLI match/mismatch paths. **No
Windows Qt6/native tests or real-publisher accuracy run has been performed
in this change.** Run the full Python suite and CTest on the exact commit
before interpreting any runtime PASS.

## Follow-up: malformed DOI gold-label guard (2026-11-01)

Goldset DOI values are now validated against the existing DOI syntax after
normalization. A malformed DOI cannot become a true positive merely because
the extraction snapshot repeats the same malformed string. Invalid **observed**
DOIs are still scored as false positives (and false negatives when a real DOI
was expected) and are labeled `invalid_doi` in per-case error reports.
Two synthetic regression tests cover malformed matching labels, legitimate
DOI URLs, invalid ACS extraction and a spurious invalid RSC SI DOI.
This is a scorer-contract safeguard only: it does not prove a DOI resolves,
that a publisher PDF was read, or that Windows Qt6/native tests passed.


## Follow-up: fail-closed field provenance (2026-11-02)

In `--require-real` mode, a scored **nonempty observed value** now needs
an explicitly recognized automatic source: Crossref, OpenAlex, combined
Crossref/OpenAlex, verified first-page text, PDF metadata, or PDF front matter.
The evaluator checks both `audit.field_sources` and legacy `*Source`
fields. Any explicit manual source wins even if another source says Crossref;
whitespace/case variations of `manual` are rejected. Missing, unknown, or
`legacy_preserved` provenance is blocked instead of being counted as an
automatic extraction true positive. An abstention needs no provenance.
`document_kind` is exempt from this source requirement because the native
record exposes classification without a `*Source` field; it still requires
independent goldset evidence.

This is a stricter **input evidence gate**, not a new parser or proof of
publisher accuracy. Old real-run JSON snapshots without per-field provenance
must be recaptured from a reviewed native adapter or separately marked
regression-only. The new tests use fabricated Crossref labels solely to
exercise the contract. No PDF, personal QSettings or private paths were added.
Run the isolated Python CI and Windows QtCore CTest at the exact commit before
claiming validation. Actual 20+ authorized PDF measurements and live network
error/restart/move regressions remain outstanding.

## Follow-up: contradictory provider provenance gate (2026-11-02)

The real-run scorer now requires **every supplied native provenance channel**
for a populated, labeled field to be an allowed automatic source. A valid
`field_sources.title=Crossref` cannot mask a stale
`titleSource=legacy_preserved`. Distinct single-provider claims such as
`Crossref` versus `OpenAlex` are blocked as ambiguous; an explicit
`Crossref + OpenAlex` composite is accepted with either component. These
are conservative **evaluation gates**, not a change to PR #16's provider
selection or merge behavior. A blocked case needs human provenance review,
not an automatic source rewrite.

Two synthetic contract tests were added. They do not establish that live
Crossref/OpenAlex responses agree, nor do they prove Windows GUI behavior.
Next: inspect the branch-specific CI result, run the full Python suite and
capture authorized real records with an exact build SHA. Actual PDF goldset,
offline/429/404 live responses, legacy QSettings and restart/move acceptance
remain unverified. Keep the PR Draft and do not merge or release.


## Follow-up: field-label denominator readiness (2026-11-02)

A 20-PDF matrix can satisfy publisher/SI quotas but still have **no independently
labeled journal abbreviation, issue, page, or article number**. A scorer can
correctly emit `null` precision/recall for those fields, but such a dataset
is **not ready** for a claim of comprehensive field-wise evaluation.

The new opt-in `--field-coverage-gate` requires `--coverage-gate
--require-real`. It adds `field_label_audit` to the local JSON report with
positive, verified-negative, and unknown-label counts for all 11 fields,
plus positive counts for DOI/title/terminal author/journal/year/document kind
for each of ACS, RSC, Wiley, Nature, Elsevier. It fails if any field has zero
known-positive labels globally or any publisher has zero known-positive
core labels. It does **not** treat a missing label as a negative or infer
accuracy from a PDF filename. It does not change the older `--coverage-gate`
contract, so callers can compare the two readiness levels explicitly.

Example (keep all actual inputs/reports outside this repository):

```sh
python -B tools/bibliography_accuracy_lab.py \
  --goldset ../private/goldset.json \
  --predictions ../private/native-snapshots.json \
  --require-real --expected-engine-commit <EXACT_40_HEX_SHA> \
  --coverage-gate --field-coverage-gate \
  --report ../private/field-accuracy-report.json
```

Synthetic regression tests exercise the coverage inventory only; **no real
publisher PDFs or native captures were available in this run**. The optional
gate is a denominator-completeness check, not independent verification of
goldset truth, PDF SHA, Windows runtime, network errors, or manual edits.

## Follow-up: goldset collection audit without engine predictions (2026-11-02)

Before a Windows Qt snapshot is available, run the independent **read-only**
`tools/bibliography_goldset_audit.py` on a local, authorized goldset:

```sh
python -B tools/bibliography_goldset_audit.py --goldset LOCAL_GOLDSET.json --report LOCAL_MISSING_LABELS.json
python -B -m unittest discover -s tests -p 'test_bibliography_goldset_audit.py' -v
```

This command does **not** need prediction snapshots, PDFs, QSettings or network.
It reports the existing 20-PDF publisher/SI/no-DOI sample quotas, the 11-field
label-coverage matrix, and an actionable `unknown_cases` list with opaque
case IDs, publisher and **omitted** labels. Explicit `null` is a verified
negative label and is never listed as unknown. `synthetic_case_ids_excluded`
prevents fabricated fixtures from being mistaken for real publisher coverage.
Exit status: `0` for structurally ready, `1` for incomplete quotas/labels,
`2` for malformed input or missing evidence. Even status 0 is **not** evidence
that any PDF hash, publisher evidence or extraction was independently verified.
The generated report can contain private case IDs; keep it outside GitHub.

The source and synthetic tests were added independently of the existing DOI
engine, PDF renderer, and application settings. The exact branch's Python and
Windows CI must be checked before claiming this follow-up passes. Real ACS,
RSC, Wiley, Nature and Elsevier accuracy remains unmeasured.

## Follow-up: collection evidence readiness gate (2026-11-02)

The read-only `tools/bibliography_goldset_audit.py` inventory now includes
`evidence_audit.ready` and case/field-specific `evidence_audit.gaps`.
It refuses to declare collection readiness when a purported public/permissioned
PDF uses an all-zero SHA256, when a **positive** field has whitespace-only
support, or when an explicit `null` (verified absence, notably SI DOI) has
no nonblank independent evidence. Omitted/unknown labels do not need evidence.
The existing breadth and per-publisher field gates remain unchanged.

Three synthetic-only subprocess tests cover these failure modes. A structurally
passing evidence audit does **not** authenticate hashes, PDFs, source URLs,
Windows execution, or reviewer judgments. The real publisher goldset and
actual Qt/Windows/QSettings/429/404/move/restart validations remain outstanding.
Run `python -B -m unittest discover -s tests -p 'test_bibliography_goldset_audit.py' -v`
and check the exact branch commit's independent CI before reporting PASS.

## Follow-up: reject placeholder evidence and protect local goldset (2026-11-02)

The prediction-free collection audit now flags case/field evidence that is only
a placeholder (e.g. `TBD`, `N/A`, `Unverified`) as
`placeholder_field_evidence`; it cannot count as an independent review of a
positive value or an explicit null. The CLI also refuses `--report` when the
resolved output path is the input goldset, preventing accidental destruction of
private source labels. Two synthetic-only regression tests cover these guards.
Neither check authenticates publisher PDFs or measures LUNA extraction quality.

## Follow-up — native persistence and failure regression (2026-11-02)

The isolated `tests/native_accuracy/main.cpp` now exercises **real QtCore
`Bibliography` storage operations** against a disposable `QTemporaryDir`
QSettings root. It does not read personal settings, PDF files, or network data.

Synthetic native assertions cover:
- An explicit `saveEdits` title correction retains `titleSource=manual` when a
  DOI-matched Crossref-shaped response has a conflicting title; the record is
  marked `needs_review` instead of silently overwriting the human value.
- `recordLookupFailure` for `offline`, `rate_limited` (HTTP 429 equivalent)
  and `not_found` (HTTP 404 equivalent) keeps original bibliography fields
  and records non-success status. These are **injected status codes**, not live
  HTTP transport tests.
- `relocate` preserves manually edited title, provenance and review status
  across a **new Bibliography instance** (restart simulation); the old identity
  no longer resolves in the new instance. A pre-existing destination record
  blocks relocation without overwriting either record.

These are **bibliography record identity** tests, not physical PDF/sidecar
file moves, rollback, Windows GUI card checks or a real process restart.
The isolated Windows QtCore workflow is the required compile/CTest gate at
the exact commit. Until it finishes successfully, the added assertions are
**source-only and unverified**. Real 20+ publisher goldset, live HTTP
responses, legacy personal QSettings, and the production PDF move integration
remain outstanding. Keep Draft and do not merge/release.

## Isolated stale-editor and invalid-DOI regression (2026-11-02)

`tests/native_accuracy/main.cpp` now exercises the existing `Bibliography::saveEdits` implementation against disposable QtCore `QSettings`. It opens a synthetic edit snapshot, applies a later background journal/volume update, saves only the human-edited title, and checks that provider fields/provenance survive. It also submits a malformed manual DOI together with a title edit and verifies that **neither** field nor the status is partially persisted; a new `Bibliography` object reads the durable result. These are isolated persistence-contract tests, **not** an actual concurrent Windows GUI, provider HTTP request, or real PDF run. Keep any user settings, PDFs and raw snapshots outside GitHub.


## Follow-up: opt-in local PDF integrity gate (2026-11-02)

The existing collection audit checks **declared** SHA256/evidence only. It can
now optionally compare each non-synthetic case against a locally staged file,
without reading PDF text, uploading files, or revealing original filenames.

Stage **copies** of authorized/public PDFs outside the repository in a private
directory. Name each copy `<source.sha256>.pdf` (64 hex characters from its
goldset record). Never commit that directory, the source PDFs, or reports.
Run:

```sh
python -B tools/bibliography_goldset_audit.py \
  --goldset /private/local/goldset.json \
  --pdf-dir /private/local/pdf-copies \
  --report /private/local/collection-audit.json
```

The additive `pdf_integrity_audit` reports only opaque case IDs and reasons:
`missing`, `unsafe_file`, `unreadable`, `invalid_pdf_header`,
`sha256_mismatch`, or a count of `hash_match_pdf_header`. Symlinks are
rejected; files are hashed in chunks. A provided PDF directory makes overall
collection `ready` require every declared real PDF to match its hash and have
a PDF header. Without `--pdf-dir`, integrity is `not_checked`, and the old
`ready` remains **metadata collection readiness only**, never a verified
publisher/accuracy claim.

Hash/header checks **cannot** authenticate a publisher, establish permission
to use a PDF, prove that a PDF parses correctly, or verify its field labels.
The tests use fabricated byte streams with a PDF header, not real publisher
files. Actual 20+ PDF goldset collection, independent page/publisher evidence,
native prediction capture, and real Windows GUI/network/move/restart acceptance
remain outstanding. Do not run this tool against the user's original library
files automatically or upload any local artifacts.
