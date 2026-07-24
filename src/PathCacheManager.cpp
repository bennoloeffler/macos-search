#include "PathCacheManager.h"
#include "ExcludeSettings.h"
#include "FileCacheManager.h"
#include "MaudeConfig.h"
#include "PathStore.h"
#include <QCryptographicHash>
#include <QDir>
#include <QHash>
#include "FsEventsWatcher.h"

// Defined below; used by onDirectoryChanged/onRescanNeeded above their bodies.
static bool isPathLevelExcluded(const QString &absolutePath);
static bool isOpaqueBundle(const QString &basename);
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <QQueue>
#include <QMetaObject>
#include <QtConcurrent>
#include <QThreadPool>
#include <malloc/malloc.h>
#include <dirent.h>

namespace {
// Join a directory path and a child name without ever producing a leading
// "//". The scan root can be "/" (the "Macintosh HD" favorite); a naive
// dir + "/" + name yields "//Users", and since every path-level exclude uses a
// single-slash prefix, the double slash silently bypasses ALL excludes AND the
// completed-root dedup — the scan then walked straight into ~/Library's app
// containers and blocked on a TCC "access data from other apps" prompt.
inline QString joinPath(const QString &dir, const QString &name)
{
    return dir.endsWith(QLatin1Char('/')) ? dir + name
                                          : dir + QLatin1Char('/') + name;
}

// Count a directory's direct entries (excluding . and ..), bailing as soon as
// the count exceeds `cap`. POSIX readdir — no per-entry stat, unlike
// QDir::entryInfoList — so learning "this dir is huge" costs O(cap), never
// O(actual) on a million-entry dir. Returns cap+1 as the sentinel for "over".
int countChildrenCapped(const QString &path, int cap)
{
    DIR *d = ::opendir(QFile::encodeName(path).constData());
    if (!d) return 0;
    int n = 0;
    while (struct dirent *e = ::readdir(d)) {
        const char *nm = e->d_name;
        if (nm[0] == '.' && (nm[1] == '\0' || (nm[1] == '.' && nm[2] == '\0')))
            continue;                       // skip . and ..
        if (++n > cap) break;               // over the cap — stop early
    }
    ::closedir(d);
    return n;
}
}  // namespace

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
    // Always start at the default per-dir guard — never persisted (by design).
    m_maxDirChildren.storeRelease(kDefaultMaxDirChildren);

    // One recursive FSEvents stream, armed with the completed scan roots
    // after each scan (see finishScan). Lives on the main thread; its
    // signals arrive here on the main thread.
    m_fsWatcher = new FsEventsWatcher(0.5, this);
    connect(m_fsWatcher, &FsEventsWatcher::directoryChanged,
            this, &PathCacheManager::onDirectoryChanged);
    connect(m_fsWatcher, &FsEventsWatcher::rescanNeeded,
            this, &PathCacheManager::onRescanNeeded);

    // Throttle live UI refreshes to at most ~4/sec (see scheduleLiveCacheUpdate).
    m_liveUpdateThrottle = new QTimer(this);
    m_liveUpdateThrottle->setSingleShot(true);
    m_liveUpdateThrottle->setInterval(250);
    connect(m_liveUpdateThrottle, &QTimer::timeout,
            this, [this]() { emit cacheUpdated(); });
}

void PathCacheManager::scheduleLiveCacheUpdate()
{
    // Collapse a burst of change events into one delayed cacheUpdated().
    if (!m_liveUpdateThrottle->isActive()) m_liveUpdateThrottle->start();
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
    // Reconcile semantics: the user explicitly clicked "Scan now", so a
    // "path already known" early-return would silently no-op the click.
    reconcileTo(rootPath);
}

bool PathCacheManager::isPathUnderRoot(const QString &path, const QString &root)
{
    if (root.isEmpty() || path.isEmpty()) return false;
    if (path == root) return true;
    if (root == QLatin1String("/")) return path.startsWith(QLatin1Char('/'));
    return path.startsWith(root + QLatin1Char('/'));
}

bool PathCacheManager::isPathScanned(const QString &path) const
{
    if (path.isEmpty()) return false;
    const QString clean = QDir::cleanPath(path);
    QMutexLocker locker(&m_mutex);
    for (const QString &root : m_completedRoots) {
        if (isPathUnderRoot(clean, root)) {
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
    // Under the scan root, or an ancestor of it (a scan of "/" means every
    // favorite is "being scanned"; a scan of ~/Documents means ~ is too).
    return isPathUnderRoot(clean, root) || isPathUnderRoot(root, clean);
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
    // Stop watching during the full rebuild; finishScan re-arms the stream
    // with the fresh completed roots.
    m_fsWatcher->stop();
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

        // Fresh generation for this scan run (docs/210 mark-and-sweep).
        m_store->beginScanGeneration();

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

        finishScan(normalizedRoot, progressThread);
    });
    m_scanThread->start();
}

void PathCacheManager::expandTo(const QString &rootPath)
{
    expandToImpl(rootPath, /*forceReconcile=*/false);
}

void PathCacheManager::reconcileTo(const QString &rootPath)
{
    expandToImpl(rootPath, /*forceReconcile=*/true);
}

void PathCacheManager::expandToImpl(const QString &rootPath, bool forceReconcile)
{
    if (rootPath.isEmpty()) {
        return;
    }

    QString normalizedRoot = QDir::cleanPath(rootPath);

    // Check if this root or a parent is already fully scanned
    {
        QMutexLocker locker(&m_mutex);
        for (const QString &completedRoot : m_completedRoots) {
            if (isPathUnderRoot(normalizedRoot, completedRoot)) {
                // Already covered by a completed scan
                return;
            }
        }

    }

    // Check if path is already in cache (we already have it). A reconcile
    // pass must NOT take this exit: after a warm start every snapshot path is
    // "known" here, yet still needs its mark-and-sweep re-walk (and its
    // completedRoots entry, which arms FSEvents and turns the dots green).
    if (!forceReconcile
        && m_store->isEntry(m_store->find(normalizedRoot), PathStore::Folder)) {
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

        // Fresh generation for this scan run (docs/210 mark-and-sweep).
        m_store->beginScanGeneration();

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

        finishScan(normalizedRoot, progressThread);
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
    // Cheap guards on the MAIN thread; the heavy diff (entryList on a
    // possibly-huge changed directory — a 100k-entry dir once blocked the UI
    // for 13 s) is dispatched to a background thread. Deduped by path so an
    // FSEvents flood for the same dir doesn't pile up tasks.
    if (m_stopRequested.loadAcquire() || m_scanning.loadAcquire()) return;
    const QString clean = QDir::cleanPath(path);
    if (isPathLevelExcluded(clean)) return;
    {
        const QString base = QFileInfo(clean).fileName();
        if (m_excludeSettings && m_excludeSettings->shouldExclude(base)) return;
    }
    if (m_store->find(clean) < 0) return;   // only diff a tracked directory
    if (m_dirDiffScheduled.contains(clean)) return;
    m_dirDiffScheduled.insert(clean);
    QThreadPool::globalInstance()->start([this, clean]() {
        diffDirectory(clean);
        QMetaObject::invokeMethod(this, [this, clean]() {
            m_dirDiffScheduled.remove(clean);
            scheduleLiveCacheUpdate();
        }, Qt::QueuedConnection);
    });
}

void PathCacheManager::diffDirectory(const QString &clean)
{
    // Runs on a background thread pool. The store and file cache are internally
    // locked; only the completion (scheduleLiveCacheUpdate + dedup removal) is
    // marshalled back to the main thread by the caller.
    if (m_stopRequested.loadAcquire()) return;
    constexpr quint8 kBothKinds = (1u << PathStore::Folder) | (1u << PathStore::File);
    const qint32 dirNode = m_store->find(clean);
    if (dirNode < 0) return;

    QDir dir(clean);
    if (!dir.exists()) {
        if (QFileInfo(clean).exists()) return;   // unreadable, not truly gone
        m_store->markDeletedRecursive(dirNode, kBothKinds, nullptr);
        return;
    }

    // Huge-directory guard (same rule as the scan): don't diff/read/descend a
    // cache/backup/build blob. Its name stays indexed; contents are skipped.
    {
        const int maxChildren = m_maxDirChildren.loadAcquire();
        if (maxChildren > 0
            && countChildrenCapped(clean, maxChildren) > maxChildren) {
            return;
        }
    }

    // NoSymLinks: never follow cloud-storage symlinks (privacy prompts / loops).
    const QDir::Filters folderFilters = QDir::Dirs | QDir::NoDotAndDotDot
                                | QDir::Readable | QDir::Hidden | QDir::NoSymLinks;
    const QStringList currentEntries = dir.entryList(folderFilters);

    // Root "/" is already "/"; anything else gets a single trailing slash.
    // (A naive clean + "/" makes "//" at the root — see joinPath().)
    const QString prefix = clean.endsWith(QLatin1Char('/')) ? clean : clean + QLatin1Char('/');
    QHash<QString, qint32> cachedFolders, cachedFilesHere;
    for (qint32 c : m_store->childrenOf(dirNode)) {
        if (m_store->isEntry(c, PathStore::Folder)) cachedFolders.insert(m_store->nameOf(c), c);
        else if (m_store->isEntry(c, PathStore::File)) cachedFilesHere.insert(m_store->nameOf(c), c);
    }

    // New directories: index the name; descend (on THIS bg thread) unless it's
    // a bundle or a name-excluded dir (node_modules/build/… — name only).
    for (const QString &entry : currentEntries) {
        if (cachedFolders.contains(entry)) continue;
        const QString childPath = prefix + entry;
        addPathToCache(childPath);
        if (isOpaqueBundle(entry)
            || (m_excludeSettings && m_excludeSettings->shouldExclude(entry)))
            continue;
        indexNewSubtree(childPath);
    }

    // Deleted directories (truly gone, not just unreadable).
    for (auto it = cachedFolders.cbegin(); it != cachedFolders.cend(); ++it) {
        if (currentEntries.contains(it.key())) continue;
        if (QFileInfo(prefix + it.key()).exists()) continue;
        m_store->markDeletedRecursive(it.value(), kBothKinds, nullptr);
    }

    // File-level diff (files piggyback on the parent-dir event).
    {
        const QDir::Filters fileFilters = QDir::Files | QDir::NoDotAndDotDot
                                    | QDir::Readable | QDir::Hidden;
        const QStringList currentFiles = dir.entryList(fileFilters);
        FileCacheManager *fileCache = FileCacheManager::instance();
        for (const QString &entry : currentFiles) {
            if (m_excludeSettings && m_excludeSettings->shouldExcludeFile(entry)) continue;
            if (!cachedFilesHere.contains(entry)) fileCache->addFile(prefix + entry);
        }
        for (auto it = cachedFilesHere.cbegin(); it != cachedFilesHere.cend(); ++it) {
            if (currentFiles.contains(it.key())) continue;
            if (!QFileInfo(prefix + it.key()).exists()) fileCache->removeFile(prefix + it.key());
        }
    }
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
    static const QStringList kExcludes = []() {
        QStringList l = {
        // OS-managed roots — millions of files nobody browses interactively.
        QStringLiteral("/System"),
        QStringLiteral("/private"),
        QStringLiteral("/dev"),
        QStringLiteral("/Volumes"),     // external drives — separately mounted
        QStringLiteral("/cores"),

        // BSD layout — package managers and system binaries.
        QStringLiteral("/usr"),         // /usr/share alone is millions of tiny files
        QStringLiteral("/Library"),     // system-wide app support
        // NOTE: /Applications is intentionally NOT excluded — users search for
        // apps. Its .app bundles are opaque leaves (isOpaqueBundle), so it
        // costs only a few hundred entries, and it is scanned EARLY (see
        // ScanQueue::build) so apps are searchable within seconds.
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

        // Per-user excludes. ~/Library holds app-private data — Reminders,
        // AddressBook, other apps' sandbox containers. Descending into it
        // triggers macOS TCC privacy prompts ("would like to access your
        // reminders / contacts / data from other apps") and indexes nothing
        // a user ever picks in a folder dialog. ~/.Trash: deleted files
        // must not surface as search results.
        const QString home = QDir::homePath();
        l << home + QStringLiteral("/Library")
          << home + QStringLiteral("/.Trash")
          // freedesktop-style trash used by the `trash` CLI and cross-platform
          // tools — same rationale as ~/.Trash: deleted files must not surface
          // as search results (they showed up in real queries: 9k+ items).
          << home + QStringLiteral("/.local/share/Trash");
        return l;
    }();
    return kExcludes;
}

static bool isPathLevelExcluded(const QString &absolutePath)
{
    // Carve-out: system apps (Terminal, Activity Monitor, Preview, …) live in
    // /System/Applications, inside the otherwise-excluded /System. Users search
    // for them, and they are cheap opaque .app leaves — allow this subtree even
    // though /System is excluded.
    if (absolutePath == QLatin1String("/System/Applications")
        || absolutePath.startsWith(QLatin1String("/System/Applications/"))) {
        return false;
    }
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
                if (isPathUnderRoot(currentPath, completedRoot)) {
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

        if (qEnvironmentVariableIsSet("MSEARCH_TRACE_SCAN"))
            fprintf(stderr, "SCAN %s\n", currentPath.toUtf8().constData());

        // Huge-directory guard (the primary index bound). A dir with more than
        // maxDirChildren direct entries is a cache / backup / build / migration
        // blob — index its NAME (already added by its parent) but don't read or
        // descend its contents. readdir-counted with an early bail so we never
        // fully read a million-entry dir.
        {
            const int maxChildren = m_maxDirChildren.loadAcquire();
            if (maxChildren > 0
                && countChildrenCapped(currentPath, maxChildren) > maxChildren) {
                m_foldersExcluded.fetchAndAddRelaxed(1);
                continue;   // name kept, contents skipped
            }
        }

        // Standalone-app drift: always include hidden in the cache.
        // Eye-toggle is now purely presentational — filtered out by the
        // search worker / tree view. See docs/todos.md TODO 4.
        // NoSymLinks: never follow symlinks. Following the cloud-storage
        // symlinks in ~ (→ ~/Library/CloudStorage/OneDrive-*, iCloud) makes
        // macOS prompt for File Provider / privacy access and stalls the scan.
        // Real target dirs (~/VundS Dropbox, ~/Dropbox (Personal)) are local
        // and scanned directly, so nothing searchable is lost.
        QDir::Filters folderFilters = QDir::Dirs | QDir::NoDotAndDotDot
                                      | QDir::Readable | QDir::Hidden | QDir::NoSymLinks;
        const QFileInfoList folderEntries = dir.entryInfoList(folderFilters);
        const QSet<QString> pathExcluded = pathLevelExcludedChildren(currentPath);

        QStringList newNames;
        // Names cached as a selectable LEAF but NEVER descended into:
        //   - symlinks (following them walks linked trees twice / loops), and
        //   - name-pattern-excluded dirs (node_modules, build, target, .git,
        //     dist, …). The user's rule: index the FOLDER NAME so it's findable
        //     ("find the build folder"), but don't index its binary/build
        //     contents. Path-level excludes (/System, /usr, ~/Library) are a
        //     different beast — dropped entirely, not even the name.
        QSet<QString> noDescendNames;
        for (const QFileInfo &info : folderEntries) {
            if (m_stopRequested.loadAcquire()) {
                break;
            }
            const QString entry = info.fileName();
            if (pathExcluded.contains(entry)) {   // /System, /usr, ~/Library …
                m_foldersExcluded.fetchAndAddRelaxed(1);
                continue;                         // no name, no contents
            }
            const bool nameExcluded =
                m_excludeSettings && m_excludeSettings->shouldExclude(entry);
            if (info.isSymLink() || nameExcluded) noDescendNames.insert(entry);
            newNames.append(entry);               // name is indexed either way
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
        if (res.added > 0 && qEnvironmentVariableIsSet("MSEARCH_TRACE_SCAN"))
            fprintf(stderr, "ADD %d node=%d %s\n", res.added, currentNode,
                    currentPath.toUtf8().constData());
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
        // (.app, .photoslibrary, …) and symlinked directories stay cached
        // but are never descended — bundles read as a single thing, and
        // symlinks alias trees that are (or will be) walked at their real
        // location. Cap-blocked children were not cached, so they are not
        // descended either.
        {
            QMutexLocker locker(&m_queueMutex);
            bool queued = false;
            for (qsizetype i = 0; i < res.nodes.size(); ++i) {
                const qint32 node = res.nodes.at(i);
                if (node < 0 || isOpaqueBundle(newNames.at(i))
                    || noDescendNames.contains(newNames.at(i))) continue;
                m_scanQueue.enqueue({joinPath(currentPath, newNames.at(i)), node});
                queued = true;
            }
            if (queued) m_queueCondition.wakeAll();
        }
    }
}

void PathCacheManager::performScan()
{
    QString homeDir = QDir::homePath();

    // Open a fresh generation for this scan run — every ingestListing below
    // stamps it, and finishScan sweeps against it (docs/210).
    m_store->beginScanGeneration();

    // Initialize queue with home directory (scaffold node — the home dir
    // itself is not a cache entry, matching the original behavior).
    const qint32 homeNode = m_store->ensurePath(homeDir);
    {
        QMutexLocker locker(&m_queueMutex);
        m_scanQueue.clear();
        m_scanQueue.enqueue({homeDir, homeNode});
    }

    // The FSEvents stream is armed once, recursively, in finishScan() after
    // the root completes — no per-directory registration during the walk.

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

    finishScan(homeDir, progressThread);
}

void PathCacheManager::finishScan(const QString &completedRoot, QThread *progressThread)
{
    // Only mark as complete if we weren't stopped
    const bool wasStopped = m_stopRequested.loadAcquire() != 0;

    // Workers are done. Reconcile deletions BEFORE advertising the scan as
    // finished (docs/210): tombstone entries whose parent was re-listed this
    // generation but which vanished from disk, so no searcher (or test
    // polling isScanning()) ever observes the un-swept, stale state. On a
    // cold scan nothing is stale — a cheap no-op walk.
    if (!wasStopped) {
        const qint32 rootNode = m_store->find(completedRoot);
        if (rootNode >= 0) m_store->sweepStale(rootNode);
    }

    m_scanning.storeRelease(0);

    progressThread->wait();
    delete progressThread;

    // Only mark root as completed and emit signals if scan finished normally
    if (wasStopped) return;

    {
        QMutexLocker locker(&m_mutex);
        m_completedRoots.insert(completedRoot);
    }

    // Reconciliation for the first post-warm-start scan is done: drop the
    // "verifying…" flag so the UI shows plain "Ready".
    m_loadedFromSnapshot.storeRelease(0);

    // The BFS churns through millions of transient allocations (entryList
    // QStrings, queue nodes). Ask malloc to hand freed pages back to the OS
    // so RSS drops to the live cache size after the scan.
    malloc_zone_pressure_relief(nullptr, 0);

    // Persist the reconciled index for the next warm start.
    saveSnapshot();

    const int indexed = m_foldersIndexed.loadAcquire();
    const int excluded = m_foldersExcluded.loadAcquire();

    // Re-arm the recursive FSEvents stream on the main thread (it owns the
    // CF stream + dispatch queue) so live create/delete/rename under every
    // completed root now flows into onDirectoryChanged.
    const QStringList roots = watchRoots();
    QMetaObject::invokeMethod(this, [this, roots, indexed, excluded]() {
        m_fsWatcher->setRoots(roots);
        emit scanComplete(indexed, excluded);
        emit cacheUpdated();
    }, Qt::QueuedConnection);
}

QStringList PathCacheManager::watchRoots() const
{
    QMutexLocker locker(&m_mutex);
    // FsEventsWatcher::setRoots() reduces these to top-level ancestors.
    return QStringList(m_completedRoots.cbegin(), m_completedRoots.cend());
}

void PathCacheManager::onRescanNeeded(const QString &path)
{
    // FSEvents dropped events under `path`: one directory diff isn't enough,
    // re-walk the whole subtree (it lives under a completed root, so expandTo
    // would skip it).
    if (m_stopRequested.loadAcquire() || m_scanning.loadAcquire()) return;
    const QString clean = QDir::cleanPath(path);
    if (isPathLevelExcluded(clean)) return;
    if (m_store->find(clean) < 0) return;
    scheduleSubtreeIndex(clean);
}

void PathCacheManager::scheduleSubtreeIndex(const QString &dirPath)
{
    // Run the (potentially huge) recursive walk on a BACKGROUND thread — a
    // synchronous indexNewSubtree() on the main thread was THE hang: an
    // FSEvents MustScanSubDirs (or a large new directory) walked millions of
    // entries on the UI thread, freezing the app. The store + file cache are
    // internally locked, so the walk is safe off-thread. Dedupe identical
    // in-flight paths (FSEvents floods the same dir). m_subtreeIndexScheduled
    // is touched only on the main thread (scheduleSubtreeIndex is called from
    // main-thread slots and the completion lambda is a queued main-thread call).
    if (m_subtreeIndexScheduled.contains(dirPath)) return;
    m_subtreeIndexScheduled.insert(dirPath);
    QThreadPool::globalInstance()->start([this, dirPath]() {
        indexNewSubtree(dirPath);
        QMetaObject::invokeMethod(this, [this, dirPath]() {
            m_subtreeIndexScheduled.remove(dirPath);
            scheduleLiveCacheUpdate();
        }, Qt::QueuedConnection);
    });
}

void PathCacheManager::indexNewSubtree(const QString &dirPath)
{
    if (m_stopRequested.loadAcquire()) return;
    QDir dir(dirPath);
    if (!dir.exists() || !dir.isReadable()) return;

    // Huge-directory guard (same rule as the initial scan): don't read or
    // descend a cache/backup/build blob. The dir's name was already indexed
    // by the caller; here we just skip its contents.
    {
        const int maxChildren = m_maxDirChildren.loadAcquire();
        if (maxChildren > 0
            && countChildrenCapped(dirPath, maxChildren) > maxChildren) {
            return;
        }
    }

    QStringList toRecurse;
    const QFileInfoList subs = dir.entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable | QDir::Hidden
        | QDir::NoSymLinks);
    for (const QFileInfo &info : subs) {
        const QString name = info.fileName();
        const QString full = joinPath(dirPath, name);
        if (isPathLevelExcluded(full)) continue;   // /System … — drop entirely
        addPathToCache(full);                        // folder name is indexed
        // Symlinks, opaque bundles, and name-excluded dirs (node_modules,
        // build, …): index the name but never descend into the contents.
        const bool nameExcluded =
            m_excludeSettings && m_excludeSettings->shouldExclude(name);
        if (info.isSymLink() || isOpaqueBundle(name) || nameExcluded) continue;
        toRecurse.append(full);
    }

    const QStringList files = dir.entryList(
        QDir::Files | QDir::NoDotAndDotDot | QDir::Readable | QDir::Hidden);
    FileCacheManager *fc = FileCacheManager::instance();
    for (const QString &f : files) {
        if (m_excludeSettings && m_excludeSettings->shouldExcludeFile(f)) continue;
        fc->addFile(joinPath(dirPath, f));
    }

    for (const QString &c : toRecurse) indexNewSubtree(c);
}

// Snapshot warm-start (docs/210_persistent_index.md) --------------------------

static QString snapshotPath()
{
    return MaudeConfig::configDir() + QStringLiteral("/index-v1.bin");
}

QByteArray PathCacheManager::indexFingerprint() const
{
    QCryptographicHash h(QCryptographicHash::Sha256);
    const auto feed = [&h](const QByteArray &b) {
        h.addData(b);
        h.addData(QByteArrayLiteral("\x1f"));   // unit separator — no collisions
    };

    feed(QByteArrayLiteral("macos-search-index-v")
         + QByteArray::number(kIndexFormatVersion));

    // Enabled exclude patterns (folder + file), sorted for order-independence.
    if (m_excludeSettings) {
        QStringList folders = m_excludeSettings->enabledPatterns();
        folders.sort();
        for (const QString &p : folders) feed(p.toUtf8());
        feed(QByteArrayLiteral("|files|"));
        QStringList files = m_excludeSettings->enabledFilePatterns();
        files.sort();
        for (const QString &p : files) feed(p.toUtf8());
    }

    // Path-level excludes (fixed order, and includes $HOME-based entries).
    feed(QByteArrayLiteral("|paths|"));
    for (const QString &p : pathLevelExcludes()) feed(p.toUtf8());

    // Both caches' soft caps + hard ceilings.
    feed(QByteArrayLiteral("|caps|"));
    feed(QByteArray::number(m_softCap.loadAcquire()));
    feed(QByteArray::number(m_hardCeiling.loadAcquire()));
    FileCacheManager *fc = FileCacheManager::instance();
    feed(QByteArray::number(fc->softCap()));
    feed(QByteArray::number(fc->hardCeiling()));

    return h.result();
}

void PathCacheManager::saveSnapshot() const
{
    // mkpath so a first-ever run (or a bench with no ~/.macos-search) never
    // fails the write. QSaveFile inside saveTo() keeps the write atomic.
    QDir().mkpath(MaudeConfig::configDir());
    m_store->saveTo(snapshotPath(), indexFingerprint());
}

bool PathCacheManager::tryLoadSnapshot()
{
    PathCacheManager *self = instance();
    // Never load while a scan mutates the store: loadFrom() swaps the whole
    // node vector, and a running scan would keep stale indices and duplicate.
    // (The call site loads before any scan starts; this is belt + braces.)
    self->stopScan();
    if (!PathStore::shared()->loadFrom(snapshotPath(), self->indexFingerprint()))
        return false;

    // Self-heal contaminated snapshots. An earlier build (the '//'-path
    // exclude-bypass bug) indexed entries UNDER path-level-excluded roots
    // (~/Library/Containers, /System, /usr, …). A reconcile can NEVER remove
    // them: their excluded parent is never re-listed, and sweepStale only
    // condemns children of listed parents — so they persist and the counter
    // climbs on every restart (the "why does it count MORE?" bug). Drop every
    // excluded subtree here, on load. No-op for a clean snapshot (nodes absent).
    // Legit carve-outs (e.g. /System/Applications) are scan roots and get
    // re-added by the reconcile that follows, so purging their excluded parent
    // is safe.
    int purged = 0;
    for (const QString &ex : pathLevelExcludes()) {
        const qint32 node = self->m_store->find(ex);
        if (node >= 0) purged += self->m_store->markDeletedRecursive(node);
    }

    self->m_loadedFromSnapshot.storeRelease(1);
    emit self->cacheUpdated();
    return true;
}
