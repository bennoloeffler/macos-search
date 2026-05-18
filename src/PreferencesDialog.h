#pragma once

#include <QDialog>

class QCheckBox;
class QPushButton;
class ExcludeSettings;
class GlobalHotkey;

/// Preferences modal — depends on Autostart (TODO 5) + GlobalHotkey (TODO 6).
///
/// Three live-applied checkboxes:
///   - "Start macos-search automatically at login"
///       → Autostart::setEnabled(bool) (writes QSettings + LaunchAgent plist
///         in production builds, no-op at OS layer in dev/tests).
///   - "Enable global hotkey ⌃⌥⇧S to summon the app"
///       → persists `hotkeyEnabled` QSettings; if a GlobalHotkey* was passed
///         in, also dispatches register/unregister immediately.
///   - "Show hidden folders"
///       → persists `showHidden` QSettings + emits showHiddenChanged so the
///         eye button in the parent dialog can sync.
///
/// Plus an "Edit exclude rules…" button that opens the legacy
/// ExcludeSettingsDialog modally on top.
///
/// Tests pass a nullptr GlobalHotkey to verify the QSettings layer in
/// isolation, then pass a real dry-run hotkey for the dispatch test.
class PreferencesDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PreferencesDialog(ExcludeSettings *excludes,
                               GlobalHotkey *hotkey,
                               QWidget *parent = nullptr);

    QCheckBox *autostartCheckbox() const { return m_autostart; }
    QCheckBox *hotkeyCheckbox() const { return m_hotkey; }
    QCheckBox *showHiddenCheckbox() const { return m_showHidden; }
    QPushButton *editExcludesButton() const { return m_editExcludes; }
    QPushButton *closeButton() const { return m_closeButton; }

signals:
    void showHiddenChanged(bool enabled);
    void hotkeyEnabledChanged(bool enabled);

private slots:
    void onAutostartToggled(bool checked);
    void onHotkeyToggled(bool checked);
    void onShowHiddenToggled(bool checked);
    void onEditExcludesClicked();

private:
    ExcludeSettings *m_excludes = nullptr;
    GlobalHotkey *m_hotkeyHandle = nullptr;
    QCheckBox *m_autostart = nullptr;
    QCheckBox *m_hotkey = nullptr;
    QCheckBox *m_showHidden = nullptr;
    QPushButton *m_editExcludes = nullptr;
    QPushButton *m_closeButton = nullptr;
};
