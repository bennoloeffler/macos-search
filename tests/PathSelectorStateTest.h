#ifndef PATHSELECTORSTATETEST_H
#define PATHSELECTORSTATETEST_H

#include <QObject>

class PathSelectorStateTest : public QObject
{
    Q_OBJECT

private slots:
    void testInitialStateIsInvalidWithEmptyPath();
    void testCompletePathHasNoCompletions();
    void testPartialSingleMatchAutoCompletes();
    void testPartialMultipleMatchOffersCompletions();
    void testInvalidPathReportsInvalid();
    void testTabAcceptsSingleCompletion();
};

#endif // PATHSELECTORSTATETEST_H
