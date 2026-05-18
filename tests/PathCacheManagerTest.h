#ifndef PATHCACHEMANAGERTEST_H
#define PATHCACHEMANAGERTEST_H

#include <QObject>

class PathCacheManagerTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

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
