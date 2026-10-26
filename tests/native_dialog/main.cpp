// SPDX-License-Identifier: MIT
// A small real Qt dialog test that runs before the expensive PDF4QT build.
#include "lunasavedialog.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QTimer>
#include <QDebug>

int main(int argc, char** argv) {
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication application(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);
    QTemporaryDir temporary(QStringLiteral("%1/luna-native-dialog-XXXXXX").arg(QDir::tempPath()));
    if (!temporary.isValid()) return 1;
    const QString root = temporary.path();
    QDir(root).mkdir(QStringLiteral("library"));
    const QString original = root + QStringLiteral("/library/synthetic.pdf");
    const QByteArray bytes("LUNA synthetic filename fixture; not a rendered PDF\n");
    QFile fixture(original);
    if (!fixture.open(QIODevice::WriteOnly) || fixture.write(bytes) != bytes.size()) return 2;
    fixture.close();
    QString error;
    for (int variant = 0; variant < 2; ++variant) {
        QString parent = root;
#ifdef Q_OS_WIN
        // QFileDialog may resolve case and path aliases differently from its
        // caller. Both forms must select the same existing directory.
        if (variant) parent = QDir::toNativeSeparators(root).toUpper();
#endif
        const QString target = parent + QStringLiteral("/annotated-%1.pdf").arg(variant);
        QFileDialog dialog(nullptr, QStringLiteral("Synthetic Save As"), original,
                           QStringLiteral("Portable Document (*.pdf)"));
        dialog.setAcceptMode(QFileDialog::AcceptSave);
        dialog.setFileMode(QFileDialog::AnyFile);
        bool selected = false;
        QTimer::singleShot(150, &dialog, [&] {
            if (auto* filename = dialog.findChild<QLineEdit*>(QStringLiteral("fileNameEdit")))
                filename->setFocus();
            selected = luna::selectSyntheticCopy(dialog, target, error);
            if (selected) QMetaObject::invokeMethod(&dialog, "accept", Qt::DirectConnection);
            else dialog.reject();
        });
        const int result = dialog.exec();
        if (!selected || result != QDialog::Accepted) {
            qCritical().noquote() << "Native dialog probe:" << error;
            return 3;
        }
        if (QFileInfo::exists(target)) return 4; // Choosing is not saving.
    }
    // Reject an existing target before accepting any overwrite operation.
    QFileDialog invalid(nullptr, QStringLiteral("Synthetic existing file"), original);
    if (luna::selectSyntheticCopy(invalid, original, error)) return 5;
    if (!fixture.open(QIODevice::ReadOnly) || fixture.readAll() != bytes) return 6;
    qInfo() << "[PASS] Native Qt" << qVersion()
            << "focused filename input, Windows path identity and overwrite guard";
    return 0;
}
