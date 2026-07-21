#ifndef FILECACHEMANAGER_H
#define FILECACHEMANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QAtomicInt>

class PathStore;

// In-memory file-name cache. Populated by the same BFS walk that drives
// PathCacheManager (one scan visit per directory, two destinations).
//
// SCOPE
// -----
// **Files are only indexed inside the user's home directory.** Any
// addFile(path) call for a path outside $HOME is silently rejected. This
// prevents the runaway growth observed when scanning from "/" — the user
// almost never wants /usr/share/man pages in their file picker, and
// indexing them costs gigabytes of RAM. Folder search still works
// everywhere; this guard only applies to files.
//
// CAPS
// ----
// Two layered limits, matching the design in docs/search_files-too.md:
//   - softCap   default 5 000 000.   Backstop against background runaway.
//                                     Background scans stop adding at the
//                                     soft cap.  A user-initiated
//                                     "Scan now" can bump it by +1 M files
//                                     at a time (capped by hardCeiling).
//   - hardCeiling default 10 000 000. Absolute ceiling. Nothing exceeds
//                                     it without a Preferences change.
//                                     PathStore stores entries at a
//                                     measured 15–50 B each, so even the
//                                     ceiling (10 M files + 5 M folders)
//                                     is well under 1 GB. A real 2 M-entry
//                                     home measured 32 MB
//                                     (docs/200_pathstore_redesign.md).
//
// STORAGE
// -------
// File entries live in the PathStore shared with PathCacheManager
// (Kind::File) — ~30 bytes per entry instead of three full path copies.
// clear() only tombstones file entries; the folder cache is untouched.
// Threading: the store carries its own QReadWriteLock.
class FileCacheManager : public QObject
{
    Q_OBJECT

public:
    // Default cap values — pinned constants so tests can assert them.
    static constexpr int kDefaultSoftCap = 5'000'000;
    static constexpr int kDefaultHardCeiling = 10'000'000;
    static constexpr int kSoftCapIncrement = 1'000'000;

    static FileCacheManager *instance();

    // Add a single absolute file path. Returns true if added. Background
    // scans use this directly; "Scan now" first calls bumpSoftCap() to make
    // room, then the same scan calls addFile() — no source flag needed.
    bool addFile(const QString &absolutePath);

    // Raise the soft cap by one increment (clamped at hardCeiling). Invoked
    // by PathCacheManager::expandToUser before a user-initiated scan starts.
    // Returns the new soft-cap value.
    int bumpSoftCap();

    // Scan hot path: ingest one directory's file listing in one atomic
    // batch (dirNode = the directory's PathStore node). Applies the $HOME
    // scope guard once per directory and the caps per file.
    void ingestScan(qint32 dirNode, const QString &dirPath,
                    const QStringList &names);

    void removeFile(const QString &absolutePath);
    int removeFilesUnder(const QString &directoryPath);
    void clear();

    int fileCount() const;
    bool contains(const QString &absolutePath) const;
    QStringList cachedFiles() const;

    // Cap accessors / mutators.
    int softCap() const;
    int hardCeiling() const;
    void setSoftCap(int newCap);
    void setHardCeiling(int newCeiling);
    bool capReached() const;     // soft cap reached
    bool ceilingReached() const; // hard ceiling reached

    // Compatibility wrappers — existing callers used capLimit / setCapLimit.
    int capLimit() const { return softCap(); }
    void setCapLimit(int newCap) { setSoftCap(newCap); }

    // Scope policy. Returns true when `absolutePath` is inside the home
    // directory (or equal to it). Exposed for testing.
    static bool isUnderHome(const QString &absolutePath);

    // Test seam: override the directory that addFile() treats as the home
    // boundary. Empty string clears the override (back to QDir::homePath()).
    // Useful for tests that drive scans against QTemporaryDir (which on
    // macOS lives outside the user's actual $HOME).
    static void setHomeOverrideForTests(const QString &path);

    QStringList search(const QString &query,
                       const QString &rootPath = QString(),
                       int maxResults = 100) const;

signals:
    void capReachedSignal();      // soft cap reached
    void ceilingReachedSignal();  // hard ceiling reached
    void capRaised(int newSoftCap);
    void cacheUpdated();

private:
    explicit FileCacheManager(QObject *parent = nullptr);
    ~FileCacheManager() override = default;

    FileCacheManager(const FileCacheManager &) = delete;
    FileCacheManager &operator=(const FileCacheManager &) = delete;

    void noteCapHit();

    PathStore *m_store = nullptr;
    QAtomicInt m_capReached{0};
    QAtomicInt m_ceilingReached{0};
    QAtomicInt m_softCap{kDefaultSoftCap};
    QAtomicInt m_hardCeiling{kDefaultHardCeiling};

    static FileCacheManager *s_instance;
};

#endif // FILECACHEMANAGER_H
