// SPDX-License-Identifier: MIT
// LUNA Library V2 UI overlay. PDF rendering remains upstream PDF4QT.
#include "lunalibrary.h"
#include "pdfviewermainwindow.h"
#include "pdfprogramcontroller.h"

#include <QAbstractItemView>
#include <QAction>
#include <QButtonGroup>
#include <QDir>
#include <QDirIterator>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStyle>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>
#include <memory>

namespace luna {
namespace {

constexpr int kScanBatch = 90;
constexpr int kMaximumEntries = 15000;

class LibraryPanel final : public QWidget {
public:
    explicit LibraryPanel(pdfviewer::PDFViewerMainWindow* viewer, QWidget* parent)
        : QWidget(parent), viewer_(viewer)
    {
        setObjectName(QStringLiteral("LunaLibraryPanel"));
        setMinimumWidth(250);
        setStyleSheet(QStringLiteral(R"STYLE(
            QWidget#LunaLibraryPanel { background:#F8FAFC; color:#1D2B40; }
            QLabel#LunaBrand { font-size:19px; font-weight:700; color:#14233D; }
            QLabel#LunaSection { font-size:12px; font-weight:600; color:#64748B; }
            QPushButton#LunaTab { background:#E8EDF4; border:0; border-radius:7px;
                padding:9px; color:#475569; font-weight:600; }
            QPushButton#LunaTab:checked { background:#3B82F6; color:white; }
            QToolButton#LunaTool { border:0; background:transparent; border-radius:6px;
                padding:6px 7px; color:#334155; }
            QToolButton#LunaTool:hover { background:#DFEAFE; }
            QLineEdit#LunaSearch { border:1px solid #CBD5E1; border-radius:6px;
                background:white; padding:7px; }
            QTreeView, QListWidget { background:transparent; border:none;
                outline:0; color:#1E293B; font-size:12px; }
            QTreeView::item, QListWidget::item { padding:5px; }
            QTreeView::item:selected, QListWidget::item:selected { background:#DBEAFE; color:#1D4ED8; }
        )STYLE"));

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(11, 13, 11, 10);
        layout->setSpacing(8);

        auto* header = new QHBoxLayout;
        auto* logo = new QLabel(QStringLiteral("☾  LUNA Library"), this);
        logo->setObjectName(QStringLiteral("LunaBrand"));
        header->addWidget(logo, 1);
        layout->addLayout(header);

        auto* tools = new QHBoxLayout;
        tools->setSpacing(2);
        auto button = [this, tools](const QString& text, QStyle::StandardPixmap icon) {
            auto* control = new QToolButton(this);
            control->setObjectName(QStringLiteral("LunaTool"));
            control->setIcon(style()->standardIcon(icon));
            control->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            control->setText(text);
            tools->addWidget(control);
            return control;
        };
        auto* addFile = button(QStringLiteral("추가"), QStyle::SP_FileIcon);
        auto* addFolder = button(QStringLiteral("새 폴더"), QStyle::SP_DirIcon);
        auto* find = button(QStringLiteral("검색"), QStyle::SP_FileDialogContentsView);
        auto* location = button(QStringLiteral("위치"), QStyle::SP_DirOpenIcon);
        layout->addLayout(tools);

        query_ = new QLineEdit(this);
        query_->setObjectName(QStringLiteral("LunaSearch"));
        query_->setPlaceholderText(QStringLiteral("PDF 파일명 검색…"));
        query_->setClearButtonEnabled(true);
        query_->hide();
        layout->addWidget(query_);

        auto* switcher = new QHBoxLayout;
        switcher->setSpacing(4);
        folderTab_ = new QPushButton(QStringLiteral("폴더"), this);
        pdfTab_ = new QPushButton(QStringLiteral("PDF"), this);
        auto* group = new QButtonGroup(this);
        group->setExclusive(true);
        for (auto* tab : {folderTab_, pdfTab_}) {
            tab->setObjectName(QStringLiteral("LunaTab"));
            tab->setCheckable(true);
            group->addButton(tab);
            switcher->addWidget(tab);
        }
        folderTab_->setChecked(true);
        layout->addLayout(switcher);

        pages_ = new QStackedWidget(this);
        layout->addWidget(pages_, 1);

        auto* folderPage = new QWidget(this);
        auto* folderLayout = new QVBoxLayout(folderPage);
        folderLayout->setContentsMargins(0, 0, 0, 0);
        folderLayout->setSpacing(5);
        auto* folderHeading = new QLabel(QStringLiteral("내 라이브러리"), folderPage);
        folderHeading->setObjectName(QStringLiteral("LunaSection"));
        folderLayout->addWidget(folderHeading);

        auto* vertical = new QSplitter(Qt::Vertical, folderPage);
        vertical->setChildrenCollapsible(false);
        model_ = new QFileSystemModel(this);
        model_->setReadOnly(true);
        model_->setFilter(QDir::AllDirs | QDir::NoDotAndDotDot);
        tree_ = new QTreeView(vertical);
        tree_->setModel(model_);
        tree_->setHeaderHidden(true);
        tree_->setAnimated(true);
        tree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        for (int i = 1; i < 4; ++i) tree_->hideColumn(i);
        vertical->addWidget(tree_);

        auto* documents = new QWidget(vertical);
        auto* docsLayout = new QVBoxLayout(documents);
        docsLayout->setContentsMargins(0, 7, 0, 0);
        docsLayout->setSpacing(4);
        folderCount_ = new QLabel(QStringLiteral("PDF 문서"), documents);
        folderCount_->setObjectName(QStringLiteral("LunaSection"));
        docsLayout->addWidget(folderCount_);
        folderPdfs_ = makePdfList(documents);
        docsLayout->addWidget(folderPdfs_, 1);
        vertical->addWidget(documents);
        vertical->setSizes({310, 350});
        folderLayout->addWidget(vertical, 1);
        pages_->addWidget(folderPage);

        auto* allPage = new QWidget(this);
        auto* allLayout = new QVBoxLayout(allPage);
        allLayout->setContentsMargins(0, 0, 0, 0);
        allCount_ = new QLabel(QStringLiteral("전체 PDF"), allPage);
        allCount_->setObjectName(QStringLiteral("LunaSection"));
        allLayout->addWidget(allCount_);
        allPdfs_ = makePdfList(allPage);
        allLayout->addWidget(allPdfs_, 1);
        pages_->addWidget(allPage);

        folderTimer_ = new QTimer(this);
        allTimer_ = new QTimer(this);
        folderTimer_->setInterval(0);
        allTimer_->setInterval(0);
        connect(folderTimer_, &QTimer::timeout, this, [this] { scanBatch(false); });
        connect(allTimer_, &QTimer::timeout, this, [this] { scanBatch(true); });

        connect(tree_, &QTreeView::clicked, this, [this](const QModelIndex& index) {
            const QString path = model_->filePath(index);
            if (QFileInfo(path).isDir()) selectFolder(path);
        });
        connect(folderPdfs_, &QListWidget::itemDoubleClicked, this,
                [this](QListWidgetItem* item) { openItem(item); });
        connect(allPdfs_, &QListWidget::itemDoubleClicked, this,
                [this](QListWidgetItem* item) { openItem(item); });

        connect(folderTab_, &QPushButton::clicked, this, [this] { pages_->setCurrentIndex(0); });
        connect(pdfTab_, &QPushButton::clicked, this, [this] {
            pages_->setCurrentIndex(1);
            if (!allScanned_) scanAll();
        });
        connect(query_, &QLineEdit::textChanged, this, [this](const QString& word) {
            filter(folderPdfs_, word); filter(allPdfs_, word);
        });
        connect(find, &QToolButton::clicked, this, [this] {
            query_->setVisible(!query_->isVisible());
            if (query_->isVisible()) query_->setFocus();
            else query_->clear();
        });
        connect(location, &QToolButton::clicked, this, [this] {
            const QString path = QFileDialog::getExistingDirectory(this,
                QStringLiteral("논문 폴더 선택"), root_);
            if (!path.isEmpty()) selectRoot(path);
        });
        connect(addFile, &QToolButton::clicked, this, [this] {
            const QString path = QFileDialog::getOpenFileName(
                this, QStringLiteral("PDF 열기"), current_, QStringLiteral("PDF (*.pdf *.PDF)"));
            if (!path.isEmpty()) openPdf(path);
        });
        connect(addFolder, &QToolButton::clicked, this, [this] { createFolder(); });

        QSettings settings(QStringLiteral("LUNA"), QStringLiteral("LUNA Library"));
        QString start = settings.value(QStringLiteral("library/root")).toString();
        if (!QFileInfo(start).isDir()) {
            start = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        }
        if (!QFileInfo(start).isDir()) start = QDir::homePath();
        selectRoot(start);
    }

private:
    QListWidget* makePdfList(QWidget* parent) {
        auto* list = new QListWidget(parent);
        list->setUniformItemSizes(true);
        list->setSelectionMode(QAbstractItemView::SingleSelection);
        list->setEditTriggers(QAbstractItemView::NoEditTriggers);
        list->setSortingEnabled(false);
        return list;
    }
    void filter(QListWidget* list, const QString& word) {
        for (int i = 0; i < list->count(); ++i) {
            QListWidgetItem* item = list->item(i);
            item->setHidden(!word.isEmpty() &&
                            !item->text().contains(word, Qt::CaseInsensitive));
        }
    }
    void selectRoot(const QString& path) {
        QFileInfo info(path);
        if (!info.isDir()) return;
        root_ = info.canonicalFilePath();
        if (root_.isEmpty()) return;
        model_->setRootPath(root_);
        tree_->setRootIndex(model_->index(root_));
        QSettings(QStringLiteral("LUNA"), QStringLiteral("LUNA Library"))
            .setValue(QStringLiteral("library/root"), root_);
        allTimer_->stop();
        allIterator_.reset();
        allPdfs_->clear();
        allScanned_ = false;
        allCount_->setText(QStringLiteral("전체 PDF"));
        selectFolder(root_);
        if (pages_->currentIndex() == 1) scanAll();
    }
    void selectFolder(const QString& path) {
        if (!QFileInfo(path).isDir()) return;
        current_ = path;
        folderTimer_->stop();
        folderIterator_.reset();
        folderPdfs_->clear();
        folderPdfs_->setSortingEnabled(false);
        folderCount_->setText(QStringLiteral("PDF 검색 중…"));
        folderIterator_ = std::make_unique<QDirIterator>(
            current_, QStringList{QStringLiteral("*.pdf"), QStringLiteral("*.PDF")},
            QDir::Files | QDir::Readable | QDir::NoDotAndDotDot);
        folderTimer_->start();
    }
    void scanAll() {
        allTimer_->stop();
        allIterator_.reset();
        allPdfs_->clear();
        allPdfs_->setSortingEnabled(false);
        allScanned_ = true;
        allIterator_ = std::make_unique<QDirIterator>(
            root_, QStringList{QStringLiteral("*.pdf"), QStringLiteral("*.PDF")},
            QDir::Files | QDir::Readable | QDir::NoDotAndDotDot,
            QDirIterator::Subdirectories);
        allCount_->setText(QStringLiteral("전체 PDF 검색 중…"));
        allTimer_->start();
    }
    void scanBatch(bool all) {
        auto& iterator = all ? allIterator_ : folderIterator_;
        QTimer* timer = all ? allTimer_ : folderTimer_;
        QListWidget* list = all ? allPdfs_ : folderPdfs_;
        QLabel* count = all ? allCount_ : folderCount_;
        if (!iterator) { timer->stop(); return; }
        int checked = 0;
        while (checked < kScanBatch &&
               list->count() < kMaximumEntries && iterator->hasNext()) {
            const QString path = iterator->next();
            ++checked;
            QFileInfo info(path);
            if (!info.isFile() || info.suffix().compare(
                    QStringLiteral("pdf"), Qt::CaseInsensitive) != 0) continue;
            auto* item = new QListWidgetItem(
                style()->standardIcon(QStyle::SP_FileIcon), info.completeBaseName(), list);
            item->setData(Qt::UserRole, path);
            item->setToolTip(all ? QDir(root_).relativeFilePath(path) : path);
            item->setHidden(!query_->text().isEmpty() &&
                             !item->text().contains(query_->text(), Qt::CaseInsensitive));
        }
        if (list->count() >= kMaximumEntries || !iterator->hasNext()) {
            timer->stop();
            iterator.reset();
            list->setSortingEnabled(true);
            list->sortItems(Qt::AscendingOrder);
            count->setText(QStringLiteral("%1 PDF · %2개")
                .arg(all ? QStringLiteral("전체") : QStringLiteral("현재 폴더"))
                .arg(list->count()));
        } else {
            count->setText(QStringLiteral("PDF 검색 중… %1개").arg(list->count()));
        }
    }
    void openItem(QListWidgetItem* item) {
        if (item) openPdf(item->data(Qt::UserRole).toString());
    }
    void openPdf(const QString& path) {
        const QFileInfo info(path);
        if (!info.isFile() ||
            info.suffix().compare(QStringLiteral("pdf"), Qt::CaseInsensitive) != 0) return;
        auto* controller = viewer_->getProgramController();
        // Upstream openDocument closes the previous document; never bypass save prompt.
        if (!controller->canClose()) {
            QMessageBox::information(this, QStringLiteral("PDF 여는 중"),
                QStringLiteral("현재 로딩 완료 후 다시 시도해 주세요."));
            return;
        }
        if (!controller->askForSaveDocumentBeforeClose()) return;
        controller->openDocument(info.absoluteFilePath());
    }
    void createFolder() {
        if (!QFileInfo(current_).isDir()) return;
        bool accepted = false;
        QString name = QInputDialog::getText(this, QStringLiteral("새 폴더"),
            QStringLiteral("폴더 이름"), QLineEdit::Normal, QString(), &accepted).trimmed();
        if (!accepted || name.isEmpty()) return;
        static const QRegularExpression invalid(
            QStringLiteral(R"REGEX([<>:"/\\|?*])REGEX"));
        static const QRegularExpression reserved(
            QStringLiteral(R"REGEX(^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\..*)?$)REGEX"),
            QRegularExpression::CaseInsensitiveOption);
        if (name == QStringLiteral(".") || name == QStringLiteral("..") ||
            name.endsWith(QLatin1Char('.')) || invalid.match(name).hasMatch() ||
            reserved.match(name).hasMatch()) {
            QMessageBox::warning(this, QStringLiteral("폴더 이름 오류"),
                QStringLiteral("Windows에서 허용되지 않는 폴더 이름입니다."));
            return;
        }
        if (!QDir(current_).mkdir(name)) {
            QMessageBox::warning(this, QStringLiteral("폴더 생성 실패"),
                QStringLiteral("폴더가 이미 존재하거나 쓰기 권한이 없습니다."));
        }
    }

    pdfviewer::PDFViewerMainWindow* viewer_;
    QFileSystemModel* model_ = nullptr;
    QTreeView* tree_ = nullptr;
    QStackedWidget* pages_ = nullptr;
    QListWidget* folderPdfs_ = nullptr;
    QListWidget* allPdfs_ = nullptr;
    QPushButton* folderTab_ = nullptr;
    QPushButton* pdfTab_ = nullptr;
    QLineEdit* query_ = nullptr;
    QLabel* folderCount_ = nullptr;
    QLabel* allCount_ = nullptr;
    QTimer* folderTimer_ = nullptr;
    QTimer* allTimer_ = nullptr;
    std::unique_ptr<QDirIterator> folderIterator_;
    std::unique_ptr<QDirIterator> allIterator_;
    QString root_;
    QString current_;
    bool allScanned_ = false;
};

} // namespace

void installLibrary(pdfviewer::PDFViewerMainWindow& window) {
    // Hide the old PDF4QT sidebar: LUNA must have precisely two visible columns.
    if (auto* old = window.findChild<QDockWidget*>(
        QStringLiteral("SidebarDockWidget"))) {
        old->hide();
        old->toggleViewAction()->setVisible(false);
        QObject::connect(old, &QDockWidget::visibilityChanged, &window,
                         [old](bool visible) { if (visible) old->hide(); });
    }
    auto* dock = new QDockWidget(QStringLiteral("LUNA Library"), &window);
    dock->setObjectName(QStringLiteral("LunaLibraryDock"));
    dock->setAllowedAreas(Qt::LeftDockWidgetArea);
    dock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    dock->setTitleBarWidget(new QWidget(dock));
    dock->setMinimumWidth(260);
    dock->setMaximumWidth(480);
    dock->setWidget(new LibraryPanel(&window, dock));
    window.addDockWidget(Qt::LeftDockWidgetArea, dock);
    dock->show();
    window.resizeDocks({dock}, {315}, Qt::Horizontal);

    if (auto* bar = window.findChild<QToolBar*>(QStringLiteral("mainToolBar"))) {
        bar->setMovable(false);
        bar->setIconSize(QSize(20, 20));
        for (const char* name : {"actionGoToDocumentStart", "actionGoToDocumentEnd",
                                 "actionFitPage", "actionFitHeight",
                                 "actionSelectTable", "actionMagnifier"}) {
            if (auto* action = window.findChild<QAction*>(QString::fromLatin1(name)))
                bar->removeAction(action);
        }
        for (const char* name : {"actionFind", "actionBookmarkPage"}) {
            if (auto* action = window.findChild<QAction*>(QString::fromLatin1(name)))
                bar->addAction(action);
        }
    }
    QObject::connect(&window, &QWidget::windowTitleChanged, &window,
        [&window](const QString& title) {
            if (!title.startsWith(QStringLiteral("LUNA Library")))
                window.setWindowTitle(QStringLiteral("LUNA Library · ") + title);
        });
    window.setWindowTitle(QStringLiteral("LUNA Library"));
    window.resize(1420, 880);
}

} // namespace luna
