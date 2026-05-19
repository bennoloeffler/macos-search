#include "FileSearchWorkerTest.h"
#include "FileCacheManager.h"
#include "FileSearchWorker.h"
#include "FolderSearchWorker.h"
#include "PathCacheManager.h"

#include <QSignalSpy>
#include <QtTest/QtTest>

namespace {
QList<SearchResult> waitForResults(FileSearchWorker *w)
{
    QSignalSpy spy(w, &FileSearchWorker::resultsReady);
    if (!spy.wait(1000)) return {};
    return spy.first().first().value<QList<SearchResult>>();
}
}

void FileSearchWorkerTest::initTestCase()
{
    qRegisterMetaType<QList<SearchResult>>("QList<SearchResult>");
    PathCacheManager::instance()->stopScan();
    FileCacheManager::instance()->clear();
    // Tests fabricate paths outside the real $HOME (`/Users/me/...`,
    // `/scope/...`). Override the home boundary so they get accepted.
    FileCacheManager::setHomeOverrideForTests("/");
}

void FileSearchWorkerTest::cleanup()
{
    PathCacheManager::instance()->stopScan();
    FileCacheManager::instance()->clear();
    FileCacheManager::setHomeOverrideForTests("/");
}

void FileSearchWorkerTest::testEmptyQueryReturnsEmpty()
{
    FileSearchWorker w;
    QSignalSpy spy(&w, &FileSearchWorker::resultsReady);
    w.search("");
    QVERIFY(spy.wait(500));
    QCOMPARE(spy.first().first().value<QList<SearchResult>>().size(), 0);
}

void FileSearchWorkerTest::testBasicMatchEmitsResult()
{
    FileCacheManager::instance()->addFile("/Users/me/projects/readme.md");
    FileSearchWorker w;
    w.search("readme");
    auto results = waitForResults(&w);
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.first().path, QString("/Users/me/projects/readme.md"));
    QCOMPARE(results.first().displayName, QString("readme.md"));
}

void FileSearchWorkerTest::testMultiTermAndLogic()
{
    auto *c = FileCacheManager::instance();
    c->addFile("/Users/me/projects/readme.md");
    c->addFile("/Users/me/notes/readme.md");
    c->addFile("/Users/me/projects/license.txt");
    FileSearchWorker w;
    w.search("projects readme");
    auto results = waitForResults(&w);
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.first().path, QString("/Users/me/projects/readme.md"));
}

void FileSearchWorkerTest::testHiddenFilesFilteredByDefault()
{
    auto *c = FileCacheManager::instance();
    c->addFile("/Users/me/.config/data.json");
    c->addFile("/Users/me/visible/data.json");
    FileSearchWorker w;
    w.setIncludeHidden(false);
    w.search("data");
    auto results = waitForResults(&w);
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.first().path, QString("/Users/me/visible/data.json"));
}

void FileSearchWorkerTest::testHiddenFilesShownWhenIncluded()
{
    auto *c = FileCacheManager::instance();
    c->addFile("/Users/me/.config/data.json");
    c->addFile("/Users/me/visible/data.json");
    FileSearchWorker w;
    w.setIncludeHidden(true);
    w.search("data");
    auto results = waitForResults(&w);
    QCOMPARE(results.size(), 2);
}

void FileSearchWorkerTest::testRootPathScopesResults()
{
    auto *c = FileCacheManager::instance();
    c->addFile("/scope/a.txt");
    c->addFile("/other/a.txt");
    FileSearchWorker w;
    w.search("a.txt", "/scope");
    auto results = waitForResults(&w);
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.first().path, QString("/scope/a.txt"));
}

void FileSearchWorkerTest::testResultsSortedByScore()
{
    auto *c = FileCacheManager::instance();
    c->addFile("/Users/me/proj/A/maude.txt");
    c->addFile("/Users/me/proj/maude-test/long-path-extra.txt");
    FileSearchWorker w;
    w.search("maude");
    auto results = waitForResults(&w);
    QVERIFY(results.size() >= 2);
    // Score should be non-increasing
    for (int i = 1; i < results.size(); ++i) {
        QVERIFY(results[i - 1].score >= results[i].score);
    }
}

void FileSearchWorkerTest::testCancelStopsPendingSearch()
{
    FileCacheManager::instance()->addFile("/cancel/me.txt");
    FileSearchWorker w;
    QSignalSpy spy(&w, &FileSearchWorker::resultsReady);
    w.search("me");
    w.cancel();
    // Even after waiting we should see nothing (cancel kills the debounce).
    QVERIFY(!spy.wait(200));
}

void FileSearchWorkerTest::testFuzzyScoreReturnsPositiveForBasename()
{
    int s = FolderSearchWorker::fuzzyScore(
        "/Users/me/projects/foo.txt", "foo", "/Users/me");
    QVERIFY(s > 0);
}
