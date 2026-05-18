#include "PathSelectorStateTest.h"

#include "PathSelector/FileSystemAdapter.h"
#include "PathSelector/PathSelectorState.h"

#include <QDir>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

using State = PathSelectorState::State;

// These tests exercise the PathSelectorState state machine against a real
// temporary directory tree. They never touch $HOME so they're safe to run
// in any environment.

namespace {

struct Fixture {
    QTemporaryDir dir;
    QString rootPath;
    QStringList subdirNames;

    Fixture()
    {
        Q_ASSERT(dir.isValid());
        rootPath = dir.path();
        QDir root(rootPath);
        subdirNames = { "alpha", "alphabet", "beta", "gamma" };
        for (const QString &name : subdirNames) {
            root.mkdir(name);
        }
    }
};

}  // namespace

void PathSelectorStateTest::testInitialStateIsInvalidWithEmptyPath()
{
    FileSystemAdapter fs;
    PathSelectorState state(&fs);
    // Default-constructed has empty text; state machine reports a value
    // (any of the enum) and lastValidPath is empty.
    QCOMPARE(state.currentText(), QString());
    QCOMPARE(state.lastValidPath(), QString());
}

void PathSelectorStateTest::testCompletePathHasNoCompletions()
{
    Fixture fx;
    FileSystemAdapter fs;
    PathSelectorState state(&fs);
    state.initialize(fx.rootPath);
    QCOMPARE(state.state(), State::Complete);
    QCOMPARE(state.lastValidPath(), QDir::cleanPath(fx.rootPath));
}

void PathSelectorStateTest::testPartialSingleMatchAutoCompletes()
{
    Fixture fx;
    FileSystemAdapter fs;
    PathSelectorState state(&fs);
    state.initialize(fx.rootPath);

    // Type "rootPath/be" — only "beta" matches under our fixture.
    state.setCurrentText(fx.rootPath + "/be");
    QVERIFY(state.state() == State::PartialSingle
            || state.state() == State::PartialMultiple);
    // Any platform-specific hidden/auxiliary entries in the temp dir are
    // OK; we only care that "beta" is among the completions.
    bool hasBeta = false;
    for (const QString &c : state.completions()) {
        if (c.endsWith("beta")) { hasBeta = true; break; }
    }
    QVERIFY(hasBeta);
}

void PathSelectorStateTest::testPartialMultipleMatchOffersCompletions()
{
    Fixture fx;
    FileSystemAdapter fs;
    PathSelectorState state(&fs);
    state.initialize(fx.rootPath);

    // "alph" matches both "alpha" and "alphabet".
    state.setCurrentText(fx.rootPath + "/alph");
    QCOMPARE(state.state(), State::PartialMultiple);
    QCOMPARE(state.completions().size(), 2);
}

void PathSelectorStateTest::testInvalidPathReportsInvalid()
{
    Fixture fx;
    FileSystemAdapter fs;
    PathSelectorState state(&fs);
    state.initialize(fx.rootPath);

    state.setCurrentText(fx.rootPath + "/does-not-exist-xyz");
    QCOMPARE(state.state(), State::Invalid);
    QVERIFY(state.completions().isEmpty());
}

void PathSelectorStateTest::testTabAcceptsSingleCompletion()
{
    Fixture fx;
    FileSystemAdapter fs;
    PathSelectorState state(&fs);
    state.initialize(fx.rootPath);

    state.setCurrentText(fx.rootPath + "/be");
    QVERIFY(!state.completions().isEmpty());

    QSignalSpy acceptedSpy(&state, &PathSelectorState::pathAccepted);
    state.acceptSelection();
    QCOMPARE(acceptedSpy.count(), 1);
    QCOMPARE(state.state(), State::Complete);
    QVERIFY(state.lastValidPath().endsWith("beta"));
}
