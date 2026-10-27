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
    const auto firstResult = first.acquire(path), secondResult = second.acquire(path);
    if (firstResult != Result::Acquired || secondResult != Result::Existing) {
        qCritical() << "Claim ownership failed" << int(firstResult) << int(secondResult); return 2;
    }
    auto settle = [] {
        QEventLoop loop; QTimer::singleShot(150, &loop, &QEventLoop::quit); loop.exec();
    };
    settle();
    if (activated != 1 || first.path().isEmpty() || !second.path().isEmpty()) {
        qCritical() << "Duplicate activation failed" << activated << "owner has path" << !first.path().isEmpty()
                    << "duplicate has path" << !second.path().isEmpty(); return 3;
    }
    QLocalSocket client;
    client.connectToServer(luna::DocumentClaim::endpoint(path));
    if (!client.waitForConnected(500)) { qCritical() << "Fragment client failed" << client.errorString(); return 4; }
    client.write("acti"); client.flush(); settle();
    if (activated != 1) { qCritical() << "Partial message activated" << activated; return 5; }
    client.write("vate\n"); client.flush(); settle();
    if (activated != 2) { qCritical() << "Completed message did not activate" << activated; return 6; }
    QLocalSocket invalid;
    invalid.connectToServer(luna::DocumentClaim::endpoint(path));
    if (!invalid.waitForConnected(500)) return 7;
    invalid.write("execute\n"); invalid.flush(); settle();
    if (activated != 2) return 8;
    first.release();
    if (second.acquire(path) != Result::Acquired || luna::DocumentClaim::activateExisting(path + QStringLiteral(".missing"))) return 9;
    first.release(); second.release();
    const QString future = temp.path() + QStringLiteral("/아직 없는 저장 파일.pdf");
    const QString reservedIdentity = luna::DocumentClaim::identity(future);
    if (reservedIdentity.isEmpty() || first.acquire(future) != Result::Acquired || second.acquire(future) != Result::Existing) return 10;
    QFile created(future); if (!created.open(QIODevice::WriteOnly)) return 11;
    created.write("synthetic Save As destination"); created.close();
    if (luna::DocumentClaim::identity(future) != reservedIdentity || second.acquire(future) != Result::Existing) return 12;
    first.release();
    if (second.acquire(future) != Result::Acquired) return 13;
    qInfo() << "[PASS] Per-document exclusive local claim, duplicate activation, fragmented messages, invalid command rejection and release";
    return 0;
}
