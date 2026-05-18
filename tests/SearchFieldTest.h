#ifndef SEARCHFIELDTEST_H
#define SEARCHFIELDTEST_H

#include <QObject>

class SearchFieldTest : public QObject
{
    Q_OBJECT

private slots:
    // UI Structure Tests
    void testHasObjectName();
    void testHasPlaceholderText();
    void testHasClearButton();
    // testUsesSwiftUIStyleSheet removed on port: standalone app does not
    // use SwiftUIStyle; QLineEdit uses native macOS styling.

    // Functionality Tests
    void testTextReturnsCurrentValue();
    void testSetTextUpdatesField();
    void testClearResetsText();

    // Debounce Tests
    void testDebounceDelayDefaultIs150ms();
    void testDebounceDelayCanBeCustomized();
    void testSearchTriggeredAfterDebounceDelay();
    void testSearchNotTriggeredBeforeDebounceDelay();
    void testRapidTypingOnlyTriggersOneSearch();
    void testEmptySearchStillTriggered();

    // Signal Tests
    void testSearchTriggeredSignalEmitted();
    void testTextChangedSignalEmittedImmediately();
};

#endif // SEARCHFIELDTEST_H
