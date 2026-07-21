#ifndef SCANQUEUETEST_H
#define SCANQUEUETEST_H

#include <QObject>

// Priority order of startup scan roots — most-probable search targets
// first (docs: user request 2026-07-21). Pure-function tests, no I/O.
class ScanQueueTest : public QObject
{
    Q_OBJECT

private slots:
    void testMostProbableTargetsComeFirst();
    void testDefaultStartLeadsWhenSet();
    void testDeduplicatesAcrossSources();
    void testDropboxDirsSlotBeforeHome();
};

#endif // SCANQUEUETEST_H
