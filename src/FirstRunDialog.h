#pragma once

#include <QDialog>

class QPushButton;

/// Modal shown on the first launch in a production build.
///
/// Acceptance contract:
///   - Default focus is the "Yes, enable autostart" button → Enter accepts.
///   - "Skip" rejects.
///   - Esc rejects (standard QDialog behavior).
///   - Closing the window (X) rejects.
///
/// The dialog only reports the choice via standard accept/reject. The
/// caller (main.cpp) applies the choice via Autostart::applyFirstRunChoice.
class FirstRunDialog : public QDialog
{
    Q_OBJECT
public:
    explicit FirstRunDialog(QWidget *parent = nullptr);

    QPushButton *enableButton() const { return m_enableButton; }
    QPushButton *skipButton() const { return m_skipButton; }

private:
    QPushButton *m_enableButton = nullptr;
    QPushButton *m_skipButton = nullptr;
};
