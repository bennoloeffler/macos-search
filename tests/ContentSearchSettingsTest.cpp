#include "ContentSearchSettingsTest.h"
#include "ContentSearchSettings.h"
#include "FileCacheManager.h"

#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QtTest/QtTest>

void ContentSearchSettingsTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    clear();
}

void ContentSearchSettingsTest::cleanupTestCase()
{
    clear();
    QStandardPaths::setTestModeEnabled(false);
}

void ContentSearchSettingsTest::cleanup()
{
    clear();
}

void ContentSearchSettingsTest::clear()
{
    QSettings s;
    s.remove("ContentSearchSettings");
    s.sync();
}

void ContentSearchSettingsTest::testDefaultsAreSensible()
{
    ContentSearchSettings s;
    QCOMPARE(s.threshold(), 1000);
    QCOMPARE(s.maxFileSizeMB(), 5);
    QCOMPARE(s.fileCacheCap(), FileCacheManager::kDefaultSoftCap);
}

void ContentSearchSettingsTest::testFileCacheCapDefaultMatchesFileCacheManager()
{
    QCOMPARE(ContentSearchSettings::defaultFileCacheCap(),
             FileCacheManager::kDefaultSoftCap);
}

void ContentSearchSettingsTest::testFileCacheCapLegacyDefaultMigrates()
{
    // Installs that ever saved settings persisted the old 500000 default;
    // it must not keep truncating the file index after the caps were raised.
    {
        QSettings s;
        s.beginGroup("ContentSearchSettings");
        s.setValue("fileCacheCap", 500000);
        s.endGroup();
        s.sync();
    }
    ContentSearchSettings s;
    QCOMPARE(s.fileCacheCap(), FileCacheManager::kDefaultSoftCap);
}

void ContentSearchSettingsTest::testFileCacheCapCustomValueSurvivesLoad()
{
    {
        QSettings s;
        s.beginGroup("ContentSearchSettings");
        s.setValue("fileCacheCap", 750000);
        s.endGroup();
        s.sync();
    }
    ContentSearchSettings s;
    QCOMPARE(s.fileCacheCap(), 750000);
}

void ContentSearchSettingsTest::testThresholdClampedLow()
{
    ContentSearchSettings s;
    s.setThreshold(10);
    QCOMPARE(s.threshold(), ContentSearchSettings::minThreshold());
}

void ContentSearchSettingsTest::testThresholdClampedHigh()
{
    ContentSearchSettings s;
    s.setThreshold(1000000);
    QCOMPARE(s.threshold(), ContentSearchSettings::maxThreshold());
}

void ContentSearchSettingsTest::testThresholdPersists()
{
    {
        ContentSearchSettings s;
        s.setThreshold(2500);
    }
    ContentSearchSettings s2;
    QCOMPARE(s2.threshold(), 2500);
}

void ContentSearchSettingsTest::testMaxFileSizeMinClamped()
{
    ContentSearchSettings s;
    s.setMaxFileSizeMB(-5);
    QCOMPARE(s.maxFileSizeMB(), 1);
}

void ContentSearchSettingsTest::testFileCacheCapMinClamped()
{
    ContentSearchSettings s;
    s.setFileCacheCap(100);
    QCOMPARE(s.fileCacheCap(), 1000);
}

void ContentSearchSettingsTest::testExtensionBlacklistContainsImageDefaults()
{
    ContentSearchSettings s;
    QVERIFY(s.isExtensionBlacklisted("/x/y.png"));
    QVERIFY(s.isExtensionBlacklisted("/x/y.jpg"));
    QVERIFY(s.isExtensionBlacklisted("/x/y.JPEG"));
}

void ContentSearchSettingsTest::testExtensionBlacklistContainsArchiveDefaults()
{
    ContentSearchSettings s;
    QVERIFY(s.isExtensionBlacklisted("/x/y.zip"));
    QVERIFY(s.isExtensionBlacklisted("/x/y.tar"));
}

void ContentSearchSettingsTest::testIsExtensionBlacklistedCaseInsensitive()
{
    ContentSearchSettings s;
    QVERIFY(s.isExtensionBlacklisted("/x/y.PNG"));
}

void ContentSearchSettingsTest::testIsExtensionBlacklistedFalseForCode()
{
    ContentSearchSettings s;
    QVERIFY(!s.isExtensionBlacklisted("/x/y.cpp"));
    QVERIFY(!s.isExtensionBlacklisted("/x/y.md"));
    QVERIFY(!s.isExtensionBlacklisted("/x/y.txt"));
}

void ContentSearchSettingsTest::testSetExtensionBlacklistDeduplicates()
{
    ContentSearchSettings s;
    s.setExtensionBlacklist({"png", "PNG", ".png", "  png  "});
    QStringList bl = s.extensionBlacklist();
    QCOMPARE(bl.count("png"), 1);
}

void ContentSearchSettingsTest::testResetToDefaultsRestores()
{
    ContentSearchSettings s;
    s.setThreshold(2000);
    s.setExtensionBlacklist({"custom"});
    s.resetToDefaults();
    QCOMPARE(s.threshold(), 1000);
    QVERIFY(s.isExtensionBlacklisted("/x.png"));
}

void ContentSearchSettingsTest::testSettingsChangedSignalEmitted()
{
    ContentSearchSettings s;
    QSignalSpy spy(&s, &ContentSearchSettings::settingsChanged);
    s.setThreshold(2000);
    QCOMPARE(spy.count(), 1);
    s.setMaxFileSizeMB(10);
    QCOMPARE(spy.count(), 2);
}
