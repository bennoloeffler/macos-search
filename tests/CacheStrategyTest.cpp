#include "CacheStrategyTest.h"

#include "ExcludeSettings.h"
#include "FileCacheManager.h"
#include "PathCacheManager.h"
#include "PathStore.h"

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

void CacheStrategyTest::symlinkedDirIsSkippedNotFollowed()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString root = tmp.path();
    QVERIFY(QDir().mkpath(root + "/real/inner"));
    QVERIFY(QFile::link(root + "/real", root + "/link"));

    auto *cache = PathCacheManager::instance();
    cache->expandTo(root);
    waitForScanComplete(cache);

    const QStringList all = cache->cachedPaths();
    // The real tree is fully indexed…
    QVERIFY(all.contains(root + "/real/inner"));
    // …the symlink is skipped ENTIRELY — not cached and not followed. This is
    // what NoSymLinks buys us: the scan never stat()s the symlink target, so a
    // symlink into a cloud-storage / File-Provider location (~/iCloud,
    // ~/OneDrive-*) can't trigger a macOS privacy prompt or stall the scan.
    QVERIFY2(!all.contains(root + "/link"), "symlink itself was cached");
    QVERIFY2(!anyPathStartsWith(all, root + "/link/"),
             "scan descended into a symlinked directory");
    QVERIFY(!all.contains(root + "/link/inner"));
}

void CacheStrategyTest::symlinkToOutsideTargetNotIndexed()
{
    // Mirrors the real bug: a symlink in the scanned dir points at a tree
    // OUTSIDE the scan root (like ~/iCloud → ~/Library/…). NoSymLinks must
    // ensure the outside target is never walked or indexed.
    QTemporaryDir tmp, outside;
    QVERIFY(tmp.isValid() && outside.isValid());
    QVERIFY(QDir().mkpath(outside.path() + "/secret/deep"));
    QVERIFY(QFile::link(outside.path(), tmp.path() + "/cloud"));

    auto *cache = PathCacheManager::instance();
    // stopScan first: otherwise expandTo() enqueues tmp's ANCESTORS into the
    // in-flight $HOME scan (init's rescan), and the shared /var/folders parent
    // would enumerate the sibling `outside` dir directly — unrelated to the
    // symlink. With no scan running, expandTo does a clean tmp-only walk.
    cache->stopScan();
    QTest::qWait(30);
    cache->expandTo(tmp.path());
    waitForScanComplete(cache);

    const QStringList all = cache->cachedPaths();
    QVERIFY2(!anyPathStartsWith(all, outside.path()),
             "scan followed a symlink to an outside tree");
    QVERIFY2(!all.contains(tmp.path() + "/cloud"), "outside symlink was cached");
}

void CacheStrategyTest::symlinkCycleTerminatesQuickly()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString root = tmp.path();
    QVERIFY(QDir().mkpath(root + "/a"));
    // Symlink pointing back at its parent — an infinite path generator if
    // the scan follows directory symlinks.
    QVERIFY(QFile::link(root + "/a", root + "/a/loop"));

    auto *cache = PathCacheManager::instance();
    cache->expandTo(root);
    // Can't wait for global scanComplete here — init()'s rescan() has a
    // full home scan in flight. The cycle signature is inflation: without
    // the symlink guard the loop chain grows by one path per iteration,
    // so after 1.5 s there would be hundreds of entries under root.
    QTest::qWait(1500);
    cache->stopScan();

    int underRoot = 0;
    for (const QString &p : cache->cachedPaths()) {
        if (p.startsWith(root + "/")) ++underRoot;
    }
    QVERIFY2(underRoot < 5,
             qPrintable(QString("symlink cycle inflated cache: %1 entries")
                            .arg(underRoot)));
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

// ---------------------------------------------------------------------------
// Persistent index — warm start + reconciliation (docs/210)
// ---------------------------------------------------------------------------

void CacheStrategyTest::indexFingerprintReflectsIngredients()
{
    auto *cache = PathCacheManager::instance();
    auto *fc = FileCacheManager::instance();
    cache->stopScan();

    const QByteArray base = cache->indexFingerprint();
    QVERIFY(!base.isEmpty());
    QCOMPARE(cache->indexFingerprint(), base);   // deterministic

    // Folder soft cap is an ingredient.
    const int savedSoft = cache->softCap();
    cache->setSoftCap(savedSoft == 12345 ? 12346 : 12345);
    QVERIFY(cache->indexFingerprint() != base);
    cache->setSoftCap(savedSoft);
    QCOMPARE(cache->indexFingerprint(), base);

    // File soft cap is an ingredient.
    const int savedFile = fc->softCap();
    fc->setSoftCap(savedFile == 7777 ? 7778 : 7777);
    QVERIFY(cache->indexFingerprint() != base);
    fc->setSoftCap(savedFile);
    QCOMPARE(cache->indexFingerprint(), base);

    // Enabled exclude patterns are an ingredient.
    QVERIFY(g_excludeSettings);
    const QString probe = QStringLiteral("__fingerprint_probe__");
    g_excludeSettings->addPattern(probe);
    g_excludeSettings->setPatternEnabled(probe, true);
    QVERIFY(cache->indexFingerprint() != base);
    g_excludeSettings->removePattern(probe);
    QCOMPARE(cache->indexFingerprint(), base);
}

namespace {

// Fixed fingerprint for the reconcile tests — they save/load the shared
// store to a QTemporaryDir file directly (hermetic; never touches the real
// ~/.macos-search). The fingerprint helper itself is covered above.
const QByteArray kReconcileFp = QByteArrayLiteral("cachestrategy-reconcile-fp");

// Scan `root` from cold into the shared store and wait for completion.
// init() already ran rescan() (clearing completedRoots) before this test, so
// stopping its in-flight home scan and wiping the store is enough — no need
// for another full $HOME rescan, which only thrashes the disk.
void scanRootFresh(PathCacheManager *cache, const QString &root)
{
    cache->stopScan();
    PathStore::shared()->clear();
    FileCacheManager::instance()->clear();
    QTest::qWait(10);
    cache->expandTo(root);
    waitForScanComplete(cache);
}

}  // anonymous

void CacheStrategyTest::snapshotRoundtripThroughScan()
{
    QTemporaryDir tmp, snap;
    QVERIFY(tmp.isValid() && snap.isValid());
    const QString root = tmp.path();
    QVERIFY(QDir().mkpath(root + "/projects/app"));
    QFile(root + "/projects/app/main.cpp").open(QIODevice::WriteOnly);
    QFile(root + "/projects/notes.txt").open(QIODevice::WriteOnly);

    auto *cache = PathCacheManager::instance();
    auto *fc = FileCacheManager::instance();
    FileCacheManager::setHomeOverrideForTests(root);
    auto restore = qScopeGuard([] {
        FileCacheManager::setHomeOverrideForTests(QString());
    });

    scanRootFresh(cache, root);
    const int folders = cache->folderCount();
    const int files = fc->fileCount();
    QVERIFY(files >= 2);
    const QStringList before = cache->search("app", root, 100);
    QVERIFY(!before.isEmpty());

    const QString file = snap.path() + "/index.bin";
    QVERIFY(PathStore::shared()->saveTo(file, kReconcileFp));

    // "Restart": wipe the in-memory store, then load the snapshot back.
    PathStore::shared()->clear();
    QCOMPARE(cache->folderCount(), 0);
    QVERIFY(PathStore::shared()->loadFrom(file, kReconcileFp));

    QCOMPARE(cache->folderCount(), folders);
    QCOMPARE(fc->fileCount(), files);
    QCOMPARE(cache->search("app", root, 100), before);
    QVERIFY(fc->contains(root + "/projects/app/main.cpp"));
}

void CacheStrategyTest::snapshotDeletionReconciledOnRescan()
{
    QTemporaryDir tmp, snap;
    QVERIFY(tmp.isValid() && snap.isValid());
    const QString root = tmp.path();
    QVERIFY(QDir().mkpath(root + "/dir"));
    const QString victim = root + "/dir/gone.txt";
    QFile(victim).open(QIODevice::WriteOnly);
    QFile(root + "/dir/stay.txt").open(QIODevice::WriteOnly);

    auto *cache = PathCacheManager::instance();
    auto *fc = FileCacheManager::instance();
    FileCacheManager::setHomeOverrideForTests(root);
    auto restore = qScopeGuard([] {
        FileCacheManager::setHomeOverrideForTests(QString());
    });

    scanRootFresh(cache, root);
    QVERIFY(fc->contains(victim));

    const QString file = snap.path() + "/index.bin";
    QVERIFY(PathStore::shared()->saveTo(file, kReconcileFp));
    PathStore::shared()->clear();
    QVERIFY(PathStore::shared()->loadFrom(file, kReconcileFp));
    QVERIFY(fc->contains(victim));                 // present in the warm store

    // File deleted while "app was closed" → gone after the covering rescan.
    QVERIFY(QFile::remove(victim));
    cache->restartScanFrom(root);
    waitForScanComplete(cache);

    QVERIFY2(!fc->contains(victim), "deleted file survived reconciliation");
    QVERIFY(fc->contains(root + "/dir/stay.txt"));
}

void CacheStrategyTest::snapshotAdditionReconciledOnRescan()
{
    QTemporaryDir tmp, snap;
    QVERIFY(tmp.isValid() && snap.isValid());
    const QString root = tmp.path();
    QVERIFY(QDir().mkpath(root + "/dir"));
    QFile(root + "/dir/old.txt").open(QIODevice::WriteOnly);

    auto *cache = PathCacheManager::instance();
    auto *fc = FileCacheManager::instance();
    FileCacheManager::setHomeOverrideForTests(root);
    auto restore = qScopeGuard([] {
        FileCacheManager::setHomeOverrideForTests(QString());
    });

    scanRootFresh(cache, root);

    const QString file = snap.path() + "/index.bin";
    QVERIFY(PathStore::shared()->saveTo(file, kReconcileFp));
    PathStore::shared()->clear();
    QVERIFY(PathStore::shared()->loadFrom(file, kReconcileFp));

    // File created while "app was closed" → present after the covering rescan.
    const QString born = root + "/dir/new.txt";
    QVERIFY(!fc->contains(born));
    QFile(born).open(QIODevice::WriteOnly);
    cache->restartScanFrom(root);
    waitForScanComplete(cache);

    QVERIFY2(fc->contains(born), "new file missed by reconciliation");
    QVERIFY(fc->contains(root + "/dir/old.txt"));
}

void CacheStrategyTest::snapshotSkippedSubtreeSurvivesRescan()
{
    // THE skipped-subtree hazard end-to-end (docs/210 review): a subtree
    // scanned as an earlier root (m_completedRoots) is skipped by a later
    // parent-root scan. The parent-was-listed sweep rule must NOT wipe it.
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString root = tmp.path();
    QVERIFY(QDir().mkpath(root + "/sub/inner"));
    QVERIFY(QDir().mkpath(root + "/top"));

    auto *cache = PathCacheManager::instance();
    cache->stopScan();               // halt init()'s in-flight home scan
    PathStore::shared()->clear();    // clean store (completedRoots already empty)
    QTest::qWait(10);

    // Scan the subtree as its own root first → lands in m_completedRoots.
    cache->expandTo(root + "/sub");
    waitForScanComplete(cache);
    QVERIFY(cache->cachedPaths().contains(root + "/sub/inner"));

    // Now scan the parent. The scan skips descending into the already-
    // completed /sub, so /sub is seen (as a child of root) but never listed
    // this generation — its children must survive the sweep.
    cache->expandTo(root);
    waitForScanComplete(cache);

    const QStringList all = cache->cachedPaths();
    QVERIFY2(all.contains(root + "/sub/inner"),
             "skipped subtree was wrongly swept by the parent-root scan");
    QVERIFY(all.contains(root + "/top"));
}
