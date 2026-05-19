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
