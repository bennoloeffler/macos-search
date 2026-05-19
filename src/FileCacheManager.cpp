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
    m_pathSet.insert(absolutePath);
    return true;
}

void FileCacheManager::removeFile(const QString &absolutePath)
{
    QWriteLocker locker(&m_lock);
    if (m_pathSet.contains(absolutePath)) {
        m_paths.removeAll(absolutePath);
        m_pathSet.remove(absolutePath);
    }
}

int FileCacheManager::removeFilesUnder(const QString &directoryPath)
{
    if (directoryPath.isEmpty()) return 0;
    const QString prefix = directoryPath.endsWith('/') ? directoryPath
                                                       : directoryPath + "/";

    QWriteLocker locker(&m_lock);
    QStringList toRemove;
    for (const QString &p : m_paths) {
        if (p.startsWith(prefix)) toRemove.append(p);
    }
    for (const QString &p : toRemove) {
        m_paths.removeAll(p);
        m_pathSet.remove(p);
    }
    return static_cast<int>(toRemove.size());
}

void FileCacheManager::clear()
{
    {
        QWriteLocker locker(&m_lock);
        m_paths.clear();
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
    for (const QString &path : m_paths) {
        if (!rootPath.isEmpty() && !path.startsWith(rootPath + "/") && path != rootPath) {
            continue;
        }

        const QString lowerPath = path.toLower();
        bool allMatch = true;
        for (const QString &term : terms) {
            if (!lowerPath.contains(term)) {
                allMatch = false;
                break;
            }
        }
        if (!allMatch) continue;

        results.append(path);
        if (results.size() >= maxResults) break;
    }
    return results;
}
