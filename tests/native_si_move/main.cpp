// SPDX-License-Identifier: MIT
// Independent consumer contract: PR #16's pinned move helper + PR #17 UUID.
// Only disposable synthetic PDF-shaped bytes are used. No app settings/network.
#include "lunafilemove.h"
#include "lunasiidentity.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QDebug>
#include <stdexcept>

namespace {
const QString kId = QStringLiteral("22222222-2222-4222-8222-222222222222");
const QByteArray kPdf("%PDF-1.4\n% synthetic-only move contract\n");
const QByteArray kThumb("synthetic thumbnail bytes");
const QByteArray kMeta = QJsonDocument(QJsonObject{
    {QStringLiteral("schemaVersion"), 1},
    {QStringLiteral("documentId"), kId},
    {QStringLiteral("manualTitle"), QStringLiteral("Human-supplied title")}
}).toJson(QJsonDocument::Compact);

void require(bool ok, const char* what) {
    if (!ok) throw std::runtime_error(what);
}
bool put(const QString& path, const QByteArray& data) {
    QFile f(path);
    return f.open(QIODevice::WriteOnly | QIODevice::NewOnly) &&
           f.write(data) == data.size();
}
QByteArray bytes(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}
bool absent(const QString& path) {
    return !QFileInfo::exists(path) && !QFileInfo(path).isSymLink();
}
struct Fixture {
    QString src, dst, srcMeta, dstMeta, srcThumb, dstThumb;
};
Fixture fixture(const QString& root, const QString& name) {
    const QDir base(root);
    require(QDir().mkpath(base.filePath(name + QStringLiteral("/old"))), "old folder");
    require(QDir().mkpath(base.filePath(name + QStringLiteral("/new"))), "new folder");
    const QString oldDir = base.filePath(name + QStringLiteral("/old"));
    const QString newDir = base.filePath(name + QStringLiteral("/new"));
    Fixture f{
        QDir(oldDir).filePath(QStringLiteral("Paper.pdf")),
        QDir(newDir).filePath(QStringLiteral("Paper.pdf")),
        QDir(oldDir).filePath(QStringLiteral("Paper.luna-meta.json")),
        QDir(newDir).filePath(QStringLiteral("Paper.luna-meta.json")),
        QDir(oldDir).filePath(QStringLiteral("Paper.luna-thumb.png")),
        QDir(newDir).filePath(QStringLiteral("Paper.luna-thumb.png"))
    };
    require(put(f.src, kPdf) && put(f.srcMeta, kMeta) &&
            put(f.srcThumb, kThumb), "create synthetic trio");
    return f;
}
QString targetFolder(const Fixture& f) { return QFileInfo(f.dst).absolutePath(); }
void assertSource(const Fixture& f) {
    require(bytes(f.src) == kPdf, "original PDF changed");
    require(bytes(f.srcMeta) == kMeta, "original UUID metadata changed");
    require(bytes(f.srcThumb) == kThumb, "original thumbnail changed");
}
void assertTargetEmpty(const Fixture& f) {
    require(absent(f.dst) && absent(f.dstMeta) && absent(f.dstThumb),
            "destination has unexpected moved file");
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        const QStringList args = app.arguments();
        if (args.size() == 5 && args.at(1) == QStringLiteral("--verify-restart")) {
            const QString& root = args.at(2);
            const QString& pdf = args.at(3);
            require(luna::SiIdentity::read(root, pdf).id == args.at(4),
                    "fresh process failed to recover UUID");
            require(bytes(luna::SiIdentity::sidecarPath(pdf)) == kMeta,
                    "fresh process changed metadata");
            return 0;
        }
        QTemporaryDir temp;
        require(temp.isValid(), "temporary fixture root");
        const QString root = temp.path();

        // No-overwrite collision: a different paper's sidecar must not be adopted.
        const Fixture collision = fixture(root, QStringLiteral("meta-collision"));
        require(put(collision.dstMeta, QByteArray("unrelated")), "collision sidecar");
        require(!luna::movePdfWithThumbnails(collision.src, targetFolder(collision)).success,
                "metadata collision was accepted");
        assertSource(collision);
        require(bytes(collision.dstMeta) == "unrelated" && absent(collision.dst),
                "collision overwrote an unrelated document");

        // Failure after moving both optional sidecars must restore all three.
        const Fixture rollback = fixture(root, QStringLiteral("rollback"));
        const auto rejectPdf = [&](const QString& from, const QString& to) {
            return from != rollback.src && luna::renameWithoutReplacement(from, to);
        };
        require(!luna::movePdfWithThumbnails(rollback.src, targetFolder(rollback),
                                              rejectPdf).success, "PDF failure accepted");
        assertSource(rollback);
        assertTargetEmpty(rollback);
        require(luna::SiIdentity::read(root, rollback.src).id == kId,
                "rollback did not retain original identity");

        // If metadata rollback itself fails, never erase the surviving sidecar;
        // the failure must identify its exact surviving destination to the caller.
        const Fixture stranded = fixture(root, QStringLiteral("rollback-blocked"));
        const auto rejectMetaRollback = [&](const QString& from, const QString& to) {
            if (from == stranded.src ||
                (from == stranded.dstMeta && to == stranded.srcMeta)) return false;
            return luna::renameWithoutReplacement(from, to);
        };
        const auto partial = luna::movePdfWithThumbnails(
            stranded.src, targetFolder(stranded), rejectMetaRollback);
        require(!partial.success && partial.error.contains(stranded.dstMeta),
                "stranded metadata location omitted from failure");
        require(bytes(stranded.src) == kPdf && bytes(stranded.dstMeta) == kMeta &&
                absent(stranded.srcMeta) && absent(stranded.dst),
                "failed rollback lost PDF or metadata");
        require(bytes(stranded.srcThumb) == kThumb && absent(stranded.dstThumb),
                "failed rollback corrupted unrelated thumbnail");

        // A successful physical move preserves UUID and original bytes.
        const Fixture moved = fixture(root, QStringLiteral("success"));
        const auto done = luna::movePdfWithThumbnails(moved.src, targetFolder(moved));
        require(done.success && done.documentPath == moved.dst, "paired move failed");
        require(absent(moved.src) && absent(moved.srcMeta) && absent(moved.srcThumb),
                "paired move left an old file behind");
        require(bytes(moved.dst) == kPdf && bytes(moved.dstMeta) == kMeta &&
                bytes(moved.dstThumb) == kThumb, "paired move modified bytes");
        require(luna::SiIdentity::read(root, moved.dst).id == kId,
                "moved PDF lost identity");
        QProcess fresh;
        fresh.start(QCoreApplication::applicationFilePath(),
                    {QStringLiteral("--verify-restart"), root, moved.dst, kId});
        require(fresh.waitForStarted(5000) && fresh.waitForFinished(15000) &&
                fresh.exitStatus() == QProcess::NormalExit && fresh.exitCode() == 0,
                "fresh-process identity check failed");

        // A copy must NOT inherit a UUIDv4 sidecar or a Main↔SI link.
        const QString copyDir = QDir(root).filePath(QStringLiteral("copy"));
        require(QDir().mkpath(copyDir), "copy folder");
        const auto copied = luna::copyPdfWithThumbnails(moved.dst, copyDir);
        require(copied.success && bytes(copied.documentPath) == kPdf,
                "copy of synthetic PDF failed");
        require(luna::SiIdentity::read(root, copied.documentPath).error ==
                QStringLiteral("identity_missing"), "copy inherited source UUID");
        require(absent(luna::SiIdentity::sidecarPath(copied.documentPath)),
                "copy inherited metadata sidecar");
        qInfo() << "LUNA_SI_MOVE_CONTRACT_PASS: pinned PR16 paired move, rollback, fresh restart";
        return 0;
    } catch (const std::exception& exc) {
        qCritical() << "LUNA_SI_MOVE_CONTRACT_FAIL:" << exc.what();
        return 1;
    }
}
