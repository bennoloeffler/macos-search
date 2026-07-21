#ifndef FILECACHEMANAGER_H
#define FILECACHEMANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QSet>
#include <QReadWriteLock>
#include <QAtomicInt>

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
//   - softCap   default 1 000 000.   Backstop against background runaway.
//                                     Background scans stop adding at the
//                                     soft cap.  A user-initiated
//                                     "Scan now" can bump it by +1 M files
//                                     at a time (capped by hardCeiling).
//   - hardCeiling default 2 000 000. Absolute ceiling. Nothing exceeds
//                                     it without a Preferences change.
//                                     At today's ~730 B/entry the worst
//                                     case (2 M files + 1 M folders) is
//                                     ~2.2 GB; the PathStore redesign
//                                     (docs/200_pathstore_redesign.md)
//                                     brings that far down again.
//
// Threading: same QReadWriteLock pattern as ExcludeSettings.
class FileCacheManager : public QObject
{
    Q_OBJECT

public:
    // Default cap values — pinned constants so tests can assert them.
    static constexpr int kDefaultSoftCap = 1'000'000;
    static constexpr int kDefaultHardCeiling = 2'000'000;
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

    mutable QReadWriteLock m_lock;
    QStringList m_paths;
    QStringList m_lowerPaths;
    QSet<QString> m_pathSet;
    QAtomicInt m_capReached{0};
    QAtomicInt m_ceilingReached{0};
    QAtomicInt m_softCap{kDefaultSoftCap};
    QAtomicInt m_hardCeiling{kDefaultHardCeiling};

    static FileCacheManager *s_instance;
};

#endif // FILECACHEMANAGER_H
