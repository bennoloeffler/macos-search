#include "SearchFieldTest.h"
#include "SearchField.h"
#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QLineEdit>

void SearchFieldTest::testHasObjectName()
{
    SearchField field;
    QCOMPARE(field.objectName(), QString("searchField"));
}

void SearchFieldTest::testHasPlaceholderText()
{
    SearchField field;
    auto *lineEdit = field.findChild<QLineEdit*>("searchLineEdit");
    QVERIFY(lineEdit != nullptr);
    QVERIFY(!lineEdit->placeholderText().isEmpty());
}

void SearchFieldTest::testHasClearButton()
{
    SearchField field;
    auto *lineEdit = field.findChild<QLineEdit*>("searchLineEdit");
    QVERIFY(lineEdit != nullptr);
    QVERIFY(lineEdit->isClearButtonEnabled());
}

void SearchFieldTest::testTextReturnsCurrentValue()
{
    SearchField field;
    auto *lineEdit = field.findChild<QLineEdit*>("searchLineEdit");
    QVERIFY(lineEdit != nullptr);

    QVERIFY(field.text().isEmpty());
    lineEdit->setText("test query");
    QCOMPARE(field.text(), QString("test query"));
}

void SearchFieldTest::testSetTextUpdatesField()
{
    SearchField field;
    field.setText("hello world");
    QCOMPARE(field.text(), QString("hello world"));
}

void SearchFieldTest::testClearResetsText()
{
    SearchField field;
    field.setText("some text");
    QCOMPARE(field.text(), QString("some text"));

    field.clear();
    QVERIFY(field.text().isEmpty());
}

void SearchFieldTest::testDebounceDelayDefaultIs150ms()
{
    SearchField field;
    QCOMPARE(field.debounceDelay(), 150);
}

void SearchFieldTest::testDebounceDelayCanBeCustomized()
{
    SearchField field;
    field.setDebounceDelay(300);
    QCOMPARE(field.debounceDelay(), 300);
}

void SearchFieldTest::testSearchTriggeredAfterDebounceDelay()
{
    SearchField field;
    field.setDebounceDelay(50); // Short delay for testing

    QSignalSpy spy(&field, &SearchField::searchTriggered);
    QVERIFY(spy.isValid());

    field.setText("query");

    // Wait for debounce - process events to let the timer fire
    QTest::qWait(100);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().first().toString(), QString("query"));
}

void SearchFieldTest::testSearchNotTriggeredBeforeDebounceDelay()
{
    SearchField field;
    field.setDebounceDelay(200);

    QSignalSpy spy(&field, &SearchField::searchTriggered);
    QVERIFY(spy.isValid());

    field.setText("query");

    // Wait less than debounce delay
    QTest::qWait(50);
    QCOMPARE(spy.count(), 0);

    // Wait for debounce to complete to avoid segfault on destruction
    QTest::qWait(200);
}

void SearchFieldTest::testRapidTypingOnlyTriggersOneSearch()
{
    SearchField field;
    field.setDebounceDelay(50);

    QSignalSpy spy(&field, &SearchField::searchTriggered);
    QVERIFY(spy.isValid());

    // Simulate rapid typing
    field.setText("a");
    QTest::qWait(10);
    field.setText("ab");
    QTest::qWait(10);
    field.setText("abc");
    QTest::qWait(10);
    field.setText("abcd");

    // Wait for debounce after final keystroke
    QTest::qWait(100);

    // Only one search should be triggered with final value
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().first().toString(), QString("abcd"));
}

void SearchFieldTest::testEmptySearchStillTriggered()
{
    SearchField field;
    field.setDebounceDelay(50);

    // Set some text first
    field.setText("query");

    // Wait for the first search to complete
    QTest::qWait(100);

    QSignalSpy spy(&field, &SearchField::searchTriggered);
    QVERIFY(spy.isValid());

    // Clear the text
    field.clear();

    // Wait for debounce
    QTest::qWait(100);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().first().toString(), QString(""));
}

void SearchFieldTest::testSearchTriggeredSignalEmitted()
{
    SearchField field;
    field.setDebounceDelay(50);

    QSignalSpy spy(&field, &SearchField::searchTriggered);
    QVERIFY(spy.isValid());

    field.setText("test");

    QTest::qWait(100);
    QCOMPARE(spy.count(), 1);
}

void SearchFieldTest::testTextChangedSignalEmittedImmediately()
{
    SearchField field;

    QSignalSpy spy(&field, &SearchField::textChanged);
    QVERIFY(spy.isValid());

    field.setText("instant");

    // textChanged should be emitted immediately (not debounced)
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().first().toString(), QString("instant"));
}
