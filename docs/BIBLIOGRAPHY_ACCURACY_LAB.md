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

Capture read-only before/after snapshots for: stale `QSettings` from PR #13 (both ambiguous legacy `manual` states), explicit user-edited title/author/journal/citation, concurrent edit and refresh, network offline, HTTP 429/404, invalid DOI, reference-only DOI, mismatched Crossref/OpenAlex DOI, restart, PDF rename/move, SI classification, and two simultaneously opened tabs. The optional `regressions` array compares `before` and `after` via `must_preserve`, `must_remain_empty`, and `expected_status`/`actual_status`. Each regression must contain at least one explicit assertion; assertion-free entries, duplicate/contradictory field checks, and an `actual_status` without `expected_status` are rejected rather than counted as PASS. A status-only regression is allowed when `expected_status` is explicit. For `must_remain_empty`, the `after` snapshot must **explicitly contain** each asserted key with `null`, `""`, or `[]`; a missing key fails as `missing_asserted_field`, not a silent PASS. This closes a false-positive gap in the invalid-DOI and SI-blank-field gates. It checks only **supplied snapshots**; a synthetic PASS does not prove these events occurred in LUNA.

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

## Follow-up: selected senior-author index contamination guard (2026-11-02)

PR #16's `Metadata::labCitation()` can choose an author through
`senior_author_index` even when `senior_author_override` is empty. The
read-only native snapshot adapter previously flagged only the textual
override, so a manually selected author could be mistaken for automatic
terminal-author card accuracy. The adapter now sets its existing
`senior_author_override_active` audit flag when a selected index is within
the structured-author list, without exporting the index, user identities, or
paths. The real-mode scorer already rejects this flag; no production engine
or UI changes are needed. A missing/out-of-range index does not alter the
card and is not marked active. Synthetic QtCore and source-contract tests
cover ordinary, selected, and invalid-index cases.

**Not yet acceptance evidence:** these tests do not exercise the actual
Windows card, live providers, user QSettings, or 20+ publisher PDF goldset.
Check the exact new-head CI before marking PASS. Keep PR #18 Draft.

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

## Follow-up 2026-11-03 — duplicate PDF sample protection

The read-only goldset inventory now marks a repeated non-synthetic
`source.sha256` as `duplicate_pdf_sha256`, naming only the two opaque case
IDs. The real-run accuracy scorer also rejects the repeated digest before
calculating precision/recall. Without this guard, one physical PDF could be
counted multiple times as independent publisher evidence and inflate both
coverage and accuracy denominators. Synthetic fixtures remain excluded from
the real-PDF uniqueness check. The report never prints source digests or
local PDF paths for this error.

Two new synthetic-only regressions cover the inventory and real-run rejection.
No actual ACS/RSC/Wiley/Nature/Elsevier PDF, native Windows capture or personal
QSettings was used. Verify exact-HEAD Python and Windows QtCore CI; a green
synthetic test is **not** a real publisher precision/recall measurement.
Next collect >=20 **distinct** authorized/public PDFs, independently verify
field evidence, and score read-only PR #16 snapshots at the exact build SHA.

## Follow-up 2026-11-02 — protected-value snapshot completeness

The offline regression scorer now requires a `must_preserve` field to exist
in **both** `before` and `after` snapshots. Previously, an explicit
`before: {"doi": null}` compared equal to a missing `after.doi` through
`after.get("doi")`, falsely passing a manual-value preservation test.
Missing snapshots now emit `missing_asserted_field`; present but changed
values still emit `changed_protected_value`. Synthetic tests cover explicit
null, blank string, empty list, missing before/after keys, and a changed
protected value, including the top-level `pass` result.

This is a **contract correction only**. No user QSettings, real PDFs, network
429/404, Windows GUI, or physical file relocation was exercised by this
change. Verify exact-HEAD isolated Python and Windows QtCore CI before
claiming PASS. Then collect permissioned/public publisher goldset PDFs and
read-only native snapshots; keep all private artifacts local.

## Follow-up — negative-label capture completeness (2026-11-02)

The previous scorer treated an omitted native `observed` key as equivalent to
an explicitly captured `null` when the gold label was negative. That
incorrectly incremented true negatives (e.g. SI DOI absent, ACS article number
absent) and could produce a false PASS without proof the native field had
been read.

The **independent** read-only Qt snapshot adapter now emits an explicit JSON
`null` for each absent whitelisted field, including `terminal_author` and
`document_kind`. Nonempty unknown document classifications are preserved for
the existing `invalid_document_kind` scorer rather than silently dropped.
The scorer reports `missing_snapshot_field` and increments the separate
`missing_snapshot` count when a gold-negative field is omitted; it does not
credit a true negative or invent an FP/FN. An omitted known-positive value
continues to count as FN. The synthetic fixture and Qt/Python regressions
exercise the new contract; these are **not** real extraction measurements.

Validation commands (synthetic, isolated only):

```sh
python -B -m unittest discover -s tests -p 'test_bibliography_accuracy_lab.py' -v
python -B -m unittest discover -s tests -p 'test_accuracy_snapshot_source_contract.py' -v
cmake -S tests/native_accuracy -B build/native_accuracy
cmake --build build/native_accuracy --config Release
ctest --test-dir build/native_accuracy -C Release --output-on-failure
```

No Windows Qt build, full application GUI, publisher PDF accuracy, live HTTP
429/404, actual file relocation, or user QSettings can be inferred from
source edits or synthetic test fixtures. Inspect exact-HEAD isolated CI before
marking PASS. Then collect 20+ authorized ACS/RSC/Wiley/Nature/Elsevier PDF
gold labels with independent evidence and read-only snapshots from an exact
PR #16 build; keep private data outside GitHub. Keep PR Draft.

## Follow-up 2026-11-02 — physical same-volume move regression (CI pending)

The isolated QtCore native test now invokes the **existing production**
`src/lunafilemove.h::movePdfWithThumbnails()` against a temporary, fabricated
`.pdf`, `.luna-thumb.png`, and `.luna-meta.json` under `QTemporaryDir`.
It first places an unrelated destination metadata sidecar and asserts that
the collision is rejected **without modifying any source bytes or the
occupied destination**. After clearing the fixture, it checks all three
files moved with exact byte equality, that source files are absent, then
calls the existing `Bibliography::relocate()`. A newly constructed Qt
bibliography store must retain the human-edited title, `manual` provenance,
and `needs_review` status at the new file identity. This extends the
previous in-memory relocation-only synthetic checks without modifying the
production mover, PDF4QT, reader, or other PRs.

The fake PDF contains only a header and fabricated bytes: this tests the
**file-management transaction**, not PDF parsing, annotations, the rendered
card, Windows GUI interactions, full-process restart, cross-volume moves,
or actual user files. No original PDF, real QSettings, network request or
private sidecar is read or uploaded. The native test is compiled only in the
independent Qt6 Windows job; source contracts are also checked by Python.

Validation commands:
```sh
python -B -m unittest discover -s tests -p 'test_accuracy_snapshot_source_contract.py' -v
cmake -S tests/native_accuracy -B build/native_accuracy
cmake --build build/native_accuracy --config Release
ctest --test-dir build/native_accuracy -C Release --output-on-failure
```

**Exact new HEAD CI not yet verified.** Next: inspect the branch's
`python-contract` and `windows-qtcore` results and correct any failures;
then gather >=20 permissioned/public publisher PDFs with independently
reviewed labels and opt-in local-only read-only native snapshots. Real
HTTP 429/404, Windows GUI and actual process restart remain separate gates.
Keep PR #18 Draft; do not merge or release.

## Follow-up 2026-11-02 — strict typed preservation assertions (CI pending)

The independent offline regression scorer now rejects a preserved-field
snapshot when the **type** changes even if Python considers the values equal.
In particular, `False == 0` and `True == 1` previously let a manual
QSettings boolean become an integer while `must_preserve` still passed.
A changed type now reports `changed_protected_value`. The new synthetic
unit test checks both directions for true/false and 0/1, and unchanged
values of all four types.

This only validates supplied snapshots; it does **not** prove that production
QSettings migration, manual edits, or a Windows process restart behaved
correctly. Exact-HEAD Python and Windows QtCore CI have not been observed
passing for this change. Run the isolated workflow and then real Windows
regressions with disposable settings. Authorized multi-publisher PDF goldset
and field-wise extraction accuracy are still outstanding. Keep Draft #18;
do not merge, release, or modify the reader/metadata engine.

## Follow-up 2026-11-02 — fix Windows native test failure; expose sidecar gap

The previous exact-HEAD isolated run
[Actions #41425360560](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41425360560)
had **Python PASS but Windows QtCore FAIL** at
`physical move rejects occupied metadata destination`. Inspection of
`src/lunafilemove.h::movePdfWithThumbnails` shows that the production helper
handles the PDF and `.luna-thumb.png/.jpg/.jpeg` only; it does **not**
move or check `.luna-meta.json`. The prior synthetic test incorrectly
asserted a feature not present in that helper. This is not a compiler failure.

The corrected independent test now injects a **thumbnail** destination
collision (supported by the helper), verifies that no source bytes are
changed, then verifies successful PDF+thumbnail move and bibliography
`relocate()` persistence. It explicitly checks that the fabricated
`.luna-meta.json` **remains at the source** and is absent at the target.
This is a **known sidecar integration gap**, not an accepted metadata-sidecar
move. The earlier physical-move section's assertion that all three files
move is superseded by this finding.

Do **not** change production `lunafilemove.h` or another developer's PR
from this independent branch. The owner of Issue #9 must decide whether
`.luna-meta.json` is a supported persisted artifact and, if so, implement
an atomic sidecar-aware move in the main file-management workstream.
Meanwhile the independent regression must report the limitation accurately.
New branch CI must pass Python and Windows QtCore before this fix is
called verified. Real user QSettings, Windows GUI, process restart and
publisher PDF accuracy remain unverified.

## Follow-up: private audit report output safety (synthetic verification)

The opt-in `--report` output now refuses a symlink destination and writes through
an access-restricted temporary file in the destination directory, followed by
an atomic replacement. This prevents accidentally following a report symlink
into unrelated local research files and avoids a truncated report after a
failed write. Synthetic CLI regressions cover the symlink refusal, repeated
report generation and temporary-file cleanup. This is **not** a PDF goldset
accuracy measurement and does not validate actual publisher evidence.


## Follow-up: actual process-boundary restart regression (2026-11-02)

The isolated QtCore test now starts **a second OS process** of its own test
executable, pointing QSettings IniFormat UserScope at the same disposable
`QTemporaryDir`. The child loads the *physically moved* synthetic PDF's
bibliography by its destination identity, checks manual title provenance and
`needs_review`, checks the moved thumbnail exists, and verifies that a
stale-dialog human title and background Crossref journal both survive a
process restart. It also reloads the synthetic `rate_limited` diagnostic
and confirms it remains `partial`, not `complete`.

Previously the test only created another `Bibliography` object in the same
process; that could not distinguish disk persistence from in-process Qt
settings caching. The child uses no user QSettings, private PDF or network.
The test does **not** establish real Windows GUI restart behavior, HTTP 429
responses, or that `.luna-meta.json` moves with the PDF (it currently does
not). It only verifies already-saved synthetic records across an actual
process boundary.

Run on the exact branch commit:

```sh
python -B -m unittest discover -s tests -p 'test_accuracy_snapshot_source_contract.py' -v
cmake -S tests/native_accuracy -B build/native_accuracy -DCMAKE_BUILD_TYPE=Release
cmake --build build/native_accuracy --config Release
ctest --test-dir build/native_accuracy -C Release --output-on-failure
```

**Acceptance pending:** verify exact-head Python/Windows QtCore CI; authorized
20+ publisher PDF goldset and field-level precision/recall; opt-in local
legacy-user-settings regression; real Windows GUI process restart and physical
PDF move; live Crossref/OpenAlex offline/429/404 tests. Keep PR #18 Draft.


## Follow-up — protect scorer inputs during local report generation (2026-11-02)

The offline **accuracy scorer** `tools/bibliography_accuracy_lab.py` previously
used `Path.write_text()` for `--report`. A mistyped report path equal to
`--goldset` or `--predictions` could overwrite the original local
permissioned labels or read-only native snapshots. The separate collection
audit already had an analogous guard; this follow-up applies it to the
**scorer**, without changing scoring, extraction, native Qt, or PDF handling.

The scorer now rejects either input-path alias (including relative/resolved
aliases), rejects report symlinks, and writes a private same-directory
temporary file before atomic replacement. Failed writes clean up the temp
file and return exit code 2. Three synthetic subprocess regression tests
exercise input byte preservation, unrelated symlink target protection, and
repeatable report replacement with no temp-file leaks.

Run on the exact independent branch:

```sh
python -B -m unittest discover -s tests -p 'test_bibliography_accuracy_lab.py' -v
python -B tools/bibliography_accuracy_lab.py \
  --goldset tests/fixtures/bibliography_accuracy_goldset.synthetic.json \
  --predictions tests/fixtures/bibliography_accuracy_predictions.synthetic.json
```

**Validation boundary:** source updates and synthetic tests do not measure
real publisher precision/recall. Check the exact-head isolated GitHub Actions
Python and Windows QtCore results before claiming PASS. Still missing:
20+ authorized/public ACS/RSC/Wiley/Nature/Elsevier PDF labels with independent
evidence, privacy-reviewed native captures from an exact PR #16 build, real
Windows GUI/legacy QSettings, and live offline/429/404/restart/move acceptance.
Never upload local PDF copies, private goldsets, snapshots, reports or settings.
Keep Draft PR #18; no merge, release, or changes to other PRs.

## Legacy author migration matrix — 2026-11-03

This isolated native test extends the previously passing fresh-process QSettings
regression, without changing PR #13/#16 production behavior. Four disposable
legacy records cover both record-wide `manual=false/true` values and the
ambiguous old `source=Manual` marker, with/without independently verified
`authorEvidence=first-page-terminal-and`. Only the `manual=false` record
with verified terminal-author evidence may expose the author; the other
three remain unverified, retaining `legacyAuthors` solely for recovery.
A separate synthetic test preserves a deliberately blank manually owned author
against first-page enrichment and retains a one-word human author. The child
process repeats these checks after loading the INI settings from disk.

Modified: `tests/native_accuracy/main.cpp`,
`tests/test_accuracy_snapshot_source_contract.py`, this handoff document.
The source contract is NOT a substitute for a compiled Windows QtCore CTest.
Run `python -B -m unittest discover -s tests -p
'test_accuracy_snapshot_source_contract.py' -v` and the existing isolated
Windows QtCore CI on this exact commit; inspect the results before claiming
PASS. No private QSettings, real publisher PDF, network response, GUI state,
or actual metadata-engine precision/recall is involved. Real user-PC legacy
settings and the >=20 authorized PDF goldset remain unverified.


## Follow-up 2026-11-03 — repair duplicate-hash fixture regression

Exact-HEAD Actions [#41477618847](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41477618847)
reported **Windows QtCore PASS, Python FAIL (2 tests)**. Both failing
`test_require_real_*` tests reused the same fabricated `a * 64` SHA256
for all seven supposedly independent PDF cases. The newly added production
scorer duplicate-PDF guard correctly rejected them before reaching their
intended snapshot-identity and negative-label assertions. This was a **test
fixture defect**, not a reason to weaken the duplicate protection.

The two tests now generate distinct, deterministic synthetic SHA256 strings
for each fabricated public-kind case, and restore the exact original
case-specific digest after the deliberate placeholder-hash rejection.
No real PDFs, personal QSettings or network data are used. Only the
independent test and handoff documentation changed; the scorer and
publisher extraction engine are untouched.

**Exact-HEAD CI must be checked** for Python and Windows QtCore before
calling this fix verified. Actual publisher precision/recall, Windows GUI,
live 429/404 and private user settings remain unverified.

## Follow-up 2026-11-03 — fresh-process offline/429/404 persistence

The prior fixture-only repair at `195510d5477a8566e84c3745bb59978f750c2c9e`
passed both isolated jobs in Actions run
[#41487332871](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41487332871).
That PASS belongs to the **previous** commit, not this change.

The QtCore synthetic regression already exercised `recordLookupFailure` for
`offline`, `rate_limited` (HTTP 429), and `not_found` (HTTP 404)
in the original process, but the child-process persistence assertion covered
only 429. The fresh OS process now reloads **all three** cases from the
isolated on-disk QSettings INI and checks the exact diagnostic, non-complete
status (`offline` or `partial`), and unchanged original title. The Python
source contract checks that this native regression is present. No production
metadata engine, PDF reader, or user QSettings was modified.

Reproduce with the existing
`python -B -m unittest discover -s tests -p 'test_accuracy_snapshot_source_contract.py' -v`
and the isolated Windows QtCore CTest job in
`.github/workflows/bibliography-accuracy-lab.yml`.
**The new exact-HEAD CI result must be checked before declaring PASS.**
This tests synthetic persisted status, **not** real HTTP 429/404 responses,
Windows GUI, authorized multi-publisher goldset precision/recall, or actual
user settings. Those acceptance gates remain open.

## Follow-up 2026-11-03 — composite-provider provenance mismatch

**Cause:** the real-run scorer accepted a populated field when one audit
channel said `Crossref + OpenAlex` and the other said `pdf_metadata` or
`first_page_verified`. The mere presence of the composite provider label
suppressed the existing conflict guard, despite incompatible source claims.
This could count a disputed title as an automatic-extraction true positive.

**Reproduction (synthetic only):** set
`audit.field_sources.title="Crossref + OpenAlex"` and
`audit.titleSource="pdf_metadata"` on a fabricated real-mode prediction.
The previous scorer accepted this combination. The isolated scorer now rejects
it as `conflicting automatic provenance for title`, while accepting a
composite provider paired with `Crossref`, `OpenAlex`, or the same composite
label. The added regression tests cover all six combinations.

Run `python -B -m unittest discover -s tests -p 'test_bibliography_accuracy_lab.py' -v`
and inspect the **exact new-head** Python/Windows CI. No actual PDF, network
response, user QSettings or application UI was exercised by this change.
Next: collect >=20 distinct authorized/public publisher PDFs and independently
review field labels; capture actual PR #16 read-only snapshots at the verified
Windows build SHA; test live 429/404, legacy settings and GUI/move/restart
separately. Keep PR #18 Draft and do not merge or release.

### Follow-up 2026-11-03 — real-mode evidence placeholder gate

- `--require-real` now rejects explicit `TBD`, `TODO`, `N/A`, `unknown`, `pending`, `placeholder`, `unverified`, and `-` evidence markers (case/outer-whitespace insensitive) for both positive fields and independently verified absence (`null`) labels. Previously such markers could satisfy the nonblank evidence check and yield a misleading real-mode structural PASS.
- The scorer and read-only goldset inventory share the same placeholder policy; no production DOI engine, PDF4QT, QSettings or private files were changed.
- Added fabricated-contract regression cases for positive title and null SI DOI evidence. **CI on the new exact HEAD is required**; no actual multi-publisher PDF precision/recall or Windows GUI validation is claimed.
- Next: check exact-commit Python and Windows QtCore workflow results; if green, collect authorized/public 20+ PDF goldset, run private local hash/header audit and native snapshot capture, then report real field-wise metrics only with independently reviewed labels.

### Follow-up 2026-11-03 — DOI suffix punctuation accuracy

- **Root cause:** `normalize("doi", value)` used `rstrip("., ")` when scoring. DOI suffixes can contain terminal punctuation; the scorer could incorrectly classify `10.1000/example.` and `10.1000/example` as the same DOI (false TP). This is an **evaluation bug**, not a request to change the production DOI extractor.
- **Reproduction:** use the synthetic ACS case, append `.` or `,` to the predicted DOI only, then score it. Previously the wrong value passed; now it produces one DOI FP and one FN. A matching full DOI URL including the punctuation still passes.
- **Files:** `tools/bibliography_accuracy_lab.py`, `tests/test_bibliography_accuracy_lab.py`. No metadata fetcher, reader, QSettings, PDF4QT, or other PR touched.
- **Prior verified CI:** [#41514606504](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41514606504) reported Python and Windows QtCore success for the **previous** `80faae5c` HEAD, not for these new commits.
- **Next exact action:** check the new branch-head Python/Windows CI jobs for the added DOI test; fix any failure before claiming PASS. Then obtain 20+ independently reviewed authorized/public PDFs, capture native read-only snapshots on Windows at a pinned PR #16 SHA, and separately verify real HTTP 429/404, existing user-like QSettings, file move/restart, and card display. No real accuracy numbers are available yet.

### Follow-up 2026-11-03 — opt-in local PDF byte-integrity gate

**Gap:** The existing `--require-real` scorer checked a declared 64-hex PDF
SHA256 string, but never read the corresponding PDF bytes. A fabricated or
stale digest could therefore accompany a numerically passing accuracy report.
The separate goldset audit already supported a local, read-only hash/header
check; this follow-up **reuses** that function instead of implementing a
second PDF reader or changing the production metadata engine.

Use `--pdf-dir` with `--require-real` to require locally staged files
named `<sha256>.pdf`. The scorer now includes `pdf_integrity_audit` in
its report and returns **FAIL (exit 1)** for missing, tampered, unreadable or
unsafe files. It blocks `--pdf-dir` without real mode, nonexistent/symlink
directories, and report paths inside the PDF directory (**exit 2**).
The report includes only opaque case IDs and reason codes, not private file
paths, PDF bytes or SHA256 values. Both inputs remain untouched.

```sh
python -B tools/bibliography_accuracy_lab.py --require-real \
  --expected-engine-commit YOUR_EXACT_40_HEX_SHA \
  --goldset LOCAL_GOLDSET.json --predictions LOCAL_SNAPSHOT.json \
  --pdf-dir LOCAL_PRIVATE_HASH_NAMED_PDFS --report LOCAL_SCORE.json
```

**Synthetic reproduction:** the added tests create seven distinct fake
PDF-header byte streams and fabricated evidence in a disposable directory.
With zero staged files, expect `pdf_integrity_audit.ready=false`, exit 1.
With all matching hashes, expect the integrity subcheck to pass; after
tampering with one file, expect `sha256_mismatch`, exit 1. CLI tests
also cover unsafe report/directory options. This is **not** real publisher
PDF extraction or independently authenticated labels. A successful hash
check proves only that the declared bytes match the staged files; it does
not establish the publisher, correctness of gold labels, or a Windows run.

Changed: `tools/bibliography_accuracy_lab.py`,
`tests/test_bibliography_accuracy_lab.py`, this document. Prior exact-head
CI [#41523997677](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41523997677)
was **SUCCESS** for `a61df5ea`; new-HEAD Python/Windows jobs must be
checked independently. Next: independently label 20+ authorized/public
publisher PDFs, verify their bytes privately, capture PR #16 native snapshots
at an exact Windows build SHA, then test real HTTP 429/404, QSettings and
GUI file-move/restart. Do not commit any real PDF, report, or user settings.
Keep Draft PR #18; no merge or release.


### Follow-up 2026-11-03 — capture-time PDF identity binding

**Root cause:** The earlier `--pdf-dir` integrity check proved that the
goldset-declared PDF bytes existed locally, but the scorer joined the native
prediction to those bytes using only an opaque case ID. Two independently
captured snapshots could be swapped between cases while all goldset PDF hashes
still matched. This was a **measurement-identity gap**, not a defect in the
production DOI/Crossref/OpenAlex extractor.

**Fix:** With `--require-real --pdf-dir`, each prediction case must now
include a 64-hex `source_sha256` matching that case's goldset
`source.sha256`. Missing, malformed, or mismatched bindings block the
report with exit code **2**; neither hashes nor private paths appear in error
messages. The standalone QtCore `captureCase(opaqueId, visibleRecord,
localPdfSha256)` accepts an optional caller-supplied hash, rejects invalid or
all-zero values, and emits a lowercase `source_sha256` without reading any
PDF or touching production QSettings. Legacy synthetic two-argument calls
remain supported. The local driver must independently hash the **exact PDF
opened for the captured record** at capture time; copying the goldset digest
without measuring that PDF defeats the purpose. Neither hash matching nor
this binding authenticates publisher identity or independently verifies
human gold labels.

**Synthetic reproduction:** Construct two goldset cases with distinct
fabricated PDF hashes and otherwise correct observed fields. Swap their
`source_sha256` values while keeping case IDs unchanged. Previously the
`--pdf-dir` integrity check could pass; now the scorer blocks with
`capture PDF binding mismatch`. Missing or malformed bindings also block.
The Python regression checks all three, the native QtCore harness checks
optional/invalid digest handling, and source-contract tests check the
pure read-only adapter.

**Changed:** `tools/bibliography_accuracy_lab.py`,
`tests/native_accuracy/snapshot_adapter.h`,
`tests/native_accuracy/main.cpp`,
`tests/test_bibliography_accuracy_lab.py`,
`tests/test_accuracy_snapshot_source_contract.py`, and this handoff.
Previous exact-HEAD GitHub Actions
[#41534233194](https://github.com/MRYoonn/LUNA-Library-V2/actions/runs/41534233194)
was **SUCCESS** for `77b9c3bf` (Python and Windows QtCore), **not** for this
change. Check new exact-HEAD CI before claiming PASS.

**Next exact steps:** (1) Check Python/Windows QtCore CI at the new commit,
repair any failure; (2) implement/review a local-only driver that hashes
the actual authorized PDF while capturing PR #16's record and supplies
`source_sha256`; (3) independently label 20+ ACS/RSC/Wiley/Nature/Elsevier
PDFs, then score with `--require-real --pdf-dir --expected-engine-commit`;
(4) separately validate real HTTP 429/404, legacy QSettings, Windows GUI,
manual-value protection and physical move/restart. No real extraction
accuracy is claimed. PR #18 remains Draft; no main/other PR changes,
auto-merge or release.

### Follow-up 2026-11-03 — opt-in local PDF byte capture helper

**Root cause / reproducible gap:** The previous read-only Qt adapter accepted a
caller-supplied `source_sha256`. A local driver could accidentally copy the
goldset digest instead of hashing the PDF whose record was captured, making
two unrelated files appear correctly bound. Reproduce by providing a fabricated
matching digest for the wrong PDF: the existing schema checks only compare
strings, not actual bytes.

**Added (isolated test-only code):**
`tests/native_accuracy/local_pdf_capture.h` provides
`captureCaseFromLocalPdf(opaqueId, alreadyLoadedRecord, absolutePdfPath)`.
It opens an explicit local PDF read-only, checks the PDF header, hashes the
complete bytes in bounded 1 MiB chunks, and passes the computed SHA256 to the
existing whitelisted `captureCase`. Missing/non-PDF files and stale paths
return an empty capture. It never writes a PDF, reads QSettings, fetches DOI
metadata, exports a filename, or touches the production renderer. The native
synthetic test writes disposable PDF-like bytes in `QTemporaryDir`, verifies
hash equality, rename stability, mutation detection and missing/invalid input.
`tools/preflight_public.py` allowlists only this reviewed source header.

**Important limitations:** The caller must ensure `alreadyLoadedRecord` was
obtained from the **same opened PDF** and keep the file stable during hashing;
this helper cannot prove the association or prevent a concurrent replacement.
It rejects direct file symlinks, but it is not a security boundary against
untrusted parent directories. A digest match does not establish publisher
authenticity, accuracy of human gold labels, or actual GUI card display.
No real or personal PDFs, QSettings, snapshots, or results were uploaded.

**Validation:** Source committed for independent Python/Windows QtCore CI;
check the exact new HEAD run before reporting PASS. The prior HEAD
`49ffa153` passed GitHub Actions #41543035855, but that does **not** test this
new helper. A future Windows owner must explicitly wire this opt-in helper to
the same already-loaded PR #16 bibliography record, then run
`--require-real --pdf-dir --expected-engine-commit` against 20+ authorized
ACS/RSC/Wiley/Nature/Elsevier PDFs and independently verify manual edits,
offline/429/404, restart/move, and card rendering. Keep PR #18 Draft.

**Windows CI failure / repair:** Exact commit `c5d7a866` passed Python
but failed the QtCore MSVC compile in run #41549471856:
`main.cpp(411): C2374/C2086 pdfBytes redefinition`. The newly added
synthetic capture test reused the name of an existing physical move fixture
later in the same `main()` scope. Reproduce by compiling
`tests/native_accuracy` with MSVC. Commit `88415499` renamed only the new
fixture to `capturePdfBytes`; verify the subsequent exact-HEAD Windows CI
before treating the repair as PASS. No production logic was changed.

## Follow-up 2026-11-03 — SI parent metadata must not satisfy main-paper field coverage

The `--field-coverage-gate` publisher-specific core-field counts now use
**main articles only** (`expected.document_kind == "main_article"`). An SI
may legitimately mention its parent's DOI, title, journal, year, and authors,
but those labels are not proof that the publisher's **main-paper** extractor
was evaluated. Previously an ACS SI carrying parent metadata could satisfy
the ACS core-field coverage quota even if every ACS main paper lacked a DOI,
title, or terminal-author gold label. Global field label totals still include
all labeled documents, and SI document-kind labels remain separately scored.

Synthetic-only regression: `test_si_metadata_cannot_satisfy_main_publisher_core_coverage`
confirms that an SI's positive parent DOI/title are counted globally but
cannot inflate ACS main-paper denominators; adding an independently labeled
main DOI changes only the correct publisher/main denominator. This is a
collection-readiness correction, **not** an actual publisher PDF precision
or recall measurement. Existing DOI/Crossref/OpenAlex extraction and the
native UI remain untouched.

Verify exact-HEAD Python/Windows QtCore CI. Then collect 20+ distinct
authorized/public PDF copies with independent per-field evidence, capture
read-only records from an exact PR #16 Windows build, and run the local
goldset and snapshot scorer. Real provider HTTP 429/404, personal legacy
QSettings, physical PDF/sidecar moves, and Windows GUI remain unverified.


## Follow-up: explicit blank-snapshot type contract (2026-11-03)

The regression scorer now defines a captured empty value explicitly as JSON
null, an empty string, or an empty array (with exact types). Boolean false and
numeric zero are **not** empty values. A synthetic regression test exercises
both rejected and accepted types. This is defensive contract hardening; no
real publisher PDF, native Windows snapshot, or QSettings was tested here.

Reproduce locally:

```sh
python -B -m unittest discover -s tests -p 'test_bibliography_accuracy_lab.py' -v
```

Next: check CI on this exact HEAD, then prioritize the read-only Windows
capture binding to an actual extracted PDF and 20+ authorized/public goldset
PDFs. Record errors and actual source evidence locally; do not upload user
settings, private documents, or credentials. PR remains Draft.


### 2026-11-03 follow-up: incomplete positive-field capture

**Cause:** The scorer previously flagged an omitted snapshot key only when the gold label was blank. When a known-positive field (for example a title) was absent from the native snapshot, it was counted as an FN but did not raise a snapshot-integrity error. A capture adapter silently dropping a field was indistinguishable from an explicit extractor abstention.

**Change:** Missing observed keys now produce `missing_snapshot_field` for both positive and negative gold labels. Positive omissions additionally contribute FN; explicit `null` or `""` still represents a measured abstention and contributes FN without a snapshot-integrity error. A synthetic regression covers both outcomes.

**Reproduce:** Run `python -B -m unittest discover -s tests -p 'test_bibliography_accuracy_lab.py' -v`. Remove `title` from the first synthetic prediction case and score again; the report must contain `missing_snapshot_field` and a title FN. Restore the key with `null`; the report must contain `missing` and a title FN, with no `missing_snapshot` increment.

**Pending:** Check this HEAD's Python/Windows CI; connect read-only native capture to the actual PDF SHA256 on Windows; verify real persisted QSettings/manual edits, HTTP 429/404, restart/move; collect permissioned/public publisher PDFs and independently audit evidence. No real accuracy measurement was performed in this change.


### Follow-up — local position-order SI-risk probe (privacy-safe)

Run `python -B -m unittest discover -s tests -p 'test_bibliography_si_risk_probe.py' -v` for fabricated front-page text and SHA/privacy contracts; no private PDF is used in CI. For locally authorized PDFs, install PyMuPDF **on the user's machine only**, keep all PDF files and the reference JSON **outside this repository**, and run:

```sh
python -B tools/bibliography_si_risk_probe.py \
  --reference /PRIVATE/REFERENCE_CANDIDATES.json \
  --pdf-dir /PRIVATE/PDF_DIRECTORY \
  --report /PRIVATE/si-risk-proxy.json
```

Multiple `--pdf-dir` values are supported. Input must be a *provisional reference candidate* document with opaque `case_id`, bare local `file_name`, verified `pdf_sha256`, and `candidate_expected.document_kind`. The tool compares actual local bytes to SHA256; it fails closed on a conflicting same-name file, invalid PDF header, missing file, or leaf symlink. Do not copy personal/permissioned PDFs or candidate references into the repository, CI, or PR comments. The output intentionally contains **only opaque IDs and predicted kind**, not paths, filenames, page text, DOI, titles, authors, or watermark information.

**Scope:** The production `src/lunafirstpage.h` in PR #16 uses any `Supporting Information` / `Supplementary Information` text in the first 16 nonblank y/x-sorted rows to classify the document as SI. A synthetic ACS main-page sample with an `Article Recommendations / Supporting Information` link is therefore flagged as SI by that condition, while a standalone top-edge RSC SI heading is correctly flagged. The local diagnostic uses PyMuPDF's *approximate* y/x geometry and is **not PDF4QT/LUNA execution**, nor precision/recall. Production SI remediation must be owned and verified in PR #16, **not silently patched by this independent PR**. Retest with actual PDF4QT text order and the validated 20+ publisher corpus once a Windows build is available.


### 2026-11-03 — ACS metadata corruption: root cause, remedy, acceptance boundary

Evidence: A local, permissioned set of 27 PDFs (24 main, 3 SI) was examined **outside GitHub**. Their candidate reference SHA256 checks passed using the 19 original ZIP entries and 8 uploaded attachments. Five intermediary scratch copies had conflicting SHA256 hashes; they were excluded. No PDF, watermark, private filename, personal QSettings, or private bibliography catalogue was committed. The comparisons below are a PyMuPDF layout proxy, **NOT native LUNA/PDF4QT accuracy**.

**Source-level root causes inherited from PR #16:**
1. The first-page code and two bibliography routines treated any early "Supporting Information" mention as an SI cover. ACS main papers contain a "Supporting Information" navigation link beside "Article Recommendations"; the old heuristic was triggered in seven sampled ACS main PDFs. This would suppress main-paper metadata when the actual PDF4QT text order matches.
2. The title credibility check accepted Info Title strings such as "jo5c01153 1..9" (printer job names), and the first-page reader could promote that text above actual printed article evidence. Seven sampled ACS PDFs had such Info titles.

**Scoped review candidate added ONLY to branch agent/bibliography-accuracy-lab:**
- A shared isSupportingInformationCover function checks cover-heading shape and ACS article navigation/citation cues; it is used by the first-page reader, firstPageCitation and firstPageIdentity. Genuine ACS and RSC SI covers remain recognized.
- credibleTitle excludes the observed printer-job ID format and first-page evidence outranks PDF Info.
- An offline firstPageAcsTitle fallback extracts the visible ACS main title only with a publisher masthead, "Cite This" line and plausible author boundary. It abstains on SI and ambiguous layouts; it is not a generic cross-publisher parser.
- Native QtCore tests were added for SI covers, ACS navigation, title candidate recovery and negative cases. Manual field overrides, DOI extraction, Crossref/OpenAlex, PDF4QT and main were not changed.

**Local proxy outcome:** After the rule changes the position-order proxy agreed with 27/27 provisional kind labels, and seven ACS printed-title candidates matched their provisional reference titles after Unicode/hyphen normalization. Before the changes the same proxy triggered seven ACS-main SI false positives. These references still need independent human sign-off; this is NOT a real Windows LUNA precision/recall measurement and cannot justify a "perfect" accuracy claim.

**Reproduce and verify:** From the original 19-entry ZIP and eight new attachments in a private local directory, first verify PDF SHA256 against the privately held reference candidates, then y/x-sort first-page text lines and compare presence of the ACS masthead, "Cite This", article navigation and a standalone SI heading. The old first-16-lines contains check flags seven main articles as SI. Run the exact PR #18 HEAD isolated Windows QtCore CI and compile the full LUNA PR #16 stack on Windows, then capture exact PDF4QT extraction snapshots for all 27 original files. Review all DOI/title/final-author/journal/year/volume/issue/page/article-number/kind labels manually and score them with the existing private goldset harness. Test offline, 429/404, QSettings manual overrides, move and restart. Keep all original PDFs, private filenames, watermark text and settings out of GitHub and CI.

**Outstanding:** New Windows QtCore checks have been added but their exact-HEAD CI completion has NOT been verified. The full Windows GUI, real DOI-provider network behavior and end-to-end field accuracy remain unverified. PR #18 remains Draft; PR #16 and other PR branches, main, renderer and PDF4QT are unchanged. No merge or Release.


### 2026-11-03 — private 27-PDF layout review and publisher-specific citation fixes

The user's 27 original PDFs (24 main, 3 SI, five publisher families) were inspected locally, not uploaded to GitHub. For the 19 ZIP files, independently checked original ZIP member bytes, NOT different SHA256 scratch copies, must be used. The eight additional attachments were also checked. No personal PDF, watermark, file path, private title list, QSettings or credentials was committed.

**Root causes:** The inherited general printed citation regex matched only 11 of 24 real publisher-first-page citation layouts in an independent PyMuPDF y/x-order proxy. Missing classes included Elsevier journal-volume-(year)-article numbering, Springer volume:page ranges, Nature publication footers, ACS Medicinal Chemistry Letters and full RSC page-range footers. Some early article references could also masquerade as the paper's publication fields.

**Remedies scoped ONLY to agent/bibliography-accuracy-lab:** Exact publisher layout patterns and authoritative RSC footers; page versus article_number separation; compatibility with previous low-level Wiley e-number-as-page callers while persisting an explicit article_number; reject ACS/RSC printer spool title strings; RSC main printed title and final author extraction with DOI and masthead evidence even when acceptance-date rows interleave; strict Elsevier printed title/last author from a publisher cover with two named authors and an affiliation boundary. No unverified author or title is promoted. Manual field provenance, existing DOI matching and provider trust remain.

**Local proxy results, NOT LUNA/PDF4QT actual accuracy:** With the updated patterns mirrored in a local PyMuPDF layout diagnostic, the 24 main papers matched all provisionally reviewed journal/year/volume/page-or-article labels (using canonical journal abbreviation aliases). Fifteen have positive page-range labels and nine positive article-number labels. The issue field is blank/unknown in every candidate, so issue recall is NOT measurable. The proxy recovered 7/7 ACS, 4/4 RSC, and 5/5 Elsevier printed title candidates; for the five Elsevier samples the author-block proxy also found the designated last listed authors. Original PDF SHA256 checks passed for 27/27. These are provisional private reference candidates, NOT approved ground truth and NOT measured LUNA extraction success.

**Verification commands:** Run the exact branch isolated Windows QtCore CMake suite plus tests/test_accuracy_snapshot_source_contract.py. Then use an approved Windows LUNA/PDF4QT build to extract the exact 27 user-authorized PDFs locally, collect SHA-bound whitelisted snapshots and execute the existing bibliography_accuracy_lab.py scorer with --require-real against independently reviewed gold labels. Confirm QSettings/manual field preservation, move/restart, provider offline/429/404 and full GUI title/author refresh. The new native QtCore checks have been added but exact-HEAD Windows CI success is NOT YET VERIFIED. Keep PR #18 Draft; do not merge or release, edit PR #16, PDF4QT, renderer or main.


### 2026-11-03 — stale automatic SI state recovery (safely gated)

**Cause:** Before the ACS navigation fix, an ACS main PDF could be stored as supporting_information. Merely fixing the classifier would not clear this QSettings classification on re-extraction. Such a stale label can continue suppressing the article's own title/author and DOI lookup.

**Remedy:** The first-page reader now emits an explicit main_article document kind ONLY when the publisher's printed main title is verified AND a nonconflicting front-matter DOI is accepted. Bibliography's automatic enrichment/explicit refresh recognizes this new verified kind together with first-page title provenance, nonempty DOI and score >=70 to recover a stale machine-classified SI. All other SI states abstain, including low-scoring DOI, absent or ambiguous title, and existing manual record/document-kind ownership. Archived old SI fields are never blindly restored. A new auto-SI label carries its own source provenance.

**Reproduce in isolated disposable QSettings:** Seed an automatic supporting_information record with archived unverified author; rescan with first_page_verified main title, DOI and score 90, and require new main_article title with no resurrected archived author. Seed manual SI and require it to stay SI. Retry with DOI score 55 and require no reclassification. Repeat the strong-evidence restoration through explicit first-page refresh and confirm manual SI still resists it. The corresponding QtCore synthetic cases live in tests/native_accuracy/main.cpp. They are **added, not yet proven passed on the exact Windows CI HEAD**.

**Remaining:** Windows native/full GUI checks for automatic and legacy SI persistence, actual PDF4QT line order, manual QSettings, provider 429/404 and file move/restart; independently approved real goldset and field-by-field precision/recall. Never push private PDFs or personal QSettings. PR #18 stays Draft; no modifications to PR #16/main/PDF4QT/renderer or release.
