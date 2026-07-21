#ifndef CACHESTRATEGYTEST_H
#define CACHESTRATEGYTEST_H

#include <QObject>

// Cache-strategy tests — verify the priority-driven scan order and the
// path-level system excludes (`/System`, `/private`, …).
//
// These tests poke `PathCacheManager` directly because that's where the
// strategy lives.
class CacheStrategyTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();

    // Path-level excludes
    void systemPathIsNotInCacheAfterScan();
    void privatePathIsNotInCacheAfterScan();
    void devPathIsNotInCacheAfterScan();
    void volumesPathIsNotInCacheAfterScan();
    void normalPathIsInCacheAfterScan();

    // Priority-driven scan order (via expandTo)
    void expandToHandlesMultipleRoots();
    void expandToDeduplicatesAlreadyCovered();

    // Expanded path-level excludes (2026-05-19 — runaway-scan regression)
    void usrPathIsNotInCacheAfterScan();
    void libraryPathIsNotInCacheAfterScan();
    void applicationsPathIsNotInCacheAfterScan();
    void optPathIsNotInCacheAfterScan();

    // ~/Library + ~/.Trash excludes (2026-07-21 — descending into
    // ~/Library trips macOS TCC privacy prompts: Reminders, Contacts,
    // other apps' containers).
    void homeLibraryIsNotInCacheAfterScan();
    void homeTrashIsNotInCacheAfterScan();

    // Symlink handling (2026-07-21). Following directory symlinks walks
    // linked trees twice (~/Dropbox → ~/VundS Dropbox) and loops forever
    // on ancestor links. Symlinked dirs are cached as leaves, never
    // descended.
    void symlinkedDirIsSkippedNotFollowed();
    void symlinkToOutsideTargetNotIndexed();
    void symlinkCycleTerminatesQuickly();

    // Folder cap (2026-05-19)
    void folderHardCeilingBlocksAdditions();
    // Cap-aware descent (2026-07-21, Block 2): when folder AND file caps
    // are both reached, the BFS stops enqueueing subdirectories instead of
    // walking the rest of the disk for nothing.
    void cappedScanStopsDescendingAndCompletes();
    void folderSoftCapEmitsSignalOnce();
    void expandToUserBumpsFolderSoftCap();
    void expandToUserBumpsFileSoftCap();

    // Persistent-index warm start + reconciliation (docs/210). The
    // fingerprint must change when any ingredient changes; a saved snapshot
    // must reconcile against filesystem changes on the next rescan.
    void indexFingerprintReflectsIngredients();
    void snapshotRoundtripThroughScan();
    void snapshotDeletionReconciledOnRescan();
    void snapshotAdditionReconciledOnRescan();
    void snapshotSkippedSubtreeSurvivesRescan();
};

#endif // CACHESTRATEGYTEST_H
