#ifndef SCANSTATEINDICATORTEST_H
#define SCANSTATEINDICATORTEST_H

#include <QObject>

class ScanStateIndicatorTest : public QObject
{
    Q_OBJECT
private slots:
    void testDefaultsToIdle();
    void testSetStateRoundtrips();
    void testSetStateNoOpForSameState();
    void testClickInIdleEmitsScanRequested();
    void testClickInScanningDoesNotEmit();
    void testClickInScannedDoesNotEmit();
    void testCompactModeStartsAsDot();
    void testRepresentedPathFlowsThroughSignal();
    void testTooltipIncludesPath();
    void testSizeHintShrinksInCompactMode();
};

#endif // SCANSTATEINDICATORTEST_H
