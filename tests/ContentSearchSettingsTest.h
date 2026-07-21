#ifndef CONTENTSEARCHSETTINGSTEST_H
#define CONTENTSEARCHSETTINGSTEST_H

#include <QObject>

class ContentSearchSettingsTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void cleanup();

    void testDefaultsAreSensible();
    void testThresholdClampedLow();
    void testThresholdClampedHigh();
    void testThresholdPersists();
    void testMaxFileSizeMinClamped();
    void testFileCacheCapMinClamped();
    // 2026-07-21: the content-search cap default must follow the file
    // cache's real default, and a persisted legacy 500k migrates up.
    void testFileCacheCapDefaultMatchesFileCacheManager();
    void testFileCacheCapLegacyDefaultMigrates();
    void testFileCacheCapCustomValueSurvivesLoad();
    void testExtensionBlacklistContainsImageDefaults();
    void testExtensionBlacklistContainsArchiveDefaults();
    void testIsExtensionBlacklistedCaseInsensitive();
    void testIsExtensionBlacklistedFalseForCode();
    void testSetExtensionBlacklistDeduplicates();
    void testResetToDefaultsRestores();
    void testSettingsChangedSignalEmitted();

private:
    void clear();
};

#endif // CONTENTSEARCHSETTINGSTEST_H
