// SPDX-License-Identifier: MIT
// LUNA Library V2 UI overlay. PDF rendering remains upstream PDF4QT.
#include "lunalibrary.h"
#include "lunathumbnailcache.h"
#include "lunafilemove.h"
#include "lunadocumentclaim.h"
#include "lunareadertoolbar.h"
#include "pdfeditormainwindow.h"
#include "pdfprogramcontroller.h"
#include "pdfdrawwidget.h"
#include "pdfdrawspacecontroller.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QAbstractButton>
#include <QDateTime>
#include <QDebug>
#include <QCursor>
#include <QEvent>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QFile>
#include <QFontMetrics>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QProgressBar>
#include <QStorageInfo>
#include <QSet>
#include <QScrollArea>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QMainWindow>
#include <QKeySequence>
#include <QKeyEvent>
#include <QSizePolicy>
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
#include <QPointer>
#include <QProcess>
#include <QScopedValueRollback>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QSaveFile>
#include <QMouseEvent>
#include <functional>
#include <QImageReader>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QScreen>
#include <QCache>
#include <QThreadPool>
#include <memory>

namespace luna {
namespace {

// Shared design tokens used by both native painting and the QSS template.
constexpr const char* kNAVY = "#172B50";
constexpr const char* kBLUE = "#4F70B5";
constexpr const char* kCANVAS = "#F7F8FC";
constexpr const char* kPANEL = "#FFFFFF";
constexpr const char* kSELECTION = "#E8ECFA";
constexpr const char* kLINE = "#E0E4ED";
constexpr const char* kTEXT = "#263247";
constexpr const char* kMUTED = "#77839A";
constexpr const char* kTHUMB_LINE = "#C8CFDD";
constexpr const char* kSTAR_OFF = "#A2ACBF";
constexpr const char* kHOVER = "#EEF1F8";
constexpr const char* kACTIVE_TEXT = "#31549B";
constexpr const char* kBLUE_HOVER = "#3E60A5";

QString themeSheet(const QByteArray& bytes) {
    QString sheet = QString::fromUtf8(bytes);
    sheet.replace(QStringLiteral("@NAVY@"), QString::fromLatin1(kNAVY));
    sheet.replace(QStringLiteral("@BLUE@"), QString::fromLatin1(kBLUE));
    sheet.replace(QStringLiteral("@CANVAS@"), QString::fromLatin1(kCANVAS));
    sheet.replace(QStringLiteral("@PANEL@"), QString::fromLatin1(kPANEL));
    sheet.replace(QStringLiteral("@SELECTION@"), QString::fromLatin1(kSELECTION));
    sheet.replace(QStringLiteral("@LINE@"), QString::fromLatin1(kLINE));
    sheet.replace(QStringLiteral("@TEXT@"), QString::fromLatin1(kTEXT));
    sheet.replace(QStringLiteral("@MUTED@"), QString::fromLatin1(kMUTED));
    sheet.replace(QStringLiteral("@THUMB_LINE@"), QString::fromLatin1(kTHUMB_LINE));
    sheet.replace(QStringLiteral("@STAR_OFF@"), QString::fromLatin1(kSTAR_OFF));
    sheet.replace(QStringLiteral("@HOVER@"), QString::fromLatin1(kHOVER));
    sheet.replace(QStringLiteral("@ACTIVE_TEXT@"), QString::fromLatin1(kACTIVE_TEXT));
    sheet.replace(QStringLiteral("@BLUE_HOVER@"), QString::fromLatin1(kBLUE_HOVER));
    return sheet;
}

constexpr int kScanBatch = 90;
constexpr int kMaximumEntries = 15000;

class ThumbnailCropOverlay final : public QWidget {
public:
    using Finished = std::function<void(const QRect&)>;
    ThumbnailCropOverlay(QWidget* documentViewport, Finished callback)
        : QWidget(documentViewport), callback_(std::move(callback)) {
        setAttribute(Qt::WA_DeleteOnClose);
        setFocusPolicy(Qt::StrongFocus);
        setCursor(Qt::CrossCursor);
        setMouseTracking(true);
        setGeometry(documentViewport->rect());
        raise();
        show();
        setFocus();
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(15, 23, 42, 36));
        painter.setPen(QColor("#ffffff"));
        painter.setBrush(QColor(15, 23, 42, 190));
        painter.drawRoundedRect(QRect(18, 14, qMin(width() - 36, 380), 38), 7, 7);
        painter.drawText(QRect(27, 17, qMin(width() - 54, 370), 32),
            Qt::AlignLeft | Qt::AlignVCenter,
            QStringLiteral("썸네일로 저장할 영역을 드래그하세요  ·  ESC 취소"));
        if (!selecting_) return;
        QPen pen(QColor("#3B82F6"), 2, Qt::SolidLine);
        painter.setPen(pen);
        painter.setBrush(QColor(59, 130, 246, 35));
        painter.drawRect(selection_);
    }
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton) return;
        origin_ = event->position().toPoint();
        selection_ = QRect(origin_, origin_);
        selecting_ = true;
        update();
        event->accept();
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        if (!selecting_) return;
        selection_ = QRect(origin_, event->position().toPoint()).normalized()
            .intersected(rect());
        update();
        event->accept();
    }
    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton || !selecting_) return;
        selecting_ = false;
        selection_ = QRect(origin_, event->position().toPoint()).normalized()
            .intersected(rect());
        const QRect selected = selection_;
        hide(); // Exclude the selection layer from QWidget::grab().
        if (selected.width() >= 40 && selected.height() >= 40) {
            QTimer::singleShot(0, this, [this, selected] {
                if (callback_) callback_(selected);
                close();
            });
        } else {
            close();
        }
        event->accept();
    }
    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Escape) {
            hide();
            close();
            event->accept();
            return;
        }
        QWidget::keyPressEvent(event);
    }
private:
    Finished callback_;
    QPoint origin_;
    QRect selection_;
    bool selecting_ = false;
};

class DocumentCard final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override { return {300, 104}; }
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        painter->save();
        const QRect r = option.rect;
        const bool selected = option.state & QStyle::State_Selected;
        painter->fillRect(r, QColor(selected ? kSELECTION : kPANEL));
        if (selected) painter->fillRect(QRect(r.left(), r.top(), 3, r.height()), QColor(kBLUE));
        painter->setPen(QColor(kLINE));
        painter->drawLine(r.bottomLeft(), r.bottomRight());
        const QRect paper(r.left() + 18, r.top() + 16, 40, 64);
        painter->setBrush(Qt::white);
        painter->drawRoundedRect(paper, 4, 4);
        for (int y = 11; y <= 35; y += 6) {
            painter->setPen(QColor(kTHUMB_LINE));
            painter->drawLine(paper.left() + 7, paper.top() + y, paper.right() - 7, paper.top() + y);
        }
        QFont captionFont = option.font;
        captionFont.setPointSize(8);
        painter->setFont(captionFont);
        painter->setPen(QColor(kBLUE));
        painter->drawText(paper.adjusted(0, 43, 0, 0), Qt::AlignHCenter | Qt::AlignTop, QStringLiteral("PDF"));
        const QFileInfo pdf(index.data(Qt::UserRole).toString());
        const QString sidecar = pdf.absolutePath() + QLatin1Char('/') + pdf.completeBaseName() + QStringLiteral(".luna-thumb.png");
        QPointer<QWidget> view(const_cast<QWidget*>(option.widget));
        const QImage thumbnail = ThumbnailCache::instance()->request(sidecar, view, [view] {
            if (auto* list = qobject_cast<QAbstractItemView*>(view.data())) list->viewport()->update();
            else if (view) view->update();
        });
        if (!thumbnail.isNull()) {
            const QSize size = thumbnail.size().scaled(paper.size(), Qt::KeepAspectRatio);
            painter->fillRect(paper, Qt::white);
            painter->drawImage(QRect(paper.center() - QPoint(size.width()/2,size.height()/2), size), thumbnail);
        }
        QFont title = option.font;
        title.setPointSize(10);
        title.setBold(true);
        painter->setFont(title);
        painter->setPen(QColor(kNAVY));
        const QRect text(r.left() + 74, r.top() + 14, qMax(20, r.width() - 112), 52);
        painter->save();
        painter->setClipRect(text, Qt::IntersectClip);
        painter->drawText(text, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap | Qt::TextWrapAnywhere, index.data().toString());
        painter->restore();
        painter->setFont(captionFont);
        painter->setPen(QColor(kMUTED));
        const QString location = index.data(Qt::UserRole + 1).toString();
        painter->drawText(QRect(text.left(), r.top() + 75, qMax(20, r.width() - 94), 18),
            Qt::AlignLeft | Qt::AlignVCenter,
            QFontMetrics(captionFont).elidedText(location, Qt::ElideMiddle, qMax(20, r.width() - 94)));
        painter->setFont(option.font);
        painter->setPen(QColor(index.data(Qt::UserRole + 2).toBool() ? kBLUE : kSTAR_OFF));
        painter->drawText(QRect(r.right() - 30, r.top() + 16, 22, 24), Qt::AlignCenter,
            index.data(Qt::UserRole + 2).toBool() ? QStringLiteral("★") : QStringLiteral("☆"));
        painter->restore();
    }
};

class IndependentDocuments final : public QObject {
public:
    explicit IndependentDocuments(pdfviewer::PDFEditorMainWindow& window)
        : QObject(&window), window_(window), claim_([this] { focusWindow(); }) {
        setObjectName(QStringLiteral("LunaIndependentDocuments"));
        connect(&window_, &QWidget::windowTitleChanged, this, [this] { synchronize(); });
        connect(&poll_, &QTimer::timeout, this, [this] { synchronize(); });
        poll_.start(200);
        window_.setAcceptDrops(true);
        qApp->installEventFilter(this);
    }
    void focusWindow() {
        if (window_.isMinimized()) window_.showNormal();
        window_.raise(); window_.activateWindow();
    }
    bool initial(const QString& path) {
        if (!validPdf(path)) {
            QMessageBox::warning(&window_, QStringLiteral("PDF 열기 실패"), QStringLiteral("PDF 파일이 없거나 읽을 수 없는 경로입니다."));
            return false;
        }
        QScopedValueRollback<bool> opening(opening_, true);
        const auto result = claim_.acquire(path);
        if (result == DocumentClaim::Result::Existing) return false;
        if (result != DocumentClaim::Result::Acquired) {
            QMessageBox::warning(&window_, QStringLiteral("문서 열기 실패"),
                QStringLiteral("문서 창의 중복 열기 방지 기능을 시작하지 못했습니다."));
            return false;
        }
        pending_ = true; pendingIdle_ = 0;
        window_.getProgramController()->openDocument(path);
        return true;
    }
    void open(const QString& path) {
        if (!validPdf(path)) {
            QMessageBox::warning(&window_, QStringLiteral("PDF 열기 실패"), QStringLiteral("PDF 파일이 이동되었거나 존재하지 않습니다."));
            return;
        }
        const QString identity = DocumentClaim::identity(path);
        if (identity == claim_.path()) { focusWindow(); return; }
        if (DocumentClaim::activateExisting(path)) return;
        auto* controller = window_.getProgramController();
        if (!controller->getDocument() && !pending_) {
            if (window_.isEnabled() && controller->canClose()) initial(path);
            return;
        }
        // Coalesce repeated clicks during child startup, before its local
        // endpoint is listening. The child also claims the path atomically.
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        for (auto it = launching_.begin(); it != launching_.end(); ) {
            if (now - it.value() > 10000) it = launching_.erase(it); else ++it;
        }
        if (launching_.contains(identity)) return;
        if (!QProcess::startDetached(QCoreApplication::applicationFilePath(), {QFileInfo(path).absoluteFilePath()}, QFileInfo(path).absolutePath())) {
            QMessageBox::warning(&window_, QStringLiteral("문서 창 열기 실패"),
                QStringLiteral("새 PDF 창을 시작하지 못했습니다. 현재 문서는 그대로 유지됩니다."));
            return;
        }
        launching_.insert(identity, now);
    }
    bool openElsewhere(const QString& path) {
        return DocumentClaim::identity(path) != claim_.path() && DocumentClaim::activateExisting(path);
    }
    void chooseFiles() {
        const QStringList paths = QFileDialog::getOpenFileNames(&window_, QStringLiteral("PDF 열기 · 문서별 독립 창"),
            QFileInfo(window_.getProgramController()->getOriginalFileName()).absolutePath(), QStringLiteral("PDF (*.pdf *.PDF)"));
        for (const QString& path : paths) open(path);
    }
protected:
    bool eventFilter(QObject* object, QEvent* event) override {
        if (event->type() != QEvent::DragEnter && event->type() != QEvent::Drop) return false;
        auto* widget = qobject_cast<QWidget*>(object);
        if (!widget || widget->window() != &window_) return false;
        auto* drop = static_cast<QDropEvent*>(event);
        const auto urls = drop->mimeData()->urls();
        if (urls.isEmpty()) return false;
        for (const auto& url : urls) if (!url.isLocalFile() || !validPdf(url.toLocalFile())) return false;
        if (event->type() == QEvent::Drop) for (const auto& url : urls) open(url.toLocalFile());
        drop->acceptProposedAction();
        return true;
    }
private:
    static bool validPdf(const QString& path) {
        const QFileInfo info(path);
        return info.isFile() && info.suffix().compare(QStringLiteral("pdf"), Qt::CaseInsensitive) == 0;
    }
    void synchronize() {
        if (opening_ || synchronizing_) return;
        QScopedValueRollback<bool> synchronizing(synchronizing_, true);
        auto* controller = window_.getProgramController();
        if (!controller->getDocument()) {
            if (pending_ && (!window_.isEnabled() || ++pendingIdle_ < 25)) return;
            pending_ = false; blockedPath_.clear(); claim_.release(); return;
        }
        pending_ = false;
        const QString path = controller->getOriginalFileName();
        if (DocumentClaim::identity(path).isEmpty() || DocumentClaim::identity(path) == claim_.path()) return;
        if (path == blockedPath_) return;
        blockedPath_.clear();
        // Native Save As and advanced recent-file actions can change the path.
        // Never discard unsaved edits if an advanced action bypasses our open UI.
        if (claim_.acquire(path) == DocumentClaim::Result::Existing && !duplicateNotice_) {
            blockedPath_ = path;
            duplicateNotice_ = true;
            QTimer::singleShot(0, this, [this, path] {
                auto* c = window_.getProgramController();
                if (c->getOriginalFileName() == path && window_.isEnabled() && c->canClose() && c->askForSaveDocumentBeforeClose())
                    c->closeDocument();
                duplicateNotice_ = false;
            });
        }
    }
    pdfviewer::PDFEditorMainWindow& window_;
    DocumentClaim claim_;
    QTimer poll_;
    QHash<QString, qint64> launching_;
    bool pending_ = false;
    int pendingIdle_ = 0;
    bool duplicateNotice_ = false;
    bool opening_ = false;
    bool synchronizing_ = false;
    QString blockedPath_;
};

IndependentDocuments* independentDocuments(pdfviewer::PDFEditorMainWindow& window) {
    return dynamic_cast<IndependentDocuments*>(window.findChild<QObject*>(QStringLiteral("LunaIndependentDocuments")));
}

class LibraryPanel final : public QWidget {
    friend bool luna::verifyLibraryUi(pdfviewer::PDFEditorMainWindow&, QString&, const QString&);
    friend bool luna::verifyCancelledLibraryMove(pdfviewer::PDFEditorMainWindow&, QString&);
public:
    explicit LibraryPanel(pdfviewer::PDFEditorMainWindow* viewer, QWidget* parent)
        : QWidget(parent), viewer_(viewer)
    {
        setObjectName(QStringLiteral("LunaLibraryPanel"));
        const QStringList favoritePaths = savedPaths(QStringLiteral("library/favorites"));
        favorites_ = QSet<QString>(favoritePaths.cbegin(), favoritePaths.cend());
        setMinimumWidth(510);
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        split_ = new QSplitter(Qt::Horizontal, this);
        split_->setObjectName(QStringLiteral("LunaLibrarySplitter"));
        split_->setChildrenCollapsible(false);
        split_->setHandleWidth(1);
        layout->addWidget(split_);

        auto* navigationScroll = new QScrollArea(split_);
        navigationScroll->setWidgetResizable(true);
        navigationScroll->setFrameShape(QFrame::NoFrame);
        navigationScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        navigationScroll->setMinimumHeight(0);
        navigation_ = navigationScroll;
        navigation_->setObjectName(QStringLiteral("LunaNavigation"));
        navigation_->setMinimumWidth(190);
        navigation_->setMaximumWidth(300);
        auto* navigationContents = new QWidget(navigationScroll);
        navigationContents->setObjectName(QStringLiteral("LunaNavigationContents"));
        navigationScroll->setWidget(navigationContents);
        auto* nav = new QVBoxLayout(navigationContents);
        nav->setSizeConstraint(QLayout::SetMinimumSize);
        nav->setContentsMargins(14, 20, 14, 16);
        nav->setSpacing(9);
        auto* section = new QLabel(QStringLiteral("내 라이브러리"), navigation_);
        section->setObjectName(QStringLiteral("LunaSection"));
        nav->addWidget(section);
        auto* group = new QButtonGroup(this);
        auto navButton = [this, nav, group](const QString& text, QStyle::StandardPixmap icon) {
            auto* b = new QPushButton(style()->standardIcon(icon), text, navigation_);
            b->setObjectName(QStringLiteral("LunaNavigationButton"));
            b->setCheckable(true);
            b->setMinimumHeight(38);
            group->addButton(b);
            nav->addWidget(b);
            return b;
        };
        pdfTab_ = navButton(QStringLiteral("모든 논문"), QStyle::SP_FileDialogDetailedView);
        folderTab_ = navButton(QStringLiteral("선택한 폴더"), QStyle::SP_DirIcon);
        auto* recent = navButton(QStringLiteral("최근 열람"), QStyle::SP_FileDialogContentsView);
        auto* favorites = navButton(QStringLiteral("즐겨찾기"), QStyle::SP_DialogYesButton);
        pdfTab_->setObjectName(QStringLiteral("LunaAllDocumentsButton"));
        folderTab_->setObjectName(QStringLiteral("LunaFolderDocumentsButton"));
        recent->setObjectName(QStringLiteral("LunaRecentDocumentsButton"));
        favorites->setObjectName(QStringLiteral("LunaFavoriteDocumentsButton"));
        for (auto* b : {pdfTab_, folderTab_, recent, favorites}) b->setProperty("lunaNavigation", true);
        folderTab_->setChecked(true);
        nav->addSpacing(18);
        auto* folders = new QHBoxLayout;
        auto* folderHeading = new QLabel(QStringLiteral("내 폴더"), navigation_);
        folderHeading->setObjectName(QStringLiteral("LunaSection"));
        folders->addWidget(folderHeading, 1);
        auto* addFolder = new QToolButton(navigation_);
        addFolder->setText(QStringLiteral("＋"));
        addFolder->setToolTip(QStringLiteral("선택한 위치에 새 폴더 만들기"));
        addFolder->setObjectName(QStringLiteral("LunaTool"));
        folders->addWidget(addFolder);
        nav->addLayout(folders);
        rootLabel_ = new QLabel(navigation_);
        rootLabel_->setObjectName(QStringLiteral("LunaSecondary"));
        rootLabel_->setWordWrap(true);
        rootLabel_->setTextFormat(Qt::PlainText);
        nav->addWidget(rootLabel_);
        model_ = new QFileSystemModel(this);
        model_->setReadOnly(true);
        model_->setFilter(QDir::AllDirs | QDir::NoDotAndDotDot);
        tree_ = new QTreeView(navigation_);
        tree_->setModel(model_);
        tree_->setHeaderHidden(true);
        tree_->setAnimated(true);
        tree_->setIndentation(16);
        tree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        for (int i = 1; i < 4; ++i) tree_->hideColumn(i);
        nav->addWidget(tree_, 1);
        auto* location = new QPushButton(QStringLiteral("라이브러리 위치 선택…"), navigation_);
        location->setObjectName(QStringLiteral("LunaLocation"));
        nav->addWidget(location);
        storageLabel_ = new QLabel(navigation_);
        storageLabel_->setObjectName(QStringLiteral("LunaSecondary"));
        storageLabel_->setWordWrap(true);
        nav->addSpacing(10);
        nav->addWidget(storageLabel_);
        storage_ = new QProgressBar(navigation_);
        storage_->setRange(0, 100);
        storage_->setTextVisible(false);
        storage_->setFixedHeight(6);
        nav->addWidget(storage_);

        documents_ = new QWidget(split_);
        documents_->setObjectName(QStringLiteral("LunaDocuments"));
        documents_->setMinimumWidth(300);
        auto* docs = new QVBoxLayout(documents_);
        docs->setContentsMargins(0, 0, 0, 0);
        docs->setSpacing(0);
        auto* heading = new QWidget(documents_);
        auto* headingLayout = new QVBoxLayout(heading);
        headingLayout->setContentsMargins(18, 20, 18, 12);
        headingLayout->setSpacing(12);
        auto* title = new QLabel(QStringLiteral("논문 라이브러리"), heading);
        title->setObjectName(QStringLiteral("LunaLibraryTitle"));
        headingLayout->addWidget(title);
        auto* addFile = new QPushButton(QStringLiteral("＋  PDF 파일 추가"), heading);
        addFile->setObjectName(QStringLiteral("LunaPrimary"));
        addFile->setMinimumHeight(40);
        addFile->setToolTip(QStringLiteral("현재 폴더에 복사합니다. 원본은 유지하며 같은 이름의 파일은 덮어쓰지 않습니다."));
        headingLayout->addWidget(addFile);
        query_ = new QLineEdit(heading);
        query_->setObjectName(QStringLiteral("LunaSearch"));
        query_->setPlaceholderText(QStringLiteral("파일명으로 라이브러리 검색…"));
        query_->setClearButtonEnabled(true);
        query_->setMinimumHeight(34);
        headingLayout->addWidget(query_);
        auto* mode = new QLabel(QStringLiteral("PDF 파일 보기"), heading);
        mode->setObjectName(QStringLiteral("LunaActiveMode"));
        headingLayout->addWidget(mode);
        docs->addWidget(heading);
        pages_ = new QStackedWidget(documents_);
        pages_->setObjectName(QStringLiteral("LunaDocumentPages"));
        docs->addWidget(pages_, 1);
        auto page = [this](QListWidget*& list, QLabel*& count) {
            auto* p = new QWidget(pages_);
            auto* box = new QVBoxLayout(p);
            box->setContentsMargins(0, 0, 0, 0);
            box->setSpacing(0);
            count = new QLabel(p);
            count->setObjectName(QStringLiteral("LunaCount"));
            box->addWidget(count);
            list = makePdfList(p);
            box->addWidget(list, 1);
            pages_->addWidget(p);
        };
        page(folderPdfs_, folderCount_);
        page(allPdfs_, allCount_);
        page(historyPdfs_, historyCount_);
        split_->setSizes({210, 330});
        split_->setStretchFactor(0, 0);
        split_->setStretchFactor(1, 1);

        folderTimer_ = new QTimer(this);
        allTimer_ = new QTimer(this);
        folderTimer_->setInterval(0);
        allTimer_->setInterval(0);
        connect(folderTimer_, &QTimer::timeout, this, [this] { scanBatch(false); });
        connect(allTimer_, &QTimer::timeout, this, [this] { scanBatch(true); });
        connect(tree_, &QTreeView::clicked, this, [this](const QModelIndex& index) {
            const QString path = model_->filePath(index);
            if (!QFileInfo(path).isDir()) return;
            folderTab_->setChecked(true);
            pages_->setCurrentIndex(0);
            selectFolder(path);
        });
        connect(folderTab_, &QPushButton::clicked, this, [this] { pages_->setCurrentIndex(0); });
        connect(pdfTab_, &QPushButton::clicked, this, [this] {
            pages_->setCurrentIndex(1);
            if (!allScanned_) scanAll();
        });
        connect(recent, &QPushButton::clicked, this, [this] { showHistory(false); });
        connect(favorites, &QPushButton::clicked, this, [this] { showHistory(true); });
        connect(query_, &QLineEdit::textChanged, this, [this](const QString& word) {
            for (auto* list : {folderPdfs_, allPdfs_, historyPdfs_}) filter(list, word);
        });
        connect(location, &QPushButton::clicked, this, [this] {
            const QString path = QFileDialog::getExistingDirectory(this, QStringLiteral("논문 폴더 선택"), root_);
            if (!path.isEmpty()) selectRoot(path);
        });
        connect(addFile, &QPushButton::clicked, this, [this] { importPdfs(); });
        connect(addFolder, &QToolButton::clicked, this, [this] { createFolder(); });
        connect(viewer_, &QWidget::windowTitleChanged, this, [this] {
            QTimer::singleShot(0, this, [this] { rememberOpenedDocument(); });
        });
        connect(split_, &QSplitter::splitterMoved, this, [this] { savePanelState(); });

        QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("LUNA"), QStringLiteral("LUNA Library"));
        QString start = settings.value(QStringLiteral("library/root")).toString();
        if (!QFileInfo(start).isDir()) start = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        if (!QFileInfo(start).isDir()) start = QDir::homePath();
        selectRoot(start);
        split_->restoreState(settings.value(QStringLiteral("library/splitter")).toByteArray());
    }

    void updateMinimum() { setMinimumWidth((navigation_->isHidden() ? 0 : 190) + (documents_->isHidden() ? 0 : 300) + 1); }
    void toggleNavigation() { savePanelState(); navigation_->setVisible(navigation_->isHidden()); updateMinimum(); }
    void toggleDocuments() { savePanelState(); documents_->setVisible(documents_->isHidden()); updateMinimum(); }
    void focusSearch() { documents_->show(); updateMinimum(); query_->setFocus(); query_->selectAll(); }
    QLineEdit* searchEdit() const { return query_; }
    void savePanelState() {
        if (navigation_->isHidden() || documents_->isHidden()) return;
        QSettings(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("LUNA"), QStringLiteral("LUNA Library"))
            .setValue(QStringLiteral("library/splitter"), split_->saveState());
    }

private:
    QStringList savedPaths(const QString& key) const {
        return QSettings(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("LUNA"), QStringLiteral("LUNA Library")).value(key).toStringList();
    }
    void decorate(QListWidgetItem* item, const QString& path) {
        item->setData(Qt::UserRole, path);
        const QString location = QDir(root_).relativeFilePath(QFileInfo(path).absolutePath());
        item->setData(Qt::UserRole + 1, location == QStringLiteral(".") ? QStringLiteral("라이브러리 폴더 · PDF") : location);
        item->setData(Qt::UserRole + 2, favorites_.contains(path));
        item->setToolTip(path);
    }
    void toggleFavorite(const QString& path) {
        QStringList paths = savedPaths(QStringLiteral("library/favorites"));
        if (paths.contains(path)) paths.removeAll(path);
        else paths.append(path);
        favorites_ = QSet<QString>(paths.cbegin(), paths.cend());
        QSettings(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("LUNA"), QStringLiteral("LUNA Library")).setValue(QStringLiteral("library/favorites"), paths);
        for (auto* list : {folderPdfs_, allPdfs_, historyPdfs_}) {
            for (int i = 0; i < list->count(); ++i) {
                auto* item = list->item(i);
                item->setData(Qt::UserRole + 2, paths.contains(item->data(Qt::UserRole).toString()));
            }
            list->viewport()->update();
        }
        if (pages_->currentIndex() == 2 && showingFavorites_) showHistory(true);
    }
    void showHistory(bool favorites) {
        showingFavorites_ = favorites;
        historyPdfs_->clear();
        const QStringList paths = savedPaths(favorites ? QStringLiteral("library/favorites") : QStringLiteral("library/recent"));
        for (const auto& path : paths) {
            const QFileInfo info(path);
            if (!info.isFile() || info.suffix().compare(QStringLiteral("pdf"), Qt::CaseInsensitive) != 0) continue;
            auto* item = new QListWidgetItem(info.completeBaseName(), historyPdfs_);
            decorate(item, path);
        }
        filter(historyPdfs_, query_->text());
        historyCount_->setText(QStringLiteral("%1 · %2개").arg(favorites ? QStringLiteral("즐겨찾기") : QStringLiteral("최근 열람"))
            .arg(historyPdfs_->count()));
        pages_->setCurrentIndex(2);
    }
    void rememberOpenedDocument() {
        auto* controller = viewer_->getProgramController();
        if (!controller->getDocument()) { lastOpened_.clear(); return; }
        const QFileInfo info(controller->getOriginalFileName());
        const QString path = info.canonicalFilePath();
        if (!info.isFile() || path.isEmpty() || path == lastOpened_) return;
        lastOpened_ = path;
        QStringList recent = savedPaths(QStringLiteral("library/recent"));
        recent.removeAll(path);
        recent.prepend(path);
        while (recent.size() > 100) recent.removeLast();
        QSettings(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("LUNA"), QStringLiteral("LUNA Library")).setValue(QStringLiteral("library/recent"), recent);
        // Reuse the native PDF4QT draw-space controller: no replacement renderer.
        auto* proxy = controller->getPdfWidget()->getDrawWidgetProxy();
        proxy->setPageLayout(pdf::PageLayout::OneColumn);
        proxy->zoom(2.0);
        if (pages_->currentIndex() == 2 && !showingFavorites_) showHistory(false);
    }
    void importPdfs() {
        const QStringList files = QFileDialog::getOpenFileNames(this,
            QStringLiteral("PDF 추가 · 원본을 유지하고 현재 폴더에 복사"), current_, QStringLiteral("PDF (*.pdf *.PDF)"));
        QString opened;
        for (const QString& path : files) {
            const QFileInfo info(path);
            if (!info.isFile() || info.suffix().compare(QStringLiteral("pdf"), Qt::CaseInsensitive) != 0) continue;
            const QString target = QDir(current_).filePath(info.fileName());
            if (info.canonicalFilePath() == QFileInfo(target).canonicalFilePath()) { opened = target; continue; }
            if (QFileInfo::exists(target)) {
                QMessageBox::information(this, QStringLiteral("같은 이름의 PDF"),
                    QStringLiteral("현재 폴더에 같은 이름의 파일이 있어 덮어쓰지 않았습니다.\n%1").arg(info.fileName()));
                continue;
            }
            if (!QFile::copy(path, target)) {
                QMessageBox::warning(this, QStringLiteral("PDF 추가 실패"),
                    QStringLiteral("파일을 복사하지 못했습니다. 원본은 그대로 유지됩니다.\n%1").arg(info.fileName()));
                continue;
            }
            opened = target;
        }
        if (opened.isEmpty()) return;
        allTimer_->stop();
        allIterator_.reset();
        allScanned_ = false;
        allPdfs_->clear();
        folderTab_->setChecked(true);
        pages_->setCurrentIndex(0);
        selectFolder(current_);
        openPdf(opened);
    }
    bool eventFilter(QObject* observed, QEvent* event) override {
        const bool historyViewport = historyPdfs_ && observed == historyPdfs_->viewport();
        const bool folderViewport = folderPdfs_ && observed == folderPdfs_->viewport();
        const bool allViewport = allPdfs_ && observed == allPdfs_->viewport();
        if (!folderViewport && !allViewport && !historyViewport)
            return QWidget::eventFilter(observed, event);
        if (event->type() == QEvent::MouseMove) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            auto* list = folderViewport ? folderPdfs_ : (allViewport ? allPdfs_ : historyPdfs_);
            QListWidgetItem* item = list->itemAt(mouse->position().toPoint());
            if (item && !item->isHidden()) {
                const QString path = item->data(Qt::UserRole).toString();
                showThumbnailFor(path);
            } else {
                hideThumbnail();
            }
        } else if (event->type() == QEvent::Leave ||
                   event->type() == QEvent::Hide ||
                   event->type() == QEvent::MouseButtonPress ||
                   event->type() == QEvent::Wheel) {
            hideThumbnail();
        }
        return QWidget::eventFilter(observed, event);
    }

    QString thumbnailForPdf(const QString& documentPath) const {
        const QFileInfo info(documentPath);
        if (!info.isFile() ||
            info.suffix().compare(QStringLiteral("pdf"), Qt::CaseInsensitive) != 0)
            return {};
        const QString prefix = info.absolutePath() + QLatin1Char('/') +
                               info.completeBaseName() + QStringLiteral(".luna-thumb");
        for (const QString& extension : {QStringLiteral(".png"), QStringLiteral(".jpg"),
                                         QStringLiteral(".jpeg")}) {
            const QString candidate = prefix + extension;
            const QFileInfo thumbnail(candidate);
            if (!thumbnail.isSymLink() && thumbnail.isFile() && thumbnail.size() > 0 && thumbnail.size() <= 5'000'000)
                return candidate;
        }
        return {};
    }
    void ensureThumbnailPopup() {
        if (thumbnailPopup_) return;
        thumbnailPopup_ = new QFrame(this, Qt::ToolTip | Qt::FramelessWindowHint);
        thumbnailPopup_->setObjectName(QStringLiteral("LunaThumbnailPopup"));
        thumbnailPopup_->setAttribute(Qt::WA_TransparentForMouseEvents);
        thumbnailPopup_->setAttribute(Qt::WA_ShowWithoutActivating);
        thumbnailPopup_->setStyleSheet(QStringLiteral(
            "QFrame#LunaThumbnailPopup {background:#FFFFFF; border:1px solid #CBD5E1;"
            " border-radius:10px;} QLabel {background:transparent; color:#334155;}"));
        auto* layout = new QVBoxLayout(thumbnailPopup_);
        layout->setContentsMargins(10, 10, 10, 10);
        layout->setSpacing(7);
        thumbnailImage_ = new QLabel(thumbnailPopup_);
        thumbnailImage_->setFixedWidth(235);
        thumbnailImage_->setAlignment(Qt::AlignCenter);
        thumbnailImage_->setTextFormat(Qt::PlainText);
        layout->addWidget(thumbnailImage_);
        thumbnailLabel_ = new QLabel(thumbnailPopup_);
        thumbnailLabel_->setFixedWidth(235);
        thumbnailLabel_->setWordWrap(true);
        thumbnailLabel_->setTextFormat(Qt::PlainText);
        layout->addWidget(thumbnailLabel_);
        auto* opacity = new QGraphicsOpacityEffect(thumbnailPopup_);
        thumbnailPopup_->setGraphicsEffect(opacity);
        fade_ = new QPropertyAnimation(opacity, "opacity", thumbnailPopup_);
        fade_->setDuration(165);
        fade_->setStartValue(0.0);
        fade_->setEndValue(1.0);
        thumbnailPopup_->hide();
    }
    void hideThumbnail() {
        if (thumbnailPopup_) thumbnailPopup_->hide();
        thumbnailPath_.clear();
        thumbnailRevision_.clear();
        hoverRequest_.clear();
        if (fade_) fade_->stop();
    }
    void showThumbnailFor(const QString& pdfPath) {
        const QString file = thumbnailForPdf(pdfPath);
        if (file.isEmpty()) { hideThumbnail(); return; }
        const QFileInfo currentImage(file);
        const QString revision = file + QStringLiteral("|%1|%2").arg(currentImage.size()).arg(currentImage.lastModified().toMSecsSinceEpoch());
        if (revision == thumbnailRevision_ && thumbnailPopup_ && thumbnailPopup_->isVisible())
            return;
        ensureThumbnailPopup();
        hoverRequest_ = file;
        QPointer<LibraryPanel> owner(this);
        const QImage image = ThumbnailCache::instance()->request(file, this, [owner, file, pdfPath] {
            if (owner && owner->hoverRequest_ == file) owner->showThumbnailFor(pdfPath);
        });
        if (image.isNull()) { if (thumbnailPopup_) thumbnailPopup_->hide(); return; }
        thumbnailImage_->setPixmap(QPixmap::fromImage(image.scaled(QSize(235,310), Qt::KeepAspectRatio, Qt::SmoothTransformation)));
        thumbnailLabel_->setText(QFileInfo(pdfPath).fileName());
        thumbnailPopup_->adjustSize();
        const QPoint cursor = QCursor::pos();
        QPoint target = cursor + QPoint(19, 12);
        if (auto* screen = QGuiApplication::screenAt(cursor)) {
            const QRect area = screen->availableGeometry();
            target.setX(qBound(area.left(), target.x(),
                qMax(area.left(), area.right() - thumbnailPopup_->width() + 1)));
            target.setY(qBound(area.top(), target.y(),
                qMax(area.top(), area.bottom() - thumbnailPopup_->height() + 1)));
        }
        fade_->stop();
        if (auto* effect = qobject_cast<QGraphicsOpacityEffect*>(thumbnailPopup_->graphicsEffect()))
            effect->setOpacity(0.0);
        thumbnailPopup_->move(target);
        thumbnailPopup_->show();
        fade_->start();
        thumbnailPath_ = file;
        thumbnailRevision_ = revision;
    }

    void startThumbnailCrop() {
        if (cropOverlay_) {
            cropOverlay_->setFocus();
            return;
        }
        auto* controller = viewer_->getProgramController();
        const QString currentPdf = controller->getOriginalFileName();
        const QFileInfo info(currentPdf);
        if (!controller->canClose() || !info.isFile() ||
            info.suffix().compare(QStringLiteral("pdf"), Qt::CaseInsensitive) != 0) {
            QMessageBox::information(this, QStringLiteral("썸네일 지정"),
                QStringLiteral("먼저 PDF를 열고 로딩이 완료된 후 이용해 주세요."));
            return;
        }
        QWidget* document = controller->getPdfWidget()->getDrawWidget()->getWidget();
        if (!document || document->width() < 60 || document->height() < 60)
            return;
        QPointer<LibraryPanel> library(this);
        QPointer<QWidget> target(document);
        auto* overlay = new ThumbnailCropOverlay(document,
            [library, target, source = info.absoluteFilePath()](const QRect& chosen) {
                if (library && target) library->saveThumbnail(target, source, chosen);
            });
        cropOverlay_ = overlay;
    }
    void saveThumbnail(QWidget* document, const QString& documentPath,
                       const QRect& chosen) {
        if (QFileInfo(viewer_->getProgramController()->getOriginalFileName()).canonicalFilePath() !=
            QFileInfo(documentPath).canonicalFilePath()) return;
        const QRect area = chosen.intersected(document->rect());
        if (area.width() < 40 || area.height() < 40) return;
        const QFileInfo original(documentPath);
        if (!original.isFile() ||
            original.suffix().compare(QStringLiteral("pdf"), Qt::CaseInsensitive) != 0)
            return;
        const QString output = original.absolutePath() + QLatin1Char('/') +
            original.completeBaseName() + QStringLiteral(".luna-thumb.png");
        if (QFileInfo(output).isSymLink()) {
            QMessageBox::warning(this, QStringLiteral("썸네일 저장 취소"), QStringLiteral("연결 파일에는 저장하지 않습니다."));
            return;
        }
        if (QFileInfo::exists(output) &&
            QMessageBox::question(this, QStringLiteral("썸네일 덮어쓰기"),
                QStringLiteral("기존 썸네일을 새 이미지로 교체할까요?"),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;
        // The image is saved beside the local PDF, including MYBOX-synced folders.
        // QWidget::grab is used *after* the crop overlay is hidden.
        document->repaint();
        const QPixmap image = document->grab(area);
        if (image.isNull()) {
            QMessageBox::warning(this, QStringLiteral("썸네일 오류"),
                QStringLiteral("선택한 화면 영역을 캡처할 수 없습니다."));
            return;
        }
        QSaveFile dest(output);
        dest.setDirectWriteFallback(false);
        if (!dest.open(QIODevice::WriteOnly) || !image.save(&dest, "PNG") ||
            !dest.commit()) {
            QMessageBox::warning(this, QStringLiteral("썸네일 저장 실패"),
                QStringLiteral("원본 PDF 폴더에 이미지를 저장할 수 없습니다."));
            return;
        }
        ThumbnailCache::instance()->invalidate();
        hideThumbnail();
        for (auto* list : {folderPdfs_, allPdfs_, historyPdfs_}) if (list) list->viewport()->update();

    }

    QListWidget* makePdfList(QWidget* parent) {
        auto* list = new QListWidget(parent);
        list->setMouseTracking(true);
        list->viewport()->installEventFilter(this);
        list->setUniformItemSizes(true);
        list->setSelectionMode(QAbstractItemView::SingleSelection);
        list->setEditTriggers(QAbstractItemView::NoEditTriggers);
        list->setSortingEnabled(false);
        list->setObjectName(QStringLiteral("LunaDocumentList"));
        list->setItemDelegate(new DocumentCard(list));
        list->setWordWrap(true);
        list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        list->setContextMenuPolicy(Qt::CustomContextMenu);
        auto* favoriteAction = new QAction(QStringLiteral("즐겨찾기 전환"), list);
        favoriteAction->setObjectName(QStringLiteral("LunaToggleFavorite"));
        favoriteAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+D")));
        favoriteAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        list->addAction(favoriteAction);
        connect(favoriteAction, &QAction::triggered, this, [this, list] {
            if (auto* item = list->currentItem()) toggleFavorite(item->data(Qt::UserRole).toString());
        });
        connect(list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) { openItem(item); });
        connect(list, &QListWidget::customContextMenuRequested, this, [this, list](const QPoint& point) {
            auto* item = list->itemAt(point);
            if (!item) return;
            const QString path = item->data(Qt::UserRole).toString();
            QMenu menu(this);
            auto* favorite = menu.addAction(item->data(Qt::UserRole + 2).toBool() ? QStringLiteral("즐겨찾기 해제") : QStringLiteral("즐겨찾기 추가"));
            auto* crop = menu.addAction(QStringLiteral("현재 PDF의 썸네일 지정…"));
            crop->setEnabled(QFileInfo(path).canonicalFilePath() == QFileInfo(viewer_->getProgramController()->getOriginalFileName()).canonicalFilePath());
            auto* move = menu.addAction(QStringLiteral("PDF와 썸네일 이동…"));
            auto* chosen = menu.exec(list->viewport()->mapToGlobal(point));
            if (chosen == favorite) toggleFavorite(path);
            else if (chosen == crop) startThumbnailCrop();
            else if (chosen == move) moveDocument(path);
        });
        connect(list, &QListWidget::itemClicked, this, [this, list](QListWidgetItem* item) {
            if (list->viewport()->mapFromGlobal(QCursor::pos()).x() >= list->viewport()->width() - 36)
                toggleFavorite(item->data(Qt::UserRole).toString());
        });
        return list;
    }
    void moveDocument(const QString& path) {
        const QString destination = QFileDialog::getExistingDirectory(this,
            QStringLiteral("PDF와 썸네일 이동 · 같은 드라이브의 대상 폴더"), QFileInfo(path).absolutePath());
        if (destination.isEmpty()) return;
        moveDocumentTo(path, destination);
    }
    void moveDocumentTo(const QString& path, const QString& destination) {
        const QString source = QFileInfo(path).canonicalFilePath();
        if (source.isEmpty()) return;
        if (auto* documents = independentDocuments(*viewer_); documents && documents->openElsewhere(source)) {
            QMessageBox::information(this, QStringLiteral("다른 창에서 열린 PDF"),
                QStringLiteral("이 PDF를 열고 있는 LUNA 창에서 이동해 주세요. 해당 창을 활성화했습니다."));
            return;
        }
        auto* controller = viewer_->getProgramController();
        const bool opened = controller->getDocument() &&
            QFileInfo(controller->getOriginalFileName()).canonicalFilePath() == source;
        int page = 0;
        double zoom = 2.0;
        if (opened) {
            if (!viewer_->isEnabled() || !controller->canClose()) return;
            if (!controller->askForSaveDocumentBeforeClose()) return;
            // Save As may have changed the current document while prompting.
            // Do not close or relocate a different document under the old name.
            if (QFileInfo(controller->getOriginalFileName()).canonicalFilePath() != source) {
                QMessageBox::information(this, QStringLiteral("이동 취소"),
                    QStringLiteral("저장한 문서의 경로가 변경되었습니다. 이동할 파일을 다시 선택해 주세요."));
                return;
            }
            const auto currentPages = controller->getPdfWidget()->getDrawWidget()->getCurrentPages();
            if (!currentPages.empty()) page = int(currentPages.front());
            zoom = controller->getPdfWidget()->getDrawWidgetProxy()->getZoom();
            if (cropOverlay_) cropOverlay_->close();
            controller->closeDocument();
        }
        hideThumbnail();
        const auto result = movePdfWithThumbnails(source, destination);
        if (result.success) {
            QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("LUNA"), QStringLiteral("LUNA Library"));
            for (const QString& key : {QStringLiteral("library/favorites"), QStringLiteral("library/recent")}) {
                QStringList paths = settings.value(key).toStringList();
                for (QString& value : paths) if (value == source) value = result.documentPath;
                paths.removeDuplicates();
                settings.setValue(key, paths);
            }
            const auto favorites = savedPaths(QStringLiteral("library/favorites"));
            favorites_ = QSet<QString>(favorites.cbegin(), favorites.cend());
            ThumbnailCache::instance()->invalidate();
            allTimer_->stop(); allIterator_.reset(); allScanned_ = false; allPdfs_->clear();
            selectFolder(current_);
            if (pages_->currentIndex() == 1) scanAll();
            else if (pages_->currentIndex() == 2) showHistory(showingFavorites_);
        }
        if (opened) {
            controller->openDocument(result.documentPath);
            auto* restore = new QTimer(this);
            connect(restore, &QTimer::timeout, this, [this, restore, target = result.documentPath, page, zoom, attempts = 0]() mutable {
                auto* c = viewer_->getProgramController();
                if (++attempts > 200) { restore->stop(); restore->deleteLater(); return; }
                if (!viewer_->isEnabled() || !c->getDocument()) return;
                if (QFileInfo(c->getOriginalFileName()).canonicalFilePath() == QFileInfo(target).canonicalFilePath()) {
                    c->getPdfWidget()->getDrawWidgetProxy()->zoom(zoom);
                    c->getPdfWidget()->getDrawWidgetProxy()->goToPage(page);
                }
                restore->stop(); restore->deleteLater();
            });
            restore->start(50);
        }
        if (!result.success) QMessageBox::warning(this, QStringLiteral("파일 이동 실패"), result.error);
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
        rootLabel_->setText(info.fileName().isEmpty() ? root_ : info.fileName());
        rootLabel_->setToolTip(root_);
        const QStorageInfo disk(root_);
        if (disk.isReady() && disk.bytesTotal() > 0 && disk.bytesAvailable() >= 0) {
            const double gib = 1024.0 * 1024.0 * 1024.0;
            storage_->setValue(int(100.0 * (disk.bytesTotal() - disk.bytesAvailable()) / disk.bytesTotal()));
            storageLabel_->setText(QStringLiteral("디스크 사용 공간\n%1 GB / %2 GB")
                .arg((disk.bytesTotal() - disk.bytesAvailable()) / gib, 0, 'f', 1).arg(disk.bytesTotal() / gib, 0, 'f', 1));
        } else { storage_->setValue(0); storageLabel_->setText(QStringLiteral("저장 공간 정보 없음")); }
        model_->setRootPath(root_);
        tree_->setRootIndex(model_->index(root_));
        QSettings(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("LUNA"), QStringLiteral("LUNA Library"))
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
        hideThumbnail();
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
            decorate(item, path);
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
            if (list->count() >= kMaximumEntries) count->setText(count->text() + QStringLiteral(" 이상 · 표시 제한"));
        } else {
            count->setText(QStringLiteral("PDF 검색 중… %1개").arg(list->count()));
        }
    }
    void openItem(QListWidgetItem* item) {
        if (item) openPdf(item->data(Qt::UserRole).toString());
    }
    void openPdf(const QString& path) {
        if (auto* documents = independentDocuments(*viewer_)) { documents->open(path); return; }
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

    QFrame* thumbnailPopup_ = nullptr;
    QLabel* thumbnailImage_ = nullptr;
    QLabel* thumbnailLabel_ = nullptr;
    QPropertyAnimation* fade_ = nullptr;
    QString thumbnailPath_;
    QString thumbnailRevision_;
    QString hoverRequest_;
    QPointer<ThumbnailCropOverlay> cropOverlay_;
    pdfviewer::PDFEditorMainWindow* viewer_;
    QSplitter* split_ = nullptr;
    QWidget* navigation_ = nullptr;
    QWidget* documents_ = nullptr;
    QLabel* rootLabel_ = nullptr;
    QLabel* storageLabel_ = nullptr;
    QProgressBar* storage_ = nullptr;
    QListWidget* historyPdfs_ = nullptr;
    QLabel* historyCount_ = nullptr;
    bool showingFavorites_ = false;
    QString lastOpened_;
    QSet<QString> favorites_;
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

class WindowState final : public QObject {
public:
    explicit WindowState(QMainWindow* window) : QObject(window), window_(window) { window->installEventFilter(this); }
protected:
    bool eventFilter(QObject* object, QEvent* event) override {
        if (object == window_ && event->type() == QEvent::Close) {
            QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("LUNA"), QStringLiteral("LUNA Library"));
            settings.setValue(QStringLiteral("window/geometry"), window_->saveGeometry());
            settings.setValue(QStringLiteral("window/state"), window_->saveState());
        }
        return QObject::eventFilter(object, event);
    }
private:
    QMainWindow* window_;
};

} // namespace

void installLibrary(pdfviewer::PDFEditorMainWindow& window) {
    QFile sheet(QStringLiteral(":/luna/lunalibrary.qss"));
    if (sheet.open(QIODevice::ReadOnly)) window.setStyleSheet(themeSheet(sheet.readAll()));
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
    dock->setMinimumWidth(1);
    dock->setMaximumWidth(850);
    auto* library = new LibraryPanel(&window, dock);
    dock->setWidget(library);
    window.addDockWidget(Qt::LeftDockWidgetArea, dock);
    dock->show();
    window.resizeDocks({dock}, {540}, Qt::Horizontal);

    auto* controls = new QToolBar(QStringLiteral("라이브러리"), &window);
    controls->setObjectName(QStringLiteral("LunaLibraryToolbar"));
    controls->setMovable(false);
    controls->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    controls->setIconSize(QSize(18, 18));
    auto* folders = controls->addAction(window.style()->standardIcon(QStyle::SP_DirIcon), QStringLiteral("폴더"));
    folders->setObjectName(QStringLiteral("LunaToggleNavigation"));
    folders->setCheckable(true);
    folders->setChecked(true);
    QObject::connect(folders, &QAction::triggered, library, [library] { library->toggleNavigation(); });
    auto* documents = controls->addAction(window.style()->standardIcon(QStyle::SP_FileDialogDetailedView), QStringLiteral("목록"));
    documents->setObjectName(QStringLiteral("LunaToggleDocuments"));
    documents->setCheckable(true);
    documents->setChecked(true);
    QObject::connect(documents, &QAction::triggered, library, [library] { library->toggleDocuments(); });
    auto* focus = controls->addAction(QStringLiteral("집중 모드"));
    focus->setObjectName(QStringLiteral("LunaFocusMode"));
    focus->setCheckable(true);
    QObject::connect(focus, &QAction::toggled, dock, [dock](bool enabled) { dock->setVisible(!enabled); });
    auto* space = new QWidget(controls);
    space->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    controls->addWidget(space);
    auto* globalSearch = new QLineEdit(controls);
    globalSearch->setObjectName(QStringLiteral("LunaGlobalSearch"));
    globalSearch->setPlaceholderText(QStringLiteral("라이브러리 검색…  Ctrl+K"));
    globalSearch->setMinimumWidth(240);
    globalSearch->setMaximumWidth(400);
    globalSearch->setClearButtonEnabled(true);
    controls->addWidget(globalSearch);
    QObject::connect(globalSearch, &QLineEdit::textChanged, library->searchEdit(), &QLineEdit::setText);
    QObject::connect(library->searchEdit(), &QLineEdit::textChanged, globalSearch, &QLineEdit::setText);
    auto* search = new QAction(QStringLiteral("라이브러리 검색"), &window);
    search->setObjectName(QStringLiteral("LunaSearchAction"));
    search->setShortcut(QKeySequence(QStringLiteral("Ctrl+K")));
    // PDF4QT uses Ctrl+K for Options. Reserve the requested LUNA shortcut
    // for library search while keeping Options available in the menu.
    if (auto* options = window.findChild<QAction*>(QStringLiteral("actionOptions")))
        options->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+O")));
    window.addAction(search);
    QObject::connect(search, &QAction::triggered, &window, [dock, library, globalSearch, documents, focus] {
        focus->setChecked(false);
        dock->show();
        documents->setChecked(true);
        library->focusSearch();
        globalSearch->setFocus();
        globalSearch->selectAll();
    });
    if (auto* pdfBar = window.findChild<QToolBar*>(QStringLiteral("mainToolBar"))) {
        window.insertToolBar(pdfBar, controls);
    } else window.addToolBar(Qt::TopToolBarArea, controls);
    const QList<QPair<QString, QString>> menus = {
        {QStringLiteral("menuFile"), QStringLiteral("파일")}, {QStringLiteral("menuEdit"), QStringLiteral("편집")},
        {QStringLiteral("menuView"), QStringLiteral("보기")}, {QStringLiteral("menuGoTo"), QStringLiteral("이동")},
        {QStringLiteral("menuInsert"), QStringLiteral("삽입")},
        {QStringLiteral("menuTools"), QStringLiteral("도구")}, {QStringLiteral("menuHelp"), QStringLiteral("도움말")}
    };
    for (const auto& entry : menus) if (auto* menu = window.findChild<QMenu*>(entry.first)) menu->setTitle(entry.second);
    auto* brand = new QLabel(QStringLiteral("<img src=\":/luna/LUNA-Moon.png\" width=\"24\" height=\"24\">  LUNA Library  v2"), &window);
    brand->setContentsMargins(12, 6, 20, 6);
    brand->setStyleSheet(QStringLiteral("color:%1; font-weight:700; font-size:16px;").arg(QString::fromLatin1(kNAVY)));
    window.menuBar()->setCornerWidget(brand, Qt::TopLeftCorner);

    if (auto* bar = window.findChild<QToolBar*>(QStringLiteral("mainToolBar"))) {
        installReaderToolbar(window, *bar, *focus);
        // Keep the real PDF widget and all controller pointers. Only the native
        // toolbar is reparented beside it, so library panels start at the header.
        window.removeToolBar(bar);
        auto* pdfWidget = window.takeCentralWidget();
        auto* frame = new QWidget(&window);
        frame->setObjectName(QStringLiteral("LunaPdfFrame"));
        auto* frameLayout = new QVBoxLayout(frame);
        frameLayout->setContentsMargins(0, 0, 0, 0);
        frameLayout->setSpacing(0);
        auto* documentHeader = new QWidget(frame);
        documentHeader->setObjectName(QStringLiteral("LunaDocumentHeader"));
        auto* documentRow = new QHBoxLayout(documentHeader);
        documentRow->setContentsMargins(12, 4, 8, 4);
        auto* documentTitle = new QLabel(QStringLiteral("PDF 문서"), documentHeader);
        documentTitle->setObjectName(QStringLiteral("LunaDocumentTitle"));
        documentTitle->setTextFormat(Qt::PlainText);
        documentTitle->setMinimumWidth(0);
        documentTitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        documentRow->addWidget(documentTitle, 1);
        QObject::connect(&window, &QWidget::windowTitleChanged, documentTitle, [&window, documentTitle] {
            QTimer::singleShot(0, documentTitle, [&window, documentTitle] {
                const QString file = window.getProgramController()->getOriginalFileName();
                documentTitle->setText(file.isEmpty() ? QStringLiteral("PDF 문서") : QFileInfo(file).fileName());
                documentTitle->setToolTip(file);
            });
        });
        frameLayout->addWidget(documentHeader);
        frameLayout->addWidget(bar);
        bar->show(); // QMainWindow::removeToolBar() explicitly hides it.
        if (pdfWidget) { frameLayout->addWidget(pdfWidget, 1); pdfWidget->show(); }
        window.setCentralWidget(frame);
    }
    QObject::connect(&window, &QWidget::windowTitleChanged, &window,
        [&window](const QString& title) {
            if (!title.startsWith(QStringLiteral("LUNA Library")))
                window.setWindowTitle(QStringLiteral("LUNA Library · ") + title);
        });
    window.setWindowTitle(QStringLiteral("LUNA Library"));
    window.resize(1366, 768);
    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("LUNA"), QStringLiteral("LUNA Library"));
    window.restoreGeometry(settings.value(QStringLiteral("window/geometry")).toByteArray());
    window.restoreState(settings.value(QStringLiteral("window/state")).toByteArray());
    focus->setChecked(dock->isHidden());
    hideAdvancedToolbars(window); // Old saved PDF4QT toolbar rows must not reappear.
    auto* documentWindows = new IndependentDocuments(window);
    if (auto* originalOpen = window.findChild<QAction*>(QStringLiteral("actionOpen"))) {
        // Preserve the native QAction and its connections; route the public
        // open UI through independent processes instead of replacing a document.
        auto* open = new QAction(originalOpen->icon(), QStringLiteral("PDF 열기…"), &window);
        open->setObjectName(QStringLiteral("LunaOpenDocuments"));
        open->setShortcut(QKeySequence(QStringLiteral("Ctrl+O")));
        open->setToolTip(QStringLiteral("PDF 열기 · 여러 문서는 독립 창에서 열립니다 (Ctrl+O)"));
        originalOpen->setShortcut(QKeySequence());
        window.addAction(open);
        QObject::connect(open, &QAction::triggered, documentWindows, &IndependentDocuments::chooseFiles);
        if (auto* reader = window.findChild<QToolBar*>(QStringLiteral("mainToolBar"))) {
            reader->insertAction(originalOpen, open); reader->removeAction(originalOpen);
        }
        for (auto* menu : window.findChildren<QMenu*>()) menu->removeAction(originalOpen);
        if (auto* file = window.findChild<QMenu*>(QStringLiteral("menuFile")))
            file->insertAction(file->actions().isEmpty() ? nullptr : file->actions().front(), open);
    }
    new WindowState(&window);
}

bool openInitialDocument(pdfviewer::PDFEditorMainWindow& window, const QString& path) {
    if (auto* documents = independentDocuments(window)) return documents->initial(path);
    return false;
}

bool verifyCancelledLibraryMove(pdfviewer::PDFEditorMainWindow& window, QString& error) {
    auto* dock = window.findChild<QDockWidget*>(QStringLiteral("LunaLibraryDock"));
    auto* library = dock ? dynamic_cast<LibraryPanel*>(dock->widget()) : nullptr;
    auto* controller = window.getProgramController();
    const auto* document = controller->getDocument();
    const QString source = controller->getOriginalFileName();
    QTemporaryDir target(QFileInfo(source).absolutePath() + QStringLiteral("/luna-cancelled-move-XXXXXX"));
    QFile original(source);
    if (!library || !document || !target.isValid() || !original.open(QIODevice::ReadOnly)) {
        error = QStringLiteral("Cannot prepare unsaved-move verification"); return false;
    }
    const QByteArray bytes = original.readAll(); original.close();
    bool cancelled = false;
    QTimer response;
    QObject::connect(&response, &QTimer::timeout, &window, [&] {
        if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
            if (auto* cancel = box->button(QMessageBox::Cancel)) { cancelled = true; cancel->click(); }
            else box->reject();
        }
    });
    response.start(20); library->moveDocumentTo(source, target.path()); response.stop();
    if (!cancelled || controller->getDocument() != document || controller->getOriginalFileName() != source ||
        !QDir(target.path()).entryList(QDir::Files | QDir::Hidden).isEmpty() ||
        !original.open(QIODevice::ReadOnly) || original.readAll() != bytes) {
        error = QStringLiteral("Cancelling unsaved PDF movement changed document state or files"); return false;
    }
    return true;
}

bool verifyLibraryUi(pdfviewer::PDFEditorMainWindow& window, QString& error, const QString& capturePath) {
    if (!verifyReaderToolbar(window, error, false)) return false;
    auto fail = [&error](const char* message) {
        error = QString::fromLatin1(message);
        qWarning("LUNA UI smoke: %s", message);
        return false;
    };
    auto* split = window.findChild<QSplitter*>(QStringLiteral("LunaLibrarySplitter"));
    auto* navigation = window.findChild<QWidget*>(QStringLiteral("LunaNavigation"));
    auto* documents = window.findChild<QWidget*>(QStringLiteral("LunaDocuments"));
    auto* search = window.findChild<QLineEdit*>(QStringLiteral("LunaGlobalSearch"));
    auto* pages = window.findChild<QStackedWidget*>(QStringLiteral("LunaDocumentPages"));
    auto* dock = window.findChild<QDockWidget*>(QStringLiteral("LunaLibraryDock"));
    if (!split || split->count() != 2 || !navigation || !documents || !search || !pages || !dock)
        return fail("Required native library widgets missing");
    for (auto* other : window.findChildren<QDockWidget*>()) {
        if (other != dock && other->isVisible()) return fail("Unexpected extra PDF sidebar");
    }
    if (window.styleSheet().isEmpty()) return fail("Theme resource missing");
    auto* toolbar = window.findChild<QToolBar*>(QStringLiteral("mainToolBar"));
    if (!toolbar || !toolbar->isVisible() || !window.getProgramController()->getPdfWidget()->isVisible())
        return fail("Native PDF toolbar or renderer widget is hidden");
    if (navigation->width() < 190 || documents->width() < 300 || window.getProgramController()->getPdfWidget()->width() < 300)
        return fail("Library/PDF geometry does not fit the test window");
    auto* list = pages->widget(0)->findChild<QListWidget*>();
    if (!list || list->count() != 1) return fail("Synthetic library must contain exactly one PDF");
    auto* item = list->item(0);
    int searchBindings = 0;
    const QKeySequence searchKey(QStringLiteral("Ctrl+K"));
    for (auto* action : window.findChildren<QAction*>())
        if (action->shortcuts().contains(searchKey)) ++searchBindings;
    if (searchBindings != 1) return fail("Ctrl+K has missing or conflicting action bindings");
    auto* draw = window.getProgramController()->getPdfWidget()->getDrawWidget()->getWidget();
    window.activateWindow();
    QCoreApplication::processEvents();
    draw->setFocus();
    QKeyEvent press(QEvent::KeyPress, Qt::Key_K, Qt::ControlModifier);
    QKeyEvent release(QEvent::KeyRelease, Qt::Key_K, Qt::ControlModifier);
    QApplication::sendEvent(draw, &press);
    QApplication::sendEvent(search, &release);
    if (!search->hasFocus()) return fail("Native Ctrl+K did not focus library search");
    search->setText(QStringLiteral("LUNA_NO_MATCH_742"));
    if (!item->isHidden()) return fail("Filename filtering did not hide unmatched PDF");
    search->clear();
    if (item->isHidden()) return fail("Clearing search did not restore PDF");
    list->setCurrentItem(item);
    auto* favorite = list->findChild<QAction*>(QStringLiteral("LunaToggleFavorite"));
    if (!favorite) return fail("Favorite action missing");
    const QString path = item->data(Qt::UserRole).toString();
    favorite->trigger();
    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("LUNA"), QStringLiteral("LUNA Library"));
    if (!item->data(Qt::UserRole + 2).toBool() || !settings.value(QStringLiteral("library/favorites")).toStringList().contains(path))
        return fail("Favorite toggle did not persist actual file path");
    auto* favoritesButton = window.findChild<QPushButton*>(QStringLiteral("LunaFavoriteDocumentsButton"));
    auto* recentButton = window.findChild<QPushButton*>(QStringLiteral("LunaRecentDocumentsButton"));
    auto* folderButton = window.findChild<QPushButton*>(QStringLiteral("LunaFolderDocumentsButton"));
    if (!favoritesButton || !recentButton || !folderButton) return fail("Navigation buttons missing");
    favoritesButton->click();
    auto* history = pages->currentWidget()->findChild<QListWidget*>();
    if (!history || history->count() != 1 || history->item(0)->data(Qt::UserRole).toString() != path)
        return fail("Favorite navigation did not use real saved data");
    folderButton->click();
    favorite->trigger();
    if (item->data(Qt::UserRole + 2).toBool()) return fail("Favorite removal failed");
    recentButton->click();
    history = pages->currentWidget()->findChild<QListWidget*>();
    if (!history || history->count() != 1 || history->item(0)->data(Qt::UserRole).toString() != path)
        return fail("Successful PDF open not recorded in recent history");
    folderButton->click();
    const QList<QPair<QString, QWidget*>> toggles = {
        {QStringLiteral("LunaToggleNavigation"), navigation}, {QStringLiteral("LunaToggleDocuments"), documents},
        {QStringLiteral("LunaFocusMode"), dock}
    };
    for (const auto& entry : toggles) {
        auto* action = window.findChild<QAction*>(entry.first);
        if (!action) return fail("Panel visibility action missing");
        action->trigger();
        if (!entry.second->isHidden()) return fail("Panel did not collapse");
        action->trigger();
        if (entry.second->isHidden()) return fail("Panel did not reopen");
    }
    // Exercise the real viewport crop and asynchronous hover in the temporary
    // synthetic library. Nothing here reads or modifies a user's library.
    auto* library = dynamic_cast<LibraryPanel*>(dock->widget());
    if (!library) return fail("Library panel type missing");
    auto wait = [](const std::function<bool()>& ready) {
        if (ready()) return true;
        QEventLoop loop;
        QTimer poll, timeout;
        bool success = false;
        QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
            // This verifier runs inside the smoke timer's callback. Nested
            // event loops do not normally drain DeferredDelete from that outer
            // callback; explicitly give WA_DeleteOnClose its normal event turn.
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            if (ready()) { success = true; loop.quit(); }
        });
        QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        poll.start(20); timeout.setSingleShot(true); timeout.start(5000); loop.exec();
        return success;
    };
    if (!wait([&] { return window.isEnabled() && window.getProgramController()->canClose(); }))
        return fail("Reader busy before crop verification");
    const QFileInfo pdf(path);
    const QString thumbnail = pdf.absolutePath() + QLatin1Char('/') + pdf.completeBaseName() + QStringLiteral(".luna-thumb.png");
    if (QFileInfo::exists(thumbnail)) return fail("Synthetic crop destination already exists");
    library->startThumbnailCrop();
    if (!library->cropOverlay_) return fail("Native crop overlay did not open");
    QKeyEvent cancel(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(library->cropOverlay_, &cancel);
    if (QFileInfo::exists(thumbnail)) return fail("Crop cancellation wrote an image");
    if (library->cropOverlay_ && library->cropOverlay_->isVisible()) return fail("Crop cancellation left a visible overlay");
    if (!wait([&] { return !library->cropOverlay_; })) return fail("Cancelled crop overlay was not deleted");
    library->startThumbnailCrop();
    auto* crop = library->cropOverlay_.data();
    if (!crop || crop->parentWidget() != draw) return fail("Crop does not target the native PDF viewport");
    auto mouse = [](QWidget* target, QEvent::Type type, QPoint point, Qt::MouseButton button, Qt::MouseButtons buttons) {
        QMouseEvent event(type, QPointF(point), QPointF(target->mapToGlobal(point)), button, buttons, Qt::NoModifier);
        QApplication::sendEvent(target, &event);
    };
    const QPoint from(50, 80), to(210, 240);
    if (!draw->rect().contains(to)) return fail("Crop fixture exceeds viewport");
    mouse(crop, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
    mouse(crop, QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton);
    mouse(crop, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
    if (!wait([&] { return QFileInfo::exists(thumbnail) && !library->cropOverlay_; }))
        return fail("Native crop did not atomically save its PNG");
    const QImage captured(thumbnail);
    const QSize expected = QRect(from, to).size() * draw->devicePixelRatioF();
    if (captured.isNull() || captured.size() != expected) return fail("Native crop PNG has wrong dimensions");
    const QPoint card = list->visualItemRect(item).center();
    mouse(list->viewport(), QEvent::MouseMove, card, Qt::NoButton, Qt::NoButton);
    if (!wait([&] { return library->thumbnailPopup_ && library->thumbnailPopup_->isVisible() && library->fade_->state() == QAbstractAnimation::Stopped; }))
        return fail("Asynchronous thumbnail hover did not finish fading in");
    if (library->fade_->duration() != 165 || library->thumbnailImage_->pixmap().isNull())
        return fail("Thumbnail hover image or fade duration is incorrect");
    auto* screen = library->thumbnailPopup_->screen();
    if (!screen || !screen->availableGeometry().contains(library->thumbnailPopup_->frameGeometry()))
        return fail("Thumbnail hover extends outside the available screen");
    if (!capturePath.isEmpty() && !library->thumbnailPopup_->grab().save(capturePath + QStringLiteral(".hover.png")))
        return fail("Could not capture native hover widget");
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(list->viewport(), &leave);
    if (library->thumbnailPopup_->isVisible()) return fail("Hover remained visible after leaving list");
    ThumbnailCache::instance()->invalidate();
    mouse(list->viewport(), QEvent::MouseMove, card, Qt::NoButton, Qt::NoButton);
    library->hideThumbnail(); // Same cancellation used by folder switching.
    QCoreApplication::processEvents();
    if (library->thumbnailPopup_->isVisible() || !library->hoverRequest_.isEmpty())
        return fail("Cancelled hover request resurfaced");
    // Move the opened synthetic document out and back. The real controller
    // releases/reopens it; both original PDF bytes and the sidecar must survive.
    QFile beforePdf(path), beforePng(thumbnail);
    if (!beforePdf.open(QIODevice::ReadOnly) || !beforePng.open(QIODevice::ReadOnly))
        return fail("Cannot read move verification fixtures");
    const QByteArray pdfBytes = beforePdf.readAll(), pngBytes = beforePng.readAll();
    beforePdf.close(); beforePng.close();
    const QString movedFolder = pdf.absolutePath() + QStringLiteral("/Synthetic examples");
    const QString movedPath = QDir(movedFolder).filePath(pdf.fileName());
    const QString movedThumbnail = QDir(movedFolder).filePath(QFileInfo(thumbnail).fileName());
    auto reopened = [&](const QString& expectedPath) {
        return wait([&] {
            auto* c = window.getProgramController();
            return window.isEnabled() && c->getDocument() &&
                QFileInfo(c->getOriginalFileName()).canonicalFilePath() == QFileInfo(expectedPath).canonicalFilePath();
        });
    };
    library->moveDocumentTo(path, movedFolder);
    if (!reopened(movedPath) || QFileInfo::exists(path) || QFileInfo::exists(thumbnail) || !QFileInfo::exists(movedThumbnail))
        return fail("Opened PDF and sidecar did not move/reopen together");
    library->moveDocumentTo(movedPath, pdf.absolutePath());
    if (!reopened(path) || QFileInfo::exists(movedPath) || QFileInfo::exists(movedThumbnail))
        return fail("Opened PDF did not return to its library folder");
    if (!beforePdf.open(QIODevice::ReadOnly) || !beforePng.open(QIODevice::ReadOnly) ||
        beforePdf.readAll() != pdfBytes || beforePng.readAll() != pngBytes)
        return fail("PDF/thumbnail bytes changed during paired movement");
    beforePdf.close(); beforePng.close();
    if (!wait([&] { return list->count() == 1; })) return fail("Library list did not refresh after movement");
    item = list->item(0); // Folder refresh replaced the previous item.
    {
        QTemporaryDir many(pdf.absolutePath() + QStringLiteral("/luna-library-scale-XXXXXX"));
        if (!many.isValid()) return fail("Cannot create synthetic 1000-PDF library");
        for (int i = 0; i < 1000; ++i) {
            const QString name = QStringLiteral("합성 논문 %1").arg(i, 4, 10, QLatin1Char('0'));
            if (!QFile::copy(path, many.path() + QLatin1Char('/') + name + QStringLiteral(".pdf")))
                return fail("Cannot create synthetic library fixtures");
            if (i < 40 && !QFile::copy(thumbnail, many.path() + QLatin1Char('/') + name + QStringLiteral(".luna-thumb.png")))
                return fail("Cannot create synthetic list thumbnails");
        }
        QElapsedTimer heartbeatClock, loadClock;
        heartbeatClock.start(); loadClock.start();
        qint64 longestGap = 0;
        int beats = 0;
        QTimer heartbeat;
        QObject::connect(&heartbeat, &QTimer::timeout, &window, [&] {
            longestGap = qMax(longestGap, heartbeatClock.restart()); ++beats;
        });
        heartbeat.start(10);
        library->selectFolder(many.path());
        if (!wait([&] { return list->count() == 1000 && !library->folderTimer_->isActive(); }))
            return fail("1000-PDF library did not finish incremental discovery");
        const qint64 loadMs = loadClock.elapsed();
        QElapsedTimer searchClock; searchClock.start();
        search->setText(QStringLiteral("합성 논문 0999"));
        int visible = 0;
        for (int i = 0; i < list->count(); ++i) if (!list->item(i)->isHidden()) ++visible;
        const qint64 searchMs = searchClock.elapsed();
        if (visible != 1 || searchMs > 1000) return fail("1000-PDF filename search failed or blocked the UI");
        search->clear(); list->scrollToBottom();
        if (!wait([&] { return beats >= 3; })) return fail("Library loading starved GUI events");
        heartbeat.stop();
        if (longestGap > 1000) return fail("Library GUI event gap exceeded one second");
        if (QFileInfo(window.getProgramController()->getOriginalFileName()).canonicalFilePath() != QFileInfo(path).canonicalFilePath())
            return fail("Library discovery opened an unrelated PDF");
        qInfo() << "LUNA synthetic 1000-PDF library: load ms" << loadMs
                << "search ms" << searchMs << "maximum GUI timer gap ms" << longestGap;
        library->selectFolder(pdf.absolutePath());
        if (!wait([&] { return list->count() == 1 && !library->folderTimer_->isActive(); }))
            return fail("Library did not recover after large-folder switching");
        item = list->item(0);
    }
    // Drive the same signal/slot used by double-click, including the save guard.
    window.getProgramController()->closeDocument();
    list->itemDoubleClicked(item);
    return true;
}

} // namespace luna
