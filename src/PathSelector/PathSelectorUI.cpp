#include "PathSelectorUI.h"
#include "PathSelectorState.h"
#include "PathSelectorKeyHandler.h"
#include "PathSelectorPopup.h"
#include "FileSystemAdapter.h"
#include "SwiftUIStyle.h"
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QTimer>
#include <QDir>

PathSelectorUI::PathSelectorUI(PathSelectorState *state, QWidget *parent)
    : QWidget(parent)
    , m_state(state)
    , m_keyHandler(nullptr)
    , m_lineEdit(nullptr)
    , m_hintLabel(nullptr)
    , m_createFolderButton(nullptr)
    , m_popup(nullptr)
{
    setupUi();
    // Initial styling before connecting signals
    updateTextStyle();
    updateHintText();
    // Connect signals last, after UI is fully set up
    setupConnections();
}

PathSelectorUI::~PathSelectorUI()
{
    // Popup is parented to window, will be deleted by Qt
}

void PathSelectorUI::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(SwiftUIStyle::SpacingTight);

    // Line edit
    m_lineEdit = new QLineEdit(this);
    m_lineEdit->setPlaceholderText(QStringLiteral("Enter path..."));
    m_lineEdit->installEventFilter(this);
    layout->addWidget(m_lineEdit);

    // Hint row: label + create folder button
    auto *hintRow = new QHBoxLayout();
    hintRow->setContentsMargins(0, 0, 0, 0);
    hintRow->setSpacing(SwiftUIStyle::SpacingSmall);

    m_hintLabel = new QLabel(this);
    // Match the macOS form-row hint style: small, tertiary color, slight
    // left padding so the text sits visually under the input's first
    // character rather than under the container's outer edge.
    QFont hintFont = m_hintLabel->font();
    hintFont.setPointSize(10);
    m_hintLabel->setFont(hintFont);
    m_hintLabel->setStyleSheet(
        QStringLiteral("color: rgba(0,0,0,0.42); padding: 0 0 0 6px;"));
    hintRow->addWidget(m_hintLabel);

    m_createFolderButton = new QPushButton(QStringLiteral("Create Folder"), this);
    m_createFolderButton->setFlat(true);
    m_createFolderButton->setFont(SwiftUIStyle::captionFont());
    m_createFolderButton->setStyleSheet(SwiftUIStyle::secondaryButtonStyleSheet());
    m_createFolderButton->setVisible(false);
    m_createFolderButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    connect(m_createFolderButton, &QPushButton::clicked,
            this, &PathSelectorUI::onCreateFolderClicked);
    hintRow->addWidget(m_createFolderButton);

    hintRow->addStretch();
    layout->addLayout(hintRow);

    // Popup - now a regular widget in the layout (not a popup overlay)
    m_popup = new PathSelectorPopup(m_lineEdit, this);
    layout->addWidget(m_popup);  // Add to layout so it's always part of the UI
    m_popup->setVisible(false);  // Initially hidden

    // Key handler
    m_keyHandler = new PathSelectorKeyHandler(m_state, this);
}

void PathSelectorUI::setupConnections()
{
    // State -> UI
    connect(m_state, &PathSelectorState::stateChanged,
            this, &PathSelectorUI::onStateChanged);
    connect(m_state, &PathSelectorState::completionsChanged,
            this, &PathSelectorUI::onCompletionsChanged);
    connect(m_state, &PathSelectorState::selectedIndexChanged,
            this, &PathSelectorUI::onSelectedIndexChanged);
    connect(m_state, &PathSelectorState::currentTextChanged,
            this, &PathSelectorUI::onCurrentTextChanged);

    // Line edit -> State
    connect(m_lineEdit, &QLineEdit::textEdited,
            this, &PathSelectorUI::onTextEdited);

    // Key handler signals
    connect(m_keyHandler, &PathSelectorKeyHandler::showPopupRequested,
            this, &PathSelectorUI::onShowPopupRequested);
    connect(m_keyHandler, &PathSelectorKeyHandler::hidePopupRequested,
            this, &PathSelectorUI::onHidePopupRequested);
    connect(m_keyHandler, &PathSelectorKeyHandler::focusTraversalRequested,
            this, &PathSelectorUI::onFocusTraversalRequested);
    connect(m_keyHandler, &PathSelectorKeyHandler::slashAppended,
            this, &PathSelectorUI::onSlashAppended);

    // Popup signals
    connect(m_popup, &PathSelectorPopup::itemSelected,
            this, &PathSelectorUI::onPopupItemSelected);
    connect(m_popup, &PathSelectorPopup::itemActivated,
            this, &PathSelectorUI::onPopupItemActivated);

    // Popup navigation signals (for when popup has focus)
    connect(m_popup, &PathSelectorPopup::navigationRequested,
            this, &PathSelectorUI::onPopupNavigationRequested);
    connect(m_popup, &PathSelectorPopup::tabPressed,
            this, &PathSelectorUI::onPopupTabPressed);
    connect(m_popup, &PathSelectorPopup::returnPressed,
            this, &PathSelectorUI::onPopupReturnPressed);
    connect(m_popup, &PathSelectorPopup::escapePressed,
            this, &PathSelectorUI::onPopupEscapePressed);
}

void PathSelectorUI::focusLineEdit()
{
    m_lineEdit->setFocus();
    m_lineEdit->selectAll();
}

bool PathSelectorUI::eventFilter(QObject *watched, QEvent *event)
{
    // Debug: log ALL events to lineEdit when MAUDE_DEBUG is set
    if (qEnvironmentVariableIsSet("MAUDE_DEBUG") && watched == m_lineEdit) {
        if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
            auto *keyEvent = static_cast<QKeyEvent *>(event);
            qInfo() << "[PathSelectorUI] eventFilter:" << event->type()
                    << "key:" << keyEvent->key()
                    << "text:" << keyEvent->text()
                    << "navigationMode:" << m_popupNavigationMode;
        }
    }

    if (watched == m_lineEdit && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        bool popupVisible = m_popup->isVisible();

        // Debug: log key events when MAUDE_DEBUG is set
        if (qEnvironmentVariableIsSet("MAUDE_DEBUG")) {
            qInfo() << "[PathSelectorUI] Key press:" << keyEvent->key()
                    << "popup visible:" << popupVisible
                    << "navigationMode:" << m_popupNavigationMode
                    << "state:" << static_cast<int>(m_state->state());
        }

        // CRITICAL: Always ensure focus stays in text field when popup is visible
        // The popup should NEVER receive focus - it's just a visual overlay
        if (popupVisible && !m_lineEdit->hasFocus()) {
            if (qEnvironmentVariableIsSet("MAUDE_DEBUG")) {
                qInfo() << "[PathSelectorUI] Popup visible but lineEdit lost focus - restoring focus";
            }
            m_lineEdit->setFocus();
        }

        // Handle special keys when in popup navigation mode
        if (m_popupNavigationMode && popupVisible) {
            // If '/' or Return pressed while navigating popup, accept selection
            if (keyEvent->key() == Qt::Key_Slash || 
                keyEvent->key() == Qt::Key_Return || 
                keyEvent->key() == Qt::Key_Enter) {
                if (qEnvironmentVariableIsSet("MAUDE_DEBUG")) {
                    qInfo() << "[PathSelectorUI] Accepting selection from popup navigation mode, selectedIndex:" << m_state->selectedIndex();
                }
                // Accept the currently highlighted item (this updates the text field)
                m_state->acceptSelection();
                m_popup->hideAnimated();
                m_popupNavigationMode = false;
                
                // If '/' was pressed, append it after accepting the selection
                if (keyEvent->key() == Qt::Key_Slash) {
                    QString text = m_state->currentText();
                    if (!text.endsWith(QLatin1Char('/'))) {
                        text += QLatin1Char('/');
                        m_updatingFromState = true;
                        m_lineEdit->setText(text);
                        m_state->setCurrentText(text);
                        m_updatingFromState = false;
                        // Show popup again for the new path
                        if (m_state->shouldShowPopup()) {
                            m_popup->showAnimated();
                        }
                    }
                }
                
                // Ensure focus stays in line edit
                m_lineEdit->setFocus();
                return true; // Consume the event
            }
        }

        // Handle arrow keys: enter navigation mode when arrow down/up is pressed
        // This happens BEFORE key handler so we can track navigation state
        bool isArrowKey = (keyEvent->key() == Qt::Key_Down || keyEvent->key() == Qt::Key_Up);
        
        if (isArrowKey) {
            // If popup is visible OR will become visible, enter navigation mode
            if (popupVisible || m_state->shouldShowPopup()) {
                m_popupNavigationMode = true;
                if (qEnvironmentVariableIsSet("MAUDE_DEBUG")) {
                    qInfo() << "[PathSelectorUI] Entered popup navigation mode via arrow key, popupVisible:" << popupVisible;
                }
            }
            // Ensure focus stays in line edit (don't let popup steal it)
            if (!m_lineEdit->hasFocus()) {
                m_lineEdit->setFocus();
            }
        } else if (!keyEvent->text().isEmpty() && 
                   keyEvent->key() != Qt::Key_Backspace && 
                   keyEvent->key() != Qt::Key_Delete &&
                   keyEvent->key() != Qt::Key_Tab &&
                   keyEvent->key() != Qt::Key_Escape &&
                   keyEvent->key() != Qt::Key_Return &&
                   keyEvent->key() != Qt::Key_Enter &&
                   keyEvent->key() != Qt::Key_Slash) {
            // Typing characters (not navigation/special keys) exits navigation mode
            // Keep focus in text field
            if (m_popupNavigationMode) {
                if (qEnvironmentVariableIsSet("MAUDE_DEBUG")) {
                    qInfo() << "[PathSelectorUI] Exiting popup navigation mode (typing character)";
                }
                m_popupNavigationMode = false;
            }
            // Ensure focus stays in line edit
            if (!m_lineEdit->hasFocus()) {
                m_lineEdit->setFocus();
            }
        }

        // Let key handler process the key (it will show popup if needed, cycle selection, etc.)
        bool handled = m_keyHandler->handleKeyPress(keyEvent, popupVisible);
        
        // After key handler runs, check if popup became visible and we should be in navigation mode
        if (isArrowKey && !popupVisible && m_popup->isVisible()) {
            // Popup just opened via arrow key - ensure we're in navigation mode
            m_popupNavigationMode = true;
            if (qEnvironmentVariableIsSet("MAUDE_DEBUG")) {
                qInfo() << "[PathSelectorUI] Popup opened via arrow key, entered navigation mode";
            }
        }
        
        if (handled) {
            if (qEnvironmentVariableIsSet("MAUDE_DEBUG")) {
                qInfo() << "[PathSelectorUI] Event consumed by key handler";
            }
            return true; // Event was consumed
        }
    }

    return QWidget::eventFilter(watched, event);
}

void PathSelectorUI::onStateChanged(PathSelectorState::State state)
{
    Q_UNUSED(state)

    if (!m_lineEdit || !m_hintLabel || !m_popup) {
        return;  // Not fully initialized yet
    }

    updateTextStyle();
    updateHintText();

    // Auto-show/hide popup based on state
    if (m_state->shouldShowPopup() && !m_popup->isVisible()) {
        m_popup->showAnimated();
    } else if (!m_state->shouldShowPopup() && m_popup->isVisible()) {
        m_popup->hideAnimated();
    }
}

void PathSelectorUI::onCompletionsChanged(const QStringList &completions)
{
    if (m_popup) {
        m_popup->setItems(completions);
    }
}

void PathSelectorUI::onSelectedIndexChanged(int index)
{
    if (m_popup) {
        m_popup->setSelectedIndex(index);
    }
}

void PathSelectorUI::onCurrentTextChanged(const QString &text)
{
    if (m_updatingFromState || !m_lineEdit) {
        return;
    }

    m_updatingFromState = true;
    m_lineEdit->setText(text);
    m_updatingFromState = false;
}

void PathSelectorUI::onTextEdited(const QString &text)
{
    if (m_updatingFromState) {
        return;
    }

    m_updatingFromState = true;
    m_state->setCurrentText(text);
    m_updatingFromState = false;
}

void PathSelectorUI::onShowPopupRequested()
{
    if (!m_popup->isVisible() && !m_state->completions().isEmpty()) {
        m_popup->showAnimated();
        // CRITICAL: When popup is shown, ensure focus ALWAYS stays in line edit
        // The popup is just a visual overlay - it should NEVER receive focus
        // Navigation mode will be entered when user presses arrow down
        m_popupNavigationMode = false;
        if (m_lineEdit) {
            // Force focus to stay in line edit
            m_lineEdit->setFocus();
            // Use a timer to ensure focus stays after popup animation
            QTimer::singleShot(50, this, [this]() {
                if (m_lineEdit && m_popup && m_popup->isVisible()) {
                    m_lineEdit->setFocus();
                }
            });
        }
        if (qEnvironmentVariableIsSet("MAUDE_DEBUG")) {
            qInfo() << "[PathSelectorUI] Popup shown, focus forced to stay in line edit";
        }
    }
}

void PathSelectorUI::onHidePopupRequested()
{
    if (m_popup->isVisible()) {
        m_popup->hideAnimated();
        // Exit navigation mode when popup is hidden
        m_popupNavigationMode = false;
        if (qEnvironmentVariableIsSet("MAUDE_DEBUG")) {
            qInfo() << "[PathSelectorUI] Popup hidden, exiting navigation mode";
        }
    }
}

void PathSelectorUI::onFocusTraversalRequested()
{
    // Move focus to the next widget in the tab order
    focusNextPrevChild(true);
    // Also emit the signal for external listeners
    emit focusTraversalRequested();
}

void PathSelectorUI::onSlashAppended(const QString &newText)
{
    m_updatingFromState = true;
    m_lineEdit->setText(newText);
    m_state->setCurrentText(newText);
    m_updatingFromState = false;
}

void PathSelectorUI::onPopupItemSelected(int index)
{
    m_state->setSelectedIndex(index);
}

void PathSelectorUI::onPopupItemActivated(int index)
{
    m_state->setSelectedIndex(index);
    m_state->acceptSelection();
    m_popup->hideAnimated();
}

void PathSelectorUI::onPopupNavigationRequested(int delta)
{
    // Enter navigation mode when popup navigation is requested
    m_popupNavigationMode = true;
    if (qEnvironmentVariableIsSet("MAUDE_DEBUG")) {
        qInfo() << "[PathSelectorUI] Popup navigation requested, entering navigation mode";
    }
    m_state->cycleSelection(delta);
}

void PathSelectorUI::onPopupTabPressed()
{
    // Tab in popup: cycle selection or accept
    PathSelectorState::State state = m_state->state();

    if (state == PathSelectorState::State::PartialSingle) {
        m_state->acceptSelection();
        m_popup->hideAnimated();
    } else if (state == PathSelectorState::State::PartialMultiple ||
               state == PathSelectorState::State::Browsing) {
        m_state->cycleSelection(1);
    }
}

void PathSelectorUI::onPopupReturnPressed()
{
    if (qEnvironmentVariableIsSet("MAUDE_DEBUG")) {
        qInfo() << "[PathSelectorUI] Return pressed in popup, accepting selection";
    }
    m_state->acceptSelection();
    m_popup->hideAnimated();
    m_popupNavigationMode = false;
    // Ensure focus returns to line edit
    if (m_lineEdit) {
        m_lineEdit->setFocus();
    }
}

void PathSelectorUI::onPopupEscapePressed()
{
    m_state->revert();
    m_popup->hideAnimated();
}

void PathSelectorUI::updateTextStyle()
{
    if (!m_lineEdit || !m_state) {
        return;
    }

    // The PathSelector lives inside a styled container (pathFieldFrame in
    // FolderBrowserDialog) that provides the soft-rounded gray bg + border.
    // The internal QLineEdit must therefore be visually transparent — no
    // bg, no border — or it punches a white card out of the container.
    //
    // Each state-specific stylesheet keeps the per-state color tweak but
    // ships the same neutral container styling so updateTextStyle() can be
    // called any number of times without losing the "transparent input"
    // look.
    static const QString kInputBase =
        QStringLiteral("QLineEdit { background: transparent; border: none; "
                       "padding: 4px 6px; selection-background-color: %1; "
                       "selection-color: white; ");

    QString stateColor;
    QFont font = SwiftUIStyle::bodyFont();

    switch (m_state->state()) {
    case PathSelectorState::State::Complete:
        font.setBold(true);
        stateColor = SwiftUIStyle::primaryTextColor();
        break;
    case PathSelectorState::State::Browsing:
        font.setBold(false);
        stateColor = SwiftUIStyle::primaryTextColor();
        break;
    case PathSelectorState::State::PartialMultiple:
        font.setBold(false);
        stateColor = SwiftUIStyle::secondaryTextColor();
        break;
    case PathSelectorState::State::PartialSingle:
        font.setBold(true);
        stateColor = SwiftUIStyle::secondaryTextColor();
        break;
    case PathSelectorState::State::Invalid:
        font.setBold(false);
        stateColor = SwiftUIStyle::ErrorColor;
        break;
    }

    const QString styleSheet =
        kInputBase.arg(SwiftUIStyle::BrandColor)
        + QStringLiteral("color: %1; }").arg(stateColor);

    m_lineEdit->setFont(font);
    m_lineEdit->setStyleSheet(styleSheet);
}

void PathSelectorUI::updateHintText()
{
    if (!m_hintLabel || !m_state) {
        return;
    }

    QString hint;
    // Default hint color: tertiary text. Invalid state overrides to error red.
    QString color = QStringLiteral("rgba(0,0,0,0.42)");
    bool showCreateFolder = false;

    switch (m_state->state()) {
    case PathSelectorState::State::Complete:
        hint = QStringLiteral("/ or \u2193 = show folders list");
        break;

    case PathSelectorState::State::Browsing:
    case PathSelectorState::State::PartialMultiple:
        hint = QStringLiteral("Tab = move in list, Return = choose");
        break;

    case PathSelectorState::State::PartialSingle:
        hint = QStringLiteral("Tab or Return = choose");
        break;

    case PathSelectorState::State::Invalid: {
        hint = QStringLiteral("Not a valid path");
        color = SwiftUIStyle::ErrorColor;
        // Show "Create Folder" if the path is absolute (creatable)
        QString text = m_state->currentText();
        if (text.startsWith(QLatin1Char('/')) || text.startsWith(QLatin1Char('~'))) {
            showCreateFolder = true;
        }
        break;
    }
    }

    m_hintLabel->setText(hint);
    // Keep the left padding from the initial style; only update the color.
    m_hintLabel->setStyleSheet(
        QStringLiteral("color: %1; padding: 0 0 0 6px;").arg(color));

    if (m_createFolderButton) {
        m_createFolderButton->setVisible(showCreateFolder);
    }
}

void PathSelectorUI::onCreateFolderClicked()
{
    if (!m_state) {
        return;
    }

    QString path = m_state->currentText();
    if (path.isEmpty()) {
        return;
    }

    FileSystemAdapter *fs = m_state->fileSystemAdapter();
    if (!fs) {
        return;
    }

    if (fs->createDirectory(path)) {
        // Re-trigger state evaluation by re-setting the current text.
        // The path now exists, so the state will transition to Complete or Browsing.
        QString text = m_state->currentText();
        m_state->setCurrentText(QString());
        m_state->setCurrentText(text);
    }
}
