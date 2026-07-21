#ifndef PATHSTORE_H
#define PATHSTORE_H

#include <QByteArray>
#include <QByteArrayView>
#include <QList>
#include <QReadWriteLock>
#include <QString>
#include <QStringList>
#include <vector>

// The ONLY place cached path data lives (design: docs/200_pathstore_redesign.md).
//
// Paths are stored as a tree of nodes over a single UTF-8 name arena:
// 12 bytes per node + the original name bytes — no per-entry QString, no
// lowered copy, no hash node. Node 0 is the implicit "/" root.
//
// Invariants:
//   - parent index < child index (append-only).
//   - Deletion = tombstone (the Entry flag is cleared); indexes are stable
//     forever. clear() is the only shrink operation (rescan path).
//   - Intermediate segments created on the way to an entry are "scaffold"
//     nodes: they shape the tree and their names count for search matching,
//     but they are not counted, listed, or returned as results.
//
// Threading: every public method takes the internal QReadWriteLock; safe to
// call from the scan worker threads and the main thread concurrently
// (see docs/120_qt_threading.md).
class PathStore
{
public:
    enum Kind : quint8 { Folder = 0, File = 1 };
    enum class Add { Added, Existed, CapBlocked };

    PathStore();

    // The process-wide store shared by PathCacheManager + FileCacheManager.
    static PathStore *shared();

    // Append a child entry under `parent`. Raw append, no dedupe — the
    // caller guarantees (parent, name) is new (fresh directory listing).
    // Returns the new node index.
    qint32 addChild(qint32 parent, QByteArrayView utf8Name, Kind kind);

    // Atomically ingest one directory listing (the scan hot path). Names
    // already present under `dir` as live entries keep their node (not
    // re-added, but returned so the walk can descend); tombstoned or
    // scaffold ones are (re)livened. Stops adding once count(kind) reaches
    // `cap` (cap < 0 = unbounded). Stamps the current scan generation on
    // `dir` (with the *listed* bit) and on every seen child — the mark half
    // of the mark-and-sweep reconciliation (docs/210_persistent_index.md).
    struct Ingest {
        QList<qint32> nodes;   // per name: node index, or -1 (cap-blocked)
        int added = 0;         // names that became live entries
        bool capHit = false;
    };
    Ingest ingestListing(qint32 dir, const QStringList &names, Kind kind, int cap);

    // Mark-and-sweep reconciliation (docs/210_persistent_index.md).
    // beginScanGeneration() opens a fresh generation before a scan run;
    // 15-bit space, wraps by normalizing every node back to generation 0 so
    // a stale value can never alias the fresh one. sweepStale() tombstones
    // exactly those entries under `root` whose parent was listed this
    // generation but which were not seen in that listing (plus their
    // subtrees) — never touching children of parents that were skipped this
    // run. Returns the number of entries tombstoned.
    void beginScanGeneration();
    int sweepStale(qint32 root);

    // Walk/create nodes along absPath; the final node becomes a live entry
    // of `kind` (subject to `cap`). Rare path — roots, watcher, public adds.
    qint32 findOrCreatePath(const QString &absPath, Kind kind,
                            int cap = -1, Add *status = nullptr);
    // Same walk, but leaves entry status untouched (scaffold only).
    qint32 ensurePath(const QString &absPath);

    qint32 find(const QString &absPath) const;    // -1 if absent
    bool isEntry(qint32 node) const;
    bool isEntry(qint32 node, Kind kind) const;
    QString pathOf(qint32 node) const;            // materialize on demand
    QString nameOf(qint32 node) const;
    QList<qint32> childrenOf(qint32 node) const;  // linear scan — fine (watcher)

    // Tombstoning. kindMask is a bitmask of (1 << Kind); the node itself is
    // included when its kind is in the mask. Returns the number of entries
    // removed. `removedFolders` (optional) receives the paths of removed
    // folder entries (filesystem-watcher cleanup).
    int markDeleted(qint32 node);
    int markDeletedRecursive(qint32 node,
                             quint8 kindMask = (1u << Folder) | (1u << File),
                             QStringList *removedFolders = nullptr);
    void clear();

    int count(Kind k) const;                      // live entries of a kind
    qint64 bytesUsed() const;                     // exact accounting (G1 gate)
    QStringList entries(Kind k) const;            // live paths, insertion order

    // Snapshot persistence (docs/210_persistent_index.md). Raw-blob format:
    // MSIX magic, version, fingerprint, counts, node array + name arena as
    // one write each. saveTo() writes live nodes only (tombstones dropped,
    // parent indexes remapped) via QSaveFile — a crash never corrupts an
    // existing snapshot. loadFrom() refuses on any mismatch or corruption
    // and leaves the store unchanged; the snapshot is a pure accelerator.
    bool saveTo(const QString &filePath, const QByteArray &fingerprint) const;
    bool loadFrom(const QString &filePath, const QByteArray &expectedFingerprint);

    // Search: every term must appear case-insensitively in SOME segment of
    // the path (ancestor names count). Whitespace and '/' both split terms.
    QStringList search(const QString &query, Kind kind,
                       const QString &rootPath, int maxResults) const;

private:
    struct Node {                       // 12 bytes
        qint32  parent;                 // -1 for the "/" root
        quint32 nameOff;                // offset into m_names
        quint8  nameLen;                // macOS names ≤ 255 UTF-8 bytes
        quint8  flags;                  // kKindFile | kEntry | kNonAscii | kHasChild
        quint16 pad;
    };
    static constexpr quint8 kKindFile = 1;   // 0 = Folder
    static constexpr quint8 kEntry    = 2;   // live cached entry (vs scaffold/tombstone)
    static constexpr quint8 kNonAscii = 4;   // name needs Unicode-aware matching
    static constexpr quint8 kHasChild = 8;   // dedupe hint for ingestListing

    // Generation bits packed into Node::pad: low 15 = scan generation,
    // high bit = "this dir was listed this generation" (docs/210).
    static constexpr quint16 kGenMask   = 0x7FFF;
    static constexpr quint16 kListedBit = 0x8000;

    qint32 appendNodeLocked(qint32 parent, QByteArrayView utf8Name, Kind kind,
                            bool asEntry);
    void stampSeenLocked(qint32 node);   // mark node seen in the current gen
    qint32 childByNameLocked(qint32 parent, QByteArrayView utf8Name) const;
    qint32 walkLocked(const QString &absPath, bool create);
    QString pathOfLocked(qint32 node) const;
    void makeEntryLocked(qint32 node, Kind kind);
    void initRootLocked();

    std::vector<Node> m_nodes;
    QByteArray        m_names;          // ONE arena: concatenated UTF-8 names
    int               m_counts[2] = {0, 0};
    quint16           m_generation = 0; // current scan generation (mark-and-sweep)
    mutable QReadWriteLock m_lock;
};

#endif // PATHSTORE_H
