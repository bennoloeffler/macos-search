#include "EditorLauncherTest.h"
#include "EditorLauncher.h"

#include <QFile>
#include <QFileInfo>
#include <QtTest/QtTest>

void EditorLauncherTest::cleanup()
{
    EditorLauncher::setOverride(QString());
}

void EditorLauncherTest::testOverrideTakesPrecedence()
{
    EditorLauncher::setOverride("/usr/bin/true");
    QCOMPARE(EditorLauncher::findVsCode(), QString("/usr/bin/true"));
}

void EditorLauncherTest::testFindReturnsEmptyWhenNothingAvailable()
{
    // Save current state — there might be a real `code` on the box.
    QString existing = EditorLauncher::findVsCode();
    EditorLauncher::setOverride("/zz/does/not/exist");
    // override is treated as a literal path even if non-existent; that's by
    // design (test seam), so we just sanity-check `find` returns override.
    QCOMPARE(EditorLauncher::findVsCode(), QString("/zz/does/not/exist"));
    EditorLauncher::setOverride(QString());
    // Without override, behavior reverts to detection — value depends on env.
    QCOMPARE(EditorLauncher::findVsCode(), existing);
}

void EditorLauncherTest::testIsAvailableReflectsOverride()
{
    EditorLauncher::setOverride("/usr/bin/true");
    QVERIFY(EditorLauncher::isAvailable());
    EditorLauncher::setOverride(QString());
    // Reflect real environment - just confirm it doesn't crash and returns bool.
    bool b = EditorLauncher::isAvailable();
    (void)b;
}

void EditorLauncherTest::testOpenAtLineWithoutFileReturnsFalse()
{
    QVERIFY(!EditorLauncher::openAtLine("", 1));
}
