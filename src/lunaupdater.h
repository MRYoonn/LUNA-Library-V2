// SPDX-License-Identifier: MIT
#pragma once
// Local-only update coordinator. The companion PowerShell program performs
// verified release downloads and rollback AFTER the native PDF app has exited.
#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMainWindow>
#include <QMessageBox>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QSettings>
#include <QStringList>
#include <QTimer>
#include <QUuid>

namespace luna {
class UpdateController final : public QObject {
public:
    explicit UpdateController(QMainWindow& shell)
        : QObject(&shell), shell_(shell), action_(new QAction(QStringLiteral("업데이트 확인…"), this)),
          automatic_(new QAction(QStringLiteral("자동 업데이트 확인"), this)) {
        action_->setObjectName(QStringLiteral("LunaCheckUpdates"));
        automatic_->setObjectName(QStringLiteral("LunaAutoUpdateCheck"));
        automatic_->setCheckable(true);
        automatic_->setToolTip(QStringLiteral("공개된 정식 버전을 하루 한 번 자동 확인합니다. 다운로드와 설치는 동의 후에만 진행합니다."));
        automatic_->setChecked(QSettings(QStringLiteral("LUNA"), QStringLiteral("LUNA Library"))
            .value(QStringLiteral("updater/autoCheckEnabled"), true).toBool());
        QObject::connect(automatic_, &QAction::toggled, this, [this](bool enabled) {
            QSettings settings(QStringLiteral("LUNA"), QStringLiteral("LUNA Library"));
            settings.setValue(QStringLiteral("updater/autoCheckEnabled"), enabled);
            settings.sync();
            if (settings.status() != QSettings::NoError) {
                const QSignalBlocker block(automatic_);
                automatic_->setChecked(!enabled);
                QMessageBox::warning(&shell_, QStringLiteral("자동 업데이트 설정 오류"),
                    QStringLiteral("자동 업데이트 확인 설정을 저장하지 못했습니다."));
            }
        });
        QObject::connect(action_, &QAction::triggered, this, [this] { check(true); });
        QObject::connect(qApp, &QCoreApplication::aboutToQuit, this, [this] {
            if (!installOnQuit_ || !validToken(stageToken_)) return;
            // Download and integrity verification have ALREADY finished.
            // Only the replacement runs after every LUNA window closes.
            const QString helper = QDir::temp().filePath(
                QStringLiteral("LUNA-Update-%1.ps1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
            if (!QFile::copy(script(), helper)) return;
            const QStringList args = {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
                QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"), QStringLiteral("-WindowStyle"),
                QStringLiteral("Hidden"), QStringLiteral("-File"), helper, QStringLiteral("-Mode"),
                QStringLiteral("Apply"), QStringLiteral("-InstallDir"), installDir(),
                QStringLiteral("-WaitPid"), QString::number(QCoreApplication::applicationPid()),
                QStringLiteral("-Token"), stageToken_};
            QProcess::startDetached(QStringLiteral("powershell.exe"), args, installDir());
        });
        // A verified update becomes committed only when the replacement
        // native shell actually appears. No user PDF or settings are touched.
        const QString ackToken = qEnvironmentVariable("LUNA_UPDATE_ACK_TOKEN");
        if (validToken(ackToken)) {
            QTimer::singleShot(5000, this, [this, ackToken] {
                if (!shell_.isVisible() || !shell_.centralWidget()) return;
                const QDir dir(QDir::temp().filePath(QStringLiteral("LUNA-Update-Stage-") + ackToken));
                QSaveFile ready(dir.filePath(QStringLiteral("startup.ok")));
                if (ready.open(QIODevice::WriteOnly)) {
                    ready.write(ackToken.toUtf8());
                    ready.commit();
                }
            });
        }
    }
    QAction* action() const { return action_; }
    QAction* automaticAction() const { return automatic_; }
    void check(bool explicitRequest) {
        if (!explicitRequest && !automatic_->isChecked()) return;
#ifndef Q_OS_WIN
        if (explicitRequest)
            QMessageBox::information(&shell_, QStringLiteral("LUNA 업데이트"), QStringLiteral("Windows Portable에서만 지원합니다."));
        return;
#else
        if (qEnvironmentVariableIsSet("LUNA_DISABLE_UPDATES") || qEnvironmentVariableIsSet("LUNA_UI_AUDIT") ||
            !QFileInfo::exists(script()) || checking_ || staging_) return;
        if (!explicitRequest) {
            QSettings settings(QStringLiteral("LUNA"), QStringLiteral("LUNA Library"));
            const QDateTime last = settings.value(QStringLiteral("updater/lastCheckedUtc")).toDateTime();
            if (last.isValid() && last.secsTo(QDateTime::currentDateTimeUtc()) < 86400) return;
        }
        checking_ = true;
        auto* process = new QProcess(this);
        process->setProgram(QStringLiteral("powershell.exe"));
        process->setArguments({QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
            QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"), QStringLiteral("-WindowStyle"),
            QStringLiteral("Hidden"), QStringLiteral("-File"), script(), QStringLiteral("-Mode"),
            QStringLiteral("Check"), QStringLiteral("-InstallDir"), installDir()});
        process->setProcessChannelMode(QProcess::SeparateChannels);
        QObject::connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                         [this, process, explicitRequest](int exitCode, QProcess::ExitStatus status) {
            checking_ = false;
            const QByteArray raw = process->readAllStandardOutput().trimmed();
            process->deleteLater();
            QJsonParseError error;
            const QJsonDocument json = QJsonDocument::fromJson(raw, &error);
            if (status != QProcess::NormalExit || exitCode != 0 || !json.isObject() ||
                json.object().value(QStringLiteral("status")).toString() != QStringLiteral("ok")) {
                if (explicitRequest)
                    QMessageBox::information(&shell_, QStringLiteral("업데이트 확인"),
                        QStringLiteral("현재 확인할 수 있는 공개 Release가 없습니다. 인터넷 연결과 릴리스 상태를 확인해 주세요."));
                return;
            }
            const QJsonObject value = json.object();
            // An offline/invalid check must not suppress retries for a day.
            if (!explicitRequest) {
                QSettings settings(QStringLiteral("LUNA"), QStringLiteral("LUNA Library"));
                settings.setValue(QStringLiteral("updater/lastCheckedUtc"), QDateTime::currentDateTimeUtc());
            }
            if (!value.value(QStringLiteral("available")).toBool()) {
                if (explicitRequest)
                    QMessageBox::information(&shell_, QStringLiteral("업데이트 확인"),
                        QStringLiteral("이미 최신 공개 버전이거나 더 새로운 개발 빌드를 사용 중입니다."));
                return;
            }
            const QString tag = value.value(QStringLiteral("tag")).toString();
            if (tag.isEmpty() || QApplication::activeModalWidget()) return;
            if (QMessageBox::question(&shell_, QStringLiteral("LUNA 업데이트"),
                QStringLiteral("새 정식 LUNA 버전 %1이 있습니다.\n"
                               "사용 중에 다운로드와 검증을 먼저 진행할까요?\n"
                               "PDF와 메모는 변경하지 않습니다.")
                               .arg(tag), QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
                != QMessageBox::Yes) return;
            stage(tag);
        });
        QObject::connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError) {
            checking_ = false;
            process->deleteLater();
        });
        process->start();
#endif
    }
private:
    static bool validToken(const QString& token) {
        return QRegularExpression(QStringLiteral("^[a-f0-9]{32}$")).match(token).hasMatch();
    }
    void cancelStage(const QString& token) {
        if (!validToken(token) || !QFileInfo::exists(script())) return;
        QProcess::startDetached(QStringLiteral("powershell.exe"),
            {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
             QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"), QStringLiteral("-WindowStyle"),
             QStringLiteral("Hidden"), QStringLiteral("-File"), script(), QStringLiteral("-Mode"),
             QStringLiteral("Cancel"), QStringLiteral("-InstallDir"), installDir(),
             QStringLiteral("-Token"), token}, installDir());
    }
    void stage(const QString& expectedTag) {
        if (staging_) return;
        staging_ = true;
        auto* process = new QProcess(this);
        process->setProgram(QStringLiteral("powershell.exe"));
        process->setArguments({QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
            QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"), QStringLiteral("-WindowStyle"),
            QStringLiteral("Hidden"), QStringLiteral("-File"), script(), QStringLiteral("-Mode"),
            QStringLiteral("Stage"), QStringLiteral("-InstallDir"), installDir()});
        process->setProcessChannelMode(QProcess::SeparateChannels);
        QObject::connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                         [this, process, expectedTag](int exitCode, QProcess::ExitStatus status) {
            staging_ = false;
            const QByteArray raw = process->readAllStandardOutput().trimmed();
            process->deleteLater();
            QJsonParseError error;
            const QJsonDocument document = QJsonDocument::fromJson(raw, &error);
            const QJsonObject value = document.isObject() ? document.object() : QJsonObject();
            const QString token = value.value(QStringLiteral("token")).toString();
            if (status != QProcess::NormalExit || exitCode != 0 ||
                value.value(QStringLiteral("status")).toString() != QStringLiteral("staged") ||
                value.value(QStringLiteral("tag")).toString() != expectedTag || !validToken(token)) {
                QMessageBox::warning(&shell_, QStringLiteral("LUNA 업데이트"),
                    QStringLiteral("새 버전 다운로드 또는 검증에 실패했습니다.\n"
                                   "기존 프로그램과 사용자 데이터는 그대로 유지됩니다."));
                return;
            }
            if (QMessageBox::question(&shell_, QStringLiteral("LUNA 업데이트 준비 완료"),
                    QStringLiteral("%1 다운로드 및 파일 검증이 완료됐습니다.\n"
                                   "열려 있는 PDF 저장을 확인하고 LUNA를 다시 시작할까요?")
                        .arg(expectedTag), QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
                    != QMessageBox::Yes) {
                cancelStage(token);
                return;
            }
            stageToken_ = token;
            installOnQuit_ = true;
            shell_.close(); // Existing PDF unsaved-change prompts still apply.
            if (shell_.isVisible()) {
                installOnQuit_ = false;
                stageToken_.clear();
                cancelStage(token);
            }
        });
        QObject::connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError) {
            staging_ = false;
            process->deleteLater();
        });
        process->start();
    }
    static QString installDir() { return QCoreApplication::applicationDirPath(); }
    static QString script() { return QDir(installDir()).filePath(QStringLiteral("LUNA-Update.ps1")); }
    QMainWindow& shell_;
    QAction* action_;
    QAction* automatic_;
    bool checking_ = false;
    bool staging_ = false;
    bool installOnQuit_ = false;
    QString stageToken_;
};
} // namespace luna
