#ifndef EDITORLAUNCHERTEST_H
#define EDITORLAUNCHERTEST_H

#include <QObject>

class EditorLauncherTest : public QObject
{
    Q_OBJECT
private slots:
    void cleanup();

    void testOverrideTakesPrecedence();
    void testFindReturnsEmptyWhenNothingAvailable();
    void testIsAvailableReflectsOverride();
    void testOpenAtLineWithoutFileReturnsFalse();
};

#endif // EDITORLAUNCHERTEST_H
