// SPDX-License-Identifier: MIT
// LUNA Library V2 UI overlay. PDF rendering remains upstream PDF4QT.
#include "lunalibrary.h"
#include "lunathumbnailcache.h"
#include "lunafilemove.h"
#include "lunaautofilename.h"
#include "lunamanualsi.h"
#include "lunatrashundo.h"
#include "lunalocalsync.h"
#include "lunadocumentclaim.h"
#include "lunabibliography.h"
#include "lunafirstpage.h"
#include "lunametadatafetch.h"
#include "pdfdocumentreader.h"
#include "pdfprogress.h"
#include <QQueue>
#include "lunaperformance.h"
#include <QThread>
#include "pdfdocument.h"
#include "pdfcompiler.h"
#include "pdftextlayout.h"
#include "lunareadertoolbar.h"
#include "pdfeditormainwindow.h"
#include "pdfprogramcontroller.h"
#include "pdfundoredomanager.h"
#include "pdfdrawwidget.h"
#include "pdfdrawspacecontroller.h"
#include "pdfitemmodels.h"
#include "lunapagegrid.h"
#include <QListView>
#include <QTabBar>
#include <QTabWidget>
#include <QComboBox>
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
#include <QPainterPath>
#include <QTextLayout>
#include <QLinearGradient>
#include <QSet>
#include <QHash>
#include <QScrollArea>
#include <QAbstractScrollArea>
#include <QWheelEvent>
#include <QEasingCurve>
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
#include <QLockFile>
#include <QItemSelectionModel>
#include <QMouseEvent>
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
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QClipboard>
#include <QUrl>
#include <QDropEvent>
#include <QSaveFile>
#include <QMouseEvent>
#include <functional>
#include <algorithm>
#include <QImageReader>
#include <QImage>
#include <QGridLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QUuid>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QScreen>
#include <QCache>
#include <QThreadPool>
#include <memory>
#include <cstdio>
namespace luna {
namespace {
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
QString savedThumbnailForPdf(const QString& documentPath) {
    const QFileInfo pdf(documentPath);
    if (!pdf.isFile() || pdf.suffix().compare(QStringLiteral("pdf"), Qt::CaseInsensitive) != 0)
        return {};
    for (const QString& ext : {QStringLiteral(".png"), QStringLiteral(".jpg"), QStringLiteral(".jpeg")}) {
        for (const QString& candidate : {SidecarStorage::thumbnail(documentPath, ext),
                                       SidecarStorage::legacyThumbnail(documentPath, ext)}) {
            const QFileInfo sidecar(candidate);
            if (sidecar.isFile() && !sidecar.isSymLink() &&
                sidecar.size() > 0 && sidecar.size() <= 5'000'000 &&
                (candidate == SidecarStorage::legacyThumbnail(documentPath, ext) ||
                 SidecarStorage::safeManagedDirectory(documentPath)))
                return candidate;
        }
    }
    return {};
}
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
class SmoothLibraryWheel final : public QObject {
public:
    explicit SmoothLibraryWheel(QAbstractScrollArea* area, int step)
        : QObject(area), area_(area), step_(step), animation_(new QPropertyAnimation(this)) {
        animation_->setTargetObject(area->verticalScrollBar());
        animation_->setPropertyName("value");
        animation_->setDuration(155);
        animation_->setEasingCurve(QEasingCurve::OutCubic);
        connect(area->verticalScrollBar(), &QScrollBar::sliderPressed,
                animation_, &QAbstractAnimation::stop);
        area->viewport()->installEventFilter(this);
    }
protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (!area_ || watched != area_->viewport() || event->type() != QEvent::Wheel)
            return QObject::eventFilter(watched, event);
        auto* wheel = static_cast<QWheelEvent*>(event);
        if (!wheel->pixelDelta().isNull() || wheel->phase() != Qt::NoScrollPhase ||
            wheel->angleDelta().y() == 0 ||
            (wheel->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier))) {
            if (!wheel->pixelDelta().isNull() || wheel->phase() != Qt::NoScrollPhase)
                animation_->stop();
            return false;
        }
        auto* scrollbar = area_->verticalScrollBar();
        if (!scrollbar || scrollbar->maximum() <= scrollbar->minimum()) return false;
        const int from = scrollbar->value();
        const int baseline = animation_->state() == QAbstractAnimation::Running ?
            animation_->endValue().toInt() : from;
        const int delta = qRound(double(wheel->angleDelta().y()) * step_ / 120.0);
        if (!delta) return false;
        const int target = qBound(scrollbar->minimum(), baseline - delta, scrollbar->maximum());
        animation_->stop();
        if (target == from) return false; // allow normal boundary propagation
        animation_->setStartValue(from);
        animation_->setEndValue(target);
        animation_->start();
        wheel->accept();
        return true;
    }
private:
    QPointer<QAbstractScrollArea> area_;
    int step_;
    QPropertyAnimation* animation_;
};
class DocumentCard final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    static constexpr int kCardHeight = 322;
    static constexpr int kCompactHeight = 112;
    static constexpr int kSIRowHeight = 46;
    static constexpr int kSIFooterHeight = 29;
    void setCompact(bool compact) { compact_ = compact; }
    bool compact() const { return compact_; }
    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex& index) const override {
        if(index.data(Qt::UserRole + 10).toBool())return {1,kSIRowHeight};
        return {1,(compact_ ? kCompactHeight : kCardHeight) +
            (index.data(Qt::UserRole + 11).toInt()>0 ? kSIFooterHeight : 0)};
    }
    static QRect siFooterRect(const QRect& r) {
        return QRect(r.left()+12,r.bottom()-kSIFooterHeight+1,
                     qMax(1,r.width()-24),kSIFooterHeight);
    }
    std::function<void(const QString&, const QString&)> saveMemo;

    void paintSIFooter(QPainter* painter,const QStyleOptionViewItem& option,
                       const QModelIndex& index) const {
        const int count=index.data(Qt::UserRole+11).toInt();
        if(count<=0)return;
        const QRect area=siFooterRect(option.rect);
        painter->save();
        painter->setPen(QColor(kLINE));
        painter->drawLine(area.left()+5,area.top()+1,area.right()-5,area.top()+1);
        painter->setPen(QColor(kBLUE));
        QFont font=option.font;font.setPixelSize(12);font.setBold(true);
        painter->setFont(font);
        const bool expanded=index.data(Qt::UserRole+12).toBool();
        painter->drawText(area.adjusted(13,3,-6,-1),Qt::AlignVCenter|Qt::AlignLeft,
            (expanded?QStringLiteral("▾  "):QStringLiteral("▸  "))+
             QStringLiteral("Supporting Information (%1)").arg(count));
        painter->restore();
    }
    void paintSIChild(QPainter* painter,const QStyleOptionViewItem& option,
                      const QModelIndex& index) const {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing,true);
        const QRect r=option.rect.adjusted(18,2,-6,-2);
        const bool selected=option.state&QStyle::State_Selected;
        painter->setPen(QPen(QColor(selected?"#B0CDF2":kLINE),1));
        painter->setBrush(QColor(selected?"#EAF2FF":"#F6F9FD"));
        painter->drawRoundedRect(r,6,6);
        readerIcon(QStringLiteral("documents")).paint(painter,
            QRect(r.left()+12,r.top()+10,20,24));
        QFont font=option.font;font.setFamily(QStringLiteral("Malgun Gothic"));font.setPixelSize(12);
        painter->setFont(font);painter->setPen(QColor(kNAVY));
        const QString name=QFileInfo(index.data(Qt::UserRole).toString()).completeBaseName();
        painter->drawText(r.adjusted(42,4,-10,-4),Qt::AlignVCenter|Qt::AlignLeft,
            QFontMetrics(font).elidedText(name,Qt::ElideRight,qMax(12,r.width()-60)));
        painter->restore();
    }
    static QRect contentRect(const QRect& r) {
        return QRect(r.left() + 16, r.top() + 14, qMax(25, r.width() - 32), kCardHeight - 28);
    }
    static QRect thumbnailRect(const QRect& r) {
        const QRect content = contentRect(r);
        const int height = qMin(164, qMax(48, content.width() * 2 / 3));
        return QRect(content.left(), r.top() + 14, content.width(), height);
    }
    static QRect titleRect(const QRect& r) {
        const QRect content = contentRect(r);
        return QRect(content.left(), thumbnailRect(r).bottom() + 13, content.width(), 45);
    }
    static QRect citationRect(const QRect& r) {
        const QRect content = contentRect(r);
        return QRect(content.left(), titleRect(r).bottom() + 7, content.width(), 24);
    }
    static QRect memoRect(const QRect& r) {
        const QRect content = contentRect(r);
        return QRect(content.left(), citationRect(r).bottom() + 16, content.width(), 26);
    }
    static QRect favoriteRect(const QRect& r) {
        return QRect(r.right() - 48, r.top() + 22, 28, 28);
    }
    static QRect compactFavoriteRect(const QRect& r) {
        return QRect(r.right() - 32, r.top() + 11, 22, 22);
    }
    static int compactThumbWidth(const QRect& r) {
        return qBound(34, (r.width() - 70) / 2, 72);
    }
    static int compactTextLeft(const QRect& r) {
        return r.left() + 21 + compactThumbWidth(r);
    }
    static QRect compactMemoRect(const QRect& r) {
        const int left = compactTextLeft(r);
        return QRect(left, r.top() + 87, qMax(1, r.right() - left - 11), 20);
    }
    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem&, const QModelIndex&) const override {
        auto* editor = new QLineEdit(parent);
        editor->setObjectName(QStringLiteral("LunaPaperMemoEditor"));
        editor->setPlaceholderText(QStringLiteral("논문 한 줄 메모"));
        editor->setAccessibleName(QStringLiteral("논문 한 줄 메모"));
        return editor;
    }
    void setEditorData(QWidget* editor, const QModelIndex& index) const override {
        auto* input = static_cast<QLineEdit*>(editor);
        input->setText(index.data(Qt::UserRole + 4).toString());
        input->selectAll();
    }
    void setModelData(QWidget* editor, QAbstractItemModel*, const QModelIndex& index) const override {
        if (saveMemo) saveMemo(index.data(Qt::UserRole).toString(), static_cast<QLineEdit*>(editor)->text());
    }
    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex&) const override {
        editor->setGeometry((compact_ ? compactMemoRect(option.rect) :
            memoRect(option.rect)).adjusted(21, 0, 0, 0));
    }
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        if(index.data(Qt::UserRole+10).toBool()){paintSIChild(painter,option,index);return;}
        if(compact_){paintCompact(painter,option,index);paintSIFooter(painter,option,index);return;}
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
        const QRect r = option.rect;
        const QRect card = r.adjusted(5, 4, -6, -5);
        const bool selected = option.state & QStyle::State_Selected;
        const bool activeDocument = index.data(Qt::UserRole + 7).toBool();
        const bool hovered = option.state & QStyle::State_MouseOver;
        painter->setPen(QPen(QColor(activeDocument ? "#92B7EE" : selected ? "#BAD1F2" : kLINE), 1));
        painter->setBrush(QColor(activeDocument ? "#F1F7FF" : selected ? "#EAF2FF" :
            hovered ? "#F8FBFF" : kPANEL));
        painter->drawRoundedRect(card, 10, 10);
        const QRect imageArea = thumbnailRect(r);
        painter->setPen(QPen(QColor(kLINE), 1));
        painter->setBrush(Qt::white);
        painter->drawRoundedRect(imageArea, 7, 7);
        const QString sidecar = savedThumbnailForPdf(index.data(Qt::UserRole).toString());
        QPointer<QWidget> view(const_cast<QWidget*>(option.widget));
        const QImage image = ThumbnailCache::instance()->request(sidecar, view, [view] {
            if (auto* list = qobject_cast<QAbstractItemView*>(view.data())) list->viewport()->update();
            else if (view) view->update();
        });
        if (!image.isNull()) {
            const QRect frame = imageArea.adjusted(3, 3, -3, -3);
            const QSize fitted = image.size().scaled(frame.size(), Qt::KeepAspectRatio);
            const QRect target(QPoint(frame.center().x() - fitted.width() / 2,
                                     frame.center().y() - fitted.height() / 2), fitted);
            painter->save();
            QPainterPath clip;
            clip.addRoundedRect(imageArea.adjusted(1, 1, -1, -1), 6, 6);
            painter->setClipPath(clip);
            painter->drawImage(target, image);
            painter->restore();
        } else {
            readerIcon(QStringLiteral("documents")).paint(painter,
                QRect(imageArea.center().x() - 18, imageArea.center().y() - 30, 36, 42));
            QFont placeholder = option.font;
            placeholder.setPixelSize(11);
            painter->setFont(placeholder);
            painter->setPen(QColor(kMUTED));
            painter->drawText(QRect(imageArea.left(), imageArea.center().y() + 16,
                                    imageArea.width(), 21),
                Qt::AlignHCenter | Qt::AlignVCenter, QStringLiteral("PDF"));
        }
        const QRect heading = titleRect(r);
        QFont titleFont = option.font;
        titleFont.setFamily(QStringLiteral("Malgun Gothic"));
        titleFont.setPixelSize(13);
        titleFont.setWeight(QFont::Bold);
        const QFontMetrics titleMetrics(titleFont);
        painter->setFont(titleFont);
        painter->setPen(QColor(kNAVY));
        const QString title = index.data(Qt::DisplayRole).toString().simplified();
        QTextLayout titleLayout(title, titleFont);
        QTextOption wrap;
        wrap.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        titleLayout.setTextOption(wrap);
        titleLayout.beginLayout();
        const int lineHeight = qMax(20, titleMetrics.height() + 1);
        for (int row = 0; row < 2; ++row) {
            QTextLine line = titleLayout.createLine();
            if (!line.isValid()) break;
            line.setLineWidth(heading.width());
            const QString visibleText = row == 0 ?
                title.mid(line.textStart(), line.textLength()).trimmed() :
                title.mid(line.textStart()).trimmed();
            const QString elided = titleMetrics.elidedText(visibleText, Qt::ElideRight, heading.width());
            painter->drawText(QRect(heading.left(), heading.top() + row * lineHeight,
                                    heading.width(), lineHeight + 1),
                Qt::AlignLeft | Qt::AlignVCenter, elided);
        }
        titleLayout.endLayout();
        QFont captionFont = option.font;
        captionFont.setFamily(QStringLiteral("Malgun Gothic"));
        captionFont.setPixelSize(12);
        painter->setFont(captionFont);
        painter->setPen(QColor(kMUTED));
        const QVariantMap bibliography = index.data(Qt::UserRole + 5).toMap();
        const QString lastAuthor = luna::Bibliography::lastListedAuthor(
            bibliography.value(QStringLiteral("authors")).toString());
        const QString citation = luna::Bibliography::citation(bibliography);
        const QString metadata =
            (!bibliography.value(QStringLiteral("doi")).toString().isEmpty() ||
             !bibliography.value(QStringLiteral("citation")).toString().isEmpty()) ?
            citation :
            ((lastAuthor.isEmpty() ? QStringLiteral("저자 미확인") : lastAuthor) +
             QStringLiteral(", ") +
             (citation.isEmpty() ? QStringLiteral("저널·연도 미확인") : citation));
        const bool oneLineTitle = titleMetrics.horizontalAdvance(title) <= heading.width();
        const QRect citeArea = oneLineTitle ? citationRect(r).translated(0, -19) : citationRect(r);
        painter->drawText(citeArea, Qt::AlignLeft | Qt::AlignVCenter,
            QFontMetrics(captionFont).elidedText(metadata, Qt::ElideRight, citeArea.width()));
        const QRect content = contentRect(r);
        painter->setPen(QColor(kLINE));
        painter->drawLine(content.left(), memoRect(r).top() - 8, content.right(), memoRect(r).top() - 8);
        const QString memo = index.data(Qt::UserRole + 4).toString();
        painter->setPen(QColor(memo.isEmpty() ? "#829BBE" : "#48648A"));
        const QRect note = memoRect(r);
        readerIcon(QStringLiteral("memo-inline")).paint(painter,
            QRect(note.left(), note.center().y() - 8, 16, 16),
            Qt::AlignCenter, QIcon::Normal, QIcon::Off);
        const QRect noteText = note.adjusted(22, 0, 0, 0);
        painter->drawText(noteText, Qt::AlignLeft | Qt::AlignVCenter,
            QFontMetrics(captionFont).elidedText(
                memo.isEmpty() ? QStringLiteral("메모 추가") : memo,
                Qt::ElideRight, noteText.width()));
        const QRect favorite = favoriteRect(r);
        painter->setPen(QPen(QColor(kLINE), 1));
        painter->setBrush(QColor(255, 255, 255, 240));
        painter->drawEllipse(favorite);
        readerIcon(QStringLiteral("star")).paint(painter, favorite.adjusted(3, 3, -3, -3),
            Qt::AlignCenter, index.data(Qt::UserRole + 2).toBool() ?
            QIcon::Selected : QIcon::Disabled);
        painter->restore();
        paintSIFooter(painter,option,index);
    }
private:
    // Restored density of the original paper list: thumbnail at LEFT, title,
    // one-line citation and one-line memo at RIGHT; still one paper per row.
    void paintCompact(QPainter* painter, const QStyleOptionViewItem& option,
                      const QModelIndex& index) const {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
        const QRect r = option.rect;
        const bool selected = option.state & QStyle::State_Selected;
        const bool active = index.data(Qt::UserRole + 7).toBool();
        const bool hovered = option.state & QStyle::State_MouseOver;
        painter->setPen(QPen(QColor(active ? "#92B7EE" : selected ? "#BAD1F2" : kLINE), 1));
        painter->setBrush(QColor(active ? "#F1F7FF" : selected ? "#EAF2FF" :
                                 hovered ? "#F8FBFF" : kPANEL));
        painter->drawRoundedRect(r.adjusted(4, 3, -5, -4), 7, 7);
        const int thumbWidth = compactThumbWidth(r);
        const int thumbHeight = qMin(78, qMax(42, thumbWidth * 78 / 72));
        const QRect thumb(r.left() + 11, r.top() + 12, thumbWidth, thumbHeight);
        painter->setPen(QPen(QColor(kTHUMB_LINE), 1));
        painter->setBrush(Qt::white);
        painter->drawRoundedRect(thumb, 5, 5);
        const QString sidecar = savedThumbnailForPdf(index.data(Qt::UserRole).toString());
        QPointer<QWidget> view(const_cast<QWidget*>(option.widget));
        const QImage image = ThumbnailCache::instance()->request(sidecar, view, [view] {
            if (auto* list = qobject_cast<QAbstractItemView*>(view.data())) list->viewport()->update();
            else if (view) view->update();
        });
        if (!image.isNull()) {
            const QRect frame = thumb.adjusted(3, 3, -3, -3);
            const QSize fitted = image.size().scaled(frame.size(), Qt::KeepAspectRatio);
            painter->save();
            QPainterPath clip;
            clip.addRoundedRect(thumb.adjusted(1,1,-1,-1), 4, 4);
            painter->setClipPath(clip);
            painter->drawImage(QRect(QPoint(frame.center().x() - fitted.width()/2,
                                           frame.center().y() - fitted.height()/2), fitted), image);
            painter->restore();
        } else {
            readerIcon(QStringLiteral("documents")).paint(painter,
                QRect(thumb.center().x()-13, thumb.center().y()-18, 26, 32));
        }
        const int left = compactTextLeft(r);
        const int width = qMax(1, r.right() - left - 11);
        QFont titleFont = option.font;
        titleFont.setFamily(QStringLiteral("Malgun Gothic"));
        titleFont.setPixelSize(12);
        titleFont.setWeight(QFont::Bold);
        painter->setFont(titleFont);
        painter->setPen(QColor(kNAVY));
        const QFontMetrics titleMetrics(titleFont);
        const QString title = index.data(Qt::DisplayRole).toString().simplified();
        QTextLayout layout(title, titleFont);
        QTextOption wrap;
        wrap.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        layout.setTextOption(wrap);
        layout.beginLayout();
        const int lineHeight = qMax(16, titleMetrics.height());
        for (int row=0; row<2; ++row) {
            QTextLine line = layout.createLine();
            if (!line.isValid()) break;
            const int available = qMax(16, width - (row == 0 ? 25 : 0));
            line.setLineWidth(available);
            const QString text = row == 0 ?
                title.mid(line.textStart(), line.textLength()).trimmed() :
                title.mid(line.textStart()).trimmed();
            painter->drawText(QRect(left, r.top()+9+row*lineHeight, available, lineHeight+1),
                Qt::AlignVCenter | Qt::AlignLeft,
                titleMetrics.elidedText(text, Qt::ElideRight, available));
        }
        layout.endLayout();
        QFont captionFont = option.font;
        captionFont.setFamily(QStringLiteral("Malgun Gothic"));
        captionFont.setPixelSize(11);
        painter->setFont(captionFont);
        painter->setPen(QColor(kMUTED));
        const QVariantMap bibliography = index.data(Qt::UserRole + 5).toMap();
        const QString citation = luna::Bibliography::citation(bibliography);
        const QString author = luna::Bibliography::lastListedAuthor(
            bibliography.value(QStringLiteral("authors")).toString());
        const bool formatted = !bibliography.value("doi").toString().isEmpty() ||
                               !bibliography.value("citation").toString().isEmpty();
        const QString metadata = formatted ? citation :
            ((author.isEmpty() ? QStringLiteral("저자 미확인") : author) +
             QStringLiteral(", ") +
             (citation.isEmpty() ? QStringLiteral("저널·연도 미확인") : citation));
        // Reserve two title lines, but put the citation immediately below them.
        painter->drawText(QRect(left, r.top()+49, width, 20), Qt::AlignVCenter | Qt::AlignLeft,
            QFontMetrics(captionFont).elidedText(metadata, Qt::ElideRight, width));
        painter->setPen(QColor(kLINE));
        painter->drawLine(left, r.top()+80, left+width, r.top()+80);
        painter->setPen(QColor(index.data(Qt::UserRole + 4).toString().isEmpty() ?
                               "#829BBE" : "#48648A"));
        const QRect memo = compactMemoRect(r);
        readerIcon(QStringLiteral("memo-inline")).paint(painter,
            QRect(memo.left(), memo.center().y()-7, 15, 15));
        const QString note = index.data(Qt::UserRole + 4).toString();
        painter->drawText(memo.adjusted(20, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft,
            QFontMetrics(captionFont).elidedText(
                note.isEmpty() ? QStringLiteral("메모 추가") : note, Qt::ElideRight,
                qMax(15, memo.width()-20)));
        readerIcon(QStringLiteral("star")).paint(painter,
            compactFavoriteRect(r).adjusted(1,1,-1,-1), Qt::AlignCenter,
            index.data(Qt::UserRole + 2).toBool() ? QIcon::Selected : QIcon::Disabled);
        painter->restore();
    }
    bool compact_ = false;
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
        if (event->type() != QEvent::DragEnter &&
            event->type() != QEvent::DragMove && event->type() != QEvent::Drop) return false;
        auto* widget = qobject_cast<QWidget*>(object);
        if (!widget || widget->window() != &window_) return false;
        // Library folder tree handles URL drops as real file moves/copies;
        // the global editor drop-to-open handler must not intercept them.
        for (auto* parent = widget; parent; parent = parent->parentWidget())
            if (parent->objectName() == QStringLiteral("LunaFolderDropTree") ||
                parent->objectName() == QStringLiteral("LunaFolderDocumentsButton"))
                return false;
        auto* drop = static_cast<QDropEvent*>(event);
        const auto urls = drop->mimeData()->urls();
        if (urls.isEmpty()) return false;
        for (const auto& url : urls) if (!url.isLocalFile() || !validPdf(url.toLocalFile())) return false;
        if (event->type() == QEvent::Drop) for (const auto& url : urls) open(url.toLocalFile());
        drop->setDropAction(Qt::CopyAction); // Opening never moves an external source.
        drop->accept();
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
// Version 1.0 approved pack: twenty bundled SVG resources, stable ID only.
// Portable per-folder metadata follows the directory through MYBOX sync or rename.
constexpr const char* kFolderIconMetadata = "LUNA.folder.json";
struct LunaFolderIconPreset { const char* id; const char* label; };
constexpr LunaFolderIconPreset kFolderIconPresets[] = {
    {"general","일반 폴더"}, {"important","중요 폴더"},
    {"favorite","즐겨찾기 폴더"}, {"research","연구 주제"},
    {"project","프로젝트"}, {"review","리뷰"},
    {"mechanism","메커니즘"}, {"synthesis","합성"},
    {"spectroscopy","분광·분석"}, {"computational","계산화학"},
    {"data","데이터"}, {"reference","참고자료"},
    {"ideas","아이디어"}, {"drafting","작성 중"},
    {"completed","완료"}, {"shared","공유"},
    {"external","외부 자료"}, {"archive","보관"},
    {"private","비공개"}, {"temporary","임시"}
};
// The 40 user-supplied general-purpose SVG symbols are separate from
// the legacy 20 folder drawings. Stable IDs survive folder moves and MYBOX.
struct LunaSymbolPreset { const char* id; const char* label; const char* category; };
constexpr LunaSymbolPreset kSymbolIconPresets[] = {
    {"star","별","favorites"}, {"heart","하트","favorites"},
    {"crown","왕관","favorites"}, {"gem","보석","favorites"},
    {"flame","불꽃","favorites"}, {"sparkles","반짝임","favorites"},
    {"bookmark","북마크","favorites"}, {"pin","핀","favorites"},
    {"flag","깃발","favorites"}, {"trophy","트로피","favorites"},
    {"book","책","research"}, {"flask","플라스크","research"},
    {"atom","원자","research"}, {"microscope","현미경","research"},
    {"graduation","학사모","research"}, {"lightbulb","전구","research"},
    {"puzzle","퍼즐","research"}, {"target","과녁","research"},
    {"network","네트워크","research"}, {"brain","두뇌","research"},
    {"check","체크","workflow"}, {"clock","시계","workflow"},
    {"timer","타이머","workflow"}, {"calendar","달력","workflow"},
    {"hourglass","모래시계","workflow"}, {"refresh","새로고침","workflow"},
    {"archive","보관함","workflow"}, {"inbox","수신함","workflow"},
    {"send","전송","workflow"}, {"layers","레이어","workflow"},
    {"moon","달","personal"}, {"sun","태양","personal"},
    {"coffee","커피","personal"}, {"palette","팔레트","personal"},
    {"leaf","나뭇잎","personal"}, {"gift","선물","personal"},
    {"smile","스마일","personal"}, {"music","음표","personal"},
    {"camera","카메라","personal"}, {"rocket","로켓","personal"}
};
QString folderMetadataPath(const QString& folder) {
    return QDir(folder).filePath(QString::fromLatin1(kFolderIconMetadata));
}
QString validatedFolderIconId(QString id) {
    for (const auto& preset : kFolderIconPresets)
        if (id == QString::fromLatin1(preset.id)) return id;
    if (id.startsWith(QStringLiteral("symbol:")))
        for (const auto& preset : kSymbolIconPresets)
            if (id == QStringLiteral("symbol:") + QString::fromLatin1(preset.id))
                return id;
    return QStringLiteral("general");
}
QIcon embeddedFolderIcon(const QString& id) {
    const QString validId = validatedFolderIconId(id);
    // Resource names are from a closed set, never untrusted file paths.
    static QHash<QString, QIcon> cache;
    const auto cached = cache.constFind(validId);
    if (cached != cache.cend()) return cached.value();
    const QIcon icon(validId.startsWith(QStringLiteral("symbol:")) ?
        QStringLiteral(":/luna/symbols/") + validId.mid(7) + QStringLiteral(".svg") :
        QStringLiteral(":/luna/folders/") + validId + QStringLiteral(".svg"));
    cache.insert(validId, icon);
    return icon;
}
QJsonObject readFolderMetadata(const QString& folder) {
    const QFileInfo info(folderMetadataPath(folder));
    if (!info.isFile() || info.isSymLink() || info.size() > 16 * 1024) return {};
    QFile input(info.absoluteFilePath());
    if (!input.open(QIODevice::ReadOnly)) return {};
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(input.readAll(), &error);
    return error.error == QJsonParseError::NoError && document.isObject()
        ? document.object() : QJsonObject();
}
QString folderIconId(const QString& folder) {
    return validatedFolderIconId(readFolderMetadata(folder).value(QStringLiteral("iconId")).toString());
}
class FolderIcons final : public QObject, public QFileIconProvider {
public:
    explicit FolderIcons(QObject* parent) : QObject(parent) {}
    QIcon icon(IconType type) const override {
        return type == Folder ? embeddedFolderIcon(QStringLiteral("general")) : QFileIconProvider::icon(type);
    }
    QIcon icon(const QFileInfo& info) const override {
        return info.isDir() ? embeddedFolderIcon(QStringLiteral("general")) : QFileIconProvider::icon(info);
    }
};
class LunaFolderModel final : public QFileSystemModel {
public:
    explicit LunaFolderModel(QObject* parent) : QFileSystemModel(parent) {
        recountTimer_.setSingleShot(true);
        connect(&recountTimer_, &QTimer::timeout, this, [this] { beginRecount(); });
    }
    std::function<void()> countsUpdated;
    int pdfCount(const QString& folder) const {
        return pdfCounts_.value(QDir::cleanPath(folder).toCaseFolded(), 0);
    }
    bool countsAvailable() const { return countsReady_; }
    // Count PDF files in each directory without adding descendants.
    // Never enumerate filesystem contents from data()/paint(), which would
    // make expanding a folder block the Qt UI and the PDF reader.
    void refreshPdfCounts(const QString& root) {
        countRoot_ = QDir::cleanPath(root);
        ++generation_;
        countsReady_ = false;
        recountTimer_.start(280); // Coalesce drag/move/import bursts.
    }
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override {
        if (role == Qt::DecorationRole && index.isValid() && isDir(index)) {
            const QString folder = filePath(index);
            if (!ids_.contains(folder)) {
                if (ids_.size() > 4096) ids_.clear();
                ids_.insert(folder, folderIconId(folder));
            }
            return embeddedFolderIcon(ids_.value(folder));
        }
        if (role == Qt::DisplayRole && index.isValid() && index.column() == 0 && isDir(index) && countsReady_) {
            const QString label = QFileSystemModel::data(index, role).toString();
            // Folder badge counts PDFs directly inside that folder.
            return QStringLiteral("%1  ·  %2").arg(label).arg(pdfCount(filePath(index)));
        }
        return QFileSystemModel::data(index, role);
    }
    void refreshFolderIcon(const QModelIndex& index) {
        if (!index.isValid()) return;
        ids_.remove(filePath(index));
        Q_EMIT dataChanged(index, index, {Qt::DecorationRole});
    }
private:
    void beginRecount() {
        const QString root = countRoot_;
        const int version = generation_;
        if (root.isEmpty() || !QFileInfo(root).isDir()) return;
        auto result = std::make_shared<QHash<QString, int>>();
        auto* worker = QThread::create([root, result] {
            const QString normalizedRoot = root.toCaseFolded();
            result->insert(normalizedRoot, 0);
            QDirIterator it(root, {QStringLiteral("*.pdf"), QStringLiteral("*.PDF")},
                            QDir::Files | QDir::Readable | QDir::NoDotAndDotDot | QDir::NoSymLinks,
                            QDirIterator::Subdirectories);
            // This is off the GUI thread. No PDF bytes are opened or uploaded.
            while (it.hasNext()) {
                const QFileInfo pdf(it.next());
                if (!pdf.isFile() || pdf.isSymLink() ||
                    pdf.suffix().compare(QStringLiteral("pdf"), Qt::CaseInsensitive) != 0) continue;
                QString folder = QDir::cleanPath(pdf.absolutePath()).toCaseFolded();
                if (folder == normalizedRoot ||
                    folder.startsWith(normalizedRoot + QLatin1Char('/')))
                    result->insert(folder, result->value(folder) + 1);
            }
        });
        connect(worker, &QThread::finished, this, [this, result, version] {
            if (generation_ != version) return;
            pdfCounts_ = *result;
            countsReady_ = true;
            if (countsUpdated) countsUpdated();
        });
        connect(qApp, &QCoreApplication::aboutToQuit, worker, [worker] { worker->wait(); });
        connect(worker, &QThread::finished, worker, &QObject::deleteLater);
        worker->start(QThread::LowPriority);
    }
    QTimer recountTimer_;
    QString countRoot_;
    int generation_ = 0;
    bool countsReady_ = false;
    QHash<QString, int> pdfCounts_;
    mutable QHash<QString, QString> ids_;
};
// Native file drags use URLs rather than Qt model indices: the destination
// remains a real directory and never receives an in-memory-only library row.
class PaperFileList final : public QListWidget {
public:
    using QListWidget::QListWidget;
protected:
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::RightButton) {
            if (auto* item = itemAt(e->position().toPoint());
                selectedItems().size() > 1 || (item && item->isSelected())) {
                e->accept();
                return;
            }
        }
        QListWidget::mousePressEvent(e);
    }
    void mouseReleaseEvent(QMouseEvent* e) override {
        if (e->button() == Qt::RightButton) { e->accept(); return; }
        QListWidget::mouseReleaseEvent(e);
    }
    QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override {
        auto* data = new QMimeData;
        QList<QUrl> urls;
        QSet<QString> seen;
        for (auto* item : items) {
            const QString path = item->data(Qt::UserRole).toString();
            const QFileInfo file(path);
            if (file.isFile() && !seen.contains(file.canonicalFilePath())) {
                seen.insert(file.canonicalFilePath());
                urls.append(QUrl::fromLocalFile(file.canonicalFilePath()));
            }
        }
        data->setUrls(urls);
        return data;
    }
    Qt::DropActions supportedDropActions() const override {
        return Qt::CopyAction | Qt::MoveAction;
    }
    void startDrag(Qt::DropActions) override {
        const auto chosen = selectedItems();
        QMimeData* files = mimeData(chosen);
        if (!files || files->urls().isEmpty()) { delete files; return; }
        // Never drag a 322px card or the entire 112px row over the folder tree.
        // The cursor always carries a small, fixed-size PDF badge.
        QPixmap badge(132, 38);
        badge.fill(Qt::transparent);
        QPainter painter(&badge);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(QColor("#B4CAE7"), 1));
        painter.setBrush(QColor("#F5F9FF"));
        painter.drawRoundedRect(QRect(1, 1, 129, 35), 7, 7);
        readerIcon(QStringLiteral("documents")).paint(&painter, QRect(9, 8, 20, 21));
        painter.setPen(QColor("#254D82"));
        painter.drawText(QRect(34, 3, 94, 32), Qt::AlignVCenter,
                         QStringLiteral("%1 PDF").arg(files->urls().size()));
        painter.end();
        auto* drag = new QDrag(this);
        drag->setMimeData(files);
        drag->setPixmap(badge);
        drag->setHotSpot(QPoint(13, 18));
        // The target applies the actual filesystem move/copy. Do not let the
        // default Qt item-view drag remove model rows afterward.
        drag->exec(Qt::MoveAction | Qt::CopyAction, Qt::MoveAction);
        drag->deleteLater();
    }
};
class FolderFileDropTree final : public QTreeView {
public:
    using QTreeView::QTreeView;
    std::function<void(const QStringList&, const QString&, bool)> dropped;
protected:
    static QStringList pdfPaths(const QMimeData* data) {
        QStringList result;
        if (!data || !data->hasUrls()) return result;
        const auto urls = data->urls();
        if (urls.isEmpty()) return result;
        for (const auto& url : urls) {
            if (!url.isLocalFile()) return {};
            const QFileInfo file(url.toLocalFile());
            if (!file.isFile() || file.isSymLink() ||
                file.suffix().compare(QStringLiteral("pdf"), Qt::CaseInsensitive) != 0)
                return {};
            if (!result.contains(file.canonicalFilePath()))
                result.append(file.canonicalFilePath());
        }
        return result;
    }
    QString folderAt(const QPoint& point) const {
        auto* folders = qobject_cast<QFileSystemModel*>(model());
        const QModelIndex index = indexAt(point);
        if (!folders || !index.isValid() || !folders->isDir(index)) return {};
        const QFileInfo folder(folders->filePath(index));
        return folder.isDir() && !folder.isSymLink() ? folder.canonicalFilePath() : QString();
    }
    void dragEnterEvent(QDragEnterEvent* event) override {
        if (pdfPaths(event->mimeData()).isEmpty()) { event->ignore(); return; }
        event->acceptProposedAction();
    }
    void dragMoveEvent(QDragMoveEvent* event) override {
        if (folderAt(event->position().toPoint()).isEmpty() ||
            pdfPaths(event->mimeData()).isEmpty()) { event->ignore(); return; }
        // Files dragged from Windows Explorer are imports: NEVER move their
        // originals. Only paper cards originating in LUNA default to move.
        const auto* sourceList = qobject_cast<QListWidget*>(event->source());
        const bool internal = sourceList &&
            sourceList->objectName() == QStringLiteral("LunaDocumentList");
        const bool copy = !internal || bool(event->modifiers() & Qt::ControlModifier);
        event->setDropAction(copy ? Qt::CopyAction : Qt::MoveAction);
        event->accept();
    }
    void dropEvent(QDropEvent* event) override {
        const auto files = pdfPaths(event->mimeData());
        const QString folder = folderAt(event->position().toPoint());
        if (files.isEmpty() || folder.isEmpty() || !dropped) { event->ignore(); return; }
        const auto* sourceList = qobject_cast<QListWidget*>(event->source());
        const bool internal = sourceList &&
            sourceList->objectName() == QStringLiteral("LunaDocumentList");
        const bool copy = !internal || bool(event->modifiers() & Qt::ControlModifier);
        dropped(files, folder, copy);
        event->setDropAction(copy ? Qt::CopyAction : Qt::MoveAction);
        event->accept();
    }
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
        for (const QString& path : savedPaths(QStringLiteral("library/recent"))) {
            const QString date = openedDates_.value(openedDateKey(path));
            if (!date.isEmpty()) rememberDateAlias(path, date);
        }
        setMinimumWidth(0);
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(7, 7, 0, 7);
        split_ = new QSplitter(Qt::Horizontal, this);
        split_->setObjectName(QStringLiteral("LunaLibrarySplitter"));
        split_->setChildrenCollapsible(false);
        split_->setHandleWidth(5);
        layout->addWidget(split_);
        // Keep both splitter children in their ORIGINAL order even when
        // either content panel is collapsed. Each owns its own 36px inline
        // restore tab: library on the left, paper list directly after it.
        navigationSlot_ = new QWidget(split_);
        navigationSlot_->setObjectName(QStringLiteral("LunaNavigationSlot"));
        auto* navigationSlotLayout = new QHBoxLayout(navigationSlot_);
        navigationSlotLayout->setContentsMargins(0, 0, 0, 0);
        navigationSlotLayout->setSpacing(0);
        auto* navigationScroll = new QScrollArea(navigationSlot_);
        navigationScroll->setObjectName(QStringLiteral("LunaNavigation"));
        navigationSlotLayout->addWidget(navigationScroll, 1);
        navigationFold_ = new QWidget(navigationSlot_);
        navigationFold_->setObjectName(QStringLiteral("LunaNavigationFold"));
        navigationFold_->setAttribute(Qt::WA_StyledBackground, true);
        navigationFold_->setFixedWidth(foldWidth_);
        auto* navigationFoldLayout = new QVBoxLayout(navigationFold_);
        navigationFoldLayout->setContentsMargins(3, 0, 3, 0);
        navigationFoldLayout->setAlignment(Qt::AlignTop);
        navigationFoldLayout->setSpacing(0);
        navigationFold_->hide();
        navigationSlotLayout->addWidget(navigationFold_);
        split_->addWidget(navigationSlot_);
        split_->setCollapsible(0, false);
        navigationScroll->setWidgetResizable(true);
        navigationScroll->setFrameShape(QFrame::NoFrame);
        navigationScroll->setAttribute(Qt::WA_StyledBackground, true);
        navigationScroll->setContentsMargins(0, 0, 1, 0);
        navigationScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        navigationScroll->setMinimumHeight(0);
        navigation_ = navigationScroll;
        navigation_->setMinimumWidth(175);
        navigation_->setMaximumWidth(300);
        auto* navigationContents = new QWidget(navigationScroll);
        navigationContents->setObjectName(QStringLiteral("LunaNavigationContents"));
        navigationScroll->setWidget(navigationContents);
        new SmoothLibraryWheel(navigationScroll, 58);
        navigationContents->setAttribute(Qt::WA_StyledBackground, true);
        navigationContents->setAutoFillBackground(true);
        navigationScroll->viewport()->setAttribute(Qt::WA_StyledBackground, true);
        navigationScroll->viewport()->setAutoFillBackground(true);
        navigationScroll->viewport()->setObjectName(QStringLiteral("LunaNavigationViewport"));
        auto* nav = new QVBoxLayout(navigationContents);
        nav->setSizeConstraint(QLayout::SetMinimumSize);
        nav->setContentsMargins(10, 0, 10, 12);
        nav->setSpacing(0);
        auto* libraryHeader = new QWidget(navigation_);
        libraryHeader->setFixedHeight(41);
        libraryHeader->setObjectName(QStringLiteral("LunaLibraryHeader"));
        auto* libraryHeaderLayout = new QHBoxLayout(libraryHeader);
        libraryHeaderLayout->setContentsMargins(7, 0, 0, 0);
        libraryHeaderLayout->setSpacing(9);
        auto* shelf = new QLabel(libraryHeader);
        shelf->setPixmap(readerIcon(QStringLiteral("library")).pixmap(24, 24));
        shelf->setAccessibleName(QStringLiteral("라이브러리"));
        shelf->setFixedSize(24, 24);
        libraryHeaderLayout->addWidget(shelf);
        shelf->hide();
        auto* section = new QLabel(QStringLiteral("라이브러리"), libraryHeader);
        section->setObjectName(QStringLiteral("LunaLibraryTitle"));
        QFont headingFont(QStringLiteral("Malgun Gothic")); headingFont.setPixelSize(18); headingFont.setBold(true); headingFont.setLetterSpacing(QFont::AbsoluteSpacing, -.45); section->setFont(headingFont);
        libraryHeaderLayout->addWidget(section, 1);
        section->hide();
        libraryHeaderLayout->addStretch(1);
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
        // A long root-folder name must not widen the scroll contents beyond
        // its viewport and push the header's collapse button out of reach.
        folderTab_->setSizePolicy(QSizePolicy::Ignored, folderTab_->sizePolicy().verticalPolicy());
        auto* favorites = navButton(QStringLiteral("즐겨찾기"), QStringLiteral("star"));
        auto* recent = navButton(QStringLiteral("최근 열람"), QStringLiteral("recent"));
        pdfTab_->setObjectName(QStringLiteral("LunaAllDocumentsButton"));
        folderTab_->setObjectName(QStringLiteral("LunaFolderDocumentsButton"));
        recent->setObjectName(QStringLiteral("LunaRecentDocumentsButton"));
        favorites->setObjectName(QStringLiteral("LunaFavoriteDocumentsButton"));
        for (auto* b : {pdfTab_, folderTab_, recent, favorites}) b->setProperty("lunaNavigation", true);
        folderTab_->setChecked(true);
        folderTab_->setAcceptDrops(true);
        folderTab_->installEventFilter(this);
        folderTab_->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(folderTab_, &QPushButton::customContextMenuRequested, this, [this](const QPoint& point) {
            QMenu menu(this);
            auto* rename = menu.addAction(QStringLiteral("최상위 폴더 이름 변경…"));
            auto* paste = menu.addAction(QStringLiteral("최상위 폴더에 PDF 붙여넣기"));
            auto* chosen = menu.exec(folderTab_->mapToGlobal(point));
            if (chosen == rename) renameFolder(root_);
            else if (chosen == paste) pastePdfs(root_);
        });
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
        // One folder picker serves both ordinary libraries and desktop-synced folders.
        nav->addLayout(folders);
        rootLabel_ = new QLabel(navigation_);
        rootLabel_->setObjectName(QStringLiteral("LunaSecondary"));
        rootLabel_->setWordWrap(true);
        rootLabel_->setTextFormat(Qt::PlainText);
        rootLabel_->hide();
        nav->addWidget(folderTab_);
        model_ = new LunaFolderModel(this);
        model_->countsUpdated = [this] { if (tree_) tree_->viewport()->update(); queueNavigationCounts(); };
        model_->setReadOnly(true); model_->setIconProvider(new FolderIcons(model_));
        model_->setFilter(QDir::AllDirs | QDir::NoDotAndDotDot);
        tree_ = new FolderFileDropTree(navigation_);
        tree_->setObjectName(QStringLiteral("LunaFolderDropTree"));
        tree_->setSelectionMode(QAbstractItemView::SingleSelection);
        tree_->setAcceptDrops(true);
        tree_->viewport()->setAcceptDrops(true);
        tree_->setDragDropMode(QAbstractItemView::DropOnly);
        static_cast<FolderFileDropTree*>(tree_)->dropped = [this](const QStringList& paths, const QString& folder, bool copy) {
            transferPdfs(paths, folder, copy);
        };
        tree_->setModel(model_);
        tree_->setHeaderHidden(true);
        tree_->setAnimated(true);
        tree_->setIndentation(16); tree_->setUniformRowHeights(true); tree_->setIconSize(QSize(20, 20));
        tree_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        new SmoothLibraryWheel(tree_, 58);
        tree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        tree_->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(tree_, &QTreeView::customContextMenuRequested, this, [this](const QPoint& point) {
            const QModelIndex index = tree_->indexAt(point);
            if (!index.isValid() || !model_->isDir(index)) return;
            const QString folder = model_->filePath(index);
            QMenu menu(this);
            auto* change = menu.addAction(QStringLiteral("폴더 아이콘 변경…"));
            change->setObjectName(QStringLiteral("LunaChangeFolderIcon"));
            auto* reset = menu.addAction(QStringLiteral("기본 아이콘으로 복원"));
            reset->setObjectName(QStringLiteral("LunaResetFolderIcon"));
            menu.addSeparator();
            auto* rename = menu.addAction(QStringLiteral("이름 변경 (F2)"));
            rename->setObjectName(QStringLiteral("LunaRenameFolder"));
            auto* paste = menu.addAction(QStringLiteral("이 폴더에 PDF 붙여넣기 (Ctrl+V)"));
            reset->setEnabled(folderIconId(folder) != QStringLiteral("general"));
            auto* chosen = menu.exec(tree_->viewport()->mapToGlobal(point));
            if (chosen == change) {
                QDialog dialog(this);
                dialog.setObjectName(QStringLiteral("LunaFolderIconPicker"));
                dialog.setWindowTitle(QStringLiteral("폴더 아이콘 변경"));
                auto* layout = new QVBoxLayout(&dialog);
                layout->setContentsMargins(10, 10, 10, 10);
                QString selected;
                auto* tabs = new QTabWidget(&dialog);
                tabs->setObjectName(QStringLiteral("LunaFolderIconTabs"));
                auto makeGrid = [&](const QString& name, int count, auto bindButton) {
                    auto* scroll = new QScrollArea(tabs);
                    scroll->setWidgetResizable(true);
                    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
                    auto* body = new QWidget(scroll);
                    auto* grid = new QGridLayout(body);
                    grid->setContentsMargins(6, 6, 6, 6);
                    grid->setSpacing(7);
                    for (int i = 0; i < count; ++i) {
                        auto* button = new QToolButton(body);
                        bindButton(button, i);
                        button->setIconSize(QSize(44, 44));
                        button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
                        button->setFixedSize(110, 88);
                        grid->addWidget(button, i / 4, i % 4);
                    }
                    grid->setRowStretch((count + 3) / 4, 1);
                    scroll->setWidget(body);
                    tabs->addTab(scroll, name);
                    return scroll;
                };
                makeGrid(QStringLiteral("폴더"), 20, [&](QToolButton* button, int i) {
                    const auto& preset = kFolderIconPresets[i];
                    const QString id = QString::fromLatin1(preset.id);
                    button->setObjectName(QStringLiteral("LunaFolderIconOption_%1").arg(id));
                    button->setText(QString::fromUtf8(preset.label));
                    button->setIcon(embeddedFolderIcon(id));
                    connect(button, &QToolButton::clicked, &dialog, [&, id] {
                        selected = id;
                        dialog.accept();
                    });
                });
                auto* symbols = new QWidget(tabs);
                auto* symbolLayout = new QVBoxLayout(symbols);
                symbolLayout->setContentsMargins(0, 0, 0, 0);
                auto* filters = new QHBoxLayout;
                auto* search = new QLineEdit(symbols);
                search->setObjectName(QStringLiteral("LunaSymbolSearch"));
                search->setPlaceholderText(QStringLiteral("일반 아이콘 검색"));
                filters->addWidget(search, 1);
                auto* category = new QComboBox(symbols);
                category->setObjectName(QStringLiteral("LunaSymbolCategory"));
                category->addItem(QStringLiteral("전체"), QString());
                category->addItem(QStringLiteral("인기·즐겨찾기"), QStringLiteral("favorites"));
                category->addItem(QStringLiteral("연구·학습"), QStringLiteral("research"));
                category->addItem(QStringLiteral("작업·관리"), QStringLiteral("workflow"));
                category->addItem(QStringLiteral("감성·개인화"), QStringLiteral("personal"));
                filters->addWidget(category);
                symbolLayout->addLayout(filters);
                auto* symbolScroll = new QScrollArea(symbols);
                symbolScroll->setWidgetResizable(true);
                symbolScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
                auto* symbolBody = new QWidget(symbolScroll);
                auto* symbolGrid = new QGridLayout(symbolBody);
                symbolGrid->setContentsMargins(6, 6, 6, 6);
                symbolGrid->setSpacing(7);
                QList<QToolButton*> symbolButtons;
                for (const auto& preset : kSymbolIconPresets) {
                    const QString id = QStringLiteral("symbol:") + QString::fromLatin1(preset.id);
                    auto* button = new QToolButton(symbolBody);
                    button->setObjectName(QStringLiteral("LunaSymbolIconOption_%1").arg(QString::fromLatin1(preset.id)));
                    button->setText(QString::fromUtf8(preset.label));
                    button->setIcon(embeddedFolderIcon(id));
                    button->setIconSize(QSize(44, 44));
                    button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
                    button->setFixedSize(110, 88);
                    button->setProperty("symbolCategory", QString::fromLatin1(preset.category));
                    button->setProperty("symbolId", id);
                    symbolButtons.append(button);
                    connect(button, &QToolButton::clicked, &dialog, [&, id] {
                        selected = id;
                        dialog.accept();
                    });
                }
                auto updateSymbolFilter = [=] {
                    const QString query = search->text().trimmed();
                    const QString activeCategory = category->currentData().toString();
                    int visible = 0;
                    for (auto* button : symbolButtons) {
                        const bool show = (activeCategory.isEmpty() ||
                                           button->property("symbolCategory").toString() == activeCategory) &&
                            (query.isEmpty() || button->text().contains(query, Qt::CaseInsensitive) ||
                             button->property("symbolId").toString().contains(query, Qt::CaseInsensitive));
                        button->setVisible(show);
                        if (show) {
                            symbolGrid->addWidget(button, visible / 4, visible % 4);
                            ++visible;
                        }
                    }
                };
                for (int i = 0; i < symbolButtons.size(); ++i)
                    symbolGrid->addWidget(symbolButtons[i], i / 4, i % 4);
                connect(search, &QLineEdit::textChanged, symbols, [=](const QString&) { updateSymbolFilter(); });
                connect(category, QOverload<int>::of(&QComboBox::currentIndexChanged),
                        symbols, [=](int) { updateSymbolFilter(); });
                symbolScroll->setWidget(symbolBody);
                symbolLayout->addWidget(symbolScroll);
                tabs->addTab(symbols, QStringLiteral("일반"));
                tabs->setCurrentIndex(folderIconId(folder).startsWith(QStringLiteral("symbol:")) ? 1 : 0);
                layout->addWidget(tabs, 1);
                dialog.resize(520, 470);
                auto* cancel = new QPushButton(QStringLiteral("취소"), &dialog);
                connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
                layout->addWidget(cancel, 0, Qt::AlignRight);
                if (dialog.exec() == QDialog::Accepted && !selected.isEmpty()) {
                    QString error;
                    if (!saveFolderIcon(folder, selected, &error))
                        QMessageBox::warning(this, QStringLiteral("폴더 아이콘 변경 실패"), error);
                }
            } else if (chosen == reset) {
                QString error;
                if (!resetFolderIcon(folder, &error))
                    QMessageBox::warning(this, QStringLiteral("아이콘 초기화 실패"), error);
            } else if (chosen == rename) renameFolder(folder);
            else if (chosen == paste) pastePdfs(folder);
        });
        auto* renameFolderAction = new QAction(QStringLiteral("폴더 이름 변경"), tree_);
        renameFolderAction->setShortcut(QKeySequence(Qt::Key_F2));
        renameFolderAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        tree_->addAction(renameFolderAction);
        connect(renameFolderAction, &QAction::triggered, this, [this] {
            const QModelIndex index = tree_->currentIndex();
            if (index.isValid() && model_->isDir(index))
                renameFolder(model_->filePath(index));
        });
        auto* pasteFolderAction = new QAction(QStringLiteral("PDF를 폴더에 붙여넣기"), tree_);
        pasteFolderAction->setShortcut(QKeySequence::Paste);
        pasteFolderAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        tree_->addAction(pasteFolderAction);
        connect(pasteFolderAction, &QAction::triggered, this, [this] {
            const QModelIndex index = tree_->currentIndex();
            if (index.isValid() && model_->isDir(index))
                pastePdfs(model_->filePath(index));
        });
        for (int i = 1; i < 4; ++i) tree_->hideColumn(i);
        nav->addWidget(tree_, 1);
        // Poll ONLY visible tree rows: changes delivered by MYBOX on another
        // PC should appear without rescanning a large local paper library.
        folderIconPoll_ = new QTimer(this);
        folderIconPoll_->setInterval(3000);
        connect(folderIconPoll_, &QTimer::timeout, this, [this] { refreshVisibleFolderIcons(); });
        folderIconPoll_->start();
        // External Explorer/MYBOX changes may not go through LUNA's own
        // import/move actions. Refresh the badges periodically off-thread.
        auto* folderCountPoll = new QTimer(this);
        folderCountPoll->setInterval(60000);
        connect(folderCountPoll, &QTimer::timeout, this, [this] {
            if (tree_ && tree_->isVisible() && !root_.isEmpty())
                model_->refreshPdfCounts(root_);
        });
        folderCountPoll->start();
        auto* location = new QPushButton(readerIcon(QStringLiteral("open")), QString(), libraryHeader);
        location->setFixedSize(30, 30);
        location->setToolTip(QStringLiteral("라이브러리 폴더 선택 (MYBOX 등 동기화 폴더 사용 가능)"));
        location->setAccessibleName(QStringLiteral("라이브러리 위치 선택"));
        location->setObjectName(QStringLiteral("LunaLocation"));
        libraryHeaderLayout->addWidget(location);
        documentsSlot_ = new QWidget(split_);
        documentsSlot_->setObjectName(QStringLiteral("LunaDocumentsSlot"));
        auto* documentsSlotLayout = new QHBoxLayout(documentsSlot_);
        documentsSlotLayout->setContentsMargins(0, 0, 0, 0);
        documentsSlotLayout->setSpacing(0);
        documents_ = new QWidget(documentsSlot_);
        documents_->setObjectName(QStringLiteral("LunaDocuments"));
        documentsSlotLayout->addWidget(documents_, 1);
        documentsFold_ = new QWidget(documentsSlot_);
        documentsFold_->setObjectName(QStringLiteral("LunaDocumentsFold"));
        documentsFold_->setAttribute(Qt::WA_StyledBackground, true);
        documentsFold_->setFixedWidth(foldWidth_);
        auto* documentsFoldLayout = new QVBoxLayout(documentsFold_);
        documentsFoldLayout->setContentsMargins(3, 0, 3, 0);
        documentsFoldLayout->setAlignment(Qt::AlignTop);
        documentsFoldLayout->setSpacing(0);
        documentsFold_->hide();
        documentsSlotLayout->addWidget(documentsFold_);
        split_->addWidget(documentsSlot_);
        split_->setCollapsible(1, false);
        documents_->setAttribute(Qt::WA_StyledBackground, true);
        documents_->setMinimumWidth(145);
        auto* docs = new QVBoxLayout(documents_);
        // Match the 7px top inset of LunaLibraryPanel's outer layout.
        // The paper-header button was 7px ABOVE the library-header button:
        // because each folded rail copies its own header Y, the two restore
        // icons were also visibly staggered even though both were centered
        // horizontally in their original 36px slots.
        docs->setContentsMargins(0, 7, 1, 0);
        docs->setSpacing(0);
        auto* heading = new QWidget(documents_);
        auto* headingLayout = new QVBoxLayout(heading);
        heading->setObjectName(QStringLiteral("LunaPaperHeader"));
        headingLayout->setContentsMargins(8, 0, 8, 0);
        headingLayout->setSpacing(0);
        auto* title = new QLabel(QStringLiteral("논문 목록"), heading);
        title->setObjectName(QStringLiteral("LunaLibraryTitle"));
        title->setFont(headingFont);
        title->setFixedHeight(41);
        title->setMinimumWidth(0);
        title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        auto* titleRow = new QHBoxLayout;
        titleRow->setObjectName(QStringLiteral("LunaPaperHeadingLayout"));
        titleRow->setSpacing(3);
        // Right-side controls stay visible when the title has to contract.
        titleRow->addWidget(title, 1);
        title->hide();
        titleRow->insertStretch(0, 1);
        headingLayout->addLayout(titleRow);
        // Both paper views keep the same vertical QListWidget and model.
        compactPaperMode_ = QSettings(QSettings::defaultFormat(), QSettings::UserScope,
            QStringLiteral("LUNA"), QStringLiteral("LUNA Library"))
            .value(QStringLiteral("library/paperViewMode"), QStringLiteral("card"))
            .toString() == QStringLiteral("list");
        auto* paperViewGroup = new QButtonGroup(this);
        paperViewGroup->setExclusive(true);
        auto* listMode = new QToolButton(heading);
        listMode->setObjectName(QStringLiteral("LunaListViewMode"));
        listMode->setAccessibleName(QStringLiteral("논문 목록형 보기"));
        listMode->setToolTip(QStringLiteral("목록형 보기 · 작은 썸네일과 서지정보"));
        listMode->setIcon(readerIcon(QStringLiteral("list-mode")));
        listMode->setIconSize(QSize(19, 19));
        listMode->setFixedSize(29, 30);
        listMode->setCheckable(true);
        paperViewGroup->addButton(listMode);
        titleRow->addWidget(listMode);
        auto* cardMode = new QToolButton(heading);
        cardMode->setObjectName(QStringLiteral("LunaCardViewMode"));
        cardMode->setAccessibleName(QStringLiteral("논문 카드형 보기"));
        cardMode->setToolTip(QStringLiteral("카드형 보기 · 큰 썸네일과 제목"));
        cardMode->setIcon(readerIcon(QStringLiteral("card-mode")));
        cardMode->setIconSize(QSize(19, 19));
        cardMode->setFixedSize(29, 30);
        cardMode->setCheckable(true);
        paperViewGroup->addButton(cardMode);
        titleRow->addWidget(cardMode);
        listMode->setChecked(compactPaperMode_);
        cardMode->setChecked(!compactPaperMode_);
        connect(listMode, &QToolButton::clicked, this, [this] { setPaperViewMode(true); });
        connect(cardMode, &QToolButton::clicked, this, [this] { setPaperViewMode(false); });
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
            pagePreviewAction_->setToolTip(QStringLiteral("페이지 미리보기를 사용할 수 없습니다."));
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
            pages_->setCurrentIndex(0);
            selectFolder(path);
        });
        connect(folderTab_, &QPushButton::clicked, this, [this] {
            pages_->setCurrentIndex(0);
            // The root shortcut always selects and shows the root folder itself.
            selectFolder(root_);
        });
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
        widthSaveTimer_.setSingleShot(true);
        widthSaveTimer_.setInterval(250);
        connect(&widthSaveTimer_, &QTimer::timeout, this, [this] { savePanelState(); });
        // Qt may coalesce native QDockWidget resize events. Poll the actual
        // settled width, independently of QSplitter::splitterMoved.
        auto* expandedDockPoll = new QTimer(this);
        expandedDockPoll->setInterval(200);
        connect(expandedDockPoll, &QTimer::timeout, this, [this] {
            if (adjustingPanels_ || navigation_->isHidden() || documents_->isHidden()) return;
            auto* dock = qobject_cast<QDockWidget*>(parentWidget());
            if (!dock || !dock->isVisible()) return;
            const int actualWidth = dock->width();
            if (actualWidth < 432 || actualWidth > 850) return;
            QSettings settings(QSettings::defaultFormat(), QSettings::UserScope,
                               QStringLiteral("LUNA"), QStringLiteral("LUNA Library"));
            if (settings.value(QStringLiteral("library/expandedDockWidth")).toInt() == actualWidth)
                return;
            settings.setValue(QStringLiteral("library/expandedDockWidth"), actualWidth);
            settings.sync();
            savePanelState();
        });
        expandedDockPoll->start();
        QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("LUNA"), QStringLiteral("LUNA Library"));
        QString start = settings.value(QStringLiteral("library/root")).toString();
        if (!QFileInfo(start).isDir()) start = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        if (!QFileInfo(start).isDir()) start = QDir::homePath();
        bibliography_.changed = [this](const QString& identity) {
            refreshBibliography(identity);
            // The tab strip uses real bibliography titles, not stale PDF basenames.
            auto* host = window();
            host->setProperty("LunaBibliographyRevision",
                host->property("LunaBibliographyRevision").toInt() + 1);
            if(localSync_ && !applyingRemote_) localSync_->publish(identity);
            // Only metadata changes trigger physical renaming; ordinary browsing
            // never rewrites files. Open PDFs remain queued until they close.
            if (!applyingRemote_ && !identity.isEmpty()) {
                pendingTitleRenames_.insert(identity);
                // A newly verified parent title can rename its already-linked
                // SI without asking the user to revisit every child document.
                const QFileInfo parent(identity);
                if (parent.isFile() && !bibliography_.record(identity).value("manualSI").toBool()) {
                    QDirIterator siblings(parent.absolutePath(),
                        QStringList{QStringLiteral("*.pdf"),QStringLiteral("*.PDF")},
                        QDir::Files | QDir::NoSymLinks);
                    while (siblings.hasNext()) {
                        const QString child=siblings.next();
                        const QString childId=DocumentClaim::identity(child);
                        if (childId==identity) continue;
                        const QVariantMap metadata=bibliography_.record(childId);
                        if (metadata.value("manualSI").toBool() &&
                            metadata.value("manualSiParentFile").toString().compare(
                                parent.fileName(),Qt::CaseInsensitive)==0)
                            pendingTitleRenames_.insert(childId);
                    }
                }
                if (!titleRenameTimer_.isActive()) titleRenameTimer_.start();
            }
        };
        localSync_ = new luna::LocalFolderSync(this);
        localSync_->localSnapshot = [this](const QString& pdf) {
            return luna::capturePortableRecord(pdf,bibliography_);
        };
        localSync_->remoteChanged = [this](const QString& pdf,const QJsonObject& record) {
            applyingRemote_=true;
            const bool applied=luna::applyPortableRecord(pdf,record,bibliography_);
            if(applied) {
                auto store=luna::lunaLocalStore();
                memos_.insert(openedDateKey(pdf),store.value(QStringLiteral("library/memos/")+openedDateKey(pdf)).toString());
                const auto favorites=store.value(QStringLiteral("library/favorites")).toStringList();
                favorites_=QSet<QString>(favorites.cbegin(),favorites.cend());
            }
            applyingRemote_=false;
            return applied;
        };
        localSync_->renamed = [this](const QString& before,const QString& after) {
            QString error;
            if(!luna::reconcileExplorerRename(before,after,bibliography_,&error)) {
                if(!error.isEmpty()) qWarning().noquote()<<error;
                return;
            }
            const QString oldKey=openedDateKey(before),newKey=openedDateKey(after);
            if(memos_.contains(oldKey)) memos_.insert(newKey,memos_.take(oldKey));
            if(openedDates_.contains(oldKey)) openedDates_.insert(newKey,openedDates_.take(oldKey));
            const auto favorites=savedPaths(QStringLiteral("library/favorites"));
            favorites_=QSet<QString>(favorites.cbegin(),favorites.cend());
            ThumbnailCache::instance()->invalidate();
            // Explorer renaming the original paper must update only its
            // explicitly attached, same-folder SI records; no PDF bytes move.
            const QFileInfo oldPdf(before), renamed(after);
            if(oldPdf.absolutePath()==renamed.absolutePath()) {
                QDirIterator pdfs(renamed.absolutePath(),
                    QStringList{QStringLiteral("*.pdf"),QStringLiteral("*.PDF")},QDir::Files);
                while(pdfs.hasNext()) {
                    const QString child=pdfs.next();
                    if(child==after || QFileInfo(child).isSymLink())continue;
                    const QString id=DocumentClaim::identity(child);
                    const QVariantMap data=bibliography_.record(id);
                    if(data.value("manualSI").toBool() &&
                       data.value("manualSiParentFile").toString().compare(
                           oldPdf.fileName(),Qt::CaseInsensitive)==0) {
                        if(bibliography_.attachManualSI(id,renamed.fileName()) && localSync_)
                            localSync_->publish(child);
                    }
                }
            }
        };
        localSync_->metadataChanged = [this](const QString& pdf) {
            refreshBibliography(DocumentClaim::identity(pdf));
        };
        localSync_->filesChanged = [this] {
            if (folderTimer_->isActive() || allTimer_->isActive()) return;
            refreshFileLists();
        };
        titleRenameTimer_.setInterval(2000);
        connect(&titleRenameTimer_, &QTimer::timeout, this, [this] {
            int budget = 3;
            for (const QString& identity : pendingTitleRenames_.values()) {
                if (--budget < 0) break;
                if (renameExtractedTitleFile(identity)) pendingTitleRenames_.remove(identity);
            }
            if (pendingTitleRenames_.isEmpty()) titleRenameTimer_.stop();
        });
        metadataFetcher_ = new luna::MetadataFetcher(this);
        selectRoot(start);
        split_->restoreState(settings.value(QStringLiteral("library/splitter")).toByteArray());
        // Restore only the two original expanded widths. The old splitter
        // state has two children, so it remains compatible with this wrapper.
        expandedNavWidth_ = qBound(225, settings.value(
            QStringLiteral("library/expandedNavigationWidth"), 225).toInt(), 300);
        expandedDocumentsWidth_ = qBound(195, settings.value(
            QStringLiteral("library/expandedDocumentsWidth"), 325).toInt(), 650);
        // The shell stylesheet exists before these child layouts are built.
        // Refresh selector caches after naming/populating the panels, so an
        // early layout polish cannot leave the old inherited canvas/border.
        for (QWidget* panel : {navigation_, documents_}) {
            panel->style()->unpolish(panel);
            panel->style()->polish(panel);
            panel->setAttribute(Qt::WA_StyledBackground, true);
        }
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
    static constexpr int foldWidth_ = 36;
    QWidget* navigationFold() const { return navigationFold_; }
    QWidget* documentsFold() const { return documentsFold_; }
    void updateMinimum() {
        const QScopedValueRollback<bool> updating(adjustingPanels_, true);
        const bool showNav = !navigation_->isHidden();
        const bool showDocs = !documents_->isHidden();
        navigationFold_->setVisible(!showNav);
        documentsFold_->setVisible(!showDocs);
        // QSplitter children are never removed or reordered. Unlike a
        // floating global restore bar, each fold stays at its old x position.
        navigationSlot_->setMinimumWidth(showNav ? 175 : foldWidth_);
        navigationSlot_->setMaximumWidth(showNav ? 300 : foldWidth_);
        documentsSlot_->setMinimumWidth(showDocs ? 145 : foldWidth_);
        documentsSlot_->setMaximumWidth(showDocs ? 850 : foldWidth_);
        const auto margins = layout()->contentsMargins();
        const int framing = split_->handleWidth() + margins.left() + margins.right();
        const int collapsedTarget = (showNav ? expandedNavWidth_ : foldWidth_) +
                                    (showDocs ? expandedDocumentsWidth_ : foldWidth_) + framing;
        const int minWidth = (showNav ? 175 : foldWidth_) +
                             (showDocs ? 145 : foldWidth_) + framing;
        setMinimumWidth(minWidth);
        setMaximumWidth(showNav && showDocs ? 850 : qMax(minWidth, collapsedTarget));
        updateGeometry();
        if (auto* dock = qobject_cast<QDockWidget*>(parentWidget())) {
            dock->setMinimumWidth(1);
            dock->setMaximumWidth(showNav && showDocs ? 850 : qMax(minWidth, collapsedTarget));
            dock->show(); // Both folded rails remain visible, never disappear.
            dock->updateGeometry();
            if (auto* main = qobject_cast<QMainWindow*>(dock->parentWidget())) {
                const int requested = showNav && showDocs ?
                    qBound(432, QSettings(QSettings::defaultFormat(), QSettings::UserScope,
                        QStringLiteral("LUNA"), QStringLiteral("LUNA Library"))
                        .value(QStringLiteral("library/expandedDockWidth"), 562).toInt(), 850) :
                    collapsedTarget;
                main->resizeDocks({dock}, {requested}, Qt::Horizontal);
                QTimer::singleShot(0, this, [this, dock, requested, showNav, showDocs] {
                    if (!dock->isVisible()) return;
                    if (auto* main = qobject_cast<QMainWindow*>(dock->parentWidget()))
                        main->resizeDocks({dock}, {requested}, Qt::Horizontal);
                    // QSplitter otherwise remembers the narrowed 36px size and
                    // expands navigation only to its minimum, losing custom widths.
                    split_->setSizes({showNav ? expandedNavWidth_ : foldWidth_,
                                      showDocs ? expandedDocumentsWidth_ : foldWidth_});
                });
            }
        }
    }
    // Measure the live header before hiding it. The two header layouts have
    // different heights; using a shared pixel offset would shift one button.
    void anchorFoldButton(QWidget* slot, QWidget* fold, const QString& name) {
        if (auto* button = slot->findChild<QToolButton*>(name)) {
            auto margins = fold->layout()->contentsMargins();
            margins.setTop(qMax(0, button->mapTo(slot, QPoint()).y()));
            fold->layout()->setContentsMargins(margins);
        }
    }
    void toggleNavigation() {
        if (!navigation_->isHidden()) anchorFoldButton(navigationSlot_, navigationFold_,
            QStringLiteral("LunaNavigationCollapseButton"));
        if (!navigation_->isHidden() && !documents_->isHidden()) savePanelState();
        navigation_->setVisible(navigation_->isHidden());
        updateMinimum();
    }
    void toggleDocuments() {
        if (!documents_->isHidden()) anchorFoldButton(documentsSlot_, documentsFold_,
            QStringLiteral("LunaDocumentsCollapseButton"));
        if (!navigation_->isHidden() && !documents_->isHidden()) savePanelState();
        documents_->setVisible(documents_->isHidden());
        updateMinimum();
    }
    void showBothPanels() { navigation_->show(); documents_->show(); updateMinimum(); }
    void focusSearch() { documents_->show(); updateMinimum(); query_->setFocus(); query_->selectAll(); }
    QLineEdit* searchEdit() const { return query_; }
    void savePanelState() {
        if (adjustingPanels_ || navigation_->isHidden() || documents_->isHidden()) return;
        QSettings settings(QSettings::defaultFormat(), QSettings::UserScope,
                           QStringLiteral("LUNA"), QStringLiteral("LUNA Library"));
        const auto sizes = split_->sizes();
        if (sizes.size() == 2 && sizes[0] >= 225 && sizes[1] >= 195) {
            expandedNavWidth_ = qBound(225, sizes[0], 300);
            expandedDocumentsWidth_ = qBound(195, sizes[1], 650);
            settings.setValue(QStringLiteral("library/splitter"), split_->saveState());
            settings.setValue(QStringLiteral("library/expandedNavigationWidth"), expandedNavWidth_);
            settings.setValue(QStringLiteral("library/expandedDocumentsWidth"), expandedDocumentsWidth_);
        }
        if (auto* dock = qobject_cast<QDockWidget*>(parentWidget()))
            if (dock->width() >= 432 && dock->width() <= 850)
                settings.setValue(QStringLiteral("library/expandedDockWidth"), dock->width());
    }
    // A real tab-close or whole-LUNA exit must drain verified filename work
    // AFTER PDF4QT has closed the reader and released document claim locks.
    // Otherwise a pending two-second timer disappears at normal app exit.
    void flushPendingTitleRenamesAfterReaderClose() {
        titleRenameTimer_.stop();
        for (int round = 0; round < 2 && !pendingTitleRenames_.isEmpty(); ++round) {
            const QStringList pending = pendingTitleRenames_.values();
            bool completed = false;
            for (const QString& identity : pending) {
                if (renameExtractedTitleFile(identity)) {
                    pendingTitleRenames_.remove(identity);
                    completed = true;
                }
            }
            if (!completed) break;
        }
        if (!pendingTitleRenames_.isEmpty() &&
            !window()->property("LunaClosing").toBool()) titleRenameTimer_.start();
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
            if (!query_->text().isEmpty()) filter(list, query_->text());
        }
    }
    // Prefer the exact title. Existing PDFs, records or orphaned managed files
    // get distinct names; never delete an existing user's data.
    QString availableVerifiedTitleName(const QString& identity, const QString& preferred) {
        if (preferred.isEmpty()) return {};
        const QFileInfo source(identity);
        const int budget=qMin(150,235-QFileInfo(source.absolutePath()).absoluteFilePath().size());
        for (int i=1;i<=99;++i) {
            const QString name=i==1?preferred:luna::AutoFileName::numberedCopy(preferred,i,budget);
            if (name.isEmpty()) return {};
            if (source.fileName().compare(name,Qt::CaseInsensitive)==0) return name;
            const QString target=QDir(source.absolutePath()).filePath(name);
            if (QFileInfo::exists(target) || QFileInfo(target).isSymLink() ||
                bibliography_.hasStoredRecord(DocumentClaim::identity(target))) continue;
            bool conflict=false;
            for (const auto& pair:SidecarStorage::companions(identity,target))
                if (QFileInfo::exists(pair.second) || QFileInfo(pair.second).isSymLink()) {
                    conflict=true; break;
                }
            if (!conflict) return name;
        }
        return {};
    }
    // A manually linked SI follows its verified parent's title; never use the
    // SI cover or guessed PDF Info as if it were an independent article title.
    QString verifiedFilenameFor(const QString& identity, const QFileInfo& source) {
        const QVariantMap record=bibliography_.record(identity);
        if (!record.value(QStringLiteral("manualSI")).toBool())
            return luna::AutoFileName::fromVerifiedTitle(record,source.absolutePath());
        const QString parentName=record.value(QStringLiteral("manualSiParentFile")).toString();
        if (!luna::ManualSI::validParentFileName(parentName)) return {};
        const QFileInfo parent(QDir(source.absolutePath()).filePath(parentName));
        if (!parent.isFile() || parent.isSymLink() ||
            parent.canonicalPath().compare(source.canonicalPath(),Qt::CaseInsensitive)!=0)
            return {};
        const QString paperName=luna::AutoFileName::fromVerifiedTitle(
            bibliography_.record(DocumentClaim::identity(parent.canonicalFilePath())),
            source.absolutePath());
        if (paperName.isEmpty()) return {};
        const int available=qMin(150,235-QFileInfo(source.absolutePath()).absoluteFilePath().size());
        if (available < 32) return {};
        QString stem=QStringLiteral("SI · ")+
            luna::ManualSI::safeTitle(paperName.left(paperName.size()-4));
        stem=stem.left(available-4).trimmed();
        while(stem.endsWith(QLatin1Char('.')) || stem.endsWith(QLatin1Char(' ')))stem.chop(1);
        return stem.size()>=8 ? stem+QStringLiteral(".pdf") : QString();
    }
    // Return the actual on-disk result only after the paired move is verified.
    bool renameExtractedTitleFile(const QString& identity, QString* reason = nullptr,
                                 QString* renamedPath = nullptr) {
        const auto explain=[reason](const QString& message) {
            if (reason) *reason=message;
        };
        const QFileInfo source(identity);
        if (root_.isEmpty() || !source.isFile() || source.isSymLink()) {
            explain(QStringLiteral("원본 PDF를 찾을 수 없거나 일반 PDF 파일이 아닙니다."));
            return true;
        }
        const QString original=source.canonicalFilePath();
        if (!original.startsWith(root_ + QLatin1Char('/'), Qt::CaseInsensitive)) {
            explain(QStringLiteral("선택한 PDF가 현재 LUNA 라이브러리의 연결 폴더 밖에 있습니다. 연결 폴더를 확인하세요."));
            return true;
        }
        const QString preferred=verifiedFilenameFor(identity,source);
        const QString filename=availableVerifiedTitleName(identity,preferred);
        if (filename.isEmpty()) {
            explain(QStringLiteral("제목이 유효하지 않거나 동일 제목의 PDF·서지정보가 너무 많아 변경하지 않았습니다."));
            return true;
        }
        if (source.fileName().compare(filename,Qt::CaseInsensitive)==0) {
            explain(QStringLiteral("실제 PDF 파일명이 이미 서지정보 제목과 일치합니다."));
            return true;
        }
        const QString destination=QDir(source.absolutePath()).filePath(filename);
        // Another process may have used our proposed name since selection.
        if (QFileInfo::exists(destination) || QFileInfo(destination).isSymLink() ||
            bibliography_.hasStoredRecord(DocumentClaim::identity(destination))) {
            explain(QStringLiteral("파일명이 다른 작업과 충돌했습니다. 다시 시도하면 번호를 다시 선택합니다."));
            return false;
        }
        // Capture the old opened-date identity before the PDF is closed and
        // its Windows 8.3 / long-name paths are changed by the real rename.
        // A title migration must not discard already recorded opened dates.
        QString renamedOpenedDate=openedDates_.value(openedDateKey(original));
        if (renamedOpenedDate.isEmpty() && !identity.isEmpty())
            renamedOpenedDate=openedDates_.value(openedDateKey(identity));
        if (renamedOpenedDate.isEmpty())
            renamedOpenedDate=displayedDates_.value(identity);
        QPointer<pdfviewer::PDFEditorMainWindow> activeEditor;
        int page=0;
        int scrollPosition=0;
        double zoom=2.0;
        pdf::PageLayout layout=pdf::PageLayout::OneColumn;
        for (auto* editor:window()->findChildren<pdfviewer::PDFEditorMainWindow*>()) {
            auto* controller=editor->getProgramController();
            if (!controller || !controller->getDocument() ||
                DocumentClaim::identity(controller->getOriginalFileName())!=identity)continue;
    
            auto* undo=controller->findChild<pdfviewer::PDFUndoRedoManager*>();
            if (!movedHandler || !editor->isEnabled() || !controller->canClose() ||
                controller->getIsBusy() || !undo || !undo->isCurrentSaved() ||
                editor->property("LunaMoveRestorePending").toBool() ||
                QApplication::activeModalWidget()) {
                explain(QStringLiteral("PDF에 저장되지 않은 변경 또는 진행 중인 작업이 있습니다. 저장 후 자동 변경합니다."));
                return false;
            }
            activeEditor=editor;
            if (renamedOpenedDate.isEmpty()) {
                const QString currentlyOpened=controller->getOriginalFileName();
                renamedOpenedDate=openedDates_.value(openedDateKey(currentlyOpened));
                if (renamedOpenedDate.isEmpty())
                    renamedOpenedDate=displayedDates_.value(
                        DocumentClaim::identity(currentlyOpened));
            }
            const auto pages=controller->getPdfWidget()->getDrawWidget()->getCurrentPages();
            if (!pages.empty())page=int(pages.front());
            zoom=controller->getPdfWidget()->getDrawWidgetProxy()->getZoom();
            layout=controller->getPdfWidget()->getDrawWidgetProxy()->getPageLayout();
            scrollPosition=controller->getPdfWidget()->getVerticalScrollbar()->value();
        }

        std::unique_ptr<QLockFile> renameGuard;
        if (!activeEditor) {
            const QString lockPath=QStandardPaths::writableLocation(
                QStandardPaths::AppLocalDataLocation)+QStringLiteral("/document-claims/")+
                DocumentClaim::endpoint(original)+QStringLiteral(".lock");
            if (!QDir().mkpath(QFileInfo(lockPath).absolutePath())) {
                explain(QStringLiteral("문서 잠금 폴더를 만들 수 없습니다. 쓰기 권한을 확인하세요."));
                return true;
            }
            renameGuard=std::make_unique<QLockFile>(lockPath);
            renameGuard->setStaleLockTime(0);
            if (!renameGuard->tryLock(0)) {
                explain(QStringLiteral("다른 LUNA 프로세스가 이 PDF를 사용 중이거나 잠금 확보에 실패했습니다. PDF 탭을 닫으세요."));
                return false;
            }
        }
        const auto reopenReader=[this,activeEditor,page,zoom,layout,scrollPosition,original](
                const QString& target, bool moved) {
            if (!activeEditor)return;
            auto* controller=activeEditor->getProgramController();
            if (moved && movedHandler) {
                const bool accepted=movedHandler(activeEditor.data(),original,target);
                activeEditor->setProperty("LunaMoveClaimAccepted",accepted);
                if (!accepted) {
                    activeEditor->setProperty("LunaMoveRestorePending",false);
                    qWarning()<<"LUNA auto-rename moved PDF but could not acquire new tab claim:"<<target;
                    return;
                }
            }
            controller->openDocument(target);
            auto* restore=new QTimer(this);
            connect(restore,&QTimer::timeout,this,
                [restore,editor=activeEditor,target,page,zoom,layout,scrollPosition,
                 attempts=0,lastShortRange=-1,shortRangeTicks=0]() mutable {
                    if (!editor) {restore->stop();restore->deleteLater();return;}
                    auto* c=editor->getProgramController();
                    if (++attempts>200) {
                        qWarning()<<"LUNA viewport restore timeout"<<target<<scrollPosition;
                        editor->setProperty("LunaMoveRestorePending",false);
                        restore->stop();restore->deleteLater();return;
                    }
                    if (!editor->isEnabled() || !c->getDocument() ||
                        DocumentClaim::identity(c->getOriginalFileName())!=
                            DocumentClaim::identity(target)) return;
                    c->getPdfWidget()->getDrawWidgetProxy()->setPageLayout(layout);
                    c->getPdfWidget()->getDrawWidgetProxy()->zoom(zoom);
                    c->getPdfWidget()->getDrawWidgetProxy()->goToPage(page);
                    auto* vertical=c->getPdfWidget()->getVerticalScrollbar();
                    // Allow a settled shorter scroll range at high DPI.
                    const int available=vertical->maximum();
                    if (scrollPosition>0 && available<scrollPosition) {
                        if (!editor->isVisible()) return;
                        if (available!=lastShortRange) {
                            lastShortRange=available; shortRangeTicks=0; return;
                        }
                        if (++shortRangeTicks<10) return;
                        qInfo()<<"LUNA DPI scroll clamp"<<scrollPosition<<available;
                    }
                    const int restoredScroll=qMin(scrollPosition,available);
                    vertical->setValue(restoredScroll);
                    if (vertical->value()!=restoredScroll) return;
                    editor->setProperty("LunaMoveRestorePending",false);
                    restore->stop();restore->deleteLater();
                });
            restore->start(50);
        };
        if (activeEditor) {
            activeEditor->setProperty("LunaMoveRestorePending",true);
            activeEditor->setProperty("LunaPreserveReopenView",true);
            activeEditor->getProgramController()->closeDocument();
        }
        const auto moved=luna::movePdfWithThumbnails(
            original,source.absolutePath(),luna::renameWithoutReplacement,filename);
        if (!moved.success) {
            reopenReader(original,false);
            explain(QStringLiteral("Windows에서 실제 PDF/연결 파일 이동을 거부했습니다.\n%1").arg(moved.error));
            qWarning().noquote()<<"LUNA title-file rename skipped:"<<moved.error;
            return true; // Explain the actual failure; never claim a rename.
        }
        if (QFileInfo::exists(original) || !QFileInfo(moved.documentPath).isFile()) {
            reopenReader(QFileInfo(original).isFile()?original:moved.documentPath,
                         !QFileInfo(original).isFile());
            explain(QStringLiteral("실제 파일명 변경 검증 실패: 원본 또는 대상 파일 상태를 확인하세요."));
            return false;
        }
        if (renamedPath) *renamedPath=moved.documentPath;
        explain(QStringLiteral("파일명 변경 완료: %1").arg(QFileInfo(moved.documentPath).fileName()));
        // This existing reconciliation rewrites recent/favorite/memo keys,
        // bibliography identity, and manually linked SI parent names.
        if (localSync_ && localSync_->renamed)
            localSync_->renamed(original, moved.documentPath);

        if (!renamedOpenedDate.isEmpty()) {
            QSettings store(QSettings::defaultFormat(), QSettings::UserScope,
                            QStringLiteral("LUNA"),QStringLiteral("LUNA Library"));
            const QString canonical=QFileInfo(moved.documentPath).canonicalFilePath();
            for (const QString& newPath : {moved.documentPath, canonical}) {
                if (newPath.isEmpty()) continue;
                const QString key=openedDateKey(newPath);
                openedDates_.insert(key,renamedOpenedDate);
                store.setValue(QStringLiteral("library/openedDates/")+key,renamedOpenedDate);
            }
            rememberDateAlias(moved.documentPath,renamedOpenedDate);
            store.sync();
            if (store.status()!=QSettings::NoError)
                qWarning()<<"LUNA auto-rename opened-date persistence failed";
        }
        if (localSync_) localSync_->publish(moved.documentPath);
        reopenReader(moved.documentPath,true);
        ThumbnailCache::instance()->invalidate();
        if (!window()->property("LunaClosing").toBool()) {
            allTimer_->stop(); allIterator_.reset(); allScanned_ = false; allPdfs_->clear();
            refreshFileLists();
            if (pages_->currentIndex() == 1) scanAll();
            else if (pages_->currentIndex() == 2) showHistory(showingFavorites_);
            model_->refreshPdfCounts(root_);
        }
        return true;
    }
    // Explicit user action, no modal dialog and no forced auto-save of
    // speculative title/author data. Trust only publication evidence found
    // on the opened document's first page.
    void extractBibliography(const QString& path, bool restore = false) {
        const QString identity = DocumentClaim::identity(path);
        if (identity.isEmpty() || !QFileInfo(path).isFile()) return;
        if (!viewer_ || !viewer_->getProgramController()) return;
        auto* controller = viewer_->getProgramController();
        if (!controller->getDocument() ||
            DocumentClaim::identity(controller->getOriginalFileName()) != identity) {
            // Independent PDF windows cannot switch the active controller.
            // Parse the selected file directly in the isolated worker instead.
            enqueueImportedMetadata(path, restore);
            return;
        }
        if (!controller->getDocument()->getCatalog() ||
            controller->getDocument()->getCatalog()->getPageCount() < 1) return;
        if (restore) pendingOriginalResets_.insert(identity);
        queueBibliography(identity, *controller->getDocument(), true);
    }
    void refreshOriginalBibliography(const QString& path) {
        if (QMessageBox::question(this, QStringLiteral("원본 서지정보 복원"),
            QStringLiteral("이 PDF의 수동 수정 서지정보를 검증된 원본 값으로 교체할까요?\\n"
                           "검증할 수 없는 항목은 그대로 보존됩니다."),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes)
            return;
        extractBibliography(path, true);
    }
    void queueBibliography(const QString& identity, const pdf::PDFDocument& document, bool force = false) {
        if (identity.isEmpty()) return;
        if (runningCitations_.contains(identity)) {
            // The running first-page worker will honor an explicit reset.
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
                const bool restore = pendingOriginalResets_.remove(identity) > 0;
                // Repair verified old auto author cache on the UI thread.
                if (!restore && result->value(QStringLiteral("documentKind")).toString() !=
                    QStringLiteral("supporting_information"))
                    bibliography_.reconcileVerifiedAuthorship(identity);
                const bool found = restore ?
                    bibliography_.restoreFromFirstPage(identity, *result) :
                    bibliography_.enrichFromFirstPage(identity, *result);
                if (result->value(QStringLiteral("documentKind")).toString() !=
                    QStringLiteral("supporting_information")) {
                    QString doi = result->value(QStringLiteral("doi")).toString();
                    if (doi.isEmpty()) doi=luna::Bibliography::trustedSavedDoi(
                        bibliography_.record(identity),restore);
                    if (!doi.isEmpty()) beginMetadataLookup(identity, doi, restore);
                    else if (restore && !found)
                        QMessageBox::information(this, QStringLiteral("서지정보 원본 확인 불가"),
                            QStringLiteral("첫 페이지에서 신뢰할 수 있는 서지정보나 DOI를 찾지 못했습니다. 수정값은 유지됩니다."));
                }
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
    void beginMetadataLookup(const QString& identity, const QString& doi, bool refresh = false) {
        if (!metadataFetcher_ || identity.isEmpty()) return;
        const QString safe=luna::Metadata::normalizeDoi(doi);
        if (safe.isEmpty()) return;
        const QVariantMap old=bibliography_.record(identity);
        // A legacy "complete" cache may still hold only a wrong/unknown
        // first author. Honor DOI-only verified repair and hourly throttling.
        const bool needsAuthorRepair=luna::Bibliography::needsVerifiedAuthorRefresh(old);
        if (!refresh && !needsAuthorRepair && old.value("doi").toString()==safe &&
            old.value("metadata_status").toString()==QStringLiteral("complete"))return;
        // Reuse local data during offline and transient provider failures.
        const QDateTime last=QDateTime::fromString(
            old.value(QStringLiteral("metadata_checked_at")).toString(),Qt::ISODateWithMs);
        if(!refresh && last.isValid() && last.secsTo(QDateTime::currentDateTimeUtc())<3600 &&
           old.value(QStringLiteral("doi")).toString()==safe)return;
        bibliography_.markLookupStarted(identity);
        metadataFetcher_->lookup(safe,[this,identity,safe,refresh](QVariantMap data, QString reason) {
            if(!data.isEmpty()) {
                const bool updated = refresh ?
                    bibliography_.restoreVerifiedOnline(identity,safe,data) :
                    bibliography_.mergeVerifiedOnline(identity,safe,data,reason);
                if(refresh && !updated)
                    QMessageBox::information(this,QStringLiteral("원본 서지정보 보존"),
                        QStringLiteral("DOI가 일치하는 신뢰 가능한 서지정보를 확인하지 못했습니다. 기존 값을 유지합니다."));
            } else if(reason != QStringLiteral("loading")) {
                bibliography_.recordLookupFailure(identity,reason);
                if(refresh)
                    QMessageBox::information(this,QStringLiteral("서지정보 조회 실패"),
                        QStringLiteral("서지정보를 다시 불러오지 못했습니다 (%1). 기존 값은 보존됩니다.").arg(reason));
            }
        });
    }
    void enqueueImportedMetadata(const QString& path, bool restore = false) {
        const QFileInfo file(path);
        if (!file.isFile() || file.size()==0 || file.size()>(restore?200:40)*1024*1024)return;
        pendingImportedMetadata_.enqueue(qMakePair(file.absoluteFilePath(), restore));
        startNextImportedMetadata();
    }
    void startNextImportedMetadata() {
        if(importMetadataRunning_ || pendingImportedMetadata_.isEmpty())return;
        const auto request=pendingImportedMetadata_.dequeue();
        const QString path=request.first;
        const bool restore=request.second;
        const QString identity=DocumentClaim::identity(path);
        importMetadataRunning_=true;
        auto result=std::make_shared<QVariantMap>();
        // Worker owns its PDF4QT parser and font caches. No QWidget, QSettings
        // or viewer controller may be touched from this worker.
        auto* worker=QThread::create([path,result] {
            try {
                pdf::PDFProgress progress(nullptr);
                pdf::PDFDocumentReader reader(&progress,
                    [](bool*) {return QString();},true,false);
                auto document=reader.readFromFile(path);
                if(reader.getReadingResult()==pdf::PDFDocumentReader::Result::OK)
                    *result=luna::extractFirstPage(document);
            }catch(...){} // Password / malformed PDFs never become trusted.
        });
        connect(worker,&QThread::finished,this,[this,result,identity,restore] {
            importMetadataRunning_=false;
            const bool found = restore ?
                bibliography_.restoreFromFirstPage(identity,*result) :
                bibliography_.enrichFromFirstPage(identity,*result);
            if(result->value(QStringLiteral("documentKind")).toString()!=
               QStringLiteral("supporting_information")) {
                QString doi=result->value(QStringLiteral("doi")).toString();
                if(restore && doi.isEmpty())
                    doi=bibliography_.record(identity).value("doi").toString();
                if(!doi.isEmpty())beginMetadataLookup(identity,doi,restore);
                else if(restore && !found)
                    QMessageBox::information(this,QStringLiteral("서지정보 원본 확인 불가"),
                        QStringLiteral("PDF 첫 페이지에서 검증할 서지정보를 찾지 못했습니다. 기존 값을 유지합니다."));
            }
            QTimer::singleShot(0,this,[this]{startNextImportedMetadata();});
        });
        connect(qApp,&QCoreApplication::aboutToQuit,worker,[worker] {worker->wait();});
        connect(worker,&QThread::finished,worker,&QObject::deleteLater);
        worker->start(QThread::LowPriority);
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
        const QString suggestedTitle=existing.value("titleCandidate").toString().simplified();
        const bool titleNeedsReview=existing.value("titleCandidateSource").toString() ==
            QStringLiteral("first_page_layout_review") &&
            luna::Bibliography::credibleTitle(suggestedTitle) &&
            suggestedTitle!=title.text().simplified();
        if (titleNeedsReview)
            title.setToolTip(QStringLiteral("첫 페이지에서 추출한 미확인 제목 제안:\n") +
                             suggestedTitle + QStringLiteral("\n사용자가 적용하기 전에는 원제목을 변경하지 않습니다."));
        QLineEdit authors(existing.value("authors").toString(), &dialog);
        QLineEdit journal(existing.value("journal").toString(), &dialog);
        QLineEdit year(existing.value("year").toString(), &dialog);
        QLineEdit volume(existing.value("volume").toString(), &dialog);
        QLineEdit pages(existing.value("page").toString(), &dialog);
        QLineEdit doi(existing.value("doi").toString(), &dialog);
        QLineEdit issue(existing.value("issue").toString(), &dialog);
        QLineEdit articleNumber(existing.value("article_number").toString(), &dialog);
        QLineEdit journalShort(existing.value("journal_abbreviation").toString(), &dialog);
        QLineEdit seniorAuthor(existing.value("senior_author_override").toString(), &dialog);
        form.addRow(QStringLiteral("서지정보 한 줄"), &fullCitation);
        form.addRow(QStringLiteral("논문 제목"), &title);
        if (titleNeedsReview) {
            auto* acceptSuggestion=new QPushButton(QStringLiteral("검토 후 제목 제안 적용"), &dialog);
            acceptSuggestion->setObjectName(QStringLiteral("LunaAcceptSuggestedBibliographyTitle"));
            acceptSuggestion->setToolTip(suggestedTitle);
            connect(acceptSuggestion, &QPushButton::clicked, &dialog, [&title, suggestedTitle] {
                title.setText(suggestedTitle);
                title.setFocus();
            });
            form.addRow(QStringLiteral("자동 추출 제안"), acceptSuggestion);
        }
        form.addRow(QStringLiteral("저자"), &authors);
        form.addRow(QStringLiteral("저널"), &journal);
        form.addRow(QStringLiteral("발행 연도"), &year);
        form.addRow(QStringLiteral("권(Volume)"), &volume);
        form.addRow(QStringLiteral("페이지"), &pages);
        form.addRow(QStringLiteral("DOI (사용자 확인)"), &doi);
        form.addRow(QStringLiteral("저널 공식 축약명"), &journalShort);
        form.addRow(QStringLiteral("호(Issue)"), &issue);
        form.addRow(QStringLiteral("논문 번호"), &articleNumber);
        form.addRow(QStringLiteral("대표 저자 직접 지정"), &seniorAuthor);
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
        const QString revisedDoi=doi.text().simplified();
        if(!revisedDoi.isEmpty() && luna::Metadata::normalizeDoi(revisedDoi).isEmpty()) {
            QMessageBox::warning(this,QStringLiteral("DOI 형식 오류"),
                QStringLiteral("입력된 DOI를 검증할 수 없습니다. 기존 서지정보는 변경되지 않았습니다."));
            return;
        }
        const QString revisedYear = year.text().trimmed();
        QVariantMap revised = existing;
        revised["citation"] = fullCitation.text().simplified().left(4096);
        revised["title"] = title.text().simplified().left(4096);
        revised["authors"] = authors.text().simplified().left(4096);
        revised["journal"] = journal.text().simplified().left(4096);
        revised["year"] = revisedYear;
        revised["volume"] = volume.text().simplified().left(256);
        revised["page"] = pages.text().simplified().left(256);
        revised["doi"] = luna::Metadata::normalizeDoi(revisedDoi);
        revised["journal_abbreviation"] = journalShort.text().simplified().left(256);
        revised["issue"] = issue.text().simplified().left(80);
        revised["article_number"] = articleNumber.text().simplified().left(128);
        revised["senior_author_override"] = seniorAuthor.text().simplified().left(256);
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

        if (date.isEmpty() && !fileIdentity.isEmpty())
            date=openedDates_.value(openedDateKey(fileIdentity));
        if (date.isEmpty()) date = displayedDates_.value(fileIdentity);
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
        const QString lastAuthor = luna::Bibliography::lastListedAuthor(bibliography.value("authors").toString());
        const bool formattedCitation = !bibliography.value("doi").toString().isEmpty() ||
            !bibliography.value("citation").toString().isEmpty();
        const QString metadataStatus = bibliography.value("metadata_status").toString();
        const QString doi = bibliography.value("doi").toString();
        item->setToolTip((memo.isEmpty() ? QString() : memo + QLatin1Char('\n')) +
            (!formattedCitation && !lastAuthor.isEmpty() ? lastAuthor + QLatin1Char('\n') : QString()) +
            (citation.isEmpty() ? QString() : citation + QLatin1Char('\n')) +
            (doi.isEmpty() ? QString() : QStringLiteral("DOI: ") + doi + QLatin1Char('\n')) +
            (metadataStatus.isEmpty() ? QString() : QStringLiteral("서지정보: ") +
             metadataStatus + QLatin1Char('\n')) + path);
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
        if(localSync_ && !applyingRemote_) localSync_->publish(path);
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
            folderBadge_->setText(model_->countsAvailable() ? QString::number(model_->pdfCount(root_)) :
                QString::number(folderPdfs_->count()) + (folderIterator_ ? QStringLiteral("+") : QString()));
            favoriteBadge_->setText(QString::number(favorites_.size())); recentBadge_->setText(QString::number(recentCount_));
        });
    }
    void toggleFavorite(const QString& path) {
        QStringList paths = savedPaths(QStringLiteral("library/favorites"));
        if (paths.contains(path)) paths.removeAll(path);
        else paths.append(path);
        favorites_ = QSet<QString>(paths.cbegin(), paths.cend());
        QSettings(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("LUNA"), QStringLiteral("LUNA Library")).setValue(QStringLiteral("library/favorites"), paths);
        if(localSync_ && !applyingRemote_) localSync_->publish(path);
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
        if (date.isEmpty()) return;
        const QString identity=DocumentClaim::identity(path);
        if (identity.isEmpty()) return;

        const QString key=openedDateKey(identity);
        displayedDates_.insert(identity,date);
        openedDates_.insert(key,date);
        QSettings(QSettings::defaultFormat(),QSettings::UserScope,
                  QStringLiteral("LUNA"),QStringLiteral("LUNA Library"))
            .setValue(QStringLiteral("library/openedDates/")+key,date);
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

        pendingTitleRenames_.insert(DocumentClaim::identity(path));
        if (!titleRenameTimer_.isActive()) titleRenameTimer_.start();
        queueBibliography(DocumentClaim::identity(path), *controller->getDocument());

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
            // Default 200%, configurable in View > Default opening zoom.
            // Restoring an existing document's view never uses this default.
            QSettings preferences(QSettings::defaultFormat(), QSettings::UserScope,
                                  QStringLiteral("LUNA"), QStringLiteral("LUNA Library"));
            const int percent = qBound(25, preferences.value(
                QStringLiteral("reader/defaultZoomPercent"), 200).toInt(), 500);
            proxy->zoom(double(percent) / 100.0);
        }
        editor.setProperty("LunaPreserveReopenView", false);
        if (viewer_ == &editor) selectActiveCard();

    }
    void refreshFileLists() {
        allTimer_->stop();
        allIterator_.reset();
        allPdfs_->clear();
        allSeen_.clear();
        allScanned_ = false;
        selectFolder(QFileInfo(current_).isDir() ? current_ : root_);
        if (pages_->currentIndex() == 1) scanAll();
        else if (pages_->currentIndex() == 2) showHistory(showingFavorites_);
        ThumbnailCache::instance()->invalidate();
        model_->refreshPdfCounts(root_);
    }
    // Persist per-document user data alongside the physical copy, without
    // making the copied record an alias of the source's future edits.
    void cloneCopiedSettings(const QString& source, const QString& destination) {
        const QString from = DocumentClaim::identity(source);
        const QString to = DocumentClaim::identity(destination);
        if (!bibliography_.cloneTo(from, to))
            QMessageBox::warning(this, QStringLiteral("서지정보 복사 실패"),
                QStringLiteral("PDF는 복사했지만 서지정보를 복제하지 못했습니다. 원본 설정은 보존됩니다."));
        // The generic Copy action changes folders. Do not accidentally attach
        // the new PDF to an unrelated paper that happens to share the same
        // filename in the destination directory.
        if (bibliography_.record(from).value("manualSI").toBool())
            bibliography_.detachManualSI(to);
        QSettings settings(QSettings::defaultFormat(), QSettings::UserScope,
                           QStringLiteral("LUNA"), QStringLiteral("LUNA Library"));
        const QString a = openedDateKey(source), b = openedDateKey(destination);
        for (const QString& prefix : {QStringLiteral("library/memos/"),
                                     QStringLiteral("library/openedDates/")}) {
            const QVariant value = settings.value(prefix + a);
            if (value.isValid() && !settings.contains(prefix + b)) {
                settings.setValue(prefix + b, value);
                if (prefix == QStringLiteral("library/memos/"))
                    memos_.insert(b, value.toString());
                else openedDates_.insert(b, value.toString());
            }
        }
        QStringList favorites = settings.value(QStringLiteral("library/favorites")).toStringList();
        if (favorites.contains(source) && !favorites.contains(destination)) {
            favorites.append(destination);
            settings.setValue(QStringLiteral("library/favorites"), favorites);
            favorites_.insert(destination);
        }
        settings.sync();
        if (localSync_) localSync_->publish(destination);
        if (settings.status() != QSettings::NoError)
            QMessageBox::warning(this, QStringLiteral("복사 데이터 저장 실패"),
                QStringLiteral("PDF는 복사됐지만 일부 메모·즐겨찾기 저장에 실패했습니다. 원본 데이터는 유지됩니다."));
    }
    void copyDocumentTo(const QString& source, const QString& destination) {
        const auto result = copyPdfWithThumbnails(source, destination);
        if (!result.success) {
            QMessageBox::warning(this, QStringLiteral("PDF 복사 실패"), result.error);
            return;
        }
        cloneCopiedSettings(source, result.documentPath);
        if (bibliography_.record(DocumentClaim::identity(result.documentPath)).isEmpty())
            enqueueImportedMetadata(result.documentPath);
        refreshFileLists();
    }
    void transferPdfs(const QStringList& paths, const QString& destination, bool copy) {
        if (!QFileInfo(destination).isDir()) return;
        for (const QString& path : paths) {
            if (copy) copyDocumentTo(path, destination);
            else moveDocumentTo(path, destination);
        }
    }
    void copySelectedPdfs(QListWidget* list) {
        if (!list) return;
        QList<QUrl> urls;
        QSet<QString> seen;
        for (auto* item : list->selectedItems()) {
            const QString path = item->data(Qt::UserRole).toString();
            const QFileInfo info(path);
            if (info.isFile() && !seen.contains(info.canonicalFilePath())) {
                seen.insert(info.canonicalFilePath());
                urls.append(QUrl::fromLocalFile(info.canonicalFilePath()));
            }
        }
        if (urls.isEmpty() && list->currentItem()) {
            const QFileInfo file(list->currentItem()->data(Qt::UserRole).toString());
            if (file.isFile()) urls.append(QUrl::fromLocalFile(file.canonicalFilePath()));
        }
        if (urls.isEmpty()) return;
        auto* data = new QMimeData;
        data->setUrls(urls);
        QGuiApplication::clipboard()->setMimeData(data);
    }
    void pastePdfs(const QString& destination) {
        const QMimeData* data = QGuiApplication::clipboard()->mimeData();
        if (!data || !data->hasUrls() || !QFileInfo(destination).isDir()) return;
        QStringList paths;
        for (const auto& url : data->urls()) {
            if (!url.isLocalFile()) return;
            const QFileInfo info(url.toLocalFile());
            if (!info.isFile() || info.isSymLink() ||
                info.suffix().compare(QStringLiteral("pdf"), Qt::CaseInsensitive)) return;
            if (!paths.contains(info.canonicalFilePath())) paths.append(info.canonicalFilePath());
        }
        transferPdfs(paths, destination, true);
    }
    void undoRecentTrash() {
        if (!recentUndo_.available()) return;
        const QStringList restoredPaths = recentUndo_.pdfPaths();
        QString error;
        if (!recentUndo_.undo(&error)) {
            QMessageBox::warning(this, QStringLiteral("삭제 취소 실패"), error);
            return;
        }
        auto store=luna::lunaLocalStore();
        // Restore one batch's memberships without erasing later user changes.
        QStringList favorites=store.value(QStringLiteral("library/favorites")).toStringList();
        QStringList recent=store.value(QStringLiteral("library/recent")).toStringList();
        for (const QString& restored : restoredPaths) {
            if (undoFavorites_.contains(restored) && !favorites.contains(restored))
                favorites.append(restored);
            const int order=undoRecent_.indexOf(restored);
            if (order >= 0 && !recent.contains(restored))
                recent.insert(qMin(order, int(recent.size())), restored);
        }
        store.setValue(QStringLiteral("library/favorites"),favorites);
        store.setValue(QStringLiteral("library/recent"),recent);
        store.sync();
        if(store.status()!=QSettings::NoError)
            QMessageBox::warning(this,QStringLiteral("복원 완료 · 설정 확인"),
                QStringLiteral("PDF는 복원했지만 일부 기록 설정을 저장하지 못했습니다."));
        favorites_=QSet<QString>(favorites.cbegin(),favorites.cend());
        undoFavorites_.clear(); undoRecent_.clear();
        refreshFileLists();
    }
    void trashSelectedDocuments(const QStringList& selection, const QString& clicked) {
        QStringList paths;
        QSet<QString> seen;
        for (const QString& path : selection) {
            const QString id = DocumentClaim::identity(path);
            if (!id.isEmpty() && !seen.contains(id)) { seen.insert(id); paths.append(path); }
        }
        if (paths.isEmpty()) paths = {clicked};
        if (paths.isEmpty()) return;
        // Check every source and open document before moving any file.
        for (const QString& path : paths) {
            const QFileInfo source(path);
            if (!source.isFile() || source.isSymLink()) {
                QMessageBox::warning(this, QStringLiteral("휴지통 이동 취소"),
                    QStringLiteral("선택한 PDF 중 접근할 수 없는 파일이 있습니다."));
                return;
            }
            const QString identity = DocumentClaim::identity(path);
            if ((activateElsewhere && activateElsewhere(path)) ||
                (independentDocuments(*viewer_) && independentDocuments(*viewer_)->openElsewhere(path)) ||
                (viewer_->getProgramController()->getDocument() &&
                 DocumentClaim::identity(viewer_->getProgramController()->getOriginalFileName()) == identity)) {
                QMessageBox::information(this, QStringLiteral("열린 PDF"),
                    QStringLiteral("열린 PDF가 선택되어 있습니다. 해당 탭을 닫은 뒤 다시 삭제해 주세요."));
                return;
            }
        }
        if (QMessageBox::question(this, QStringLiteral("PDF 휴지통 이동"),
            QStringLiteral("선택한 PDF %1개와 썸네일·메타데이터를 휴지통으로 옮길까요?\n"
                           "Ctrl+Z는 이번 삭제 묶음 전체를 복원합니다.").arg(paths.size()),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) return;
        const QStringList oldFavorites = savedPaths(QStringLiteral("library/favorites"));
        const QStringList oldRecent = savedPaths(QStringLiteral("library/recent"));
        QString error;
        if (!recentUndo_.trashBatch(paths, &error)) {
            QMessageBox::warning(this, QStringLiteral("휴지통 이동 실패"), error);
            return;
        }
        undoFavorites_ = oldFavorites;
        undoRecent_ = oldRecent;
        auto store = luna::lunaLocalStore();
        QStringList favorites = oldFavorites;
        QStringList recent = oldRecent;
        for (const QString& path : paths) {
            favorites.removeAll(path);
            recent.removeAll(path);
        }
        store.setValue(QStringLiteral("library/favorites"), favorites);
        store.setValue(QStringLiteral("library/recent"), recent);
        store.sync();
        if (store.status() != QSettings::NoError)
            QMessageBox::warning(this, QStringLiteral("PDF 삭제 · 설정 확인"),
                QStringLiteral("PDF는 휴지통으로 이동했지만 즐겨찾기·최근 기록 설정은 저장하지 못했습니다."));
        favorites_ = QSet<QString>(favorites.cbegin(), favorites.cend());
        refreshFileLists();
    }
    void importPdfs() {
        const QStringList files = QFileDialog::getOpenFileNames(this,
            QStringLiteral("PDF 추가 · 원본을 유지하고 현재 폴더에 복사"), current_, QStringLiteral("PDF (*.pdf *.PDF)"));
        QString opened;
        QStringList imported;
        for (const QString& path : files) {
            const QFileInfo info(path);
            if (!info.isFile() || info.suffix().compare(QStringLiteral("pdf"), Qt::CaseInsensitive) != 0) continue;
            const QString target = QDir(current_).filePath(info.fileName());
            if (info.canonicalFilePath() == QFileInfo(target).canonicalFilePath()) { opened = target; continue; }
            const auto copied = copyPdfWithThumbnails(path, current_);
            if (!copied.success) {
                QMessageBox::warning(this, QStringLiteral("PDF 추가 실패"), copied.error);
                continue;
            }
            cloneCopiedSettings(path, copied.documentPath);
            opened = copied.documentPath;
            imported.append(copied.documentPath);
        }
        if (opened.isEmpty()) return;
        allTimer_->stop();
        allIterator_.reset();
        allScanned_ = false;
        allPdfs_->clear();
        folderTab_->setChecked(true);
        pages_->setCurrentIndex(0);
        selectFolder(current_);
        // Opening the last file uses the existing owned-document worker.
        // Independently scan other newly imported PDFs without opening tabs.
        for (const QString& path : imported)
            if(path!=opened) enqueueImportedMetadata(path);
        openPdf(opened);
    }
    bool eventFilter(QObject* observed, QEvent* event) override {
        if (observed == parentWidget() && event->type() == QEvent::Resize) {
            // A real dock resize does not emit QSplitter::splitterMoved. The
            // earlier PR #35 lost user-set widths when a 690px dock was resized.
            if (!adjustingPanels_ && !navigation_->isHidden() && !documents_->isHidden()) {
                widthSaveTimer_.start();
                QTimer::singleShot(0, this, [this] {
                    if (!adjustingPanels_ && !navigation_->isHidden() && !documents_->isHidden())
                        savePanelState();
                });
            }
            return QWidget::eventFilter(observed, event);
        }
        if (observed == folderTab_ &&
            (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove ||
             event->type() == QEvent::Drop)) {
            auto* drag = static_cast<QDropEvent*>(event);
            const auto urls = drag->mimeData()->urls();
            QStringList paths;
            for (const auto& url : urls) {
                const QFileInfo info(url.toLocalFile());
                if (!url.isLocalFile() || !info.isFile() || info.isSymLink() ||
                    info.suffix().compare(QStringLiteral("pdf"), Qt::CaseInsensitive) != 0)
                    return QWidget::eventFilter(observed, event);
                if (!paths.contains(info.canonicalFilePath())) paths.append(info.canonicalFilePath());
            }
            if (paths.isEmpty() || !QFileInfo(root_).isDir())
                return QWidget::eventFilter(observed, event);
            const auto* sourceList = qobject_cast<QListWidget*>(drag->source());
            const bool internal = sourceList &&
                sourceList->objectName() == QStringLiteral("LunaDocumentList");
            const bool copy = !internal || bool(drag->modifiers() & Qt::ControlModifier);
            drag->setDropAction(copy ? Qt::CopyAction : Qt::MoveAction);
            drag->accept();
            if (event->type() == QEvent::Drop) transferPdfs(paths, root_, copy);
            return true;
        }
        const bool historyViewport = historyPdfs_ && observed == historyPdfs_->viewport();
        const bool folderViewport = folderPdfs_ && observed == folderPdfs_->viewport();
        const bool allViewport = allPdfs_ && observed == allPdfs_->viewport();
        if (!folderViewport && !allViewport && !historyViewport)
            return QWidget::eventFilter(observed, event);
        // Card thumbnails are already visible; never pop up a second hover preview.
        if (event->type() == QEvent::Leave ||
                   event->type() == QEvent::Hide ||
                   event->type() == QEvent::MouseButtonPress ||
                   event->type() == QEvent::Wheel) {
            hideThumbnail();
        }
        return QWidget::eventFilter(observed, event);
    }
    QString thumbnailForPdf(const QString& documentPath) const {
        return savedThumbnailForPdf(documentPath);
    }
    // The old floating thumbnail-hover popup is intentionally disabled.
    // User-selected crops are still displayed directly inside paper cards.
    void hideThumbnail() {
        thumbnailPath_.clear(); thumbnailRevision_.clear(); hoverRequest_.clear();
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
        const QString output = SidecarStorage::thumbnail(documentPath, QStringLiteral(".png"));
        if (!SidecarStorage::migrateLegacy(documentPath) ||
            !SidecarStorage::ensureArea(documentPath, false) || QFileInfo(output).isSymLink()) {
            QMessageBox::warning(this, QStringLiteral("썸네일 저장 취소"),
                QStringLiteral("기존 관리 파일 충돌 또는 숨김 폴더 오류입니다. 원본은 보존되었습니다."));
            return;
        }
        if (QFileInfo::exists(output) &&
            QMessageBox::question(this, QStringLiteral("썸네일 덮어쓰기"),
                QStringLiteral("기존 썸네일을 새 이미지로 교체할까요?"),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;
        // The image is saved under .luna/thumbnails beside the local synced PDF.
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
    void setPaperViewMode(bool compact) {
        if (compactPaperMode_ == compact) return;
        compactPaperMode_ = compact;
        QSettings(QSettings::defaultFormat(), QSettings::UserScope,
            QStringLiteral("LUNA"), QStringLiteral("LUNA Library"))
            .setValue(QStringLiteral("library/paperViewMode"),
                      compact ? QStringLiteral("list") : QStringLiteral("card"));
        for (auto* list : {folderPdfs_, allPdfs_, historyPdfs_}) {
            if (!list) continue;
            auto* delegate = static_cast<DocumentCard*>(list->itemDelegate());
            if (!delegate) continue;
            QListWidgetItem* anchor = list->itemAt(QPoint(10, 5));
            delegate->setCompact(compact);
            list->setUniformItemSizes(false); // SI child rows and parent footers have different heights
            // doItemsLayout is a Qt virtual slot, not a public C++ method.
            // Invoke through the meta-object after changing uniform size.
            QMetaObject::invokeMethod(list, "doItemsLayout", Qt::DirectConnection);
            if (anchor) list->scrollToItem(anchor, QAbstractItemView::PositionAtTop);
            list->viewport()->update();
        }
    }

    void arrangeManualSIRows(QListWidget* list) {
        if(!list)return;
        QHash<QString,QListWidgetItem*> items;
        for(int i=0;i<list->count();++i) {
            auto* row=list->item(i);
            row->setData(Qt::UserRole+10,false); // inline child
            row->setData(Qt::UserRole+11,0);     // parent SI count
            row->setData(Qt::UserRole+12,false); // parent expanded
            row->setData(Qt::UserRole+13,QString());
            items.insert(row->data(Qt::UserRole).toString().toCaseFolded(),row);
        }
        QHash<QListWidgetItem*,QList<QListWidgetItem*>> grouped;
        for(int i=0;i<list->count();++i) {
            auto* si=list->item(i);
            const QVariantMap info=si->data(Qt::UserRole+5).toMap();
            if(!info.value("manualSI").toBool())continue;
            const QString parentName=info.value("manualSiParentFile").toString();
            if(!luna::ManualSI::validParentFileName(parentName))continue;
            const QString siPath=si->data(Qt::UserRole).toString();
            const QString fullParent=QDir(QFileInfo(siPath).absolutePath())
                .filePath(parentName).toCaseFolded();
            auto* parent=items.value(fullParent,nullptr);
            if(!parent || parent==si || parent->data(Qt::UserRole+5).toMap()
                .value("manualSI").toBool())continue;
            grouped[parent].append(si);
        }
        if(grouped.isEmpty())return;
        // Keep the top-level paper model intact: only physically associated
        // SI PDFs are re-ordered immediately beneath their manual parent.
        for(auto it=grouped.cbegin();it!=grouped.cend();++it) {
            auto* parent=it.key();
            const QString parentPath=parent->data(Qt::UserRole).toString();
            const bool expanded=expandedSIParents_.contains(parentPath);
            parent->setData(Qt::UserRole+11,it.value().size());
            parent->setData(Qt::UserRole+12,expanded);
            int offset=0;
            for(auto* si:it.value()) {
                const int oldIndex=list->row(si);
                if(oldIndex<0)continue;
                auto* moved=list->takeItem(oldIndex);
                moved->setData(Qt::UserRole+10,true);
                moved->setData(Qt::UserRole+13,parentPath);
                list->insertItem(list->row(parent)+1+offset++,moved);
                moved->setHidden(!expanded && query_->text().isEmpty());
            }
        }
        QMetaObject::invokeMethod(list,"doItemsLayout",Qt::QueuedConnection);
    }
    void toggleManualSIGroup(const QString& parent) {
        if(expandedSIParents_.contains(parent))expandedSIParents_.remove(parent);
        else expandedSIParents_.insert(parent);
        for(auto* list:{folderPdfs_,allPdfs_,historyPdfs_}) {
            if(!list)continue;
            for(int i=0;i<list->count();++i) {
                auto* row=list->item(i);
                if(row->data(Qt::UserRole).toString()==parent &&
                   row->data(Qt::UserRole+11).toInt()>0)
                    row->setData(Qt::UserRole+12,expandedSIParents_.contains(parent));
                if(row->data(Qt::UserRole+10).toBool() &&
                   row->data(Qt::UserRole+13).toString()==parent)
                    row->setHidden(!expandedSIParents_.contains(parent) &&
                                  query_->text().isEmpty());
            }
            list->viewport()->update();
        }
    }
    void addManualSI(const QString& parent) {
        const QFileInfo paper(parent);
        if(!paper.isFile() || paper.isSymLink())return;
        const QString selected=QFileDialog::getOpenFileName(this,
            QStringLiteral("Supporting Information 추가 · PDF 선택"),
            paper.absolutePath(),QStringLiteral("PDF 파일 (*.pdf *.PDF)"));
        if(selected.isEmpty())return;
        const QString sourceIdentity=DocumentClaim::identity(selected);
        const QVariantMap existing=bibliography_.record(sourceIdentity);
        if(existing.value("manualSI").toBool() &&
           existing.value("manualSiParentFile").toString().compare(
                paper.fileName(),Qt::CaseInsensitive)!=0) {
            QMessageBox::warning(this,QStringLiteral("SI 연결 충돌"),
                QStringLiteral("선택한 SI는 다른 원논문에 연결되어 있습니다. 먼저 기존 연결을 해제해 주세요."));
            return;
        }
        // Renaming the selected PDF currently open in the native editor
        // could invalidate its document/annotation controller. Never do it.
        const QString title=bibliography_.record(DocumentClaim::identity(parent))
            .value("title",paper.completeBaseName()).toString();
        const QFileInfo selectedInfo(selected);
        const bool sameFolder=selectedInfo.canonicalPath().compare(
            paper.canonicalPath(),Qt::CaseInsensitive)==0;
        const bool renameNeeded=sameFolder &&
            !luna::ManualSI::alreadyNamed(selectedInfo.fileName(),title);
        const QString opened=viewer_->getProgramController()->getOriginalFileName();
        if(renameNeeded && !opened.isEmpty() &&
           QFileInfo(opened).canonicalFilePath()==selectedInfo.canonicalFilePath()) {
            QMessageBox::information(this,QStringLiteral("열린 SI PDF"),
                QStringLiteral("현재 열린 SI PDF를 닫은 후 추가해 주세요. 주석과 저장 내용을 보존합니다."));
            return;
        }
        const auto placed=luna::ManualSI::placeNextToParent(selected,parent,title);
        if(!placed.success) {
            QMessageBox::warning(this,QStringLiteral("SI 추가 실패"),placed.error);
            return;
        }
        if(placed.renamedExisting) {
            QString error;
            if(!luna::reconcileExplorerRename(placed.previousPath,placed.path,bibliography_,&error)) {
                // Attempt the exact reverse rename + metadata reconciliation.
                // If this fails, do not delete either user's PDF or sidecar.
                QString rollback;
                if(luna::renameWithoutReplacement(placed.path,placed.previousPath)) {
                    if(!luna::reconcileExplorerRename(
                           placed.path,placed.previousPath,bibliography_,&rollback))
                        rollback=QStringLiteral("이전 이름의 메타데이터를 자동 복구하지 못했습니다.");
                } else rollback=QStringLiteral("이전 파일명으로 되돌리지 못했습니다.");
                QMessageBox::warning(this,QStringLiteral("SI 이름 변경 취소"),
                    QStringLiteral("SI PDF와 기존 주석·서지정보 연결을 확인할 수 없습니다.\\n%1\\n%2")
                        .arg(error,rollback));
                return;
            }
        }
        const QString siId=DocumentClaim::identity(placed.path);
        if(!bibliography_.attachManualSI(siId,paper.fileName())) {
            // Deleting an existing SI is never allowed. For an imported SI,
            // only the newly copied PDF can be removed on failed attachment.
            if(!placed.reusedExisting && !placed.renamedExisting)QFile::remove(placed.path);
            QMessageBox::warning(this,QStringLiteral("SI 연결 실패"),
                QStringLiteral("기존 SI와 서지정보는 보존했습니다. 다시 연결해 주세요."));
            return;
        }
        QString syncError;
        if(localSync_ && !localSync_->publish(placed.path,&syncError))
            QMessageBox::warning(this,QStringLiteral("SI 동기화 확인 필요"),
                QStringLiteral("로컬 SI 연결은 완료됐지만 MYBOX 메타데이터 저장을 확인하세요.\\n")+syncError);
        expandedSIParents_.insert(parent);
        model_->refreshPdfCounts(root_);
        pages_->setCurrentIndex(0);
        selectFolder(paper.absolutePath());
    }
    void detachManualSI(const QString& path) {
        if(!bibliography_.detachManualSI(DocumentClaim::identity(path)))return;
        if(localSync_)localSync_->publish(path);
        refreshFileLists();
    }

    QListWidget* makePdfList(QWidget* parent) {
        auto* list = new PaperFileList(parent);
        list->setMouseTracking(true);
        list->viewport()->installEventFilter(this);
        // Preserve the existing one-card-per-row vertical list.
        // Never switch this widget to IconMode / wrapping / a multi-column grid.
        list->setViewMode(QListView::ListMode);
        list->setFlow(QListView::TopToBottom);
        list->setWrapping(false);
        list->setSpacing(0);
        list->setUniformItemSizes(false); // allow small SI children and 29px expandable parent footer
        list->setResizeMode(QListView::Adjust);
        list->setSelectionMode(QAbstractItemView::ExtendedSelection);
        list->setDragEnabled(true);
        list->setDragDropMode(QAbstractItemView::DragOnly);
        list->setDefaultDropAction(Qt::MoveAction);
        list->setEditTriggers(QAbstractItemView::NoEditTriggers);
        list->setSortingEnabled(false);
        list->setObjectName(QStringLiteral("LunaDocumentList"));
        auto* delegate = new DocumentCard(list);
        delegate->setCompact(compactPaperMode_);
        delegate->saveMemo = [this](const QString& path, const QString& text) { savePaperMemo(path, text); };
        list->setItemDelegate(delegate);
        list->setWordWrap(true);
        list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        new SmoothLibraryWheel(list, 96);
        list->setContextMenuPolicy(Qt::CustomContextMenu);
        auto* favoriteAction = new QAction(QStringLiteral("즐겨찾기 전환"), list);
        favoriteAction->setObjectName(QStringLiteral("LunaToggleFavorite"));
        favoriteAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+D")));
        favoriteAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        list->addAction(favoriteAction);
        auto* copyFiles = new QAction(QStringLiteral("PDF 파일 복사"), list);
        copyFiles->setObjectName(QStringLiteral("LunaCopyPdfs"));
        copyFiles->setShortcut(QKeySequence::Copy);
        copyFiles->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        list->addAction(copyFiles);
        auto* undoTrash = new QAction(QStringLiteral("PDF 삭제 실행 취소 (Ctrl+Z)"), list);
        undoTrash->setObjectName(QStringLiteral("LunaUndoTrash"));
        undoTrash->setShortcut(QKeySequence::Undo);
        undoTrash->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        list->addAction(undoTrash);
        connect(undoTrash, &QAction::triggered, this, [this] { undoRecentTrash(); });
        connect(copyFiles, &QAction::triggered, this, [this, list] { copySelectedPdfs(list); });
        auto* pasteFiles = new QAction(QStringLiteral("현재 폴더에 PDF 붙여넣기"), list);
        pasteFiles->setObjectName(QStringLiteral("LunaPastePdfs"));
        pasteFiles->setShortcut(QKeySequence::Paste);
        pasteFiles->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        list->addAction(pasteFiles);
        connect(pasteFiles, &QAction::triggered, this, [this] { pastePdfs(current_); });
        connect(favoriteAction, &QAction::triggered, this, [this, list] {
            if (auto* item = list->currentItem()) toggleFavorite(item->data(Qt::UserRole).toString());
        });
        connect(list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) { openItem(item); });
        connect(list, &QListWidget::customContextMenuRequested, this, [this, list](const QPoint& point) {
            auto* item = list->itemAt(point);
            if (!item) return;
            const QString path = item->data(Qt::UserRole).toString();
            // Snapshot the Shift selection before a modal context menu or
            // background folder rescan can rebuild QListWidgetItem pointers.
            QStringList contextSelection;
            for (auto* selected : list->selectedItems())
                contextSelection.append(selected->data(Qt::UserRole).toString());
            QMenu menu(this);
            auto* favorite = menu.addAction(item->data(Qt::UserRole + 2).toBool() ? QStringLiteral("즐겨찾기 해제") : QStringLiteral("즐겨찾기 추가"));
            auto* crop = menu.addAction(QStringLiteral("현재 PDF의 썸네일 지정…"));
            crop->setEnabled(QFileInfo(path).canonicalFilePath() == QFileInfo(viewer_->getProgramController()->getOriginalFileName()).canonicalFilePath());
            auto* memo = menu.addAction(QStringLiteral("메모 편집…"));
            memo->setObjectName(QStringLiteral("LunaEditPaperMemo"));
            auto* citation = menu.addAction(QStringLiteral("서지정보 수정…"));
            auto* renameFromTitle=menu.addAction(QStringLiteral("서지정보 제목으로 실제 PDF 파일명 변경…"));
            renameFromTitle->setObjectName(QStringLiteral("LunaApplyVerifiedPdfFilename"));
            const QVariantMap fileRecord=bibliography_.record(DocumentClaim::identity(path));
            const bool linkedSI=fileRecord.value("manualSI").toBool();
            renameFromTitle->setEnabled(linkedSI ||
                (!fileRecord.value("title").toString().simplified().isEmpty() &&
                 fileRecord.value("documentKind").toString()!=QStringLiteral("supporting_information")));
            if(linkedSI) {
                renameFromTitle->setText(QStringLiteral("연결된 원논문 제목으로 SI 실제 파일명 변경…"));
                renameFromTitle->setToolTip(QStringLiteral(
                    "SI는 자신의 첫 페이지가 아닌 연결된 원논문의 검증된 제목으로 이름을 정합니다."));
            }
            auto* extractCitation = menu.addAction(QStringLiteral("첫 페이지 서지정보 추출…"));
            extractCitation->setObjectName(QStringLiteral("LunaExtractCitationContextMenu"));
            auto* refreshCitation = menu.addAction(QStringLiteral("원본 서지정보로 새로고침"));
            refreshCitation->setObjectName(QStringLiteral("LunaRefreshMetadata"));
            menu.addSeparator();
            const bool manualChild=item->data(Qt::UserRole+5).toMap().value("manualSI").toBool();
            auto* addSI=menu.addAction(QStringLiteral("SI 추가…"));
            addSI->setObjectName(QStringLiteral("LunaAddManualSI"));
            addSI->setEnabled(!manualChild);
            auto* unlinkSI=menu.addAction(QStringLiteral("SI 연결 해제 (파일 유지)"));
            unlinkSI->setObjectName(QStringLiteral("LunaDetachManualSI"));
            unlinkSI->setEnabled(manualChild);
            menu.addSeparator();
            auto* copy = menu.addAction(QStringLiteral("PDF 복사 (Ctrl+C)"));
            auto* paste = menu.addAction(QStringLiteral("현재 폴더에 붙여넣기 (Ctrl+V)"));
            auto* copyTo = menu.addAction(QStringLiteral("PDF와 썸네일 복사 위치…"));
            auto* move = menu.addAction(QStringLiteral("PDF와 썸네일 이동…"));
            auto* trash = menu.addAction(QStringLiteral("Windows 휴지통으로 삭제…"));
            auto* undoTrash = menu.addAction(QStringLiteral("직전 삭제 묶음 복원 (Ctrl+Z)"));
            undoTrash->setEnabled(recentUndo_.available());
            trash->setObjectName(QStringLiteral("LunaTrashPdf"));
            auto* chosen = menu.exec(list->viewport()->mapToGlobal(point));
            if (chosen == favorite) toggleFavorite(path);
            else if (chosen == crop) startThumbnailCrop();
            else if (chosen == memo) {
                // Explicit edit only. QListWidget has NoEditTriggers so
                // ordinary row clicks cannot accidentally open a text editor.
                list->editItem(item);
            }
            else if (chosen == citation) editBibliography(path);
            else if (chosen == renameFromTitle) {
                const QString id=DocumentClaim::identity(path);
                QVariantMap record=bibliography_.record(id);
                const bool siLinked=record.value("manualSI").toBool();
                QVariantMap approved=record;
                if (!siLinked) approved["titleSource"]=QStringLiteral("manual");
                const QString preferred=siLinked
                    ? verifiedFilenameFor(id,QFileInfo(path))
                    : luna::AutoFileName::fromVerifiedTitle(
                        approved,QFileInfo(path).absolutePath());
                const QString name=availableVerifiedTitleName(id,preferred);
                if (name.isEmpty()) {
                    QMessageBox::information(this,QStringLiteral("파일명 변경 불가"),
                        siLinked ? QStringLiteral(
                            "연결된 원논문의 제목을 아직 검증하지 못했습니다. 원논문 서지정보를 확인한 후 자동으로 SI 파일명을 변경합니다.")
                            : QStringLiteral("제목 또는 경로를 확인해 주세요. 원본 PDF는 변경하지 않았습니다."));
                } else if (QMessageBox::question(this,QStringLiteral("PDF 실제 파일명 변경"),
                    QStringLiteral("탐색기에서 보이는 실제 파일명을 변경할까요?\n\n%1\n→ %2\n\n"
                                   "PDF·썸네일·서지정보를 같이 연결하며, 열린 문서는 탭을 닫은 후 변경됩니다.")
                        .arg(QFileInfo(path).fileName(),name),
                    QMessageBox::Yes|QMessageBox::Cancel,QMessageBox::Cancel)==QMessageBox::Yes) {
                    if (!siLinked) {
                        record["titleSource"]=QStringLiteral("manual");
                        record["source"]=QStringLiteral("User-confirmed filename");
                    }
                    if (!siLinked && !bibliography_.save(id,record,false)) {
                        QMessageBox::warning(this,QStringLiteral("파일명 변경 실패"),
                            QStringLiteral("서지정보 확인 상태를 저장하지 못했습니다."));
                    } else {
                        QString reason, renamedPath;
                        if(renameExtractedTitleFile(id,&reason,&renamedPath)) pendingTitleRenames_.remove(id);
                        else {
                            pendingTitleRenames_.insert(id);
                            if (!titleRenameTimer_.isActive()) titleRenameTimer_.start();
                        }
                        const bool changed=!renamedPath.isEmpty() && !QFileInfo::exists(path) &&
                            QFileInfo(renamedPath).isFile();
                        if (changed) QMessageBox::information(this,QStringLiteral("PDF 파일명 변경 완료"),
                            QStringLiteral("Windows 탐색기의 실제 PDF 파일명을 확인했습니다.\n%1").arg(QFileInfo(renamedPath).fileName()));
                        else QMessageBox::information(this,QStringLiteral("PDF 파일명 변경 상태"),
                            reason.isEmpty()?QStringLiteral("파일명 변경 결과를 확인할 수 없습니다."):reason);
                    }
                }
            }
            else if (chosen == extractCitation) extractBibliography(path);
            else if (chosen == refreshCitation) refreshOriginalBibliography(path);
            else if (chosen == addSI) addManualSI(path);
            else if (chosen == unlinkSI) detachManualSI(path);
            else if (chosen == copy) {
                if (list->selectedItems().isEmpty()) list->setCurrentItem(item);
                copySelectedPdfs(list);
            }
            else if (chosen == paste) pastePdfs(current_);
            else if (chosen == copyTo) {
                const QString target = QFileDialog::getExistingDirectory(this,
                    QStringLiteral("PDF와 썸네일 복사 대상 폴더"), QFileInfo(path).absolutePath());
                if (!target.isEmpty()) copyDocumentTo(path, target);
            }
            else if (chosen == move) moveDocument(path);
            else if (chosen == trash) trashSelectedDocuments(contextSelection, path);
            else if (chosen == undoTrash) undoRecentTrash();
        });
        connect(list, &QListWidget::itemClicked, this, [this, list](QListWidgetItem* item) {
            const QPoint cursor = list->viewport()->mapFromGlobal(QCursor::pos());
            const QRect rect = list->visualItemRect(item);
            if(!item->data(Qt::UserRole+10).toBool() &&
               item->data(Qt::UserRole+11).toInt()>0 &&
               DocumentCard::siFooterRect(rect).contains(cursor)) {
                toggleManualSIGroup(item->data(Qt::UserRole).toString());
                return;
            }
            const auto* delegate = static_cast<DocumentCard*>(list->itemDelegate());
            const QRect hit = delegate && delegate->compact() ?
                DocumentCard::compactFavoriteRect(rect) : DocumentCard::favoriteRect(rect);
            if (hit.contains(cursor))
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
        operation->setProperty("LunaMoveSourceOpened", opened);
        operation->setProperty("LunaMoveClaimAccepted", false);
        int page = 0;
        double zoom = 2.0;
        pdf::PageLayout layout = pdf::PageLayout::OneColumn;
        if (opened) {
            if (!operation->isEnabled() || !controller->canClose()) {
                qWarning("LUNA paired move deferred: native reader is busy");
                return;
            }
            if (!controller->askForSaveDocumentBeforeClose()) return;
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
        operation->setProperty("LunaMoveFileSucceeded", result.success);
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
            if (localSync_) localSync_->publish(result.documentPath);
            const auto favorites = savedPaths(QStringLiteral("library/favorites"));
            favorites_ = QSet<QString>(favorites.cbegin(), favorites.cend());
            ThumbnailCache::instance()->invalidate();
            allTimer_->stop(); allIterator_.reset(); allScanned_ = false; allPdfs_->clear();
            selectFolder(current_);
            if (pages_->currentIndex() == 1) scanAll();
            else if (pages_->currentIndex() == 2) showHistory(showingFavorites_);
        }
        if (opened && result.success && movedHandler) {
            const bool accepted = movedHandler(operation.data(), source, result.documentPath);
            operation->setProperty("LunaMoveClaimAccepted", accepted);
            if (!accepted) return;
        }
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
        if (result.success) model_->refreshPdfCounts(root_);
        if (!result.success) QMessageBox::warning(this, QStringLiteral("파일 이동 실패"), result.error);
    }
    void filter(QListWidget* list, const QString& word) {
        if(!list)return;
        QSet<QString> matchedSIParents;
        for(int i=0;i<list->count();++i) {
            auto* item=list->item(i);
            const bool child=item->data(Qt::UserRole+10).toBool();
            const bool matches=word.isEmpty() ||
                item->text().contains(word,Qt::CaseInsensitive) ||
                QFileInfo(item->data(Qt::UserRole).toString()).completeBaseName()
                    .contains(word,Qt::CaseInsensitive) ||
                item->data(Qt::UserRole+4).toString().contains(word,Qt::CaseInsensitive) ||
                item->data(Qt::UserRole+5).toMap().value("authors").toString()
                    .contains(word,Qt::CaseInsensitive) ||
                item->data(Qt::UserRole+5).toMap().value("journal").toString()
                    .contains(word,Qt::CaseInsensitive) ||
                luna::Bibliography::citation(item->data(Qt::UserRole+5).toMap())
                    .contains(word,Qt::CaseInsensitive);
            const bool visible=matches && (!child || !word.isEmpty() ||
                expandedSIParents_.contains(item->data(Qt::UserRole+13).toString()));
            item->setHidden(!visible);
            if(child && visible && !word.isEmpty())
                matchedSIParents.insert(item->data(Qt::UserRole+13).toString());
        }
        // Searching a child SI always exposes its parent card as context,
        // without silently modifying the user's ordinary folded state.
        if(!word.isEmpty() && !matchedSIParents.isEmpty())
            for(int i=0;i<list->count();++i) {
                auto* item=list->item(i);
                if(matchedSIParents.contains(item->data(Qt::UserRole).toString()))
                    item->setHidden(false);
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
        allSeen_.clear();
        allScanned_ = false;
        allCount_->setText(QStringLiteral("전체 PDF"));
        selectFolder(root_);
        model_->refreshPdfCounts(root_);
        // The selected library folder is the sole source of truth. Its PDF
        // metadata sidecars travel with files if MYBOX/OneDrive/etc. syncs
        // that folder. No separate cloud button, credentials or API.
        if (localSync_) localSync_->configure(root_,true);
        if (pages_->currentIndex() == 1) scanAll();
    }
    void selectFolder(const QString& path) {
        hideThumbnail();
        if (!QFileInfo(path).isDir()) return;
        current_ = path;
        const bool isRoot = QFileInfo(path).canonicalFilePath() == root_;
        if (isRoot) folderTab_->setChecked(true);
        else if (auto* group = folderTab_->group()) {
            // The root shortcut is not the selected child folder.
            group->setExclusive(false);
            for (auto* button : group->buttons()) button->setChecked(false);
            group->setExclusive(true);
        }
        if (auto* selection = tree_->selectionModel()) {
            if (isRoot) selection->clearSelection();
            else {
                const QModelIndex index = model_->index(path);
                if (index.isValid())
                    selection->setCurrentIndex(index,
                        QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            }
        }
        folderTimer_->stop();
        folderIterator_.reset();
        folderPdfs_->clear();
        folderSeen_.clear();
        folderPdfs_->setSortingEnabled(false);
        folderCount_->setText(QStringLiteral("PDF 검색 중…"));
        // Folder view is nonrecursive; All Documents remains recursive.
        folderIterator_ = std::make_unique<QDirIterator>(
            current_, QStringList{QStringLiteral("*.pdf"), QStringLiteral("*.PDF")},
            QDir::Files | QDir::Readable | QDir::NoDotAndDotDot | QDir::NoSymLinks);
        folderTimer_->start();
    }
    void scanAll() {
        allTimer_->stop();
        allIterator_.reset();
        allPdfs_->clear();
        allSeen_.clear();
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
        // repainting all full-width paper cards after each individual insertion.
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
            if (!info.isFile() || info.isSymLink() || info.suffix().compare(
                    QStringLiteral("pdf"), Qt::CaseInsensitive) != 0) continue;
            const QString identity = DocumentClaim::identity(path);
            auto& seen = all ? allSeen_ : folderSeen_;
            if (identity.isEmpty() || seen.contains(identity)) continue;
            seen.insert(identity);
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
            arrangeManualSIRows(list);
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
    bool allowedIconFolder(const QString& folder, QString* error) const {
        const QFileInfo info(folder);
        if (!info.isDir() || info.isSymLink()) {
            if (error) *error = QStringLiteral("일반 로컬 폴더만 변경할 수 있습니다.");
            return false;
        }
        const QString canonical = info.canonicalFilePath();
        const QString relative = QDir(root_).relativeFilePath(canonical);
        if (root_.isEmpty() || canonical.isEmpty() ||
            relative == QStringLiteral("..") ||
            relative.startsWith(QStringLiteral("../")) || QDir::isAbsolutePath(relative)) {
            if (error) *error = QStringLiteral("현재 LUNA 라이브러리 밖의 폴더는 변경하지 않습니다.");
            return false;
        }
        return true;
    }
    bool saveFolderIcon(const QString& folder, const QString& iconId, QString* error) {
        if (!allowedIconFolder(folder, error)) return false;
        if (validatedFolderIconId(iconId) != iconId) {
            if (error) *error = QStringLiteral("지원하지 않는 폴더 아이콘 ID입니다.");
            return false;
        }
        const QString path = folderMetadataPath(folder);
        const QFileInfo file(path);
        if (file.isSymLink() || (file.exists() && (file.size() > 16 * 1024 || !file.isFile()))) {
            if (error) *error = QStringLiteral("연결 파일이나 비정상 폴더 메타데이터는 수정하지 않습니다.");
            return false;
        }
        QJsonObject metadata = readFolderMetadata(folder); // Preserve other future fields.
        if (!metadata.value(QStringLiteral("folderId")).isString() ||
            QUuid(metadata.value(QStringLiteral("folderId")).toString()).isNull())
            metadata.insert(QStringLiteral("folderId"), QUuid::createUuid().toString(QUuid::WithoutBraces));
        metadata.insert(QStringLiteral("schemaVersion"), 1);
        metadata.insert(QStringLiteral("iconId"), iconId);
        if (!metadata.contains(QStringLiteral("isImportant")))
            metadata.insert(QStringLiteral("isImportant"), false);
        metadata.insert(QStringLiteral("relativePath"),
            QDir(root_).relativeFilePath(QFileInfo(folder).canonicalFilePath()));
        metadata.insert(QStringLiteral("updatedAt"),
            QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
        const QByteArray contents = QJsonDocument(metadata).toJson(QJsonDocument::Indented);
        QSaveFile output(path);
        output.setDirectWriteFallback(false);
        if (!output.open(QIODevice::WriteOnly) ||
            output.write(contents) != contents.size() || !output.commit()) {
            if (error) *error = QStringLiteral("폴더 내부 LUNA.folder.json 저장에 실패했습니다.");
            return false;
        }
        folderIconStamps_.remove(QFileInfo(folder).canonicalFilePath());
        model_->refreshFolderIcon(model_->index(folder));
        tree_->viewport()->update();
        return true;
    }
    bool resetFolderIcon(const QString& folder, QString* error) {
        // Keep folderId and independent isImportant, only restore icon type.
        return saveFolderIcon(folder, QStringLiteral("general"), error);
    }
    void refreshVisibleFolderIcons() {
        if (!tree_ || !tree_->isVisible() || tree_->viewport()->width() < 1) return;
        const int x = tree_->viewport()->width() / 2;
        const int stride = qMax(12, tree_->iconSize().height());
        for (int y = 1; y < tree_->viewport()->height(); y += stride) {
            const QModelIndex index = tree_->indexAt(QPoint(x, y));
            if (!index.isValid() || !model_->isDir(index)) continue;
            const QString folder = QFileInfo(model_->filePath(index)).canonicalFilePath();
            if (folder.isEmpty()) continue;
            const QFileInfo file(folderMetadataPath(folder));
            const QString stamp = file.isFile() ?
                QStringLiteral("%1:%2").arg(file.size()).arg(file.lastModified().toMSecsSinceEpoch()) :
                QStringLiteral("-");
            auto found = folderIconStamps_.constFind(folder);
            if (found != folderIconStamps_.cend() && found.value() != stamp)
                model_->refreshFolderIcon(index);
            folderIconStamps_.insert(folder, stamp);
        }
    }
    void renameFolder(const QString& path) {
        const QFileInfo folder(path);
        if (!folder.isDir() || folder.isSymLink()) return;
        const QString original = folder.canonicalFilePath();
        if (original.isEmpty() ||
            !(original == root_ ||
              original.startsWith(root_ + QLatin1Char('/'), Qt::CaseInsensitive))) return;
        bool accepted = false;
        const QString name = QInputDialog::getText(this, QStringLiteral("폴더 이름 변경"),
            QStringLiteral("새 폴더 이름"), QLineEdit::Normal, folder.fileName(), &accepted).trimmed();
        if (!accepted || name == folder.fileName()) return;
        static const QRegularExpression invalid(QStringLiteral(R"REGEX([<>:"/\\|?*])REGEX"));
        static const QRegularExpression reserved(
            QStringLiteral(R"REGEX(^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\..*)?$)REGEX"),
            QRegularExpression::CaseInsensitiveOption);
        if (name.isEmpty() || name == QStringLiteral(".") || name == QStringLiteral("..") ||
            name.endsWith(QLatin1Char('.')) || name.endsWith(QLatin1Char(' ')) ||
            invalid.match(name).hasMatch() || reserved.match(name).hasMatch()) {
            QMessageBox::warning(this, QStringLiteral("잘못된 폴더 이름"),
                QStringLiteral("Windows에서 허용되지 않는 폴더 이름입니다."));
            return;
        }
        const QString destination = QDir(folder.absolutePath()).filePath(name);
        if (QFileInfo::exists(destination)) {
            QMessageBox::warning(this, QStringLiteral("폴더 이름 중복"),
                QStringLiteral("대상 폴더가 이미 존재하므로 덮어쓰지 않습니다."));
            return;
        }
        const QString openPath = viewer_->getProgramController()->getOriginalFileName();
        if (!openPath.isEmpty() &&
            (openPath.startsWith(original + QLatin1Char('/'), Qt::CaseInsensitive) ||
             openPath.startsWith(original + QLatin1Char('\\'), Qt::CaseInsensitive))) {
            QMessageBox::information(this, QStringLiteral("열린 PDF"),
                QStringLiteral("폴더 내부 PDF를 닫은 다음 폴더 이름을 변경해 주세요."));
            return;
        }
        // Resolve old identities and old memo keys before Windows renames the
        // directory; after the rename these paths no longer exist.
        struct FileIdentity { QString oldPath, identity, relative, dateKey; };
        QList<FileIdentity> files;
        QDirIterator scan(original, QStringList{QStringLiteral("*.pdf"), QStringLiteral("*.PDF")},
                          QDir::Files | QDir::Readable | QDir::NoDotAndDotDot,
                          QDirIterator::Subdirectories);
        while (scan.hasNext()) {
            const QString oldPath = scan.next();
            files.append({oldPath, DocumentClaim::identity(oldPath),
                          QDir(original).relativeFilePath(oldPath), openedDateKey(oldPath)});
        }
        if (!renameWithoutReplacement(original, destination)) {
            QMessageBox::warning(this, QStringLiteral("폴더 이름 변경 실패"),
                QStringLiteral("다른 프로그램에서 파일을 사용 중이거나 권한이 없습니다. 원래 폴더는 유지됩니다."));
            return;
        }
        const auto rewrite = [&](const QString& value) {
            if (value.compare(original, Qt::CaseInsensitive) == 0) return destination;
            if (value.startsWith(original + QLatin1Char('/'), Qt::CaseInsensitive) ||
                value.startsWith(original + QLatin1Char('\\'), Qt::CaseInsensitive))
                return destination + value.mid(original.size());
            return value;
        };
        QSettings settings(QSettings::defaultFormat(), QSettings::UserScope,
                           QStringLiteral("LUNA"), QStringLiteral("LUNA Library"));
        for (const QString& key : {QStringLiteral("library/favorites"), QStringLiteral("library/recent")}) {
            QStringList values = settings.value(key).toStringList();
            for (QString& value : values) value = rewrite(value);
            values.removeDuplicates();
            settings.setValue(key, values);
        }
        bool partialMetadataFailure = false;
        for (const auto& file : files) {
            const QString newPath = QDir(destination).filePath(file.relative);
            const QString newKey = openedDateKey(newPath);
            for (const QString& prefix : {QStringLiteral("library/memos/"),
                                         QStringLiteral("library/openedDates/")}) {
                const QVariant value = settings.value(prefix + file.dateKey);
                if (value.isValid() && !settings.contains(prefix + newKey)) {
                    settings.setValue(prefix + newKey, value);
                    if (prefix == QStringLiteral("library/memos/")) {
                        memos_.insert(newKey, value.toString());
                        memos_.remove(file.dateKey);
                    } else {
                        openedDates_.insert(newKey, value.toString());
                        openedDates_.remove(file.dateKey);
                    }
                    settings.remove(prefix + file.dateKey);
                }
            }
            if (!bibliography_.relocate(file.identity, DocumentClaim::identity(newPath)))
                partialMetadataFailure = true;
        }
        const bool rootChanged = original.compare(root_, Qt::CaseInsensitive) == 0;
        if (rootChanged) {
            root_ = destination;
            settings.setValue(QStringLiteral("library/root"), root_);
        }
        current_ = rewrite(current_);
        settings.sync();
        if (settings.status() != QSettings::NoError) partialMetadataFailure = true;
        const QStringList favorites = settings.value(QStringLiteral("library/favorites")).toStringList();
        favorites_ = QSet<QString>(favorites.cbegin(), favorites.cend());
        // Folder icon JSON moves with the directory. Rewrite only the relative
        // location hint, preserving stable folderId and custom iconId.
        QStringList folders{destination};
        QDirIterator descendants(destination, QDir::Dirs | QDir::NoDotAndDotDot,
                                 QDirIterator::Subdirectories);
        while (descendants.hasNext()) folders.append(descendants.next());
        for (const QString& folderPath : folders) {
            const QString metadataFile = folderMetadataPath(folderPath);
            const QFileInfo metaFile(metadataFile);
            if (!metaFile.isFile() || metaFile.isSymLink()) continue;
            QJsonObject metadata = readFolderMetadata(folderPath);
            if (metadata.isEmpty()) continue;
            metadata.insert(QStringLiteral("relativePath"),
                            QDir(root_).relativeFilePath(folderPath));
            const QByteArray bytes = QJsonDocument(metadata).toJson(QJsonDocument::Indented);
            QSaveFile writer(metadataFile);
            writer.setDirectWriteFallback(false);
            if (!writer.open(QIODevice::WriteOnly) ||
                writer.write(bytes) != bytes.size() || !writer.commit())
                partialMetadataFailure = true;
        }
        folderIconStamps_.clear();
        if (rootChanged) selectRoot(root_);
        else {
            model_->setRootPath(root_);
            tree_->setRootIndex(model_->index(root_));
            refreshFileLists();
        }
        if (partialMetadataFailure)
            QMessageBox::warning(this, QStringLiteral("폴더 변경 완료 · 메타데이터 확인"),
                QStringLiteral("실제 폴더 이름은 변경됐지만 일부 사용자 정보 갱신이 실패했습니다. 기존 설정을 삭제하지 않았습니다."));
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
        } else model_->refreshPdfCounts(root_);
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
    QSet<QString> pendingTitleRenames_;
    QTimer titleRenameTimer_;
    bool compactPaperMode_ = false;
    QSet<QString> expandedSIParents_;
    luna::MetadataFetcher* metadataFetcher_ = nullptr;
    QQueue<QPair<QString, bool>> pendingImportedMetadata_;
    bool importMetadataRunning_ = false;
    QSet<QString> analyzedCitations_;
    QSet<QString> runningCitations_;
    QHash<QString, QTimer*> delayedCitations_;
    QSet<QString> pendingCitationExtracts_;
    QSet<QString> pendingOriginalResets_;
    QAction* cropAction_ = nullptr;
    QAction* pagePreviewAction_ = nullptr;
    QMetaObject::Connection documentTitleConnection_, cropConnection_;
    QSplitter* split_ = nullptr;
    QTimer widthSaveTimer_;
    QWidget *navigationSlot_ = nullptr, *documentsSlot_ = nullptr;
    QWidget *navigationFold_ = nullptr, *documentsFold_ = nullptr;
    int expandedNavWidth_ = 225, expandedDocumentsWidth_ = 325;
    bool adjustingPanels_ = false;
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
    luna::RecentTrashUndo recentUndo_;
    luna::LocalFolderSync* localSync_=nullptr;
    bool applyingRemote_=false;
    QStringList undoFavorites_, undoRecent_;
    LunaFolderModel* model_ = nullptr;
    QTreeView* tree_ = nullptr;
    QTimer* folderIconPoll_ = nullptr;
    QHash<QString, QString> folderIconStamps_;
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
    QSet<QString> allSeen_;
    QSet<QString> folderSeen_;
};
class WindowState final : public QObject {
public:
    explicit WindowState(QMainWindow* window) : QObject(window), window_(window) { window->installEventFilter(this); }
protected:
    bool eventFilter(QObject* object, QEvent* event) override {
        if (object == window_ && event->type() == QEvent::Close) {
            QSettings settings(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("LUNA"), QStringLiteral("LUNA Library"));
            settings.setValue(QStringLiteral("window/geometry"), window_->saveGeometry());
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
    dock->installEventFilter(library);
    window.addDockWidget(Qt::LeftDockWidgetArea, dock);
    dock->show();
    const int storedWidth = qBound(432, QSettings(QSettings::defaultFormat(), QSettings::UserScope,
        QStringLiteral("LUNA"), QStringLiteral("LUNA Library")).value(QStringLiteral("library/expandedDockWidth"), 562).toInt(), 850);
    window.resizeDocks({dock}, {storedWidth}, Qt::Horizontal);
    auto* controls = new QToolBar(QStringLiteral("라이브러리"), &window);
    controls->setObjectName(QStringLiteral("LunaLibraryToolbar"));
    controls->setMovable(false);
    controls->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    controls->setIconSize(QSize(18, 18));
    auto* folders = controls->addAction(readerIcon(QStringLiteral("library")), QStringLiteral("라이브러리"));
    folders->setObjectName(QStringLiteral("LunaToggleNavigation"));
    folders->setCheckable(true);
    folders->setChecked(true);
    QObject::connect(folders, &QAction::triggered, library, [library] { library->toggleNavigation(); });
    auto* documents = controls->addAction(readerIcon(QStringLiteral("documents")), QStringLiteral("논문 목록"));
    documents->setObjectName(QStringLiteral("LunaToggleDocuments"));
    documents->setCheckable(true);
    documents->setChecked(true);
    QObject::connect(documents, &QAction::triggered, library, [library] { library->toggleDocuments(); });
    auto* focus = controls->addAction(QStringLiteral("집중 모드"));
    focus->setObjectName(QStringLiteral("LunaFocusMode"));
    focus->setCheckable(true);
    QObject::connect(focus, &QAction::toggled, dock, [dock, library, folders, documents](bool enabled) {
        if (!enabled) {
            library->showBothPanels();
            folders->setChecked(true);
            documents->setChecked(true);
        }
        dock->setVisible(!enabled);
    });
    // Legacy individual panel actions remain available in View, not as three
    // competing permanent controls in the approved interface.
    for (auto* action : {folders, documents, focus}) controls->removeAction(action);
    if (auto* view = viewer.findChild<QMenu*>(QStringLiteral("menuView"))) {
        view->addAction(folders);
        view->addAction(documents);
        auto* defaultZoom = new QAction(QStringLiteral("기본 시작 배율 설정…"), &window);
        defaultZoom->setObjectName(QStringLiteral("LunaDefaultOpeningZoom"));
        window.addAction(defaultZoom);
        view->addAction(defaultZoom);
        defaultZoom->setToolTip(QStringLiteral("새 PDF를 열 때 적용하는 기본 배율 (25~500%, 기본 200%)"));
        QObject::connect(defaultZoom, &QAction::triggered, &window, [&window] {
            QSettings preferences(QSettings::defaultFormat(), QSettings::UserScope,
                                  QStringLiteral("LUNA"), QStringLiteral("LUNA Library"));
            const int current = qBound(25, preferences.value(
                QStringLiteral("reader/defaultZoomPercent"), 200).toInt(), 500);
            bool accepted = false;
            const int percent = QInputDialog::getInt(&window,
                QStringLiteral("기본 시작 배율"),
                QStringLiteral("새 PDF를 열 때 사용할 배율 (%)"), current,
                25, 500, 5, &accepted);
            if (!accepted) return;
            preferences.setValue(QStringLiteral("reader/defaultZoomPercent"), percent);
            preferences.sync();
            if (preferences.status() != QSettings::NoError)
                QMessageBox::warning(&window, QStringLiteral("기본 배율 저장 실패"),
                    QStringLiteral("설정을 저장하지 못했습니다. 다시 시도해 주세요."));
        });
    }
    // Collapse buttons belong to the panel they operate, not to PDF tabs.
    auto panelButton = [](QWidget* parent, QAction* action, const QString& name) {
        auto* button = new QToolButton(parent);
        button->setObjectName(name);
        button->setDefaultAction(action);
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setIconSize(QSize(18, 18));
        button->setFixedSize(29, 29);
        button->setAccessibleName(action->text() + QStringLiteral(" 접기 / 펼치기"));
        button->setToolTip(button->accessibleName());
        return button;
    };
    auto* libraryHeading = library->findChild<QWidget*>(QStringLiteral("LunaLibraryHeader"));
    auto* paperHeading = library->findChild<QHBoxLayout*>(QStringLiteral("LunaPaperHeadingLayout"));
    if (libraryHeading && paperHeading) {
        libraryHeading->layout()->addWidget(panelButton(libraryHeading, folders,
            QStringLiteral("LunaNavigationCollapseButton")));
        paperHeading->addWidget(panelButton(paperHeading->parentWidget(), documents,
            QStringLiteral("LunaDocumentsCollapseButton")));
    }
    // Restore controls live INSIDE the original two splitter slots, never
    // float above PDF or jump to the far side of the paper list.
    auto* restoreNav = panelButton(library->navigationFold(), folders,
        QStringLiteral("LunaNavigationRestoreButton"));
    auto* restoreDocs = panelButton(library->documentsFold(), documents,
        QStringLiteral("LunaDocumentsRestoreButton"));
    library->navigationFold()->layout()->addWidget(restoreNav);
    library->documentsFold()->layout()->addWidget(restoreDocs);
    // Both 36px rails have a 1px outline. Center the 28px controls in
    // the remaining content width; preserve their original header Y anchors.
    restoreNav->setFixedWidth(28);
    restoreDocs->setFixedWidth(28);
    library->navigationFold()->layout()->setAlignment(restoreNav, Qt::AlignHCenter);
    library->documentsFold()->layout()->setAlignment(restoreDocs, Qt::AlignHCenter);
    restoreNav->setToolTip(QStringLiteral("라이브러리 펼치기"));
    restoreDocs->setToolTip(QStringLiteral("논문 목록 펼치기"));

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
    // Empty native QToolBar left a dark top rule above the PDF tabs.
    // Actions remain in View and panel headers, but no toolbar is docked.
    controls->hide();
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
        auto* layout = new QVBoxLayout(frame);
        layout->setContentsMargins(0, 7, 0, 0); layout->setSpacing(0);
        window.setCentralWidget(frame);
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
    // Restore window geometry only; never restore native PDF4QT toolbar state.
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
void flushLibraryTitleRenames(QMainWindow& shell) {
    if (auto* p = panel(shell)) p->flushPendingTitleRenamesAfterReaderClose();
}
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
// Explicit offline evaluator: no LibraryPanel, remote lookups, edits or renames.
int evaluatePdfCorpus(const QString& directory, const QString& report) {
    QJsonObject results;
    const auto files = QDir(directory).entryInfoList({QStringLiteral("*.pdf")}, QDir::Files, QDir::Name);
    if (files.isEmpty()) return 2;
    int evaluated=0;
    for (const auto& file : files) {
        std::fprintf(stderr,"LUNA_CORPUS_PROGRESS: %d/%d\n",++evaluated,int(files.size()));
        std::fflush(stderr);
        QVariantMap evidence;
        try {
            pdf::PDFProgress progress(nullptr);
            pdf::PDFDocumentReader reader(&progress, [](bool*) { return QString(); }, true, false);
            auto document = reader.readFromFile(file.absoluteFilePath());
            if (reader.getReadingResult() == pdf::PDFDocumentReader::Result::OK) {
                QVariantMap audit;
                const bool enabled=qEnvironmentVariableIsSet("LUNA_PUBLIC_GOLDSET_AUDIT");
                evidence = extractFirstPage(document,nullptr,enabled?&audit:nullptr);
                if (enabled) evidence["_titleAudit"]=audit;
            }
            else {
                evidence["evaluationError"] = QStringLiteral("unreadable_pdf");
                QString detail=reader.getErrorMessage();
                detail.replace(file.absoluteFilePath(),QStringLiteral("<input>"));
                detail.replace(file.fileName(),QStringLiteral("<document>"));
                evidence["evaluationDetail"]=detail.left(180);
            }
        } catch (...) { evidence["evaluationError"] = QStringLiteral("extraction_failed"); }
        results[file.completeBaseName()] = QJsonObject::fromVariantMap(evidence);
    }
    QSaveFile output(report);
    if (!output.open(QIODevice::WriteOnly)) return 3;
    return output.write(QJsonDocument(results).toJson()) > 0 && output.commit() ? 0 : 4;
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
    QJsonArray samples;
    QVariantMap seen;
    auto capture = [&](const char* phase,const QVariantMap& data) {
        QVariantMap value;
        for (const char* key : {"authors","authorsSource","authors_structured","field_sources",
             "metadata_source","title","titleSource","titleCandidate","titleGlyphStatus","documentKind","doi"})
            if (data.contains(key)) value[key]=data.value(key);
        value["phase"]=QString::fromLatin1(phase);
        value["sourceFileExists"]=QFileInfo(controller->getOriginalFileName()).isFile();
        if (samples.size()<16) samples.append(QJsonObject::fromVariantMap(value));
        QSaveFile file(capturePath+QStringLiteral(".metadata.json"));
        if (file.open(QIODevice::WriteOnly)) { file.write(QJsonDocument(samples).toJson()); file.commit(); }
    };
    auto awaitAuthor = [&](const QString& expected) {
        QElapsedTimer timer; timer.start();
        while (timer.elapsed() < 15000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            for (auto* candidate : {panel->folderPdfs_, panel->allPdfs_, panel->historyPdfs_})
                for (int i = 0; i < candidate->count(); ++i) {
                    auto* row = candidate->item(i);
                    if (row->data(Qt::UserRole + 6).toString() != identity) continue;
                    const auto metadata = row->data(Qt::UserRole + 5).toMap();
                    if (seen!=metadata) { seen=metadata; capture("card",metadata); }
                    bool matches = luna::Bibliography::lastListedAuthor(metadata.value("authors").toString())==expected;
                    const auto names=metadata.value("authors_structured").toStringList();
                    if (!names.isEmpty()) matches=matches && names.last()==expected;
                    matches=matches && luna::Bibliography::citation(metadata).startsWith(expected);
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
    if (!siProbe) {
        auto snapshot=*controller->getDocument();
        const auto direct=luna::extractFirstPage(snapshot);
        capture("direct-first-page",direct);
        if (direct.value("authors").toString()!=correct) { error="Exact first-page final author incorrect"; return false; }
    }
    if (!awaitAuthor(correct)) {
        error = siProbe ? "Real SI PDF generated paper metadata or was not classified as SI" :
            "Real PDF automatic card author was not John J. Molloy";
        capture("failure",seen);
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
        seen.clear();
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
    auto checkpoint = [&capturePath](const char* label) {
        if (capturePath.isEmpty()) return;
        QFile f(capturePath + QStringLiteral(".stage.txt"));
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(label);
    };
    checkpoint("init");
    auto fail = [&error, &checkpoint](const char* message) {
        error = QString::fromLatin1(message);
        checkpoint(message);
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
    // Real packaged Qt resource verification. Source-only contracts cannot
    // prove that the 40 SVGs actually ship in the Windows executable.
    for (const auto& preset : kFolderIconPresets)
        if (embeddedFolderIcon(QString::fromLatin1(preset.id)).pixmap(20, 20).isNull())
            return fail("Folder SVG resource missing from the native Windows package");
    for (const auto& preset : kSymbolIconPresets) {
        const QString id = QStringLiteral("symbol:") + QString::fromLatin1(preset.id);
        if (validatedFolderIconId(id) != id ||
            embeddedFolderIcon(id).pixmap(20, 20).isNull() ||
            embeddedFolderIcon(id).pixmap(44, 44).isNull())
            return fail("General 40-symbol SVG resource missing or unrenderable");
    }
    if (validatedFolderIconId(QStringLiteral("symbol:unknown")) != QStringLiteral("general"))
        return fail("Unrecognized icon ID bypassed the fallback rule");
    auto* toolbar = window.findChild<QToolBar*>(QStringLiteral("mainToolBar"));
    if (!toolbar || !toolbar->isVisible() || !window.getProgramController()->getPdfWidget()->isVisible())
        return fail("Native PDF toolbar or renderer widget is hidden");
    if (navigation->width() < 192 || documents->width() < 285 || window.getProgramController()->getPdfWidget()->width() < 300)
        return fail("Library/PDF geometry does not fit the test window");
    auto* list = pages->widget(0)->findChild<QListWidget*>();
    if (!list || list->count() != 1) return fail("Synthetic library must contain exactly one PDF");
    auto* item = list->item(0);
    // Real Qt viewport event: preserve three Shift-selected items when
    // right-clicking another row. No actual user PDFs are deleted.
    auto* selectedA = new QListWidgetItem(QStringLiteral("selection A"), list);
    auto* selectedB = new QListWidgetItem(QStringLiteral("selection B"), list);
    auto* outside = new QListWidgetItem(QStringLiteral("unselected target"), list);
    list->selectionModel()->select(list->model()->index(0, 0), QItemSelectionModel::ClearAndSelect);
    list->selectionModel()->select(list->model()->index(1, 0), QItemSelectionModel::Select);
    list->selectionModel()->select(list->model()->index(2, 0), QItemSelectionModel::Select);
    list->scrollToItem(outside, QAbstractItemView::PositionAtTop);
    QCoreApplication::processEvents();
    const QPoint clickAt = list->visualItemRect(outside).topLeft() + QPoint(25, 20);
    if (list->itemAt(clickAt) != outside)
        return fail("Synthetic unselected context row was not visible");
    QMouseEvent rightDown(QEvent::MouseButtonPress, QPointF(clickAt),
        QPointF(list->viewport()->mapToGlobal(clickAt)),
        Qt::RightButton, Qt::RightButton, Qt::NoModifier);
    QApplication::sendEvent(list->viewport(), &rightDown);
    QMouseEvent rightUp(QEvent::MouseButtonRelease, QPointF(clickAt),
        QPointF(list->viewport()->mapToGlobal(clickAt)),
        Qt::RightButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(list->viewport(), &rightUp);
    if (list->selectedItems().size() != 3 || outside->isSelected())
        return fail("Right-click collapsed a multi-selected PDF group");
    delete list->takeItem(list->row(outside));
    delete list->takeItem(list->row(selectedB));
    delete list->takeItem(list->row(selectedA));
    list->clearSelection();
    list->scrollToItem(item, QAbstractItemView::PositionAtTop);
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
    // Tags are removed from the card and popup, but old QSettings entries
    // are deliberately left untouched for a safe rollback.
    auto* manualPanel = dynamic_cast<LibraryPanel*>(dock->widget());
    if (!manualPanel || item->data(Qt::UserRole + 8).isValid() ||
        list->editTriggers() != QAbstractItemView::NoEditTriggers)
        return fail("Removed tags or accidental paper memo edit triggers returned");
    // Explicit editing is allowed only through the context action, never by
    // the old delegate's left-click/double-click editorEvent.
    if (!list->itemDelegate() || !DocumentCard::memoRect(list->visualItemRect(item)).isValid())
        return fail("Paper memo row geometry missing");
    auto* listMode = window.findChild<QToolButton*>(QStringLiteral("LunaListViewMode"));
    auto* cardMode = window.findChild<QToolButton*>(QStringLiteral("LunaCardViewMode"));
    if (!listMode || !cardMode || !listMode->isCheckable() ||
        !cardMode->isCheckable() || listMode->isChecked() == cardMode->isChecked())
        return fail("Exclusive paper list/card view buttons are missing or ambiguous");
    const bool originalCompact = listMode->isChecked();
    if (originalCompact) cardMode->click();
    QCoreApplication::processEvents();
    const QVariant originalMemo = item->data(Qt::UserRole + 4);
    const QVariant originalCitation = item->data(Qt::UserRole + 5);
    const QVariant originalFavorite = item->data(Qt::UserRole + 2);
    const QRect paperCard = list->visualItemRect(item);
    const QRect cardImage = DocumentCard::thumbnailRect(paperCard);
    const QRect cardTitle = DocumentCard::titleRect(paperCard);
    const QRect cardCitation = DocumentCard::citationRect(paperCard);
    const QRect cardMemo = DocumentCard::memoRect(paperCard);
    // Native UI contract: IMAGE at the top, then full-width TITLE, CITATION,
    // MEMO below it. NOT a small thumbnail to the left of text.
    if (paperCard.height() != DocumentCard::kCardHeight ||
        cardImage.bottom() >= cardTitle.top() ||
        cardTitle.bottom() >= cardCitation.top() ||
        cardCitation.bottom() >= cardMemo.top() ||
        cardImage.left() != cardTitle.left() ||
        cardTitle.left() != cardCitation.left() ||
        cardCitation.left() != cardMemo.left() ||
        cardImage.width() != cardTitle.width() ||
        cardTitle.width() != cardCitation.width() ||
        cardCitation.width() != cardMemo.width())
        return fail("Stacked card image/title/citation/memo geometry regressed");
    listMode->click();
    QCoreApplication::processEvents();
    const QRect compactRow = list->visualItemRect(item);
    if (!listMode->isChecked() || cardMode->isChecked() ||
        compactRow.height() != DocumentCard::kCompactHeight ||
        !DocumentCard::compactMemoRect(compactRow).isValid())
        return fail("List view toggle did not activate 112px compact rows");
    cardMode->click();
    QCoreApplication::processEvents();
    if (!cardMode->isChecked() || listMode->isChecked() ||
        list->visualItemRect(item).height() != DocumentCard::kCardHeight ||
        item->data(Qt::UserRole + 4) != originalMemo ||
        item->data(Qt::UserRole + 5) != originalCitation ||
        item->data(Qt::UserRole + 2) != originalFavorite)
        return fail("List/card switching lost memo, bibliography or favorite data");
    if (originalCompact) {
        listMode->click();
        QCoreApplication::processEvents();
    }
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
    favoritesButton->click();
    auto* history = pages->currentWidget()->findChild<QListWidget*>();
    if (!history || history->count() != 1 || history->item(0)->data(Qt::UserRole).toString() != path)
        return fail("Favorite navigation did not use real saved data");
    folderButton->click();
    // selectFolder() clears its QListWidgetItem objects and repopulates
    // asynchronously. The old 'item' pointer is invalid after navigation,
    // and Ctrl+D must be triggered on a freshly selected real card.
    if (!wait([&] { return list->count() == 1 &&
        list->item(0)->data(Qt::UserRole).toString() == path; }))
        return fail("Folder navigation did not restore the actual PDF row");
    item = list->item(0);
    list->setCurrentItem(item);
    favorite->trigger();
    settings.sync();
    if (item->data(Qt::UserRole + 2).toBool() ||
        settings.value(QStringLiteral("library/favorites")).toStringList().contains(path))
        return fail("Favorite removal failed");
    recentButton->click();
    history = pages->currentWidget()->findChild<QListWidget*>();
    if (!history || history->count() != 1 || history->item(0)->data(Qt::UserRole).toString() != path)
        return fail("Successful PDF open not recorded in recent history");
    if (history->item(0)->data(Qt::UserRole + 3).toString() != QDate::currentDate().toString(QStringLiteral("yy.MM.dd.")))
        return fail("Successful open did not record the actual local-calendar date");
    folderButton->click();
    // The Recent -> Folder transition also rebuilds the list. Never reuse
    // a stale QListWidgetItem for the subsequent memo and crop checks.
    if (!wait([&] { return list->count() == 1 &&
        list->item(0)->data(Qt::UserRole).toString() == path; }))
        return fail("Recent-to-folder navigation did not restore the PDF row");
    item = list->item(0);
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
    auto* navigationToggle = window.findChild<QToolButton*>(QStringLiteral("LunaNavigationCollapseButton"));
    auto* documentsToggle = window.findChild<QToolButton*>(QStringLiteral("LunaDocumentsCollapseButton"));
    auto* restoreNav = window.findChild<QToolButton*>(QStringLiteral("LunaNavigationRestoreButton"));
    auto* restoreDocs = window.findChild<QToolButton*>(QStringLiteral("LunaDocumentsRestoreButton"));
    auto* navSlot = window.findChild<QWidget*>(QStringLiteral("LunaNavigationSlot"));
    auto* docsSlot = window.findChild<QWidget*>(QStringLiteral("LunaDocumentsSlot"));
    auto* navFold = window.findChild<QWidget*>(QStringLiteral("LunaNavigationFold"));
    auto* docsFold = window.findChild<QWidget*>(QStringLiteral("LunaDocumentsFold"));
    if (!navigationToggle || !documentsToggle || !restoreNav || !restoreDocs ||
        !navSlot || !docsSlot || !navFold || !docsFold ||
        !navigationToggle->isVisible() || !documentsToggle->isVisible() ||
        navigationToggle->parentWidget()->objectName() != QStringLiteral("LunaLibraryHeader") ||
        documentsToggle->parentWidget()->objectName() != QStringLiteral("LunaPaperHeader") ||
        restoreNav->parentWidget() != navFold || restoreDocs->parentWidget() != docsFold ||
        navFold->parentWidget() != navSlot || docsFold->parentWidget() != docsSlot ||
        navSlot->x() >= docsSlot->x())
        return fail("Independent folding controls not anchored to original panel slots");
    navigationToggle->click();
    QCoreApplication::processEvents();
    if (!navigation->isHidden() || documents->isHidden() || dock->isHidden() ||
        !restoreNav->isVisible() || restoreDocs->isVisible() ||
        navSlot->width() != 36 || navSlot->x() >= docsSlot->x())
        return fail("Collapsing navigation did not leave its 36px fold on the left");
    documentsToggle->click();
    QCoreApplication::processEvents();
    if (!navigation->isHidden() || !documents->isHidden() || dock->isHidden() ||
        !restoreNav->isVisible() || !restoreDocs->isVisible() ||
        navSlot->width() != 36 || docsSlot->width() != 36 ||
        dock->width() > 100 ||
        restoreNav->mapTo(dock, QPoint()).x() >= restoreDocs->mapTo(dock, QPoint()).x())
        return fail("Both folded panels did not remain in order beside the PDF");
    restoreNav->click();
    QCoreApplication::processEvents();
    if (navigation->isHidden() || !documents->isHidden() || dock->isHidden() ||
        !restoreDocs->isVisible() || restoreNav->isVisible() ||
        docsSlot->width() != 36 || navSlot->width() < 225 ||
        navSlot->x() >= docsSlot->x())
        return fail("Opening navigation moved the folded paper list or opened the wrong panel");
    restoreDocs->click();
    QCoreApplication::processEvents();
    if (dock->isHidden() || navigation->isHidden() || documents->isHidden() ||
        restoreNav->isVisible() || restoreDocs->isVisible() ||
        navSlot->x() >= docsSlot->x())
        return fail("Inline restore did not reopen both original panel slots");
    // Exercise the real viewport crop and asynchronous hover in the temporary
    // synthetic library. Nothing here reads or modifies a user's library.
    auto* library = dynamic_cast<LibraryPanel*>(dock->widget());
    if (!library) return fail("Library panel type missing");
    checkpoint("count-start");
    if (!wait([&] { return library->model_->countsAvailable(); }, 10000))
        return fail("Asynchronous folder PDF counts were not delivered");
    const QString countRoot = library->root_;
    QString childFolderPath;
    // Keep the counted directory alive until ALL UI smoke checks finish.
    // QFileSystemModel may still be watching a selected child; deleting that
    // directory mid-test races native QFileSystemWatcher notifications.
    QTemporaryDir subfolder(QDir(countRoot).filePath(QStringLiteral("luna-count-XXXXXX")));
    const int baselineCount = library->model_->pdfCount(countRoot);
    if (baselineCount < 1) return fail("Root folder PDF count excluded its local document");
    {
        if (!subfolder.isValid() ||
            !QFile::copy(path, QDir(subfolder.path()).filePath(QStringLiteral("nested.pdf"))))
            return fail("Nested PDF count fixture could not be created");
        childFolderPath = subfolder.path();
        checkpoint("count-child");
        library->model_->refreshPdfCounts(countRoot);
        if (!wait([&] { return library->model_->countsAvailable() &&
            library->model_->pdfCount(countRoot) == baselineCount &&
            library->model_->pdfCount(subfolder.path()) == 1; }, 10000))
            return fail("Child PDF changed parent direct count or missed child count");
    }
    // Delete only the synthetic PDF. Keep its directory watchable until
    // QTemporaryDir is destroyed after the complete smoke has returned.
    if (!QFile::remove(QDir(subfolder.path()).filePath(QStringLiteral("nested.pdf"))))
        return fail("Synthetic nested PDF could not be removed");
    checkpoint("count-deleted");
    library->model_->refreshPdfCounts(countRoot);
    if (!wait([&] { return library->model_->countsAvailable() &&
        library->model_->pdfCount(countRoot) == baselineCount &&
        library->model_->pdfCount(childFolderPath) == 0; }, 10000))
        return fail("Folder count did not recover after child PDF removal");
    checkpoint("count-recovered");
    if (!wait([&] { return window.isEnabled() && window.getProgramController()->canClose(); }))
        return fail("Reader busy before crop verification");
    checkpoint("preview-enter");
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
    checkpoint("crop-enter");
    const QFileInfo pdf(path);
    const QString thumbnail = SidecarStorage::thumbnail(path, QStringLiteral(".png"));
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
    // Real Windows filesystem contract: copying must create BOTH physical
    // files, refuse a second same-name copy and never delete the source.
    QTemporaryDir copied; // Keep generated copies outside the library's recursive scanner.
    if (!copied.isValid()) return fail("Cannot create physical PDF copy test directory");
    const auto copiedFiles = copyPdfWithThumbnails(path, copied.path());
    const QString copiedThumb = SidecarStorage::thumbnail(copiedFiles.documentPath, QStringLiteral(".png"));
    if (!copiedFiles.success || !QFileInfo::exists(copiedFiles.documentPath) ||
        !QFileInfo::exists(copiedThumb) ||
        QFileInfo(copiedFiles.documentPath).size() != pdf.size() ||
        QFileInfo(copiedThumb).size() != QFileInfo(thumbnail).size())
        return fail("Physical PDF/thumbnail copy failed");
    if (copyPdfWithThumbnails(path, copied.path()).success ||
        !QFileInfo::exists(path) || !QFileInfo::exists(thumbnail))
        return fail("Copy overwrote a target or damaged its original");
    const QString before = QDir(copied.path()).filePath(QStringLiteral("before-rename"));
    const QString after = QDir(copied.path()).filePath(QStringLiteral("after-rename"));
    if (!QDir().mkdir(before) || !copyPdfWithThumbnails(path, before).success ||
        !renameWithoutReplacement(before, after) || QFileInfo::exists(before) ||
        !QFileInfo::exists(QDir(after).filePath(pdf.fileName())) ||
        !QFileInfo::exists(SidecarStorage::thumbnail(
             QDir(after).filePath(pdf.fileName()), QStringLiteral(".png"))))
        return fail("Windows physical folder rename did not preserve PDF and thumbnail");
    if (renameWithoutReplacement(copied.path(), after))
        return fail("Folder rename overwrote an existing destination");
    auto* folderDrop = window.findChild<QTreeView*>(QStringLiteral("LunaFolderDropTree"));
    if (!folderDrop || !folderDrop->acceptDrops())
        return fail("Native directory drop target missing");
    if (!list->dragEnabled() || list->dragDropMode() != QAbstractItemView::DragOnly)
        return fail("Paper-list file drag source missing");
    checkpoint("folder-icons-enter");
    // Verify all twenty approved SVG resources and portable metadata.
    // A selection stores only iconId, and never writes into the PDF.
    QString iconError;
    const QString iconFolder = pdf.absolutePath();
    const QString metadataPath = folderMetadataPath(iconFolder);
    if (QFileInfo::exists(metadataPath)) return fail("Synthetic folder metadata pre-existed");
    const QModelIndex iconIndex = library->model_->index(iconFolder);
    if (!iconIndex.isValid()) return fail("Synthetic folder icon index missing");
    QString stableId;
    for (const auto& preset : kFolderIconPresets) {
        const QString expected = QString::fromLatin1(preset.id);
        if (!library->saveFolderIcon(iconFolder, expected, &iconError)) {
            error = QStringLiteral("Folder icon save failed: ") + iconError; return false;
        }
        const QJsonObject saved = readFolderMetadata(iconFolder);
        if (saved.value(QStringLiteral("iconId")).toString() != expected ||
            saved.value(QStringLiteral("schemaVersion")).toInt() != 1 ||
            !saved.contains(QStringLiteral("isImportant")) ||
            !saved.contains(QStringLiteral("updatedAt")) ||
            !saved.contains(QStringLiteral("relativePath")) ||
            QUuid(saved.value(QStringLiteral("folderId")).toString()).isNull())
            return fail("Selected folder icon metadata corrupt or incomplete");
        if (!stableId.isEmpty() && stableId != saved.value(QStringLiteral("folderId")).toString())
            return fail("Folder UUID changed while switching icon preset");
        stableId = saved.value(QStringLiteral("folderId")).toString();
        const QIcon rendered = qvariant_cast<QIcon>(
            library->model_->data(iconIndex, Qt::DecorationRole));
        if (rendered.isNull() || rendered.pixmap(QSize(20,20)).isNull())
            return fail("Approved SVG folder icon did not render at 20px");
    }
    // Simulate MYBOX delivering the same JSON to a differently rooted folder.
    // No absolute icon path should be needed on the second machine.
    const QString secondFolder = QDir(iconFolder).filePath(QStringLiteral("Synthetic examples"));
    const QString secondMetadata = folderMetadataPath(secondFolder);
    if (QFileInfo::exists(secondMetadata) || !QFile::copy(metadataPath, secondMetadata) ||
        folderIconId(secondFolder) != QStringLiteral("temporary"))
        return fail("Synced folder metadata did not resolve iconId on another path");
    if (!QFile::remove(secondMetadata)) return fail("Could not clean synthetic MYBOX fixture");
    if (!library->resetFolderIcon(iconFolder, &iconError) ||
        folderIconId(iconFolder) != QStringLiteral("general") ||
        readFolderMetadata(iconFolder).value(QStringLiteral("folderId")).toString() != stableId) {
        error = QStringLiteral("Folder icon reset corrupted metadata: ") + iconError;
        return false;
    }
    if (!QFileInfo::exists(thumbnail) || !QFileInfo::exists(path))
        return fail("Folder icon reset altered PDF or thumbnail files");
    // Paper-card thumbnails are sufficient: no secondary hover popup.
    checkpoint("hover-enter");
    const QPoint card = list->visualItemRect(item).center();
    mouse(list->viewport(), QEvent::MouseMove, card, Qt::NoButton, Qt::NoButton);
    QCoreApplication::processEvents();
    if (library->thumbnailPopup_ && library->thumbnailPopup_->isVisible())
        return fail("Removed paper hover preview unexpectedly appeared");
    // No click-to-edit: an incidental click on the '+ 메모' line must
    // never open a QLineEdit. Explicit editItem is the same method invoked
    // by the right-click "메모 편집…" action.
    checkpoint("memo-enter");
    const QPoint memoPoint = DocumentCard::memoRect(list->visualItemRect(item)).center();
    mouse(list->viewport(), QEvent::MouseButtonPress, memoPoint, Qt::LeftButton, Qt::LeftButton);
    mouse(list->viewport(), QEvent::MouseButtonRelease, memoPoint, Qt::LeftButton, Qt::NoButton);
    if (list->findChild<QLineEdit*>(QStringLiteral("LunaPaperMemoEditor")))
        return fail("Accidental left click opened paper memo editor");
    list->editItem(item);
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
    const QString movedThumbnail = SidecarStorage::thumbnail(movedPath, QStringLiteral(".png"));
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
        checkpoint("1000");
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
        QCoreApplication::processEvents();
        const int preservedScroll = list->verticalScrollBar()->value();
        if (library->localSync_ && library->localSync_->metadataChanged)
            library->localSync_->metadataChanged(list->item(500)->data(Qt::UserRole).toString());
        QCoreApplication::processEvents();
        if (preservedScroll == 0 || list->verticalScrollBar()->value() != preservedScroll)
            return fail("Sidecar metadata refresh reset 1000-PDF list scroll");
        if (!wait([&] { return beats >= 3; })) return fail("Library loading starved GUI events");
        heartbeat.stop();
        if (longestGap > 1000) return fail("Library GUI event gap exceeded one second");
        if (QFileInfo(window.getProgramController()->getOriginalFileName()).canonicalFilePath() != QFileInfo(path).canonicalFilePath())
            return fail("Library discovery opened an unrelated PDF");
        qInfo() << "LUNA synthetic 1000-PDF library: load ms" << loadMs
                << "search ms" << searchMs << "maximum GUI timer gap ms" << longestGap;
        // Parent lists direct PDFs only; All Documents still scans descendants.
        checkpoint("scan-parent-direct");
        library->selectFolder(pdf.absolutePath());
        if (!wait([&] { return list->count() == 1 &&
            !library->folderTimer_->isActive(); }, 15000))
            return fail("Parent folder included child PDFs");
        library->scanAll();
        if (!wait([&] { return library->allPdfs_->count() == 1001 &&
            !library->allTimer_->isActive(); }, 15000))
            return fail("All Documents missed nested 1000 PDFs");
        bool foundRootDocument = false;
        for (int i = 0; i < list->count(); ++i)
            if (list->item(i)->data(Qt::UserRole).toString() == path) {
                foundRootDocument = true;
                break;
            }
        if (!foundRootDocument)
            return fail("Parent folder omitted its own local PDF");
    }
    // After the temporary child directory is deleted, a fresh scan must
    // return precisely the parent's single local PDF.
    checkpoint("clean");
    library->selectFolder(pdf.absolutePath());
    if (!wait([&] { return list->count() == 1 &&
        !library->folderTimer_->isActive() &&
        list->item(0)->data(Qt::UserRole).toString() == path; }, 15000))
        return fail("Library did not recover after nested 1000-PDF directory removal");
    item = list->item(0);
    // Drive the same signal/slot used by double-click, including the save guard.
    window.getProgramController()->closeDocument();
    list->itemDoubleClicked(item);
    // Last destructive list-resetting test. All earlier hover/memo checks hold
    // QListWidgetItem* pointers and must finish BEFORE changing folders.
    QTemporaryDir selectionFolder(pdf.absolutePath() + QStringLiteral("/luna-selection-XXXXXX"));
    if (!selectionFolder.isValid() ||
        !QFile::copy(path, QDir(selectionFolder.path()).filePath(QStringLiteral("child.pdf"))))
        return fail("Child selection fixture unavailable");
    library->selectFolder(selectionFolder.path());
    if (!wait([&] { return list->count() == 1 &&
        !library->folderTimer_->isActive(); }, 10000) ||
        library->folderTab_->isChecked() ||
        !library->tree_->selectionModel()->isSelected(library->model_->index(selectionFolder.path())))
        return fail("Child selection left root toggled or wrong PDF list");
    library->selectFolder(pdf.absolutePath());
    if (!wait([&] { return list->count() == 1 &&
        !library->folderTimer_->isActive(); }, 10000) ||
        !library->folderTab_->isChecked() ||
        !library->tree_->selectionModel()->selectedIndexes().isEmpty())
        return fail("Root shortcut did not clear child-folder highlight");
    // No previously captured QListWidgetItem* is referenced after this point.
    checkpoint("ui-done");
    return true;
}
} // namespace luna
