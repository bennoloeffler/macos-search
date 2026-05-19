#ifndef CONTENTSEARCHE2ETEST_H
#define CONTENTSEARCHE2ETEST_H

#include <QObject>

class ContentSearchE2ETest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanup();

    void testEndToEndFromCacheToContentMatch();
    void testContentSearchSkipsBlacklistedExtensions();
    void testContentSearchRegexMode();
    void testContentSearchRespectsMaxFileSize();
    void testContentSearchAndAcrossTerms();
    void testContentSearchMultipleFiles();
    void testContentSearchEmptyQueryEmits();
};

#endif // CONTENTSEARCHE2ETEST_H
