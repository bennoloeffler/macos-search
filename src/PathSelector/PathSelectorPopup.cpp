#include "PathSelectorPopup.h"
#include "SwiftUIStyle.h"
#include <QListWidget>
#include <QLineEdit>
#include <QPropertyAnimation>
#include <QVBoxLayout>
#include <QEvent>
#include <QKeyEvent>
#include <QFileInfo>
#include <QEasingCurve>
#include <QApplication>
#include <QScreen>
#include <QTimer>

PathSelectorPopup::PathSelectorPopup(QWidget *anchor, QWidget *parent)
    : QWidget(parent)  // Changed: No longer a popup window, just a regular widget in layout
    , m_anchor(anchor)
    , m_list(nullptr)
    , m_animation(nullptr)
{
    setupUi();
    
    // Initially hidden - will be shown when there are completions
    setVisible(false);
}

PathSelectorPopup::~PathSelectorPopup()
{
    if (m_anchor) {
        m_anchor->removeEventFilter(this);
    }
}

void PathSelectorPopup::setupUi()
{
    // Changed: Regular widget, no popup attributes needed
    setFocusPolicy(Qt::NoFocus);  // CRITICAL: Should never receive focus

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_list = new QListWidget(this);
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setFocusPolicy(Qt::NoFocus);  // CRITICAL: List should never receive focus

    // Style the list
    m_list->setStyleSheet(QString(
        "QListWidget {"
        "  background-color: palette(window);"
        "  border: 1px solid %1;"
        "  border-radius: %2px;"
        "}"
        "QListWidget::item {"
        "  padding: 4px 8px;"
        "  border: none;"
        "}"
        "QListWidget::item:selected {"
        "  background-color: rgba(0, 122, 255, 0.15);"
        "  color: palette(highlighted-text);"
        "}"
        "QListWidget::item:hover {"
        "  background-color: %3;"
        "}"
    ).arg(SwiftUIStyle::subtleBorder())
     .arg(SwiftUIStyle::CornerRadiusCard)
     .arg(SwiftUIStyle::chipBackground()));

    layout->addWidget(m_list);

    // Connect signals
    connect(m_list, &QListWidget::itemClicked,
            this, &PathSelectorPopup::onItemClicked);
    connect(m_list, &QListWidget::itemDoubleClicked,
            this, &PathSelectorPopup::onItemDoubleClicked);

    // Setup animation
    m_animation = new QPropertyAnimation(this, "popupHeight", this);
    m_animation->setDuration(AnimationDuration);
    m_animation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_animation, &QPropertyAnimation::finished,
            this, &PathSelectorPopup::onAnimationFinished);
}

void PathSelectorPopup::setItems(const QStringList &items)
{
    m_list->clear();

    for (const QString &path : items) {
        auto *item = new QListWidgetItem(displayText(path));
        item->setData(Qt::UserRole, path);
        item->setToolTip(path);
        m_list->addItem(item);
    }
    
    // Update height when items change - always maintain FixedVisibleItems height
    if (isVisible() && !isAnimating()) {
        // If already visible and not animating, update height immediately
        int targetHeight = calculateHeight();
        setFixedHeight(targetHeight);
        if (qEnvironmentVariableIsSet("MAUDE_DEBUG")) {
            qInfo() << "[PathSelectorPopup] Updated height to" << targetHeight 
                    << "for" << items.size() << "items";
        }
    }
}

void PathSelectorPopup::setSelectedIndex(int index)
{
    if (index < 0 || index >= m_list->count()) {
        m_list->setCurrentRow(-1);
        m_list->clearSelection();
        return;
    }

    m_list->setCurrentRow(index);
    m_list->scrollToItem(m_list->item(index));
}

int PathSelectorPopup::selectedIndex() const
{
    return m_list->currentRow();
}

void PathSelectorPopup::showAnimated()
{
    if (m_animation->state() == QAbstractAnimation::Running) {
        m_animation->stop();
    }

    updatePosition();

    int targetHeight = calculateHeight();
    m_isShowing = true;

    // Start from height 0
    setFixedHeight(0);
    setVisible(true);  // Changed: use setVisible instead of show()
    
    // CRITICAL: Ensure anchor (line edit) keeps focus - popup should NEVER receive focus
    if (m_anchor) {
        m_anchor->setFocus(Qt::OtherFocusReason);
    }

    m_animation->setStartValue(0);
    m_animation->setEndValue(targetHeight);
    m_animation->start();
    
    if (qEnvironmentVariableIsSet("MAUDE_DEBUG")) {
        qInfo() << "[PathSelectorPopup] Showing with height" << targetHeight 
                << "for" << m_list->count() << "items (always shows space for" 
                << FixedVisibleItems << "items)";
    }
}

void PathSelectorPopup::hideAnimated()
{
    if (m_animation->state() == QAbstractAnimation::Running) {
        m_animation->stop();
    }

    m_isShowing = false;

    m_animation->setStartValue(height());
    m_animation->setEndValue(0);
    m_animation->start();
}

bool PathSelectorPopup::isAnimating() const
{
    return m_animation->state() == QAbstractAnimation::Running;
}

void PathSelectorPopup::updatePosition()
{
    // Changed: No longer needs positioning - it's in the layout
    // Just match the width of the anchor if available
    if (m_anchor) {
        setFixedWidth(m_anchor->width());
    }
}

bool PathSelectorPopup::eventFilter(QObject *watched, QEvent *event)
{
    // Changed: No longer need to track anchor movement since we're in layout
    // All key events are handled by PathSelectorUI's eventFilter
    Q_UNUSED(watched)
    Q_UNUSED(event)
    return false;
}

void PathSelectorPopup::onItemClicked(QListWidgetItem *item)
{
    int index = m_list->row(item);
    emit itemSelected(index);
}

void PathSelectorPopup::onItemDoubleClicked(QListWidgetItem *item)
{
    int index = m_list->row(item);
    emit itemActivated(index);
}

void PathSelectorPopup::onAnimationFinished()
{
    if (!m_isShowing) {
        setVisible(false);  // Changed: use setVisible instead of hide()
    }
}

void PathSelectorPopup::setPopupHeight(int h)
{
    setFixedHeight(h);
}

int PathSelectorPopup::calculateHeight() const
{
    // Always maintain height for FixedVisibleItems (5) items, regardless of actual count
    // This prevents the list from shrinking when items decrease
    return (FixedVisibleItems * ItemHeight) + 2;  // 5 items + border
}

QString PathSelectorPopup::displayText(const QString &fullPath) const
{
    // Show just the folder name, not the full path
    return QFileInfo(fullPath).fileName();
}

void PathSelectorPopup::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Up:
        emit navigationRequested(-1);
        event->accept();
        return;

    case Qt::Key_Down:
        emit navigationRequested(1);
        event->accept();
        return;

    case Qt::Key_Tab:
    case Qt::Key_Backtab:
        emit tabPressed();
        event->accept();
        return;

    case Qt::Key_Return:
    case Qt::Key_Enter:
        emit returnPressed();
        event->accept();
        return;

    case Qt::Key_Escape:
        emit escapePressed();
        event->accept();
        return;

    default:
        break;
    }

    QWidget::keyPressEvent(event);
}
