#ifndef PATHSELECTORKEYHANDLER_H
#define PATHSELECTORKEYHANDLER_H

#include <QObject>

class QKeyEvent;
class PathSelectorState;

/**
 * @brief Single dispatch table for path selector keyboard events.
 *
 * Consolidates all keyboard handling into one class with one handleKeyPress()
 * method. Eliminates the duplicated keyboard logic in the original
 * FolderBrowserDialog.
 *
 * Key behaviors:
 * - Tab: Focus traversal (complete), accept (single), cycle (multiple)
 * - Return: Accept selection
 * - Escape: Revert to last valid path
 * - Arrow Up/Down: Navigate completions
 * - Slash: Append and show popup
 */
class PathSelectorKeyHandler : public QObject
{
    Q_OBJECT

public:
    explicit PathSelectorKeyHandler(PathSelectorState *state,
                                    QObject *parent = nullptr);
    ~PathSelectorKeyHandler() override = default;

    /**
     * @brief Handle a key press event.
     * @param event The key event to handle.
     * @param popupVisible Whether the completion popup is currently visible.
     * @return true if the event was handled and should not propagate.
     */
    bool handleKeyPress(QKeyEvent *event, bool popupVisible);

signals:
    /**
     * @brief Emitted when the popup should be shown.
     */
    void showPopupRequested();

    /**
     * @brief Emitted when the popup should be hidden.
     */
    void hidePopupRequested();

    /**
     * @brief Emitted when Tab should traverse to next widget.
     *
     * This happens when the path is complete and no popup is visible.
     */
    void focusTraversalRequested();

    /**
     * @brief Emitted when a slash was appended to the path.
     * @param newText The text with appended slash.
     */
    void slashAppended(const QString &newText);

private:
    bool handleTab(bool popupVisible);
    bool handleReturn(bool popupVisible);
    bool handleEscape(bool popupVisible);
    bool handleArrowDown(bool popupVisible);
    bool handleArrowUp(bool popupVisible);
    bool handleSlash();

    PathSelectorState *m_state;
};

#endif // PATHSELECTORKEYHANDLER_H
