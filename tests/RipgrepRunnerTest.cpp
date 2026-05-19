#include "RipgrepRunnerTest.h"
#include "RipgrepRunner.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest/QtTest>

void RipgrepRunnerTest::initTestCase()
{
    qRegisterMetaType<QList<ContentMatch>>("QList<ContentMatch>");
    m_rgAvailable = !RipgrepRunner::findBinary().isEmpty();
}

void RipgrepRunnerTest::cleanup()
{
    RipgrepRunner::setBinaryOverride(QString());
}

void RipgrepRunnerTest::testParseJsonLineHandlesMatchType()
{
    const QString line = R"({"type":"match","data":{"path":{"text":"/x/y.txt"},)"
                         R"("lines":{"text":"hello world\n"},"line_number":3,)"
                         R"("absolute_offset":0,"submatches":[{"match":{"text":"hello"},)"
                         R"("start":0,"end":5}]}})";
    auto matches = RipgrepRunner::parseJsonLine(line);
    QCOMPARE(matches.size(), 1);
    QCOMPARE(matches.first().filePath, QString("/x/y.txt"));
    QCOMPARE(matches.first().lineNumber, 3);
    QCOMPARE(matches.first().snippet, QString("hello world"));
    QCOMPARE(matches.first().matchStart, 0);
    QCOMPARE(matches.first().matchEnd, 5);
}

void RipgrepRunnerTest::testParseJsonLineIgnoresNonMatchTypes()
{
    const QString line = R"({"type":"begin","data":{"path":{"text":"/x/y.txt"}}})";
    auto matches = RipgrepRunner::parseJsonLine(line);
    QCOMPARE(matches.size(), 0);
}

void RipgrepRunnerTest::testParseJsonLineHandlesEmptyInput()
{
    auto matches = RipgrepRunner::parseJsonLine("");
    QCOMPARE(matches.size(), 0);
}

void RipgrepRunnerTest::testParseJsonLineHandlesGarbage()
{
    auto matches = RipgrepRunner::parseJsonLine("not-json-at-all{");
    QCOMPARE(matches.size(), 0);
}

void RipgrepRunnerTest::testParseJsonLineExtractsSubmatchOffsets()
{
    const QString line = R"({"type":"match","data":{"path":{"text":"/x"},"lines":)"
                         R"({"text":"prefix matched suffix"},"line_number":1,)"
                         R"("submatches":[{"match":{"text":"matched"},"start":7,"end":14}]}})";
    auto matches = RipgrepRunner::parseJsonLine(line);
    QCOMPARE(matches.size(), 1);
    QCOMPARE(matches.first().matchStart, 7);
    QCOMPARE(matches.first().matchEnd, 14);
}

void RipgrepRunnerTest::testParseJsonLineTruncatesLongLines()
{
    QString longLine(500, QChar('x'));
    longLine.replace(450, 5, "MATCH");
    QString jsonInner = QString(R"({"type":"match","data":{"path":{"text":"/x"},"lines":{"text":"%1"},"line_number":1,"submatches":[{"match":{"text":"MATCH"},"start":450,"end":455}]}})").arg(longLine);
    auto matches = RipgrepRunner::parseJsonLine(jsonInner);
    QCOMPARE(matches.size(), 1);
    QVERIFY(matches.first().snippet.length() <= 200);
}

void RipgrepRunnerTest::testFindBinaryHonorsOverride()
{
    RipgrepRunner::setBinaryOverride("/fake/path/rg");
    QCOMPARE(RipgrepRunner::findBinary(), QString("/fake/path/rg"));
}

void RipgrepRunnerTest::testFindBinaryReturnsPathBinaryIfAvailable()
{
    if (!m_rgAvailable) QSKIP("ripgrep not installed on this machine");
    QString found = RipgrepRunner::findBinary();
    QVERIFY(!found.isEmpty());
}

void RipgrepRunnerTest::testStartFailsWithoutBinary()
{
    RipgrepRunner::setBinaryOverride("/nope/does/not/exist/rg");
    RipgrepRunner r;
    QSignalSpy errSpy(&r, &RipgrepRunner::errorOccurred);
    bool started = r.start("foo", { "/tmp/x" }, false, 5);
    QVERIFY(!started);
    QCOMPARE(errSpy.count(), 1);
}

void RipgrepRunnerTest::testStartFailsWithEmptyQuery()
{
    RipgrepRunner r;
    bool started = r.start("", { "/tmp/x" }, false, 5);
    QVERIFY(!started);
}

void RipgrepRunnerTest::testStartFailsWithEmptyFiles()
{
    RipgrepRunner r;
    bool started = r.start("foo", {}, false, 5);
    QVERIFY(!started);
}

void RipgrepRunnerTest::testIntegrationFixedStringFindsMatch()
{
    if (!m_rgAvailable) QSKIP("ripgrep not installed on this machine");
    QTemporaryDir tdir;
    QVERIFY(tdir.isValid());

    const QString p1 = tdir.filePath("a.txt");
    QFile f1(p1);
    QVERIFY(f1.open(QIODevice::WriteOnly | QIODevice::Text));
    f1.write("line one\nthe quick brown fox\nline three\n");
    f1.close();

    RipgrepRunner r;
    QSignalSpy matchSpy(&r, &RipgrepRunner::matchesReady);
    QSignalSpy doneSpy(&r, &RipgrepRunner::finished);
    QVERIFY(r.start("quick", { p1 }, false, 5));
    QVERIFY(doneSpy.wait(5000));

    int total = 0;
    for (int i = 0; i < matchSpy.count(); ++i) {
        total += matchSpy.at(i).first().value<QList<ContentMatch>>().size();
    }
    QCOMPARE(total, 1);
}

void RipgrepRunnerTest::testIntegrationRegexMode()
{
    if (!m_rgAvailable) QSKIP("ripgrep not installed on this machine");
    QTemporaryDir tdir;
    QVERIFY(tdir.isValid());

    const QString p1 = tdir.filePath("a.txt");
    QFile f1(p1);
    QVERIFY(f1.open(QIODevice::WriteOnly | QIODevice::Text));
    f1.write("v1.2.3\nfoo\nv4.5.6\n");
    f1.close();

    RipgrepRunner r;
    QSignalSpy matchSpy(&r, &RipgrepRunner::matchesReady);
    QSignalSpy doneSpy(&r, &RipgrepRunner::finished);
    QVERIFY(r.start("v\\d\\.\\d\\.\\d", { p1 }, true, 5));
    QVERIFY(doneSpy.wait(5000));

    int total = 0;
    for (int i = 0; i < matchSpy.count(); ++i) {
        total += matchSpy.at(i).first().value<QList<ContentMatch>>().size();
    }
    QCOMPARE(total, 2);
}

void RipgrepRunnerTest::testCancelStopsRunning()
{
    if (!m_rgAvailable) QSKIP("ripgrep not installed on this machine");
    RipgrepRunner r;
    r.start("anything", { "/tmp/no-such-file-xyz" }, false, 5);
    r.cancel();
    QVERIFY(!r.isRunning());
}
