// SPDX-License-Identifier: MIT
#pragma once
#include <QCryptographicHash>
#include <QFileInfo>
#include <QDir>
#include <QEventLoop>
#include <QLockFile>
#include <QStandardPaths>
#include <QLocalServer>
#include <QLocalSocket>
#include <QObject>
#include <QTimer>
#include <functional>
#include <memory>

namespace luna {
// Per-user local IPC carries only an activation request, never document bytes
// or commands. Each native Editor lives in its own process so PDF4QT's shared
// plugin instances cannot be rebound to a different document/controller.
class DocumentClaim final : public QObject {
public:
    enum class Result { Acquired, Existing, Error };
    ~DocumentClaim() override { release(); }
    explicit DocumentClaim(std::function<void()> activate, QObject* parent = nullptr)
        : QObject(parent), activate_(std::move(activate)) {
        server_.setSocketOptions(QLocalServer::UserAccessOption);
        server_.setMaxPendingConnections(8);
        connect(&server_, &QLocalServer::newConnection, this, [this] {
            while (auto* client = server_.nextPendingConnection()) {
                client->setReadBufferSize(32);
                auto request = std::make_shared<QByteArray>();
                auto receive = [this, client, request] {
                    request->append(client->readAll());
                    if (request->size() < 9 && !request->contains('\n')) return;
                    if (*request == "activate\n") {
                        activate_();
                        client->write("ok\n");
                        client->flush();
                        return; // The acknowledged client closes the pipe.
                    }
                    client->disconnectFromServer();
                };
                connect(client, &QLocalSocket::readyRead, this, receive);
                connect(client, &QLocalSocket::disconnected, client, &QObject::deleteLater);
                if (client->bytesAvailable()) receive();
                QTimer::singleShot(1000, client, [client] { client->abort(); client->deleteLater(); });
            }
        });
    }
    static QString identity(const QString& path) {
        QString result = QFileInfo(path).canonicalFilePath();
#ifdef Q_OS_WIN
        result = result.toCaseFolded();
#endif
        return result;
    }
    static QString endpoint(const QString& path) {
        return QStringLiteral("luna-document-") + QString::fromLatin1(
            QCryptographicHash::hash(identity(path).toUtf8(), QCryptographicHash::Sha256).toHex());
    }
    static bool activateExisting(const QString& path) {
        if (identity(path).isEmpty()) return false;
        QLocalSocket socket;
        socket.connectToServer(endpoint(path), QIODevice::ReadWrite);
        if (!socket.waitForConnected(150)) return false;
        // Keep the Windows pipe alive until the owner has consumed the request.
        // A completed write is not an acknowledgement from the other process.
        QEventLoop loop;
        QTimer timeout;
        timeout.setSingleShot(true);
        QByteArray response;
        connect(&socket, &QLocalSocket::readyRead, &loop, [&] {
            response.append(socket.readAll());
            if (response.contains('\n') || response.size() > 16) loop.quit();
        });
        connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        connect(&socket, &QLocalSocket::disconnected, &loop, [&] {
            response.append(socket.readAll()); loop.quit();
        });
        if (socket.write("activate\n") != 9) return false;
        socket.flush();
        timeout.start(1000);
        loop.exec(QEventLoop::ExcludeUserInputEvents);
        socket.disconnectFromServer();
        return response == "ok\n";
    }
    Result acquire(const QString& path) {
        const QString canonical = identity(path);
        if (canonical.isEmpty()) return Result::Error;
        if (canonical == path_ && server_.isListening()) return Result::Acquired;
        release();
        if (activateExisting(path)) return Result::Existing;
        // Windows permits several named-pipe server instances with the same
        // name. A per-user PID-aware lock makes ownership exclusive even while
        // the first process is still starting its local server.
        const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/document-claims");
        if (!QDir().mkpath(directory)) return Result::Error;
        lock_ = std::make_unique<QLockFile>(directory + QLatin1Char('/') + endpoint(path) + QStringLiteral(".lock"));
        lock_->setStaleLockTime(0);
        if (!lock_->tryLock(0)) {
            const auto error = lock_->error(); lock_.reset();
            if (error == QLockFile::LockFailedError) { activateExisting(path); return Result::Existing; }
            return Result::Error;
        }
        if (!server_.listen(endpoint(path))) { lock_.reset(); return Result::Error; }
        path_ = canonical;
        return Result::Acquired;
    }
    void release() { server_.close(); lock_.reset(); path_.clear(); }
    QString path() const { return path_; }
private:
    QLocalServer server_;
    std::unique_ptr<QLockFile> lock_;
    QString path_;
    std::function<void()> activate_;
};
} // namespace luna
