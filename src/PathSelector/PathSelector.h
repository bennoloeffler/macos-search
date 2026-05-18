#ifndef PATHSELECTOR_H
#define PATHSELECTOR_H

#include <QWidget>

class FileSystemAdapter;
class PathSelectorState;
class PathSelectorUI;

/**
 * @brief Path selector widget with completion support.
 *
 * A Finder-like path completion field that provides:
 * - Tab/arrow navigation through completions
 * - Contains-match filtering (typing "s" matches "Documents")
 * - Visual state feedback (bold/grey/red text)
 * - Animated popup dropdown
 *
 * This is the public facade that hides the internal implementation.
 *
 * Usage:
 * @code
 * auto *selector = new PathSelector(this);
 * selector->setPath("/Users/benno");
 * connect(selector, &PathSelector::pathChanged, this, &MyWidget::onPathChanged);
 * @endcode
 */
class PathSelector : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged)

public:
    explicit PathSelector(QWidget *parent = nullptr);

    /**
     * @brief Constructor with custom filesystem adapter (for testing).
     * @param fs The filesystem adapter (takes ownership).
     * @param parent Parent widget.
     */
    explicit PathSelector(FileSystemAdapter *fs, QWidget *parent = nullptr);

    ~PathSelector() override;

    /**
     * @brief Get the current valid path.
     * @return The last valid path that was set or accepted.
     */
    QString path() const;

    /**
     * @brief Set the current path.
     * @param path The path to set (must be a valid directory).
     *
     * If the path is not a valid directory, this call is ignored.
     */
    void setPath(const QString &path);

    /**
     * @brief Focus the path input field.
     *
     * Selects all text in the field for easy replacement.
     */
    void focusPathField();

    // Internal accessors for greybox testing
    PathSelectorState *state() const { return m_state; }
    PathSelectorUI *ui() const { return m_ui; }
    FileSystemAdapter *fileSystemAdapter() const { return m_fs; }

signals:
    /**
     * @brief Emitted when a valid path is accepted.
     * @param validPath The accepted path.
     */
    void pathChanged(const QString &validPath);

    /**
     * @brief Emitted when the user cancels (ESC).
     *
     * The path reverts to the last valid value.
     */
    void pathCancelled();

    /**
     * @brief Emitted when Tab should move focus to next widget.
     *
     * This happens when the path is complete and no popup is shown.
     * Connect to this to handle focus traversal in your container.
     */
    void focusTraversalRequested();

private:
    void init(FileSystemAdapter *fs);
    void setupConnections();

    FileSystemAdapter *m_fs;
    PathSelectorState *m_state;
    PathSelectorUI *m_ui;
    bool m_ownsFs;
};

#endif // PATHSELECTOR_H
