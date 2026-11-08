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
#include <cstdio>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    const bool secondary = args.size() >= 4 && args[1] == QStringLiteral("--second");
    const bool duplicateRetry = args.size() >= 5 && args[1] == QStringLiteral("--retry-same-id");
    const QString appId = (secondary || duplicateRetry) ? args[2]
        : QStringLiteral("LUNA-WindowRelay-Native-%1").arg(app.applicationPid());
    QCoreApplication::setOrganizationName(QStringLiteral("LUNA-Test"));
    QCoreApplication::setApplicationName(appId);
    auto fail = [](const char* message, int exitCode) {
        std::fprintf(stderr, "WINDOW_RELAY_STAGE_FAIL %d %s\n", exitCode, message);
        std::fflush(stderr);
        return exitCode;
    };

    if (secondary) {
        luna::WindowRelay client;
        return client.claimOrForward(args.mid(3)) ==
                       luna::WindowRelay::Result::Forwarded ? 0 : 4;
    }
    if (duplicateRetry) {
        luna::WindowRelay client;
        // Two acknowledged sends with the same request ID model a lost-ack
        // retry. Only ONE callback may reach the actual LUNA reader.
        const bool first=client.forward(args.mid(4), args[3]);
        const bool second=client.forward(args.mid(4), args[3]);
        return first && second ? 0 : 5;
    }

    QTemporaryDir directory;
    if (!directory.isValid()) return fail("Temporary directory unavailable", 11);
    const QString filename = directory.filePath(QStringLiteral("논문 Sample with spaces.pdf"));
    QFile file(filename);
    if (!file.open(QIODevice::WriteOnly) ||
        file.write("%PDF-1.4\nSynthetic only\n") < 20) return fail("Synthetic PDF setup failed", 12);
    file.close();
    const QString secondFilename = directory.filePath(QStringLiteral("두 번째 distinct research paper.pdf"));
    QFile second(secondFilename);
    if (!second.open(QIODevice::WriteOnly) ||
        second.write("%PDF-1.4\nSynthetic second PDF only\n") < 20)
        return fail("Distinct synthetic PDF setup failed", 21);
    second.close();

    std::fprintf(stderr, "WINDOW_RELAY_STAGE start owner pid=%lld app=%s\n", (long long)app.applicationPid(), qPrintable(appId));
    std::fflush(stderr);
    luna::WindowRelay owner;
    if (owner.claimOrForward({}) != luna::WindowRelay::Result::Primary)
        return fail("Fresh app could not acquire exclusive window ownership", 13);
    int received = 0;
    QStringList delivered;
    owner.setOpenHandler([&](const QStringList& paths) {
        ++received;
        delivered = paths;
    });
    auto spawn = [&](const QStringList& requested, bool retrySameId=false) {
        QProcess child;
        child.setProcessChannelMode(QProcess::MergedChannels);
        const QString program = app.applicationFilePath();
        child.setProgram(program);
        if (retrySameId)
            child.setArguments(QStringList{QStringLiteral("--retry-same-id"), appId,
                QStringLiteral("a93249ac-bd2b-44b5-8b74-9c21fc275145")} + requested);
        else
            child.setArguments(QStringList{QStringLiteral("--second"), appId} + requested);
        std::fprintf(stderr, "WINDOW_RELAY_CHILD_START program=%s file=%s exists=%d\\n",
            qPrintable(program), qPrintable(requested.join(QStringLiteral(";"))), QFileInfo(program).isFile());
        std::fflush(stderr);
        child.start();
        if (!child.waitForStarted(3500)) {
            std::fprintf(stderr, "WINDOW_RELAY_CHILD_START_FAIL code=%d error=%s output=%s\\n",
                int(child.error()), qPrintable(child.errorString()), child.readAll().constData());
            std::fflush(stderr);
            return false;
        }
        QEventLoop loop;
        QTimer interval;
        QTimer timeout;
        interval.setInterval(20); timeout.setSingleShot(true);
        QObject::connect(&interval, &QTimer::timeout, &loop, [&] {
            if (received >= 1) loop.quit();
        });
        QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        interval.start(); timeout.start(5000); loop.exec();
        if (received < 1) {
            std::fprintf(stderr, "WINDOW_RELAY_CHILD_NO_MESSAGE state=%d error=%d exit=%d output=%s\\n",
                int(child.state()), int(child.error()), child.exitCode(), child.readAll().constData());
            std::fflush(stderr);
            child.kill(); child.waitForFinished(); return false;
        }
        // IMPORTANT: the primary process also owns the Qt local-socket server.
        // Blocking waitForFinished() on this GUI/event-loop thread starves the
        // server of the event needed to flush its acknowledgement. This caused
        // intermittent timeouts even after a valid PDF batch was received.
        // Wait asynchronously so the server can finish its pending IPC writes.
        QEventLoop completion;
        QTimer completionTimeout;
        completionTimeout.setSingleShot(true);
        QObject::connect(&child, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                         &completion, &QEventLoop::quit);
        QObject::connect(&completionTimeout, &QTimer::timeout, &completion, &QEventLoop::quit);
        if (child.state() != QProcess::NotRunning) {
            completionTimeout.start(10000);
            completion.exec();
        }
        const bool finished = child.state() == QProcess::NotRunning;
        if (!finished || child.exitStatus() != QProcess::NormalExit || child.exitCode() != 0) {
            std::fprintf(stderr, "WINDOW_RELAY_CHILD_RESULT finished=%d state=%d code=%d error=%d output=%s\\n",
                int(finished), int(child.state()), child.exitCode(), int(child.error()), child.readAll().constData());
            std::fflush(stderr);
        }
        return finished && child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0;
    };
    if (!spawn({filename})) return fail("Child PDF launch did not join primary process", 14);
    if (received != 1 || delivered != QStringList{filename})
        return fail("First forwarded PDF filename did not survive IPC", 15);
    // Opening a DIFFERENT PDF is the actual user regression. A duplicate
    // alone does not prove second Explorer launches reach a new document.
    received = 0;
    delivered.clear();
    if (!spawn({secondFilename})) return fail("Distinct second PDF did not join primary", 22);
    if (received != 1 || delivered != QStringList{secondFilename})
        return fail("Distinct second PDF path changed over real Windows IPC", 23);

    // Repeating the second file also forwards its exact path so NativeTabs
    // may select the existing document rather than create another tab.
    received = 0;
    delivered.clear();
    if (!spawn({secondFilename})) return fail("Repeated second PDF did not join primary", 16);
    if (received != 1 || delivered != QStringList{secondFilename}) {
        std::fprintf(stderr, "WINDOW_RELAY_REPEAT_DIAGNOSTIC received=%d expected=%s actual=%s\n",
            received, secondFilename.toUtf8().toHex().constData(),
            delivered.join(QStringLiteral(";")).toUtf8().toHex().constData());
        return fail("Duplicate second PDF filename was corrupted", 17);
    }
    // A lost ACK may cause exactly the same launch to be sent twice. It must
    // NOT create two extra activations, unlike two separate Explorer launches.
    received = 0;
    delivered.clear();
    if (!spawn({secondFilename}, true))
        return fail("Same-request-id relay retry failed", 27);
    if (received != 1 || delivered != QStringList{secondFilename})
        return fail("Same request ID dispatched twice or changed its Unicode path", 28);

    // Batch delivery must preserve ordering and exact Unicode paths.
    received = 0;
    delivered.clear();
    if (!spawn({filename, secondFilename}))
        return fail("Two-file batch was rejected", 24);
    if (received != 1 || delivered != (QStringList{filename, secondFilename}))
        return fail("Two-file batch lost ordering or Unicode path", 25);

    const QString invalid = directory.filePath(QStringLiteral("malicious.exe"));
    QFile binary(invalid);
    if (!binary.open(QIODevice::WriteOnly)) return fail("Bad extension fixture failed", 18);
    binary.write("MZ"); binary.close();
    if (owner.forward({invalid}) || owner.forward({QStringLiteral("missing.pdf")}))
        return fail("Non-PDF or missing document unexpectedly accepted", 19);
    if (received != 1) return fail("Bad request changed primary document batch", 20);
    qInfo() << "PASS: same-user child joins first window; distinct PDF,"
                 " Unicode/space paths, duplicate and batch launches, bad-path rejection";
    return 0;
}
