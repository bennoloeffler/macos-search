#include "ExcludeSettingsTest.h"
#include "ExcludeSettings.h"
#include <QtTest/QtTest>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>

void ExcludeSettingsTest::initTestCase()
{
    // Use test location to avoid polluting real config
    QStandardPaths::setTestModeEnabled(true);

    // Clear any existing settings to ensure clean test environment
    clearSettings();
}

void ExcludeSettingsTest::cleanupTestCase()
{
    // Clean up settings after all tests
    clearSettings();

    QStandardPaths::setTestModeEnabled(false);
}

void ExcludeSettingsTest::cleanup()
{
    // Reset settings between tests
    clearSettings();
}

void ExcludeSettingsTest::clearSettings()
{
    // Use the default QSettings constructor — matches the standalone app's
    // ExcludeSettings, which no longer routes through MaudeConfig.
    QSettings settings;
    settings.remove("ExcludeSettings");
    settings.sync();
}

// Default patterns tests

void ExcludeSettingsTest::testDefaultPatternsNotEmpty()
{
    QStringList defaults = ExcludeSettings::defaultPatterns();
    QVERIFY(!defaults.isEmpty());
}

void ExcludeSettingsTest::testDefaultPatternsContainsNodeModules()
{
    QStringList defaults = ExcludeSettings::defaultPatterns();
    QVERIFY(defaults.contains("node_modules"));
}

void ExcludeSettingsTest::testDefaultPatternsContainsGit()
{
    QStringList defaults = ExcludeSettings::defaultPatterns();
    QVERIFY(defaults.contains(".git"));
}

void ExcludeSettingsTest::testDefaultPatternsContainsVenv()
{
    QStringList defaults = ExcludeSettings::defaultPatterns();
    QVERIFY(defaults.contains(".venv") || defaults.contains("venv"));
}

void ExcludeSettingsTest::testDefaultPatternsContainToolCaches()
{
    const QStringList defaults = ExcludeSettings::defaultPatterns();
    // Spot-check the 2026-07-21 additions: package registries and tool
    // caches that bloat a dev home by hundreds of thousands of entries.
    for (const char *p : { ".cargo", ".rustup", ".m2", ".nvm", ".android",
                           ".mypy_cache", ".pytest_cache", ".cpcache",
                           ".next", "Pods", ".svn", ".dropbox.cache" }) {
        QVERIFY2(defaults.contains(QLatin1String(p)),
                 qPrintable(QString("missing default pattern: %1").arg(p)));
    }
}

void ExcludeSettingsTest::testUpgradeMergesNewDefaultsIntoSavedSettings()
{
    // Simulate an existing install: saved folder patterns WITHOUT the new
    // defaults and no defaults-version marker.
    {
        QSettings s;
        s.beginGroup("ExcludeSettings");
        s.setValue("folderPatterns", QStringList{"node_modules", ".git"});
        s.setValue("enabledFolderPatterns", QStringList{"node_modules", ".git"});
        s.endGroup();
        s.sync();
    }

    ExcludeSettings settings;
    QVERIFY(settings.allPatterns().contains(".cargo"));
    QVERIFY(settings.isPatternEnabled(".cargo"));
    QVERIFY(settings.shouldExclude(".cargo"));
}

void ExcludeSettingsTest::testUpgradeKeepsUserAddedAndDisabledPatterns()
{
    {
        QSettings s;
        s.beginGroup("ExcludeSettings");
        // User added "my-secret-stuff" and disabled ".git".
        s.setValue("folderPatterns",
                   QStringList{"node_modules", ".git", "my-secret-stuff"});
        s.setValue("enabledFolderPatterns",
                   QStringList{"node_modules", "my-secret-stuff"});
        s.endGroup();
        s.sync();
    }

    ExcludeSettings settings;
    QVERIFY(settings.allPatterns().contains("my-secret-stuff"));
    QVERIFY(settings.isPatternEnabled("my-secret-stuff"));
    QVERIFY(!settings.isPatternEnabled(".git"));  // user's choice survives
    QVERIFY(settings.allPatterns().contains(".rustup"));  // new default merged
}

// Pattern management tests

void ExcludeSettingsTest::testAllPatternsReturnsAllPatterns()
{
    ExcludeSettings settings;
    QStringList all = settings.allPatterns();
    // Should contain defaults
    QVERIFY(all.contains("node_modules"));
    QVERIFY(all.contains(".git"));
}

void ExcludeSettingsTest::testEnabledPatternsReturnsOnlyEnabled()
{
    ExcludeSettings settings;
    // By default all are enabled
    QStringList enabled = settings.enabledPatterns();
    QStringList all = settings.allPatterns();
    QCOMPARE(enabled.size(), all.size());

    // Disable one pattern
    settings.setPatternEnabled("node_modules", false);
    enabled = settings.enabledPatterns();
    QVERIFY(!enabled.contains("node_modules"));
    QCOMPARE(enabled.size(), all.size() - 1);
}

void ExcludeSettingsTest::testAddPatternAddsNewPattern()
{
    ExcludeSettings settings;
    int initialCount = settings.allPatterns().size();

    settings.addPattern("custom_pattern");

    QCOMPARE(settings.allPatterns().size(), initialCount + 1);
    QVERIFY(settings.allPatterns().contains("custom_pattern"));
}

void ExcludeSettingsTest::testAddPatternIgnoresEmpty()
{
    ExcludeSettings settings;
    int initialCount = settings.allPatterns().size();

    settings.addPattern("");
    settings.addPattern("   ");

    QCOMPARE(settings.allPatterns().size(), initialCount);
}

void ExcludeSettingsTest::testAddPatternIgnoresDuplicates()
{
    ExcludeSettings settings;
    int initialCount = settings.allPatterns().size();

    settings.addPattern("node_modules");

    QCOMPARE(settings.allPatterns().size(), initialCount);
}

void ExcludeSettingsTest::testRemovePatternRemovesPattern()
{
    ExcludeSettings settings;
    QVERIFY(settings.allPatterns().contains("node_modules"));

    settings.removePattern("node_modules");

    QVERIFY(!settings.allPatterns().contains("node_modules"));
}

void ExcludeSettingsTest::testRemovePatternDoesNothingForMissing()
{
    ExcludeSettings settings;
    int initialCount = settings.allPatterns().size();

    settings.removePattern("nonexistent_pattern");

    QCOMPARE(settings.allPatterns().size(), initialCount);
}

// Enable/disable tests

void ExcludeSettingsTest::testIsPatternEnabledReturnsTrue()
{
    ExcludeSettings settings;
    // By default all patterns are enabled
    QVERIFY(settings.isPatternEnabled("node_modules"));
}

void ExcludeSettingsTest::testIsPatternEnabledReturnsFalse()
{
    ExcludeSettings settings;
    settings.setPatternEnabled("node_modules", false);
    QVERIFY(!settings.isPatternEnabled("node_modules"));
}

void ExcludeSettingsTest::testSetPatternEnabledEnablesPattern()
{
    ExcludeSettings settings;
    settings.setPatternEnabled("node_modules", false);
    QVERIFY(!settings.isPatternEnabled("node_modules"));

    settings.setPatternEnabled("node_modules", true);
    QVERIFY(settings.isPatternEnabled("node_modules"));
}

void ExcludeSettingsTest::testSetPatternEnabledDisablesPattern()
{
    ExcludeSettings settings;
    QVERIFY(settings.isPatternEnabled("node_modules"));

    settings.setPatternEnabled("node_modules", false);
    QVERIFY(!settings.isPatternEnabled("node_modules"));
}

void ExcludeSettingsTest::testSetPatternEnabledDoesNothingForMissing()
{
    ExcludeSettings settings;
    // Should not crash or add pattern
    settings.setPatternEnabled("nonexistent", true);
    QVERIFY(!settings.allPatterns().contains("nonexistent"));
}

// shouldExclude tests

void ExcludeSettingsTest::testShouldExcludeExactMatch()
{
    ExcludeSettings settings;
    QVERIFY(settings.shouldExclude("node_modules"));
}

void ExcludeSettingsTest::testShouldExcludeExactMatchCaseInsensitive()
{
    ExcludeSettings settings;
    QVERIFY(settings.shouldExclude("NODE_MODULES"));
    QVERIFY(settings.shouldExclude("Node_Modules"));
}

void ExcludeSettingsTest::testShouldExcludeReturnsFalseForNonMatch()
{
    ExcludeSettings settings;
    QVERIFY(!settings.shouldExclude("my_folder"));
    QVERIFY(!settings.shouldExclude("src"));
}

void ExcludeSettingsTest::testShouldExcludeWildcardPrefix()
{
    ExcludeSettings settings;
    settings.addPattern("*.bak");

    QVERIFY(settings.shouldExclude("file.bak"));
    QVERIFY(settings.shouldExclude("data.bak"));
    QVERIFY(!settings.shouldExclude("backup"));
}

void ExcludeSettingsTest::testShouldExcludeWildcardSuffix()
{
    ExcludeSettings settings;
    settings.addPattern("temp*");

    QVERIFY(settings.shouldExclude("temp"));
    QVERIFY(settings.shouldExclude("temporary"));
    QVERIFY(settings.shouldExclude("tempfiles"));
    QVERIFY(!settings.shouldExclude("mytemp"));
}

void ExcludeSettingsTest::testShouldExcludeWildcardContains()
{
    ExcludeSettings settings;
    settings.addPattern("*cache*");

    QVERIFY(settings.shouldExclude("cache"));
    QVERIFY(settings.shouldExclude("mycache"));
    QVERIFY(settings.shouldExclude("cache_dir"));
    QVERIFY(settings.shouldExclude("my_cache_folder"));
    QVERIFY(!settings.shouldExclude("cach"));
}

void ExcludeSettingsTest::testShouldExcludeDisabledPatternNotExcluded()
{
    ExcludeSettings settings;
    QVERIFY(settings.shouldExclude("node_modules"));

    settings.setPatternEnabled("node_modules", false);
    QVERIFY(!settings.shouldExclude("node_modules"));
}

// Reset tests

void ExcludeSettingsTest::testResetToDefaultsRestoresPatterns()
{
    ExcludeSettings settings;
    settings.removePattern("node_modules");
    settings.addPattern("custom");
    QVERIFY(!settings.allPatterns().contains("node_modules"));
    QVERIFY(settings.allPatterns().contains("custom"));

    settings.resetToDefaults();

    QVERIFY(settings.allPatterns().contains("node_modules"));
    QVERIFY(!settings.allPatterns().contains("custom"));
}

void ExcludeSettingsTest::testResetToDefaultsEnablesAll()
{
    ExcludeSettings settings;
    settings.setPatternEnabled("node_modules", false);
    settings.setPatternEnabled(".git", false);

    settings.resetToDefaults();

    QVERIFY(settings.isPatternEnabled("node_modules"));
    QVERIFY(settings.isPatternEnabled(".git"));
}

// Signal tests

void ExcludeSettingsTest::testPatternsChangedEmittedOnAdd()
{
    ExcludeSettings settings;
    QSignalSpy spy(&settings, &ExcludeSettings::patternsChanged);

    settings.addPattern("new_pattern");

    QCOMPARE(spy.count(), 1);
}

void ExcludeSettingsTest::testPatternsChangedEmittedOnRemove()
{
    ExcludeSettings settings;
    QSignalSpy spy(&settings, &ExcludeSettings::patternsChanged);

    settings.removePattern("node_modules");

    QCOMPARE(spy.count(), 1);
}

void ExcludeSettingsTest::testPatternsChangedEmittedOnEnable()
{
    ExcludeSettings settings;
    QSignalSpy spy(&settings, &ExcludeSettings::patternsChanged);

    settings.setPatternEnabled("node_modules", false);

    QCOMPARE(spy.count(), 1);
}

void ExcludeSettingsTest::testPatternsChangedEmittedOnReset()
{
    ExcludeSettings settings;
    QSignalSpy spy(&settings, &ExcludeSettings::patternsChanged);

    settings.resetToDefaults();

    QCOMPARE(spy.count(), 1);
}

// =====================================================================
// File-pattern tests (file-search v1)
// =====================================================================

void ExcludeSettingsTest::testDefaultFilePatternsNotEmpty()
{
    QVERIFY(!ExcludeSettings::defaultFilePatterns().isEmpty());
}

void ExcludeSettingsTest::testDefaultFilePatternsContainsDsStore()
{
    QVERIFY(ExcludeSettings::defaultFilePatterns().contains(".DS_Store"));
}

void ExcludeSettingsTest::testDefaultFilePatternsContainsPyc()
{
    QVERIFY(ExcludeSettings::defaultFilePatterns().contains("*.pyc"));
}

void ExcludeSettingsTest::testFilePatternsLoadDefaultsOnFreshConfig()
{
    ExcludeSettings settings;
    QStringList all = settings.allFilePatterns();
    QVERIFY(all.contains(".DS_Store"));
    QVERIFY(all.contains("*.pyc"));
}

void ExcludeSettingsTest::testAddFilePatternAddsAndEnables()
{
    ExcludeSettings settings;
    int initial = settings.allFilePatterns().size();
    settings.addFilePattern("*.bak");
    QCOMPARE(settings.allFilePatterns().size(), initial + 1);
    QVERIFY(settings.isFilePatternEnabled("*.bak"));
}

void ExcludeSettingsTest::testAddFilePatternIgnoresEmpty()
{
    ExcludeSettings settings;
    int initial = settings.allFilePatterns().size();
    settings.addFilePattern("");
    settings.addFilePattern("   ");
    QCOMPARE(settings.allFilePatterns().size(), initial);
}

void ExcludeSettingsTest::testAddFilePatternIgnoresDuplicates()
{
    ExcludeSettings settings;
    int initial = settings.allFilePatterns().size();
    settings.addFilePattern(".DS_Store");
    QCOMPARE(settings.allFilePatterns().size(), initial);
}

void ExcludeSettingsTest::testRemoveFilePatternRemoves()
{
    ExcludeSettings settings;
    QVERIFY(settings.allFilePatterns().contains(".DS_Store"));
    settings.removeFilePattern(".DS_Store");
    QVERIFY(!settings.allFilePatterns().contains(".DS_Store"));
}

void ExcludeSettingsTest::testSetFilePatternEnabledToggles()
{
    ExcludeSettings settings;
    settings.setFilePatternEnabled(".DS_Store", false);
    QVERIFY(!settings.isFilePatternEnabled(".DS_Store"));
    settings.setFilePatternEnabled(".DS_Store", true);
    QVERIFY(settings.isFilePatternEnabled(".DS_Store"));
}

void ExcludeSettingsTest::testShouldExcludeFileExactMatch()
{
    ExcludeSettings settings;
    QVERIFY(settings.shouldExcludeFile(".DS_Store"));
}

void ExcludeSettingsTest::testShouldExcludeFileWildcardSuffix()
{
    ExcludeSettings settings;
    QVERIFY(settings.shouldExcludeFile("foo.pyc"));
    QVERIFY(settings.shouldExcludeFile("module.class"));
    QVERIFY(!settings.shouldExcludeFile("foo.py"));
}

void ExcludeSettingsTest::testShouldExcludeFileCaseInsensitive()
{
    ExcludeSettings settings;
    QVERIFY(settings.shouldExcludeFile(".ds_store"));
    QVERIFY(settings.shouldExcludeFile("FOO.PYC"));
}

void ExcludeSettingsTest::testShouldExcludeFileRespectsDisabled()
{
    ExcludeSettings settings;
    settings.setFilePatternEnabled(".DS_Store", false);
    QVERIFY(!settings.shouldExcludeFile(".DS_Store"));
}

void ExcludeSettingsTest::testResetFilePatternsRestores()
{
    ExcludeSettings settings;
    settings.removeFilePattern(".DS_Store");
    settings.addFilePattern("custom");
    QVERIFY(!settings.allFilePatterns().contains(".DS_Store"));

    settings.resetFilePatternsToDefaults();

    QVERIFY(settings.allFilePatterns().contains(".DS_Store"));
    QVERIFY(!settings.allFilePatterns().contains("custom"));
}

void ExcludeSettingsTest::testFilePatternsChangedSignalEmitted()
{
    ExcludeSettings settings;
    QSignalSpy spy(&settings, &ExcludeSettings::filePatternsChanged);
    settings.addFilePattern("foo");
    QCOMPARE(spy.count(), 1);
    settings.setFilePatternEnabled("foo", false);
    QCOMPARE(spy.count(), 2);
    settings.removeFilePattern("foo");
    QCOMPARE(spy.count(), 3);
}

void ExcludeSettingsTest::testFolderAndFilePatternsAreIndependent()
{
    ExcludeSettings settings;
    // node_modules is a folder pattern.
    QVERIFY(settings.shouldExclude("node_modules"));
    QVERIFY(!settings.shouldExcludeFile("node_modules"));
    // .DS_Store is a file pattern.
    QVERIFY(!settings.shouldExclude(".DS_Store"));
    QVERIFY(settings.shouldExcludeFile(".DS_Store"));
}

// =====================================================================
// INI migration tests
// =====================================================================

void ExcludeSettingsTest::testLegacyPatternsMigratedToFolderPatterns()
{
    // Seed legacy keys directly.
    clearSettings();
    {
        QSettings s;
        s.beginGroup("ExcludeSettings");
        s.setValue("patterns", QStringList{ "node_modules", ".git", "custom_legacy" });
        s.setValue("enabledPatterns", QStringList{ "node_modules", ".git" });
        s.endGroup();
        s.sync();
    }

    ExcludeSettings settings;
    QVERIFY(settings.allPatterns().contains("custom_legacy"));
    QVERIFY(settings.isPatternEnabled("node_modules"));
    QVERIFY(settings.isPatternEnabled(".git"));
    QVERIFY(!settings.isPatternEnabled("custom_legacy"));
}

void ExcludeSettingsTest::testLegacyAliasKeptForOneRelease()
{
    // After save we should write both `folderPatterns` and `patterns`.
    {
        ExcludeSettings settings;
        settings.addPattern("alias_test");
    }
    QSettings s;
    s.beginGroup("ExcludeSettings");
    QStringList legacy = s.value("patterns").toStringList();
    QStringList modern = s.value("folderPatterns").toStringList();
    s.endGroup();
    QVERIFY(legacy.contains("alias_test"));
    QVERIFY(modern.contains("alias_test"));
}

void ExcludeSettingsTest::testNewFolderPatternsKeyTakesPriorityOverLegacy()
{
    clearSettings();
    {
        QSettings s;
        s.beginGroup("ExcludeSettings");
        s.setValue("patterns", QStringList{ "old_pattern" });
        s.setValue("enabledPatterns", QStringList{ "old_pattern" });
        s.setValue("folderPatterns", QStringList{ "new_pattern" });
        s.setValue("enabledFolderPatterns", QStringList{ "new_pattern" });
        s.endGroup();
        s.sync();
    }
    ExcludeSettings settings;
    QVERIFY(settings.allPatterns().contains("new_pattern"));
    QVERIFY(!settings.allPatterns().contains("old_pattern"));
}

void ExcludeSettingsTest::testFilePatternsLoadFreshDefaultsWhenAbsent()
{
    clearSettings();
    {
        QSettings s;
        s.beginGroup("ExcludeSettings");
        s.setValue("folderPatterns", QStringList{ "node_modules" });
        s.setValue("enabledFolderPatterns", QStringList{ "node_modules" });
        // Intentionally omit filePatterns.
        s.endGroup();
        s.sync();
    }
    ExcludeSettings settings;
    QVERIFY(settings.allFilePatterns().contains(".DS_Store"));
}
