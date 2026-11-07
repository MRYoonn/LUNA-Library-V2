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
                client->setReadBufferSize(32768);
                connect(client, &QLocalSocket::readyRead, this, [this, client, payload] {
                    payload->append(client->readAll());
                    if (payload->size() > 32768) { client->abort(); return; }
                    const qsizetype end = payload->indexOf('\n');
                    if (end < 0) return;
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
                    client->write("ok\n");
                    client->flush();
                    pending_.append(paths);
                    QTimer::singleShot(0, this, [this] { dispatchPending(); });
                    client->disconnectFromServer();
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
        if (forward(paths)) return Result::Forwarded;
        if (!QDir().mkpath(QFileInfo(lockPath_).absolutePath())) return Result::Error;
        lock_ = std::make_unique<QLockFile>(lockPath_);
        lock_->setStaleLockTime(0);
        if (!lock_->tryLock(0)) {
            lock_.reset();
            // Another invocation may own the lock before it starts listening.
            for (int attempt = 0; attempt < 50; ++attempt) {
                QThread::msleep(50);
                if (forward(paths)) return Result::Forwarded;
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

    bool forward(const QStringList& paths) const {
        if (!allValid(paths)) return false;
        QJsonObject object;
        object.insert(QStringLiteral("files"), QJsonArray::fromStringList(paths));
        QByteArray payload = QJsonDocument(object).toJson(QJsonDocument::Compact);
        payload.append('\n');
        if (payload.size() > 32768) return false;
        QLocalSocket socket;
        socket.connectToServer(channel_, QIODevice::ReadWrite);
        if (!socket.waitForConnected(200)) return false;
        if (socket.write(payload) != payload.size() || !socket.waitForBytesWritten(1000))
            return false;
        if (!socket.waitForReadyRead(1500)) return false;
        const QByteArray answer = socket.readAll();
        return answer.startsWith("ok\n");
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
    OpenHandler handler_;
};
} // namespace luna
