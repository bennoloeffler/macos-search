#ifndef FILESEARCHINTEGRATIONTEST_H
#define FILESEARCHINTEGRATIONTEST_H

#include <QObject>

class FileSearchIntegrationTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanup();

    // End-to-end via PathCacheManager::expandTo (scan walk feeds both caches).
    void testScanPopulatesFileCache();
    void testScanRespectsFileExcludePatterns();
    void testOpaqueBundleNotDescended();
    void testScanRespectsFolderExcludePatterns();
    void testRescanRebuildsFileCache();
};

#endif // FILESEARCHINTEGRATIONTEST_H
