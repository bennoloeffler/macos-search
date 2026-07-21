#include "PathCacheManager.h"
#include "ExcludeSettings.h"
#include "FileCacheManager.h"
#include "PathStore.h"
#include <QDir>
#include <QHash>
#include <QFileSystemWatcher>
#include <QFileInfo>
#include <QQueue>
#include <QMetaObject>
#include <QtConcurrent>
#include <QThreadPool>

PathCacheManager* PathCacheManager::s_instance = nullptr;

PathCacheManager* PathCacheManager::instance()
{
    if (!s_instance) {
        s_instance = new PathCacheManager();
    }
    return s_instance;
}

PathCacheManager::PathCacheManager(QObject *parent)
    : QObject(parent)
{
    m_store = PathStore::shared();
    m_softCap.storeRelease(kDefaultSoftCap);
    m_hardCeiling.storeRelease(kDefaultHardCeiling);

    // Create filesystem watcher on main thread
    m_watcher = new QFileSystemWatcher(this);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged,
            this, &PathCacheManager::onDirectoryChanged);
}

void PathCacheManager::setSoftCap(int newCap)
{
    if (newCap < 1) newCap = 1;
    const int hard = m_hardCeiling.loadAcquire();
    if (newCap > hard) newCap = hard;
    m_softCap.storeRelease(newCap);
    if (m_store->count(PathStore::Folder) >= newCap) {
        m_capReached.storeRelease(1);
    } else {
        m_capReached.storeRelease(0);
    }
}

void PathCacheManager::setHardCeiling(int newCeiling)
{
    if (newCeiling < 1) newCeiling = 1;
    m_hardCeiling.storeRelease(newCeiling);
    if (m_softCap.loadAcquire() > newCeiling) {
        m_softCap.storeRelease(newCeiling);
    }
    if (m_store->count(PathStore::Folder) >= newCeiling) {
        m_ceilingReached.storeRelease(1);
    } else {
        m_ceilingReached.storeRelease(0);
    }
}

void PathCacheManager::expandToUser(const QString &rootPath)
{
    // Bump folder + file soft caps by one increment before delegating to
    // the regular expandTo() flow. The user clicked "Scan this folder now"
    // — that's a deliberate "use more memory if needed" signal.
    const int oldSoft = m_softCap.loadAcquire();
    const int hard = m_hardCeiling.loadAcquire();
    const int newSoft = qMin(oldSoft + kSoftCapIncrement, hard);
    if (newSoft > oldSoft) {
        m_softCap.storeRelease(newSoft);
        m_capReached.storeRelease(0);
        emit folderCapRaised(newSoft);
    }
    FileCacheManager::instance()->bumpSoftCap();
    expandTo(rootPath);
}

bool PathCacheManager::isPathScanned(const QString &path) const
{
    if (path.isEmpty()) return false;
    const QString clean = QDir::cleanPath(path);
    QMutexLocker locker(&m_mutex);
    for (const QString &root : m_completedRoots) {
        if (clean == root || clean.startsWith(root + QLatin1Char('/'))) {
            return true;
        }
    }
    return false;
}

bool PathCacheManager::isPathScanning(const QString &path) const
{
    if (path.isEmpty() || !isScanning()) return false;
    const QString clean = QDir::cleanPath(path);
    QMutexLocker locker(&m_mutex);
    const QString root = m_currentScanRoot;
    if (root.isEmpty()) return false;
    return clean == root || clean.startsWith(root + QLatin1Char('/'))
           || root.startsWith(clean + QLatin1Char('/'));
}

PathCacheManager::~PathCacheManager()
{
    stopScan();
}

void PathCacheManager::setExcludeSettings(ExcludeSettings *settings)
{
    m_excludeSettings = settings;
}

void PathCacheManager::setShowHidden(bool show)
{
    // The cache ALWAYS scans with QDir::Hidden included. The eye toggle
    // is purely presentational and lives in FolderBrowserDialog +
    // FolderSearchWorker. This setter is kept for API compatibility but
    // is a no-op aside from remembering the requested state.
    //
    // Rationale: triggering a full rescan on every flip of a *display*
    // toggle was a terrible UX — multiple seconds of re-indexing for a
    // visual preference. The cache is the storage; the dialog is the
    // presenter; we keep them properly separated. See docs/todos.md.
    m_showHidden = show;
}

void PathCacheManager::startScan()
{
    if (m_scanning.loadAcquire()) {
        return; // Already scanning
    }

    m_stopRequested.storeRelease(0);
    m_scanning.storeRelease(1);
    m_foldersIndexed.storeRelease(0);
    m_foldersExcluded.storeRelease(0);
    m_watcherLimitReached = false;
    {
        QMutexLocker locker(&m_mutex);
        m_currentScanRoot = QDir::homePath();
    }

    emit scanStarted();

    // Start background thread
    m_scanThread = QThread::create([this]() {
        performScan();
    });
    m_scanThread->start();
}

void PathCacheManager::stopScan()
{
    m_stopRequested.storeRelease(1);
    if (m_scanThread) {
        m_scanThread->wait();
        delete m_scanThread;
        m_scanThread = nullptr;
    }
    m_scanning.storeRelease(0);
}

void PathCacheManager::rescan()
{
    stopScan();
    m_store->clear();   // wipes folders AND files — one shared store
    {
        QMutexLocker locker(&m_mutex);
        m_completedRoots.clear();
    }
    m_capReached.storeRelease(0);
    m_ceilingReached.storeRelease(0);
    // The file cache is rebuilt by the same scan walks; clear it in lockstep.
    FileCacheManager::instance()->clear();
    // Clear watcher
    QStringList watched = m_watcher->directories();
    if (!watched.isEmpty()) {
        m_watcher->removePaths(watched);
    }
    startScan();
}

bool PathCacheManager::isScanning() const
{
    return m_scanning.loadAcquire() != 0;
}

bool PathCacheManager::isComplete() const
{
    return !isScanning() && folderCount() > 0;
}

int PathCacheManager::folderCount() const
{
    return m_store->count(PathStore::Folder);
}

QStringList PathCacheManager::cachedPaths() const
{
    return m_store->entries(PathStore::Folder);
}

QStringList PathCacheManager::search(const QString &query, const QString &rootPath, int maxResults) const
{
    const QStringList matches =
        m_store->search(query, PathStore::Folder, rootPath, maxResults);

    // "Subfolder of an existing result" suppression — folder-search only.
    // Store results come in insertion order (parents before children), so
    // the ≤ maxResults materialized paths are enough.
    QStringList results;
    for (const QString &path : matches) {
        bool isSubfolder = false;
        for (const QString &existing : results) {
            if (path.startsWith(existing + "/")) { isSubfolder = true; break; }
        }
        if (!isSubfolder) results.append(path);
    }
    return results;
}

QStringList PathCacheManager::getSubdirectories(const QString &parentPath) const
{
    QStringList results;
    QString prefix = parentPath.endsWith('/') ? parentPath : parentPath + "/";

    const QStringList paths = m_store->entries(PathStore::Folder);
    for (const QString &path : paths) {
        if (path.startsWith(prefix)) {
            // Get the immediate child directory name
            QString remainder = path.mid(prefix.length());
            qsizetype slashPos = remainder.indexOf('/');
            QString childName = (slashPos == -1) ? remainder : remainder.left(slashPos);

            QString childPath = prefix + childName;
            if (!results.contains(childPath)) {
                results.append(childPath);
            }
        }
    }

    results.sort(Qt::CaseInsensitive);
    return results;
}

void PathCacheManager::restartScanFrom(const QString &rootPath)
{
    QString normalizedRoot = rootPath.isEmpty() ? QDir::homePath() : QDir::cleanPath(rootPath);

    // Stop any current scan (but don't clear cache)
    stopScan();

    // Reset scan counters but keep cache intact
    m_foldersIndexed.storeRelease(0);
    m_foldersExcluded.storeRelease(0);

    // CRITICAL: Clear the stop flag BEFORE setting scanning flag
    // This must happen on the main thread before the scan thread starts
    // to prevent a race where workers see the old stop flag
    m_stopRequested.storeRelease(0);

    // Clear the queue from any previous scan
    {
        QMutexLocker locker(&m_queueMutex);
        m_scanQueue.clear();
    }

    // Drop the completed-root marker for this path so a rescan of the same
    // root actually re-walks it (otherwise scanWorker's "already covered"
    // shortcut bails immediately).
    {
        QMutexLocker locker(&m_mutex);
        m_completedRoots.remove(normalizedRoot);
        m_currentScanRoot = normalizedRoot;
    }

    m_scanning.storeRelease(1);

    emit scanStarted();

    // Start background thread for scan from new root
    m_scanThread = QThread::create([this, normalizedRoot]() {
        // Double-check stop wasn't requested between thread creation and start
        if (m_stopRequested.loadAcquire()) {
            m_scanning.storeRelease(0);
            return;
        }

        // Add the root path to cache first
        const qint32 rootNode = addPathToCache(normalizedRoot);

        // Initialize queue with the new root
        {
            QMutexLocker locker(&m_queueMutex);
            m_scanQueue.enqueue({normalizedRoot, rootNode});
        }

        // Use parallel workers
        m_numWorkers = qMin(QThread::idealThreadCount(), 8);
        if (m_numWorkers < 2) m_numWorkers = 2;

        m_activeWorkers.storeRelease(m_numWorkers);
        m_workersFinished.storeRelease(0);

        QList<QThread*> workers;
        for (int i = 0; i < m_numWorkers; ++i) {
            QThread *worker = QThread::create([this]() {
                scanWorker();
            });
            workers.append(worker);
            worker->start();
        }

        // Progress reporting
        QThread *progressThread = QThread::create([this]() {
            while (m_scanning.loadAcquire() && !m_stopRequested.loadAcquire()) {
                int indexed = m_foldersIndexed.loadAcquire();
                int excluded = m_foldersExcluded.loadAcquire();

                QMetaObject::invokeMethod(this, [this, indexed, excluded]() {
                    emit scanProgress(indexed, excluded, QString());
                    emit cacheUpdated();
                }, Qt::QueuedConnection);

                QThread::msleep(200);
            }
        });
        progressThread->start();

        for (QThread *worker : workers) {
            worker->wait();
            delete worker;
        }

        // Only mark as complete if we weren't stopped
        bool wasStopped = m_stopRequested.loadAcquire();

        m_scanning.storeRelease(0);

        progressThread->wait();
        delete progressThread;

        // Only mark root as completed and emit signals if scan finished normally
        if (!wasStopped) {
            {
                QMutexLocker locker(&m_mutex);
                m_completedRoots.insert(normalizedRoot);
            }

            int indexed = m_foldersIndexed.loadAcquire();
            int excluded = m_foldersExcluded.loadAcquire();

            QMetaObject::invokeMethod(this, [this, indexed, excluded]() {
                emit scanComplete(indexed, excluded);
                emit cacheUpdated();
            }, Qt::QueuedConnection);
        }
    });
    m_scanThread->start();
}

void PathCacheManager::expandTo(const QString &rootPath)
{
    if (rootPath.isEmpty()) {
        return;
    }

    QString normalizedRoot = QDir::cleanPath(rootPath);

    // Check if this root or a parent is already fully scanned
    {
        QMutexLocker locker(&m_mutex);
        for (const QString &completedRoot : m_completedRoots) {
            if (normalizedRoot.startsWith(completedRoot + "/") || normalizedRoot == completedRoot) {
                // Already covered by a completed scan
                return;
            }
        }

    }

    // Check if path is already in cache (we already have it)
    if (m_store->isEntry(m_store->find(normalizedRoot), PathStore::Folder)) {
        return;
    }

    // If currently scanning, add to queue instead of starting new scan
    if (m_scanning.loadAcquire()) {
        QMutexLocker locker(&m_queueMutex);
        // Add the root and its ancestors to queue
        QString current = normalizedRoot;
        while (!current.isEmpty() && current != "/") {
            m_scanQueue.enqueue({current, m_store->ensurePath(current)});
            QDir dir(current);
            dir.cdUp();
            QString parent = dir.absolutePath();
            if (parent == current) break;
            current = parent;
        }
        m_queueCondition.wakeAll();
        return;
    }

    // Start a targeted scan from the new root
    // This doesn't clear the existing cache
    m_stopRequested.storeRelease(0);

    // Clear the queue from any previous scan
    {
        QMutexLocker locker(&m_queueMutex);
        m_scanQueue.clear();
    }
    {
        QMutexLocker locker(&m_mutex);
        m_currentScanRoot = normalizedRoot;
    }

    m_scanning.storeRelease(1);

    emit scanStarted();

    // Start background thread for targeted scan
    m_scanThread = QThread::create([this, normalizedRoot]() {
        // Double-check stop wasn't requested between thread creation and start
        if (m_stopRequested.loadAcquire()) {
            m_scanning.storeRelease(0);
            return;
        }

        // Add the root path to cache first
        const qint32 rootNode = addPathToCache(normalizedRoot);

        // Initialize queue with the new root
        {
            QMutexLocker locker(&m_queueMutex);
            m_scanQueue.enqueue({normalizedRoot, rootNode});
        }

        // Use parallel workers
        m_numWorkers = qMin(QThread::idealThreadCount(), 8);
        if (m_numWorkers < 2) m_numWorkers = 2;

        m_activeWorkers.storeRelease(m_numWorkers);
        m_workersFinished.storeRelease(0);

        QList<QThread*> workers;
        for (int i = 0; i < m_numWorkers; ++i) {
            QThread *worker = QThread::create([this]() {
                scanWorker();
            });
            workers.append(worker);
            worker->start();
        }

        // Progress reporting
        QThread *progressThread = QThread::create([this]() {
            while (m_scanning.loadAcquire() && !m_stopRequested.loadAcquire()) {
                int indexed = m_foldersIndexed.loadAcquire();
                int excluded = m_foldersExcluded.loadAcquire();

                QMetaObject::invokeMethod(this, [this, indexed, excluded]() {
                    emit scanProgress(indexed, excluded, QString());
                    emit cacheUpdated();
                }, Qt::QueuedConnection);

                QThread::msleep(200);
            }
        });
        progressThread->start();

        for (QThread *worker : workers) {
            worker->wait();
            delete worker;
        }

        // Only mark as complete if we weren't stopped
        bool wasStopped = m_stopRequested.loadAcquire();

        m_scanning.storeRelease(0);

        progressThread->wait();
        delete progressThread;

        // Only mark root as completed and emit signals if scan finished normally
        if (!wasStopped) {
            {
                QMutexLocker locker(&m_mutex);
                m_completedRoots.insert(normalizedRoot);
            }

            int indexed = m_foldersIndexed.loadAcquire();
            int excluded = m_foldersExcluded.loadAcquire();

            QMetaObject::invokeMethod(this, [this, indexed, excluded]() {
                emit scanComplete(indexed, excluded);
                emit cacheUpdated();
            }, Qt::QueuedConnection);
        }
    });
    m_scanThread->start();
}

qint32 PathCacheManager::addPathToCache(const QString &path)
{
    const int hard = m_hardCeiling.loadAcquire();
    const int soft = m_softCap.loadAcquire();
    PathStore::Add status = PathStore::Add::Existed;
    const qint32 node = m_store->findOrCreatePath(
        path, PathStore::Folder, qMin(soft, hard), &status);
    if (status == PathStore::Add::CapBlocked) {
        if (m_store->count(PathStore::Folder) >= hard) {
            m_ceilingReached.storeRelease(1);
        } else if (!m_capReached.loadAcquire()) {
            m_capReached.storeRelease(1);
        }
    }
    return node;   // valid (possibly scaffold) even when cap-blocked
}

void PathCacheManager::onDirectoryChanged(const QString &path)
{
    // Ignore filesystem changes during scan transitions to avoid race conditions
    // that could incorrectly remove cached paths
    if (m_stopRequested.loadAcquire()) {
        return;
    }

    // A watched directory changed - rescan just that directory
    QDir dir(path);
    constexpr quint8 kBothKinds = (1u << PathStore::Folder) | (1u << PathStore::File);

    if (!dir.exists()) {
        // Directory was deleted - remove it and all children from cache
        // But only if we can confirm it's truly gone (not just temporarily unreadable)
        if (QFileInfo(path).exists()) {
            // Directory exists but is unreadable - don't remove from cache
            return;
        }
        const qint32 node = m_store->find(path);
        if (node >= 0) {
            QStringList deadFolders;
            m_store->markDeletedRecursive(node, kBothKinds, &deadFolders);
            for (const QString &p : deadFolders) m_watcher->removePath(p);
        }
        emit cacheUpdated();
        return;
    }

    // Standalone-app drift: always include hidden — eye-toggle is
    // presentational only. See docs/todos.md TODO 4.
    QDir::Filters scanFilters = QDir::Dirs | QDir::NoDotAndDotDot
                                | QDir::Readable | QDir::Hidden;
    QStringList currentEntries = dir.entryList(scanFilters);

    // Snapshot cached children (folders and files) of this directory —
    // childrenOf() instead of a full-cache sweep per fs event.
    const QString prefix = path.endsWith('/') ? path : path + "/";
    const qint32 dirNode = m_store->ensurePath(path);
    QHash<QString, qint32> cachedFolders, cachedFilesHere;
    for (qint32 c : m_store->childrenOf(dirNode)) {
        if (m_store->isEntry(c, PathStore::Folder)) {
            cachedFolders.insert(m_store->nameOf(c), c);
        } else if (m_store->isEntry(c, PathStore::File)) {
            cachedFilesHere.insert(m_store->nameOf(c), c);
        }
    }

    // Find new directories
    for (const QString &entry : currentEntries) {
        if (m_excludeSettings && m_excludeSettings->shouldExclude(entry)) {
            continue;
        }
        if (cachedFolders.contains(entry)) continue;
        // New directory - add to cache and watcher
        addPathToCache(prefix + entry);
        if (!m_watcherLimitReached) {
            if (!m_watcher->addPath(prefix + entry)) {
                m_watcherLimitReached = true;
            }
        }
    }

    // Find deleted directories
    // Only remove if the directory truly doesn't exist (not just unreadable)
    for (auto it = cachedFolders.cbegin(); it != cachedFolders.cend(); ++it) {
        if (currentEntries.contains(it.key())) continue;
        // Double-check: is the directory actually deleted, or just unreadable?
        if (QFileInfo(prefix + it.key()).exists()) continue;
        // Directory was truly deleted - remove it and all children
        QStringList deadFolders;
        m_store->markDeletedRecursive(it.value(), kBothKinds, &deadFolders);
        for (const QString &p : deadFolders) m_watcher->removePath(p);
    }

    // File-level diff: pick up newly created and removed files in this
    // directory. (Files don't get their own watcher entry; we piggyback on
    // the watched parent directory.)
    {
        QDir::Filters fileFilters = QDir::Files | QDir::NoDotAndDotDot
                                    | QDir::Readable | QDir::Hidden;
        const QStringList currentFiles = dir.entryList(fileFilters);
        FileCacheManager *fileCache = FileCacheManager::instance();

        // Added files.
        for (const QString &entry : currentFiles) {
            if (m_excludeSettings && m_excludeSettings->shouldExcludeFile(entry)) {
                continue;
            }
            if (!cachedFilesHere.contains(entry)) fileCache->addFile(prefix + entry);
        }

        // Removed files.
        for (auto it = cachedFilesHere.cbegin(); it != cachedFilesHere.cend(); ++it) {
            if (currentFiles.contains(it.key())) continue;
            if (!QFileInfo(prefix + it.key()).exists()) {
                fileCache->removeFile(prefix + it.key());
            }
        }
    }

    emit cacheUpdated();
}

// Path-level exclude list.
//
// These are absolute path prefixes that are unconditionally skipped by the
// scan, separate from the per-folder-name patterns in ExcludeSettings.
// Rationale: on macOS, paths like /System and /private contain hundreds
// of thousands of files that no human user ever searches for in a folder
// picker. Including them bloats the cache and slows every search.
//
// If you need to scan into these, comment out the entry — but in 99 %
// of cases the user wants `/` to mean "the user-visible parts of /".
//
// Match is prefix-based on the cleaned absolute path. Entries are
// matched as "exact match" or "starts with PATH/".
static const QStringList &pathLevelExcludes()
{
    static const QStringList kExcludes = {
        // OS-managed roots — millions of files nobody browses interactively.
        QStringLiteral("/System"),
        QStringLiteral("/private"),
        QStringLiteral("/dev"),
        QStringLiteral("/Volumes"),     // external drives — separately mounted
        QStringLiteral("/cores"),

        // BSD layout — package managers and system binaries.
        QStringLiteral("/usr"),         // /usr/share alone is millions of tiny files
        QStringLiteral("/Library"),     // system-wide app support
        QStringLiteral("/Applications"),// .app bundles already skipped, but this
                                        // also skips the directory itself
        QStringLiteral("/opt"),         // Homebrew, MacPorts
        QStringLiteral("/bin"),
        QStringLiteral("/sbin"),
        QStringLiteral("/etc"),

        // System-managed hidden directories at the root.
        QStringLiteral("/.fseventsd"),
        QStringLiteral("/.Spotlight-V100"),
        QStringLiteral("/.DocumentRevisions-V100"),
        QStringLiteral("/.PKInstallSandboxManager"),
        QStringLiteral("/.PKInstallSandboxManager-SystemSoftware"),
        QStringLiteral("/.Trashes"),
        QStringLiteral("/.TemporaryItems"),
        QStringLiteral("/.MobileBackups"),
        QStringLiteral("/.HFS+ Private Directory Data"),
    };
    return kExcludes;
}

static bool isPathLevelExcluded(const QString &absolutePath)
{
    for (const QString &prefix : pathLevelExcludes()) {
        if (absolutePath == prefix || absolutePath.startsWith(prefix + QLatin1Char('/'))) {
            return true;
        }
    }
    return false;
}

// Opaque bundle directories — these have a directory structure on disk
// (Application bundles, Photos library packages, etc.) but the user thinks
// of them as a single thing. Don't descend into them.
//
// We match by basename suffix (case-insensitive) so the rule applies anywhere
// in the tree (~/Pictures/Foo.photoslibrary, /Applications/Bar.app, etc.).
static const QStringList &opaqueBundleSuffixes()
{
    static const QStringList kBundles = {
        QStringLiteral(".app"),
        QStringLiteral(".photoslibrary"),
        QStringLiteral(".imovielibrary"),
        QStringLiteral(".musiclibrary"),
        QStringLiteral(".tvlibrary"),
        QStringLiteral(".aplibrary"),
    };
    return kBundles;
}

static bool isOpaqueBundle(const QString &basename)
{
    const QString lower = basename.toLower();
    for (const QString &suffix : opaqueBundleSuffixes()) {
        if (lower.endsWith(suffix)) return true;
    }
    return false;
}

// Child names of `dirPath` that fall on the path-level exclude list —
// resolved once per directory so the per-entry loop stays string-free.
static QSet<QString> pathLevelExcludedChildren(const QString &dirPath)
{
    QSet<QString> names;
    const QString prefix = dirPath.endsWith(QLatin1Char('/'))
                               ? dirPath : dirPath + QLatin1Char('/');
    for (const QString &ex : pathLevelExcludes()) {
        if (ex.startsWith(prefix)) {
            const QString rest = ex.mid(prefix.length());
            if (!rest.isEmpty() && !rest.contains(QLatin1Char('/'))) {
                names.insert(rest);
            }
        }
    }
    return names;
}

void PathCacheManager::scanWorker()
{
    while (!m_stopRequested.loadAcquire()) {
        QString currentPath;
        qint32 currentNode = -1;

        // Get next directory from queue
        {
            QMutexLocker locker(&m_queueMutex);

            // Wait for work or termination
            while (m_scanQueue.isEmpty() && !m_stopRequested.loadAcquire()) {
                // Mark this worker as idle (waiting for work)
                int wasActive = m_activeWorkers.fetchAndSubRelaxed(1);

                // If we were the last active worker and queue is empty, scan is complete
                // But we must re-check the queue while still holding the mutex to avoid race
                if (wasActive == 1 && m_scanQueue.isEmpty()) {
                    // Double-check: no more work and no active workers = done
                    m_queueCondition.wakeAll();
                    return;
                }

                // Wait for more work (with timeout to recheck stop flag)
                m_queueCondition.wait(&m_queueMutex, 100);

                // Re-increment to mark as active again before checking queue
                m_activeWorkers.fetchAndAddRelaxed(1);

                // If stop was requested while waiting, exit
                if (m_stopRequested.loadAcquire()) {
                    return;
                }

                // Loop back to check queue again
            }

            if (m_stopRequested.loadAcquire()) {
                return;
            }

            if (m_scanQueue.isEmpty()) {
                continue;
            }

            const ScanItem item = m_scanQueue.dequeue();
            currentPath = item.path;
            currentNode = item.node;
        }

        // Check if this path is under an already-completed root (skip if so)
        {
            QMutexLocker locker(&m_mutex);
            bool alreadyCovered = false;
            for (const QString &completedRoot : m_completedRoots) {
                if (currentPath.startsWith(completedRoot + "/") || currentPath == completedRoot) {
                    alreadyCovered = true;
                    break;
                }
            }
            if (alreadyCovered) {
                continue;
            }
        }

        // Process this directory
        QDir dir(currentPath);
        if (currentNode < 0 || !dir.exists() || !dir.isReadable()) {
            continue;
        }

        // Path-level system excludes (/System, /private, /dev, …).
        // Separate from the name-pattern excludes because we want them
        // unconditionally enforced — these directories are never useful
        // for personal-file search and add millions of entries.
        if (isPathLevelExcluded(currentPath)) {
            continue;
        }

        // Standalone-app drift: always include hidden in the cache.
        // Eye-toggle is now purely presentational — filtered out by the
        // search worker / tree view. See docs/todos.md TODO 4.
        QDir::Filters folderFilters = QDir::Dirs | QDir::NoDotAndDotDot
                                      | QDir::Readable | QDir::Hidden;
        const QStringList folderEntries = dir.entryList(folderFilters);
        const QSet<QString> pathExcluded = pathLevelExcludedChildren(currentPath);

        QStringList newNames;
        for (const QString &entry : folderEntries) {
            if (m_stopRequested.loadAcquire()) {
                break;
            }
            // Name-pattern exclusions (user-configurable list) + the
            // path-level excludes that land exactly at this directory.
            if ((m_excludeSettings && m_excludeSettings->shouldExclude(entry))
                || pathExcluded.contains(entry)) {
                m_foldersExcluded.fetchAndAddRelaxed(1);
                continue;
            }
            newNames.append(entry);
        }

        // One atomic batch per directory — no per-entry path strings, no
        // per-entry dedupe lookups (the store snapshots existing children
        // once when the directory was listed before).
        const int hard = m_hardCeiling.loadAcquire();
        const int soft = m_softCap.loadAcquire();
        const PathStore::Ingest res = m_store->ingestListing(
            currentNode, newNames, PathStore::Folder, qMin(soft, hard));
        if (res.added > 0) {
            m_foldersIndexed.fetchAndAddRelaxed(res.added);
        }
        if (res.capHit) {
            if (m_store->count(PathStore::Folder) >= hard) {
                if (!m_ceilingReached.loadAcquire()) {
                    m_ceilingReached.storeRelease(1);
                    QMetaObject::invokeMethod(this,
                        [this]() { emit folderCeilingReachedSignal(); },
                        Qt::QueuedConnection);
                }
            } else if (!m_capReached.loadAcquire()) {
                m_capReached.storeRelease(1);
                QMetaObject::invokeMethod(this,
                    [this]() { emit folderCapReachedSignal(); },
                    Qt::QueuedConnection);
            }
        }

        // Also enumerate files in the same directory and feed them to the
        // file cache. One scan walk, two destinations. Files are checked
        // against the dedicated file-exclude pattern list, not the folder one.
        if (!m_stopRequested.loadAcquire()) {
            QDir::Filters fileFilters = QDir::Files | QDir::NoDotAndDotDot
                                        | QDir::Readable | QDir::Hidden;
            const QStringList fileEntries = dir.entryList(fileFilters);
            QStringList fileNames;
            for (const QString &entry : fileEntries) {
                if (m_stopRequested.loadAcquire()) break;
                if (m_excludeSettings && m_excludeSettings->shouldExcludeFile(entry)) {
                    continue;
                }
                fileNames.append(entry);
            }
            FileCacheManager::instance()->ingestScan(currentNode, currentPath,
                                                     fileNames);
        }

        // Enqueue new (or re-livened) folders for descent. Opaque bundles
        // (.app, .photoslibrary, …) stay cached but are never descended —
        // the user thinks of them as a single thing. Cap-blocked children
        // were not cached, so they are not descended either.
        {
            QMutexLocker locker(&m_queueMutex);
            bool queued = false;
            for (qsizetype i = 0; i < res.nodes.size(); ++i) {
                const qint32 node = res.nodes.at(i);
                if (node < 0 || isOpaqueBundle(newNames.at(i))) continue;
                m_scanQueue.enqueue({currentPath + "/" + newNames.at(i), node});
                queued = true;
            }
            if (queued) m_queueCondition.wakeAll();
        }
    }
}

void PathCacheManager::performScan()
{
    QString homeDir = QDir::homePath();

    // Initialize queue with home directory (scaffold node — the home dir
    // itself is not a cache entry, matching the original behavior).
    const qint32 homeNode = m_store->ensurePath(homeDir);
    {
        QMutexLocker locker(&m_queueMutex);
        m_scanQueue.clear();
        m_scanQueue.enqueue({homeDir, homeNode});
    }

    // Add home dir to watcher
    QMetaObject::invokeMethod(m_watcher, [this, homeDir]() {
        m_watcher->addPath(homeDir);
    }, Qt::QueuedConnection);

    // Determine number of workers (use available cores, max 8)
    m_numWorkers = qMin(QThread::idealThreadCount(), 8);
    if (m_numWorkers < 2) m_numWorkers = 2;

    m_activeWorkers.storeRelease(m_numWorkers);
    m_workersFinished.storeRelease(0);

    // Start worker threads
    QList<QThread*> workers;
    for (int i = 0; i < m_numWorkers; ++i) {
        QThread *worker = QThread::create([this]() {
            scanWorker();
        });
        workers.append(worker);
        worker->start();
    }

    // Progress reporting thread
    QThread *progressThread = QThread::create([this]() {
        while (m_scanning.loadAcquire() && !m_stopRequested.loadAcquire()) {
            int indexed = m_foldersIndexed.loadAcquire();
            int excluded = m_foldersExcluded.loadAcquire();

            QMetaObject::invokeMethod(this, [this, indexed, excluded]() {
                emit scanProgress(indexed, excluded, QString());
                emit cacheUpdated();
            }, Qt::QueuedConnection);

            QThread::msleep(200); // Update 5x per second
        }
    });
    progressThread->start();

    // Wait for all workers to finish
    for (QThread *worker : workers) {
        worker->wait();
        delete worker;
    }

    // Only mark as complete if we weren't stopped
    bool wasStopped = m_stopRequested.loadAcquire();

    m_scanning.storeRelease(0);

    // Stop progress thread
    progressThread->wait();
    delete progressThread;

    // Only mark root as completed and emit signals if scan finished normally
    if (!wasStopped) {
        {
            QMutexLocker locker(&m_mutex);
            m_completedRoots.insert(homeDir);
        }

        int indexed = m_foldersIndexed.loadAcquire();
        int excluded = m_foldersExcluded.loadAcquire();

        // Emit completion on main thread
        QMetaObject::invokeMethod(this, [this, indexed, excluded]() {
            emit scanComplete(indexed, excluded);
            emit cacheUpdated();
        }, Qt::QueuedConnection);
    }
}
