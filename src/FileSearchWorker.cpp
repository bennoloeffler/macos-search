#include "FileSearchWorker.h"
#include "FileCacheManager.h"

#include <QtConcurrent>
#include <QFileInfo>
#include <QTimer>
#include <algorithm>

FileSearchWorker::FileSearchWorker(QObject *parent)
    : QObject(parent)
{
    m_debounceTimer = new QTimer(this);
    m_debounceTimer->setSingleShot(true);
    m_debounceTimer->setInterval(50);
    connect(m_debounceTimer, &QTimer::timeout, this, &FileSearchWorker::performSearch);
}

FileSearchWorker::~FileSearchWorker()
{
    cancel();
    // Wait for any in-flight background search to finish before we're
    // destroyed — its lambda captures `this`. Rare (workers usually outlive
    // the app); matters for stack-allocated workers in tests.
    if (m_future.isRunning()) m_future.waitForFinished();
}

void FileSearchWorker::search(const QString &query, const QString &rootPath)
{
    m_pendingQuery = query;
    m_pendingRootPath = rootPath;
    m_debounceTimer->start();
}

void FileSearchWorker::cancel()
{
    m_debounceTimer->stop();
    ++m_generation;  // drop any in-flight background search's results
    m_searching = false;
}

bool FileSearchWorker::isSearching() const
{
    return m_searching;
}

void FileSearchWorker::setIncludeHidden(bool include)
{
    m_includeHidden = include;
}

void FileSearchWorker::performSearch()
{
    const quint64 gen = ++m_generation;

    if (m_pendingQuery.isEmpty()) {
        emit resultsReady({});
        emit searchFinished(0);
        return;
    }

    emit searchStarted();
    m_searching = true;

    // Run the O(n) cache scan off the GUI thread so typing never blocks.
    // Inputs are copied; results are marshalled back to this thread, where a
    // generation check drops anything the user has already typed past.
    const QString query = m_pendingQuery;
    const QString rootPath = m_pendingRootPath;
    const bool includeHidden = m_includeHidden;
    m_future = QtConcurrent::run([this, gen, query, rootPath, includeHidden]() {
        QList<SearchResult> results = computeResults(query, rootPath, includeHidden);
        QMetaObject::invokeMethod(this, [this, gen, results]() {
            if (gen != m_generation) return;  // superseded — a newer search won
            m_searching = false;
            emit resultsReady(results);
            emit searchFinished(static_cast<int>(results.size()));
        }, Qt::QueuedConnection);
    });
}

QList<SearchResult> FileSearchWorker::computeResults(const QString &query,
                                                     const QString &rootPath,
                                                     bool includeHidden)
{
    // FileCacheManager::search is thread-safe (internal QReadWriteLock).
    auto *cache = FileCacheManager::instance();
    const int upstreamCap = includeHidden ? 100 : 600;
    QStringList paths = cache->search(query, rootPath, upstreamCap);

    QList<SearchResult> results;
    for (const QString &path : paths) {
        if (!includeHidden && FolderSearchWorker::pathIsHidden(path)) {
            continue;
        }
        SearchResult result;
        result.path = path;
        result.displayName = QFileInfo(path).fileName();
        result.score = FolderSearchWorker::fuzzyScore(path, query, rootPath);
        results.append(result);
        if (results.size() >= 100) break;
    }

    std::sort(results.begin(), results.end(),
              [](const SearchResult &a, const SearchResult &b) {
                  if (a.score != b.score) return a.score > b.score;
                  return a.path.length() < b.path.length();
              });

    return results;
}
