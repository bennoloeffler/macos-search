#include "FileCacheManager.h"
#include "PathStore.h"

#include <QDir>

FileCacheManager *FileCacheManager::s_instance = nullptr;

namespace { QString g_homeOverride; }

// File entries share a kind bitmask helper with removeFilesUnder/clear.
static constexpr quint8 kFileOnly = 1u << PathStore::File;

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
    m_store = PathStore::shared();
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

// Shared cap-hit bookkeeping: latch the right flag, emit the right signal
// (outside the store lock — the store locks internally).
void FileCacheManager::noteCapHit()
{
    if (m_store->count(PathStore::File) >= m_hardCeiling.loadAcquire()) {
        if (!m_ceilingReached.loadAcquire()) {
            m_ceilingReached.storeRelease(1);
            emit ceilingReachedSignal();
        }
    } else if (!m_capReached.loadAcquire()) {
        m_capReached.storeRelease(1);
        emit capReachedSignal();
    }
}

bool FileCacheManager::addFile(const QString &absolutePath)
{
    if (absolutePath.isEmpty()) return false;

    // Scope guard: files are indexed only under $HOME. Folder search remains
    // available everywhere; this guard only narrows file indexing.
    if (!isUnderHome(absolutePath)) return false;

    if (m_ceilingReached.loadAcquire()) return false;

    const int cap = qMin(m_softCap.loadAcquire(), m_hardCeiling.loadAcquire());
    PathStore::Add status = PathStore::Add::Existed;
    m_store->findOrCreatePath(absolutePath, PathStore::File, cap, &status);
    if (status == PathStore::Add::CapBlocked) {
        noteCapHit();
        return false;
    }
    return status == PathStore::Add::Added;
}

void FileCacheManager::ingestScan(qint32 dirNode, const QString &dirPath,
                                  const QStringList &names)
{
    if (dirNode < 0 || names.isEmpty()) return;
    if (!isUnderHome(dirPath)) return;
    if (m_ceilingReached.loadAcquire()) return;

    const int cap = qMin(m_softCap.loadAcquire(), m_hardCeiling.loadAcquire());
    const PathStore::Ingest res =
        m_store->ingestListing(dirNode, names, PathStore::File, cap);
    if (res.capHit) noteCapHit();
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
    const qint32 node = m_store->find(absolutePath);
    if (m_store->isEntry(node, PathStore::File)) {
        m_store->markDeleted(node);
    }
}

int FileCacheManager::removeFilesUnder(const QString &directoryPath)
{
    if (directoryPath.isEmpty()) return 0;
    const qint32 node = m_store->find(QDir::cleanPath(directoryPath));
    if (node < 0) return 0;
    return m_store->markDeletedRecursive(node, kFileOnly);
}

void FileCacheManager::clear()
{
    // Tombstone every file entry; folder entries in the shared store stay.
    m_store->markDeletedRecursive(m_store->find(QStringLiteral("/")), kFileOnly);
    m_capReached.storeRelease(0);
    m_ceilingReached.storeRelease(0);
}

int FileCacheManager::fileCount() const
{
    return m_store->count(PathStore::File);
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
    m_capReached.storeRelease(fileCount() >= newCap ? 1 : 0);
}

void FileCacheManager::setHardCeiling(int newCeiling)
{
    if (newCeiling < 1) newCeiling = 1;
    m_hardCeiling.storeRelease(newCeiling);
    if (m_softCap.loadAcquire() > newCeiling) {
        m_softCap.storeRelease(newCeiling);
    }
    m_ceilingReached.storeRelease(fileCount() >= newCeiling ? 1 : 0);
}

bool FileCacheManager::contains(const QString &absolutePath) const
{
    return m_store->isEntry(m_store->find(absolutePath), PathStore::File);
}

QStringList FileCacheManager::cachedFiles() const
{
    return m_store->entries(PathStore::File);
}

QStringList FileCacheManager::search(const QString &query,
                                     const QString &rootPath,
                                     int maxResults) const
{
    return m_store->search(query, PathStore::File, rootPath, maxResults);
}
