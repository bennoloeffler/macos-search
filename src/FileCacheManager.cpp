#include "FileCacheManager.h"

#include <QDir>
#include <QReadLocker>
#include <QWriteLocker>

FileCacheManager *FileCacheManager::s_instance = nullptr;

namespace { QString g_homeOverride; }

void FileCacheManager::setHomeOverrideForTests(const QString &path)
{
    g_homeOverride = path;
}

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

bool FileCacheManager::isUnderHome(const QString &absolutePath)
{
    if (absolutePath.isEmpty()) return false;
    const QString home = QDir::cleanPath(
        g_homeOverride.isEmpty() ? QDir::homePath() : g_homeOverride);
    const QString clean = QDir::cleanPath(absolutePath);
    if (clean == home) return true;
    // `/` is special — every absolute path is "under" it. Other roots use
    // a boundary check (`/Users/benno-extra/x` should NOT match home
    // `/Users/benno`).
    if (home == QLatin1String("/")) {
        return clean.startsWith(QLatin1Char('/'));
    }
    return clean.startsWith(home + QLatin1Char('/'));
}

bool FileCacheManager::addFile(const QString &absolutePath)
{
    if (absolutePath.isEmpty()) return false;

    // Scope guard: files are indexed only under $HOME. Folder search remains
    // available everywhere; this guard only narrows file indexing.
    if (!isUnderHome(absolutePath)) return false;

    if (m_ceilingReached.loadAcquire()) return false;

    QWriteLocker locker(&m_lock);

    if (m_pathSet.contains(absolutePath)) return false;

    const int currentSize = static_cast<int>(m_paths.size());
    const int hard = m_hardCeiling.loadAcquire();
    if (currentSize >= hard) {
        m_ceilingReached.storeRelease(1);
        locker.unlock();
        emit ceilingReachedSignal();
        return false;
    }
    const int soft = m_softCap.loadAcquire();
    if (currentSize >= soft) {
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

int FileCacheManager::bumpSoftCap()
{
    const int oldSoft = m_softCap.loadAcquire();
    const int hard = m_hardCeiling.loadAcquire();
    const int newSoft = qMin(oldSoft + kSoftCapIncrement, hard);
    if (newSoft > oldSoft) {
        m_softCap.storeRelease(newSoft);
        m_capReached.storeRelease(0);
        emit capRaised(newSoft);
    }
    return newSoft;
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
    m_ceilingReached.storeRelease(0);
}

int FileCacheManager::fileCount() const
{
    QReadLocker locker(&m_lock);
    return static_cast<int>(m_paths.size());
}

int FileCacheManager::softCap() const
{
    return m_softCap.loadAcquire();
}

int FileCacheManager::hardCeiling() const
{
    return m_hardCeiling.loadAcquire();
}

bool FileCacheManager::capReached() const
{
    return m_capReached.loadAcquire() != 0;
}

bool FileCacheManager::ceilingReached() const
{
    return m_ceilingReached.loadAcquire() != 0;
}

void FileCacheManager::setSoftCap(int newCap)
{
    if (newCap < 1) newCap = 1;
    const int hard = m_hardCeiling.loadAcquire();
    if (newCap > hard) newCap = hard;
    m_softCap.storeRelease(newCap);
    QReadLocker locker(&m_lock);
    if (m_paths.size() >= newCap) {
        m_capReached.storeRelease(1);
    } else {
        m_capReached.storeRelease(0);
    }
}

void FileCacheManager::setHardCeiling(int newCeiling)
{
    if (newCeiling < 1) newCeiling = 1;
    m_hardCeiling.storeRelease(newCeiling);
    if (m_softCap.loadAcquire() > newCeiling) {
        m_softCap.storeRelease(newCeiling);
    }
    QReadLocker locker(&m_lock);
    if (m_paths.size() >= newCeiling) {
        m_ceilingReached.storeRelease(1);
    } else {
        m_ceilingReached.storeRelease(0);
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
