#include "FileSearchWorker.h"
#include "FileCacheManager.h"

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
    if (m_pendingQuery.isEmpty()) {
        emit resultsReady({});
        emit searchFinished(0);
        return;
    }

    emit searchStarted();
    m_searching = true;

    auto *cache = FileCacheManager::instance();
    const int upstreamCap = m_includeHidden ? 100 : 600;
    QStringList paths = cache->search(m_pendingQuery, m_pendingRootPath, upstreamCap);

    QList<SearchResult> results;
    for (const QString &path : paths) {
        if (!m_includeHidden && FolderSearchWorker::pathIsHidden(path)) {
            continue;
        }
        SearchResult result;
        result.path = path;
        result.displayName = QFileInfo(path).fileName();
        result.score = FolderSearchWorker::fuzzyScore(path, m_pendingQuery, m_pendingRootPath);
        results.append(result);
        if (results.size() >= 100) break;
    }

    std::sort(results.begin(), results.end(),
              [](const SearchResult &a, const SearchResult &b) {
                  if (a.score != b.score) return a.score > b.score;
                  return a.path.length() < b.path.length();
              });

    m_searching = false;
    emit resultsReady(results);
    emit searchFinished(static_cast<int>(results.size()));
}
