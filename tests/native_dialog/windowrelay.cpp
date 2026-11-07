// SPDX-License-Identifier: MIT
// Real second-process roundtrip for Explorer .pdf -> first LUNA window.
// Synthetic user-profile channel, synthetic local PDF; never user documents.
#include "lunawindowrelay.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <QDebug>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    const bool secondary = args.size() == 4 && args[1] == QStringLiteral("--second");
    const QString appId = secondary ? args[2]
        : QStringLiteral("LUNA-WindowRelay-Native-%1").arg(app.applicationPid());
    QCoreApplication::setOrganizationName(QStringLiteral("LUNA-Test"));
    QCoreApplication::setApplicationName(appId);
    auto fail = [](const char* message) {
        qCritical() << message;
        return 1;
    };

    if (secondary) {
        luna::WindowRelay client;
        return client.claimOrForward({args[3]}) ==
                       luna::WindowRelay::Result::Forwarded ? 0 : 4;
    }

    QTemporaryDir directory;
    if (!directory.isValid()) return fail("Temporary directory unavailable");
    const QString filename = directory.filePath(QStringLiteral("논문 Sample with spaces.pdf"));
    QFile file(filename);
    if (!file.open(QIODevice::WriteOnly) ||
        file.write("%PDF-1.4\nSynthetic only\n") < 20) return fail("Synthetic PDF setup failed");
    file.close();

    luna::WindowRelay owner;
    if (owner.claimOrForward({}) != luna::WindowRelay::Result::Primary)
        return fail("Fresh app could not acquire exclusive window ownership");
    int received = 0;
    QStringList delivered;
    owner.setOpenHandler([&](const QStringList& paths) {
        ++received;
        delivered = paths;
    });
    auto spawn = [&]() {
        QProcess child;
        child.setProgram(app.applicationFilePath());
        child.setArguments({QStringLiteral("--second"), appId, filename});
        child.start();
        if (!child.waitForStarted(3500)) return false;
        QEventLoop loop;
        QTimer interval;
        QTimer timeout;
        interval.setInterval(20); timeout.setSingleShot(true);
        QObject::connect(&interval, &QTimer::timeout, &loop, [&] {
            if (received >= 1) loop.quit();
        });
        QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        interval.start(); timeout.start(5000); loop.exec();
        if (received < 1) { child.kill(); child.waitForFinished(); return false; }
        return child.waitForFinished(3500) && child.exitStatus() == QProcess::NormalExit &&
               child.exitCode() == 0;
    };
    if (!spawn()) return fail("Child PDF launch did not join primary process");
    if (received != 1 || delivered != QStringList{filename})
        return fail("First forwarded PDF filename did not survive IPC");
    // Repeat command: frontend can choose existing tab instead of new duplicate.
    received = 0;
    if (!spawn()) return fail("Second shell launch did not join primary");
    if (received != 1 || delivered != QStringList{filename})
        return fail("Duplicate forwarded filename was corrupted");

    const QString invalid = directory.filePath(QStringLiteral("malicious.exe"));
    QFile binary(invalid);
    if (!binary.open(QIODevice::WriteOnly)) return fail("Bad extension fixture failed");
    binary.write("MZ"); binary.close();
    if (owner.forward({invalid}) || owner.forward({QStringLiteral("missing.pdf")}))
        return fail("Non-PDF or missing document unexpectedly accepted");
    if (received != 1) return fail("Bad request changed primary document batch");
    qInfo() << "PASS: same-user child PDF launch joins existing window,"
                 " Korean paths, spaces, repeated launches, bad-path rejection";
    return 0;
}
