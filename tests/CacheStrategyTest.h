#ifndef CACHESTRATEGYTEST_H
#define CACHESTRATEGYTEST_H

#include <QObject>

// Cache-strategy tests — verify the priority-driven scan order and the
// path-level system excludes (`/System`, `/private`, …).
//
// These tests poke `PathCacheManager` directly because that's where the
// strategy lives.
class CacheStrategyTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();

    // Path-level excludes
    void systemPathIsNotInCacheAfterScan();
    void privatePathIsNotInCacheAfterScan();
    void devPathIsNotInCacheAfterScan();
    void volumesPathIsNotInCacheAfterScan();
    void normalPathIsInCacheAfterScan();

    // Priority-driven scan order (via expandTo)
    void expandToHandlesMultipleRoots();
    void expandToDeduplicatesAlreadyCovered();
};

#endif // CACHESTRATEGYTEST_H
