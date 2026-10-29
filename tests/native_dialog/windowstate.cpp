// SPDX-License-Identifier: MIT
// Disposable reproduction of legacy window-state restoration across nested windows.
#include <QApplication>
#include <QDockWidget>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLayout>
#include <QMainWindow>
#include <QTabWidget>
#include <QToolBar>
#include <QSettings>
#include <QDebug>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    QMainWindow legacy;
    auto* original = new QToolBar(&legacy);
    original->setObjectName(QStringLiteral("mainToolBar"));
    legacy.addToolBar(original);
    const QByteArray state = legacy.saveState();
    if (app.arguments().size() == 2 && app.arguments().at(1) == "--verify-legacy") {
        QSettings settings(QSettings::NativeFormat, QSettings::UserScope, QStringLiteral("LUNA"), QStringLiteral("LUNA Library"));
        if (settings.value(QStringLiteral("window/state")).toByteArray() != state) return 6;
        qInfo() << "Verified actual native QSettings QByteArray decoding of seeded legacy layout";
        return 0;
    }
    if (app.arguments().size() == 3 && app.arguments().at(1) == "--export-legacy") {
        QFile file(app.arguments().at(2));
        if (!file.open(QIODevice::WriteOnly)) return 2;
        file.write(QJsonDocument(QJsonObject{{"stateBase64", QString::fromLatin1(state.toBase64())},
                                           {"qt", QString::fromLatin1(qVersion())}}).toJson());
        return 0;
    }
    const bool unsafe = app.arguments().contains("--unsafe");
    QMainWindow shell;
    // Match the production ownership boundary: safe sessions join the shell
    // only after shell.restoreState. --unsafe preserves the pre-fix order.
    auto* tabs = new QTabWidget(unsafe ? &shell : nullptr);
    if (unsafe) shell.setCentralWidget(tabs);
    auto* editor = new QMainWindow(tabs);
    auto* bar = new QToolBar(editor); bar->setObjectName(QStringLiteral("mainToolBar"));
    editor->addToolBar(bar); bar->addAction(QStringLiteral("Synthetic PDF action"));
    tabs->addTab(editor, QStringLiteral("A"));
    if (!shell.restoreState(state)) return 3;
    const bool adopted = shell.toolBarArea(bar) != Qt::NoToolBarArea;
    qInfo() << "qt" << qVersion() << "legacyStateAdoptedNestedToolbar" << adopted
            << "toolbarParentStillEditor" << (bar->parentWidget() == editor)
            << "editorStillOwnsLayoutItem" << (editor->toolBarArea(bar) != Qt::NoToolBarArea);
    if (!unsafe && adopted) return 4;
    if (!unsafe) shell.setCentralWidget(tabs);
    if (!unsafe && (shell.toolBarArea(bar) != Qt::NoToolBarArea || editor->toolBarArea(bar) != Qt::TopToolBarArea)) return 5;
    shell.show(); app.processEvents();
    tabs->removeTab(0); editor->deleteLater();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    qInfo() << "editor deleted; dispatching posted shell layout";
    shell.layout()->invalidate(); app.processEvents();
    qInfo() << "shell minimum" << shell.layout()->totalMinimumSize();
    return 0;
}
