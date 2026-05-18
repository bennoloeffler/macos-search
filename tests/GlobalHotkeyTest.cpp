#include "GlobalHotkeyTest.h"

#include "ExcludeSettings.h"
#include "FolderBrowserDialog.h"
#include "GlobalHotkey.h"
#include "PathCacheManager.h"

#include <QApplication>
#include <QLabel>
#include <QLineEdit>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

void GlobalHotkeyTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    static ExcludeSettings excludes;
    PathCacheManager::instance()->setExcludeSettings(&excludes);
    GlobalHotkey::Testing::setDryRun(true);
}

void GlobalHotkeyTest::cleanupTestCase()
{
    GlobalHotkey::Testing::setDryRun(false);
    QStandardPaths::setTestModeEnabled(false);
}

void GlobalHotkeyTest::init()
{
    GlobalHotkey::Testing::resetCallTracking();
}

void GlobalHotkeyTest::cleanup() {}

void GlobalHotkeyTest::dryRunRegisterReturnsTrueWithoutCarbonCall()
{
    GlobalHotkey hk;
    QVERIFY(hk.registerSummonChord());
    QVERIFY(hk.isRegistered());
    QVERIFY(GlobalHotkey::Testing::lastRegisterAttempted());
}

void GlobalHotkeyTest::dryRunUnregisterClearsRegisteredFlag()
{
    GlobalHotkey hk;
    QVERIFY(hk.registerSummonChord());
    QVERIFY(hk.isRegistered());
    hk.unregisterSummonChord();
    QVERIFY(!hk.isRegistered());
    QVERIFY(GlobalHotkey::Testing::lastUnregisterAttempted());
}

void GlobalHotkeyTest::registerIsIdempotent()
{
    GlobalHotkey hk;
    QVERIFY(hk.registerSummonChord());
    // Second call should return true without re-registering.
    QVERIFY(hk.registerSummonChord());
    QVERIFY(hk.isRegistered());
}

void GlobalHotkeyTest::summonSignalEmittedManually()
{
    GlobalHotkey hk;
    QSignalSpy spy(&hk, &GlobalHotkey::summonRequested);
    hk.emitSummonForTesting();
    QCOMPARE(spy.count(), 1);
}

void GlobalHotkeyTest::summonInvokesDialogFocusAndSelectAll()
{
    FolderBrowserDialog dialog(QDir::homePath(), nullptr);
    dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));

    auto *searchField = dialog.findChild<QLineEdit *>("searchField");
    QVERIFY(searchField);

    // Type some text, move focus away, then summon. We expect focus back
    // on searchField AND the entire content selected.
    searchField->setText(QStringLiteral("trafo"));
    QWidget *other = dialog.findChild<QWidget *>("favoritesList");
    if (other) other->setFocus();
    QVERIFY(QApplication::focusWidget() != searchField);

    dialog.summon();

    QCOMPARE(QApplication::focusWidget(), searchField);
    QCOMPARE(searchField->selectedText(), QStringLiteral("trafo"));
    QVERIFY(dialog.isVisible());
}

void GlobalHotkeyTest::shortcutsHintContainsSummonChord()
{
    FolderBrowserDialog dialog(QDir::homePath(), nullptr);
    auto *hint = dialog.findChild<QLabel *>("shortcutsHint");
    QVERIFY(hint);
    QVERIFY2(hint->text().contains(QStringLiteral("⌃⌥⇧S")),
             qPrintable(QStringLiteral("hint text was: ") + hint->text()));
}
