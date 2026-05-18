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
