#ifndef EXCLUDESETTINGSTEST_H
#define EXCLUDESETTINGSTEST_H

#include <QObject>

class ExcludeSettingsTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void cleanup();

    // Default patterns tests
    void testDefaultPatternsNotEmpty();
    void testDefaultPatternsContainsNodeModules();
    void testDefaultPatternsContainsGit();
    void testDefaultPatternsContainsVenv();
    // 2026-07-21: package registries / tool caches joined the defaults.
    void testDefaultPatternsContainToolCaches();
    void testUpgradeMergesNewDefaultsIntoSavedSettings();
    void testUpgradeKeepsUserAddedAndDisabledPatterns();

    // Pattern management tests
    void testAllPatternsReturnsAllPatterns();
    void testEnabledPatternsReturnsOnlyEnabled();
    void testAddPatternAddsNewPattern();
    void testAddPatternIgnoresEmpty();
    void testAddPatternIgnoresDuplicates();
    void testRemovePatternRemovesPattern();
    void testRemovePatternDoesNothingForMissing();

    // Enable/disable tests
    void testIsPatternEnabledReturnsTrue();
    void testIsPatternEnabledReturnsFalse();
    void testSetPatternEnabledEnablesPattern();
    void testSetPatternEnabledDisablesPattern();
    void testSetPatternEnabledDoesNothingForMissing();

    // shouldExclude tests
    void testShouldExcludeExactMatch();
    void testShouldExcludeExactMatchCaseInsensitive();
    void testShouldExcludeReturnsFalseForNonMatch();
    void testShouldExcludeWildcardPrefix();
    void testShouldExcludeWildcardSuffix();
    void testShouldExcludeWildcardContains();
    void testShouldExcludeDisabledPatternNotExcluded();

    // Reset tests
    void testResetToDefaultsRestoresPatterns();
    void testResetToDefaultsEnablesAll();

    // Signal tests
    void testPatternsChangedEmittedOnAdd();
    void testPatternsChangedEmittedOnRemove();
    void testPatternsChangedEmittedOnEnable();
    void testPatternsChangedEmittedOnReset();

    // File-pattern tests (file-search v1)
    void testDefaultFilePatternsNotEmpty();
    void testDefaultFilePatternsContainsDsStore();
    void testDefaultFilePatternsContainsPyc();
    void testFilePatternsLoadDefaultsOnFreshConfig();
    void testAddFilePatternAddsAndEnables();
    void testAddFilePatternIgnoresEmpty();
    void testAddFilePatternIgnoresDuplicates();
    void testRemoveFilePatternRemoves();
    void testSetFilePatternEnabledToggles();
    void testShouldExcludeFileExactMatch();
    void testShouldExcludeFileWildcardSuffix();
    void testShouldExcludeFileCaseInsensitive();
    void testShouldExcludeFileRespectsDisabled();
    void testResetFilePatternsRestores();
    void testFilePatternsChangedSignalEmitted();
    void testFolderAndFilePatternsAreIndependent();

    // INI migration tests
    void testLegacyPatternsMigratedToFolderPatterns();
    void testLegacyAliasKeptForOneRelease();
    void testNewFolderPatternsKeyTakesPriorityOverLegacy();
    void testFilePatternsLoadFreshDefaultsWhenAbsent();

private:
    void clearSettings();
};

#endif // EXCLUDESETTINGSTEST_H
