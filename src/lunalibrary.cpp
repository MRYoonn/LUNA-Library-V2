// SPDX-License-Identifier: MIT
// LUNA Library V2 UI overlay. PDF rendering remains upstream PDF4QT.
#include "lunalibrary.h"
#include "lunathumbnailcache.h"
#include "lunafilemove.h"
#include "lunadocumentclaim.h"
#include "lunabibliography.h"
#include "lunafirstpage.h"
#include "lunaperformance.h"
#include <QThread>
#include "pdfdocument.h"
#include "pdfcompiler.h"
#include "pdftextlayout.h"
#include "lunareadertoolbar.h"
#include "pdfeditormainwindow.h"
#include "pdfprogramcontroller.h"
#include "pdfdrawwidget.h"
#include "pdfdrawspacecontroller.h"
#include "pdfitemmodels.h"
#include "lunapagegrid.h"
#include <QListView>
#include <QTabBar>
#include <QResizeEvent>

#include <QAbstractItemView>
#include <QApplication>
#include <QAbstractButton>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QCryptographicHash>
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
#include <QTextLayout>
#include <QLinearGradient>
#include <QSet>
#include <QHash>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QFileIconProvider>
#include <QProxyStyle>
#include <QStyleFactory>
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
#include <QSignalBlocker>
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
#include <algorithm>
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
constexpr const char* kCANVAS = "#F3F6FC";
constexpr const char* kPANEL = "#FFFFFF";
constexpr const char* kSELECTION = "#E7EFFF";
constexpr const char* kLINE = "#E0E7F1";
constexpr const char* kTEXT = "#263247";
constexpr const char* kMUTED = "#77839A";
constexpr const char* kTHUMB_LINE = "#C8CFDD";
constexpr const char* kSTAR_OFF = "#A2ACBF";
constexpr const char* kHOVER = "#F0F5FD";
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

// Size the actual menu hit rectangles, then center their glyphs. QSS item
// min-height is ignored by QMenuBar on some native styles.
class MenuBarStyle final : public QProxyStyle {
public:
    MenuBarStyle() : QProxyStyle(QStyleFactory::create(QApplication::style()->objectName())) {}
    QSize sizeFromContents(ContentsType type, const QStyleOption* option, const QSize& contents, const QWidget* widget = nullptr) const override {
        if (type == CT_MenuBarItem) return {contents.width() + 18, 42};
        return QProxyStyle::sizeFromContents(type, option, contents, widget);
    }
    int pixelMetric(PixelMetric metric, const QStyleOption* option = nullptr, const QWidget* widget = nullptr) const override {
        if (metric == PM_MenuBarVMargin) return 3;
        if (metric == PM_MenuBarHMargin) return 0;
        if (metric == PM_MenuBarItemSpacing || metric == PM_MenuBarPanelWidth) return 0;
        return QProxyStyle::pixelMetric(metric, option, widget);
    }
    void drawPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget = nullptr) const override {
        if (element == PE_PanelMenuBar) { QLinearGradient g(0, 0, 0, 48); g.setColorAt(0, QColor("#F8FBFF")); g.setColorAt(1, QColor("#EFF5FC")); painter->fillRect(option->rect, g); return; }
        QProxyStyle::drawPrimitive(element, option, painter, widget);
    }
    void drawControl(ControlElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget = nullptr) const override {
        if (element == CE_MenuBarEmptyArea) { QLinearGradient g(0, 0, 0, 48); g.setColorAt(0, QColor("#F8FBFF")); g.setColorAt(1, QColor("#EFF5FC")); painter->fillRect(option->rect, g); return; }
        if (element == CE_MenuBarItem) if (const auto* item = qstyleoption_cast<const QStyleOptionMenuItem*>(option)) {
            painter->save(); painter->setRenderHint(QPainter::Antialiasing);
            QLinearGradient g(0, 0, 0, 48); g.setColorAt(0, QColor("#F8FBFF")); g.setColorAt(1, QColor("#EFF5FC")); painter->fillRect(item->rect, g);
            if (item->state & (State_Selected | State_MouseOver | State_Sunken)) {
                painter->setPen(Qt::NoPen); painter->setBrush(QColor(kSELECTION)); painter->drawRoundedRect(item->rect, 4, 4);
            }
            painter->setFont(item->font); QPalette palette = item->palette;
            palette.setColor(QPalette::WindowText, QColor(kNAVY)); palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(kMUTED));
            const int mnemonic = styleHint(SH_UnderlineShortcut, item, widget) ? Qt::TextShowMnemonic : Qt::TextHideMnemonic;
            drawItemText(painter, item->rect, Qt::AlignCenter | Qt::TextSingleLine | mnemonic, palette, item->state & State_Enabled, item->text, QPalette::WindowText);
            painter->restore(); return;
        }
        QProxyStyle::drawControl(element, option, painter, widget);
    }
};

constexpr int kScanBatch = 90;
constexpr int kMaximumEntries = 15000;

class ThumbnailCropOverlay final : public QWidget {
public:
    using Finished = std::function<void(const QRect&)>;
    ThumbnailCropOverlay(QWidget* documentViewport, Finished callback)
        : QWidget(documentViewport), callback_(std::move(callback)) {
        setObjectName(QStringLiteral("LunaThumbnailCrop"));
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

QString openedDateKey(const QString& path) {
    return QString::fromLatin1(QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha256).toHex());
}

class DocumentCard final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override { return {300, 96}; }
    std::function<void(const QString&, const QString&)> saveMemo;
    static QRect memoRect(const QRect& r) { return QRect(r.left() + 108, r.top() + 77, qMax(20, r.width() - 158), 18); }
    static QRect favoriteRect(const QRect& r) { return QRect(r.right() - 37, r.top() + (r.height() - 24) / 2, 22, 24); }
    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem&, const QModelIndex&) const override {
        auto* editor = new QLineEdit(parent);
        editor->setObjectName(QStringLiteral("LunaPaperMemoEditor"));
        editor->setPlaceholderText(QStringLiteral("논문 한 줄 메모"));
        editor->setAccessibleName(QStringLiteral("논문 한 줄 메모"));
        return editor;
    }
    void setEditorData(QWidget* editor, const QModelIndex& index) const override {
        auto* input = static_cast<QLineEdit*>(editor);
        input->setText(index.data(Qt::UserRole + 4).toString()); input->selectAll();
    }
    void setModelData(QWidget* editor, QAbstractItemModel*, const QModelIndex& index) const override {
        if (saveMemo) saveMemo(index.data(Qt::UserRole).toString(), static_cast<QLineEdit*>(editor)->text());
    }
    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex&) const override {
        editor->setGeometry(memoRect(option.rect).adjusted(18, 0, 0, 0));
    }
    // Editing is intentionally NOT opened by a left click or double-click.
    // Use the explicit right-click "메모 편집…" command to avoid accidents.
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        painter->save();
        const QRect r = option.rect;
        const bool selected = option.state & QStyle::State_Selected;
        const bool activeDocument = index.data(Qt::UserRole + 7).toBool();
        painter->fillRect(r, QColor(activeDocument ? "#EAF3FF" :
            selected ? "#E4EDFC" : kPANEL));
        if (activeDocument) painter->fillRect(QRect(r.left(), r.top(), 3, r.height()), QColor("#79A6DF"));
        if (!activeDocument && !selected && (option.state & QStyle::State_MouseOver))
            painter->fillRect(r, QColor("#F6F9FF"));
        painter->setPen(QColor(kLINE));
        painter->drawLine(r.bottomLeft(), r.bottomRight());
        const QRect paper(r.left() + 11, r.top() + 15, 88, 65);
        painter->setBrush(Qt::white);
        painter->drawRoundedRect(paper, 4, 4);
        readerIcon(QStringLiteral("documents")).paint(painter, QRect(paper.center() - QPoint(14, 20), QSize(28, 32)));
        QFont captionFont = option.font;
        captionFont.setFamily(QStringLiteral("Malgun Gothic")); captionFont.setPixelSize(12);
        painter->setFont(captionFont); painter->setPen(QColor(kMUTED));
        painter->drawText(paper.adjusted(0, 39, 0, 0), Qt::AlignHCenter | Qt::AlignTop, QStringLiteral("PDF"));
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
        title.setPixelSize(13);
        title.setWeight(QFont::DemiBold);
        painter->setFont(title);
        painter->setPen(QColor(kNAVY));
        const QRect text(r.left() + 108, r.top() + 6, qMax(20, r.width() - 158), 32);
        painter->save();
        painter->setClipRect(text, Qt::IntersectClip);
        QTextLayout titleLayout(index.data().toString(), title);
        QTextOption titleOption; titleOption.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere); titleLayout.setTextOption(titleOption);
        titleLayout.beginLayout();
        for (int row = 0; row < 2; ++row) {
            auto line = titleLayout.createLine(); if (!line.isValid()) break;
            line.setLineWidth(text.width()); line.setPosition(QPointF(0, row * 16));
        }
        titleLayout.endLayout(); titleLayout.draw(painter, text.topLeft());
        painter->restore();
        painter->setFont(captionFont);
        painter->setPen(QColor(kMUTED));
        const QVariantMap bibliography = index.data(Qt::UserRole + 5).toMap();
        const QString citation = luna::Bibliography::citation(bibliography);
        painter->drawText(QRect(text.left(), r.top() + 40, text.width(), 15),
            Qt::AlignLeft | Qt::AlignVCenter,
            QFontMetrics(captionFont).elidedText(
                citation.isEmpty() ? QStringLiteral("저널·연도 미확인") : citation,
                Qt::ElideRight, text.width()));
        // Full-width author row, with a font-metrics-safe height. A fixed
        // 15px rectangle previously clipped descenders on Windows DPI scaling.
        const QString authors = luna::Bibliography::lastListedAuthor(bibliography.value("authors").toString());
        const QRect authorArea(text.left(), r.top() + 57, text.width(),
            qMax(20, QFontMetrics(captionFont).height() + 3));
        painter->setPen(QColor(kMUTED));
        painter->drawText(authorArea, Qt::AlignLeft | Qt::AlignVCenter,
            QFontMetrics(captionFont).elidedText(
                authors.isEmpty() ? QStringLiteral("저자 미확인") : authors,
                Qt::ElideRight, authorArea.width()));
        const QString memo = index.data(Qt::UserRole + 4).toString();
        painter->setPen(QColor(memo.isEmpty() ? "#A0ACBD" : "#48648A"));
        const QRect note = memoRect(r);
        readerIcon(QStringLiteral("memo-inline")).paint(painter, QRect(note.left(), note.center().y() - 7, 14, 14),
            Qt::AlignCenter, QIcon::Normal, QIcon::Off);
        painter->drawText(note.adjusted(18, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter,
            QFontMetrics(captionFont).elidedText(memo.isEmpty() ? QStringLiteral("+ 메모 추가") : memo, Qt::ElideRight, note.width() - 18));
        readerIcon(QStringLiteral("star")).paint(painter, favoriteRect(r).adjusted(2, 2, -2, -2), Qt::AlignCenter,
            index.data(Qt::UserRole + 2).toBool() ? QIcon::Selected : QIcon::Disabled);
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
        // The public research smoke enters through the standalone Editor,
        // not NativeTabs::open(). Mark its draw widget too, otherwise the
        // first-page-paint metric is silently absent for 209-page SI probes.
        if (qEnvironmentVariableIsSet("LUNA_PERF")) {
            auto* draw = window_.getProgramController()->getPdfWidget()->getDrawWidget()->getWidget();
            draw->setProperty("LunaOpenStart", performanceNow());
        }
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

class FolderIcons final : public QObject, public QFileIconProvider {
public:
    explicit FolderIcons(QObject* parent) : QObject(parent), folder_(readerIcon(QStringLiteral("folder"))) {}
    QIcon icon(IconType type) const override { return type == Folder ? folder_ : QFileIconProvider::icon(type); }
    QIcon icon(const QFileInfo& info) const override { return info.isDir() ? folder_ : QFileIconProvider::icon(info); }
private:
    QIcon folder_;
};

class LibraryPanel final : public QWidget {
    friend bool luna::verifyBibliographyUi(pdfviewer::PDFEditorMainWindow&, QString&, const QString&);
    friend bool luna::verifyLibraryUi(pdfviewer::PDFEditorMainWindow&, QString&, const QString&);
    friend bool luna::verifyCancelledLibraryMove(pdfviewer::PDFEditorMainWindow&, QString&);
public:
    explicit LibraryPanel(pdfviewer::PDFEditorMainWindow* viewer, QWidget* parent)
        : QWidget(parent), viewer_(viewer), bibliography_(this)
    {
        setObjectName(QStringLiteral("LunaLibraryPanel"));
        const QStringList favoritePaths = savedPaths(QStringLiteral("library/favorites"));
        favorites_ = QSet<QString>(favoritePaths.cbegin(), favoritePaths.cend());
        QSettings dates(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("LUNA"), QStringLiteral("LUNA Library"));
        dates.beginGroup(QStringLiteral("library/openedDates"));
        for (const QString& key : dates.childKeys()) openedDates_.insert(key, dates.value(key).toString());
        dates.endGroup(); dates.beginGroup(QStringLiteral("library/memos"));
        for (const QString& key : dates.childKeys()) memos_.insert(key, dates.value(key).toString());
        dates.endGroup(); // Do not load legacy tags; leave stored preferences intact for rollback.
        // Read existing date keys without migrating memos or changing their keys.
        for (const QString& path : savedPaths(QStringLiteral("library/recent"))) {
            const QString date = openedDates_.value(openedDateKey(path));
            if (!date.isEmpty()) rememberDateAlias(path, date);
        }
        setMinimumWidth(482);
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(7, 7, 0, 7);
        split_ = new QSplitter(Qt::Horizontal, this);
        split_->setObjectName(QStringLiteral("LunaLibrarySplitter"));
        split_->setChildrenCollapsible(false);
        split_->setHandleWidth(5);
        layout->addWidget(split_);

        auto* navigationScroll = new QScrollArea(split_);
        navigationScroll->setWidgetResizable(true);
        navigationScroll->setFrameShape(QFrame::NoFrame);
        navigationScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        navigationScroll->setMinimumHeight(0);
        navigation_ = navigationScroll;
        navigation_->setObjectName(QStringLiteral("LunaNavigation"));
        navigation_->setMinimumWidth(225);
        navigation_->setMaximumWidth(300);
        auto* navigationContents = new QWidget(navigationScroll);
        navigationContents->setObjectName(QStringLiteral("LunaNavigationContents"));
        navigationScroll->setWidget(navigationContents);
        navigationContents->setAutoFillBackground(false);
        navigationScroll->viewport()->setAutoFillBackground(false);
        navigationScroll->viewport()->setObjectName(QStringLiteral("LunaNavigationViewport"));
        auto* nav = new QVBoxLayout(navigationContents);
        nav->setSizeConstraint(QLayout::SetMinimumSize);
        nav->setContentsMargins(10, 0, 10, 12);
        nav->setSpacing(0);
        auto* libraryHeader = new QWidget(navigation_);
        libraryHeader->setFixedHeight(48);
        auto* libraryHeaderLayout = new QHBoxLayout(libraryHeader);
        libraryHeaderLayout->setContentsMargins(7, 0, 0, 0);
        libraryHeaderLayout->setSpacing(9);
        auto* shelf = new QLabel(libraryHeader);
        shelf->setPixmap(readerIcon(QStringLiteral("library")).pixmap(24, 24));
        shelf->setAccessibleName(QStringLiteral("라이브러리"));
        shelf->setFixedSize(24, 24);
        libraryHeaderLayout->addWidget(shelf);
        auto* section = new QLabel(QStringLiteral("라이브러리"), libraryHeader);
        section->setObjectName(QStringLiteral("LunaLibraryTitle"));
        QFont headingFont(QStringLiteral("Malgun Gothic")); headingFont.setPixelSize(17); headingFont.setBold(true); headingFont.setLetterSpacing(QFont::AbsoluteSpacing, -.45); section->setFont(headingFont);
        libraryHeaderLayout->addWidget(section, 1);
        nav->addWidget(libraryHeader);
        auto* group = new QButtonGroup(this);
        auto navButton = [this, nav, group](const QString& text, const QString& icon) {
            auto* b = new QPushButton(readerIcon(icon), text, navigation_);
            b->setObjectName(QStringLiteral("LunaNavigationButton"));
            b->setIconSize(QSize(28, 19));
            b->setCheckable(true);
            b->setMinimumHeight(34);
            group->addButton(b);
            nav->addWidget(b);
            return b;
        };
        pdfTab_ = navButton(QStringLiteral("전체 문서"), QStringLiteral("documents"));
        folderTab_ = navButton(QStringLiteral("선택한 폴더"), QStringLiteral("folder"));
        auto* favorites = navButton(QStringLiteral("즐겨찾기"), QStringLiteral("star"));
        auto* recent = navButton(QStringLiteral("최근 열람"), QStringLiteral("recent"));
        pdfTab_->setObjectName(QStringLiteral("LunaAllDocumentsButton"));
        folderTab_->setObjectName(QStringLiteral("LunaFolderDocumentsButton"));
        recent->setObjectName(QStringLiteral("LunaRecentDocumentsButton"));
        favorites->setObjectName(QStringLiteral("LunaFavoriteDocumentsButton"));
        for (auto* b : {pdfTab_, folderTab_, recent, favorites}) b->setProperty("lunaNavigation", true);
        folderTab_->setChecked(true);
        auto badge = [](QPushButton* button) {
            auto* layout = new QHBoxLayout(button); layout->setContentsMargins(0, 0, 9, 0); layout->addStretch();
            auto* label = new QLabel(button); label->setObjectName(QStringLiteral("LunaNavigationCount"));
            label->setAttribute(Qt::WA_TransparentForMouseEvents); layout->addWidget(label); return label;
        };
        allBadge_ = badge(pdfTab_); folderBadge_ = badge(folderTab_); recentBadge_ = badge(recent); favoriteBadge_ = badge(favorites);
        recentCount_ = savedPaths(QStringLiteral("library/recent")).size();
        nav->addSpacing(23);
        nav->removeWidget(folderTab_);
        auto* folders = new QHBoxLayout;
        auto* folderHeading = new QLabel(QStringLiteral("내 폴더"), navigation_);
        folderHeading->setObjectName(QStringLiteral("LunaSection"));
        folders->addWidget(folderHeading, 1);
        auto* addFolder = new QToolButton(navigation_);
        addFolder->setIcon(readerIcon(QStringLiteral("add")));
        addFolder->setAccessibleName(QStringLiteral("새 폴더 만들기"));
        addFolder->setToolTip(QStringLiteral("선택한 위치에 새 폴더 만들기"));
        addFolder->setObjectName(QStringLiteral("LunaTool"));
        folders->addWidget(addFolder);
        nav->addLayout(folders);
        rootLabel_ = new QLabel(navigation_);
        rootLabel_->setObjectName(QStringLiteral("LunaSecondary"));
        rootLabel_->setWordWrap(true);
        rootLabel_->setTextFormat(Qt::PlainText);
        rootLabel_->hide();
        nav->addWidget(folderTab_);
        model_ = new QFileSystemModel(this);
        model_->setReadOnly(true); model_->setIconProvider(new FolderIcons(model_));
        model_->setFilter(QDir::AllDirs | QDir::NoDotAndDotDot);
        tree_ = new QTreeView(navigation_);
        tree_->setModel(model_);
        tree_->setHeaderHidden(true);
        tree_->setAnimated(true);
        tree_->setIndentation(16); tree_->setUniformRowHeights(true); tree_->setIconSize(QSize(18, 18));
        tree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        for (int i = 1; i < 4; ++i) tree_->hideColumn(i);
        nav->addWidget(tree_, 1);
        auto* location = new QPushButton(readerIcon(QStringLiteral("open")), QString(), libraryHeader);
        location->setFixedSize(30, 30);
        location->setToolTip(QStringLiteral("라이브러리 위치 선택…"));
        location->setAccessibleName(QStringLiteral("라이브러리 위치 선택"));
        location->setObjectName(QStringLiteral("LunaLocation"));
        libraryHeaderLayout->addWidget(location);

        documents_ = new QWidget(split_);
        documents_->setObjectName(QStringLiteral("LunaDocuments"));
        documents_->setMinimumWidth(325);
        auto* docs = new QVBoxLayout(documents_);
        docs->setContentsMargins(0, 0, 0, 0);
        docs->setSpacing(0);
        auto* heading = new QWidget(documents_);
        auto* headingLayout = new QVBoxLayout(heading);
        headingLayout->setContentsMargins(12, 0, 12, 0);
        headingLayout->setSpacing(0);
        auto* title = new QLabel(QStringLiteral("논문 목록"), heading);
        title->setObjectName(QStringLiteral("LunaLibraryTitle"));
        title->setFont(headingFont);
        title->setFixedHeight(48);
        auto* titleRow = new QHBoxLayout;
        titleRow->setObjectName(QStringLiteral("LunaPaperHeadingLayout"));
        titleRow->addWidget(title, 1);
        headingLayout->addLayout(titleRow);
        auto* addFile = new QPushButton(readerIcon(QStringLiteral("file-add")), QString(), heading);
        addFile->setFixedSize(30, 30);
        addFile->setObjectName(QStringLiteral("LunaPrimary"));
        addFile->setMinimumHeight(28);
        addFile->setAccessibleName(QStringLiteral("라이브러리에 PDF 파일 추가"));
        addFile->setToolTip(QStringLiteral("현재 폴더에 복사합니다. 원본은 유지하며 같은 이름의 파일은 덮어쓰지 않습니다."));
        titleRow->addWidget(addFile);
        query_ = new QLineEdit(heading);
        query_->setObjectName(QStringLiteral("LunaGlobalSearch"));
        query_->setPlaceholderText(QStringLiteral("파일명 검색…  Ctrl+K"));
        query_->setClearButtonEnabled(true);
        query_->setMinimumHeight(36);
        nav->addWidget(query_);
        // Reading-first layout: the rarely used page thumbnails belong in
        // View, not in a second 45-pixel tab row above every paper list.
        docs->addWidget(heading);
        pages_ = new QStackedWidget(documents_);
        pages_->setObjectName(QStringLiteral("LunaDocumentPages"));
        auto* content = new QStackedWidget(documents_);
        content->setObjectName(QStringLiteral("LunaContentStack"));
        content->addWidget(pages_);
        docs->addWidget(content, 1);
        pagePreviewAction_ = new QAction(QStringLiteral("페이지 미리보기"), this);
        pagePreviewAction_->setObjectName(QStringLiteral("LunaPagePreview"));
        pagePreviewAction_->setCheckable(true);
        pagePreviewAction_->setChecked(false);
        pagePreviewAction_->setToolTip(QStringLiteral("필요할 때만 페이지 썸네일을 표시합니다"));
        connect(pagePreviewAction_, &QAction::toggled, content,
            [content](bool enabled) { content->setCurrentIndex(enabled ? 1 : 0); });
        if (auto* viewMenu = viewer_->findChild<QMenu*>(QStringLiteral("menuView")))
            viewMenu->addAction(pagePreviewAction_);
        if (auto* native = viewer_->findChild<QListView*>(QStringLiteral("thumbnailsListView"))) {
            QPointer<pdf::PDFThumbnailsItemModel> thumbnails(qobject_cast<pdf::PDFThumbnailsItemModel*>(native->model()));
            auto* grid = new PageGrid(*native, [this](int size) {
                if (auto* view = viewer_->findChild<QListView*>(QStringLiteral("thumbnailsListView")))
                    if (auto* model = qobject_cast<pdf::PDFThumbnailsItemModel*>(view->model())) model->setThumbnailsSize(size);
            }, content);
            content->addWidget(grid);
            connect(grid, &QListView::clicked, this, [this](const QModelIndex& index) {
                if (index.isValid()) viewer_->getProgramController()->getPdfWidget()->getDrawWidgetProxy()->goToPage(index.row());
            });
        } else {
            pagePreviewAction_->setEnabled(false);
            pagePreviewAction_->setToolTip(QStringLiteral("PDF4QT 페이지 미리보기 모델을 사용할 수 없습니다."));
        }
        auto page = [this](QListWidget*& list, QLabel*& count) {
            auto* p = new QWidget(pages_);
            auto* box = new QVBoxLayout(p);
            box->setContentsMargins(0, 0, 0, 0);
            box->setSpacing(0);
            count = new QLabel(p);
            count->setObjectName(QStringLiteral("LunaCount"));
            count->hide(); // Counts remain in the model/tooltips; no extra row above HTML cards.
            box->addWidget(count);
            list = makePdfList(p);
            box->addWidget(list, 1);
            pages_->addWidget(p);
        };
        page(folderPdfs_, folderCount_);
        page(allPdfs_, allCount_);
        page(historyPdfs_, historyCount_);
        for (auto* list : {folderPdfs_, allPdfs_, historyPdfs_}) {
            connect(list->model(), &QAbstractItemModel::rowsInserted, this, [this] { queueNavigationCounts(); });
            connect(list->model(), &QAbstractItemModel::rowsRemoved, this, [this] { queueNavigationCounts(); });
            connect(list->model(), &QAbstractItemModel::modelReset, this, [this] { queueNavigationCounts(); });
        }
        queueNavigationCounts();
        split_->setSizes({225, 325});
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
        auto* cropAction = new QAction(QStringLiteral("논문 목록 썸네일 크롭"), this);
        cropAction->setObjectName(QStringLiteral("LunaCropThumbnail"));
        cropAction->setEnabled(false);
        connect(cropAction, &QAction::triggered, this, [this] { startThumbnailCrop(); });
        cropAction_ = cropAction;
        if (auto* selectText = viewer_->findChild<QAction*>(QStringLiteral("actionSelectText"))) {
            auto updateCrop = [cropAction, selectText] { cropAction->setEnabled(selectText->isEnabled()); };
            cropConnection_ = connect(selectText, &QAction::changed, cropAction, updateCrop);
            updateCrop();
        }
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
        documentTitleConnection_ = connect(viewer_, &QWidget::windowTitleChanged, this, [this] {
            QTimer::singleShot(0, this, [this] { rememberOpenedDocument(); });
        });
        connect(split_, &QSplitter::splitterMoved, this, [this] { savePanelState(); });

        QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("LUNA"), QStringLiteral("LUNA Library"));
        QString start = settings.value(QStringLiteral("library/root")).toString();
        if (!QFileInfo(start).isDir()) start = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        if (!QFileInfo(start).isDir()) start = QDir::homePath();
        bibliography_.changed = [this](const QString& identity) { refreshBibliography(identity); };
        selectRoot(start);
        split_->restoreState(settings.value(QStringLiteral("library/splitter")).toByteArray());
    }

    std::function<void(const QString&)> openHandler;
    std::function<bool(const QString&)> activateElsewhere;
    std::function<bool(pdfviewer::PDFEditorMainWindow*, const QString&, const QString&)> movedHandler;
    QAction* cropAction() const { return cropAction_; }
    void bindEditor(pdfviewer::PDFEditorMainWindow& editor) {
        if (viewer_ == &editor) return;
        if (cropOverlay_) cropOverlay_->close();
        hideThumbnail();
        disconnect(documentTitleConnection_); disconnect(cropConnection_);
        viewer_ = &editor;
        if (auto* viewMenu = editor.findChild<QMenu*>(QStringLiteral("menuView")); viewMenu &&
            pagePreviewAction_ && !viewMenu->actions().contains(pagePreviewAction_))
            viewMenu->addAction(pagePreviewAction_);
        if (auto* grid = static_cast<PageGrid*>(findChild<QListView*>(QStringLiteral("LunaPageGrid"))))
            if (auto* native = editor.findChild<QListView*>(QStringLiteral("thumbnailsListView"))) grid->bindNative(*native);
        auto* select = editor.findChild<QAction*>(QStringLiteral("actionSelectText"));
        if (select) {
            auto update = [this, select] { cropAction_->setEnabled(select->isEnabled()); };
            cropConnection_ = connect(select, &QAction::changed, this, update); update();
        }
        documentTitleConnection_ = connect(&editor, &QWidget::windowTitleChanged, this, [this] {
            QTimer::singleShot(0, this, [this] { rememberOpenedDocument(); });
        });
        selectActiveCard();
    }
    void recordEditor(pdfviewer::PDFEditorMainWindow& editor) { rememberEditor(editor); }
    void selectActiveCard() {
        // Active-document color is independent of QListWidget selection.
        // Never call setCurrentItem: it scrolls to the item and makes the
        // paper list jump when opening tabs or changing the current PDF.
        QString active;
        if (viewer_ && viewer_->getProgramController() &&
            viewer_->getProgramController()->getDocument())
            active = DocumentClaim::identity(viewer_->getProgramController()->getOriginalFileName());
        for (auto* list : {folderPdfs_, allPdfs_, historyPdfs_}) {
            if (!list) continue;
            for (int row = 0; row < list->count(); ++row) {
                auto* item = list->item(row);
                const bool isActive = !active.isEmpty() &&
                    item->data(Qt::UserRole + 6).toString() == active;
                if (item->data(Qt::UserRole + 7).toBool() != isActive)
                    item->setData(Qt::UserRole + 7, isActive);
            }
        }
    }
    void updateMinimum() { setMinimumWidth((navigation_->isHidden() ? 0 : 225) + (documents_->isHidden() ? 0 : 325) + 12); }
    void toggleNavigation() { savePanelState(); navigation_->setVisible(navigation_->isHidden()); updateMinimum(); }
    void toggleDocuments() { savePanelState(); documents_->setVisible(documents_->isHidden()); updateMinimum(); }
    void showBothPanels() { navigation_->show(); documents_->show(); updateMinimum(); }
    void focusSearch() { showBothPanels(); query_->setFocus(); query_->selectAll(); }
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
    void refreshBibliography(const QString& identity) {
        for (auto* list : {folderPdfs_, allPdfs_, historyPdfs_}) {
            // Sorting can reorder rows when their visible titles change.
            QList<QListWidgetItem*> matching;
            for (int index = 0; index < list->count(); ++index) {
                auto* item = list->item(index);
                if (item->data(Qt::UserRole + 6).toString() == identity)
                    matching.append(item);
            }
            for (auto* item : matching) decorate(item, item->data(Qt::UserRole).toString());
            filter(list, query_->text());
        }
    }
    // Explicit user action, no modal dialog and no forced auto-save of
    // speculative title/author data. Trust only publication evidence found
    // on the opened document's first page.
    void extractBibliography(const QString& path) {
        const QString identity = DocumentClaim::identity(path);
        if (!viewer_ || !viewer_->getProgramController()) return;
        auto* controller = viewer_->getProgramController();
        if (!controller->getDocument() ||
            DocumentClaim::identity(controller->getOriginalFileName()) != identity) {
            // A context-menu action on an unopened paper should work too:
            // open/select its tab, then extract after native loading completes.
            if (identity.isEmpty() || pendingCitationExtracts_.contains(identity)) return;
            pendingCitationExtracts_.insert(identity);
            openPdf(path);
            auto* probe = new QTimer(this);
            probe->setInterval(100);
            connect(probe, &QTimer::timeout, this, [this, probe, path, identity, attempts = 0]() mutable {
                auto stop = [this, probe, identity] {
                    probe->stop(); probe->deleteLater();
                    pendingCitationExtracts_.remove(identity);
                };
                if (++attempts >= 80) { stop(); return; }
                if (!viewer_ || !viewer_->getProgramController()) return;
                auto* current = viewer_->getProgramController();
                if (current->getDocument() &&
                    DocumentClaim::identity(current->getOriginalFileName()) == identity) {
                    stop();
                    extractBibliography(path);
                }
            });
            probe->start();
            return;
        }
        if (!controller->getDocument()->getCatalog() ||
            controller->getDocument()->getCatalog()->getPageCount() < 1) return;
        queueBibliography(identity, *controller->getDocument(), true);
    }

    void queueBibliography(const QString& identity, const pdf::PDFDocument& document, bool force = false) {
        if (identity.isEmpty()) return;
        if (runningCitations_.contains(identity)) {
            // User-initiated extraction must not wait for the auto-extract
            // grace period when the document is already in the delayed queue.
            if (force) if (auto* pending = delayedCitations_.value(identity, nullptr))
                pending->start(0);
            return;
        }
        if (!force && analyzedCitations_.contains(identity)) return;
        analyzedCitations_.insert(identity);
        runningCitations_.insert(identity);
        // On large SI documents, the first-page metadata worker competes with
        // PDF4QT's first visible render. Let the renderer/UI get a head start,
        // without blocking or permanently disabling automatic bibliography.
        // An explicit user-triggered extraction is never delayed.
        const auto* catalog = document.getCatalog();
        const bool deferForLargePdf = !force && catalog && catalog->getPageCount() >= 80;
        const qint64 queuedAt = performanceNow();
        // This snapshot owns its document data across tab close/replacement.
        // QSettings and viewer/compiler state remain on the UI thread.
        auto result = std::make_shared<QVariantMap>();
        auto startWorker = [this, identity, result, snapshot = pdf::PDFDocument(document), queuedAt]() mutable {
            performanceEvent("bibliography-worker-dispatch", queuedAt);
            auto* worker = QThread::create([snapshot = std::move(snapshot), result]() mutable {
                const qint64 started = performanceNow();
                try { *result = luna::extractFirstPage(snapshot); }
                catch (...) { /* Invalid/image-only documents remain unverified. */ }
                performanceEvent("bibliography-first-page-worker", started);
            });
            connect(worker, &QThread::finished, this, [this, identity, result] {
                runningCitations_.remove(identity);
                bibliography_.enrichFromFirstPage(identity, *result);
            });
            connect(qApp, &QCoreApplication::aboutToQuit, worker, [worker] { worker->wait(); });
            connect(worker, &QThread::finished, worker, &QObject::deleteLater);
            worker->start(QThread::LowPriority);
        };
        if (deferForLargePdf) {
            auto* delay = new QTimer(this);
            delay->setSingleShot(true);
            delayedCitations_.insert(identity, delay);
            connect(delay, &QTimer::timeout, this,
                [this, delay, identity, startWorker = std::move(startWorker)]() mutable {
                    delayedCitations_.remove(identity);
                    delay->deleteLater();
                    startWorker();
                });
            delay->start(1200);
        } else {
            startWorker();
        }
    }

    void editBibliography(const QString& path) {
        const QVariantMap existing = bibliography_.record(DocumentClaim::identity(path));
        QDialog dialog(this);
        dialog.setObjectName(QStringLiteral("LunaBibliographyEditor"));
        dialog.setWindowTitle(QStringLiteral("논문 서지정보 수정"));
        dialog.setMinimumWidth(620); // Fit a typical full bibliographic reference.
        QFormLayout form(&dialog);
        form.setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
        QLineEdit fullCitation(existing.value("citation").toString(), &dialog);
        fullCitation.setObjectName(QStringLiteral("LunaBibliographyCitation"));
        fullCitation.setClearButtonEnabled(true);
        fullCitation.setPlaceholderText(QStringLiteral("서지정보 전체를 붙여넣으세요. 이 내용이 카드에 우선 표시됩니다."));
        QLineEdit title(existing.value("title").toString(), &dialog);
        QLineEdit authors(existing.value("authors").toString(), &dialog);
        QLineEdit journal(existing.value("journal").toString(), &dialog);
        QLineEdit year(existing.value("year").toString(), &dialog);
        QLineEdit volume(existing.value("volume").toString(), &dialog);
        QLineEdit pages(existing.value("page").toString(), &dialog);
        form.addRow(QStringLiteral("서지정보 한 줄"), &fullCitation);
        form.addRow(QStringLiteral("논문 제목"), &title);
        form.addRow(QStringLiteral("저자"), &authors);
        form.addRow(QStringLiteral("저널"), &journal);
        form.addRow(QStringLiteral("발행 연도"), &year);
        form.addRow(QStringLiteral("권(Volume)"), &volume);
        form.addRow(QStringLiteral("페이지 / 논문 번호"), &pages);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        form.addRow(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, [&dialog, &year] {
            const QString value = year.text().trimmed();
            static const QRegularExpression yearPattern(QStringLiteral(R"(^[0-9]{4}$)"));
            if (!value.isEmpty() && !yearPattern.match(value).hasMatch()) {
                QMessageBox::warning(&dialog, QStringLiteral("발행 연도 확인"),
                    QStringLiteral("발행 연도는 네 자리 숫자로 입력하세요."));
                return; // Preserve user input on validation error.
            }
            dialog.accept();
        });
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted) return;
        const QString revisedYear = year.text().trimmed();
        QVariantMap revised = existing;
        revised["citation"] = fullCitation.text().simplified().left(4096);
        revised["title"] = title.text().simplified().left(4096);
        revised["authors"] = authors.text().simplified().left(4096);
        revised["journal"] = journal.text().simplified().left(4096);
        revised["year"] = revisedYear;
        revised["volume"] = volume.text().simplified().left(256);
        revised["page"] = pages.text().simplified().left(256);
        revised["source"] = QStringLiteral("Manual");
        if (!bibliography_.saveEdits(DocumentClaim::identity(path), existing, revised))
            QMessageBox::warning(this, QStringLiteral("서지정보 저장 실패"), QStringLiteral("로컬 서지정보를 저장하지 못했습니다."));
    }
    void decorate(QListWidgetItem* item, const QString& path) {
        item->setData(Qt::UserRole, path);
        const QString fileIdentity = DocumentClaim::identity(path);
        item->setData(Qt::UserRole + 6, fileIdentity);
        const QString location = QDir(root_).relativeFilePath(QFileInfo(path).absolutePath());
        item->setData(Qt::UserRole + 1, location == QStringLiteral(".") ? QStringLiteral("라이브러리 폴더 · PDF") : location);
        item->setData(Qt::UserRole + 2, favorites_.contains(path));
        QString date = openedDates_.value(openedDateKey(path));
        if (date.isEmpty() && datedNames_.contains(QFileInfo(path).fileName().toCaseFolded()))
            date = displayedDates_.value(fileIdentity);
        const QDateTime opened = QDateTime::fromString(date, Qt::ISODate);
        item->setData(Qt::UserRole + 3, opened.isValid() ? opened.toLocalTime().date().toString(QStringLiteral("yy.MM.dd.")) : QString());
        const QString memo = memos_.value(openedDateKey(path));
        item->setData(Qt::UserRole + 4, memo);
        const QVariantMap bibliography = bibliography_.record(fileIdentity);
        item->setData(Qt::UserRole + 5, bibliography);
        const QString title = bibliography.value("title").toString().simplified();
        item->setText(title.isEmpty() ? QFileInfo(path).completeBaseName() : title);
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        const QString citation = luna::Bibliography::citation(bibliography);
        item->setToolTip((memo.isEmpty() ? QString() : memo + QLatin1Char('\n')) +
            (citation.isEmpty() ? QString() : citation + QLatin1Char('\n')) + path);
    }
    void savePaperMemo(const QString& path, QString text) {
        text.replace(QLatin1Char('\n'), QLatin1Char(' ')); text.replace(QLatin1Char('\r'), QLatin1Char(' '));
        const QString key = openedDateKey(path);
        QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("LUNA"), QStringLiteral("LUNA Library"));
        settings.setValue(QStringLiteral("library/memos/") + key, text); settings.sync();
        if (settings.status() != QSettings::NoError) {
            QMessageBox::warning(this, QStringLiteral("메모 저장 실패"), QStringLiteral("로컬 설정에 메모를 저장하지 못했습니다. 입력한 메모: ") + text);
            return;
        }
        memos_.insert(key, text);
        for (auto* list : findChildren<QListWidget*>()) {
            for (int row = 0; row < list->count(); ++row) {
                auto* item = list->item(row);
                if (item->data(Qt::UserRole).toString() == path) decorate(item, path);
            }
            filter(list, query_->text());
        }
    }
    void queueNavigationCounts() {
        if (countsPending_) return; countsPending_ = true;
        QTimer::singleShot(0, this, [this] {
            countsPending_ = false;
            allBadge_->setText(allScanned_ ? QString::number(allPdfs_->count()) + (allIterator_ ? QStringLiteral("+") : QString()) : QString());
            folderBadge_->setText(QString::number(folderPdfs_->count()) + (folderIterator_ ? QStringLiteral("+") : QString()));
            favoriteBadge_->setText(QString::number(favorites_.size())); recentBadge_->setText(QString::number(recentCount_));
        });
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
        queueNavigationCounts();
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
        selectActiveCard(); // Newly created Recent/Favorites rows need active blue indication.
    }
    void rememberOpenedDocument() { if (!window()->property("LunaClosing").toBool()) rememberEditor(*viewer_); }
    void rememberDateAlias(const QString& path, const QString& date) {
        displayedDates_.insert(DocumentClaim::identity(path), date);
        datedNames_.insert(QFileInfo(path).fileName().toCaseFolded());
    }
    void rememberEditor(pdfviewer::PDFEditorMainWindow& editor) {
        auto* controller = editor.getProgramController();
        if (!controller->getDocument()) { editor.setProperty("LunaRememberedPath", QString()); return; }
        // synchronize() calls this for every open tab at 10 Hz. Only resolve
        // filesystem identity on a document/path transition, never on idle ticks.
        const QString source = controller->getOriginalFileName();
        if (!editor.property("LunaRememberedPath").toString().isEmpty() &&
            editor.property("LunaRememberedSource").toString() == source) return;
        const QFileInfo info(controller->getOriginalFileName());
        const QString path = info.canonicalFilePath();
        if (!info.isFile() || path.isEmpty()) return;
        editor.setProperty("LunaRememberedSource", source);
        if (path == editor.property("LunaRememberedPath").toString()) return;
        editor.setProperty("LunaRememberedPath", path);
        queueBibliography(DocumentClaim::identity(path), *controller->getDocument());
        // Inspect only structured fields already present in the PDF info
        // dictionary. There is no DOI scanning, first-page text parsing or
        // remote lookup; QSettings writes are deferred until after open.
        if (const auto* documentInfo = controller->getDocument()->getInfo()) {
            const QString identity = DocumentClaim::identity(path);
            const QString title = documentInfo->title;
            const QString authors = documentInfo->author;
            QVariantMap structured;
            for (const auto& [rawKey, rawValue] : documentInfo->extra) {
                QString key = QString::fromLatin1(rawKey).trimmed().toLower();
                key.remove(QLatin1Char(' '));
                key.remove(QLatin1Char('_'));
                key.remove(QLatin1Char('-'));
                QString field;
                if (key == QStringLiteral("journal") || key == QStringLiteral("journaltitle"))
                    field = QStringLiteral("journal");
                else if (key == QStringLiteral("year") || key == QStringLiteral("publicationyear"))
                    field = QStringLiteral("year");
                else if (key == QStringLiteral("volume"))
                    field = QStringLiteral("volume");
                else if (key == QStringLiteral("pages") || key == QStringLiteral("pagerange") ||
                         key == QStringLiteral("articlenumber"))
                    field = QStringLiteral("page");
                if (!field.isEmpty()) structured[field] = rawValue.toString().left(256);
            }
            if (!title.isEmpty() || !authors.isEmpty() || !structured.isEmpty())
                QTimer::singleShot(0, this, [this, identity, title, authors, structured] {
                    bibliography_.ingest(identity, title, authors, structured);
                });
        }
        QStringList recent = savedPaths(QStringLiteral("library/recent"));
        recent.removeAll(path);
        recent.prepend(path);
        while (recent.size() > 100) recent.removeLast();
        recentCount_ = recent.size(); queueNavigationCounts();
        QSettings(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("LUNA"), QStringLiteral("LUNA Library")).setValue(QStringLiteral("library/recent"), recent);
        const QString opened = QDateTime::currentDateTime().toString(Qt::ISODate);
        const QString key = openedDateKey(path);
        openedDates_.insert(key, opened);
        rememberDateAlias(path, opened);
        QSettings(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("LUNA"), QStringLiteral("LUNA Library"))
            .setValue(QStringLiteral("library/openedDates/") + key, opened);
        for (auto* list : {folderPdfs_, allPdfs_, historyPdfs_})
            for (int row = 0; row < list->count(); ++row)
                if (auto* item = list->item(row)) {
                    const QString cardPath = item->data(Qt::UserRole).toString();
                    if (cardPath == path || (QFileInfo(cardPath).fileName().compare(info.fileName(), Qt::CaseInsensitive) == 0 &&
                        DocumentClaim::identity(cardPath) == DocumentClaim::identity(path))) decorate(item, cardPath);
                }
        // Reuse the native PDF4QT draw-space controller: no replacement renderer.
        auto* proxy = controller->getPdfWidget()->getDrawWidgetProxy();
        if (!editor.property("LunaPreserveReopenView").toBool()) {
            proxy->setPageLayout(pdf::PageLayout::OneColumn);
            proxy->zoom(2.0);
        }
        editor.setProperty("LunaPreserveReopenView", false);
        if (viewer_ == &editor) selectActiveCard();
        // Opening a paper must not rebuild or reorder the visible Recent list.
        // Refresh it only when the user explicitly reopens that navigation view.
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
        auto* delegate = new DocumentCard(list);
        delegate->saveMemo = [this](const QString& path, const QString& text) { savePaperMemo(path, text); };
        list->setItemDelegate(delegate);
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
            auto* memo = menu.addAction(QStringLiteral("메모 편집…"));
            memo->setObjectName(QStringLiteral("LunaEditPaperMemo"));
            auto* citation = menu.addAction(QStringLiteral("서지정보 수정…"));
            auto* extractCitation = menu.addAction(QStringLiteral("첫 페이지 서지정보 추출…"));
            extractCitation->setObjectName(QStringLiteral("LunaExtractCitationContextMenu"));
            auto* move = menu.addAction(QStringLiteral("PDF와 썸네일 이동…"));
            auto* chosen = menu.exec(list->viewport()->mapToGlobal(point));
            if (chosen == favorite) toggleFavorite(path);
            else if (chosen == crop) startThumbnailCrop();
            else if (chosen == memo) {
                // Explicit edit only. QListWidget has NoEditTriggers so
                // ordinary row clicks cannot accidentally open a text editor.
                list->editItem(item);
            }
            else if (chosen == citation) editBibliography(path);
            else if (chosen == extractCitation) extractBibliography(path);
            else if (chosen == move) moveDocument(path);
        });
        connect(list, &QListWidget::itemClicked, this, [this, list](QListWidgetItem* item) {
            const QPoint cursor = list->viewport()->mapFromGlobal(QCursor::pos());
            const QRect rect = list->visualItemRect(item);
            if (DocumentCard::favoriteRect(rect).contains(cursor))
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
        QPointer<pdfviewer::PDFEditorMainWindow> operation(viewer_);
        if (!operation) return;
        const QString source = QFileInfo(path).canonicalFilePath();
        if (source.isEmpty()) return;
        const QString sourceIdentity = DocumentClaim::identity(source);
        if ((activateElsewhere && activateElsewhere(source)) ||
            (independentDocuments(*operation) && independentDocuments(*operation)->openElsewhere(source))) {
            QMessageBox::information(this, QStringLiteral("다른 창에서 열린 PDF"),
                QStringLiteral("이 PDF를 열고 있는 LUNA 창에서 이동해 주세요. 해당 창을 활성화했습니다."));
            return;
        }
        auto* controller = operation->getProgramController();
        const bool opened = controller->getDocument() &&
            DocumentClaim::identity(controller->getOriginalFileName()) == sourceIdentity;
        int page = 0;
        double zoom = 2.0;
        pdf::PageLayout layout = pdf::PageLayout::OneColumn;
        if (opened) {
            if (!operation->isEnabled() || !controller->canClose()) {
                qWarning("LUNA paired move deferred: native reader is busy");
                return;
            }
            if (!controller->askForSaveDocumentBeforeClose()) return;
            // Save As may have changed the current document while prompting.
            // Do not close or relocate a different document under the old name.
            if (DocumentClaim::identity(controller->getOriginalFileName()) != sourceIdentity) {
                QMessageBox::information(this, QStringLiteral("이동 취소"),
                    QStringLiteral("저장한 문서의 경로가 변경되었습니다. 이동할 파일을 다시 선택해 주세요."));
                return;
            }
            const auto currentPages = controller->getPdfWidget()->getDrawWidget()->getCurrentPages();
            if (!currentPages.empty()) page = int(currentPages.front());
            zoom = controller->getPdfWidget()->getDrawWidgetProxy()->getZoom();
            layout = controller->getPdfWidget()->getDrawWidgetProxy()->getPageLayout();
            if (cropOverlay_) cropOverlay_->close();
            controller->closeDocument();
        }
        hideThumbnail();
        const auto result = movePdfWithThumbnails(source, destination);
        if (result.success) {
            QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("LUNA"), QStringLiteral("LUNA Library"));
            for (const QString& key : {QStringLiteral("library/favorites"), QStringLiteral("library/recent")}) {
                QStringList paths = settings.value(key).toStringList();
                for (QString& value : paths) if (DocumentClaim::identity(value) == sourceIdentity) value = result.documentPath;
                paths.removeDuplicates();
                settings.setValue(key, paths);
            }
            const QString oldDateKey = openedDateKey(source), newDateKey = openedDateKey(result.documentPath);
            const QString date = settings.value(QStringLiteral("library/openedDates/") + oldDateKey).toString();
            if (!date.isEmpty() && oldDateKey != newDateKey) {
                settings.setValue(QStringLiteral("library/openedDates/") + newDateKey, date);
                settings.remove(QStringLiteral("library/openedDates/") + oldDateKey);
                openedDates_.remove(oldDateKey); openedDates_.insert(newDateKey, date);
            }
            if (oldDateKey != newDateKey && memos_.contains(oldDateKey)) {
                const QString memo = memos_.value(oldDateKey);
                settings.setValue(QStringLiteral("library/memos/") + newDateKey, memo);
                settings.sync();
                if (settings.status() == QSettings::NoError) {
                    memos_.insert(newDateKey, memo); memos_.remove(oldDateKey);
                    settings.remove(QStringLiteral("library/memos/") + oldDateKey);
                } else {
                    QMessageBox::warning(this, QStringLiteral("메모 이동 저장 실패"),
                        QStringLiteral("파일은 이동했지만 메모 설정을 저장하지 못했습니다. 기존 메모는 보존됩니다: ") + memo);
                }
            }
            if (!bibliography_.relocate(sourceIdentity, DocumentClaim::identity(result.documentPath)))
                QMessageBox::warning(this, QStringLiteral("서지정보 이동 실패"),
                    QStringLiteral("PDF는 이동했지만 서지정보를 이전하지 못했습니다. 기존 설정을 확인해 주세요."));
            const auto favorites = savedPaths(QStringLiteral("library/favorites"));
            favorites_ = QSet<QString>(favorites.cbegin(), favorites.cend());
            ThumbnailCache::instance()->invalidate();
            allTimer_->stop(); allIterator_.reset(); allScanned_ = false; allPdfs_->clear();
            selectFolder(current_);
            if (pages_->currentIndex() == 1) scanAll();
            else if (pages_->currentIndex() == 2) showHistory(showingFavorites_);
        }
        if (opened && result.success && movedHandler && !movedHandler(operation.data(), source, result.documentPath)) return;
        if (opened) {
            operation->setProperty("LunaMoveRestorePending", true);
            operation->setProperty("LunaPreserveReopenView", true);
            controller->openDocument(result.documentPath);
            auto* restore = new QTimer(this);
            connect(restore, &QTimer::timeout, this, [this, restore, editor = operation, target = result.documentPath, page, zoom, layout, attempts = 0]() mutable {
                if (!editor) { restore->stop(); restore->deleteLater(); return; }
                auto* c = editor->getProgramController();
                if (++attempts > 200) { editor->setProperty("LunaMoveRestorePending", false); restore->stop(); restore->deleteLater(); return; }
                if (!editor->isEnabled() || !c->getDocument()) return;
                if (DocumentClaim::identity(c->getOriginalFileName()) == DocumentClaim::identity(target)) {
                    c->getPdfWidget()->getDrawWidgetProxy()->setPageLayout(layout);
                    c->getPdfWidget()->getDrawWidgetProxy()->zoom(zoom);
                    c->getPdfWidget()->getDrawWidgetProxy()->goToPage(page);
                }
                editor->setProperty("LunaMoveRestorePending", false);
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
                            !item->text().contains(word, Qt::CaseInsensitive) &&
                            !QFileInfo(item->data(Qt::UserRole).toString()).completeBaseName().contains(word, Qt::CaseInsensitive) &&
                            !item->data(Qt::UserRole + 4).toString().contains(word, Qt::CaseInsensitive) &&
                            !item->data(Qt::UserRole + 5).toMap().value("authors").toString().contains(word, Qt::CaseInsensitive) &&
                            !item->data(Qt::UserRole + 5).toMap().value("journal").toString().contains(word, Qt::CaseInsensitive) &&
                            !luna::Bibliography::citation(item->data(Qt::UserRole + 5).toMap()).contains(word, Qt::CaseInsensitive));
        }
    }
    void selectRoot(const QString& path) {
        QFileInfo info(path);
        if (!info.isDir()) return;
        root_ = info.canonicalFilePath();
        if (root_.isEmpty()) return;
        rootLabel_->setText(info.fileName().isEmpty() ? root_ : info.fileName());
        rootLabel_->setToolTip(root_);
        folderTab_->setText(rootLabel_->text()); folderTab_->setToolTip(root_);
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
        // Limit each 0-ms timer turn by BOTH item count and wall time.
        // Expensive Windows file metadata must not freeze the GUI; avoid
        // repainting all 96px paper cards after each individual insertion.
        QElapsedTimer batchClock;
        batchClock.start();
        const QIcon fileIcon = style()->standardIcon(QStyle::SP_FileIcon);
        const bool wasUpdating = list->updatesEnabled();
        if (wasUpdating) list->setUpdatesEnabled(false);
        int checked = 0;
        while (checked < kScanBatch && batchClock.elapsed() < 25 &&
               list->count() < kMaximumEntries && iterator->hasNext()) {
            const QString path = iterator->next();
            ++checked;
            QFileInfo info(path);
            if (!info.isFile() || info.suffix().compare(
                    QStringLiteral("pdf"), Qt::CaseInsensitive) != 0) continue;
            auto* item = new QListWidgetItem(fileIcon, info.completeBaseName(), list);
            decorate(item, path);
            const QString word = query_->text();
            const QVariantMap meta = item->data(Qt::UserRole + 5).toMap();
            item->setHidden(!word.isEmpty() &&
                !item->text().contains(word, Qt::CaseInsensitive) &&
                !info.completeBaseName().contains(word, Qt::CaseInsensitive) &&
                !item->data(Qt::UserRole + 4).toString().contains(word, Qt::CaseInsensitive) &&
                !meta.value("authors").toString().contains(word, Qt::CaseInsensitive) &&
                !meta.value("journal").toString().contains(word, Qt::CaseInsensitive) &&
                !luna::Bibliography::citation(meta).contains(word, Qt::CaseInsensitive));
        }
        const bool complete = list->count() >= kMaximumEntries || !iterator->hasNext();
        if (complete) {
            timer->stop();
            iterator.reset();
            // Sort once after scanning, but keep auto-sorting disabled.
            // Metadata title updates must not relocate the visible card.
            list->sortItems(Qt::AscendingOrder);
            count->setText(QStringLiteral("%1 PDF · %2개")
                .arg(all ? QStringLiteral("전체") : QStringLiteral("현재 폴더"))
                .arg(list->count()));
            if (list->count() >= kMaximumEntries) count->setText(count->text() + QStringLiteral(" 이상 · 표시 제한"));
        } else {
            count->setText(QStringLiteral("PDF 검색 중… %1개").arg(list->count()));
        }
        if (wasUpdating) list->setUpdatesEnabled(true);
        queueNavigationCounts();
        // Mark the active card once after discovery, not by walking every
        // existing item on every batch (quadratic work across 1000+ PDFs).
        if (complete) selectActiveCard();
    }
    void openItem(QListWidgetItem* item) {
        if (item) openPdf(item->data(Qt::UserRole).toString());
    }
    void openPdf(const QString& path) {
        if (openHandler) { openHandler(path); return; }
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
    luna::Bibliography bibliography_;
    QSet<QString> analyzedCitations_;
    QSet<QString> runningCitations_;
    QHash<QString, QTimer*> delayedCitations_;
    QSet<QString> pendingCitationExtracts_;
    QAction* cropAction_ = nullptr;
    QAction* pagePreviewAction_ = nullptr;
    QMetaObject::Connection documentTitleConnection_, cropConnection_;
    QSplitter* split_ = nullptr;
    QWidget* navigation_ = nullptr;
    QWidget* documents_ = nullptr;
    QLabel* rootLabel_ = nullptr;
    QLabel *allBadge_ = nullptr, *folderBadge_ = nullptr, *recentBadge_ = nullptr, *favoriteBadge_ = nullptr;
    bool countsPending_ = false;
    qsizetype recentCount_ = 0;
    QListWidget* historyPdfs_ = nullptr;
    QLabel* historyCount_ = nullptr;
    bool showingFavorites_ = false;
    QString lastOpened_;
    QHash<QString, QString> openedDates_;
    QHash<QString, QString> displayedDates_;
    QSet<QString> datedNames_;
    QHash<QString, QString> memos_;
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
            // LUNA uses a fixed toolbar/dock layout; Qt saveState() would traverse
            // nested PDF editor toolbars and persist them as shell toolbars.
        }
        return QObject::eventFilter(object, event);
    }
private:
    QMainWindow* window_;
};

} // namespace

static void installLibraryBase(QMainWindow& window, pdfviewer::PDFEditorMainWindow& viewer, bool sessionShell) {
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
    dock->setContentsMargins(0, 0, 0, 0);
    auto* library = new LibraryPanel(&viewer, dock);
    dock->setWidget(library);
    window.addDockWidget(Qt::LeftDockWidgetArea, dock);
    dock->show();
    window.resizeDocks({dock}, {562}, Qt::Horizontal);

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
    QObject::connect(focus, &QAction::toggled, dock, [dock, library](bool enabled) {
        if (!enabled) library->showBothPanels();
        dock->setVisible(!enabled);
    });
    // Legacy individual panel actions remain available in View, not as three
    // competing permanent controls in the approved interface.
    for (auto* action : {folders, documents, focus}) controls->removeAction(action);
    if (auto* view = viewer.findChild<QMenu*>(QStringLiteral("menuView"))) {
        view->addAction(folders);
        view->addAction(documents);
    }
    // One filename search field in the paper panel; no duplicate toolbar row.
    auto* globalSearch = library->searchEdit();
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
    if (auto* pdfBar = sessionShell ? nullptr : window.findChild<QToolBar*>(QStringLiteral("mainToolBar"))) {
        window.insertToolBar(pdfBar, controls);
    } else window.addToolBar(Qt::TopToolBarArea, controls);
    const QList<QPair<QString, QString>> menus = {
        {QStringLiteral("menuFile"), QStringLiteral("파일")}, {QStringLiteral("menuEdit"), QStringLiteral("편집")},
        {QStringLiteral("menuView"), QStringLiteral("보기")}, {QStringLiteral("menuGoTo"), QStringLiteral("이동")},
        {QStringLiteral("menuInsert"), QStringLiteral("삽입")},
        {QStringLiteral("menuTools"), QStringLiteral("도구")}, {QStringLiteral("menuHelp"), QStringLiteral("도움말")}
    };
    for (const auto& entry : menus) if (auto* menu = window.findChild<QMenu*>(entry.first)) menu->setTitle(entry.second);
    auto* brandFrame = new QWidget(&window); brandFrame->setObjectName(QStringLiteral("LunaBrandFrame"));
    brandFrame->setFixedHeight(48);
    auto* brandRow = new QHBoxLayout(brandFrame); brandRow->setContentsMargins(14, 0, 18, 0); brandRow->setSpacing(12);
    auto* moon = new QLabel(brandFrame); moon->setObjectName(QStringLiteral("LunaBrandIcon"));
    moon->setFixedSize(31, 31); moon->setPixmap(QPixmap(QStringLiteral(":/luna/LUNA-Moon.png"))); moon->setScaledContents(true);
    brandRow->addWidget(moon, 0, Qt::AlignVCenter);
    auto* brand = new QLabel(QStringLiteral("LUNA Library"), brandFrame); brand->setObjectName(QStringLiteral("LunaBrand"));
    QFont brandFont(QStringLiteral("Malgun Gothic")); brandFont.setPixelSize(20); brandFont.setBold(true); brandFont.setLetterSpacing(QFont::AbsoluteSpacing, -.45); brand->setFont(brandFont);
    brand->setAlignment(Qt::AlignVCenter | Qt::AlignLeft); brandRow->addWidget(brand, 0, Qt::AlignVCenter);
    brand->ensurePolished();
    brand->setMinimumWidth(QFontMetrics(brand->font()).horizontalAdvance(brand->text()));
    brandFrame->setMinimumWidth(brandFrame->sizeHint().width());
    brandFrame->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    window.menuBar()->setObjectName(QStringLiteral("LunaMainMenuBar"));
    auto* menuStyle = new MenuBarStyle; menuStyle->setParent(window.menuBar()); window.menuBar()->setStyle(menuStyle);
    QFont menuFont(QStringLiteral("Malgun Gothic")); menuFont.setPixelSize(13); window.menuBar()->setFont(menuFont);
    window.menuBar()->setNativeMenuBar(false); window.menuBar()->setFixedHeight(48); window.menuBar()->setCornerWidget(brandFrame, Qt::TopLeftCorner);

    if (auto* bar = sessionShell ? nullptr : window.findChild<QToolBar*>(QStringLiteral("mainToolBar"))) {
        installReaderToolbar(viewer, *bar, *focus);
        bar->removeAction(focus);
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
        auto* panelToggle = new QToolButton(documentHeader);
        panelToggle->setObjectName(QStringLiteral("LunaCombinedPanelToggle"));
        panelToggle->setDefaultAction(focus);
        panelToggle->setToolButtonStyle(Qt::ToolButtonIconOnly);
        panelToggle->setAccessibleName(QStringLiteral("라이브러리와 논문 목록 접기 / 펼치기"));
        panelToggle->setMinimumSize(30, 30);
        panelToggle->setToolButtonStyle(Qt::ToolButtonIconOnly);
        auto* paperHeading = library->findChild<QHBoxLayout*>(QStringLiteral("LunaPaperHeadingLayout"));
        auto placeToggle = [panelToggle, paperHeading, documentRow, documentHeader, focus](bool collapsed) {
            paperHeading->removeWidget(panelToggle);
            documentRow->removeWidget(panelToggle);
            panelToggle->setParent(collapsed ? documentHeader : paperHeading->parentWidget());
            if (collapsed) documentRow->insertWidget(0, panelToggle);
            else paperHeading->addWidget(panelToggle);
            panelToggle->setFixedSize(collapsed ? QSize(39, 41) : QSize(32, 30));
            panelToggle->setIconSize(QSize(19, 19));
            focus->setText(collapsed ? QStringLiteral("패널 펼치기") : QStringLiteral("패널 접기"));
            focus->setIcon(readerIcon(collapsed ? QStringLiteral("panel-open") : QStringLiteral("panel-close")));
            focus->setProperty("lunaCollapsed", collapsed);
            focus->setToolTip(collapsed ? QStringLiteral("라이브러리와 논문 목록 펼치기") : QStringLiteral("라이브러리와 논문 목록 접기"));
            panelToggle->show();
        };
        QObject::connect(focus, &QAction::toggled, panelToggle, placeToggle);
        placeToggle(false);
        QObject::connect(&window, &QWidget::windowTitleChanged, documentTitle, [&viewer, documentTitle] {
            QTimer::singleShot(0, documentTitle, [&viewer, documentTitle] {
                const QString file = viewer.getProgramController()->getOriginalFileName();
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
    if (sessionShell) {
        auto* frame = new QWidget(&window); frame->setObjectName(QStringLiteral("LunaPdfFrame"));
        auto* layout = new QVBoxLayout(frame); layout->setContentsMargins(0, 7, 7, 7); layout->setSpacing(0);
        auto* corner = new QWidget(frame); corner->setObjectName(QStringLiteral("LunaTabCorner"));
        auto* row = new QHBoxLayout(corner); row->setContentsMargins(0, 0, 4, 0);
        auto* toggle = new QToolButton(corner); toggle->setObjectName(QStringLiteral("LunaCombinedPanelToggle"));
        toggle->setDefaultAction(focus); toggle->setAccessibleName(QStringLiteral("라이브러리와 논문 목록 접기 / 펼치기")); toggle->setMinimumSize(30, 30);
        toggle->setToolButtonStyle(Qt::ToolButtonIconOnly);
        auto* paperHeading = library->findChild<QHBoxLayout*>(QStringLiteral("LunaPaperHeadingLayout"));
        auto place = [toggle, paperHeading, row, corner, focus](bool collapsed) {
            paperHeading->removeWidget(toggle); row->removeWidget(toggle);
            toggle->setParent(collapsed ? corner : paperHeading->parentWidget());
            if (collapsed) row->insertWidget(0, toggle); else paperHeading->addWidget(toggle);
            toggle->setFixedSize(collapsed ? QSize(39, 41) : QSize(32, 30));
            toggle->setIconSize(QSize(19, 19));
            focus->setText(collapsed ? QStringLiteral("패널 펼치기") : QStringLiteral("패널 접기"));
            focus->setIcon(readerIcon(collapsed ? QStringLiteral("panel-open") : QStringLiteral("panel-close")));
            focus->setProperty("lunaCollapsed", collapsed);
            focus->setToolTip(collapsed ? QStringLiteral("라이브러리와 논문 목록 펼치기") : QStringLiteral("라이브러리와 논문 목록 접기"));
            corner->setVisible(collapsed || corner->property("lunaHasOpenButton").toBool()); toggle->show();
        };
        QObject::connect(focus, &QAction::toggled, toggle, place); place(false);
        layout->addWidget(corner); window.setCentralWidget(frame);
    }
    QObject::connect(&window, &QWidget::windowTitleChanged, &window,
        [&window](const QString& title) {
            if (!title.startsWith(QStringLiteral("LUNA Library")))
                window.setWindowTitle(QStringLiteral("LUNA Library · ") + title);
        });
    window.setWindowTitle(QStringLiteral("LUNA Library"));
    const QSize available = window.screen()->availableGeometry().size() - QSize(16, 48);
    window.resize(QSize(1366, 768).boundedTo(available));
    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("LUNA"), QStringLiteral("LUNA Library"));
    window.restoreGeometry(settings.value(QStringLiteral("window/geometry")).toByteArray());
    // Never restore QMainWindow toolbar/dock state (legacy OR v0.9.1).
    // Qt 6 recursively discovers nested PDF4QT mainToolBar widgets. Even a
    // versioned key can capture ghost toolbars after the first clean exit.
    // Keep only window geometry and the independent library splitter setting.
    focus->setChecked(dock->isHidden());
    hideAdvancedToolbars(viewer); // Old saved PDF4QT toolbar rows must not reappear.
    controls->hide(); // Legacy actions remain in View; this empty row has no UI role.
    new WindowState(&window);
}

void installLibrary(pdfviewer::PDFEditorMainWindow& window) {
    installLibraryBase(window, window, false);
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
}

void installLibraryShell(QMainWindow& shell, pdfviewer::PDFEditorMainWindow& editor) { installLibraryBase(shell, editor, true); }
static LibraryPanel* panel(QMainWindow& shell) {
    auto* dock = shell.findChild<QDockWidget*>(QStringLiteral("LunaLibraryDock"));
    return dock ? dynamic_cast<LibraryPanel*>(dock->widget()) : nullptr;
}
void bindLibraryEditor(QMainWindow& shell, pdfviewer::PDFEditorMainWindow& editor) { if (auto* p = panel(shell)) p->bindEditor(editor); }
void recordLibraryDocument(QMainWindow& shell, pdfviewer::PDFEditorMainWindow& editor) { if (auto* p = panel(shell)) p->recordEditor(editor); }
void setLibraryDocumentRouter(QMainWindow& shell, std::function<void(const QString&)> open,
    std::function<bool(const QString&)> elsewhere,
    std::function<bool(pdfviewer::PDFEditorMainWindow*, const QString&, const QString&)> moved) {
    if (auto* p = panel(shell)) { p->openHandler = std::move(open); p->activateElsewhere = std::move(elsewhere); p->movedHandler = std::move(moved); }
}
void prepareReaderSession(pdfviewer::PDFEditorMainWindow& editor, QMainWindow& shell) {
    editor.setStyleSheet(shell.styleSheet());
    for (auto* dock : editor.findChildren<QDockWidget*>()) {
        dock->hide();
        if (dock->objectName() == QStringLiteral("SidebarDockWidget")) {
            dock->toggleViewAction()->setVisible(false);
            QObject::connect(dock, &QDockWidget::visibilityChanged, dock, [dock](bool visible) { if (visible) dock->hide(); });
        }
    }
    auto* focus = shell.findChild<QAction*>(QStringLiteral("LunaFocusMode"));
    auto* crop = new QAction(QStringLiteral("썸네일 크롭"), &editor); crop->setObjectName(QStringLiteral("LunaCropThumbnail"));
    if (auto* p = panel(shell)) {
        auto update = [crop, original = p->cropAction()] { crop->setEnabled(original->isEnabled()); };
        QObject::connect(p->cropAction(), &QAction::changed, crop, update); update();
        QObject::connect(crop, &QAction::triggered, p->cropAction(), &QAction::trigger);
    }
    if (auto* bar = editor.findChild<QToolBar*>(QStringLiteral("mainToolBar")); bar && focus) {
        installReaderToolbar(editor, *bar, *focus); bar->removeAction(focus);
    }
    if (auto* options = editor.findChild<QAction*>(QStringLiteral("actionOptions"))) options->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+O")));
    for (const auto& entry : QList<QPair<QString, QString>>{
        {QStringLiteral("menuFile"), QStringLiteral("파일")}, {QStringLiteral("menuEdit"), QStringLiteral("편집")},
        {QStringLiteral("menuView"), QStringLiteral("보기")}, {QStringLiteral("menuGoTo"), QStringLiteral("이동")},
        {QStringLiteral("menuInsert"), QStringLiteral("삽입")}, {QStringLiteral("menuTools"), QStringLiteral("도구")},
        {QStringLiteral("menuHelp"), QStringLiteral("도움말")}})
        if (auto* menu = editor.findChild<QMenu*>(entry.first)) menu->setTitle(entry.second);
    editor.menuBar()->hide();
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

bool verifyBibliographyUi(pdfviewer::PDFEditorMainWindow& window, QString& error, const QString& capturePath) {
    auto* dock = window.findChild<QDockWidget*>(QStringLiteral("LunaLibraryDock"));
    auto* panel = dock ? dynamic_cast<LibraryPanel*>(dock->widget()) : nullptr;
    auto* controller = window.getProgramController();
    if (!panel || !controller->getDocument()) { error = "Bibliography panel/document missing"; return false; }
    const QString identity = DocumentClaim::identity(controller->getOriginalFileName());
    QListWidgetItem* card = nullptr;
    QListWidget* list = nullptr;
    const bool siProbe = qEnvironmentVariableIsSet("LUNA_PUBLIC_BIBLIOGRAPHY_SI_PROBE");
    auto awaitAuthor = [&](const QString& expected) {
        QElapsedTimer timer; timer.start();
        while (timer.elapsed() < 15000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            for (auto* candidate : {panel->folderPdfs_, panel->allPdfs_, panel->historyPdfs_})
                for (int i = 0; i < candidate->count(); ++i) {
                    auto* row = candidate->item(i);
                    if (row->data(Qt::UserRole + 6).toString() != identity) continue;
                    const auto metadata = row->data(Qt::UserRole + 5).toMap();
                    bool matches = metadata.value("authors").toString() == expected;
                    if (siProbe) {
                        matches = metadata.value("documentKind").toString() == QStringLiteral("supporting_information");
                        for (const char* field : {"title", "authors", "citation", "journal", "year", "volume", "page"})
                            matches = matches && metadata.value(QString::fromLatin1(field)).toString().isEmpty();
                    }
                    if (matches) { card = row; list = candidate; return true; }
                }
        }
        return false;
    };
    const QString correct = QStringLiteral("John J. Molloy");
    const QString expectedTitle = controller->getDocument()->getInfo()->title.simplified();
    if (!awaitAuthor(correct)) {
        error = siProbe ? "Real SI PDF generated paper metadata or was not classified as SI" :
            "Real PDF automatic card author was not John J. Molloy";
        // Only the explicit, checksum-pinned public CI probe requests raw text.
        // Normal opening/extraction never writes PDF text into logs/settings.
        QString diagnostic;
        try {
            auto snapshot = *controller->getDocument();
            const auto evidence = luna::extractFirstPage(snapshot, &diagnostic);
            error += QStringLiteral("\nDirect evidence author: ") + evidence.value("authors").toString() +
                QStringLiteral("\nJournal: ") + evidence.value("journal").toString() +
                QStringLiteral("\nKind: ") + evidence.value("documentKind").toString() +
                QStringLiteral("\nVisual first-page text:\n") + diagnostic.left(12000);
        } catch (const std::exception& exception) {
            error += QStringLiteral("\nPDF4QT diagnostic exception: ") + QString::fromUtf8(exception.what());
        } catch (...) { error += QStringLiteral("\nUnknown PDF4QT diagnostic exception"); }
        return false;
    }
    panel->selectActiveCard();
    const int rowBefore = list->row(card), scrollBefore = list->verticalScrollBar()->value();
    const bool activeBefore = card->data(Qt::UserRole + 7).toBool();
    if (siProbe) {
        if (!activeBefore || QApplication::activeModalWidget()) { error = "SI active card/modal failure"; return false; }
        window.grab().save(capturePath + QStringLiteral(".png"));
        return true;
    }
    for (const bool manualFlag : {false, true}) {
        QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, "LUNA", "LUNA Library");
        const QString key = QStringLiteral("library/bibliography/") + QString::fromLatin1(
            QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Sha256).toHex());
        settings.setValue(key, QVariantMap{{"authors", "Hao Fang"}, {"manual", manualFlag}, {"source", "Manual"}});
        settings.sync();
        panel->refreshBibliography(identity);
        if (!card->data(Qt::UserRole + 5).toMap().value("authors").toString().isEmpty()) {
            error = "Legacy unverified author was visible"; return false;
        }
        panel->analyzedCitations_.remove(identity);
        panel->queueBibliography(identity, *controller->getDocument());
        if (!awaitAuthor(correct)) { error = "Real PDF legacy author repair failed"; return false; }
        if (card->data(Qt::UserRole + 5).toMap().value("title").toString() != expectedTitle) {
            error = "Real PDF title was replaced by publisher/sidebar prose"; return false;
        }
    }
    if (list->row(card) != rowBefore || list->verticalScrollBar()->value() != scrollBefore ||
        card->data(Qt::UserRole + 7).toBool() != activeBefore || QApplication::activeModalWidget()) {
        error = "Bibliography update moved the card/scroll/highlight or showed a dialog"; return false;
    }
    window.grab().save(capturePath + QStringLiteral(".png"));
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
    if (navigation->width() < 192 || documents->width() < 285 || window.getProgramController()->getPdfWidget()->width() < 300)
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
    auto* citationPanelForFocus = dynamic_cast<LibraryPanel*>(dock->widget());
    if (!citationPanelForFocus) return fail("Library panel unavailable for active-paper check");
    const int scrollBeforeFocus = list->verticalScrollBar()->value();
    citationPanelForFocus->selectActiveCard();
    if (list->verticalScrollBar()->value() != scrollBeforeFocus)
        return fail("Active document highlighting moved the paper list");
    if (!item->data(Qt::UserRole + 7).toBool())
        return fail("Active document did not get the blue card highlight flag");
    auto* favorite = list->findChild<QAction*>(QStringLiteral("LunaToggleFavorite"));
    if (!favorite) return fail("Favorite action missing");
    const QString path = item->data(Qt::UserRole).toString();
    // Manual tags are intentionally independent of auto bibliography or PDF
    // text. Verify the real editing dialog, durable settings and search model.
    auto* manualPanel = dynamic_cast<LibraryPanel*>(dock->widget());
    if (!manualPanel || !item->data(Qt::UserRole + 8).toStringList().isEmpty())
        return fail("Synthetic paper unexpectedly contains auto-extracted tags");
    bool tagDialogReached = false;
    QTimer tagDialog;
    QObject::connect(&tagDialog, &QTimer::timeout, &window, [&] {
        if (auto* dialog = qobject_cast<QInputDialog*>(QApplication::activeModalWidget())) {
            tagDialogReached = true;
            dialog->setTextValue(QStringLiteral("PCET, Thiouracil, Photochemistry"));
            dialog->accept();
        }
    });
    tagDialog.start(30);
    manualPanel->editManualTags(path);
    tagDialog.stop();
    const QStringList expectedTags = {QStringLiteral("PCET"),
        QStringLiteral("Thiouracil"), QStringLiteral("Photochemistry")};
    const QString tagKey = QStringLiteral("library/manualTags/") + openedDateKey(path);
    QSettings tagSettings(QSettings::defaultFormat(), QSettings::UserScope,
        QStringLiteral("LUNA"), QStringLiteral("LUNA Library"));
    if (!tagDialogReached || item->data(Qt::UserRole + 8).toStringList() != expectedTags ||
        tagSettings.value(tagKey).toStringList() != expectedTags)
        return fail("Manual paper tags did not persist through the native editor");
    search->setText(QStringLiteral("Photochemistry"));
    if (item->isHidden()) return fail("Third user-entered tag was not searchable");
    search->clear();
    // Confirm the actual Qt card model reacts to a local citation change.
    // Synthetic fixture only; no PDF/DOI is uploaded.
    auto* citationPanel = dynamic_cast<LibraryPanel*>(dock->widget());
    if (!citationPanel || !citationPanel->bibliography_.save(DocumentClaim::identity(path),
        {{"title", QStringLiteral("Synthetic Citation Card")},
         {"authors", QStringLiteral("Synthetic A")},
         {"journal", QStringLiteral("Test Journal")},
         {"year", QStringLiteral("2026")}, {"volume", QStringLiteral("42")},
         {"page", QStringLiteral("12-20")}}, true))
        return fail("Synthetic citation could not be saved");
    if (item->text() != QStringLiteral("Synthetic Citation Card") ||
        luna::Bibliography::citation(item->data(Qt::UserRole + 5).toMap()) != QStringLiteral("Test Journal 2026, 42, 12-20"))
        return fail("Citation did not populate the actual library card");
    search->setText(QStringLiteral("Test Journal"));
    if (item->isHidden()) return fail("Citation journal was not searchable");
    search->clear();
    bool editorReached = false;
    QTimer::singleShot(0, &window, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog || dialog->objectName() != QStringLiteral("LunaBibliographyEditor"))
            return;
        auto* input = dialog->findChild<QLineEdit*>(QStringLiteral("LunaBibliographyCitation"));
        auto* buttons = dialog->findChild<QDialogButtonBox*>();
        if (!input || !buttons || !buttons->button(QDialogButtonBox::Ok)) {
            dialog->reject();
            return;
        }
        editorReached = true;
        input->setText(QStringLiteral("Synthetic A, Test Journal 2026, 42, 12-20."));
        buttons->button(QDialogButtonBox::Ok)->click();
    });
    citationPanel->editBibliography(path);
    const QVariantMap manuallyEdited = citationPanel->bibliography_.record(DocumentClaim::identity(path));
    if (!editorReached ||
        luna::Bibliography::citation(manuallyEdited) !=
            QStringLiteral("Synthetic A, Test Journal 2026, 42, 12-20.") ||
        item->data(Qt::UserRole + 5).toMap().value("citation") != manuallyEdited.value("citation"))
        return fail("Native bibliography editor did not persist the pasted reference");
    search->setText(QStringLiteral("Synthetic A, Test Journal"));
    if (item->isHidden()) return fail("Pasted bibliography was not searchable");
    search->clear();
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
    if (history->item(0)->data(Qt::UserRole + 3).toString() != QDate::currentDate().toString(QStringLiteral("yy.MM.dd.")))
        return fail("Successful open did not record the actual local-calendar date");
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
    auto* combined = window.findChild<QToolButton*>(QStringLiteral("LunaCombinedPanelToggle"));
    auto* focusAction = window.findChild<QAction*>(QStringLiteral("LunaFocusMode"));
    if (!focusAction || !combined || !combined->isVisible() || combined->icon().isNull() || focusAction->property("lunaCollapsed").toBool())
        return fail("Combined panel collapse control is missing");
    combined->click();
    QCoreApplication::processEvents();
    if (!dock->isHidden() || !combined->isVisible() || combined->icon().isNull() || !focusAction->property("lunaCollapsed").toBool())
        return fail("Combined panel restore control is unreachable");
    combined->click();
    QCoreApplication::processEvents();
    if (dock->isHidden() || !navigation->isVisible() || !documents->isVisible() || focusAction->isChecked())
        return fail("Combined panel restore did not show both subpanels");
    // Exercise the real viewport crop and asynchronous hover in the temporary
    // synthetic library. Nothing here reads or modifies a user's library.
    auto* library = dynamic_cast<LibraryPanel*>(dock->widget());
    if (!library) return fail("Library panel type missing");
    auto wait = [](const std::function<bool()>& ready, int timeoutMs = 5000) {
        if (ready()) return true;
        QEventLoop loop;
        QTimer poll, timeout;
        bool success = false;
        QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
            // Process deletions posted at this nested loop level. Operations
            // under test must be dispatched inside this loop, not its caller.
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            if (ready()) { success = true; loop.quit(); }
        });
        QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        poll.start(20); timeout.setSingleShot(true); timeout.start(timeoutMs); loop.exec();
        return success;
    };
    if (!wait([&] { return window.isEnabled() && window.getProgramController()->canClose(); }))
        return fail("Reader busy before crop verification");
    auto* preview = window.findChild<QAction*>(QStringLiteral("LunaPagePreview"));
    auto* grid = window.findChild<QListView*>(QStringLiteral("LunaPageGrid"));
    auto* view = window.findChild<QMenu*>(QStringLiteral("menuView"));
    if (!preview || !view || !view->actions().contains(preview) ||
        preview->isChecked() || !grid || grid->isVisible() || grid->model()->rowCount() != 2 ||
        window.findChild<QTabBar*>(QStringLiteral("LunaContentModes")))
        return fail("Clean paper-list view or native two-page preview model missing");
    preview->trigger();
    if (!wait([&] { return grid->isVisible() && !grid->visualRect(grid->model()->index(1, 0)).isEmpty(); }))
        return fail("Page grid did not become visible");
    const auto firstPage = grid->model()->index(0, 0);
    const auto secondPage = grid->model()->index(1, 0);
    if (!wait([&] { return grid->visualRect(firstPage).top() == grid->visualRect(secondPage).top() &&
        grid->visualRect(firstPage).left() < grid->visualRect(secondPage).left(); }) || !navigation->isVisible())
        return fail("Page previews are not two columns or hid the folder panel");
    if (!wait([&] { return !grid->model()->data(firstPage, Qt::DecorationRole).value<QPixmap>().isNull(); }))
        return fail("PDF4QT did not render the real page thumbnail");
    if (capturePath.isEmpty() || !window.grab().save(capturePath + QStringLiteral(".pages.png")))
        return fail("Native two-column page preview capture failed");
    const QPoint point = grid->visualRect(secondPage).center();
    QMouseEvent pagePress(QEvent::MouseButtonPress, point, grid->viewport()->mapToGlobal(point), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent pageRelease(QEvent::MouseButtonRelease, point, grid->viewport()->mapToGlobal(point), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(grid->viewport(), &pagePress);
    QApplication::sendEvent(grid->viewport(), &pageRelease);
    if (!wait([&] { const auto visible = window.getProgramController()->getPdfWidget()->getDrawWidget()->getCurrentPages();
        return std::find(visible.begin(), visible.end(), 1) != visible.end(); }))
        return fail("Page preview click did not navigate the native document");
    preview->trigger();
    if (preview->isChecked() || grid->isVisible())
        return fail("Optional page preview did not return to the paper list");
    window.getProgramController()->getPdfWidget()->getDrawWidgetProxy()->goToPage(0);
    const QFileInfo pdf(path);
    const QString thumbnail = pdf.absolutePath() + QLatin1Char('/') + pdf.completeBaseName() + QStringLiteral(".luna-thumb.png");
    if (QFileInfo::exists(thumbnail)) return fail("Synthetic crop destination already exists");
    auto* cropAction = window.findChild<QAction*>(QStringLiteral("LunaCropThumbnail"));
    if (!cropAction || !cropAction->isEnabled()) return fail("Crop toolbar action is unavailable for loaded PDF");
    cropAction->trigger();
    if (!library->cropOverlay_) return fail("Native crop overlay did not open");
    // ESC must run at the same event-loop level that observes deletion.
    // A DeferredDelete posted by the outer smoke timer cannot be consumed by
    // a deeper loop, even with sendPostedEvents(DeferredDelete).
    QTimer::singleShot(0, library->cropOverlay_, [overlay = library->cropOverlay_] {
        if (!overlay) return;
        QKeyEvent cancel(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(overlay, &cancel);
    });
    if (!wait([&] { return !library->cropOverlay_; })) return fail("Cancelled crop overlay was not deleted");
    if (QFileInfo::exists(thumbnail)) return fail("Crop cancellation wrote an image");
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
    // Edit through the real native delegate; Enter and focus-out must persist
    // without changing the PDF title or bytes. Search includes the local memo.
    list->edit(list->model()->index(list->row(item), 0));
    auto* memoInput = list->findChild<QLineEdit*>(QStringLiteral("LunaPaperMemoEditor"));
    if (!memoInput) return fail("Inline paper memo editor did not open");
    memoInput->setText(QStringLiteral("합성 메모 검색 742"));
    QKeyEvent memoEnter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(memoInput, &memoEnter);
    if (!wait([&] { return item->data(Qt::UserRole + 4).toString() == QStringLiteral("합성 메모 검색 742"); }))
        return fail("Enter did not commit inline paper memo");
    QSettings persistedMemo(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("LUNA"), QStringLiteral("LUNA Library"));
    if (persistedMemo.value(QStringLiteral("library/memos/") + openedDateKey(path)).toString() != QStringLiteral("합성 메모 검색 742"))
        return fail("Paper memo did not persist in local metadata");
    search->setText(QStringLiteral("메모 검색 742"));
    if (item->isHidden()) return fail("Memo text was excluded from library search");
    search->clear();
    if (!capturePath.isEmpty() && (!window.screen() || !window.screen()->grabWindow(0).save(capturePath + QStringLiteral(".memo.png"))))
        return fail("Could not capture saved native paper memo");
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
            return !window.property("LunaMoveRestorePending").toBool() && window.isEnabled() && c->canClose() && !c->getIsBusy() && c->getDocument() &&
                QFileInfo(c->getOriginalFileName()).canonicalFilePath() == QFileInfo(expectedPath).canonicalFilePath();
        });
    };
    library->moveDocumentTo(path, movedFolder);
    if (!reopened(movedPath) || QFileInfo::exists(path) || QFileInfo::exists(thumbnail) || !QFileInfo::exists(movedThumbnail))
        return fail("Opened PDF and sidecar did not move/reopen together");
    if (library->memos_.value(openedDateKey(movedPath)) != QStringLiteral("합성 메모 검색 742"))
        return fail("Paper memo did not follow paired file move");
    library->moveDocumentTo(movedPath, pdf.absolutePath());
    if (!reopened(path) || QFileInfo::exists(movedPath) || QFileInfo::exists(movedThumbnail)) {
        auto* c = window.getProgramController();
        error = QStringLiteral("Opened PDF did not return to its library folder: enabled=%1 canClose=%2 busy=%3 document=%4 sourcePdf=%5 sourcePng=%6 movedPdf=%7 movedPng=%8 currentIsSource=%9 currentIsMoved=%10")
            .arg(window.isEnabled()).arg(c->canClose()).arg(c->getIsBusy()).arg(bool(c->getDocument()))
            .arg(QFileInfo::exists(path)).arg(QFileInfo::exists(thumbnail))
            .arg(QFileInfo::exists(movedPath)).arg(QFileInfo::exists(movedThumbnail))
            .arg(QFileInfo(c->getOriginalFileName()).canonicalFilePath() == QFileInfo(path).canonicalFilePath())
            .arg(QFileInfo(c->getOriginalFileName()).canonicalFilePath() == QFileInfo(movedPath).canonicalFilePath());
        qWarning().noquote() << error;
        return false;
    }
    if (library->memos_.value(openedDateKey(path)) != QStringLiteral("합성 메모 검색 742"))
        return fail("Paper memo did not survive return movement");
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
        // CI disk/AV throughput varies. Keep an explicit 15-second full
        // discovery ceiling while separately enforcing <1s GUI event gaps
        // and <1s filename search. Never accept a partially filled list.
        if (!wait([&] { return list->count() == 1000 && !library->folderTimer_->isActive(); }, 15000)) {
            error = QStringLiteral("1000-PDF library did not finish incremental discovery: count=%1 elapsedMs=%2 timerActive=%3 longestGuiGapMs=%4 beats=%5")
                .arg(list->count()).arg(loadClock.elapsed())
                .arg(library->folderTimer_->isActive()).arg(longestGap).arg(beats);
            return false;
        }
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
