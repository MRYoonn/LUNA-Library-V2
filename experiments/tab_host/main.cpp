// SPDX-License-Identifier: MIT
// Experimental host only. Never linked into LUNA or the PDF4QT engine.
#define NOMINMAX
#include <windows.h>
#include <QApplication>
#include <QCloseEvent>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QProcess>
#include <QSaveFile>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>
#include <array>

struct Document {
    QProcess* process = nullptr;
    QWidget* page = nullptr;
    QWidget* surface = nullptr;
    QWindow* foreign = nullptr;
    HWND handle = nullptr;
    LONG_PTR style = 0;
    QString filename;
    bool attached = false;
};

class Host final : public QMainWindow {
public:
    Host(const QString& executable, const QStringList& paths, bool qt, const QString& report, bool editorProbe)
        : qt_(qt), editorProbe_(editorProbe), report_(report) {
        setWindowTitle(QStringLiteral("LUNA Library · 탭 연결 실험 (검증 미완료)"));
        resize(1366, 768);
        tabs_ = new QTabWidget(this);
        tabs_->setTabsClosable(true);
        setCentralWidget(tabs_);
        for (int i = 0; i != 2; ++i) {
            auto& d = documents_[i];
            d.filename = QFileInfo(paths[i]).fileName();
            d.page = new QWidget(tabs_);
            auto* layout = new QVBoxLayout(d.page);
            layout->setContentsMargins(0, 0, 0, 0);
            layout->addWidget(new QLabel(QStringLiteral("독립 PDF4QT 프로세스 연결 중…"), d.page));
            tabs_->addTab(d.page, d.filename);
            tabs_->setTabToolTip(i, paths[i]);
            // Deliberately not parented: host failure must not invoke the
            // QProcess destructor and kill a document with unsaved edits.
            d.process = new QProcess;
            d.process->setProcessChannelMode(QProcess::ForwardedChannels);
            d.process->setWorkingDirectory(QFileInfo(executable).absolutePath());
            connect(d.process, &QProcess::finished, this, [this, i](int code, QProcess::ExitStatus status) {
                auto& doc = documents_[i];
                doc.handle = nullptr;
                tabs_->setTabText(i, doc.filename + (status == QProcess::NormalExit && code == 0
                    ? QStringLiteral(" · 닫힘") : QStringLiteral(" · 프로세스 오류")));
                // The host and the other document stay alive. No implicit reopen.
            });
            QStringList arguments{paths[i]};
            if (editorProbe_ && i == 0)
                arguments = {QStringLiteral("--luna-smoke-report"), report_ + QStringLiteral(".editor"), paths[i]};
            d.process->start(executable, arguments);
        }
        connect(tabs_, &QTabWidget::currentChanged, this, [this] { synchronize(); });
        connect(tabs_, &QTabWidget::tabCloseRequested, this, [this](int index) {
            auto& d = documents_[index];
            if (d.handle) PostMessageW(d.handle, WM_CLOSE, 0, 0);
            // Retain the tab until native close/save confirmation finishes.
        });
        timer_.setInterval(100);
        connect(&timer_, &QTimer::timeout, this, [this] { tick(); });
        timer_.start();
    }
protected:
    void closeEvent(QCloseEvent* event) override {
        for (const auto& d : documents_) {
            if (d.process->state() != QProcess::NotRunning) {
                event->ignore();
                QMessageBox::information(this, QStringLiteral("문서 닫기 확인"),
                    QStringLiteral("실험 창을 닫기 전에 각 문서 탭의 ×로 저장·닫기를 완료하세요. 프로세스를 강제 종료하지 않습니다."));
                return;
            }
        }
        event->accept();
    }
private:
    bool attach(Document& d) {
        struct Search { DWORD pid; HWND found; QString name; } search{DWORD(d.process->processId()), nullptr, d.filename};
        EnumWindows([](HWND window, LPARAM parameter) -> BOOL {
            auto* s = reinterpret_cast<Search*>(parameter);
            DWORD pid = 0; GetWindowThreadProcessId(window, &pid);
            wchar_t title[1024]{}; GetWindowTextW(window, title, 1024);
            if (pid == s->pid && IsWindowVisible(window) && IsWindowEnabled(window) &&
                QString::fromWCharArray(title).contains(s->name)) {
                s->found = window; return FALSE;
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&search));
        if (!search.found) return false;
        d.handle = search.found;
        d.style = GetWindowLongPtrW(d.handle, GWL_STYLE);
        auto* layout = qobject_cast<QVBoxLayout*>(d.page->layout());
        if (auto* placeholder = layout->takeAt(0)) { delete placeholder->widget(); delete placeholder; }
        if (qt_) {
            d.foreign = QWindow::fromWinId(WId(d.handle));
            if (!d.foreign) return false;
            d.surface = QWidget::createWindowContainer(d.foreign, d.page);
            d.surface->setFocusPolicy(Qt::StrongFocus);
            layout->addWidget(d.surface);
        } else {
            d.surface = new QWidget(d.page);
            d.surface->setAttribute(Qt::WA_NativeWindow);
            layout->addWidget(d.surface);
            const HWND parent = reinterpret_cast<HWND>(d.surface->winId());
            SetWindowLongPtrW(d.handle, GWL_STYLE, (d.style & ~(WS_POPUP | WS_CAPTION | WS_THICKFRAME)) | WS_CHILD);
            SetLastError(0);
            SetParent(d.handle, parent);
            if (GetParent(d.handle) != parent) {
                SetWindowLongPtrW(d.handle, GWL_STYLE, d.style);
                d.handle = nullptr; return false;
            }
        }
        d.attached = true;
        return true;
    }
    void synchronize() {
        for (int i = 0; i != 2; ++i) {
            auto& d = documents_[i];
            if (!d.attached || !d.handle || !IsWindow(d.handle)) continue;
            const bool active = tabs_->currentIndex() == i;
            ShowWindow(d.handle, active ? SW_SHOW : SW_HIDE);
            if (!qt_) {
                RECT rect{};
                GetClientRect(reinterpret_cast<HWND>(d.surface->winId()), &rect);
                SetWindowPos(d.handle, nullptr, 0, 0, rect.right, rect.bottom,
                    SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
            }
        }
    }
    void finish(bool basic, const QString& detail) {
        QJsonObject result{{"method", qt_ ? "qt-container" : "win32-parent"},
            {"embeddingLifecycle", basic ? "passed" : "failed"}, {"detail", detail},
            {"pocAccepted", false}, {"childCrashIsolation", basic && !editorProbe_ ? "passed" : "unverified"},
            {"nativeEditorRoundTrip", basic && editorProbe_ ? "passed" : "unverified"},
            {"annotationIsolation", "unverified"},
            {"keyboardFocusAndDialogs", "unverified"}, {"physicalDpi", "unverified"}};
        grab().save(report_ + QStringLiteral(".final.png"));
        QSaveFile file(report_);
        if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(result).toJson()) < 0 || !file.commit()) {
            qCritical("Cannot write experimental evidence");
        }
        // Automatic mode is restricted by its Python driver to temporary,
        // unmodified synthetic PDFs. On failure detach surviving windows;
        // the driver owns cleanup, never arbitrary user document processes.
        for (auto& d : documents_) {
            if (d.handle && IsWindow(d.handle)) {
                if (d.foreign) d.foreign->setParent(nullptr);
                SetParent(d.handle, nullptr);
                SetWindowLongPtrW(d.handle, GWL_STYLE, d.style);
                ShowWindow(d.handle, SW_SHOW);
            }
        }
        timer_.stop();
        if (basic) QApplication::exit(0);
        // On automatic failure remain alive so the synthetic driver can
        // clean up this exact process tree, including hidden children.
    }
    void tick() {
        ++ticks_;
        for (auto& d : documents_) if (!d.attached && d.process->state() == QProcess::Running) attach(d);
        synchronize();
        if (report_.isEmpty()) return; // Manual exploration never auto-closes documents.
        if (ticks_ > (editorProbe_ ? 1500 : 600)) { finish(false, QStringLiteral("Timed out; inspect live window/dialog evidence")); return; }
        if (!documents_[0].attached || !documents_[1].attached) return;
        if (editorProbe_) {
            // Keep A foreground while its existing native Editor regression
            // runs in the embedded process; B must remain alive and untouched.
            if (phase_ == 0 && documents_[0].process->state() == QProcess::NotRunning) {
                QFile marker(report_ + QStringLiteral(".editor"));
                if (documents_[0].process->exitStatus() != QProcess::NormalExit ||
                    documents_[0].process->exitCode() != 0 || !marker.open(QIODevice::ReadOnly) ||
                    marker.readAll() != QByteArray("LUNA_SMOKE_EDITOR_ROUNDTRIP_OK\n") ||
                    documents_[1].process->state() != QProcess::Running) {
                    finish(false, QStringLiteral("Embedded native Editor regression failed; inspect .editor marker and log")); return;
                }
                tabs_->setCurrentIndex(1);
                PostMessageW(documents_[1].handle, WM_CLOSE, 0, 0);
                phase_ = 1;
            } else if (phase_ == 1 && documents_[1].process->state() == QProcess::NotRunning) {
                finish(documents_[1].process->exitStatus() == QProcess::NormalExit && documents_[1].process->exitCode() == 0,
                    QStringLiteral("Existing native Editor regression ran inside A while B remained independent; actual Win32 input and native-dialog gates still required"));
            }
            return;
        }
        if (phase_ < 20) {
            if (++settle_ % 5) return;
            tabs_->setCurrentIndex(phase_ % 2);
            resize(phase_ % 2 ? QSize(1366, 768) : QSize(1200, 720));
            for (auto& d : documents_) if (!d.handle || !IsWindow(d.handle)) {
                finish(false, QStringLiteral("Document exited during tab switch")); return;
            }
            ++phase_;
        } else if (phase_ == 20) {
            grab().save(report_ + QStringLiteral(".png"));
            // Only read-only fixtures are used by the automated driver.
            documents_[0].process->kill(); // Intentional crash of driver-owned synthetic A.
            ++phase_;
        } else if (phase_ == 21 && documents_[0].process->state() == QProcess::NotRunning) {
            if (documents_[0].process->exitStatus() != QProcess::CrashExit || documents_[1].process->state() != QProcess::Running) {
                finish(false, QStringLiteral("Child crash affected another document or did not terminate as expected")); return;
            }
            tabs_->setCurrentIndex(1);
            PostMessageW(documents_[1].handle, WM_CLOSE, 0, 0);
            ++phase_;
        } else if (phase_ == 22 && documents_[1].process->state() == QProcess::NotRunning) {
            finish(documents_[1].process->exitStatus() == QProcess::NormalExit && documents_[1].process->exitCode() == 0, QStringLiteral("Two document processes embedded, resized and switched; synthetic A crash isolated and B closed normally; interaction gates still required"));
        }
    }
    std::array<Document, 2> documents_;
    QTabWidget* tabs_ = nullptr;
    QTimer timer_;
    bool qt_;
    bool editorProbe_;
    QString report_;
    int ticks_ = 0, phase_ = 0, settle_ = 0;
};

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({"method", "Embedding method: qt or win32", "method", "qt"});
    parser.addOption({"editor-probe", "Run existing native Editor regression in embedded synthetic A"});
    parser.addOption({"report", "Synthetic-only automatic lifecycle evidence (not POC acceptance)", "path"});
    parser.addPositionalArgument("executable", "LUNA.exe");
    parser.addPositionalArgument("pdfs", "Exactly two distinct PDF files", "a.pdf b.pdf");
    parser.process(app);
    if (parser.isSet("editor-probe") && !parser.isSet("report")) return 1;
    const auto args = parser.positionalArguments();
    if (args.size() != 3 || (parser.value("method") != "qt" && parser.value("method") != "win32")) return 1;
    for (const auto& path : args) if (!QFileInfo(path).isFile()) return 1;
    if (QFileInfo(args[1]).canonicalFilePath().compare(QFileInfo(args[2]).canonicalFilePath(), Qt::CaseInsensitive) == 0) return 1;
    Host host(QFileInfo(args[0]).absoluteFilePath(), {QFileInfo(args[1]).absoluteFilePath(), QFileInfo(args[2]).absoluteFilePath()},
        parser.value("method") == "qt", parser.value("report"), parser.isSet("editor-probe"));
    host.show();
    return app.exec();
}
