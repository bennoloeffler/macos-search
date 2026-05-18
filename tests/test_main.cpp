// Aggregate test runner for the standalone macos-search app.
//
// Convention copied from ../maude-cp-v3/tests/test_main.cpp: each test class
// is its own QObject; we instantiate them in sequence and OR their results
// into `status`. Single binary, single process, fail-fast disabled so all
// failures surface at once.
//
// Run all:    ./build*/macos-search_tests
// Run one:    ./build*/macos-search_tests --filter PathCacheManagerTest

#include <QApplication>
#include <QString>
#include <QtTest/QtTest>
#include <vector>

#include "ExcludeSettingsTest.h"
#include "PathCacheManagerTest.h"
#include "SearchFieldTest.h"
#include "PathSelectorStateTest.h"
#include "FolderBrowserDialogTest.h"
#include "UserInteractionTest.h"
#include "UsabilityTest.h"
#include "CacheStrategyTest.h"
#include "AutostartTest.h"
#include "GlobalHotkeyTest.h"
#include "PreferencesDialogTest.h"

#define RUN_TEST(TestClass, varName) \
    if (testFilter.isEmpty() || QString(#TestClass) == testFilter) \
    { TestClass varName; status |= QTest::qExec(&varName, qtArgs.size(), qtArgs.data()); }

int main(int argc, char *argv[])
{
    // Run headless on the offscreen platform so QWidget focus + key events
    // behave deterministically regardless of whether the test process is
    // foregrounded by macOS. To debug visually, run with
    //   QT_QPA_PLATFORM= ./build*/macos-search_tests
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QApplication::setAttribute(Qt::AA_DontShowIconsInMenus);

    QApplication app(argc, argv);

    // Stable per-user QSettings location for tests, separate from a real run.
    QCoreApplication::setOrganizationName("v-und-s");
    QCoreApplication::setOrganizationDomain("v-und-s.de");
    QCoreApplication::setApplicationName("macos-search-tests");

    // Parse --filter <ClassName> (everything else passes through to QTest).
    QString testFilter;
    std::vector<char *> qtArgs;
    qtArgs.push_back(argv[0]);
    for (int i = 1; i < argc; ++i) {
        QString a = QString::fromLocal8Bit(argv[i]);
        if (a == "--filter" && i + 1 < argc) {
            testFilter = QString::fromLocal8Bit(argv[++i]);
            continue;
        }
        qtArgs.push_back(argv[i]);
    }

    int status = 0;
    RUN_TEST(ExcludeSettingsTest, t1);
    RUN_TEST(PathCacheManagerTest, t2);
    RUN_TEST(SearchFieldTest, t3);
    RUN_TEST(PathSelectorStateTest, t4);
    RUN_TEST(FolderBrowserDialogTest, t5);
    RUN_TEST(UserInteractionTest, t6);
    RUN_TEST(UsabilityTest, t7);
    RUN_TEST(CacheStrategyTest, t8);
    RUN_TEST(AutostartTest, t9);
    RUN_TEST(GlobalHotkeyTest, t10);
    RUN_TEST(PreferencesDialogTest, t11);

    return status;
}
