#include "FileSearchIntegrationTest.h"
#include "ExcludeSettings.h"
#include "FileCacheManager.h"
#include "PathCacheManager.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest/QtTest>

namespace {

// Write a file at `path` containing one byte. Creates parent dirs.
void writeFile(const QString &path)
{
    QFileInfo info(path);
    QDir().mkpath(info.absolutePath());
    QFile f(path);
    QVERIFY2(f.open(QIODevice::WriteOnly), qPrintable(path));
    f.write(".");
    f.close();
}

// Run a fresh scan starting at `root` and wait for completion.
void scanAndWait(const QString &root)
{
    PathCacheManager *cache = PathCacheManager::instance();
    QSignalSpy doneSpy(cache, &PathCacheManager::scanComplete);
    cache->restartScanFrom(root);
    QVERIFY(doneSpy.wait(15000));
}

}  // namespace

void FileSearchIntegrationTest::initTestCase()
{
    // Make sure no previous scan is still alive when this suite starts.
    PathCacheManager::instance()->stopScan();
    // QTemporaryDir lives in /var/folders/... outside $HOME, but the file
    // cache scope guard wouldn't let us index it. Tests run with the home
    // override pointing at "/" so scans of the temp directory work.
    FileCacheManager::setHomeOverrideForTests("/");
}

void FileSearchIntegrationTest::cleanup()
{
    PathCacheManager::instance()->stopScan();
    FileCacheManager::instance()->clear();
    FileCacheManager::instance()->setSoftCap(FileCacheManager::kDefaultSoftCap);
    FileCacheManager::instance()->setHardCeiling(FileCacheManager::kDefaultHardCeiling);
    // Keep the home override in place for the next test in this class; the
    // last test triggers the global clear via QTest's cleanupTestCase contract.
    FileCacheManager::setHomeOverrideForTests("/");
}

void FileSearchIntegrationTest::testScanPopulatesFileCache()
{
    QTemporaryDir tdir;
    QVERIFY(tdir.isValid());
    writeFile(tdir.filePath("alpha.txt"));
    writeFile(tdir.filePath("subdir/beta.md"));

    static ExcludeSettings settings;
    PathCacheManager::instance()->setExcludeSettings(&settings);

    scanAndWait(tdir.path());

    FileCacheManager *fc = FileCacheManager::instance();
    QVERIFY(fc->contains(tdir.filePath("alpha.txt")));
    QVERIFY(fc->contains(tdir.filePath("subdir/beta.md")));
}

void FileSearchIntegrationTest::testScanRespectsFileExcludePatterns()
{
    QTemporaryDir tdir;
    QVERIFY(tdir.isValid());
    writeFile(tdir.filePath("good.txt"));
    writeFile(tdir.filePath(".DS_Store"));
    writeFile(tdir.filePath("intermediate.pyc"));

    static ExcludeSettings settings;
    // .DS_Store + *.pyc are in the default file-exclude list.
    PathCacheManager::instance()->setExcludeSettings(&settings);

    scanAndWait(tdir.path());

    FileCacheManager *fc = FileCacheManager::instance();
    QVERIFY(fc->contains(tdir.filePath("good.txt")));
    QVERIFY(!fc->contains(tdir.filePath(".DS_Store")));
    QVERIFY(!fc->contains(tdir.filePath("intermediate.pyc")));
}

void FileSearchIntegrationTest::testOpaqueBundleNotDescended()
{
    QTemporaryDir tdir;
    QVERIFY(tdir.isValid());
    // Create a fake bundle directory with a file inside.
    QDir().mkpath(tdir.filePath("Foo.app/Contents/Resources"));
    writeFile(tdir.filePath("Foo.app/Contents/Resources/secret.txt"));
    writeFile(tdir.filePath("regular/visible.txt"));

    static ExcludeSettings settings;
    PathCacheManager::instance()->setExcludeSettings(&settings);

    scanAndWait(tdir.path());

    FileCacheManager *fc = FileCacheManager::instance();
    QVERIFY(fc->contains(tdir.filePath("regular/visible.txt")));
    QVERIFY(!fc->contains(tdir.filePath("Foo.app/Contents/Resources/secret.txt")));
}

void FileSearchIntegrationTest::testScanRespectsFolderExcludePatterns()
{
    QTemporaryDir tdir;
    QVERIFY(tdir.isValid());
    writeFile(tdir.filePath("project/main.cpp"));
    writeFile(tdir.filePath("project/node_modules/dep.js"));

    static ExcludeSettings settings;
    PathCacheManager::instance()->setExcludeSettings(&settings);

    scanAndWait(tdir.path());

    FileCacheManager *fc = FileCacheManager::instance();
    QVERIFY(fc->contains(tdir.filePath("project/main.cpp")));
    QVERIFY(!fc->contains(tdir.filePath("project/node_modules/dep.js")));
}

void FileSearchIntegrationTest::testRescanRebuildsFileCache()
{
    QTemporaryDir tdir;
    QVERIFY(tdir.isValid());
    writeFile(tdir.filePath("first.txt"));

    static ExcludeSettings settings;
    PathCacheManager::instance()->setExcludeSettings(&settings);

    scanAndWait(tdir.path());
    FileCacheManager *fc = FileCacheManager::instance();
    QVERIFY(fc->contains(tdir.filePath("first.txt")));

    // Add another file and rescan from the same root.
    writeFile(tdir.filePath("second.txt"));
    scanAndWait(tdir.path());
    QVERIFY(fc->contains(tdir.filePath("first.txt")));
    QVERIFY(fc->contains(tdir.filePath("second.txt")));
}
