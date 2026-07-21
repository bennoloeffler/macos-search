#include "FolderSearchWorker.h"
#include "PathCacheManager.h"
#include <QtConcurrent>
#include <QTimer>
#include <QFileInfo>
#include <QRegularExpression>
#include <algorithm>

FolderSearchWorker::FolderSearchWorker(QObject *parent)
    : QObject(parent)
{
    m_debounceTimer = new QTimer(this);
    m_debounceTimer->setSingleShot(true);
    m_debounceTimer->setInterval(50);  // Short debounce - search is instant now
    connect(m_debounceTimer, &QTimer::timeout, this, &FolderSearchWorker::performSearch);
}

FolderSearchWorker::~FolderSearchWorker()
{
    cancel();
    // Wait for any in-flight background search to finish before we're
    // destroyed — its lambda captures `this`. Rare (workers usually outlive
    // the app); matters for stack-allocated workers in tests.
    if (m_future.isRunning()) m_future.waitForFinished();
}

void FolderSearchWorker::search(const QString &query, const QString &rootPath)
{
    m_pendingQuery = query;
    m_pendingRootPath = rootPath;
    m_debounceTimer->start();
}

void FolderSearchWorker::cancel()
{
    m_debounceTimer->stop();
    ++m_generation;  // drop any in-flight background search's results
    m_searching = false;
}

bool FolderSearchWorker::isSearching() const
{
    return m_searching;
}

void FolderSearchWorker::performSearch()
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

QList<SearchResult> FolderSearchWorker::computeResults(const QString &query,
                                                       const QString &rootPath,
                                                       bool includeHidden)
{
    // Search the in-memory cache (thread-safe: internal QReadWriteLock).
    PathCacheManager *cache = PathCacheManager::instance();
    // Ask the cache for a generous slice so the post-filter still has
    // 100 visible results after hidden paths are dropped. 600 is
    // empirically enough for ~600 hidden:1 visible ratios.
    const int upstreamCap = includeHidden ? 100 : 600;
    QStringList paths = cache->search(query, rootPath, upstreamCap);

    // Convert to SearchResult with scores. Filter out hidden paths
    // unless includeHidden is enabled (the cache always contains them
    // — eye toggle is presentational, see docs/todos.md TODO 4).
    QList<SearchResult> results;
    for (const QString &path : paths) {
        if (!includeHidden && pathIsHidden(path)) {
            continue;
        }
        SearchResult result;
        result.path = path;
        result.displayName = QFileInfo(path).fileName();
        result.score = fuzzyScore(path, query, rootPath);
        results.append(result);
        if (results.size() >= 100) break;
    }

    // Sort by score descending, then by path length
    std::sort(results.begin(), results.end(), [](const SearchResult &a, const SearchResult &b) {
        if (a.score != b.score) {
            return a.score > b.score;
        }
        return a.path.length() < b.path.length();
    });

    return results;
}

bool FolderSearchWorker::folderMatchesQuery(const QString &folderName, const QString &query)
{
    return folderName.toLower().contains(query.toLower());
}

void FolderSearchWorker::setIncludeHidden(bool include)
{
    m_includeHidden = include;
}

bool FolderSearchWorker::pathIsHidden(const QString &path)
{
    // Any segment of the path starting with '.' marks the entry as
    // hidden in the macOS sense (e.g. .git, .venv, .DS_Store).
    // The empty leading segment from an absolute "/" is skipped.
    const QStringList segments = path.split('/', Qt::SkipEmptyParts);
    for (const QString &seg : segments) {
        if (seg.startsWith('.')) return true;
    }
    return false;
}

int FolderSearchWorker::fuzzyScore(const QString &path, const QString &query, const QString &rootPath)
{
    if (query.isEmpty() || path.isEmpty()) {
        return 0;
    }

    // Use RELATIVE path from root for scoring (not full absolute path)
    QString relativePath = path;
    if (!rootPath.isEmpty() && path.startsWith(rootPath)) {
        relativePath = path.mid(rootPath.length());
        if (relativePath.startsWith('/')) {
            relativePath = relativePath.mid(1);
        }
    }

    // If relative path is empty (path == root), use basename
    if (relativePath.isEmpty()) {
        relativePath = QFileInfo(path).fileName();
    }

    QString lowerPath = relativePath.toLower();
    QString lowerQuery = query.toLower();

    // Extract query terms (split on spaces)
    QStringList terms = lowerQuery.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    if (terms.isEmpty()) {
        return 0;
    }

    // Calculate pattern size (total chars excluding spaces)
    int patternSize = 0;
    for (const QString &term : terms) {
        patternSize += term.length();
    }

    // Find all match positions for all terms (character-by-character fuzzy matching)
    QList<qsizetype> matchPositions;
    for (const QString &term : terms) {
        qsizetype pos = 0;
        for (QChar c : term) {
            qsizetype found = lowerPath.indexOf(c, pos);
            if (found == -1) {
                // Character doesn't match - score is 0
                return 0;
            }
            matchPositions.append(found);
            pos = found + 1;
        }
    }

    // Calculate match span: first to last matched char
    if (matchPositions.isEmpty()) {
        return 0;
    }

    std::sort(matchPositions.begin(), matchPositions.end());
    qsizetype firstMatch = matchPositions.first();
    qsizetype lastMatch = matchPositions.last();
    qsizetype lenOfHit = lastMatch - firstMatch + 1;

    // 1. Compactness = pattern_size / len_of_hit (how tightly packed the matches are)
    double compactness = static_cast<double>(patternSize) / lenOfHit;

    // 2. Completeness = pattern_size / relative_path_length (how much of relative path is covered)
    double completeness = static_cast<double>(patternSize) / lowerPath.length();

    // 3. Basename bonus = matched_chars_in_basename / basename_length, min 0.1
    QString basename = QFileInfo(path).fileName().toLower();
    int matchedCharsInBasename = 0;

    for (const QString &term : terms) {
        qsizetype pos = 0;
        for (QChar c : term) {
            qsizetype found = basename.indexOf(c, pos);
            if (found != -1) {
                matchedCharsInBasename++;
                pos = found + 1;
            }
        }
    }

    double basenameBonus = basename.isEmpty() ? 0.1 :
                          qMax(0.1, static_cast<double>(matchedCharsInBasename) / basename.length());

    // 4. Final score = compactness × completeness × basename_bonus × 100
    double score = compactness * completeness * basenameBonus * 100.0;

    return qBound(0, static_cast<int>(score), 100);
}
