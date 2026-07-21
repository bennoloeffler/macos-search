#include "PathCacheManager.h"
#include "ExcludeSettings.h"
#include "FileCacheManager.h"
#include <QDir>
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
    QMutexLocker locker(&m_mutex);
    if (m_paths.size() >= newCap) {
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
    QMutexLocker locker(&m_mutex);
    if (m_paths.size() >= newCeiling) {
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
    {
        QMutexLocker locker(&m_mutex);
        m_paths.clear();
        m_lowerPaths.clear();
        m_pathSet.clear();
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
    QMutexLocker locker(&m_mutex);
    return static_cast<int>(m_paths.size());
}

QStringList PathCacheManager::cachedPaths() const
{
    QMutexLocker locker(&m_mutex);
    return m_paths;
}

QStringList PathCacheManager::search(const QString &query, const QString &rootPath, int maxResults) const
{
    if (query.isEmpty()) {
        return {};
    }

    QStringList terms = query.toLower().split(' ', Qt::SkipEmptyParts);
    if (terms.isEmpty()) {
        return {};
    }

    QStringList results;

    QMutexLocker locker(&m_mutex);
    const int n = static_cast<int>(m_paths.size());
    const QString rootPrefix = rootPath.isEmpty() ? QString() : rootPath + "/";
    for (int i = 0; i < n; ++i) {
        const QString &path = m_paths.at(i);
        if (!rootPath.isEmpty() && !path.startsWith(rootPrefix) && path != rootPath) {
            continue;
        }
        // Use the pre-lowercased parallel array so we don't pay toLower()
        // per path per query.
        const QString &lowerPath = m_lowerPaths.at(i);
        bool allMatch = true;
        for (const QString &term : terms) {
            if (!lowerPath.contains(term)) { allMatch = false; break; }
        }
        if (!allMatch) continue;

        // "Subfolder of an existing result" suppression — folder-search only.
        bool isSubfolder = false;
        for (const QString &existing : results) {
            if (path.startsWith(existing + "/")) { isSubfolder = true; break; }
        }
        if (!isSubfolder) {
            results.append(path);
            if (results.size() >= maxResults) break;
        }
    }

    return results;
}

QStringList PathCacheManager::getSubdirectories(const QString &parentPath) const
{
    QStringList results;
    QString prefix = parentPath.endsWith('/') ? parentPath : parentPath + "/";

    QMutexLocker locker(&m_mutex);
    for (const QString &path : m_paths) {
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
        addPathToCache(normalizedRoot);

        // Initialize queue with the new root
        {
            QMutexLocker locker(&m_queueMutex);
            m_scanQueue.enqueue(normalizedRoot);
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

        // Check if path is already in cache (we already have it)
        if (m_pathSet.contains(normalizedRoot)) {
            return;
        }
    }

    // If currently scanning, add to queue instead of starting new scan
    if (m_scanning.loadAcquire()) {
        QMutexLocker locker(&m_queueMutex);
        // Add the root and its ancestors to queue
        QString current = normalizedRoot;
        while (!current.isEmpty() && current != "/") {
            m_scanQueue.enqueue(current);
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
        addPathToCache(normalizedRoot);

        // Initialize queue with the new root
        {
            QMutexLocker locker(&m_queueMutex);
            m_scanQueue.enqueue(normalizedRoot);
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

void PathCacheManager::addPathToCache(const QString &path)
{
    if (m_ceilingReached.loadAcquire()) return;
    QMutexLocker locker(&m_mutex);
    if (m_pathSet.contains(path)) return;
    const int hard = m_hardCeiling.loadAcquire();
    if (m_paths.size() >= hard) {
        m_ceilingReached.storeRelease(1);
        return;
    }
    if (m_paths.size() >= m_softCap.loadAcquire()) {
        if (!m_capReached.loadAcquire()) m_capReached.storeRelease(1);
        return;
    }
    m_paths.append(path);
    m_lowerPaths.append(path.toLower());
    m_pathSet.insert(path);
}

void PathCacheManager::removePathFromCache(const QString &path)
{
    QMutexLocker locker(&m_mutex);
    if (m_pathSet.contains(path)) {
        const int idx = m_paths.indexOf(path);
        if (idx >= 0) {
            m_paths.removeAt(idx);
            m_lowerPaths.removeAt(idx);
        }
        m_pathSet.remove(path);
    }
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

    if (!dir.exists()) {
        // Directory was deleted - remove it and all children from cache
        // But only if we can confirm it's truly gone (not just temporarily unreadable)
        QFileInfo info(path);
        if (info.exists()) {
            // Directory exists but is unreadable - don't remove from cache
            return;
        }

        {
            QMutexLocker locker(&m_mutex);
            for (int i = m_paths.size() - 1; i >= 0; --i) {
                const QString &cached = m_paths.at(i);
                if (cached == path || cached.startsWith(path + "/")) {
                    m_pathSet.remove(cached);
                    m_watcher->removePath(cached);
                    m_paths.removeAt(i);
                    m_lowerPaths.removeAt(i);
                }
            }
        }
        // Also drop every file under the dead directory.
        FileCacheManager::instance()->removeFile(path);
        FileCacheManager::instance()->removeFilesUnder(path);
        emit cacheUpdated();
        return;
    }

    // Standalone-app drift: always include hidden — eye-toggle is
    // presentational only. See docs/todos.md TODO 4.
    QDir::Filters scanFilters = QDir::Dirs | QDir::NoDotAndDotDot
                                | QDir::Readable | QDir::Hidden;
    QStringList currentEntries = dir.entryList(scanFilters);

    // Get cached children of this directory
    QSet<QString> cachedChildren;
    {
        QMutexLocker locker(&m_mutex);
        for (const QString &cached : m_paths) {
            if (cached.startsWith(path + "/")) {
                // Extract immediate child
                QString relative = cached.mid(path.length() + 1);
                qsizetype slashPos = relative.indexOf('/');
                if (slashPos == -1) {
                    cachedChildren.insert(cached);
                }
            }
        }
    }

    // Find new directories
    for (const QString &entry : currentEntries) {
        if (m_excludeSettings && m_excludeSettings->shouldExclude(entry)) {
            continue;
        }

        QString fullPath = path + "/" + entry;
        if (!cachedChildren.contains(fullPath)) {
            // New directory - add to cache and watcher
            addPathToCache(fullPath);
            if (!m_watcherLimitReached) {
                if (!m_watcher->addPath(fullPath)) {
                    m_watcherLimitReached = true;
                }
            }
        }
    }

    // Find deleted directories
    // Only remove if the directory truly doesn't exist (not just unreadable)
    for (const QString &cached : cachedChildren) {
        QString name = cached.mid(path.length() + 1);
        if (!currentEntries.contains(name)) {
            // Double-check: is the directory actually deleted, or just unreadable?
            QFileInfo info(cached);
            if (info.exists()) {
                // Directory exists but wasn't in entryList (unreadable) - keep in cache
                continue;
            }

            // Directory was truly deleted - remove it and all children
            {
                QMutexLocker locker(&m_mutex);
                for (int i = m_paths.size() - 1; i >= 0; --i) {
                    const QString &p = m_paths.at(i);
                    if (p == cached || p.startsWith(cached + "/")) {
                        m_pathSet.remove(p);
                        m_watcher->removePath(p);
                        m_paths.removeAt(i);
                        m_lowerPaths.removeAt(i);
                    }
                }
            }
            FileCacheManager::instance()->removeFilesUnder(cached);
        }
    }

    // File-level diff: pick up newly created and removed files in this
    // directory. (Files don't get their own watcher entry; we piggyback on
    // the watched parent directory.)
    {
        QDir::Filters fileFilters = QDir::Files | QDir::NoDotAndDotDot
                                    | QDir::Readable | QDir::Hidden;
        const QStringList currentFiles = dir.entryList(fileFilters);
        FileCacheManager *fileCache = FileCacheManager::instance();

        // Snapshot of cached files directly under `path` (one level deep).
        QStringList cachedFilesHere;
        const QString prefix = path.endsWith('/') ? path : path + "/";
        for (const QString &p : fileCache->cachedFiles()) {
            if (p.startsWith(prefix)) {
                const QString remainder = p.mid(prefix.length());
                if (!remainder.contains('/')) cachedFilesHere.append(p);
            }
        }

        // Added files.
        for (const QString &entry : currentFiles) {
            if (m_excludeSettings && m_excludeSettings->shouldExcludeFile(entry)) {
                continue;
            }
            const QString full = prefix + entry;
            if (!fileCache->contains(full)) fileCache->addFile(full);
        }

        // Removed files.
        for (const QString &cached : cachedFilesHere) {
            const QString name = cached.mid(prefix.length());
            if (!currentFiles.contains(name)) {
                QFileInfo info(cached);
                if (!info.exists()) fileCache->removeFile(cached);
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

void PathCacheManager::scanWorker()
{
    while (!m_stopRequested.loadAcquire()) {
        QString currentPath;

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

            currentPath = m_scanQueue.dequeue();
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
        if (!dir.exists() || !dir.isReadable()) {
            continue;
        }

        // Standalone-app drift: always include hidden in the cache.
        // Eye-toggle is now purely presentational — filtered out by the
        // search worker / tree view. See docs/todos.md TODO 4.
        QDir::Filters folderFilters = QDir::Dirs | QDir::NoDotAndDotDot
                                      | QDir::Readable | QDir::Hidden;
        QStringList folderEntries = dir.entryList(folderFilters);
        QStringList newPaths;

        for (const QString &entry : folderEntries) {
            if (m_stopRequested.loadAcquire()) {
                break;
            }

            // Check name-pattern exclusions (user-configurable list).
            if (m_excludeSettings && m_excludeSettings->shouldExclude(entry)) {
                m_foldersExcluded.fetchAndAddRelaxed(1);
                continue;
            }

            // Opaque bundles (.app, .photoslibrary, ...) are added to the
            // folder cache so they remain selectable in the picker, but we
            // do NOT descend into them — they're treated as a single thing.
            // Skip the queue-enqueue step for these.
            const bool isBundle = isOpaqueBundle(entry);

            QString fullPath = currentPath + "/" + entry;

            // Path-level system excludes (/System, /private, /dev, …).
            // Separate from the name-pattern excludes because we want them
            // unconditionally enforced — these directories are never useful
            // for personal-file search and add millions of entries.
            if (isPathLevelExcluded(fullPath)) {
                m_foldersExcluded.fetchAndAddRelaxed(1);
                continue;
            }

            // Skip if already in cache (already scanned)
            {
                QMutexLocker locker(&m_mutex);
                if (m_pathSet.contains(fullPath)) {
                    continue;
                }
            }

            // Bundles go into the folder cache but don't get enqueued for
            // further scanning. Regular folders are added to both the cache
            // and the work queue.
            if (isBundle) {
                if (m_ceilingReached.loadAcquire()) continue;
                QMutexLocker locker(&m_mutex);
                if (!m_pathSet.contains(fullPath)) {
                    const int hard = m_hardCeiling.loadAcquire();
                    if (m_paths.size() >= hard) {
                        m_ceilingReached.storeRelease(1);
                        continue;
                    }
                    if (m_paths.size() >= m_softCap.loadAcquire()) {
                        if (!m_capReached.loadAcquire()) {
                            m_capReached.storeRelease(1);
                            QMetaObject::invokeMethod(this,
                                [this]() { emit folderCapReachedSignal(); },
                                Qt::QueuedConnection);
                        }
                        continue;
                    }
                    m_paths.append(fullPath);
                    m_lowerPaths.append(fullPath.toLower());
                    m_pathSet.insert(fullPath);
                    m_foldersIndexed.fetchAndAddRelaxed(1);
                }
            } else {
                newPaths.append(fullPath);
            }
        }

        // Also enumerate files in the same directory and feed them to the
        // file cache. One scan walk, two destinations. Files are checked
        // against the dedicated file-exclude pattern list, not the folder one.
        if (!m_stopRequested.loadAcquire()) {
            QDir::Filters fileFilters = QDir::Files | QDir::NoDotAndDotDot
                                        | QDir::Readable | QDir::Hidden;
            QStringList fileEntries = dir.entryList(fileFilters);
            FileCacheManager *fileCache = FileCacheManager::instance();
            for (const QString &entry : fileEntries) {
                if (m_stopRequested.loadAcquire()) break;
                if (m_excludeSettings && m_excludeSettings->shouldExcludeFile(entry)) {
                    continue;
                }
                QString fullFilePath = currentPath + "/" + entry;
                fileCache->addFile(fullFilePath);
            }
        }

        // Add folders to cache and queue
        if (!newPaths.isEmpty()) {
            {
                QMutexLocker locker(&m_mutex);
                const int hard = m_hardCeiling.loadAcquire();
                const int soft = m_softCap.loadAcquire();
                bool emittedCap = false;
                bool emittedCeiling = false;
                for (const QString &p : newPaths) {
                    if (m_paths.size() >= hard) {
                        if (!m_ceilingReached.loadAcquire()) {
                            m_ceilingReached.storeRelease(1);
                            emittedCeiling = true;
                        }
                        break;
                    }
                    if (m_paths.size() >= soft) {
                        if (!m_capReached.loadAcquire()) {
                            m_capReached.storeRelease(1);
                            emittedCap = true;
                        }
                        break;
                    }
                    if (!m_pathSet.contains(p)) {
                        m_paths.append(p);
                        m_lowerPaths.append(p.toLower());
                        m_pathSet.insert(p);
                        m_foldersIndexed.fetchAndAddRelaxed(1);
                    }
                }
                if (emittedCap) {
                    QMetaObject::invokeMethod(this,
                        [this]() { emit folderCapReachedSignal(); },
                        Qt::QueuedConnection);
                }
                if (emittedCeiling) {
                    QMetaObject::invokeMethod(this,
                        [this]() { emit folderCeilingReachedSignal(); },
                        Qt::QueuedConnection);
                }
            }

            // Add to queue for further processing — unless BOTH caches are
            // capped. Once neither folders nor files can be added, descending
            // further is pure disk churn: stop enqueueing so the queue drains
            // and the scan completes instead of walking the whole disk.
            FileCacheManager *fc = FileCacheManager::instance();
            const bool foldersCapped = m_capReached.loadAcquire()
                                       || m_ceilingReached.loadAcquire();
            const bool filesCapped = fc->capReached() || fc->ceilingReached();
            if (!(foldersCapped && filesCapped)) {
                QMutexLocker locker(&m_queueMutex);
                for (const QString &p : newPaths) {
                    m_scanQueue.enqueue(p);
                }
                m_queueCondition.wakeAll();
            }
        }
    }
}

void PathCacheManager::performScan()
{
    QString homeDir = QDir::homePath();

    // Initialize queue with home directory
    {
        QMutexLocker locker(&m_queueMutex);
        m_scanQueue.clear();
        m_scanQueue.enqueue(homeDir);
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
