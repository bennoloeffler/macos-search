#include "PathStore.h"

// STUB — TDD step: the real implementation lands after PathStoreTest is
// committed red. Every method is inert.

PathStore::PathStore() {}

PathStore *PathStore::shared()
{
    static PathStore s;
    return &s;
}

qint32 PathStore::addChild(qint32, QByteArrayView, Kind) { return -1; }

PathStore::Ingest PathStore::ingestListing(qint32, const QStringList &, Kind, int)
{
    return {};
}

qint32 PathStore::findOrCreatePath(const QString &, Kind, int, Add *) { return -1; }
qint32 PathStore::ensurePath(const QString &) { return -1; }
qint32 PathStore::find(const QString &) const { return -1; }
bool PathStore::isEntry(qint32) const { return false; }
bool PathStore::isEntry(qint32, Kind) const { return false; }
QString PathStore::pathOf(qint32) const { return {}; }
QString PathStore::nameOf(qint32) const { return {}; }
QList<qint32> PathStore::childrenOf(qint32) const { return {}; }
int PathStore::markDeleted(qint32) { return 0; }
int PathStore::markDeletedRecursive(qint32, quint8, QStringList *) { return 0; }
void PathStore::clear() {}
int PathStore::count(Kind) const { return 0; }
qint64 PathStore::bytesUsed() const { return 0; }
QStringList PathStore::entries(Kind) const { return {}; }
QStringList PathStore::search(const QString &, Kind, const QString &, int) const
{
    return {};
}

qint32 PathStore::appendNodeLocked(qint32, QByteArrayView, Kind, bool) { return -1; }
qint32 PathStore::childByNameLocked(qint32, QByteArrayView) const { return -1; }
qint32 PathStore::walkLocked(const QString &, bool) { return -1; }
QString PathStore::pathOfLocked(qint32) const { return {}; }
void PathStore::makeEntryLocked(qint32, Kind) {}
void PathStore::initRootLocked() {}
