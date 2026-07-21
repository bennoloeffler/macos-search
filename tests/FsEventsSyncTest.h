#ifndef FSEVENTSSYNCTEST_H
#define FSEVENTSSYNCTEST_H

#include <QObject>

// Unit tests for FsEventsWatcher::reduceRoots() — the pure, dependency-free
// part of the FSEvents recursive watcher.
//
// The live filesystem-sync integration (FSEvents stream -> per-directory diff)
// is exercised separately against the cache manager; here we only lock down
// the root-reduction logic, which is a self-contained static function.
class FsEventsSyncTest : public QObject
{
    Q_OBJECT

private slots:
    void testReduceRootsCollapsesNested();
    void testReduceRootsDropsDuplicatesAndSlashes();
    void testReduceRootsEmptyInputStaysEmpty();
    void testReduceRootsSiblingsAllKept();

    // Live end-to-end: real FSEvents stream -> PathCacheManager diff.
    void initTestCase();
    void cleanupTestCase();
    void cleanup();
    void testCreateFileAppears();
    void testDeleteFileDisappears();
    void testCreateDirAppears();
    void testCreateDeepTreeAppears();
    void testRenameDirSubtreeFollows();
    void testExcludedDirStaysOut();
    void testUntrackedEventIsIgnored();
};

#endif // FSEVENTSSYNCTEST_H
