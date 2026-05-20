#include "ScanStateIndicator.h"

#include <QEnterEvent>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

namespace {

constexpr int kPillHeight   = 22;
constexpr int kPillRadius   = 11;
constexpr int kPillPadH     = 10;

constexpr int kDotDiameter  = 10;
constexpr int kDotPadding   = 4;  // margins around the dot when at rest

// Apple system colors at ~80 % alpha so they coexist with the brand purple.
QColor idleBgIdle()       { return QColor(0, 0, 0, 200); }     // near-black pill
QColor idleBgHover()      { return QColor(28, 28, 30, 230); }  // slightly lighter
QColor idleFg()           { return QColor(255, 255, 255, 235); }

QColor scanningBg()       { return QColor(0xFF, 0x9F, 0x0A, 38);  }   // ~15 % alpha
QColor scanningBgHover()  { return QColor(0xFF, 0x9F, 0x0A, 60);  }
QColor scanningFg()       { return QColor(0xC0, 0x70, 0x00, 255); }   // darker amber

QColor scannedBg()        { return QColor(0x34, 0xC7, 0x59, 38);  }
QColor scannedBgHover()   { return QColor(0x34, 0xC7, 0x59, 60);  }
QColor scannedFg()        { return QColor(0x1F, 0x7A, 0x35, 255); }

QColor dotFor(ScanStateIndicator::State s)
{
    using S = ScanStateIndicator::State;
    switch (s) {
        case S::Idle:     return QColor(0, 0, 0, 130);
        case S::Scanning: return QColor(0xFF, 0x9F, 0x0A);
        case S::Scanned:  return QColor(0x34, 0xC7, 0x59);
    }
    return Qt::transparent;
}

}  // anonymous

ScanStateIndicator::ScanStateIndicator(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover, true);
    updateTooltip();
}

void ScanStateIndicator::setState(State newState)
{
    if (m_state == newState) return;
    m_state = newState;
    setCursor(m_state == State::Idle ? Qt::PointingHandCursor : Qt::ArrowCursor);
    updateTooltip();
    updateGeometry();
    update();
}

void ScanStateIndicator::setCompact(bool compact)
{
    if (m_compact == compact) return;
    m_compact = compact;
    updateGeometry();
    update();
}

void ScanStateIndicator::setRepresentedPath(const QString &path)
{
    if (m_path == path) return;
    m_path = path;
    updateTooltip();
}

QString ScanStateIndicator::labelForState() const
{
    switch (m_state) {
        case State::Idle:     return tr("Scan now");
        case State::Scanning: return tr("Scanning…");
        case State::Scanned:  return tr("Scanned");
    }
    return {};
}

void ScanStateIndicator::updateTooltip()
{
    switch (m_state) {
        case State::Idle:
            setToolTip(tr("Add %1 to the search index").arg(m_path.isEmpty() ? tr("this folder") : m_path));
            break;
        case State::Scanning:
            setToolTip(tr("Scanning %1…").arg(m_path.isEmpty() ? tr("this folder") : m_path));
            break;
        case State::Scanned:
            setToolTip(tr("%1 is in the search index").arg(m_path.isEmpty() ? tr("This folder") : m_path));
            break;
    }
}

QSize ScanStateIndicator::sizeHint() const
{
    QFont f = font();
    f.setPointSize(qMax(8, f.pointSize() - 2));
    f.setWeight(QFont::Medium);
    QFontMetrics fm(f);
    const int labelW = fm.horizontalAdvance(labelForState());
    const int pillW = labelW + 2 * kPillPadH;

    if (m_compact && !m_hovered) {
        return { kDotDiameter + 2 * kDotPadding, kPillHeight };
    }
    return { pillW, kPillHeight };
}

QSize ScanStateIndicator::minimumSizeHint() const { return sizeHint(); }

void ScanStateIndicator::enterEvent(QEnterEvent *event)
{
    m_hovered = true;
    if (m_compact) updateGeometry();
    update();
    QWidget::enterEvent(event);
}

void ScanStateIndicator::leaveEvent(QEvent *event)
{
    m_hovered = false;
    if (m_compact) updateGeometry();
    update();
    QWidget::leaveEvent(event);
}

void ScanStateIndicator::mousePressEvent(QMouseEvent *event)
{
    if (m_state == State::Idle && event->button() == Qt::LeftButton) {
        emit scanRequested(m_path);
    }
    QWidget::mousePressEvent(event);
}

void ScanStateIndicator::paintEvent(QPaintEvent * /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    const bool drawDot = m_compact && !m_hovered;

    if (drawDot) {
        // Mini dot — centered, 10 px, color = state.
        const int cx = width() / 2;
        const int cy = height() / 2;
        p.setPen(Qt::NoPen);
        p.setBrush(dotFor(m_state));
        p.drawEllipse(QPoint(cx, cy), kDotDiameter / 2, kDotDiameter / 2);
        return;
    }

    // Full pill — state-dependent bg/fg, with hover variant.
    QColor bg, fg;
    switch (m_state) {
        case State::Idle:
            bg = m_hovered ? idleBgHover() : idleBgIdle();
            fg = idleFg();
            break;
        case State::Scanning:
            bg = m_hovered ? scanningBgHover() : scanningBg();
            fg = scanningFg();
            break;
        case State::Scanned:
            bg = m_hovered ? scannedBgHover() : scannedBg();
            fg = scannedFg();
            break;
    }

    const QRect r = rect().adjusted(0, (height() - kPillHeight) / 2,
                                    0, -(height() - kPillHeight) / 2);
    QPainterPath path;
    path.addRoundedRect(r, kPillRadius, kPillRadius);
    p.fillPath(path, bg);

    QFont f = font();
    f.setPointSize(qMax(8, f.pointSize() - 2));
    f.setWeight(QFont::Medium);
    p.setFont(f);
    p.setPen(fg);
    p.drawText(r, Qt::AlignCenter, labelForState());
}
