#ifndef SCANSTATEINDICATOR_H
#define SCANSTATEINDICATOR_H

#include <QWidget>

class QPropertyAnimation;

// Tri-state scan-status pill / dot.
//
// States:
//   Idle      — actionable; click emits scanRequested()
//   Scanning  — disabled; subdued amber pulse
//   Scanned   — disabled; subdued green check
//
// Form factors (setCompact):
//   false (default) — full pill with label ~22 px tall, used inside the
//                     Search-in field.
//   true            — 10 px circular dot at rest, hover-expanded to a tiny
//                     labeled pill. Used per-favorite in the sidebar.
//
// State is derived externally and pushed in via setState() — the widget
// itself never queries PathCacheManager.
class ScanStateIndicator : public QWidget
{
    Q_OBJECT

public:
    enum class State {
        Idle = 0,      // not scanned, actionable
        Scanning = 1,  // in progress
        Scanned = 2,   // complete
    };

    explicit ScanStateIndicator(QWidget *parent = nullptr);

    State state() const { return m_state; }
    void setState(State newState);

    bool isCompact() const { return m_compact; }
    void setCompact(bool compact);

    // The path this indicator represents — used only for its own tooltip text
    // and emitted alongside scanRequested(). The widget is path-agnostic
    // otherwise; the owner decides how state and path relate.
    void setRepresentedPath(const QString &path);
    QString representedPath() const { return m_path; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    /// Emitted when the user clicks the indicator while in the Idle state.
    /// The `path` is the one passed via setRepresentedPath(), so a single
    /// slot can dispatch by source widget without bookkeeping.
    void scanRequested(const QString &path);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    QString labelForState() const;
    void updateTooltip();

    State m_state = State::Idle;
    bool m_compact = false;
    bool m_hovered = false;
    QString m_path;
};

#endif // SCANSTATEINDICATOR_H
