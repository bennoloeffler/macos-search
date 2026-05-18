#include "PreferencesDialogTest.h"

#include "Autostart.h"
#include "ExcludeSettings.h"
#include "FolderBrowserDialog.h"
#include "GlobalHotkey.h"
#include "PathCacheManager.h"
#include "PreferencesDialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>
#include <QTimer>

namespace {

QSettings folderBrowserSettings() { return QSettings("Maude", "FolderBrowser"); }

void wipeKeys()
{
    auto s = folderBrowserSettings();
    s.remove("autostart");
    s.remove("hotkeyEnabled");
    s.remove("showHidden");
    s.remove("firstRunCompleted");
    s.sync();
}

}  // namespace

void PreferencesDialogTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    static ExcludeSettings excludes;
    PathCacheManager::instance()->setExcludeSettings(&excludes);
    Autostart::Testing::setDryRun(true);
    GlobalHotkey::Testing::setDryRun(true);
}

void PreferencesDialogTest::cleanupTestCase()
{
    Autostart::Testing::setDryRun(false);
    GlobalHotkey::Testing::setDryRun(false);
    wipeKeys();
    QStandardPaths::setTestModeEnabled(false);
}

void PreferencesDialogTest::init()
{
    wipeKeys();
    Autostart::Testing::clearProductionBuildOverride();
    Autostart::Testing::resetCallTracking();
    GlobalHotkey::Testing::resetCallTracking();
}

void PreferencesDialogTest::cleanup()
{
    wipeKeys();
}

// ----- structure ------------------------------------------------------------

void PreferencesDialogTest::hasAllThreeCheckboxes()
{
    ExcludeSettings excludes;
    PreferencesDialog dlg(&excludes, nullptr);
    QVERIFY(dlg.autostartCheckbox());
    QVERIFY(dlg.hotkeyCheckbox());
    QVERIFY(dlg.showHiddenCheckbox());
}

void PreferencesDialogTest::hasEditExcludesButton()
{
    ExcludeSettings excludes;
    PreferencesDialog dlg(&excludes, nullptr);
    QVERIFY(dlg.editExcludesButton());
}

// ----- initial state reflects QSettings ------------------------------------

void PreferencesDialogTest::autostartCheckboxInitiallyReflectsQSettings()
{
    {
        auto s = folderBrowserSettings();
        s.setValue("autostart", true);
        s.sync();
    }
    ExcludeSettings excludes;
    PreferencesDialog dlg(&excludes, nullptr);
    QVERIFY(dlg.autostartCheckbox()->isChecked());
}

void PreferencesDialogTest::hotkeyCheckboxInitiallyReflectsQSettings()
{
    {
        auto s = folderBrowserSettings();
        s.setValue("hotkeyEnabled", false);
        s.sync();
    }
    ExcludeSettings excludes;
    PreferencesDialog dlg(&excludes, nullptr);
    QVERIFY(!dlg.hotkeyCheckbox()->isChecked());
}

void PreferencesDialogTest::showHiddenCheckboxInitiallyReflectsQSettings()
{
    {
        auto s = folderBrowserSettings();
        s.setValue("showHidden", true);
        s.sync();
    }
    ExcludeSettings excludes;
    PreferencesDialog dlg(&excludes, nullptr);
    QVERIFY(dlg.showHiddenCheckbox()->isChecked());
}

// ----- toggling persists ----------------------------------------------------

void PreferencesDialogTest::togglingAutostartPersists()
{
    ExcludeSettings excludes;
    PreferencesDialog dlg(&excludes, nullptr);
    QVERIFY(!dlg.autostartCheckbox()->isChecked());

    dlg.autostartCheckbox()->setChecked(true);
    QVERIFY(Autostart::isEnabled());
    QVERIFY(Autostart::Testing::osRegistrationCalled());
    QVERIFY(Autostart::Testing::lastOsRegistrationRequest());

    Autostart::Testing::resetCallTracking();
    dlg.autostartCheckbox()->setChecked(false);
    QVERIFY(!Autostart::isEnabled());
    QVERIFY(Autostart::Testing::osRegistrationCalled());
    QVERIFY(!Autostart::Testing::lastOsRegistrationRequest());
}

void PreferencesDialogTest::togglingHotkeyPersistsAndDispatches()
{
    ExcludeSettings excludes;
    GlobalHotkey hotkey;
    PreferencesDialog dlg(&excludes, &hotkey);

    QSignalSpy spy(&dlg, &PreferencesDialog::hotkeyEnabledChanged);

    dlg.hotkeyCheckbox()->setChecked(false);
    QCOMPARE(folderBrowserSettings().value("hotkeyEnabled").toBool(), false);
    QVERIFY(!hotkey.isRegistered());

    dlg.hotkeyCheckbox()->setChecked(true);
    QCOMPARE(folderBrowserSettings().value("hotkeyEnabled").toBool(), true);
    QVERIFY(hotkey.isRegistered());

    QCOMPARE(spy.count(), 2);
}

void PreferencesDialogTest::togglingShowHiddenPersistsAndEmits()
{
    ExcludeSettings excludes;
    PreferencesDialog dlg(&excludes, nullptr);

    QSignalSpy spy(&dlg, &PreferencesDialog::showHiddenChanged);

    dlg.showHiddenCheckbox()->setChecked(true);
    QCOMPARE(folderBrowserSettings().value("showHidden").toBool(), true);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.last().value(0).toBool(), true);

    dlg.showHiddenCheckbox()->setChecked(false);
    QCOMPARE(folderBrowserSettings().value("showHidden").toBool(), false);
    QCOMPARE(spy.count(), 2);
}

// ----- close button + main-dialog integration ------------------------------

void PreferencesDialogTest::closeButtonAccepts()
{
    ExcludeSettings excludes;
    PreferencesDialog dlg(&excludes, nullptr);
    QSignalSpy spy(&dlg, &QDialog::accepted);
    QTimer::singleShot(0, &dlg, [&]() { dlg.closeButton()->click(); });
    dlg.exec();
    QCOMPARE(spy.count(), 1);
}

void PreferencesDialogTest::gearOnMainDialogOpensPreferencesNotExclude()
{
    FolderBrowserDialog dialog(QDir::homePath(), nullptr);
    dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));

    auto *gear = dialog.findChild<QPushButton *>("excludeButton");
    QVERIFY(gear);

    // Schedule a search-for-the-preferences-dialog + dismiss it.
    QTimer::singleShot(120, &dialog, [&]() {
        auto *prefs = dialog.findChild<PreferencesDialog *>("preferencesDialog");
        QVERIFY(prefs);
        prefs->accept();
    });

    QTest::mouseClick(gear, Qt::LeftButton);
    // Wait for the timer above to fire and close the modal.
    QTest::qWait(250);
}
