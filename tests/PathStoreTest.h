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

    // Snapshot save/load (docs/210_persistent_index.md)
    void saveLoadRoundtripPreservesStore();
    void saveDropsTombstonedNodes();
    void loadRefusesWrongFingerprint();
    void loadRefusesTruncatedFile();
    void loadRefusesGarbage();
    void snapshotTiming100k();

    // G1 gate (hard-fail): ≤ 36 bytes per entry on 100k realistic names.
    void memoryGateG1();
};

#endif // PATHSTORETEST_H
