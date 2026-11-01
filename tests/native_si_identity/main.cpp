// SPDX-License-Identifier: MIT
// Isolated QtCore tests: synthetic PDF bytes only, never user documents.
#include "../../src/lunasiidentity.h"
#include "../../src/lunametadataengine.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QProcess>
#include <QTemporaryDir>
#include <cstdio>
#include <cstdlib>

static void check(bool yes, const char* name) {
    if (yes) return;
    std::fprintf(stderr, "LUNA_SI_IDENTITY_FAIL: %s\n", name);
    std::fflush(stderr);
    std::abort();
}
static bool put(const QString& path, const QByteArray& bytes) {
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}
static QJsonObject json(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    // A genuine fresh process must recover the same local UUID from disk.
    if (argc == 5 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--verify-restart")) {
        const auto read = luna::SiIdentity::read(QString::fromLocal8Bit(argv[2]),
                                                 QString::fromLocal8Bit(argv[3]));
        return read.ok() && read.id == QString::fromLocal8Bit(argv[4]) ? 0 : 3;
    }
    QTemporaryDir temp;
    check(temp.isValid(), "temporary library");
    const QString root = temp.path();
    check(QDir(root).mkpath(QStringLiteral("Chem")), "create Chem");
    check(QDir(root).mkpath(QStringLiteral("Moved")), "create Moved");
    const QString pdf = QDir(root).filePath(QStringLiteral("Chem/Paper.pdf"));
    const QByteArray original("%PDF-1.4\n% synthetic-only\n");
    check(put(pdf, original), "synthetic PDF fixture");
    const QString meta = luna::SiIdentity::sidecarPath(pdf);
    check(luna::SiIdentity::read(root, pdf).error == QStringLiteral("identity_missing"),
          "missing identity is not invented by read");
    const QJsonObject legacy{{QStringLiteral("schemaVersion"), 1},
                             {QStringLiteral("manualTitle"), QStringLiteral("Human edit")}};
    check(put(meta, QJsonDocument(legacy).toJson()), "legacy sidecar fixture");
    const auto first = luna::SiIdentity::ensure(root, pdf);
    check(first.ok() && first.created && luna::SiIdentity::validId(first.id),
          "explicit enrollment creates v4 UUID");
    check(json(meta).value(QStringLiteral("manualTitle")).toString() ==
              QStringLiteral("Human edit"), "existing human metadata preserved");
    check(luna::SiIdentity::read(root, pdf).id == first.id, "read persisted ID");
    const auto repeat = luna::SiIdentity::ensure(root, pdf);
    check(repeat.ok() && !repeat.created && repeat.id == first.id, "idempotent ensure");
    QFile bytes(pdf);
    check(bytes.open(QIODevice::ReadOnly) && bytes.readAll() == original,
          "PDF bytes never modified");
    bytes.close();

    // The existing Issue #9 move function owns physical moves. Simulate only
    // its successful PDF + metadata sidecar pair, without modifying that code.
    const QString moved = QDir(root).filePath(QStringLiteral("Moved/Renamed.pdf"));
    const QString movedMeta = luna::SiIdentity::sidecarPath(moved);
    check(QFile::rename(meta, movedMeta) && QFile::rename(pdf, moved),
          "synthetic paired rename");
    check(luna::SiIdentity::read(root, moved).id == first.id,
          "UUID survives paired move and restart-style re-read");
    QProcess fresh;
    fresh.start(QCoreApplication::applicationFilePath(),
                {QStringLiteral("--verify-restart"), root, moved, first.id});
    check(fresh.waitForStarted(5000) && fresh.waitForFinished(10000) &&
              fresh.exitStatus() == QProcess::NormalExit && fresh.exitCode() == 0,
          "fresh process restart recovers the same moved PDF identity");
    check(luna::SiIdentity::read(root, pdf).error == QStringLiteral("unsafe_pdf_path"),
          "stale path does not recreate identity");

    const QString copied = QDir(root).filePath(QStringLiteral("Moved/Copy.pdf"));
    check(QFile::copy(moved, copied), "copy synthetic PDF only");
    check(luna::SiIdentity::read(root, copied).error == QStringLiteral("identity_missing"),
          "copy has no inherited identity");
    const auto copyId = luna::SiIdentity::ensure(root, copied);
    check(copyId.ok() && copyId.id != first.id, "copy gets distinct UUID");

    const QString corrupt = QDir(root).filePath(QStringLiteral("Moved/Corrupt.pdf"));
    check(put(corrupt, original), "corrupt fixture");
    const QString corruptMeta = luna::SiIdentity::sidecarPath(corrupt);
    check(put(corruptMeta, QByteArray("{bad")), "malformed JSON fixture");
    check(luna::SiIdentity::ensure(root, corrupt).error == QStringLiteral("invalid_sidecar"),
          "malformed sidecar is never overwritten");
    QFile invalid(corruptMeta);
    check(invalid.open(QIODevice::ReadOnly) && invalid.readAll() == QByteArray("{bad"),
          "malformed sidecar bytes retained");
    invalid.close();
    check(put(corruptMeta, QByteArray("{\"schemaVersion\":2}")),
          "future schema fixture");
    check(luna::SiIdentity::ensure(root, corrupt).error ==
              QStringLiteral("unsupported_sidecar"), "future schema preserved");
    check(put(corruptMeta, QByteArray("{\"schemaVersion\":1,\"documentId\":\"bad\"}")),
          "invalid identity fixture");
    check(luna::SiIdentity::ensure(root, corrupt).error ==
              QStringLiteral("invalid_identity"), "invalid ID not silently replaced");

    QLockFile busy(luna::SiIdentity::sidecarPath(copied) + QStringLiteral(".luna-si.lock"));
    check(busy.tryLock(0), "acquire external identity lock");
    check(luna::SiIdentity::ensure(root, copied).error ==
              QStringLiteral("identity_busy"), "parallel enrollment refused");
    busy.unlock();

    const QString outside = QDir(root).filePath(QStringLiteral("Outside.pdf"));
    check(put(outside, original), "outside fixture at root");
    check(luna::SiIdentity::read(QDir(root).filePath(QStringLiteral("Chem")), outside)
              .error == QStringLiteral("unsafe_pdf_path"), "root boundary enforced");
    check(luna::SiIdentity::ensure(root, QDir(root).filePath(QStringLiteral("Moved")))
              .error == QStringLiteral("unsafe_pdf_path"), "directory not PDF");

    // Reuse PR #16's DOI normalizer; SI identity must not implement another.
    check(luna::Metadata::normalizeDoi(QStringLiteral("https://doi.org/10.1021/JACS.4C05881.")) ==
              QStringLiteral("10.1021/jacs.4c05881"), "shared DOI normalizer");
    std::fprintf(stderr, "LUNA_SI_IDENTITY_TESTS_PASS: synthetic scenarios\n");
    return 0;
}
