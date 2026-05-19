#ifndef RIPGREPRUNNERTEST_H
#define RIPGREPRUNNERTEST_H

#include <QObject>

class RipgrepRunnerTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanup();

    void testParseJsonLineHandlesMatchType();
    void testParseJsonLineIgnoresNonMatchTypes();
    void testParseJsonLineHandlesEmptyInput();
    void testParseJsonLineHandlesGarbage();
    void testParseJsonLineExtractsSubmatchOffsets();
    void testParseJsonLineTruncatesLongLines();
    void testFindBinaryHonorsOverride();
    void testFindBinaryReturnsPathBinaryIfAvailable();
    void testStartFailsWithoutBinary();
    void testStartFailsWithEmptyQuery();
    void testStartFailsWithEmptyFiles();
    void testIntegrationFixedStringFindsMatch();
    void testIntegrationRegexMode();
    void testCancelStopsRunning();

private:
    bool m_rgAvailable = false;
    QString m_savedOverride;
};

#endif // RIPGREPRUNNERTEST_H
