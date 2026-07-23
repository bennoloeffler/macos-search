#include "FileCacheManagerTest.h"
#include "FileCacheManager.h"
#include "PathCacheManager.h"

#include <QDir>
#include <QSignalSpy>
#include <QtTest/QtTest>

namespace {

// Helper: build an absolute path inside the test machine's $HOME so the
// FileCacheManager scope guard accepts it.
QString homePath(const QString &suffix)
{
    return QDir::homePath() + suffix;
}

}  // namespace

void FileCacheManagerTest::initTestCase()
{
    PathCacheManager::instance()->stopScan();
    FileCacheManager::instance()->clear();
    FileCacheManager::instance()->setHardCeiling(FileCacheManager::kDefaultHardCeiling);
    FileCacheManager::instance()->setSoftCap(FileCacheManager::kDefaultSoftCap);
}

void FileCacheManagerTest::cleanup()
{
    PathCacheManager::instance()->stopScan();
    FileCacheManager::instance()->clear();
    FileCacheManager::instance()->setHardCeiling(FileCacheManager::kDefaultHardCeiling);
    FileCacheManager::instance()->setSoftCap(FileCacheManager::kDefaultSoftCap);
}

void FileCacheManagerTest::testSingleton()
{
    QVERIFY(FileCacheManager::instance() != nullptr);
    QCOMPARE(FileCacheManager::instance(), FileCacheManager::instance());
}

void FileCacheManagerTest::testAddFileAddsToCache()
{
    auto *c = FileCacheManager::instance();
    QVERIFY(c->addFile(homePath("/a.txt")));
    QCOMPARE(c->fileCount(), 1);
}

void FileCacheManagerTest::testAddFileIgnoresDuplicate()
{
    auto *c = FileCacheManager::instance();
    QVERIFY(c->addFile(homePath("/a.txt")));
    QVERIFY(!c->addFile(homePath("/a.txt")));
    QCOMPARE(c->fileCount(), 1);
}

void FileCacheManagerTest::testAddFileIgnoresEmpty()
{
    auto *c = FileCacheManager::instance();
    QVERIFY(!c->addFile(""));
    QCOMPARE(c->fileCount(), 0);
}

void FileCacheManagerTest::testContainsReturnsTrueAfterAdd()
{
    auto *c = FileCacheManager::instance();
    c->addFile(homePath("/foo"));
    QVERIFY(c->contains(homePath("/foo")));
    QVERIFY(!c->contains(homePath("/bar")));
}

void FileCacheManagerTest::testFileCountUpdatesAsAdded()
{
    auto *c = FileCacheManager::instance();
    QCOMPARE(c->fileCount(), 0);
    c->addFile(homePath("/a"));
    c->addFile(homePath("/b"));
    c->addFile(homePath("/c"));
    QCOMPARE(c->fileCount(), 3);
}

void FileCacheManagerTest::testCachedFilesReturnsSnapshot()
{
    auto *c = FileCacheManager::instance();
    c->addFile(homePath("/x"));
    c->addFile(homePath("/y"));
    QStringList snap = c->cachedFiles();
    QCOMPARE(snap.size(), 2);
    QVERIFY(snap.contains(homePath("/x")));
    QVERIFY(snap.contains(homePath("/y")));
}

void FileCacheManagerTest::testRemoveFileRemovesFromCache()
{
    auto *c = FileCacheManager::instance();
    c->addFile(homePath("/a"));
    c->addFile(homePath("/b"));
    c->removeFile(homePath("/a"));
    QVERIFY(!c->contains(homePath("/a")));
    QVERIFY(c->contains(homePath("/b")));
    QCOMPARE(c->fileCount(), 1);
}

void FileCacheManagerTest::testRemoveFilesUnderPrunesPrefix()
{
    auto *c = FileCacheManager::instance();
    c->addFile(homePath("/dir/one"));
    c->addFile(homePath("/dir/two"));
    c->addFile(homePath("/dir/nested/three"));
    c->addFile(homePath("/other/four"));
    int removed = c->removeFilesUnder(homePath("/dir"));
    QCOMPARE(removed, 3);
    QVERIFY(c->contains(homePath("/other/four")));
}

void FileCacheManagerTest::testClearEmptiesEverything()
{
    auto *c = FileCacheManager::instance();
    c->addFile(homePath("/a"));
    c->addFile(homePath("/b"));
    c->clear();
    QCOMPARE(c->fileCount(), 0);
}

void FileCacheManagerTest::testSearchReturnsCaseInsensitiveMatches()
{
    auto *c = FileCacheManager::instance();
    c->addFile(homePath("/Documents/Report.PDF"));
    c->addFile(homePath("/Music/song.mp3"));
    QStringList hits = c->search("report");
    QCOMPARE(hits.size(), 1);
    QCOMPARE(hits.first(), homePath("/Documents/Report.PDF"));
}

void FileCacheManagerTest::testSearchHonorsRootPath()
{
    auto *c = FileCacheManager::instance();
    c->addFile(homePath("/scope/match.txt"));
    c->addFile(homePath("/other/match.txt"));
    QStringList hits = c->search("match.txt", homePath("/scope"));
    QCOMPARE(hits.size(), 1);
    QCOMPARE(hits.first(), homePath("/scope/match.txt"));
}

void FileCacheManagerTest::testSearchHonorsMaxResults()
{
    auto *c = FileCacheManager::instance();
    for (int i = 0; i < 200; ++i) {
        c->addFile(homePath(QString("/data/match-%1.txt").arg(i, 4, 10, QChar('0'))));
    }
    QStringList hits = c->search("match", QString(), 50);
    QCOMPARE(hits.size(), 50);
}

void FileCacheManagerTest::testSearchMultiTermAndLogic()
{
    auto *c = FileCacheManager::instance();
    c->addFile(homePath("/proj/readme.md"));
    c->addFile(homePath("/proj/license.txt"));
    c->addFile(homePath("/notes/readme.md"));
    QStringList hits = c->search("proj readme");
    QCOMPARE(hits.size(), 1);
    QCOMPARE(hits.first(), homePath("/proj/readme.md"));
}

void FileCacheManagerTest::testCapEnforced()
{
    auto *c = FileCacheManager::instance();
    c->setHardCeiling(100);
    c->setSoftCap(5);
    for (int i = 0; i < 10; ++i) {
        c->addFile(homePath(QString("/f/%1").arg(i)));
    }
    QCOMPARE(c->fileCount(), 5);
    QVERIFY(c->capReached());
}

void FileCacheManagerTest::testCapReachedSignalEmitted()
{
    auto *c = FileCacheManager::instance();
    c->setHardCeiling(100);
    c->setSoftCap(3);
    QSignalSpy spy(c, &FileCacheManager::capReachedSignal);
    for (int i = 0; i < 5; ++i) {
        c->addFile(homePath(QString("/g/%1").arg(i)));
    }
    QCOMPARE(spy.count(), 1);
}

void FileCacheManagerTest::testSetCapLimitClearsFlagIfRoom()
{
    auto *c = FileCacheManager::instance();
    c->setHardCeiling(100);
    c->setSoftCap(2);
    c->addFile(homePath("/h/0"));
    c->addFile(homePath("/h/1"));
    c->addFile(homePath("/h/2"));
    QVERIFY(c->capReached());
    c->setSoftCap(50);
    QVERIFY(!c->capReached());
}

void FileCacheManagerTest::testAddFileRejectsPathOutsideHome()
{
    auto *c = FileCacheManager::instance();
    QVERIFY(!c->addFile("/usr/share/man/man1/ls.1"));
    QVERIFY(!c->addFile("/Library/Caches/something"));
    QVERIFY(!c->addFile("/tmp/foo.txt"));
    QCOMPARE(c->fileCount(), 0);
}

void FileCacheManagerTest::testIsUnderHomeBoundaryRespected()
{
    const QString home = QDir::cleanPath(QDir::homePath());
    QVERIFY(FileCacheManager::isUnderHome(home + "/file.txt"));
    QVERIFY(FileCacheManager::isUnderHome(home + "/dir/sub/file"));
    // A sibling whose path *starts* with the home string must NOT match.
    QVERIFY(!FileCacheManager::isUnderHome(home + "-extra/file"));
}

void FileCacheManagerTest::testIsUnderHomeAllowsExactHome()
{
    const QString home = QDir::cleanPath(QDir::homePath());
    QVERIFY(FileCacheManager::isUnderHome(home));
}

void FileCacheManagerTest::testHardCeilingBlocksFurtherAdditions()
{
    auto *c = FileCacheManager::instance();
    c->setHardCeiling(3);
    c->setSoftCap(3);
    for (int i = 0; i < 5; ++i) {
        c->addFile(homePath(QString("/cap/%1").arg(i)));
    }
    QCOMPARE(c->fileCount(), 3);
    QVERIFY(c->ceilingReached());
    // Even after bumping the soft cap, the hard ceiling holds.
    c->bumpSoftCap();
    c->addFile(homePath("/cap/after-bump"));
    QCOMPARE(c->fileCount(), 3);
}

void FileCacheManagerTest::testHardCeilingSignalEmitted()
{
    auto *c = FileCacheManager::instance();
    c->setHardCeiling(2);
    c->setSoftCap(2);
    QSignalSpy spy(c, &FileCacheManager::ceilingReachedSignal);
    for (int i = 0; i < 5; ++i) {
        c->addFile(homePath(QString("/cei/%1").arg(i)));
    }
    QCOMPARE(spy.count(), 1);
}

void FileCacheManagerTest::testBumpSoftCapRaisesToIncrement()
{
    auto *c = FileCacheManager::instance();
    c->setHardCeiling(FileCacheManager::kSoftCapIncrement * 10);
    c->setSoftCap(1000);
    const int before = c->softCap();
    const int after = c->bumpSoftCap();
    QCOMPARE(after, before + FileCacheManager::kSoftCapIncrement);
    QCOMPARE(c->softCap(), after);
}

void FileCacheManagerTest::testBumpSoftCapClampsAtHardCeiling()
{
    auto *c = FileCacheManager::instance();
    c->setHardCeiling(2000);
    c->setSoftCap(1500);
    const int after = c->bumpSoftCap();
    QCOMPARE(after, 2000);   // clamped at hard ceiling
    QCOMPARE(c->softCap(), 2000);
}

void FileCacheManagerTest::testBumpSoftCapEmitsCapRaisedSignal()
{
    auto *c = FileCacheManager::instance();
    c->setHardCeiling(FileCacheManager::kSoftCapIncrement * 10);
    c->setSoftCap(1000);
    QSignalSpy spy(c, &FileCacheManager::capRaised);
    c->bumpSoftCap();
    QCOMPARE(spy.count(), 1);
}

void FileCacheManagerTest::testDefaultCapsMatchConstants()
{
    auto *c = FileCacheManager::instance();
    c->setHardCeiling(FileCacheManager::kDefaultHardCeiling);
    c->setSoftCap(FileCacheManager::kDefaultSoftCap);
    QCOMPARE(c->softCap(), FileCacheManager::kDefaultSoftCap);
    QCOMPARE(c->hardCeiling(), FileCacheManager::kDefaultHardCeiling);
    // Lowered 2026-07-21 (Block 2): at ~730 B/entry the old caps allowed
    // multi-GB caches. See docs/200_pathstore_redesign.md.
    QCOMPARE(FileCacheManager::kDefaultSoftCap, 5'000'000);
    QCOMPARE(FileCacheManager::kDefaultHardCeiling, 10'000'000);
    QCOMPARE(FileCacheManager::kSoftCapIncrement, 1'000'000);
    QCOMPARE(PathCacheManager::kDefaultSoftCap, 5'000'000);
    QCOMPARE(PathCacheManager::kDefaultHardCeiling, 10'000'000);
}
