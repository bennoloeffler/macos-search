#ifndef PATHCACHEMANAGERTEST_H
#define PATHCACHEMANAGERTEST_H

#include <QObject>

class PathCacheManagerTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // Root-coverage predicate (2026-07-24): a completed "/" scan must cover
    // every absolute path — the naive `root + "/"` prefix made "//", which
    // covered nothing (favorites gray while Macintosh HD showed green).
    void testIsPathUnderRoot();

    void testInstanceReturnsNonNull();
    void testInstanceReturnsSameSingleton();
    void testInitialStateNotScanning();
    void testFolderCountInitiallyZero();
    void testSearchEmptyQueryReturnsEmpty();
    void testSearchReturnsMaxResults();
    void testCachedPathsReturnsStringList();
    void testSetExcludeSettingsDoesNotCrash();
    void testStartScanEmitsSignal();
    void testStopScanStopsScanning();
    void testRescanClearsAndRestarts();
};

#endif // PATHCACHEMANAGERTEST_H
