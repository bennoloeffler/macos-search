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
    // Parallel array of pre-lowercased paths. Computed once on insert so the
    // search hot path doesn't pay toLower() per cached path per keystroke.
    QStringList m_lowerPaths;
    QSet<QString> m_pathSet; // For O(1) lookup

    // Two-tier cap (mirrors FileCacheManager).
    //   softCap     — backstop against unbounded background growth (the
    //                 50 M / 54 GB incident). Background scans stop adding
    //                 here. A user-initiated "Scan now" can bump it by
    //                 +150k folders per click, capped at hardCeiling.
    //   hardCeiling — absolute ceiling, only adjustable via Preferences.
    QAtomicInt m_capReached{0};
    QAtomicInt m_ceilingReached{0};
    QAtomicInt m_softCap;
    QAtomicInt m_hardCeiling;

public:
    enum class AddSource {
        BackgroundScan = 0,
        UserExpand = 1,
    };

    static constexpr int kDefaultSoftCap = 1'000'000;
    static constexpr int kDefaultHardCeiling = 5'000'000;
    static constexpr int kSoftCapIncrement = 150'000;

    int softCap() const { return m_softCap.loadAcquire(); }
    int hardCeiling() const { return m_hardCeiling.loadAcquire(); }
    void setSoftCap(int newCap);
    void setHardCeiling(int newCeiling);
    bool folderCapReached() const { return m_capReached.loadAcquire() != 0; }
    bool folderCeilingReached() const { return m_ceilingReached.loadAcquire() != 0; }

    /// Expand the cache to cover `rootPath` with a user-initiated request.
    /// Unlike expandTo(), this bumps the soft cap up to `kSoftCapIncrement`
    /// per call so a deliberate "Scan now" click can grow the index past
    /// today's soft limit (still bounded by the hard ceiling).
    void expandToUser(const QString &rootPath);

    /// True if `path` is in the completed-roots set or descends from one.
    /// Used by the per-path scan-state indicators.
    bool isPathScanned(const QString &path) const;

    /// True if a scan is currently in progress AND `path` falls under its
    /// active root. (Background-favorite scans broadcast their root via
    /// the same mechanism, so all in-flight scans contribute.)
    bool isPathScanning(const QString &path) const;

signals:
    void folderCapReachedSignal();
    void folderCeilingReachedSignal();
    void folderCapRaised(int newSoftCap);

private:

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
    // The root of the currently in-flight scan (empty if none). Used by
    // isPathScanning() so per-path indicators can flip to Scanning state.
    QString m_currentScanRoot;

    // Singleton instance
    static PathCacheManager *s_instance;
};

#endif // PATHCACHEMANAGER_H
