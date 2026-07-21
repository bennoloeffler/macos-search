#include "ScanQueueTest.h"
#include "ScanQueue.h"
#include <QtTest/QtTest>

void ScanQueueTest::testMostProbableTargetsComeFirst()
{
    const QStringList q = ScanQueue::build(
        "/Users/x", QString(), {}, {});
    // No default start, no favorites, no Dropbox dirs: the well-known
    // user dirs lead, home closes the list.
    QCOMPARE(q, QStringList({"/Users/x/Desktop", "/Users/x/Downloads",
                             "/Users/x/Documents", "/Users/x"}));
}

void ScanQueueTest::testDefaultStartLeadsWhenSet()
{
    const QStringList q = ScanQueue::build(
        "/Users/x", "/Users/x/projects", {}, {});
    QCOMPARE(q.first(), QString("/Users/x/projects"));
    QCOMPARE(q.at(1), QString("/Users/x/Desktop"));
}

void ScanQueueTest::testDeduplicatesAcrossSources()
{
    const QStringList q = ScanQueue::build(
        "/Users/x", "/Users/x/Desktop",
        {"/Users/x", "/Users/x/Downloads", "/Volumes/ext"},
        {});
    // Desktop appears once (default start), Downloads once (well-known),
    // home once, and the genuinely new favorite is appended last.
    QCOMPARE(q, QStringList({"/Users/x/Desktop", "/Users/x/Downloads",
                             "/Users/x/Documents", "/Users/x",
                             "/Volumes/ext"}));
}

void ScanQueueTest::testDropboxDirsSlotBeforeHome()
{
    const QStringList q = ScanQueue::build(
        "/Users/x", QString(), {},
        {"/Users/x/VundS Dropbox"});
    QCOMPARE(q, QStringList({"/Users/x/Desktop", "/Users/x/Downloads",
                             "/Users/x/Documents",
                             "/Users/x/VundS Dropbox", "/Users/x"}));
}
