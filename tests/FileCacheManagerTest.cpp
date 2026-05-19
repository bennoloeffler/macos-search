#include "FileCacheManagerTest.h"
#include "FileCacheManager.h"
#include "PathCacheManager.h"

#include <QSignalSpy>
#include <QtTest/QtTest>

void FileCacheManagerTest::initTestCase()
{
    PathCacheManager::instance()->stopScan();
    FileCacheManager::instance()->clear();
    FileCacheManager::instance()->setCapLimit(500000);
}

void FileCacheManagerTest::cleanup()
{
    PathCacheManager::instance()->stopScan();
    FileCacheManager::instance()->clear();
    FileCacheManager::instance()->setCapLimit(500000);
}

void FileCacheManagerTest::testSingleton()
{
    QVERIFY(FileCacheManager::instance() != nullptr);
    QCOMPARE(FileCacheManager::instance(), FileCacheManager::instance());
}

void FileCacheManagerTest::testAddFileAddsToCache()
{
    auto *c = FileCacheManager::instance();
    QVERIFY(c->addFile("/tmp/a.txt"));
    QCOMPARE(c->fileCount(), 1);
}

void FileCacheManagerTest::testAddFileIgnoresDuplicate()
{
    auto *c = FileCacheManager::instance();
    QVERIFY(c->addFile("/tmp/a.txt"));
    QVERIFY(!c->addFile("/tmp/a.txt"));
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
    c->addFile("/tmp/foo");
    QVERIFY(c->contains("/tmp/foo"));
    QVERIFY(!c->contains("/tmp/bar"));
}

void FileCacheManagerTest::testFileCountUpdatesAsAdded()
{
    auto *c = FileCacheManager::instance();
    QCOMPARE(c->fileCount(), 0);
    c->addFile("/a");
    c->addFile("/b");
    c->addFile("/c");
    QCOMPARE(c->fileCount(), 3);
}

void FileCacheManagerTest::testCachedFilesReturnsSnapshot()
{
    auto *c = FileCacheManager::instance();
    c->addFile("/x");
    c->addFile("/y");
    QStringList snap = c->cachedFiles();
    QCOMPARE(snap.size(), 2);
    QVERIFY(snap.contains("/x"));
    QVERIFY(snap.contains("/y"));
}

void FileCacheManagerTest::testRemoveFileRemovesFromCache()
{
    auto *c = FileCacheManager::instance();
    c->addFile("/a");
    c->addFile("/b");
    c->removeFile("/a");
    QVERIFY(!c->contains("/a"));
    QVERIFY(c->contains("/b"));
    QCOMPARE(c->fileCount(), 1);
}

void FileCacheManagerTest::testRemoveFilesUnderPrunesPrefix()
{
    auto *c = FileCacheManager::instance();
    c->addFile("/dir/one");
    c->addFile("/dir/two");
    c->addFile("/dir/nested/three");
    c->addFile("/other/four");
    int removed = c->removeFilesUnder("/dir");
    QCOMPARE(removed, 3);
    QVERIFY(c->contains("/other/four"));
}

void FileCacheManagerTest::testClearEmptiesEverything()
{
    auto *c = FileCacheManager::instance();
    c->addFile("/a");
    c->addFile("/b");
    c->clear();
    QCOMPARE(c->fileCount(), 0);
}

void FileCacheManagerTest::testSearchReturnsCaseInsensitiveMatches()
{
    auto *c = FileCacheManager::instance();
    c->addFile("/Users/me/Documents/Report.PDF");
    c->addFile("/Users/me/Music/song.mp3");
    QStringList hits = c->search("report");
    QCOMPARE(hits.size(), 1);
    QCOMPARE(hits.first(), QString("/Users/me/Documents/Report.PDF"));
}

void FileCacheManagerTest::testSearchHonorsRootPath()
{
    auto *c = FileCacheManager::instance();
    c->addFile("/scope/match.txt");
    c->addFile("/other/match.txt");
    QStringList hits = c->search("match", "/scope");
    QCOMPARE(hits.size(), 1);
    QCOMPARE(hits.first(), QString("/scope/match.txt"));
}

void FileCacheManagerTest::testSearchHonorsMaxResults()
{
    auto *c = FileCacheManager::instance();
    for (int i = 0; i < 200; ++i) {
        c->addFile(QString("/data/match-%1.txt").arg(i, 4, 10, QChar('0')));
    }
    QStringList hits = c->search("match", QString(), 50);
    QCOMPARE(hits.size(), 50);
}

void FileCacheManagerTest::testSearchMultiTermAndLogic()
{
    auto *c = FileCacheManager::instance();
    c->addFile("/proj/readme.md");
    c->addFile("/proj/license.txt");
    c->addFile("/notes/readme.md");
    QStringList hits = c->search("proj readme");
    QCOMPARE(hits.size(), 1);
    QCOMPARE(hits.first(), QString("/proj/readme.md"));
}

void FileCacheManagerTest::testCapEnforced()
{
    auto *c = FileCacheManager::instance();
    c->setCapLimit(5);
    for (int i = 0; i < 10; ++i) {
        c->addFile(QString("/f/%1").arg(i));
    }
    QCOMPARE(c->fileCount(), 5);
    QVERIFY(c->capReached());
}

void FileCacheManagerTest::testCapReachedSignalEmitted()
{
    auto *c = FileCacheManager::instance();
    c->setCapLimit(3);
    QSignalSpy spy(c, &FileCacheManager::capReachedSignal);
    for (int i = 0; i < 5; ++i) {
        c->addFile(QString("/g/%1").arg(i));
    }
    QCOMPARE(spy.count(), 1);
}

void FileCacheManagerTest::testSetCapLimitClearsFlagIfRoom()
{
    auto *c = FileCacheManager::instance();
    c->setCapLimit(2);
    c->addFile("/h/0");
    c->addFile("/h/1");
    c->addFile("/h/2");
    QVERIFY(c->capReached());
    c->setCapLimit(100);
    QVERIFY(!c->capReached());
}
