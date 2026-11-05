# Public PDF bibliography goldset

31 actual PDFs: 26 articles/reviews (ACS 6, RSC 6, Nature 6, Wiley/Angewandte
2, Elsevier/Heliyon 6) and 5 publisher supplements. The manifest contains public
bibliographic facts, official DOI/JATS sources and exact PDF SHA256, never PDF
bytes, private user records or local paths. Access failures were excluded, not
replaced with synthetic results. The sample is convenience-selected from
accessible 2023–2025 Europe PMC publisher copies, not a representative chemistry
benchmark. Wiley access yielded only two PDFs. Reviews are explicitly included.

Answers come from publisher-supplied JATS article metadata in Europe PMC,
cross-checked against first-page text/typography. Preserve page ranges from
`page-range` (not just `fpage`); Nature/Heliyon identifiers are article numbers.
Wiley's printed `Angew. Chem. Int. Ed.` is an official alias for its full JATS
journal title. An illustrated RSC cover precedes the article for PMC10189874;
first-page-only author/range recovery must abstain. Last author means the final
listed contributor, not necessarily the correspondence contact. SI expected
bibliographic fields are empty: never copy the parent title/DOI onto SI.
PMC10803759-SI1 has no text layer on the cover and is deliberately retained as a
real missing-text case; no OCR performance is claimed.

Fetch and run the actual PDF4QT/Qt Windows extractor (network disabled for
metadata lookup; only manifest downloads use network):

```bash
python -B tools/evaluate_goldset.py --fetch TEMP_PUBLIC_PDFS --exe PORTABLE/LUNA.exe --predictions TEMP_PREDICTIONS.json --output TEMP_SCORE.json
```

The full Windows workflow runs this command on its exact build SHA. The command
fails if any required download/checksum/native extraction fails. Results retain
field counts (`correct`, `missing`, `wrong`) and per-document exactness. Score
normalization ignores Unicode compatibility, case, punctuation and spacing only
for titles/authors/journal aliases. DOI retains punctuation; year/volume/type
are exact; page ranges normalize only dash style and spacing. No fuzzy
matching, truncation or treating a start page as a full range.
Missing documents are counted as failures. Empty SI fields count as correct
only when the native evaluator actually returned that record.

For reproducible offline algorithm comparisons, `luna_metadata_snapshot` accepts
JSON snapshots with `id`, `pages` (at most three), `lines` (`text`, `x`, `y`,
`fontSize`), `height`, `infoText`, `embeddedTitle`. It invokes the same production
`firstPageEvidence` function. Local before/after snapshots used PyMuPDF 1.26.6;
they are explicitly separate from direct Windows PDF4QT results. Raw snapshots
are kept outside the public repository. This corpus guided the fixes, so these
are development-set results, not held-out generalization estimates.
