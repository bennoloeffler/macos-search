#include "CacheStrategyTest.h"

#include "ExcludeSettings.h"
#include "FileCacheManager.h"
#include "PathCacheManager.h"

#include <QDir>
#include <QFile>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

namespace {

bool anyPathStartsWith(const QStringList &paths, const QString &prefix)
{
    for (const QString &p : paths) {
        if (p == prefix || p.startsWith(prefix + '/')) return true;
    }
    return false;
}

// Wait up to `ms` for the scan to finish; pump events while waiting.
void waitForScanComplete(PathCacheManager *cache, int ms = 5000)
{
    QSignalSpy spy(cache, &PathCacheManager::scanComplete);
    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + ms;
    while (cache->isScanning() && QDateTime::currentMSecsSinceEpoch() < deadline) {
        QTest::qWait(50);
    }
    Q_UNUSED(spy);
}

}  // namespace

namespace { ExcludeSettings *g_excludeSettings = nullptr; }

void CacheStrategyTest::initTestCase()
{
    static ExcludeSettings settings;
    g_excludeSettings = &settings;
    PathCacheManager::instance()->setExcludeSettings(&settings);
}

void CacheStrategyTest::cleanupTestCase()
{
    PathCacheManager::instance()->stopScan();
}

void CacheStrategyTest::init()
{
    PathCacheManager::instance()->stopScan();
    PathCacheManager::instance()->rescan();   // clear m_paths / m_pathSet
    QTest::qWait(20);
}

// ---------------------------------------------------------------------------
// Path-level system excludes
// ---------------------------------------------------------------------------

void CacheStrategyTest::systemPathIsNotInCacheAfterScan()
{
    // Scan from `/`, wait briefly, assert /System never made it in.
    // Even on a slow scan, /System is always near the top of /'s listing,
    // so this fires reliably within a few hundred ms.
    auto *cache = PathCacheManager::instance();
    cache->expandTo(QStringLiteral("/"));
    QTest::qWait(800);
    cache->stopScan();
    QStringList all = cache->cachedPaths();
    QVERIFY2(!anyPathStartsWith(all, QStringLiteral("/System")),
             qPrintable(QString("Cache contains /System paths; first few: ")
                            + all.filter("/System").mid(0, 3).join(", ")));
}

void CacheStrategyTest::privatePathIsNotInCacheAfterScan()
{
    auto *cache = PathCacheManager::instance();
    cache->expandTo(QStringLiteral("/"));
    QTest::qWait(800);
    cache->stopScan();
    QVERIFY(!anyPathStartsWith(cache->cachedPaths(), QStringLiteral("/private")));
}

void CacheStrategyTest::devPathIsNotInCacheAfterScan()
{
    auto *cache = PathCacheManager::instance();
    cache->expandTo(QStringLiteral("/"));
    QTest::qWait(800);
    cache->stopScan();
    QVERIFY(!anyPathStartsWith(cache->cachedPaths(), QStringLiteral("/dev")));
}

void CacheStrategyTest::volumesPathIsNotInCacheAfterScan()
{
    auto *cache = PathCacheManager::instance();
    cache->expandTo(QStringLiteral("/"));
    QTest::qWait(800);
    cache->stopScan();
    // /Volumes itself might exist as a folder root, but its contents
    // (mounted disks) must not be in the cache.
    const QStringList all = cache->cachedPaths();
    for (const QString &p : all) {
        if (p.startsWith(QStringLiteral("/Volumes/"))) {
            QFAIL(qPrintable("/Volumes/ entry leaked into cache: " + p));
        }
    }
}

void CacheStrategyTest::normalPathIsInCacheAfterScan()
{
    // Sanity check: a non-system path under / (like /Users) IS scanned.
    auto *cache = PathCacheManager::instance();
    cache->expandTo(QStringLiteral("/"));
    QTest::qWait(1500);
    cache->stopScan();
    const QStringList all = cache->cachedPaths();
    QVERIFY2(anyPathStartsWith(all, QStringLiteral("/Users")),
             "Expected /Users to be scanned — the path-level "
             "excludes should not block normal user-visible paths");
}

// ---------------------------------------------------------------------------
// Priority-driven scan order
// ---------------------------------------------------------------------------

void CacheStrategyTest::expandToHandlesMultipleRoots()
{
    // Sequential pattern matching how main.cpp actually drives scans:
    // scan root A, wait for completion, then scan root B.
    // The parallel pattern (two expandTo's in flight) is racy because
    // upstream's expandTo enqueues ancestors when a scan is already
    // running, which can starve the actual root.
    QTemporaryDir tmpA, tmpB;
    QVERIFY(tmpA.isValid() && tmpB.isValid());
    QDir(tmpA.path()).mkdir("aaa");
    QDir(tmpB.path()).mkdir("bbb");

    auto *cache = PathCacheManager::instance();
    cache->stopScan();
    QTest::qWait(30);

    cache->expandTo(tmpA.path());
    QTest::qWait(400);
    cache->stopScan();
    QTest::qWait(30);

    cache->expandTo(tmpB.path());
    QTest::qWait(400);
    cache->stopScan();

    const QStringList all = cache->cachedPaths();
    QVERIFY2(anyPathStartsWith(all, tmpA.path() + "/aaa"),
             "Root A subdir should be in cache");
    QVERIFY2(anyPathStartsWith(all, tmpB.path() + "/bbb"),
             "Root B subdir should be in cache");
}

void CacheStrategyTest::expandToDeduplicatesAlreadyCovered()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QDir(tmp.path()).mkdir("sub");

    auto *cache = PathCacheManager::instance();
    cache->stopScan();
    cache->expandTo(tmp.path());
    QTest::qWait(500);
    cache->stopScan();
    QTest::qWait(50);

    // Expanding the same root again — no duplicates, no crash.
    cache->expandTo(tmp.path());
    QTest::qWait(300);
    cache->stopScan();

    // Assert the unique paths from this tmp root appear exactly once.
    const QStringList all = cache->cachedPaths();
    const QString subPath = tmp.path() + "/sub";
    int hits = 0;
    for (const QString &p : all) {
        if (p == subPath) ++hits;
    }
    QCOMPARE(hits, 1);
}

// ---------------------------------------------------------------------------
// Expanded path-level excludes (the 50M / 54GB regression)
// ---------------------------------------------------------------------------

void CacheStrategyTest::usrPathIsNotInCacheAfterScan()
{
    auto *cache = PathCacheManager::instance();
    cache->expandTo(QStringLiteral("/"));
    QTest::qWait(800);
    cache->stopScan();
    QVERIFY(!anyPathStartsWith(cache->cachedPaths(), QStringLiteral("/usr")));
}

void CacheStrategyTest::libraryPathIsNotInCacheAfterScan()
{
    auto *cache = PathCacheManager::instance();
    cache->expandTo(QStringLiteral("/"));
    QTest::qWait(800);
    cache->stopScan();
    QVERIFY(!anyPathStartsWith(cache->cachedPaths(), QStringLiteral("/Library")));
}

void CacheStrategyTest::applicationsPathIsNotInCacheAfterScan()
{
    auto *cache = PathCacheManager::instance();
    cache->expandTo(QStringLiteral("/"));
    QTest::qWait(800);
    cache->stopScan();
    QVERIFY(!anyPathStartsWith(cache->cachedPaths(), QStringLiteral("/Applications")));
}

void CacheStrategyTest::optPathIsNotInCacheAfterScan()
{
    auto *cache = PathCacheManager::instance();
    cache->expandTo(QStringLiteral("/"));
    QTest::qWait(800);
    cache->stopScan();
    QVERIFY(!anyPathStartsWith(cache->cachedPaths(), QStringLiteral("/opt")));
}

// ---------------------------------------------------------------------------
// ~/Library + ~/.Trash excludes. Descending into ~/Library trips macOS TCC
// privacy prompts ("access your reminders / contacts / data from other
// apps") because Reminders, AddressBook, and other apps' containers live
// there. Nothing under it is user-searchable content — exclude the subtree.
// ---------------------------------------------------------------------------

void CacheStrategyTest::homeLibraryIsNotInCacheAfterScan()
{
    auto *cache = PathCacheManager::instance();
    cache->expandTo(QDir::homePath());
    // Home's first-level children are enumerated within the first BFS
    // round, so ~/Library would land in the cache almost immediately if
    // it weren't excluded.
    QTest::qWait(800);
    cache->stopScan();
    const QString lib = QDir::homePath() + QStringLiteral("/Library");
    QVERIFY2(!anyPathStartsWith(cache->cachedPaths(), lib),
             qPrintable(lib + " leaked into the cache"));
}

void CacheStrategyTest::homeTrashIsNotInCacheAfterScan()
{
    auto *cache = PathCacheManager::instance();
    cache->expandTo(QDir::homePath());
    QTest::qWait(800);
    cache->stopScan();
    const QString trash = QDir::homePath() + QStringLiteral("/.Trash");
    QVERIFY2(!anyPathStartsWith(cache->cachedPaths(), trash),
             qPrintable(trash + " leaked into the cache"));
}

// ---------------------------------------------------------------------------
// Folder cap behavior
// ---------------------------------------------------------------------------

void CacheStrategyTest::folderHardCeilingBlocksAdditions()
{
    // Use a synthetic tree under a QTemporaryDir so we hit a small ceiling.
    QTemporaryDir tdir;
    QVERIFY(tdir.isValid());
    for (int i = 0; i < 20; ++i) {
        QDir(tdir.path()).mkpath(QString("d%1").arg(i));
    }

    auto *cache = PathCacheManager::instance();
    // init() called rescan() which restarted a $HOME scan; halt that and
    // wipe state before setting tiny caps for this test.
    cache->stopScan();
    cache->rescan();
    cache->stopScan();
    QTest::qWait(30);

    cache->setHardCeiling(5);
    cache->setSoftCap(5);

    cache->expandTo(tdir.path());
    QTest::qWait(800);
    cache->stopScan();

    QVERIFY2(cache->folderCount() <= 5,
             qPrintable(QString("folderCount=%1, expected <=5")
                            .arg(cache->folderCount())));
    QVERIFY(cache->folderCeilingReached());

    cache->setHardCeiling(PathCacheManager::kDefaultHardCeiling);
    cache->setSoftCap(PathCacheManager::kDefaultSoftCap);
}

void CacheStrategyTest::cappedScanStopsDescendingAndCompletes()
{
    // Linear chain tdir/l0/l1/…/l7. Every level (root included) contains an
    // excluded "node_modules" folder and two files. With both caps set to 1,
    // the caps are hit while processing the root — the BFS must then stop
    // enqueueing subdirectories, so the deeper node_modules folders are
    // never even enumerated (foldersExcluded stays at 1 instead of 9).
    const int kDepth = 8;
    QTemporaryDir tdir;
    QVERIFY(tdir.isValid());
    auto seed = [](const QString &dir) {
        QDir(dir).mkdir(QStringLiteral("node_modules"));
        for (const char *name : {"f1.txt", "f2.txt"}) {
            QFile f(dir + '/' + name);
            f.open(QIODevice::WriteOnly);
            f.write("x");
        }
    };
    QString cur = tdir.path();
    seed(cur);
    for (int i = 0; i < kDepth; ++i) {
        cur += QString("/l%1").arg(i);
        QVERIFY(QDir().mkpath(cur));
        seed(cur);
    }

    QVERIFY(g_excludeSettings);
    g_excludeSettings->setPatternEnabled(QStringLiteral("node_modules"), true);

    auto *cache = PathCacheManager::instance();
    auto *fc = FileCacheManager::instance();
    cache->stopScan();
    cache->rescan();
    cache->stopScan();
    QTest::qWait(30);
    fc->clear();
    FileCacheManager::setHomeOverrideForTests(tdir.path());
    fc->setSoftCap(1);
    cache->setSoftCap(1);

    // Restore global state even if an assertion below aborts the test.
    auto restore = qScopeGuard([cache, fc]() {
        FileCacheManager::setHomeOverrideForTests(QString());
        fc->clear();
        fc->setSoftCap(FileCacheManager::kDefaultSoftCap);
        fc->setHardCeiling(FileCacheManager::kDefaultHardCeiling);
        cache->setSoftCap(PathCacheManager::kDefaultSoftCap);
        cache->setHardCeiling(PathCacheManager::kDefaultHardCeiling);
    });

    QSignalSpy completeSpy(cache, &PathCacheManager::scanComplete);
    cache->expandTo(tdir.path());
    // The scan must drain and complete ON ITS OWN — no stopScan().
    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + 5000;
    while (cache->isScanning()
           && QDateTime::currentMSecsSinceEpoch() < deadline) {
        QTest::qWait(20);
    }
    QVERIFY2(!cache->isScanning(), "capped scan should drain and complete");
    QTRY_COMPARE(completeSpy.count(), 1);

    // Both caches pinned at their caps.
    QCOMPARE(cache->folderCount(), 1);
    QCOMPARE(fc->fileCount(), 1);

    // Descent stopped: only the root directory was enumerated, so only the
    // root's node_modules counted as excluded — not one per level.
    const int totalExcluded = completeSpy.first().at(1).toInt();
    QVERIFY2(totalExcluded < kDepth,
             qPrintable(QString("foldersExcluded=%1 — BFS kept walking the "
                                "tree after both caps were reached")
                            .arg(totalExcluded)));
}

void CacheStrategyTest::folderSoftCapEmitsSignalOnce()
{
    QTemporaryDir tdir;
    QVERIFY(tdir.isValid());
    for (int i = 0; i < 20; ++i) {
        QDir(tdir.path()).mkpath(QString("e%1").arg(i));
    }
    auto *cache = PathCacheManager::instance();
    cache->stopScan();
    cache->rescan();
    cache->stopScan();
    QTest::qWait(30);

    cache->setHardCeiling(1000);
    cache->setSoftCap(3);

    QSignalSpy spy(cache, &PathCacheManager::folderCapReachedSignal);
    cache->expandTo(tdir.path());
    QTest::qWait(600);
    cache->stopScan();

    QVERIFY(spy.count() >= 1);

    cache->setHardCeiling(PathCacheManager::kDefaultHardCeiling);
    cache->setSoftCap(PathCacheManager::kDefaultSoftCap);
}

void CacheStrategyTest::expandToUserBumpsFolderSoftCap()
{
    auto *cache = PathCacheManager::instance();
    cache->stopScan();
    cache->setHardCeiling(10'000'000);
    cache->setSoftCap(1000);

    QSignalSpy raisedSpy(cache, &PathCacheManager::folderCapRaised);
    cache->expandToUser(QDir::homePath());
    cache->stopScan();
    QVERIFY(raisedSpy.count() >= 1);
    QCOMPARE(cache->softCap(),
             1000 + PathCacheManager::kSoftCapIncrement);

    cache->setHardCeiling(PathCacheManager::kDefaultHardCeiling);
    cache->setSoftCap(PathCacheManager::kDefaultSoftCap);
}

void CacheStrategyTest::expandToUserBumpsFileSoftCap()
{
    auto *fc = FileCacheManager::instance();
    fc->setHardCeiling(20'000'000);
    fc->setSoftCap(2000);
    const int before = fc->softCap();

    auto *cache = PathCacheManager::instance();
    cache->stopScan();
    cache->expandToUser(QDir::homePath());
    cache->stopScan();

    QCOMPARE(fc->softCap(), before + FileCacheManager::kSoftCapIncrement);

    fc->setSoftCap(FileCacheManager::kDefaultSoftCap);
    fc->setHardCeiling(FileCacheManager::kDefaultHardCeiling);
}
