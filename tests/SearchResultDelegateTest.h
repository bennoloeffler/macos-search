#ifndef SEARCHRESULTDELEGATETEST_H
#define SEARCHRESULTDELEGATETEST_H

#include <QObject>

class SearchResultDelegateTest : public QObject
{
    Q_OBJECT
private slots:
    void testEncodeDecodeRangesRoundTrip();
    void testDecodeEmpty();
    void testDecodeGarbageIsTolerated();
    void testSizeHintParentRowFixedHeight();
    void testSizeHintChildRowFixedHeight();
    void testSizeHintEmptyRowFixedHeight();
    void testNaturalWidthMonotonicInPathLength();
    void testDelegateInstallsOnList();
    void testRowsRenderViaDelegateNotItemWidget();
};

#endif // SEARCHRESULTDELEGATETEST_H
