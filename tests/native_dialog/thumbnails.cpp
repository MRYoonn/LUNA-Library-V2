// SPDX-License-Identifier: MIT
// Exercises the production cache, with generated images and no user documents.
#include "lunathumbnailcache.h"
#include <QTemporaryDir>
#include <QFile>
#include <QEventLoop>
#include <QTimer>
#include <QDebug>

static bool waitFor(const std::function<bool()>& ready) {
    if (ready()) return true;
    QEventLoop loop;
    QTimer poll, deadline;
    bool passed = false;
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (ready()) { passed = true; loop.quit(); }
    });
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    poll.start(5); deadline.setSingleShot(true); deadline.start(5000);
    loop.exec();
    return passed;
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir temp;
    if (!temp.isValid()) return 1;
    const QString path = temp.path() + QStringLiteral("/한글 공백.luna-thumb.png");
    luna::ThumbnailCache cache(nullptr);
    QObject owner;
    auto fail = [](const char* message) { qCritical() << message; return 2; };
    int completed = 0;
    auto request = [&] { return cache.request(path, &owner, [&] { ++completed; }); };
    if (!request().isNull() || completed) return fail("Missing image must use fallback");
    QImage input(1200, 1600, QImage::Format_RGB32);
    input.fill(Qt::red);
    if (!input.save(path)) return fail("Cannot create synthetic image");
    if (!request().isNull()) return fail("Cold image was decoded synchronously");
    // Repeated requests by a view must coalesce while decoding is pending.
    for (int i = 0; i < 1000; ++i) request();
    if (!waitFor([&] { return completed == 1; })) return fail("Pending requests did not coalesce");
    const QImage first = request();
    if (first.isNull() || first.width() > 470 || first.height() > 620 || first.pixelColor(0, 0) != QColor(Qt::red))
        return fail("Decoded image is wrong or unbounded");
    const QDateTime oldTime = QFileInfo(path).lastModified();
    const qint64 oldSize = QFileInfo(path).size();
    input.fill(Qt::blue);
    if (!input.save(path)) return fail("Cannot replace image");
    // Explicit crop invalidation must work even with coarse timestamp resolution.
    QFile file(path);
    if (!file.open(QIODevice::ReadWrite) || !file.setFileTime(oldTime, QFileDevice::FileModificationTime))
        return fail("Cannot preserve timestamp for invalidation test");
    file.close();
    cache.invalidate();
    completed = 0; request();
    if (!waitFor([&] { return completed == 1; }) || request().pixelColor(0, 0) != QColor(Qt::blue))
        return fail("Replacement returned stale pixels");
    if (!QFile::remove(path) || !request().isNull()) return fail("Deleted image remained visible");
    if (!file.open(QIODevice::WriteOnly) || file.write("broken PNG") != 10) return fail("Cannot create corrupt fixture");
    file.close(); completed = 0;
    request();
    if (!waitFor([&] { return completed == 1; }) || !request().isNull()) return fail("Corrupt image did not use fallback");
    // Negative cache: a warm corrupt image must not queue further callbacks.
    for (int i = 0; i < 100; ++i) request();
    QCoreApplication::processEvents();
    if (completed != 1) return fail("Corrupt image repeatedly decoded");
    if (!file.open(QIODevice::WriteOnly) || !file.resize(5'000'001)) return fail("Cannot create oversized fixture");
    file.close(); completed = 0;
    if (!request().isNull() || completed) return fail("Oversized file accepted");
    QImage wide(4097, 1, QImage::Format_RGB32); wide.fill(Qt::green);
    if (!wide.save(path)) return fail("Cannot create oversized dimension fixture");
    request();
    if (!waitFor([&] { return completed == 1; }) || !request().isNull()) return fail("Oversized dimensions accepted");
    input.fill(Qt::green);
    if (!input.save(path)) return fail("Cannot restore fixture");
    cache.invalidate();
    bool deadCallback = false;
    auto* transient = new QObject;
    cache.request(path, transient, [&] { deadCallback = true; });
    delete transient;
    completed = 0; request();
    if (!waitFor([&] { return completed == 1; }) || deadCallback) return fail("Destroyed owner callback executed");
    // Destroy a cache with jobs pending. Its workers must finish before QObject
    // destruction, and queued GUI completions must be discarded safely.
    {
        luna::ThumbnailCache shortLived(nullptr);
        shortLived.request(path, &owner, [&] { deadCallback = true; });
    }
    QCoreApplication::processEvents();
    if (deadCallback) return fail("Destroyed cache delivered callback");
    qInfo() << "[PASS] Unicode paths, bounded decode, request coalescing, replacement, deletion, corrupt/large images and owner/cache lifetime"
            << "original PNG bytes" << oldSize;
    return 0;
}
