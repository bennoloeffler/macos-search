#ifndef PATHSELECTORPOPUP_H
#define PATHSELECTORPOPUP_H

#include <QWidget>
#include <QPointer>

class QKeyEvent;
class QListWidget;
class QListWidgetItem;
class QPropertyAnimation;

/**
 * @brief Animated dropdown popup for path completions.
 *
 * Shows a list of completion options below the anchor widget.
 * Supports smooth show/hide animations using OutCubic easing.
 */
class PathSelectorPopup : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(int popupHeight READ popupHeight WRITE setPopupHeight)

public:
    explicit PathSelectorPopup(QWidget *anchor, QWidget *parent = nullptr);
    ~PathSelectorPopup() override;

    /**
     * @brief Set the list of items to display.
     * @param items Full paths to show as completions.
     */
    void setItems(const QStringList &items);

    /**
     * @brief Set the selected (highlighted) item index.
     * @param index The index to select, or -1 for no selection.
     */
    void setSelectedIndex(int index);

    /**
     * @brief Get the currently selected index.
     * @return The selected index, or -1 if none.
     */
    int selectedIndex() const;

    /**
     * @brief Show the popup with animation.
     */
    void showAnimated();

    /**
     * @brief Hide the popup with animation.
     */
    void hideAnimated();

    /**
     * @brief Check if an animation is currently running.
     * @return true if animating.
     */
    bool isAnimating() const;

    /**
     * @brief Update the popup position relative to anchor.
     */
    void updatePosition();


    // Animation constants
    static constexpr int AnimationDuration = 150;
    static constexpr int MaxVisibleItems = 8;
    static constexpr int ItemHeight = 24;
    static constexpr int FixedVisibleItems = 5;  // Always show space for 5 items

signals:
    /**
     * @brief Emitted when an item is clicked (single click).
     * @param index The clicked item index.
     */
    void itemSelected(int index);

    /**
     * @brief Emitted when an item is activated (double-click or Enter).
     * @param index The activated item index.
     */
    void itemActivated(int index);

    /**
     * @brief Emitted when navigation key pressed (Up/Down).
     * @param delta -1 for up, +1 for down.
     */
    void navigationRequested(int delta);

    /**
     * @brief Emitted when Tab key pressed (cycle/accept).
     */
    void tabPressed();

    /**
     * @brief Emitted when Return key pressed (accept).
     */
    void returnPressed();

    /**
     * @brief Emitted when Escape key pressed (cancel).
     */
    void escapePressed();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void onItemClicked(QListWidgetItem *item);
    void onItemDoubleClicked(QListWidgetItem *item);
    void onAnimationFinished();

private:
    void setupUi();
    int popupHeight() const { return height(); }
    void setPopupHeight(int h);
    int calculateHeight() const;
    QString displayText(const QString &fullPath) const;

    QPointer<QWidget> m_anchor;
    QListWidget *m_list;
    QPropertyAnimation *m_animation;
    bool m_isShowing = false;
};

#endif // PATHSELECTORPOPUP_H
