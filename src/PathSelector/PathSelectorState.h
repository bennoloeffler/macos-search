#ifndef PATHSELECTORSTATE_H
#define PATHSELECTORSTATE_H

#include <QObject>
#include <QString>
#include <QStringList>

class FileSystemAdapter;

/**
 * @brief State machine for path selector component.
 *
 * Manages path validation state and completions following the
 * state-driven UI pattern from docs/300_qt_swift_ui.md.
 *
 * States:
 * - Complete: Valid directory path (no trailing slash)
 * - Browsing: Valid with trailing slash, browsing subdirectories
 * - PartialMultiple: Partial match with multiple completions
 * - PartialSingle: Partial match with single completion (auto-completable)
 * - Invalid: No matches found
 */
class PathSelectorState : public QObject
{
    Q_OBJECT
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString currentText READ currentText WRITE setCurrentText NOTIFY currentTextChanged)
    Q_PROPERTY(QString lastValidPath READ lastValidPath NOTIFY lastValidPathChanged)
    Q_PROPERTY(QStringList completions READ completions NOTIFY completionsChanged)
    Q_PROPERTY(int selectedIndex READ selectedIndex WRITE setSelectedIndex NOTIFY selectedIndexChanged)

public:
    enum class State {
        Complete,        // Valid directory path
        Browsing,        // Valid with trailing slash, browsing subdirs
        PartialMultiple, // Partial match, multiple completions
        PartialSingle,   // Partial match, single completion
        Invalid          // No matches
    };
    Q_ENUM(State)

    explicit PathSelectorState(FileSystemAdapter *fs, QObject *parent = nullptr);
    ~PathSelectorState() override = default;

    // State accessors
    State state() const { return m_state; }
    QString currentText() const { return m_currentText; }
    QString lastValidPath() const { return m_lastValidPath; }
    QStringList completions() const { return m_completions; }
    int selectedIndex() const { return m_selectedIndex; }

    // Check if popup should be visible based on state
    bool shouldShowPopup() const;

    // Accessor for filesystem adapter
    FileSystemAdapter *fileSystemAdapter() const { return m_fs; }

    // Actions
    void setCurrentText(const QString &text);
    void setSelectedIndex(int index);

    /**
     * @brief Accept the current selection (Return/Tab on single).
     *
     * If a completion is selected, updates currentText to that path.
     * Updates lastValidPath and transitions to Complete state.
     * Emits pathAccepted signal.
     */
    void acceptSelection();

    /**
     * @brief Revert to last valid path (ESC pressed).
     *
     * Restores currentText to lastValidPath.
     * Transitions to Complete state.
     * Emits pathReverted signal.
     */
    void revert();

    /**
     * @brief Cycle selection in completions list.
     * @param delta +1 for next, -1 for previous.
     *
     * Wraps around at boundaries.
     */
    void cycleSelection(int delta);

    /**
     * @brief Initialize with a valid path.
     * @param path The initial path (should be a valid directory).
     */
    void initialize(const QString &path);

signals:
    void stateChanged(PathSelectorState::State state);
    void currentTextChanged(const QString &text);
    void lastValidPathChanged(const QString &path);
    void completionsChanged(const QStringList &completions);
    void selectedIndexChanged(int index);
    void pathAccepted(const QString &path);
    void pathReverted(const QString &path);

private:
    void updateState();
    void setState(State newState);
    void setCompletions(const QStringList &completions);
    void setLastValidPath(const QString &path);

    FileSystemAdapter *m_fs;
    State m_state = State::Complete;
    QString m_currentText;
    QString m_lastValidPath;
    QStringList m_completions;
    int m_selectedIndex = -1;
};

#endif // PATHSELECTORSTATE_H
