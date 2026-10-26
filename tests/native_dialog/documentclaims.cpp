// SPDX-License-Identifier: MIT
#include "lunadocumentclaim.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QEventLoop>
#include <QDebug>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir temp;
    const QString path = temp.path() + QStringLiteral("/한글 논문.pdf");
    QFile file(path);
    if (!temp.isValid() || !file.open(QIODevice::WriteOnly)) return 1;
    file.write("synthetic document identity"); file.close();
    int activated = 0;
    luna::DocumentClaim first([&] { ++activated; }), second([] {});
    using Result = luna::DocumentClaim::Result;
    if (first.acquire(path) != Result::Acquired || second.acquire(path) != Result::Existing) return 2;
    auto settle = [] {
        QEventLoop loop; QTimer::singleShot(150, &loop, &QEventLoop::quit); loop.exec();
    };
    settle();
    if (activated != 1 || first.path().isEmpty() || !second.path().isEmpty()) return 3;
    QLocalSocket client;
    client.connectToServer(luna::DocumentClaim::endpoint(path));
    if (!client.waitForConnected(500)) return 4;
    client.write("acti"); client.flush(); settle();
    if (activated != 1) return 5;
    client.write("vate\n"); client.flush(); settle();
    if (activated != 2) return 6;
    QLocalSocket invalid;
    invalid.connectToServer(luna::DocumentClaim::endpoint(path));
    if (!invalid.waitForConnected(500)) return 7;
    invalid.write("execute\n"); invalid.flush(); settle();
    if (activated != 2) return 8;
    first.release();
    if (second.acquire(path) != Result::Acquired || luna::DocumentClaim::activateExisting(path + QStringLiteral(".missing"))) return 9;
    qInfo() << "[PASS] Per-document exclusive local claim, duplicate activation, fragmented messages, invalid command rejection and release";
    return 0;
}
