#ifndef PATHSTORETEST_H
#define PATHSTORETEST_H

#include <QObject>

// Unit tests for PathStore, the compact tree-arena path storage behind
// PathCacheManager + FileCacheManager. See docs/200_pathstore_redesign.md.
class PathStoreTest : public QObject
{
    Q_OBJECT

private slots:
    // Structure
    void appendChildrenKeepsParentBeforeChild();
    void pathOfMaterializesFullPath();
    void findLocatesExistingAndMissesAbsent();
    void findOrCreatePathCreatesIntermediateSegments();
    void findOrCreatePathDeduplicates();
    void childrenOfListsDirectChildren();

    // Search semantics
    void searchMatchesViaAncestorSegment();
    void searchMultiTermAndAcrossAncestors();
    void searchSlashSplitsTerms();
    void searchTermStaysInsideSegment();
    void searchCaseInsensitiveNonAscii();
    void searchScopesToRootPath();
    void searchRootPathBoundaryRespected();
    void searchFiltersByKind();
    void searchHonorsMaxResults();

    // Tombstones
    void tombstoneRemovesFromSearchAndCounts();
    void tombstoneKindMaskLeavesOtherKind();
    void relivenTombstonedEntry();

    // Scan ingest + caps
    void ingestListingDedupesRelisting();
    void capBlocksFindOrCreatePath();
    void capBlocksIngestListing();

    void clearResetsStore();

    // Generation mark-and-sweep (docs/210_persistent_index.md). The
    // parent-was-listed rule is the load-bearing design decision — see the
    // skipped-subtree hazard test.
    void sweepTombstonesUnseenChildrenOfListedDirs();
    void sweepSparesChildrenOfUnlistedParents();
    void sweepLeavesStampedViaParentListingSurvive();
    void generationWrapKeepsSweepSafe();

    // Snapshot save/load (docs/210_persistent_index.md)
    void saveLoadRoundtripPreservesStore();
    void saveDropsTombstonedNodes();
    void loadRefusesWrongFingerprint();
    void loadRefusesTruncatedFile();
    void loadRefusesGarbage();
    // Warm-start reconcile must not double-count: after load, re-ingesting the
    // same listings finds every entry via the rebuilt child chain and adds
    // nothing (the counter cannot climb past the real file count).
    void reconcileAfterLoadDoesNotInflate();
    void warmReconcileViaWalkDoesNotInflate();
    void purgeExcludedSubtreeDropsJunkKeepsRest();
    void snapshotTiming100k();

    // G1 gate (hard-fail): ≤ 36 bytes per entry on 100k realistic names.
    void memoryGateG1();
};

#endif // PATHSTORETEST_H
