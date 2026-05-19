#ifndef FILESEARCHWORKERTEST_H
#define FILESEARCHWORKERTEST_H

#include <QObject>

class FileSearchWorkerTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanup();

    void testEmptyQueryReturnsEmpty();
    void testBasicMatchEmitsResult();
    void testMultiTermAndLogic();
    void testHiddenFilesFilteredByDefault();
    void testHiddenFilesShownWhenIncluded();
    void testRootPathScopesResults();
    void testResultsSortedByScore();
    void testCancelStopsPendingSearch();
    void testFuzzyScoreReturnsPositiveForBasename();
};

#endif // FILESEARCHWORKERTEST_H
