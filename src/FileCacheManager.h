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
// Architectural difference from PathCacheManager:
//   1. No "subfolder-of-existing-match" suppression. Two files in the same
//      directory are independently relevant.
//   2. Bounded size: when the cap is hit, additions are silently dropped
//      and `capReached()` is true. The toolbar surfaces this so the user
//      can tighten excludes or raise the cap.
//
// Threading: same QReadWriteLock pattern as ExcludeSettings.
//   - Writers (scan workers, FSEvents handler) hold an exclusive lock.
//   - Readers (FileSearchWorker, cap-status UI) hold a shared lock.
//
// Persistence: in-memory only in v1. Rebuilds on each launch.
class FileCacheManager : public QObject
{
    Q_OBJECT

public:
    static FileCacheManager *instance();

    // Add a single absolute file path. Returns true if added, false if the
    // cap is full or the path is already present.
    bool addFile(const QString &absolutePath);

    // Remove a single file path. Used by FSEvents on deletion.
    void removeFile(const QString &absolutePath);

    // Remove every file under the given directory prefix. Used when a parent
    // directory disappears.
    int removeFilesUnder(const QString &directoryPath);

    // Wipe everything (for rescan).
    void clear();

    int fileCount() const;
    int capLimit() const;
    bool capReached() const;
    void setCapLimit(int newCap);

    // Returns true if `path` (absolute) is in the cache.
    bool contains(const QString &absolutePath) const;

    // Snapshot copy. Cheap-ish for hundreds of thousands of entries but
    // not free — readers that only need filtered results should use
    // `search()` instead.
    QStringList cachedFiles() const;

    // Linear scan: match all space-separated terms (AND), case-insensitive,
    // against the absolute path. Optionally scoped to `rootPath` (returns
    // only paths under that root). Up to `maxResults` hits.
    QStringList search(const QString &query,
                       const QString &rootPath = QString(),
                       int maxResults = 100) const;

signals:
    void capReachedSignal();
    void cacheUpdated();

private:
    explicit FileCacheManager(QObject *parent = nullptr);
    ~FileCacheManager() override = default;

    FileCacheManager(const FileCacheManager &) = delete;
    FileCacheManager &operator=(const FileCacheManager &) = delete;

    mutable QReadWriteLock m_lock;
    QStringList m_paths;
    QSet<QString> m_pathSet;
    QAtomicInt m_capReached{0};
    QAtomicInt m_capLimit{500000};

    static FileCacheManager *s_instance;
};

#endif // FILECACHEMANAGER_H
