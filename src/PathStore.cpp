#include "PathStore.h"

#include <QFile>
#include <QReadLocker>
#include <QSaveFile>
#include <QWriteLocker>
#include <QHash>
#include <QVarLengthArray>
#include <cstring>

namespace {

inline char asciiLower(char c)
{
    return (c >= 'A' && c <= 'Z') ? char(c + 32) : c;
}

inline bool isAscii(QByteArrayView bytes)
{
    for (char c : bytes) {
        if (quint8(c) >= 0x80) return false;
    }
    return true;
}

}  // anonymous

PathStore::PathStore()
{
    static_assert(sizeof(Node) == 20, "Node is 12 base + 8 child-link bytes");
    initRootLocked();
}

PathStore *PathStore::shared()
{
    static PathStore s;
    return &s;
}

void PathStore::initRootLocked()
{
    // Node 0 is the implicit "/" root: empty name, scaffold, no children.
    m_nodes.push_back(Node{-1, -1, -1, 0, 0, 0, 0});
}

// Append with a controlled 1.125× growth factor so bytesUsed() (which counts
// capacity — honest accounting) stays within the ≤ 36 B/entry G1 gate.
qint32 PathStore::appendNodeLocked(qint32 parent, QByteArrayView utf8Name,
                                   Kind kind, bool asEntry)
{
    const int len = qMin(int(utf8Name.size()), 255);
    if (m_nodes.size() == m_nodes.capacity()) {
        m_nodes.reserve(m_nodes.size() + m_nodes.size() / 8 + 64);
    }
    const qsizetype need = m_names.size() + len;
    if (need > m_names.capacity()) {
        m_names.reserve(need + need / 8 + 4096);
    }

    Node nd;
    nd.parent = parent;
    nd.firstChild = -1;
    // Prepend to the parent's child chain (order doesn't matter to callers).
    nd.nextSibling = (parent >= 0) ? m_nodes[size_t(parent)].firstChild : -1;
    nd.nameOff = quint32(m_names.size());
    nd.nameLen = quint8(len);
    nd.flags = (kind == File ? kKindFile : 0)
             | (asEntry ? kEntry : 0)
             | (isAscii(utf8Name.first(len)) ? 0 : kNonAscii);
    nd.pad = 0;
    m_names.append(utf8Name.data(), len);
    m_nodes.push_back(nd);
    const qint32 idx = qint32(m_nodes.size() - 1);
    if (parent >= 0) {
        m_nodes[size_t(parent)].firstChild = idx;
        m_nodes[size_t(parent)].flags |= kHasChild;
    }
    if (asEntry) ++m_counts[kind];
    return idx;
}

qint32 PathStore::childByNameLocked(qint32 parent, QByteArrayView utf8Name) const
{
    // Walk the parent's child chain — O(#children), not O(total nodes).
    for (qint32 i = m_nodes[size_t(parent)].firstChild; i >= 0;
         i = m_nodes[size_t(i)].nextSibling) {
        const Node &nd = m_nodes[size_t(i)];
        if (nd.nameLen != utf8Name.size()) continue;
        if (std::memcmp(m_names.constData() + nd.nameOff, utf8Name.data(),
                        size_t(nd.nameLen)) == 0) {
            return i;
        }
    }
    return -1;
}

qint32 PathStore::walkLocked(const QString &absPath, bool create)
{
    const QByteArray utf8 = absPath.toUtf8();
    qint32 node = 0;
    qsizetype pos = 0;
    while (pos < utf8.size()) {
        while (pos < utf8.size() && utf8.at(pos) == '/') ++pos;
        qsizetype end = pos;
        while (end < utf8.size() && utf8.at(end) != '/') ++end;
        if (end == pos) break;
        const QByteArrayView seg(utf8.constData() + pos, end - pos);
        qint32 child = childByNameLocked(node, seg);
        if (child < 0) {
            if (!create) return -1;
            child = appendNodeLocked(node, seg, Folder, false);  // scaffold
        }
        node = child;
        pos = end;
    }
    return node;
}

void PathStore::makeEntryLocked(qint32 node, Kind kind)
{
    Node &nd = m_nodes[size_t(node)];
    if (nd.flags & kEntry) {
        // Re-kind (rare: a path flips file<->folder). Keep counts honest.
        --m_counts[(nd.flags & kKindFile) ? File : Folder];
    }
    nd.flags = quint8((nd.flags & ~kKindFile) | kEntry
                      | (kind == File ? kKindFile : 0));
    ++m_counts[kind];
}

void PathStore::stampSeenLocked(qint32 node)
{
    Node &nd = m_nodes[size_t(node)];
    // Refresh to the current generation only if it carries a stale one — that
    // also drops any listed bit left over from an earlier generation, so a
    // node seen-but-not-relisted this run reads as "not listed this gen".
    if ((nd.pad & kGenMask) != m_generation) nd.pad = m_generation;
}

void PathStore::beginScanGeneration()
{
    QWriteLocker lock(&m_lock);
    // 15-bit space. On wrap, normalize every node back to generation 0 so a
    // stale value can never alias the fresh generation (docs/210 review).
    if (m_generation >= kGenMask) {
        for (Node &nd : m_nodes) nd.pad = 0;
        m_generation = 0;
    }
    ++m_generation;
}

int PathStore::sweepStale(qint32 root)
{
    QWriteLocker lock(&m_lock);
    const qint32 n = qint32(m_nodes.size());
    if (root < 0 || root >= n) return 0;

    // Forward pass over the subtree. A node is condemned iff an ancestor is
    // condemned, OR its parent was listed this generation but the node itself
    // was not seen in that listing. The root is never swept. This is the
    // load-bearing "parent-was-listed" rule: children of parents skipped this
    // run (e.g. an already-scanned Desktop under a later home scan) survive.
    std::vector<char> inSub(size_t(n - root), 0);
    std::vector<char> condemned(size_t(n - root), 0);
    inSub[0] = 1;   // root itself
    int removed = 0;
    for (qint32 i = root; i < n; ++i) {
        if (i != root) {
            const qint32 p = m_nodes[size_t(i)].parent;
            if (p < root || !inSub[size_t(p - root)]) continue;
            inSub[size_t(i - root)] = 1;
            const Node &pn = m_nodes[size_t(p)];
            const bool parentListed = (pn.pad & kListedBit)
                && (pn.pad & kGenMask) == m_generation;
            const bool seen = (m_nodes[size_t(i)].pad & kGenMask) == m_generation;
            if (condemned[size_t(p - root)] || (parentListed && !seen)) {
                condemned[size_t(i - root)] = 1;
            }
        }
        if (condemned[size_t(i - root)] && (m_nodes[size_t(i)].flags & kEntry)) {
            Node &nd = m_nodes[size_t(i)];
            nd.flags &= ~kEntry;
            --m_counts[(nd.flags & kKindFile) ? File : Folder];
            ++removed;
        }
    }
    return removed;
}

qint32 PathStore::addChild(qint32 parent, QByteArrayView utf8Name, Kind kind)
{
    QWriteLocker lock(&m_lock);
    if (parent < 0 || size_t(parent) >= m_nodes.size()) return -1;
    return appendNodeLocked(parent, utf8Name, kind, true);
}

PathStore::Ingest PathStore::ingestListing(qint32 dir, const QStringList &names,
                                           Kind kind, int cap)
{
    Ingest r;
    QWriteLocker lock(&m_lock);
    if (dir < 0 || size_t(dir) >= m_nodes.size()) return r;

    // Mark half of mark-and-sweep: this directory was listed this generation.
    m_nodes[size_t(dir)].pad = quint16(m_generation | kListedBit);

    // Existing children matter only when the dir already has some (relist /
    // overlap). Snapshot them once — never a per-entry store lookup.
    QHash<QByteArray, qint32> existing;
    if (m_nodes[size_t(dir)].flags & kHasChild) {
        const qint32 n = qint32(m_nodes.size());
        for (qint32 i = dir + 1; i < n; ++i) {
            const Node &nd = m_nodes[size_t(i)];
            if (nd.parent == dir) {
                existing.insert(QByteArray(m_names.constData() + nd.nameOff,
                                           nd.nameLen), i);
            }
        }
    }

    r.nodes.reserve(names.size());
    for (const QString &name : names) {
        const QByteArray utf8 = name.toUtf8();
        qint32 node = existing.isEmpty() ? -1 : existing.value(utf8, -1);
        if (node >= 0 && (m_nodes[size_t(node)].flags & kEntry)) {
            stampSeenLocked(node);                    // seen this generation
            r.nodes.append(node);                     // return it so the walk descends
            continue;
        }
        if (cap >= 0 && m_counts[kind] >= cap) {
            r.capHit = true;
            r.nodes.append(-1);
            continue;
        }
        if (node < 0) node = appendNodeLocked(dir, utf8, kind, true);
        else makeEntryLocked(node, kind);             // re-liven tombstone/scaffold
        stampSeenLocked(node);                        // seen this generation
        ++r.added;
        r.nodes.append(node);
    }
    return r;
}

qint32 PathStore::findOrCreatePath(const QString &absPath, Kind kind,
                                   int cap, Add *status)
{
    QWriteLocker lock(&m_lock);
    const qint32 node = walkLocked(absPath, true);
    Add st = Add::Existed;
    const Node &nd = m_nodes[size_t(node)];
    const bool live = (nd.flags & kEntry)
                      && ((nd.flags & kKindFile) != 0) == (kind == File);
    if (!live) {
        if (cap >= 0 && m_counts[kind] >= cap) {
            st = Add::CapBlocked;
        } else {
            makeEntryLocked(node, kind);
            st = Add::Added;
        }
    }
    if (status) *status = st;
    return node;
}

qint32 PathStore::ensurePath(const QString &absPath)
{
    QWriteLocker lock(&m_lock);
    return walkLocked(absPath, true);
}

qint32 PathStore::find(const QString &absPath) const
{
    QReadLocker lock(&m_lock);
    return const_cast<PathStore *>(this)->walkLocked(absPath, false);
}

bool PathStore::isEntry(qint32 node) const
{
    QReadLocker lock(&m_lock);
    return node >= 0 && size_t(node) < m_nodes.size()
           && (m_nodes[size_t(node)].flags & kEntry);
}

bool PathStore::isEntry(qint32 node, Kind kind) const
{
    QReadLocker lock(&m_lock);
    if (node < 0 || size_t(node) >= m_nodes.size()) return false;
    const Node &nd = m_nodes[size_t(node)];
    return (nd.flags & kEntry)
           && ((nd.flags & kKindFile) != 0) == (kind == File);
}

QString PathStore::pathOfLocked(qint32 node) const
{
    if (node < 0 || size_t(node) >= m_nodes.size()) return {};
    if (node == 0) return QStringLiteral("/");
    // Collect ancestor chain, then emit "/seg/seg/…" in one pass.
    QVarLengthArray<qint32, 32> chain;
    for (qint32 i = node; i > 0; i = m_nodes[size_t(i)].parent) chain.append(i);
    QByteArray out;
    for (qsizetype i = chain.size() - 1; i >= 0; --i) {
        const Node &nd = m_nodes[size_t(chain[i])];
        out.append('/');
        out.append(m_names.constData() + nd.nameOff, nd.nameLen);
    }
    return QString::fromUtf8(out);
}

QString PathStore::pathOf(qint32 node) const
{
    QReadLocker lock(&m_lock);
    return pathOfLocked(node);
}

QString PathStore::nameOf(qint32 node) const
{
    QReadLocker lock(&m_lock);
    if (node < 0 || size_t(node) >= m_nodes.size()) return {};
    const Node &nd = m_nodes[size_t(node)];
    return QString::fromUtf8(m_names.constData() + nd.nameOff, nd.nameLen);
}

QList<qint32> PathStore::childrenOf(qint32 node) const
{
    QReadLocker lock(&m_lock);
    QList<qint32> out;
    if (node < 0 || size_t(node) >= m_nodes.size()) return out;
    // Walk the child chain — O(#children), not O(total nodes). This is on the
    // main-thread FSEvents diff path, so the O(n) scan it replaced was the
    // beach-ball cause under file-system bursts.
    for (qint32 i = m_nodes[size_t(node)].firstChild; i >= 0;
         i = m_nodes[size_t(i)].nextSibling) {
        out.append(i);
    }
    return out;
}

int PathStore::markDeleted(qint32 node)
{
    QWriteLocker lock(&m_lock);
    if (node < 0 || size_t(node) >= m_nodes.size()) return 0;
    Node &nd = m_nodes[size_t(node)];
    if (!(nd.flags & kEntry)) return 0;
    nd.flags &= ~kEntry;
    --m_counts[(nd.flags & kKindFile) ? File : Folder];
    return 1;
}

int PathStore::markDeletedRecursive(qint32 node, quint8 kindMask,
                                    QStringList *removedFolders)
{
    QWriteLocker lock(&m_lock);
    const qint32 n = qint32(m_nodes.size());
    if (node < 0 || node >= n) return 0;
    // Single forward pass — every descendant has a higher index than its
    // parent, so subtree membership resolves in order.
    std::vector<bool> inSubtree(size_t(n - node), false);
    inSubtree[0] = true;
    int removed = 0;
    for (qint32 i = node; i < n; ++i) {
        if (i != node) {
            const qint32 p = m_nodes[size_t(i)].parent;
            if (p < node || !inSubtree[size_t(p - node)]) continue;
            inSubtree[size_t(i - node)] = true;
        }
        Node &nd = m_nodes[size_t(i)];
        if (!(nd.flags & kEntry)) continue;
        const Kind k = (nd.flags & kKindFile) ? File : Folder;
        if (!(kindMask & (1u << k))) continue;
        if (k == Folder && removedFolders) removedFolders->append(pathOfLocked(i));
        nd.flags &= ~kEntry;
        --m_counts[k];
        ++removed;
    }
    return removed;
}

void PathStore::clear()
{
    QWriteLocker lock(&m_lock);
    std::vector<Node>().swap(m_nodes);   // actually release the memory
    m_names = QByteArray();
    m_counts[Folder] = 0;
    m_counts[File] = 0;
    m_generation = 0;
    initRootLocked();
}

int PathStore::count(Kind k) const
{
    QReadLocker lock(&m_lock);
    return m_counts[k];
}

qint64 PathStore::bytesUsed() const
{
    QReadLocker lock(&m_lock);
    return qint64(m_nodes.capacity()) * qint64(sizeof(Node))
           + qint64(m_names.capacity());
}

QStringList PathStore::entries(Kind k) const
{
    QReadLocker lock(&m_lock);
    QStringList out;
    const quint8 want = (k == File ? kKindFile : 0);
    const qint32 n = qint32(m_nodes.size());
    for (qint32 i = 0; i < n; ++i) {
        const Node &nd = m_nodes[size_t(i)];
        if ((nd.flags & kEntry) && (nd.flags & kKindFile) == want) {
            out.append(pathOfLocked(i));
        }
    }
    return out;
}

// Snapshot layout: Header, then the node array in one write, then the name
// arena in one write. Fixed-width fingerprint keeps the header POD; callers
// pass an arbitrary QByteArray which is padded/truncated to kFingerprintLen
// (SHA-256 in production — see IndexFingerprint).
namespace {

// v2: Node grew from 12 to 20 bytes (firstChild/nextSibling). v1 snapshots
// are a different node size and are rejected → harmless cold scan.
constexpr quint32 kSnapshotVersion = 2;
constexpr int kFingerprintLen = 32;

struct SnapshotHeader {
    char magic[4];                       // 'M' 'S' 'I' 'X'
    quint32 version;
    char fingerprint[kFingerprintLen];
    qint64 nodeCount;
    qint64 namesSize;
};

void fillFingerprint(char *dst, const QByteArray &src)
{
    std::memset(dst, 0, kFingerprintLen);
    std::memcpy(dst, src.constData(),
                size_t(qMin(src.size(), qsizetype(kFingerprintLen))));
}

}  // anonymous

bool PathStore::saveTo(const QString &filePath, const QByteArray &fingerprint) const
{
    QReadLocker lock(&m_lock);
    const qint32 n = qint32(m_nodes.size());

    // Keep a node iff it is a live entry or an ancestor of one (scaffold).
    // Backward pass: children have higher indexes, so a kept child marks
    // its parent before the parent is visited.
    std::vector<char> keep(size_t(n), 0);
    keep[0] = 1;                                        // "/" root
    for (qint32 i = n - 1; i > 0; --i) {
        if (keep[size_t(i)] || (m_nodes[size_t(i)].flags & kEntry)) {
            keep[size_t(i)] = 1;
            keep[size_t(m_nodes[size_t(i)].parent)] = 1;
        }
    }

    // Forward pass: remap parent indexes and compact the name arena.
    std::vector<qint32> newIdx(size_t(n), -1);
    std::vector<Node> outNodes;
    QByteArray outNames;
    for (qint32 i = 0; i < n; ++i) {
        if (!keep[size_t(i)]) continue;
        Node nd = m_nodes[size_t(i)];
        newIdx[size_t(i)] = qint32(outNodes.size());
        const qint32 p = nd.parent;
        nd.parent = (p < 0) ? -1 : newIdx[size_t(p)];
        nd.flags &= quint8(~kHasChild);                 // recomputed below
        nd.pad = 0;                                     // generation is in-memory only
        const quint32 off = quint32(outNames.size());
        outNames.append(m_names.constData() + nd.nameOff, nd.nameLen);
        nd.nameOff = off;
        if (nd.parent >= 0) outNodes[size_t(nd.parent)].flags |= kHasChild;
        outNodes.push_back(nd);
    }

    SnapshotHeader hdr;
    std::memcpy(hdr.magic, "MSIX", 4);
    hdr.version = kSnapshotVersion;
    fillFingerprint(hdr.fingerprint, fingerprint);
    hdr.nodeCount = qint64(outNodes.size());
    hdr.namesSize = outNames.size();

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) return false;
    bool ok = file.write(reinterpret_cast<const char *>(&hdr), sizeof(hdr))
              == qint64(sizeof(hdr));
    ok = ok && file.write(reinterpret_cast<const char *>(outNodes.data()),
                          qint64(outNodes.size() * sizeof(Node)))
                   == qint64(outNodes.size() * sizeof(Node));
    ok = ok && file.write(outNames.constData(), outNames.size())
                   == outNames.size();
    return ok && file.commit();
}

bool PathStore::loadFrom(const QString &filePath, const QByteArray &expectedFingerprint)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return false;

    SnapshotHeader hdr;
    if (file.read(reinterpret_cast<char *>(&hdr), sizeof(hdr))
        != qint64(sizeof(hdr))) return false;
    if (std::memcmp(hdr.magic, "MSIX", 4) != 0) return false;
    if (hdr.version != kSnapshotVersion) return false;
    char expect[kFingerprintLen];
    fillFingerprint(expect, expectedFingerprint);
    if (std::memcmp(hdr.fingerprint, expect, kFingerprintLen) != 0) return false;
    if (hdr.nodeCount < 1 || hdr.namesSize < 0) return false;
    if (file.size() != qint64(sizeof(hdr)) + hdr.nodeCount * qint64(sizeof(Node))
                           + hdr.namesSize) return false;

    // Read into locals, validate fully, then swap in — a refused snapshot
    // never leaves the store half-loaded.
    std::vector<Node> nodes(size_t(hdr.nodeCount));
    if (file.read(reinterpret_cast<char *>(nodes.data()),
                  hdr.nodeCount * qint64(sizeof(Node)))
        != hdr.nodeCount * qint64(sizeof(Node))) return false;
    QByteArray names = file.read(hdr.namesSize);
    if (names.size() != hdr.namesSize) return false;

    if (nodes[0].parent != -1 || nodes[0].nameLen != 0) return false;
    int counts[2] = {0, 0};
    for (qint64 i = 0; i < hdr.nodeCount; ++i) {
        Node &nd = nodes[size_t(i)];
        if (i > 0 && (nd.parent < 0 || nd.parent >= i)) return false;
        if (qint64(nd.nameOff) + nd.nameLen > hdr.namesSize) return false;
        nd.pad = 0;                                     // normalize generations
        nd.firstChild = -1;                             // rebuilt below
        nd.nextSibling = -1;
        if (nd.flags & kEntry) ++counts[(nd.flags & kKindFile) ? File : Folder];
    }
    // Rebuild the child chains from parent pointers — the on-disk link fields
    // are stale (pre-remap indices) and never trusted. Descending index so
    // each parent's firstChild ends up pointing at its lowest-index child.
    for (qint64 i = hdr.nodeCount - 1; i > 0; --i) {
        const qint32 p = nodes[size_t(i)].parent;
        nodes[size_t(i)].nextSibling = nodes[size_t(p)].firstChild;
        nodes[size_t(p)].firstChild = qint32(i);
    }

    QWriteLocker lock(&m_lock);
    m_nodes.swap(nodes);
    m_names = names;
    m_counts[Folder] = counts[Folder];
    m_counts[File] = counts[File];
    return true;
}

QStringList PathStore::search(const QString &query, Kind kind,
                              const QString &rootPath, int maxResults) const
{
    if (query.isEmpty() || maxResults <= 0) return {};

    // '/' and whitespace both split terms (documented semantic change vs.
    // the old flat-string search — see docs/050_porting_rules.md).
    QString q = query.toLower();
    q.replace(QLatin1Char('/'), QLatin1Char(' '));
    QStringList terms = q.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (terms.isEmpty()) return {};
    if (terms.size() > 8) terms = terms.mid(0, 8);

    struct Term {
        QString str;
        QByteArray ascii;   // set iff the term is pure ASCII
    };
    QVarLengthArray<Term, 8> ts;
    for (const QString &t : terms) {
        Term term;
        term.str = t;
        const QByteArray utf8 = t.toUtf8();
        if (isAscii(utf8)) term.ascii = utf8;
        ts.append(term);
    }
    const quint8 all = quint8((1u << ts.size()) - 1);

    QReadLocker lock(&m_lock);
    const qint32 n = qint32(m_nodes.size());

    qint32 rootNode = 0;
    if (!rootPath.isEmpty()) {
        rootNode = const_cast<PathStore *>(this)->walkLocked(rootPath, false);
        if (rootNode < 0) return {};
    }

    // Two passes, chunked so the search can stop early once maxResults hit.
    // The arena is gapless (append-only, names concatenated in node order),
    // so each ~2 MB chunk covers a contiguous node range [i, j):
    //
    //   Pass A (per chunk): lowercase the chunk once, then one memchr/memcmp
    //   walk per ASCII term (NEON-fast), merging hit offsets back to nodes
    //   with a forward cursor; hits spanning a name boundary are rejected.
    //   Non-ASCII terms fold only the nodes flagged NonAscii — a pure-ASCII
    //   name can never contain such a term.
    //
    //   Pass B (same chunk): forward propagation — parent < child, and
    //   parents live in earlier (or this) chunk, so a node inherits every
    //   term its ancestors matched; the same trick scopes to the root.
    std::vector<quint8> mask(size_t(n), 0);
    std::vector<quint8> under;
    if (rootNode > 0) {
        under.assign(size_t(n), 0);
        under[size_t(rootNode)] = 1;
    }
    bool anyAsciiTerm = false, anyUnicodeTerm = false;
    for (const Term &t : ts) (t.ascii.isEmpty() ? anyUnicodeTerm : anyAsciiTerm) = true;

    const auto nameEnd = [this](qint32 i) {
        return size_t(m_nodes[size_t(i)].nameOff) + m_nodes[size_t(i)].nameLen;
    };
    const quint8 wantKind = (kind == File ? kKindFile : 0);
    constexpr size_t kChunk = size_t(2) << 20;
    std::vector<char> buf;
    QStringList out;

    qint32 i = 0;
    while (i < n) {
        const size_t chunkStart = m_nodes[size_t(i)].nameOff;
        qint32 j = i + 1;
        while (j < n && nameEnd(j) - chunkStart <= kChunk) ++j;
        const size_t chunkEnd = (j < n) ? m_nodes[size_t(j)].nameOff
                                        : size_t(m_names.size());
        const size_t len = chunkEnd - chunkStart;

        if (anyAsciiTerm && len > 0) {
            buf.resize(len);
            const char *src = m_names.constData() + chunkStart;
            for (size_t b = 0; b < len; ++b) buf[b] = asciiLower(src[b]);

            for (int t = 0; t < ts.size(); ++t) {
                if (ts[t].ascii.isEmpty()) continue;
                const char *needle = ts[t].ascii.constData();
                const size_t nlen = size_t(ts[t].ascii.size());
                if (nlen > len) continue;
                qint32 node = i;
                size_t pos = 0;
                while (pos + nlen <= len) {
                    const char *hit = static_cast<const char *>(
                        memchr(buf.data() + pos, needle[0], len - nlen - pos + 1));
                    if (!hit) break;
                    if (nlen > 1 && std::memcmp(hit + 1, needle + 1, nlen - 1) != 0) {
                        pos = size_t(hit - buf.data()) + 1;
                        continue;
                    }
                    const size_t off = chunkStart + size_t(hit - buf.data());
                    while (node < j && nameEnd(node) <= off) ++node;
                    if (off + nlen <= nameEnd(node)) {
                        mask[size_t(node)] |= quint8(1u << t);
                        pos = nameEnd(node) - chunkStart;   // done with this name
                    } else {
                        pos = (off - chunkStart) + 1;       // spans a boundary
                    }
                }
            }
        }
        if (anyUnicodeTerm) {
            for (qint32 idx = i; idx < j; ++idx) {
                const Node &nd = m_nodes[size_t(idx)];
                if (!(nd.flags & kNonAscii)) continue;
                const QString ls = QString::fromUtf8(
                    m_names.constData() + nd.nameOff, nd.nameLen).toLower();
                for (int t = 0; t < ts.size(); ++t) {
                    if (!ts[t].ascii.isEmpty()) continue;
                    if (ls.contains(ts[t].str)) mask[size_t(idx)] |= quint8(1u << t);
                }
            }
        }

        for (qint32 idx = i; idx < j; ++idx) {
            const Node &nd = m_nodes[size_t(idx)];
            if (nd.parent >= 0) {
                mask[size_t(idx)] |= mask[size_t(nd.parent)];
                if (rootNode > 0 && under[size_t(nd.parent)]) under[size_t(idx)] = 1;
            }
            if (mask[size_t(idx)] != all) continue;
            if (rootNode > 0 && !under[size_t(idx)]) continue;
            if (!(nd.flags & kEntry) || (nd.flags & kKindFile) != wantKind) continue;
            out.append(pathOfLocked(idx));
            if (out.size() >= maxResults) return out;
        }
        i = j;
    }
    return out;
}
