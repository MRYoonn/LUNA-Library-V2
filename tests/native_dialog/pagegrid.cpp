// SPDX-License-Identifier: MIT
// Real Qt layout test. Fixture tiles test geometry, not PDF rendering.
#include "lunapagegrid.h"
#include <QApplication>
#include <QEventLoop>
#include <QStandardItemModel>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QDebug>

static bool waitFor(const std::function<bool()>& ready) {
    QEventLoop loop;
    QTimer poll, timeout;
    bool passed = false;
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (ready()) { passed = true; loop.quit(); }
    });
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    poll.start(10); timeout.setSingleShot(true); timeout.start(2000); loop.exec();
    return passed;
}
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QWidget window;
    auto* layout = new QVBoxLayout(&window);
    auto* stack = new QStackedWidget(&window);
    layout->addWidget(stack);
    stack->addWidget(new QWidget(stack));
    QStandardItemModel model(3, 1);
    QPixmap image(120, 90); image.fill(Qt::white);
    for (int i = 0; i < 3; ++i) {
        model.setData(model.index(i, 0), QString::number(i + 1));
        model.setData(model.index(i, 0), image, Qt::DecorationRole);
    }
    QListView native;
    native.setModel(&model);
    int requestedSize = 0;
    auto* grid = new luna::PageGrid(native, [&](int size) { requestedSize = size; }, stack);
    stack->addWidget(grid);
    window.resize(640, 500); window.show();
    for (int width : {328, 300, 600, 325}) {
        stack->setCurrentIndex(0);
        window.resize(width, 500);
        QCoreApplication::processEvents();
        stack->setCurrentIndex(1);
        if (!waitFor([&] {
            const QRect a = grid->visualRect(model.index(0, 0));
            const QRect b = grid->visualRect(model.index(1, 0));
            const QRect c = grid->visualRect(model.index(2, 0));
            return a.isValid() && a.top() == b.top() && a.right() < b.left() &&
                c.top() > a.bottom() && b.right() < grid->viewport()->width() && requestedSize > 0;
        })) { qCritical() << "Two columns failed" << width << grid->viewport()->size() << grid->gridSize(); return 1; }
    }
    qInfo("PASS: hidden-page activation and resizing retain two columns");
    return 0;
}
