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
#include <QSettings>
#include <QStringList>
#include <QTimer>
#include <QUuid>

namespace luna {
class UpdateController final : public QObject {
public:
    explicit UpdateController(QMainWindow& shell)
        : QObject(&shell), shell_(shell), action_(new QAction(QStringLiteral("업데이트 확인…"), this)) {
        action_->setObjectName(QStringLiteral("LunaCheckUpdates"));
        QObject::connect(action_, &QAction::triggered, this, [this] { check(true); });
        QObject::connect(qApp, &QCoreApplication::aboutToQuit, this, [this] {
            if (!installOnQuit_) return;
            // Running a file from inside the install tree while replacing that
            // same tree is fragile on Windows; copy our reviewed helper to Temp.
            const QString staged = QDir::temp().filePath(
                QStringLiteral("LUNA-Update-%1.ps1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
            if (!QFile::copy(script(), staged)) return;
            QStringList args = QStringList{QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
                QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"), QStringLiteral("-WindowStyle"),
                QStringLiteral("Hidden"), QStringLiteral("-File"), staged, QStringLiteral("-Mode"),
                QStringLiteral("Apply"), QStringLiteral("-InstallDir"), installDir(),
                QStringLiteral("-WaitPid"), QString::number(QCoreApplication::applicationPid())};
            QProcess::startDetached(QStringLiteral("powershell.exe"), args, installDir());
        });
    }
    QAction* action() const { return action_; }
    void check(bool explicitRequest) {
#ifndef Q_OS_WIN
        if (explicitRequest)
            QMessageBox::information(&shell_, QStringLiteral("LUNA 업데이트"), QStringLiteral("Windows Portable에서만 지원합니다."));
        return;
#else
        if (qEnvironmentVariableIsSet("LUNA_DISABLE_UPDATES") || qEnvironmentVariableIsSet("LUNA_UI_AUDIT") ||
            !QFileInfo::exists(script()) || checking_) return;
        if (!explicitRequest) {
            QSettings settings(QStringLiteral("LUNA"), QStringLiteral("LUNA Library"));
            const QDateTime last = settings.value(QStringLiteral("updater/lastCheckedUtc")).toDateTime();
            if (last.isValid() && last.secsTo(QDateTime::currentDateTimeUtc()) < 86400) return;
            settings.setValue(QStringLiteral("updater/lastCheckedUtc"), QDateTime::currentDateTimeUtc());
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
            if (!value.value(QStringLiteral("available")).toBool()) {
                if (explicitRequest)
                    QMessageBox::information(&shell_, QStringLiteral("업데이트 확인"),
                        QStringLiteral("이미 최신 공개 버전이거나 더 새로운 개발 빌드를 사용 중입니다."));
                return;
            }
            const QString tag = value.value(QStringLiteral("tag")).toString();
            if (tag.isEmpty() || QApplication::activeModalWidget()) return;
            if (QMessageBox::question(&shell_, QStringLiteral("LUNA 업데이트"),
                QStringLiteral("새로운 정식 LUNA 버전 %1이 있습니다.\n"
                               "지금 업데이트할까요?\n"
                               "수정한 PDF는 종료 전에 저장할 수 있으며, 사용자 PDF·메모·설정은 유지합니다.")
                               .arg(tag), QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
                != QMessageBox::Yes) return;
            installOnQuit_ = true;
            shell_.close();  // NativeTabs requests save/decline for every dirty PDF.
            if (shell_.isVisible()) installOnQuit_ = false; // User cancelled close.
        });
        QObject::connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError) {
            checking_ = false;
            process->deleteLater();
        });
        process->start();
#endif
    }
private:
    static QString installDir() { return QCoreApplication::applicationDirPath(); }
    static QString script() { return QDir(installDir()).filePath(QStringLiteral("LUNA-Update.ps1")); }
    QMainWindow& shell_;
    QAction* action_;
    bool checking_ = false;
    bool installOnQuit_ = false;
};
} // namespace luna
