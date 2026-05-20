#include "ScanStateIndicatorTest.h"
#include "ScanStateIndicator.h"

#include <QSignalSpy>
#include <QTest>

using State = ScanStateIndicator::State;

void ScanStateIndicatorTest::testDefaultsToIdle()
{
    ScanStateIndicator w;
    QCOMPARE(w.state(), State::Idle);
    QVERIFY(!w.isCompact());
}

void ScanStateIndicatorTest::testSetStateRoundtrips()
{
    ScanStateIndicator w;
    w.setState(State::Scanning);
    QCOMPARE(w.state(), State::Scanning);
    w.setState(State::Scanned);
    QCOMPARE(w.state(), State::Scanned);
    w.setState(State::Idle);
    QCOMPARE(w.state(), State::Idle);
}

void ScanStateIndicatorTest::testSetStateNoOpForSameState()
{
    ScanStateIndicator w;
    w.setState(State::Idle);  // already Idle
    QCOMPARE(w.state(), State::Idle);
}

void ScanStateIndicatorTest::testClickInIdleEmitsScanRequested()
{
    ScanStateIndicator w;
    w.setRepresentedPath("/Users/me/proj");
    w.resize(80, 22);
    QSignalSpy spy(&w, &ScanStateIndicator::scanRequested);
    QTest::mouseClick(&w, Qt::LeftButton);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().first().toString(), QString("/Users/me/proj"));
}

void ScanStateIndicatorTest::testClickInScanningDoesNotEmit()
{
    ScanStateIndicator w;
    w.setState(State::Scanning);
    w.resize(80, 22);
    QSignalSpy spy(&w, &ScanStateIndicator::scanRequested);
    QTest::mouseClick(&w, Qt::LeftButton);
    QCOMPARE(spy.count(), 0);
}

void ScanStateIndicatorTest::testClickInScannedDoesNotEmit()
{
    ScanStateIndicator w;
    w.setState(State::Scanned);
    w.resize(80, 22);
    QSignalSpy spy(&w, &ScanStateIndicator::scanRequested);
    QTest::mouseClick(&w, Qt::LeftButton);
    QCOMPARE(spy.count(), 0);
}

void ScanStateIndicatorTest::testCompactModeStartsAsDot()
{
    ScanStateIndicator w;
    w.setCompact(true);
    // In compact-and-not-hovered mode the sizeHint is roughly square (dot).
    const QSize hint = w.sizeHint();
    QVERIFY(hint.width() < 30);
}

void ScanStateIndicatorTest::testRepresentedPathFlowsThroughSignal()
{
    ScanStateIndicator w;
    w.setRepresentedPath("/Volumes/Foo/Bar");
    w.resize(80, 22);
    QSignalSpy spy(&w, &ScanStateIndicator::scanRequested);
    QTest::mouseClick(&w, Qt::LeftButton);
    QCOMPARE(spy.first().first().toString(), QString("/Volumes/Foo/Bar"));
}

void ScanStateIndicatorTest::testTooltipIncludesPath()
{
    ScanStateIndicator w;
    w.setRepresentedPath("/Users/me/projects");
    w.setState(State::Idle);
    QVERIFY(w.toolTip().contains("/Users/me/projects"));
    w.setState(State::Scanned);
    QVERIFY(w.toolTip().contains("/Users/me/projects"));
}

void ScanStateIndicatorTest::testSizeHintShrinksInCompactMode()
{
    ScanStateIndicator full;
    ScanStateIndicator compact;
    compact.setCompact(true);
    QVERIFY(compact.sizeHint().width() < full.sizeHint().width());
}
