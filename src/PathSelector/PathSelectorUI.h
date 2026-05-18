#ifndef PATHSELECTORUI_H
#define PATHSELECTORUI_H

#include <QWidget>
#include "PathSelectorState.h"

class QLineEdit;
class QLabel;
class QPushButton;
class PathSelectorPopup;
class PathSelectorKeyHandler;

/**
 * @brief Visual layer for the path selector component.
 *
 * Binds the PathSelectorState to visual widgets:
 * - Line edit for path input
 * - Hint label below the input
 * - Popup for completions
 *
 * Updates visual styling based on state:
 * - Complete: Bold black text
 * - Browsing: Normal black text
 * - PartialMultiple: Grey text
 * - PartialSingle: Bold grey text
 * - Invalid: Red text
 */
class PathSelectorUI : public QWidget
{
    Q_OBJECT

public:
    explicit PathSelectorUI(PathSelectorState *state, QWidget *parent = nullptr);
    ~PathSelectorUI() override;

    QLineEdit *lineEdit() const { return m_lineEdit; }
    PathSelectorPopup *popup() const { return m_popup; }
    QLabel *hintLabel() const { return m_hintLabel; }
    QPushButton *createFolderButton() const { return m_createFolderButton; }

    void focusLineEdit();

    // For testing
    PathSelectorKeyHandler *keyHandler() const { return m_keyHandler; }

signals:
    void focusTraversalRequested();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onStateChanged(PathSelectorState::State state);
    void onCompletionsChanged(const QStringList &completions);
    void onSelectedIndexChanged(int index);
    void onCurrentTextChanged(const QString &text);
    void onTextEdited(const QString &text);
    void onShowPopupRequested();
    void onHidePopupRequested();
    void onFocusTraversalRequested();
    void onSlashAppended(const QString &newText);
    void onPopupItemSelected(int index);
    void onPopupItemActivated(int index);
    void onPopupNavigationRequested(int delta);
    void onPopupTabPressed();
    void onPopupReturnPressed();
    void onPopupEscapePressed();
    void onCreateFolderClicked();

private:
    void setupUi();
    void setupConnections();
    void updateTextStyle();
    void updateHintText();

    PathSelectorState *m_state;
    PathSelectorKeyHandler *m_keyHandler;
    QLineEdit *m_lineEdit;
    QLabel *m_hintLabel;
    QPushButton *m_createFolderButton;
    PathSelectorPopup *m_popup;
    bool m_updatingFromState = false;
    bool m_popupNavigationMode = false;  // True when user is navigating popup with arrows
};

#endif // PATHSELECTORUI_H
