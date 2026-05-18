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

private:
    void clearSettings();
};

#endif // EXCLUDESETTINGSTEST_H
