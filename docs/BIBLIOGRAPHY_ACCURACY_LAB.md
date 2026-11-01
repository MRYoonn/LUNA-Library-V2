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

Fields: `doi`, `title`, `terminal_author`, `journal`, `journal_abbreviation`, `year`, `volume`, `issue`, `page`, `article_number`, `document_kind` (`main_article` or `supporting_information`). Use `null` to assert that a field **must be absent** (e.g. a reference-only DOI in an SI); **omit** a field when ground truth is not known. Do not label an inaccessible field `null`. For publication metadata, compare the official full journal name and abbreviation separately; no unreviewed abbreviation aliases. Pages normalize en/em dashes; chemical formula, stereochemical descriptors and title punctuation remain significant. A wrong nonempty value produces **both** FP and FN; an abstention on known-positive ground truth produces FN; an unexpected value on known-negative ground truth produces FP. Metrics with no positive denominator are `null`, never invented 100%.

A separate private `kind: "predictions"` JSON must use the same `dataset_id` and case IDs, with `observed` fields representing **captured LUNA outputs from a specific build**. Export the actual card/record state from the application using a reviewed read-only adapter; this harness deliberately does not guess what the UI showed. Record the tested commit and build/run identity outside the synthetic fixture. Never claim synthetic snapshots came from the engine.

## Minimum real-paper acceptance matrix (not yet run)

At least 20 **permissioned/public** PDFs, with at least 3 main articles per ACS/RSC/Wiley/Nature/Elsevier (15), at least 3 SI PDFs (including multiple SI and SI with a reference DOI), and at least 2 no-DOI/ambiguous cases. Add explicit cross-provider DOI/title conflicts. Include both page ranges and article numbers, publisher-specific final-author layouts, year-only dates, journal abbreviations, missing metadata, and offline behavior. Every known field needs independent page/publisher evidence; list unknown labels as `unscored`.

Capture read-only before/after snapshots for: stale `QSettings` from PR #13 (both ambiguous legacy `manual` states), explicit user-edited title/author/journal/citation, concurrent edit and refresh, network offline, HTTP 429/404, invalid DOI, reference-only DOI, mismatched Crossref/OpenAlex DOI, restart, PDF rename/move, SI classification, and two simultaneously opened tabs. The optional `regressions` array compares `before` and `after` via `must_preserve`, `must_remain_empty`, and `expected_status`/`actual_status`. It checks only **supplied snapshots**; a synthetic PASS does not prove these events occurred in LUNA.

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
