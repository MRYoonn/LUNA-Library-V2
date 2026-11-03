"""Source-only checks; NOT evidence of a compiled Qt/Windows native run."""
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "tests" / "native_accuracy" / "snapshot_adapter.h"
NATIVE = ROOT / "tests" / "native_accuracy" / "main.cpp"

class SnapshotSourceContract(unittest.TestCase):
    def test_pure_adapter_has_no_io_or_mutation(self):
        code = HEADER.read_text(encoding="utf-8")
        for forbidden in ("#include <QSettings>", "#include <QFile>", "#include <QNetwork", "setValue(", "sync(", "remove(", "open("):
            self.assertNotIn(forbidden, code)
        self.assertIn("const QVariantMap& visibleRecord", code)

    def test_whitelisted_fields_and_no_identity_export(self):
        code = HEADER.read_text(encoding="utf-8")
        for expected in ('"terminal_author"', '"document_kind"', '"metadata_status"', '"authorsSource"', '"journal_abbreviation"'):
            self.assertIn(expected, code)
        self.assertNotIn('observed.insert(QStringLiteral("identity")', code)
        self.assertNotIn('observed.insert(QStringLiteral("path")', code)
        self.assertIn('fieldSources.insert(field, provider)', code)
        self.assertNotIn('fieldSources.insert(QStringLiteral("private_note")', code)

    def test_capture_digest_is_optional_validated_and_has_no_pdf_io(self):
        header = HEADER.read_text(encoding="utf-8")
        native = NATIVE.read_text(encoding="utf-8")
        self.assertIn("const QString& localPdfSha256 = QString()", header)
        self.assertIn('captured.insert(QStringLiteral("source_sha256")', header)
        self.assertIn("digestPattern.match(localPdfSha256)", header)
        self.assertIn("unbound snapshot does not invent PDF digest", native)
        self.assertIn("synthetic capture-time PDF digest is normalized", native)
        self.assertIn("invalid capture-time PDF digest rejected", native)
        self.assertIn("placeholder capture-time PDF digest rejected", native)

    def test_known_negative_fields_are_explicitly_captured(self):
        header = HEADER.read_text(encoding="utf-8")
        native = NATIVE.read_text(encoding="utf-8")
        self.assertIn("QJsonValue(QJsonValue::Null)", header)
        self.assertIn('observed.insert(QStringLiteral("document_kind")', header)
        self.assertIn('observed.insert(QStringLiteral("terminal_author")', header)
        self.assertIn("SI explicitly captures absent paper fields", native)

    def test_selected_senior_author_is_disclosed_without_exporting_index(self):
        header = HEADER.read_text(encoding="utf-8")
        native = NATIVE.read_text(encoding="utf-8")
        self.assertIn('visibleRecord.value(QStringLiteral("senior_author_index"), -1)', header)
        self.assertIn("seniorIndex >= 0 && seniorIndex < structuredAuthors.size()", header)
        self.assertIn("|| selectedSeniorAuthor", header)
        self.assertIn("selected senior-author index is a manual card override", native)
        self.assertIn("out-of-range index does not change the card", native)
        self.assertNotIn('audit.insert(QStringLiteral("senior_author_index")', header)
        self.assertNotIn('observed.insert(QStringLiteral("senior_author_index")', header)

    def test_physical_move_uses_production_helper_and_keeps_sidecars(self):
        native = NATIVE.read_text(encoding="utf-8")
        self.assertIn('#include "../../src/lunafilemove.h"', native)
        self.assertIn('luna::movePdfWithThumbnails(physicalPdf, physicalTarget)', native)
        self.assertIn('store.relocate(physicalPdf, movedFiles.documentPath)', native)
        for expected in ("physical move rejects occupied thumbnail destination",
                         "collision preserves all original files",
                         "moved PDF and thumbnail bytes preserved",
                         "metadata JSON sidecar remains at source: current helper does not move it",
                         "physical move preserves manual title provenance",
                         "physical move preserves review status"):
            self.assertIn(expected, native)
        # The native test creates disposable files; no user paths or PDFs.
        self.assertIn('QTemporaryDir root', native)
        self.assertIn('synthetic-paper.luna-meta.json', native)
        self.assertIn('synthetic-paper.luna-thumb.png', native)

    def test_fresh_process_restart_checks_real_qsettings_disk_state(self):
        native = NATIVE.read_text(encoding="utf-8")
        self.assertIn("#include <QProcess>", native)
        self.assertIn("--verify-fresh-process", native)
        self.assertIn("freshProcess.waitForStarted(10000)", native)
        self.assertIn("freshProcess.waitForFinished(15000)", native)
        self.assertIn("freshProcess.exitCode() == 0", native)
        self.assertIn("fresh process reads moved PDF bibliography", native)
        self.assertIn("fresh process retains manual provenance", native)
        self.assertIn("fresh process retains offline/429/404 diagnostic", native)
        self.assertIn("fresh process does not claim offline/429/404 complete", native)
        self.assertIn("fresh process retains original title after provider failure", native)
        self.assertIn('QStringLiteral("not_found")', native)
        self.assertIn('QStringLiteral("offline")', native)
        self.assertIn('QStringLiteral("rate_limited")', native)
        self.assertIn("QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsRoot)", native)
        self.assertIn("QTemporaryDir root", native)

    def test_legacy_author_matrix_covers_both_flags_and_fresh_process(self):
        native = NATIVE.read_text(encoding="utf-8")
        # These checks only prove that the native regression is present;
        # the Windows QtCore CTest must actually execute its assertions.
        self.assertIn("variant < 4", native)
        self.assertIn("variant % 2", native)
        self.assertIn('QStringLiteral("source"), QStringLiteral("Manual")', native)
        self.assertIn('QStringLiteral("authorEvidence")', native)
        self.assertIn("verified first-page terminal author survives legacy migration", native)
        self.assertIn("legacy false manual flag still suppresses unverified author", native)
        self.assertIn("fresh process legacy author migration matrix", native)
        self.assertIn("deliberately empty manual author rejects automatic overwrite", native)
        self.assertIn("one-word human author remains visible", native)
        self.assertIn("fresh process preserves deliberate empty manual author", native)
        self.assertIn("fresh process preserves one-word manual author", native)

    def test_first_page_si_cover_and_printer_job_regressions(self):
        """Source safety contract; Qt behavior is checked by the native harness."""
        bibliography = (ROOT / "src" / "lunabibliography.h").read_text(encoding="utf-8")
        firstpage = (ROOT / "src" / "lunafirstpage.h").read_text(encoding="utf-8")
        native = NATIVE.read_text(encoding="utf-8")
        self.assertIn("static bool isSupportingInformationCover(", bibliography)
        self.assertIn("Cite This:", bibliography)
        self.assertIn("Article Recommendations", bibliography)
        self.assertIn("printerJob.match(text).hasMatch()", bibliography)
        self.assertIn("if (isSupportingInformationCover(text)) return {};", bibliography)
        self.assertIn("if(Bibliography::isSupportingInformationCover(first))", firstpage)
        self.assertIn('!Bibliography::credibleTitle(evidence.value("title").toString())', firstpage)
        self.assertIn("ACS main navigation SI link is not an SI cover", native)
        self.assertIn("RSC SI cover correctly identified", native)
        self.assertIn("JOC printer job cannot be title", native)
        self.assertIn("static QString firstPageAcsTitle(", bibliography)
        self.assertIn("if (!sawAuthors || titleLines.isEmpty()) return {};", bibliography)
        self.assertIn("credibleTitle(title) ? title : QString()", bibliography)
        self.assertIn("const QString printedAcsTitle = Bibliography::firstPageAcsTitle(first);", firstpage)
        self.assertIn('evidence["titleSource"] = QStringLiteral("first_page_verified");', firstpage)
        self.assertIn("offline ACS main title from printed front matter", native)
        self.assertIn("unverified ACS cover abstains rather than guessing title", native)

    def test_publisher_citation_title_and_author_guards(self):
        bibliography = (ROOT / "src" / "lunabibliography.h").read_text(encoding="utf-8")
        firstpage = (ROOT / "src" / "lunafirstpage.h").read_text(encoding="utf-8")
        native = NATIVE.read_text(encoding="utf-8")
        for token in ("chemCommFooter", "obcFooter", "naturePage",
                      "natureArticle", "elsevier.match(line)", "springer.match(line)",
                      'fill("article_number"', 'replace("article_number"',
                      "static QString firstPageRscTitle(",
                      "static QString firstPageRscLastAuthor(",
                      "static QVariantMap firstPageElsevierIdentity(",
                      "first-page-elsevier-author-block", "printerSpool.match(text)"):
            self.assertIn(token, bibliography)
        self.assertIn("Bibliography::firstPageRscTitle(first)", firstpage)
        self.assertIn("Bibliography::firstPageRscLastAuthor(first)", firstpage)
        self.assertIn("Bibliography::firstPageElsevierIdentity(first)", firstpage)
        self.assertIn("elsevierIdentity.value(QStringLiteral(", firstpage)
        for token in ("ChemComm interleaved title safely reconstructed",
                      "Elsevier last author excludes next institutional affiliation",
                      "RSC complete footer page range outranks short cite header",
                      "Wiley article number identified while legacy page view remains stable"):
            self.assertIn(token, native)

    def test_native_regressions_seed_isolated_settings(self):
        code = NATIVE.read_text(encoding="utf-8")
        for expected in ('QTemporaryDir', 'Hao Fang', 'legacy author must not reappear',
                         'senior override disclosed separately', 'SI has no invented DOI'):
            self.assertIn(expected, code)

if __name__ == "__main__":
    unittest.main()
