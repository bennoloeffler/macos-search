#include "FileSearchUiTest.h"
#include "ContentSearchSettings.h"
#include "ExcludeSettings.h"
#include "ExcludeSettingsDialog.h"
#include "FileCacheManager.h"
#include "FolderBrowserDialog.h"
#include "PathCacheManager.h"

#include <QCheckBox>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTabWidget>
#include <QtTest/QtTest>

namespace {
ExcludeSettings *g_excludeSettings = nullptr;
}

void FileSearchUiTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QSettings s;
    s.clear();
    s.sync();
    if (!g_excludeSettings) {
        g_excludeSettings = new ExcludeSettings();
    }
    PathCacheManager::instance()->setExcludeSettings(g_excludeSettings);
    PathCacheManager::instance()->stopScan();
    FileCacheManager::instance()->clear();
}

void FileSearchUiTest::cleanup()
{
    PathCacheManager::instance()->stopScan();
    FileCacheManager::instance()->clear();
    // Reset the persisted search mode between tests so test order doesn't
    // leak the previous test's choice into the next dialog construction.
    QSettings appSettings("Maude", "FolderBrowser");
    appSettings.remove("searchMode");
    appSettings.sync();
}

void FileSearchUiTest::testMinimumWidthIs820()
{
    FolderBrowserDialog dialog(QDir::homePath());
    QVERIFY(dialog.minimumWidth() >= 820);
}

void FileSearchUiTest::testModeTogglesExistAndPersist()
{
    {
        FolderBrowserDialog dialog(QDir::homePath());
        auto *folders = dialog.findChild<QPushButton *>("modeFolders");
        auto *files = dialog.findChild<QPushButton *>("modeFiles");
        auto *both = dialog.findChild<QPushButton *>("modeBoth");
        QVERIFY(folders);
        QVERIFY(files);
        QVERIFY(both);
        QVERIFY(folders->isCheckable());
        QVERIFY(both->isChecked());

        files->click();
        QVERIFY(files->isChecked());
        QCOMPARE(dialog.searchMode(), FolderBrowserDialog::SearchMode::Files);
    }
    // New instance should restore the saved mode.
    FolderBrowserDialog dialog2(QDir::homePath());
    QCOMPARE(dialog2.searchMode(), FolderBrowserDialog::SearchMode::Files);
}

void FileSearchUiTest::testDefaultModeIsBoth()
{
    // The dialog persists mode via QSettings("Maude", "FolderBrowser").
    QSettings appSettings("Maude", "FolderBrowser");
    appSettings.remove("searchMode");
    appSettings.sync();
    FolderBrowserDialog dialog(QDir::homePath());
    QCOMPARE(dialog.searchMode(), FolderBrowserDialog::SearchMode::Both);
}

void FileSearchUiTest::testContentFieldExists()
{
    FolderBrowserDialog dialog(QDir::homePath());
    QVERIFY(dialog.findChild<QLineEdit *>("contentField"));
}

void FileSearchUiTest::testContentRegexCheckboxExists()
{
    FolderBrowserDialog dialog(QDir::homePath());
    auto *cb = dialog.findChild<QCheckBox *>("contentRegex");
    QVERIFY(cb);
    QVERIFY(!cb->isChecked());  // default off
}

void FileSearchUiTest::testContentHelpButtonExists()
{
    FolderBrowserDialog dialog(QDir::homePath());
    QVERIFY(dialog.findChild<QPushButton *>("contentHelpButton"));
}

void FileSearchUiTest::testContentFieldDisabledByDefault()
{
    FolderBrowserDialog dialog(QDir::homePath());
    auto *field = dialog.findChild<QLineEdit *>("contentField");
    QVERIFY(field);
    // No filename query yet → content field disabled.
    QVERIFY(!field->isEnabled());
}

void FileSearchUiTest::testContentFieldDisabledWhenAboveThreshold()
{
    // Populate cache with enough files that any query would exceed the
    // threshold; the content field should remain disabled.
    auto *fc = FileCacheManager::instance();
    fc->clear();
    for (int i = 0; i < 2000; ++i) {
        fc->addFile(QString("/test/foo-%1.txt").arg(i));
    }

    FolderBrowserDialog dialog(QDir::homePath());
    auto *searchField = dialog.findChild<QLineEdit *>("searchField");
    QVERIFY(searchField);
    searchField->setText("foo");
    QTest::qWait(200);  // let workers debounce + emit

    auto *contentField = dialog.findChild<QLineEdit *>("contentField");
    QVERIFY(contentField);
    QVERIFY(!contentField->isEnabled());
}

void FileSearchUiTest::testHelpHintUpdatesWithMode()
{
    FolderBrowserDialog dialog(QDir::homePath());
    auto *hint = dialog.findChild<QLabel *>("contentHintLabel");
    QVERIFY(hint);
    QVERIFY(!hint->text().isEmpty());

    auto *folders = dialog.findChild<QPushButton *>("modeFolders");
    folders->click();
    auto *searchField = dialog.findChild<QLineEdit *>("searchField");
    searchField->setText("foo");
    QTest::qWait(150);
    QVERIFY(hint->text().contains("Files") || hint->text().contains("Both"));
}

void FileSearchUiTest::testHelpLineMentionsAltEnter()
{
    FolderBrowserDialog dialog(QDir::homePath());
    auto *hint = dialog.findChild<QLabel *>("shortcutsHint");
    QVERIFY(hint);
    QVERIFY(hint->text().contains("editor"));
}

void FileSearchUiTest::testTwoTabExcludeDialog()
{
    ExcludeSettings settings;
    ExcludeSettingsDialog dialog(&settings);
    auto *tabs = dialog.findChild<QTabWidget *>("excludeTabs");
    QVERIFY(tabs);
    QCOMPARE(tabs->count(), 2);
    QCOMPARE(tabs->tabText(0), QString("Folders"));
    QCOMPARE(tabs->tabText(1), QString("Files"));

    QVERIFY(dialog.findChild<QListWidget *>("folderPatternList"));
    QVERIFY(dialog.findChild<QListWidget *>("filePatternList"));

    dialog.setCurrentScope(ExcludeSettingsDialog::Scope::Files);
    QCOMPARE(dialog.currentScope(), ExcludeSettingsDialog::Scope::Files);
}
