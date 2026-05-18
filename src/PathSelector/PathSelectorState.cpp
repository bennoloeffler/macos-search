#include "PathSelectorState.h"
#include "FileSystemAdapter.h"

PathSelectorState::PathSelectorState(FileSystemAdapter *fs, QObject *parent)
    : QObject(parent)
    , m_fs(fs)
{
}

bool PathSelectorState::shouldShowPopup() const
{
    switch (m_state) {
    case State::Browsing:
    case State::PartialMultiple:
    case State::PartialSingle:
        return !m_completions.isEmpty();
    case State::Complete:
    case State::Invalid:
        return false;
    }
    return false;
}

void PathSelectorState::setCurrentText(const QString &text)
{
    if (m_currentText == text) {
        return;
    }

    m_currentText = text;
    emit currentTextChanged(m_currentText);
    updateState();
}

void PathSelectorState::setSelectedIndex(int index)
{
    if (m_completions.isEmpty()) {
        index = -1;
    } else {
        // Clamp to valid range
        int maxIndex = static_cast<int>(m_completions.size()) - 1;
        index = qBound(-1, index, maxIndex);
    }

    if (m_selectedIndex == index) {
        return;
    }

    m_selectedIndex = index;
    emit selectedIndexChanged(m_selectedIndex);
}

void PathSelectorState::acceptSelection()
{
    QString acceptedPath;

    if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_completions.size())) {
        // Accept the selected completion
        acceptedPath = m_completions.at(m_selectedIndex);
    } else if (m_state == State::PartialSingle && m_completions.size() == 1) {
        // Accept the only completion (Tab on single)
        acceptedPath = m_completions.at(0);
    } else if (m_state == State::Complete || m_state == State::Browsing) {
        // Accept current text as-is (it's already valid)
        acceptedPath = m_currentText;
        // Remove trailing slash for storage
        if (acceptedPath.endsWith(QLatin1Char('/')) && acceptedPath.length() > 1) {
            acceptedPath.chop(1);
        }
    } else {
        // Invalid state - cannot accept
        return;
    }

    // Update state
    m_currentText = acceptedPath;
    emit currentTextChanged(m_currentText);

    setLastValidPath(acceptedPath);
    setCompletions({});
    setSelectedIndex(-1);
    setState(State::Complete);

    emit pathAccepted(acceptedPath);
}

void PathSelectorState::revert()
{
    if (m_lastValidPath.isEmpty()) {
        return;
    }

    m_currentText = m_lastValidPath;
    emit currentTextChanged(m_currentText);

    setCompletions({});
    setSelectedIndex(-1);
    setState(State::Complete);

    emit pathReverted(m_lastValidPath);
}

void PathSelectorState::cycleSelection(int delta)
{
    if (m_completions.isEmpty()) {
        return;
    }

    int newIndex = m_selectedIndex + delta;
    int listSize = static_cast<int>(m_completions.size());

    // Wrap around
    if (newIndex < 0) {
        newIndex = listSize - 1;
    } else if (newIndex >= listSize) {
        newIndex = 0;
    }

    setSelectedIndex(newIndex);
}

void PathSelectorState::initialize(const QString &path)
{
    QString expandedPath = m_fs->expandTilde(path);

    if (m_fs->isValidDirectory(expandedPath)) {
        m_currentText = expandedPath;
        m_lastValidPath = expandedPath;
        m_state = State::Complete;
        m_completions.clear();
        m_selectedIndex = -1;

        emit currentTextChanged(m_currentText);
        emit lastValidPathChanged(m_lastValidPath);
        emit stateChanged(m_state);
        emit completionsChanged(m_completions);
        emit selectedIndexChanged(m_selectedIndex);
    }
}

void PathSelectorState::updateState()
{
    QString text = m_fs->expandTilde(m_currentText);

    // Case 1: Complete path (valid directory, no trailing slash)
    if (m_fs->isValidDirectory(text) && !text.endsWith(QLatin1Char('/'))) {
        setLastValidPath(text);
        setCompletions({});
        setSelectedIndex(-1);
        setState(State::Complete);
        return;
    }

    // Case 2: Browsing (valid directory with trailing slash)
    if (text.endsWith(QLatin1Char('/'))) {
        QString baseDir = text.chopped(1);
        // Special case: "/" becomes "" when chopped, but "/" is valid root directory
        if (baseDir.isEmpty() && text == QStringLiteral("/")) {
            baseDir = QStringLiteral("/");
        }
        
        if (m_fs->isValidDirectory(baseDir)) {
            // Update last valid path to include trailing slash context
            setLastValidPath(baseDir);

            // Get completions (all subdirectories)
            QStringList completions = m_fs->filterCompletions(baseDir, QString());
            setCompletions(completions);
            setSelectedIndex(completions.isEmpty() ? -1 : 0);
            setState(State::Browsing);
            return;
        }
    }

    // Case 3: Partial path - parse and filter
    QString basePath, partialName;
    m_fs->parsePath(text, basePath, partialName);

    if (!basePath.isEmpty() && m_fs->isValidDirectory(basePath)) {
        QStringList completions = m_fs->filterCompletions(basePath, partialName);

        if (completions.isEmpty()) {
            // No matches - invalid
            setCompletions({});
            setSelectedIndex(-1);
            setState(State::Invalid);
        } else if (completions.size() == 1) {
            // Single match
            setCompletions(completions);
            setSelectedIndex(0);
            setState(State::PartialSingle);
        } else {
            // Multiple matches
            setCompletions(completions);
            setSelectedIndex(0);
            setState(State::PartialMultiple);
        }
        return;
    }

    // Case 4: Invalid path
    setCompletions({});
    setSelectedIndex(-1);
    setState(State::Invalid);
}

void PathSelectorState::setState(State newState)
{
    if (m_state == newState) {
        return;
    }
    m_state = newState;
    emit stateChanged(m_state);
}

void PathSelectorState::setCompletions(const QStringList &completions)
{
    if (m_completions == completions) {
        return;
    }
    m_completions = completions;
    emit completionsChanged(m_completions);
}

void PathSelectorState::setLastValidPath(const QString &path)
{
    if (m_lastValidPath == path) {
        return;
    }
    m_lastValidPath = path;
    emit lastValidPathChanged(m_lastValidPath);
}
