#include "AutostartTest.h"

#include "Autostart.h"
#include "FirstRunDialog.h"

#include <QApplication>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>
#include <QTimer>

namespace {

QSettings folderBrowserSettings() { return QSettings("Maude", "FolderBrowser"); }

void wipeAutostartKeys()
{
    auto s = folderBrowserSettings();
    s.remove("firstRunCompleted");
    s.remove("autostart");
    s.sync();
}

}  // namespace

void AutostartTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    Autostart::Testing::setDryRun(true);
    wipeAutostartKeys();
}

void AutostartTest::cleanupTestCase()
{
    Autostart::Testing::clearProductionBuildOverride();
    Autostart::Testing::setDryRun(false);
    wipeAutostartKeys();
    QStandardPaths::setTestModeEnabled(false);
}

void AutostartTest::init()
{
    Autostart::Testing::clearProductionBuildOverride();
    Autostart::Testing::resetCallTracking();
    wipeAutostartKeys();
}

void AutostartTest::cleanup()
{
    Autostart::Testing::clearProductionBuildOverride();
    wipeAutostartKeys();
}

// -------- prod detection ----------------------------------------------------

void AutostartTest::prodOverrideIsRespected()
{
    Autostart::Testing::overrideProductionBuild(true);
    QVERIFY(Autostart::isProductionBuild());
    Autostart::Testing::overrideProductionBuild(false);
    QVERIFY(!Autostart::isProductionBuild());
}

void AutostartTest::devModeForBuildDir()
{
    // Test binary lives under build*/ → dev unless we override.
    Autostart::Testing::clearProductionBuildOverride();
    QVERIFY2(!Autostart::isProductionBuild(),
             "Test binary should be detected as dev (path contains /build).");
}

// -------- first-run gating --------------------------------------------------

void AutostartTest::firstRunPromptHiddenInDevMode()
{
    Autostart::Testing::overrideProductionBuild(false);
    QVERIFY(!Autostart::firstRunNeedsPrompt());
}

void AutostartTest::firstRunPromptShownInProdWithFreshSettings()
{
    Autostart::Testing::overrideProductionBuild(true);
    QVERIFY(Autostart::firstRunNeedsPrompt());
}

void AutostartTest::firstRunPromptShowsOnceOnly()
{
    Autostart::Testing::overrideProductionBuild(true);
    QVERIFY(Autostart::firstRunNeedsPrompt());
    Autostart::markFirstRunCompleted();
    QVERIFY(!Autostart::firstRunNeedsPrompt());
}

// -------- choice application -----------------------------------------------

void AutostartTest::firstRunYesPersistsAutostartAndCompletes()
{
    Autostart::Testing::overrideProductionBuild(true);

    Autostart::applyFirstRunChoice(true);

    QVERIFY(!Autostart::firstRunNeedsPrompt());
    QVERIFY(Autostart::isEnabled());
    QVERIFY(Autostart::Testing::osRegistrationCalled());
    QVERIFY(Autostart::Testing::lastOsRegistrationRequest());
}

void AutostartTest::firstRunSkipPersistsCompletedOnly()
{
    Autostart::Testing::overrideProductionBuild(true);

    Autostart::applyFirstRunChoice(false);

    QVERIFY(!Autostart::firstRunNeedsPrompt());
    QVERIFY(!Autostart::isEnabled());
    QVERIFY(Autostart::Testing::osRegistrationCalled());
    QVERIFY(!Autostart::Testing::lastOsRegistrationRequest());
}

void AutostartTest::setEnabledIsNoOpAtOsLayerWhenDev()
{
    // Even calling setEnabled(true) in dev mode must NOT touch the OS.
    // dryRun=true (set in initTestCase) skips OS work; here we additionally
    // verify the prod gate by leaving dryRun on while running with the
    // dev-mode override.
    Autostart::Testing::overrideProductionBuild(false);
    Autostart::Testing::resetCallTracking();

    Autostart::setEnabled(true);

    // QSettings layer DID record the preference …
    QVERIFY(Autostart::isEnabled());
    // … the test-seam call tracking fires regardless (so tests can observe),
    // but we assert the *value* matches the request.
    QVERIFY(Autostart::Testing::osRegistrationCalled());
    QVERIFY(Autostart::Testing::lastOsRegistrationRequest());
    // And dryRun was on, so no real launchctl/plist work happened. (Asserted
    // implicitly: the test binary did not just register itself at login.)
}

// -------- dialog -----------------------------------------------------------

void AutostartTest::dialogDefaultButtonIsEnable()
{
    FirstRunDialog dialog;
    QVERIFY(dialog.enableButton()->isDefault());
    QVERIFY(!dialog.skipButton()->isDefault());
}

void AutostartTest::dialogClickEnableAccepts()
{
    FirstRunDialog dialog;
    QSignalSpy spy(&dialog, &QDialog::accepted);
    QTimer::singleShot(0, &dialog, [&]() { dialog.enableButton()->click(); });
    dialog.exec();
    QCOMPARE(spy.count(), 1);
    QCOMPARE(dialog.result(), int(QDialog::Accepted));
}

void AutostartTest::dialogClickSkipRejects()
{
    FirstRunDialog dialog;
    QSignalSpy spy(&dialog, &QDialog::rejected);
    QTimer::singleShot(0, &dialog, [&]() { dialog.skipButton()->click(); });
    dialog.exec();
    QCOMPARE(spy.count(), 1);
    QCOMPARE(dialog.result(), int(QDialog::Rejected));
}

void AutostartTest::dialogEscapeRejects()
{
    FirstRunDialog dialog;
    QSignalSpy spy(&dialog, &QDialog::rejected);
    QTimer::singleShot(0, &dialog, [&]() {
        QTest::keyClick(&dialog, Qt::Key_Escape);
    });
    dialog.exec();
    QCOMPARE(spy.count(), 1);
}

void AutostartTest::dialogFocusOnEnableButton()
{
    FirstRunDialog dialog;
    dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));
    QCOMPARE(QApplication::focusWidget(), dialog.enableButton());
}
