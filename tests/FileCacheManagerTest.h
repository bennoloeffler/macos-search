#ifndef FILECACHEMANAGERTEST_H
#define FILECACHEMANAGERTEST_H

#include <QObject>

class FileCacheManagerTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanup();

    void testSingleton();
    void testAddFileAddsToCache();
    void testAddFileIgnoresDuplicate();
    void testAddFileIgnoresEmpty();
    void testContainsReturnsTrueAfterAdd();
    void testFileCountUpdatesAsAdded();
    void testCachedFilesReturnsSnapshot();
    void testRemoveFileRemovesFromCache();
    void testRemoveFilesUnderPrunesPrefix();
    void testClearEmptiesEverything();
    void testSearchReturnsCaseInsensitiveMatches();
    void testSearchHonorsRootPath();
    void testSearchHonorsMaxResults();
    void testSearchMultiTermAndLogic();
    void testCapEnforced();
    void testCapReachedSignalEmitted();
    void testSetCapLimitClearsFlagIfRoom();
};

#endif // FILECACHEMANAGERTEST_H
