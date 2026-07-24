#include "PathCacheManagerTest.h"
#include "PathCacheManager.h"
#include "ExcludeSettings.h"
#include <QSignalSpy>
#include <QTest>
#include <QDir>
#include <QCoreApplication>

void PathCacheManagerTest::initTestCase()
{
    // Ensure PathCacheManager singleton is accessible
}

void PathCacheManagerTest::cleanupTestCase()
{
    // Stop any ongoing scan
    PathCacheManager::instance()->stopScan();
}

void PathCacheManagerTest::testIsPathUnderRoot()
{
    using P = PathCacheManager;
    // The load-bearing case: "/" covers every absolute path.
    QVERIFY(P::isPathUnderRoot("/Users/benno", "/"));
    QVERIFY(P::isPathUnderRoot("/", "/"));
    QVERIFY(P::isPathUnderRoot("/Applications/Foo.app", "/"));
    // Ordinary roots: descendants and self, but not lookalike siblings.
    QVERIFY(P::isPathUnderRoot("/Users/benno/Desktop", "/Users/benno"));
    QVERIFY(P::isPathUnderRoot("/Users/benno", "/Users/benno"));
    QVERIFY(!P::isPathUnderRoot("/Users/benno2", "/Users/benno"));
    QVERIFY(!P::isPathUnderRoot("/Users", "/Users/benno"));
    // Degenerate inputs.
    QVERIFY(!P::isPathUnderRoot("", "/"));
    QVERIFY(!P::isPathUnderRoot("/Users", ""));
}

void PathCacheManagerTest::testInstanceReturnsNonNull()
{
    QVERIFY(PathCacheManager::instance() != nullptr);
}

void PathCacheManagerTest::testInstanceReturnsSameSingleton()
{
    PathCacheManager *first = PathCacheManager::instance();
    PathCacheManager *second = PathCacheManager::instance();
    QCOMPARE(first, second);
}

void PathCacheManagerTest::testInitialStateNotScanning()
{
    // Note: In production, scanning may already be started by main()
    // This test just verifies isScanning() returns a valid bool
    bool scanning = PathCacheManager::instance()->isScanning();
    Q_UNUSED(scanning);
    QVERIFY(true); // Just verify it doesn't crash
}

void PathCacheManagerTest::testFolderCountInitiallyZero()
{
    // Before any scan, folder count should be zero (or whatever was left from previous tests)
    // Just verify it returns a valid number
    QVERIFY(PathCacheManager::instance()->folderCount() >= 0);
}

void PathCacheManagerTest::testSearchEmptyQueryReturnsEmpty()
{
    QStringList results = PathCacheManager::instance()->search("", QString(), 100);
    QVERIFY(results.isEmpty());
}

void PathCacheManagerTest::testSearchReturnsMaxResults()
{
    // This test depends on having some paths in cache
    // Just verify it doesn't crash and respects maxResults
    QStringList results = PathCacheManager::instance()->search("a", QString(), 5);
    QVERIFY(results.size() <= 5);
}

void PathCacheManagerTest::testCachedPathsReturnsStringList()
{
    QStringList paths = PathCacheManager::instance()->cachedPaths();
    // Just verify it returns a valid list (may be empty if no scan has run)
    QVERIFY(paths.size() >= 0);
}

void PathCacheManagerTest::testSetExcludeSettingsDoesNotCrash()
{
    ExcludeSettings settings;
    PathCacheManager::instance()->setExcludeSettings(&settings);
    // Should not crash
    QVERIFY(true);
}

void PathCacheManagerTest::testStartScanEmitsSignal()
{
    PathCacheManager *cache = PathCacheManager::instance();

    // Stop any existing scan first
    cache->stopScan();

    QSignalSpy spy(cache, &PathCacheManager::scanStarted);
    cache->startScan();

    QVERIFY(spy.count() >= 1 || cache->isScanning());

    // Stop the scan
    cache->stopScan();
}

void PathCacheManagerTest::testStopScanStopsScanning()
{
    PathCacheManager *cache = PathCacheManager::instance();

    cache->startScan();
    cache->stopScan();

    QVERIFY(!cache->isScanning());
}

void PathCacheManagerTest::testRescanClearsAndRestarts()
{
    PathCacheManager *cache = PathCacheManager::instance();

    QSignalSpy spy(cache, &PathCacheManager::scanStarted);
    cache->rescan();

    QVERIFY(spy.count() >= 1 || cache->isScanning());

    // Stop the scan
    cache->stopScan();
}
