#include "FileCacheManager.h"

#include <QReadLocker>
#include <QWriteLocker>

FileCacheManager *FileCacheManager::s_instance = nullptr;

FileCacheManager *FileCacheManager::instance()
{
    if (!s_instance) {
        s_instance = new FileCacheManager();
    }
    return s_instance;
}

FileCacheManager::FileCacheManager(QObject *parent)
    : QObject(parent)
{
}

bool FileCacheManager::addFile(const QString &absolutePath)
{
    if (absolutePath.isEmpty()) return false;

    // Fast cap check without locking.
    if (m_capReached.loadAcquire()) return false;

    QWriteLocker locker(&m_lock);

    if (m_pathSet.contains(absolutePath)) return false;

    // Cap check under the lock as well (race-free at the boundary).
    if (m_paths.size() >= m_capLimit.loadAcquire()) {
        if (!m_capReached.loadAcquire()) {
            m_capReached.storeRelease(1);
            locker.unlock();
            emit capReachedSignal();
        }
        return false;
    }

    m_paths.append(absolutePath);
    m_lowerPaths.append(absolutePath.toLower());
    m_pathSet.insert(absolutePath);
    return true;
}

void FileCacheManager::removeFile(const QString &absolutePath)
{
    QWriteLocker locker(&m_lock);
    if (m_pathSet.contains(absolutePath)) {
        const int idx = m_paths.indexOf(absolutePath);
        if (idx >= 0) {
            m_paths.removeAt(idx);
            m_lowerPaths.removeAt(idx);
        }
        m_pathSet.remove(absolutePath);
    }
}

int FileCacheManager::removeFilesUnder(const QString &directoryPath)
{
    if (directoryPath.isEmpty()) return 0;
    const QString prefix = directoryPath.endsWith('/') ? directoryPath
                                                       : directoryPath + "/";

    QWriteLocker locker(&m_lock);
    int removed = 0;
    for (int i = m_paths.size() - 1; i >= 0; --i) {
        if (m_paths.at(i).startsWith(prefix)) {
            m_pathSet.remove(m_paths.at(i));
            m_paths.removeAt(i);
            m_lowerPaths.removeAt(i);
            ++removed;
        }
    }
    return removed;
}

void FileCacheManager::clear()
{
    {
        QWriteLocker locker(&m_lock);
        m_paths.clear();
        m_lowerPaths.clear();
        m_pathSet.clear();
    }
    m_capReached.storeRelease(0);
}

int FileCacheManager::fileCount() const
{
    QReadLocker locker(&m_lock);
    return static_cast<int>(m_paths.size());
}

int FileCacheManager::capLimit() const
{
    return m_capLimit.loadAcquire();
}

bool FileCacheManager::capReached() const
{
    return m_capReached.loadAcquire() != 0;
}

void FileCacheManager::setCapLimit(int newCap)
{
    if (newCap < 1) newCap = 1;
    m_capLimit.storeRelease(newCap);
    // If we already exceeded the new cap, set the flag; otherwise clear it.
    QReadLocker locker(&m_lock);
    if (m_paths.size() >= newCap) {
        m_capReached.storeRelease(1);
    } else {
        m_capReached.storeRelease(0);
    }
}

bool FileCacheManager::contains(const QString &absolutePath) const
{
    QReadLocker locker(&m_lock);
    return m_pathSet.contains(absolutePath);
}

QStringList FileCacheManager::cachedFiles() const
{
    QReadLocker locker(&m_lock);
    return m_paths;
}

QStringList FileCacheManager::search(const QString &query,
                                     const QString &rootPath,
                                     int maxResults) const
{
    if (query.isEmpty()) return {};

    QStringList terms = query.toLower().split(' ', Qt::SkipEmptyParts);
    if (terms.isEmpty()) return {};

    QStringList results;
    QReadLocker locker(&m_lock);
    const int n = static_cast<int>(m_paths.size());
    const QString rootPrefix = rootPath.isEmpty() ? QString() : rootPath + "/";

    for (int i = 0; i < n; ++i) {
        const QString &lowerPath = m_lowerPaths.at(i);
        // Root-scope check uses the original-cased path for correctness on
        // case-sensitive filesystems; the comparison is unaffected by case.
        if (!rootPath.isEmpty()) {
            const QString &path = m_paths.at(i);
            if (!path.startsWith(rootPrefix) && path != rootPath) continue;
        }
        bool allMatch = true;
        for (const QString &term : terms) {
            if (!lowerPath.contains(term)) {
                allMatch = false;
                break;
            }
        }
        if (!allMatch) continue;
        results.append(m_paths.at(i));
        if (results.size() >= maxResults) break;
    }
    return results;
}
