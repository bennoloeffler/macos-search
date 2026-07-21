#include "FsEventsSyncTest.h"

#include "FsEventsWatcher.h"
#include "PathCacheManager.h"
#include "FileCacheManager.h"
#include "ExcludeSettings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

// Live end-to-end tests drive the REAL FSEvents stream: scan a fixture tree
// (which arms the recursive watcher over it via finishScan), mutate the
// filesystem, and QTRY-wait for PathCacheManager/FileCacheManager to reflect
// it. FSEvents coalesces with ~0.5 s latency, so waits are generous (10 s).
//
// The fixture lives under the real $HOME (see g_base), NOT in QTemporaryDir:
// QTemporaryDir canonicalizes to /private/var/... which the scanner excludes,
// and a home subdir's path matches the canonical /Users/... paths FSEvents
// delivers. setHomeOverrideForTests scopes the file cache to the fixture.

namespace {

ExcludeSettings *g_excludes = nullptr;

// The fixture tree lives UNDER the real $HOME, not in QTemporaryDir. Two
// reasons: (1) QTemporaryDir canonicalizes to /private/var/... which is under
// the /private path-level exclude, so the scanner refuses to index it; (2) a
// home subdir's canonical path equals itself (home isn't a symlink), matching
// the canonical /Users/... paths FSEvents delivers. Removed in cleanupTestCase.
QString g_base;

// Per-test root: a unique subdir named after the running test function.
QString testRoot()
{
    const QString root = g_base + "/" + QString::fromLatin1(QTest::currentTestFunction());
    QDir(root).removeRecursively();
    QDir().mkpath(root);
    return root;
}

void scanRoot(PathCacheManager *cache, const QString &root)
{
    cache->stopScan();
    cache->expandTo(root);
    // finishScan arms the FSEvents stream on the main thread; pump until the
    // scan is done AND the root is recorded as completed.
    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + 8000;
    while ((cache->isScanning() || !cache->isPathScanned(root))
           && QDateTime::currentMSecsSinceEpoch() < deadline) {
        QTest::qWait(50);
    }
    // isPathScanned flips on the scan thread BEFORE finishScan's queued
    // setRoots() actually arms the stream on the main thread. Drain that
    // invocation and give FSEventStreamStart a moment to go live, else a
    // mutation here races stream startup and the event is dropped
    // (kFSEventStreamEventIdSinceNow). This settle is what makes the live
    // tests deterministic.
    QTest::qWait(900);
}

}  // namespace

// ---------------------------------------------------------------------------
// reduceRoots(): clean each path, drop empties/duplicates, and collapse any
// root that is a descendant of another kept root (one recursive FSEvents
// stream over the ancestor already covers the descendant).
// ---------------------------------------------------------------------------

void FsEventsSyncTest::testReduceRootsCollapsesNested()
{
    const QStringList reduced = FsEventsWatcher::reduceRoots(
        {"/a/b", "/a", "/c", "/a/b/c", "/cc"});
    QCOMPARE(reduced, QStringList({"/a", "/c", "/cc"}));
}

void FsEventsSyncTest::testReduceRootsDropsDuplicatesAndSlashes()
{
    const QStringList reduced = FsEventsWatcher::reduceRoots(
        {"/x/", "/x", "/x/y", QString()});
    QCOMPARE(reduced, QStringList({"/x"}));
}

void FsEventsSyncTest::testReduceRootsEmptyInputStaysEmpty()
{
    QCOMPARE(FsEventsWatcher::reduceRoots({}), QStringList());
    QCOMPARE(FsEventsWatcher::reduceRoots({QString(), QString()}), QStringList());
}

void FsEventsSyncTest::testReduceRootsSiblingsAllKept()
{
    // "/a" must not swallow "/ab" — the prefix guard is on "/a/", not "/a".
    const QStringList reduced = FsEventsWatcher::reduceRoots({"/ab", "/a", "/a/x"});
    QCOMPARE(reduced, QStringList({"/a", "/ab"}));
}

// --- Live FSEvents integration --------------------------------------------

void FsEventsSyncTest::initTestCase()
{
    g_excludes = new ExcludeSettings(this);
    PathCacheManager::instance()->setExcludeSettings(g_excludes);

    g_base = QDir::homePath() + "/.mssearch-fsevents-test";
    QDir(g_base).removeRecursively();
    QVERIFY(QDir().mkpath(g_base));
    // Files index only under $HOME; scope the file cache to the fixture tree.
    FileCacheManager::setHomeOverrideForTests(g_base);
}

void FsEventsSyncTest::cleanupTestCase()
{
    PathCacheManager::instance()->stopScan();
    FileCacheManager::setHomeOverrideForTests(QString());
    QDir(g_base).removeRecursively();
}

void FsEventsSyncTest::cleanup()
{
    PathCacheManager::instance()->stopScan();
}

void FsEventsSyncTest::testCreateFileAppears()
{
    const QString root = testRoot();
    auto *cache = PathCacheManager::instance();
    scanRoot(cache, root);

    QFile f(root + "/created.txt");
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("x");
    f.close();

    QTRY_VERIFY_WITH_TIMEOUT(
        FileCacheManager::instance()->contains(root + "/created.txt"), 10000);
}

void FsEventsSyncTest::testDeleteFileDisappears()
{
    const QString root = testRoot();
    QFile f(root + "/doomed.txt");
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("x");
    f.close();

    auto *cache = PathCacheManager::instance();
    scanRoot(cache, root);
    QVERIFY(FileCacheManager::instance()->contains(root + "/doomed.txt"));

    QVERIFY(QFile::remove(root + "/doomed.txt"));
    QTRY_VERIFY_WITH_TIMEOUT(
        !FileCacheManager::instance()->contains(root + "/doomed.txt"), 10000);
}

void FsEventsSyncTest::testCreateDirAppears()
{
    const QString root = testRoot();
    auto *cache = PathCacheManager::instance();
    scanRoot(cache, root);

    QVERIFY(QDir(root).mkdir("freshdir"));
    QTRY_VERIFY_WITH_TIMEOUT(
        cache->cachedPaths().contains(root + "/freshdir"), 10000);
}

void FsEventsSyncTest::testCreateDeepTreeAppears()
{
    const QString root = testRoot();
    auto *cache = PathCacheManager::instance();
    scanRoot(cache, root);

    // A whole subtree at once — proves the diff recurses into new dirs
    // (expandTo) rather than trusting the deepest FSEvents event.
    QVERIFY(QDir(root).mkpath("a/b/c"));
    QFile f(root + "/a/b/c/leaf.txt");
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("x");
    f.close();

    QTRY_VERIFY_WITH_TIMEOUT(cache->cachedPaths().contains(root + "/a/b/c"), 10000);
    QTRY_VERIFY_WITH_TIMEOUT(
        FileCacheManager::instance()->contains(root + "/a/b/c/leaf.txt"), 10000);
}

void FsEventsSyncTest::testRenameDirSubtreeFollows()
{
    const QString root = testRoot();
    QVERIFY(QDir(root).mkpath("oldname/inner"));
    auto *cache = PathCacheManager::instance();
    scanRoot(cache, root);
    QVERIFY(cache->cachedPaths().contains(root + "/oldname/inner"));

    QVERIFY(QDir(root).rename("oldname", "newname"));

    // New subtree appears…
    QTRY_VERIFY_WITH_TIMEOUT(cache->cachedPaths().contains(root + "/newname/inner"), 10000);
    // …and the old one is tombstoned (gone from search).
    QTRY_VERIFY_WITH_TIMEOUT(!cache->cachedPaths().contains(root + "/oldname/inner"), 10000);
}

void FsEventsSyncTest::testExcludedDirStaysOut()
{
    const QString root = testRoot();
    auto *cache = PathCacheManager::instance();
    scanRoot(cache, root);

    QVERIFY(QDir(root).mkpath("node_modules/pkg"));
    // Give FSEvents ample time to deliver, then assert it never got indexed.
    QTest::qWait(2500);
    QVERIFY(!cache->cachedPaths().contains(root + "/node_modules"));
    QVERIFY(!cache->cachedPaths().contains(root + "/node_modules/pkg"));
}

void FsEventsSyncTest::testUntrackedEventIsIgnored()
{
    const QString root = testRoot();
    auto *cache = PathCacheManager::instance();
    scanRoot(cache, root);

    const int before = cache->folderCount();
    // Fire the diff slot directly for a path the store never tracked — the
    // guard must return early and NOT synthesize a phantom node (the old
    // ensurePath() bug). Synchronous call: no async interference.
    cache->onDirectoryChanged(root + "/never/tracked/here");
    QCOMPARE(cache->folderCount(), before);
    QVERIFY(!cache->cachedPaths().contains(root + "/never/tracked/here"));
}
