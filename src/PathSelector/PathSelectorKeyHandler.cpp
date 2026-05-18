#include "PathSelectorKeyHandler.h"
#include "PathSelectorState.h"
#include <QKeyEvent>

PathSelectorKeyHandler::PathSelectorKeyHandler(PathSelectorState *state,
                                               QObject *parent)
    : QObject(parent)
    , m_state(state)
{
}

bool PathSelectorKeyHandler::handleKeyPress(QKeyEvent *event, bool popupVisible)
{
    switch (event->key()) {
    case Qt::Key_Tab:
    case Qt::Key_Backtab:
        return handleTab(popupVisible);

    case Qt::Key_Return:
    case Qt::Key_Enter:
        return handleReturn(popupVisible);

    case Qt::Key_Escape:
        return handleEscape(popupVisible);

    case Qt::Key_Down:
        return handleArrowDown(popupVisible);

    case Qt::Key_Up:
        return handleArrowUp(popupVisible);

    case Qt::Key_Slash:
        return handleSlash();

    default:
        return false;
    }
}

bool PathSelectorKeyHandler::handleTab(bool popupVisible)
{
    PathSelectorState::State state = m_state->state();

    // Complete state with popup closed: allow focus traversal
    if (state == PathSelectorState::State::Complete && !popupVisible) {
        emit focusTraversalRequested();
        return true; // Consume - we handled traversal via focusNextPrevChild
    }

    // Invalid state: revert and allow traversal
    if (state == PathSelectorState::State::Invalid && !popupVisible) {
        m_state->revert();
        emit hidePopupRequested();
        emit focusTraversalRequested();
        return true; // Consume - we handled traversal via focusNextPrevChild
    }

    // Single option: accept it
    if (state == PathSelectorState::State::PartialSingle) {
        m_state->acceptSelection();
        emit hidePopupRequested();
        return true;
    }

    // Multiple options or browsing: cycle to next
    if (state == PathSelectorState::State::PartialMultiple ||
        state == PathSelectorState::State::Browsing) {
        m_state->cycleSelection(1);
        return true;
    }

    return false;
}

bool PathSelectorKeyHandler::handleReturn(bool popupVisible)
{
    PathSelectorState::State state = m_state->state();

    if (qEnvironmentVariableIsSet("MAUDE_DEBUG")) {
        qInfo() << "[KeyHandler] handleReturn popup:" << popupVisible
                << "state:" << static_cast<int>(state);
    }

    // Accept if we have something to accept
    if (state == PathSelectorState::State::Complete ||
        state == PathSelectorState::State::Browsing ||
        state == PathSelectorState::State::PartialSingle ||
        state == PathSelectorState::State::PartialMultiple) {
        if (qEnvironmentVariableIsSet("MAUDE_DEBUG")) {
            qInfo() << "[KeyHandler] Accepting selection, selectedIndex:" << m_state->selectedIndex();
        }
        m_state->acceptSelection();
        emit hidePopupRequested();
        return true;
    }

    // Invalid: revert
    if (state == PathSelectorState::State::Invalid) {
        if (qEnvironmentVariableIsSet("MAUDE_DEBUG")) {
            qInfo() << "[KeyHandler] Invalid state, reverting";
        }
        m_state->revert();
        emit hidePopupRequested();
        return true;
    }

    return false;
}

bool PathSelectorKeyHandler::handleEscape(bool popupVisible)
{
    PathSelectorState::State state = m_state->state();

    // If popup is visible, close it and revert if needed
    if (popupVisible) {
        if (state == PathSelectorState::State::Invalid ||
            state == PathSelectorState::State::PartialMultiple ||
            state == PathSelectorState::State::PartialSingle) {
            m_state->revert();
        }
        emit hidePopupRequested();
        return true;
    }

    // Popup closed but state is invalid: revert
    if (state == PathSelectorState::State::Invalid) {
        m_state->revert();
        return true;
    }

    return false;
}

bool PathSelectorKeyHandler::handleArrowDown(bool popupVisible)
{
    if (qEnvironmentVariableIsSet("MAUDE_DEBUG")) {
        qInfo() << "[KeyHandler] handleArrowDown popup:" << popupVisible
                << "state:" << static_cast<int>(m_state->state());
    }

    if (popupVisible) {
        // Navigate down in the list (navigation mode should already be active)
        // Text field should NOT be updated - only selection changes
        m_state->cycleSelection(1);
        if (qEnvironmentVariableIsSet("MAUDE_DEBUG")) {
            qInfo() << "[KeyHandler] Navigated down in popup, selection:" << m_state->selectedIndex();
        }
        return true;
    }

    // Popup closed: open it and highlight first item
    // This is the FIRST arrow down press - user wants to start navigating
    if (m_state->state() == PathSelectorState::State::Complete) {
        // Append slash to trigger browsing mode
        QString text = m_state->currentText();
        if (qEnvironmentVariableIsSet("MAUDE_DEBUG")) {
            qInfo() << "[KeyHandler] Complete state, opening popup via arrow down, text:" << text;
        }
        if (!text.endsWith(QLatin1Char('/'))) {
            text += QLatin1Char('/');
            emit slashAppended(text);
            if (qEnvironmentVariableIsSet("MAUDE_DEBUG")) {
                qInfo() << "[KeyHandler] Emitted slashAppended:" << text;
            }
        }
        emit showPopupRequested();
        // Note: Navigation mode will be set by PathSelectorUI::eventFilter
        // after popup becomes visible
        return true;
    }

    // For partial states, just show popup (navigation mode will be set by UI)
    if (m_state->shouldShowPopup()) {
        if (qEnvironmentVariableIsSet("MAUDE_DEBUG")) {
            qInfo() << "[KeyHandler] Partial state, showing popup via arrow down";
        }
        emit showPopupRequested();
        return true;
    }

    return false;
}

bool PathSelectorKeyHandler::handleArrowUp(bool popupVisible)
{
    if (qEnvironmentVariableIsSet("MAUDE_DEBUG")) {
        qInfo() << "[KeyHandler] handleArrowUp popup:" << popupVisible
                << "state:" << static_cast<int>(m_state->state());
    }

    if (popupVisible) {
        // Navigate up in the list (this enters navigation mode)
        // Text field should NOT be updated - only selection changes
        m_state->cycleSelection(-1);
        if (qEnvironmentVariableIsSet("MAUDE_DEBUG")) {
            qInfo() << "[KeyHandler] Navigated up in popup, selection:" << m_state->selectedIndex();
        }
        return true;
    }

    return false;
}

bool PathSelectorKeyHandler::handleSlash()
{
    PathSelectorState::State state = m_state->state();

    if (qEnvironmentVariableIsSet("MAUDE_DEBUG")) {
        qInfo() << "[KeyHandler] handleSlash state:" << static_cast<int>(state);
    }

    // On complete state: append slash and show popup
    if (state == PathSelectorState::State::Complete) {
        QString text = m_state->currentText();
        if (!text.endsWith(QLatin1Char('/'))) {
            text += QLatin1Char('/');
            emit slashAppended(text);
            emit showPopupRequested();
            if (qEnvironmentVariableIsSet("MAUDE_DEBUG")) {
                qInfo() << "[KeyHandler] Appended slash, showing popup";
            }
            return true;
        }
    }

    // On partial single: accept first, then append slash
    if (state == PathSelectorState::State::PartialSingle) {
        if (qEnvironmentVariableIsSet("MAUDE_DEBUG")) {
            qInfo() << "[KeyHandler] PartialSingle: accepting selection before slash";
        }
        m_state->acceptSelection();
        QString text = m_state->currentText();
        if (!text.endsWith(QLatin1Char('/'))) {
            text += QLatin1Char('/');
            emit slashAppended(text);
            emit showPopupRequested();
            if (qEnvironmentVariableIsSet("MAUDE_DEBUG")) {
                qInfo() << "[KeyHandler] Accepted and appended slash, showing popup";
            }
            return true;
        }
    }

    // On partial multiple or browsing: if navigating popup, accept selection first
    // This case is handled in PathSelectorUI::eventFilter when navigation mode is active

    return false;
}
