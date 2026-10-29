// SPDX-License-Identifier: MIT
#pragma once
#include <QCoreApplication>
#include <QCache>
#include <QDateTime>
#include <QFileInfo>
#include <QHash>
#include <QImageReader>
#include <QPointer>
#include <QThreadPool>
#include <functional>

namespace luna {
// Bounded, lazy decoding shared by cards and hover previews. GUI callbacks
// are guarded by their owner; worker threads never access a widget.
class ThumbnailCache final : public QObject {
public:
    explicit ThumbnailCache(QObject* parent) : QObject(parent), images_(16 * 1024) { workers_.setMaxThreadCount(2); }
    ~ThumbnailCache() override { workers_.clear(); workers_.waitForDone(); }
    static ThumbnailCache* instance() {
        static QPointer<ThumbnailCache> cache;
        if (!cache) cache = new ThumbnailCache(qApp);
        return cache;
    }
    void invalidate() { ++generation_; images_.clear(); }
    QImage request(const QString& path, QObject* owner, std::function<void()> ready) {
        const QFileInfo file(path);
        if (!file.isFile() || file.isSymLink() || file.size() <= 0 || file.size() > 5'000'000) return {};
        const QString key = file.absoluteFilePath() + QStringLiteral("|%1|%2").arg(file.size()).arg(file.lastModified().toMSecsSinceEpoch());
        if (auto* image = images_.object(key)) return *image;
        if (!pending_.contains(key) && pending_.size() >= 32) return {};
        const bool started = pending_.contains(key);
        pending_[key].insert(owner, {QPointer<QObject>(owner), std::move(ready)});
        if (started) return {};
        const auto generation = generation_;
        workers_.start([this, key, path, generation] {
            QImageReader reader(path);
            reader.setAutoTransform(true);
            const QSize size = reader.size();
            QImage image;
            if (size.isValid() && size.width() <= 4096 && size.height() <= 4096) {
                reader.setScaledSize(size.scaled(QSize(470, 620), Qt::KeepAspectRatio));
                image = reader.read();
                if (!image.isNull()) image = image.scaled(QSize(470,620), Qt::KeepAspectRatio, Qt::SmoothTransformation);
            }
            QMetaObject::invokeMethod(this, [this, key, image, generation] {
                if (generation == generation_) images_.insert(key, new QImage(image), qMax(1, int(image.sizeInBytes() / 1024)));
                const auto callbacks = pending_.take(key);
                for (const auto& callback : callbacks) if (callback.owner) callback.ready();
            }, Qt::QueuedConnection);
        });
        return {};
    }
private:
    struct Callback { QPointer<QObject> owner; std::function<void()> ready; };
    quint64 generation_ = 0;
    QThreadPool workers_;
    QCache<QString,QImage> images_;
    QHash<QString,QHash<QObject*,Callback>> pending_;
};

} // namespace luna
