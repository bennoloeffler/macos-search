#include "PathStoreTest.h"
#include "PathStore.h"

#include <QRandomGenerator>
#include <QtTest/QtTest>

// ---------------------------------------------------------------------------
// Structure
// ---------------------------------------------------------------------------

void PathStoreTest::appendChildrenKeepsParentBeforeChild()
{
    PathStore s;
    // Node 0 is the implicit "/" root.
    QCOMPARE(s.find("/"), 0);

    const qint32 a = s.addChild(0, "a", PathStore::Folder);
    const qint32 b = s.addChild(a, "b", PathStore::Folder);
    const qint32 f = s.addChild(b, "notes.txt", PathStore::File);

    // Invariant: parent index < child index (append-only).
    QVERIFY(0 < a && a < b && b < f);
    QCOMPARE(s.count(PathStore::Folder), 2);
    QCOMPARE(s.count(PathStore::File), 1);
}

void PathStoreTest::pathOfMaterializesFullPath()
{
    PathStore s;
    const qint32 a = s.addChild(0, "a", PathStore::Folder);
    const qint32 b = s.addChild(a, "b", PathStore::Folder);
    const qint32 f = s.addChild(b, "notes.txt", PathStore::File);

    QCOMPARE(s.pathOf(0), QStringLiteral("/"));
    QCOMPARE(s.pathOf(a), QStringLiteral("/a"));
    QCOMPARE(s.pathOf(f), QStringLiteral("/a/b/notes.txt"));
}

void PathStoreTest::findLocatesExistingAndMissesAbsent()
{
    PathStore s;
    const qint32 a = s.addChild(0, "alpha", PathStore::Folder);
    const qint32 b = s.addChild(a, "beta", PathStore::Folder);

    QCOMPARE(s.find("/alpha"), a);
    QCOMPARE(s.find("/alpha/beta"), b);
    QCOMPARE(s.find("/alpha/beta/"), b);      // trailing slash tolerated
    QCOMPARE(s.find("/alpha/gamma"), -1);
    QCOMPARE(s.find("/zzz"), -1);
}

void PathStoreTest::findOrCreatePathCreatesIntermediateSegments()
{
    PathStore s;
    PathStore::Add st = PathStore::Add::Existed;
    const qint32 leaf =
        s.findOrCreatePath("/deep/nested/leaf", PathStore::Folder, -1, &st);

    QVERIFY(leaf > 0);
    QCOMPARE(st, PathStore::Add::Added);
    // Intermediate segments exist as nodes but are NOT counted entries.
    QCOMPARE(s.count(PathStore::Folder), 1);
    const qint32 mid = s.find("/deep/nested");
    QVERIFY(mid >= 0);
    QVERIFY(!s.isEntry(mid));
    QVERIFY(s.isEntry(leaf, PathStore::Folder));
}

void PathStoreTest::findOrCreatePathDeduplicates()
{
    PathStore s;
    const qint32 first = s.findOrCreatePath("/x/y", PathStore::File);
    PathStore::Add st = PathStore::Add::Added;
    const qint32 second = s.findOrCreatePath("/x/y", PathStore::File, -1, &st);

    QCOMPARE(second, first);
    QCOMPARE(st, PathStore::Add::Existed);
    QCOMPARE(s.count(PathStore::File), 1);
}

void PathStoreTest::childrenOfListsDirectChildren()
{
    PathStore s;
    const qint32 a = s.addChild(0, "a", PathStore::Folder);
    const qint32 b = s.addChild(a, "b", PathStore::Folder);
    const qint32 c = s.addChild(a, "c.txt", PathStore::File);
    const qint32 deep = s.addChild(b, "deep", PathStore::Folder);

    const QList<qint32> kids = s.childrenOf(a);
    QCOMPARE(kids.size(), 2);
    QVERIFY(kids.contains(b));
    QVERIFY(kids.contains(c));
    QVERIFY(!kids.contains(deep));            // grandchild, not direct child
    QCOMPARE(s.childrenOf(0), QList<qint32>{a});
}

// ---------------------------------------------------------------------------
// Search semantics
// ---------------------------------------------------------------------------

void PathStoreTest::searchMatchesViaAncestorSegment()
{
    PathStore s;
    s.findOrCreatePath("/a/projects/x/README.md", PathStore::File);
    s.findOrCreatePath("/a/other/y/README.md", PathStore::File);

    // "projects" only appears as an (intermediate) ancestor segment name.
    const QStringList hits = s.search("projects", PathStore::File, QString(), 100);
    QCOMPARE(hits, QStringList{QStringLiteral("/a/projects/x/README.md")});
}

void PathStoreTest::searchMultiTermAndAcrossAncestors()
{
    PathStore s;
    s.findOrCreatePath("/a/projects/x/README.md", PathStore::File);
    s.findOrCreatePath("/a/projects/x/license.txt", PathStore::File);
    s.findOrCreatePath("/a/notes/README.md", PathStore::File);

    // AND across different ancestors: "projects" in one segment, "readme"
    // in another.
    const QStringList hits =
        s.search("projects readme", PathStore::File, QString(), 100);
    QCOMPARE(hits, QStringList{QStringLiteral("/a/projects/x/README.md")});
}

void PathStoreTest::searchSlashSplitsTerms()
{
    PathStore s;
    s.findOrCreatePath("/a/projects/x/README.md", PathStore::File);
    s.findOrCreatePath("/a/notes/README.md", PathStore::File);

    // '/' acts as a term separator — "projects/readme" ≡ "projects readme".
    const QStringList hits =
        s.search("projects/readme", PathStore::File, QString(), 100);
    QCOMPARE(hits, QStringList{QStringLiteral("/a/projects/x/README.md")});
}

void PathStoreTest::searchTermStaysInsideSegment()
{
    PathStore s;
    s.findOrCreatePath("/projects/xdir/leaf.txt", PathStore::File);

    // "tsx" spans the projects|xdir boundary — a single term never
    // matches across segments.
    QVERIFY(s.search("tsx", PathStore::File, QString(), 100).isEmpty());
    // But two terms may match in different segments.
    QCOMPARE(s.search("ects xdir", PathStore::File, QString(), 100).size(), 1);
}

void PathStoreTest::searchCaseInsensitiveNonAscii()
{
    PathStore s;
    s.findOrCreatePath(QString::fromUtf8("/home/Büro-Unterlagen/Café-Liste.pdf"),
                       PathStore::File);
    s.findOrCreatePath("/home/plain/list.pdf", PathStore::File);

    const QString expect = QString::fromUtf8("/home/Büro-Unterlagen/Café-Liste.pdf");
    QCOMPARE(s.search(QString::fromUtf8("büro"), PathStore::File, QString(), 100),
             QStringList{expect});
    QCOMPARE(s.search(QString::fromUtf8("BÜRO"), PathStore::File, QString(), 100),
             QStringList{expect});
    QCOMPARE(s.search(QString::fromUtf8("café"), PathStore::File, QString(), 100),
             QStringList{expect});
    // Plain-ASCII terms still hit non-ASCII names.
    QCOMPARE(s.search("liste", PathStore::File, QString(), 100),
             QStringList{expect});
}

void PathStoreTest::searchScopesToRootPath()
{
    PathStore s;
    s.findOrCreatePath("/scope/report.txt", PathStore::File);
    s.findOrCreatePath("/other/report.txt", PathStore::File);

    const QStringList hits =
        s.search("report", PathStore::File, QStringLiteral("/scope"), 100);
    QCOMPARE(hits, QStringList{QStringLiteral("/scope/report.txt")});
    // The root itself is a candidate when it is an entry.
    s.findOrCreatePath("/scope", PathStore::Folder);
    const QStringList self =
        s.search("scope", PathStore::Folder, QStringLiteral("/scope"), 100);
    QCOMPARE(self, QStringList{QStringLiteral("/scope")});
}

void PathStoreTest::searchRootPathBoundaryRespected()
{
    PathStore s;
    s.findOrCreatePath("/scope/report.txt", PathStore::File);
    s.findOrCreatePath("/scope-extra/report.txt", PathStore::File);

    // "/scope" must not include the sibling "/scope-extra".
    const QStringList hits =
        s.search("report", PathStore::File, QStringLiteral("/scope"), 100);
    QCOMPARE(hits, QStringList{QStringLiteral("/scope/report.txt")});
}

void PathStoreTest::searchFiltersByKind()
{
    PathStore s;
    s.findOrCreatePath("/data/reports", PathStore::Folder);
    s.findOrCreatePath("/data/reports.txt", PathStore::File);

    QCOMPARE(s.search("reports", PathStore::Folder, QString(), 100),
             QStringList{QStringLiteral("/data/reports")});
    QCOMPARE(s.search("reports", PathStore::File, QString(), 100),
             QStringList{QStringLiteral("/data/reports.txt")});
}

void PathStoreTest::searchHonorsMaxResults()
{
    PathStore s;
    const qint32 d = s.findOrCreatePath("/many", PathStore::Folder);
    for (int i = 0; i < 20; ++i) {
        s.addChild(d, QString("match-%1.txt").arg(i).toUtf8(), PathStore::File);
    }
    QCOMPARE(s.search("match", PathStore::File, QString(), 5).size(), 5);
}

// ---------------------------------------------------------------------------
// Tombstones
// ---------------------------------------------------------------------------

void PathStoreTest::tombstoneRemovesFromSearchAndCounts()
{
    PathStore s;
    s.findOrCreatePath("/gone/one.txt", PathStore::File);
    s.findOrCreatePath("/gone/sub/two.txt", PathStore::File);
    const qint32 keep = s.findOrCreatePath("/keep/three.txt", PathStore::File);
    s.findOrCreatePath("/gone", PathStore::Folder);
    QCOMPARE(s.count(PathStore::File), 3);

    const int removed = s.markDeletedRecursive(s.find("/gone"));
    QCOMPARE(removed, 3);                     // the folder entry + 2 files
    QCOMPARE(s.count(PathStore::File), 1);
    QCOMPARE(s.count(PathStore::Folder), 0);
    QVERIFY(s.search("gone", PathStore::File, QString(), 100).isEmpty());

    // Indexes stay stable: the survivor keeps its node id and path.
    QCOMPARE(s.find("/keep/three.txt"), keep);
    QCOMPARE(s.pathOf(keep), QStringLiteral("/keep/three.txt"));
}

void PathStoreTest::tombstoneKindMaskLeavesOtherKind()
{
    PathStore s;
    s.findOrCreatePath("/mix", PathStore::Folder);
    s.findOrCreatePath("/mix/sub", PathStore::Folder);
    s.findOrCreatePath("/mix/a.txt", PathStore::File);
    s.findOrCreatePath("/mix/sub/b.txt", PathStore::File);

    // File-only recursive delete (removeFilesUnder semantics).
    const int removed = s.markDeletedRecursive(s.find("/mix"), 1u << PathStore::File);
    QCOMPARE(removed, 2);
    QCOMPARE(s.count(PathStore::File), 0);
    QCOMPARE(s.count(PathStore::Folder), 2);  // /mix and /mix/sub survive
}

void PathStoreTest::relivenTombstonedEntry()
{
    PathStore s;
    const qint32 n = s.findOrCreatePath("/back/again.txt", PathStore::File);
    QCOMPARE(s.markDeleted(n), 1);
    QCOMPARE(s.count(PathStore::File), 0);

    PathStore::Add st = PathStore::Add::Existed;
    const qint32 again = s.findOrCreatePath("/back/again.txt", PathStore::File, -1, &st);
    QCOMPARE(again, n);                       // same node, no duplicate
    QCOMPARE(st, PathStore::Add::Added);
    QCOMPARE(s.count(PathStore::File), 1);
}

// ---------------------------------------------------------------------------
// Scan ingest + caps
// ---------------------------------------------------------------------------

void PathStoreTest::ingestListingDedupesRelisting()
{
    PathStore s;
    const qint32 d = s.findOrCreatePath("/dir", PathStore::Folder);

    const auto r1 = s.ingestListing(d, {"one", "two"}, PathStore::Folder, -1);
    QCOMPARE(r1.added, 2);
    QVERIFY(r1.nodes.at(0) >= 0 && r1.nodes.at(1) >= 0);
    const qint32 one = s.find("/dir/one");

    // Re-listing: existing live entries are skipped, new ones added.
    const auto r2 = s.ingestListing(d, {"one", "two", "three"}, PathStore::Folder, -1);
    QCOMPARE(r2.added, 1);
    QCOMPARE(r2.nodes.at(0), -1);
    QCOMPARE(r2.nodes.at(1), -1);
    QVERIFY(r2.nodes.at(2) >= 0);
    QCOMPARE(s.count(PathStore::Folder), 4);  // /dir + three children

    // Tombstoned entries come back alive on re-listing — same node.
    s.markDeleted(one);
    const auto r3 = s.ingestListing(d, {"one"}, PathStore::Folder, -1);
    QCOMPARE(r3.added, 1);
    QCOMPARE(r3.nodes.at(0), one);
    QCOMPARE(s.count(PathStore::Folder), 4);
}

void PathStoreTest::capBlocksFindOrCreatePath()
{
    PathStore s;
    PathStore::Add st = PathStore::Add::Existed;
    s.findOrCreatePath("/a", PathStore::Folder, 1, &st);
    QCOMPARE(st, PathStore::Add::Added);
    s.findOrCreatePath("/b", PathStore::Folder, 1, &st);
    QCOMPARE(st, PathStore::Add::CapBlocked);
    QCOMPARE(s.count(PathStore::Folder), 1);
}

void PathStoreTest::capBlocksIngestListing()
{
    PathStore s;
    const qint32 d = s.findOrCreatePath("/dir", PathStore::Folder, -1);
    const auto r = s.ingestListing(d, {"a", "b", "c", "d"}, PathStore::Folder, 3);
    QCOMPARE(r.added, 2);                     // /dir already occupies 1 of 3
    QVERIFY(r.capHit);
    QCOMPARE(s.count(PathStore::Folder), 3);
}

void PathStoreTest::clearResetsStore()
{
    PathStore s;
    s.findOrCreatePath("/a/b/c.txt", PathStore::File);
    s.findOrCreatePath("/a/b", PathStore::Folder);
    s.clear();

    QCOMPARE(s.count(PathStore::Folder), 0);
    QCOMPARE(s.count(PathStore::File), 0);
    QCOMPARE(s.find("/a"), -1);
    QCOMPARE(s.find("/"), 0);                 // root survives
    // Store is usable again after clear.
    QVERIFY(s.addChild(0, "fresh", PathStore::Folder) > 0);
}

// ---------------------------------------------------------------------------
// G1 memory gate
// ---------------------------------------------------------------------------

void PathStoreTest::memoryGateG1()
{
    PathStore s;
    QRandomGenerator rng(20260721);
    const QString extras = QString::fromUtf8("äöüßé");
    auto randomName = [&rng, &extras]() {
        const int len = 10 + rng.bounded(16);  // 10–25 chars, some non-ASCII
        QString n;
        n.reserve(len);
        for (int c = 0; c < len; ++c) {
            if (rng.bounded(100) < 3) n += extras.at(rng.bounded(extras.size()));
            else n += QChar('a' + char(rng.bounded(26)));
        }
        return n.toUtf8();
    };

    // 100 top-level dirs × 40 subdirs, files round-robin → 100 000 entries.
    QList<qint32> dirs;
    for (int i = 0; i < 100; ++i) {
        const qint32 top = s.addChild(0, randomName(), PathStore::Folder);
        for (int j = 0; j < 40; ++j) {
            dirs.append(s.addChild(top, randomName(), PathStore::Folder));
        }
    }
    const int target = 100'000;
    int entries = 100 + dirs.size();
    for (int i = 0; entries < target; ++i, ++entries) {
        s.addChild(dirs.at(i % dirs.size()), randomName(), PathStore::File);
    }

    const int total = s.count(PathStore::Folder) + s.count(PathStore::File);
    QCOMPARE(total, target);
    const double perEntry = double(s.bytesUsed()) / total;
    qInfo("G1: %.1f bytes/entry (gate: 36, baseline was ~730)", perEntry);
    QVERIFY2(perEntry <= 36.0,
             qPrintable(QString("G1 gate: %1 bytes/entry > 36").arg(perEntry)));
}
