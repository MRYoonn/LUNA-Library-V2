// SPDX-License-Identifier: MIT
#pragma once
// Per-user LUNA window broker. External PDF double-clicks join the first
// tabbed window, not a second PDF4QT process. The local socket is UserAccess
// only and carries validated *paths*, never PDF contents or credentials.
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QObject>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QStringList>
#include <QSet>
#include <QQueue>
#include <QUuid>
#include <functional>
#include <memory>

namespace luna {
class WindowRelay final : public QObject {
public:
    enum class Result { Primary, Forwarded, Error };
    using OpenHandler = std::function<void(const QStringList&)>;

    explicit WindowRelay(QObject* parent = nullptr) : QObject(parent) {
        const QString directory = QStandardPaths::writableLocation(
            QStandardPaths::AppLocalDataLocation);
        channel_ = QStringLiteral("luna-tabbed-window-") + QString::fromLatin1(
            QCryptographicHash::hash(directory.toUtf8(),
                                    QCryptographicHash::Sha256).toHex());
        lockPath_ = QDir(directory).filePath(QStringLiteral("window-owner.lock"));
        server_.setSocketOptions(QLocalServer::UserAccessOption);
        server_.setMaxPendingConnections(12);
        connect(&server_, &QLocalServer::newConnection, this, [this] {
            while (auto* client = server_.nextPendingConnection()) {
                auto payload = std::make_shared<QByteArray>();
                auto handled = std::make_shared<bool>(false);
                client->setReadBufferSize(32768);
                connect(client, &QLocalSocket::readyRead, this, [this, client, payload, handled] {
                    // One request per socket. More than one readyRead event can
                    // occur before disconnect; never re-dispatch the same JSON
                    // left in the accumulated input buffer.
                    if (*handled) { client->readAll(); return; }
                    payload->append(client->readAll());
                    if (payload->size() > 32768) { client->abort(); return; }
                    const qsizetype end = payload->indexOf('\n');
                    if (end < 0) return;
                    *handled = true;
                    QJsonParseError error;
                    const QJsonDocument message = QJsonDocument::fromJson(payload->left(end), &error);
                    if (error.error != QJsonParseError::NoError || !message.isObject() ||
                        !message.object().value(QStringLiteral("files")).isArray()) {
                        client->abort(); return;
                    }
                    const QJsonArray array = message.object().value(QStringLiteral("files")).toArray();
                    if (array.size() > 16) { client->abort(); return; }
                    QStringList paths;
                    for (const auto& item : array) {
                        if (!item.isString() || !validPdf(item.toString())) {
                            client->abort(); return;
                        }
                        paths.append(QFileInfo(item.toString()).absoluteFilePath());
                    }
                    // A sender retries when its acknowledgement is lost.
                    // A retry is the same Explorer-open request, not another
                    // instruction to open the PDF. Key by a client-stable UUID.
                    const QJsonValue token = message.object().value(QStringLiteral("requestId"));
                    QString id;
                    if (!token.isUndefined()) {
                        if (!token.isString()) { client->abort(); return; }
                        id = token.toString();
                        if (id.size() != 36 || QUuid::fromString(id).isNull()) {
                            client->abort(); return;
                        }
                    }
                    if (client->write("ok\n") != 3) {
                        client->abort(); return;
                    }
                    client->flush();
                    if (id.isEmpty() || !seenRequestIds_.contains(id)) {
                        if (!id.isEmpty()) {
                            seenRequestIds_.insert(id);
                            requestOrder_.enqueue(id);
                            if (requestOrder_.size() > 256)
                                seenRequestIds_.remove(requestOrder_.dequeue());
                        }
                        pending_.append(paths);
                        QTimer::singleShot(0, this, [this] { dispatchPending(); });
                    }
                    // Disconnect on the next event-loop turn so the sender
                    // can receive our short ACK even on a busy Windows socket.
                    QTimer::singleShot(0, client, [client] {
                        if (client->state() == QLocalSocket::ConnectedState)
                            client->disconnectFromServer();
                    });
                });
                connect(client, &QLocalSocket::disconnected, client, &QObject::deleteLater);
                QTimer::singleShot(3000, client, [client] {
                    if (client->state() != QLocalSocket::UnconnectedState) client->abort();
                    client->deleteLater();
                });
            }
        });
    }

    ~WindowRelay() override {
        server_.close();
        lock_.reset();
    }

    static bool validPdf(const QString& path) {
        const QFileInfo info(path);
        return !path.isEmpty() && path.size() <= 32768 &&
               info.isFile() && !info.isSymLink() &&
               info.suffix().compare(QStringLiteral("pdf"), Qt::CaseInsensitive) == 0;
    }

    void setOpenHandler(OpenHandler handler) {
        handler_ = std::move(handler);
        dispatchPending();
    }

    Result claimOrForward(const QStringList& paths) {
        if (!allValid(paths)) return Result::Error;
        // Reuse one ID for every retry in this process. A lost ack must not
        // dispatch a duplicate tab-open request in the already-running LUNA.
        const QString requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        if (forward(paths, requestId)) return Result::Forwarded;
        if (!QDir().mkpath(QFileInfo(lockPath_).absolutePath())) return Result::Error;
        lock_ = std::make_unique<QLockFile>(lockPath_);
        lock_->setStaleLockTime(0);
        if (!lock_->tryLock(0)) {
            lock_.reset();
            // Another invocation may own the lock before it starts listening.
            for (int attempt = 0; attempt < 50; ++attempt) {
                QThread::msleep(50);
                if (forward(paths, requestId)) return Result::Forwarded;
            }
            return Result::Error;  // Do not silently create another window.
        }
        if (!server_.listen(channel_)) {
            // Only the exclusive lock holder may remove a stale local socket.
            QLocalServer::removeServer(channel_);
            if (!server_.listen(channel_)) { lock_.reset(); return Result::Error; }
        }
        return Result::Primary;
    }

    bool forward(const QStringList& paths, const QString& requestId = {}) const {
        if (!allValid(paths)) return false;
        const QString id = requestId.isEmpty()
            ? QUuid::createUuid().toString(QUuid::WithoutBraces) : requestId;
        if (id.size() != 36 || QUuid::fromString(id).isNull()) return false;
        QJsonObject object;
        object.insert(QStringLiteral("files"), QJsonArray::fromStringList(paths));
        object.insert(QStringLiteral("requestId"), id);
        QByteArray payload = QJsonDocument(object).toJson(QJsonDocument::Compact);
        payload.append('\n');
        if (payload.size() > 32768) return false;
        // The server may have received the request and begun disconnecting
        // before Windows signals readyRead. Check buffered ACK bytes even
        // when waitForReadyRead reports a disconnect. Only retries after a
        // real connection are allowed; never spend seconds contacting an
        // absent app. Every retry retains the exact same idempotency ID.
        bool connectedOnce = false;
        for (int attempt = 0; attempt < 3; ++attempt) {
            QLocalSocket socket;
            socket.connectToServer(channel_, QIODevice::ReadWrite);
            if (!socket.waitForConnected(200)) {
                if (!connectedOnce) return false;
                continue;
            }
            connectedOnce = true;
            if (socket.write(payload) != payload.size() ||
                !socket.waitForBytesWritten(1000)) continue;
            QByteArray answer;
            for (int poll = 0; poll < 3; ++poll) {
                answer.append(socket.readAll());
                if (answer.startsWith("ok\n")) return true;
                if (socket.state() == QLocalSocket::UnconnectedState) break;
                socket.waitForReadyRead(500);
            }
            answer.append(socket.readAll());
            if (answer.startsWith("ok\n")) return true;
        }
        return false;
    }

private:
    static bool allValid(const QStringList& paths) {
        if (paths.size() > 16) return false;
        for (const auto& path : paths) if (!validPdf(path)) return false;
        return true;
    }
    void dispatchPending() {
        if (!handler_) return;
        while (!pending_.isEmpty()) {
            const QStringList batch = pending_.takeFirst();
            handler_(batch);
        }
    }
    QString channel_;
    QString lockPath_;
    std::unique_ptr<QLockFile> lock_;
    QLocalServer server_;
    QList<QStringList> pending_;
    QSet<QString> seenRequestIds_;
    QQueue<QString> requestOrder_;
    OpenHandler handler_;
};
} // namespace luna
