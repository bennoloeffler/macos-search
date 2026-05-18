#include "PreferencesDialog.h"

#include "Autostart.h"
#include "ExcludeSettings.h"
#include "ExcludeSettingsDialog.h"
#include "GlobalHotkey.h"

#include <QCheckBox>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

namespace {

QSettings folderBrowserSettings() { return QSettings("Maude", "FolderBrowser"); }

QLabel *sectionLabel(const QString &text, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    QFont f = l->font();
    f.setBold(true);
    l->setFont(f);
    return l;
}

}  // namespace

PreferencesDialog::PreferencesDialog(ExcludeSettings *excludes,
                                     GlobalHotkey *hotkey,
                                     QWidget *parent)
    : QDialog(parent),
      m_excludes(excludes),
      m_hotkeyHandle(hotkey)
{
    setObjectName("preferencesDialog");
    setWindowTitle(tr("Preferences"));
    setModal(true);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(14);

    layout->addWidget(sectionLabel(tr("General"), this));

    m_autostart = new QCheckBox(tr("Start macos-search automatically at login"), this);
    m_autostart->setObjectName("autostartCheckbox");
    m_autostart->setChecked(Autostart::isEnabled());
    connect(m_autostart, &QCheckBox::toggled,
            this, &PreferencesDialog::onAutostartToggled);
    layout->addWidget(m_autostart);

    m_hotkey = new QCheckBox(tr("Enable global hotkey ⌃⌥⇧S to summon the app"), this);
    m_hotkey->setObjectName("hotkeyCheckbox");
    m_hotkey->setChecked(folderBrowserSettings()
                             .value("hotkeyEnabled", true)
                             .toBool());
    connect(m_hotkey, &QCheckBox::toggled,
            this, &PreferencesDialog::onHotkeyToggled);
    layout->addWidget(m_hotkey);

    m_showHidden = new QCheckBox(tr("Show hidden folders"), this);
    m_showHidden->setObjectName("showHiddenCheckbox");
    m_showHidden->setChecked(folderBrowserSettings()
                                 .value("showHidden", false)
                                 .toBool());
    connect(m_showHidden, &QCheckBox::toggled,
            this, &PreferencesDialog::onShowHiddenToggled);
    layout->addWidget(m_showHidden);

    layout->addSpacing(6);
    layout->addWidget(sectionLabel(tr("Exclude rules"), this));

    auto *excludeRow = new QHBoxLayout;
    auto *excludeHint = new QLabel(
        tr("Patterns that are skipped during indexing and search."), this);
    excludeHint->setWordWrap(true);
    excludeRow->addWidget(excludeHint, 1);

    m_editExcludes = new QPushButton(tr("Edit exclude rules…"), this);
    m_editExcludes->setObjectName("editExcludesButton");
    m_editExcludes->setAutoDefault(false);
    connect(m_editExcludes, &QPushButton::clicked,
            this, &PreferencesDialog::onEditExcludesClicked);
    excludeRow->addWidget(m_editExcludes);
    layout->addLayout(excludeRow);

    layout->addStretch(1);

    auto *closeRow = new QHBoxLayout;
    closeRow->addStretch(1);
    m_closeButton = new QPushButton(tr("Close"), this);
    m_closeButton->setObjectName("closeButton");
    m_closeButton->setDefault(true);
    m_closeButton->setAutoDefault(true);
    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::accept);
    closeRow->addWidget(m_closeButton);
    layout->addLayout(closeRow);

    setMinimumWidth(460);
    adjustSize();
}

void PreferencesDialog::onAutostartToggled(bool checked)
{
    Autostart::setEnabled(checked);
}

void PreferencesDialog::onHotkeyToggled(bool checked)
{
    QSettings s = folderBrowserSettings();
    s.setValue("hotkeyEnabled", checked);
    s.sync();

    if (m_hotkeyHandle) {
        if (checked) m_hotkeyHandle->registerSummonChord();
        else m_hotkeyHandle->unregisterSummonChord();
    }
    emit hotkeyEnabledChanged(checked);
}

void PreferencesDialog::onShowHiddenToggled(bool checked)
{
    QSettings s = folderBrowserSettings();
    s.setValue("showHidden", checked);
    s.sync();
    emit showHiddenChanged(checked);
}

void PreferencesDialog::onEditExcludesClicked()
{
    if (!m_excludes) return;
    ExcludeSettingsDialog dlg(m_excludes, this);
    dlg.exec();
}
