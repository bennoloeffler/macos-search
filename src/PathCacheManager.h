#ifndef PATHCACHEMANAGER_H
#define PATHCACHEMANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QMutex>
#include <QAtomicInt>
#include <QSet>
#include <QQueue>
#include <QWaitCondition>

class QFileSystemWatcher;
class ExcludeSettings;

// Singleton manager for in-memory folder path cache
// Scans the user's home directory in a background thread at startup
// Provides instant in-memory search once populated
class PathCacheManager : public QObject
{
    Q_OBJECT

public:
    static PathCacheManager* instance();

    // Start scanning (called at app startup)
    void startScan();

    // Stop scanning (called at app shutdown)
    void stopScan();

    // Trigger a rescan (clears cache and starts fresh)
    void rescan();

    // Set exclude settings for filtering
    void setExcludeSettings(ExcludeSettings *settings);

    // Set whether to include hidden directories in scans
    void setShowHidden(bool show);

    // Cache status
    bool isScanning() const;
    bool isComplete() const;
    int folderCount() const;

    // Get all cached paths (thread-safe copy)
    QStringList cachedPaths() const;

    // Search the cache (instant, in-memory)
    // Returns paths matching the query, limited to maxResults
    // If rootPath is provided, only returns paths under that root
    QStringList search(const QString &query, const QString &rootPath = QString(), int maxResults = 100) const;

    // Get subdirectories of a path (for auto-completion)
    QStringList getSubdirectories(const QString &parentPath) const;

    // Expand cache to cover a new root path without clearing existing cache
    // Scans from / but skips directories already in cache (completed roots)
    void expandTo(const QString &rootPath);

    // Restart scan from a new root path without clearing existing cache
    // Stops any current scan, then starts scanning from the new root
    void restartScanFrom(const QString &rootPath);

signals:
    void scanStarted();
    void scanProgress(int foldersIndexed, int foldersExcluded, const QString &currentFolder);
    void scanComplete(int totalFolders, int totalExcluded);
    void cacheUpdated();

private slots:
    void onDirectoryChanged(const QString &path);

private:
    explicit PathCacheManager(QObject *parent = nullptr);
    ~PathCacheManager() override;

    // Disable copy
    PathCacheManager(const PathCacheManager&) = delete;
    PathCacheManager& operator=(const PathCacheManager&) = delete;

    // Background scan worker
    void performScan();
    void scanWorker(); // Parallel worker thread

    // Add a path to cache and watcher
    void addPathToCache(const QString &path);
    void removePathFromCache(const QString &path);

    // Parallel scan queue
    QQueue<QString> m_scanQueue;
    QMutex m_queueMutex;
    QWaitCondition m_queueCondition;
    QAtomicInt m_activeWorkers{0};
    QAtomicInt m_workersFinished{0};
    int m_numWorkers = 0;

    // Thread-safe path storage
    mutable QMutex m_mutex;
    QStringList m_paths;
    QSet<QString> m_pathSet; // For O(1) lookup

    // Filesystem watcher for real-time updates
    QFileSystemWatcher *m_watcher = nullptr;
    bool m_watcherLimitReached = false;

    // Scan state
    QThread *m_scanThread = nullptr;
    QAtomicInt m_scanning{0};
    QAtomicInt m_stopRequested{0};
    QAtomicInt m_foldersIndexed{0};
    QAtomicInt m_foldersExcluded{0};

    // Exclude settings (not owned)
    ExcludeSettings *m_excludeSettings = nullptr;

    // Whether to include hidden directories
    bool m_showHidden = false;

    // Track completed scan roots to avoid re-scanning
    QSet<QString> m_completedRoots;

    // Singleton instance
    static PathCacheManager *s_instance;
};

#endif // PATHCACHEMANAGER_H
