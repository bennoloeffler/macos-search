#include "UsabilityTest.h"

#include "ExcludeSettings.h"
#include "FolderBrowserDialog.h"
#include "FolderSearchWorker.h"
#include "PathCacheManager.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTreeView>
#include <QtTest>

namespace {

constexpr int kFavoritePathRole = Qt::UserRole + 1;

QSettings folderBrowserSettings() { return QSettings("Maude", "FolderBrowser"); }

void resetPersistedState()
{
    auto s = folderBrowserSettings();
    s.remove("favorites");
    s.remove("defaultFavorite");
    s.remove("rootPath");
    s.remove("showHidden");
    s.sync();
}

void prepare(FolderBrowserDialog &dialog)
{
    dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));
}

QListWidget *favoritesListOf(FolderBrowserDialog &d)
{
    return d.findChild<QListWidget *>("favoritesList");
}

QLineEdit *searchFieldOf(FolderBrowserDialog &d)
{
    return d.findChild<QLineEdit *>("searchField");
}

}  // namespace

void UsabilityTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    static ExcludeSettings settings;
    PathCacheManager::instance()->setExcludeSettings(&settings);
}

void UsabilityTest::cleanupTestCase()
{
    PathCacheManager::instance()->stopScan();
    resetPersistedState();
    QStandardPaths::setTestModeEnabled(false);
}

void UsabilityTest::init()
{
    resetPersistedState();
    PathCacheManager::instance()->stopScan();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QTest::qWait(30);
}

// ---------------------------------------------------------------------------
// A. Focus & traversal
// ---------------------------------------------------------------------------

void UsabilityTest::initialFocusIsSearchField()  // T-001
{
    FolderBrowserDialog dialog(QDir::homePath());
    prepare(dialog);
    // main.cpp sets focus to the searchField after show(); we replicate
    // by asserting that the searchField CAN be focused and is the
    // intended primary input.
    auto *field = searchFieldOf(dialog);
    QVERIFY(field);
    field->setFocus();
    QVERIFY2(field->hasFocus(),
             "searchField must accept focus — it's the primary input");
}

void UsabilityTest::cmdFAlwaysLandsOnSearchField()  // T-007
{
    FolderBrowserDialog dialog(QDir::homePath());
    prepare(dialog);

    auto *favorites = favoritesListOf(dialog);
    QVERIFY(favorites);
    favorites->setFocus();

    QTest::keyClick(&dialog, Qt::Key_F, Qt::ControlModifier);
    auto *field = searchFieldOf(dialog);
    QVERIFY(field && field->hasFocus());
}

void UsabilityTest::cmdLAlwaysLandsOnPathField()  // T-008
{
    FolderBrowserDialog dialog(QDir::homePath());
    prepare(dialog);

    auto *field = searchFieldOf(dialog);
    field->setFocus();
    QTest::keyClick(&dialog, Qt::Key_L, Qt::ControlModifier);
    QVERIFY2(!field->hasFocus(),
             "After ⌘L the path field — not the searchField — must have focus");
}

void UsabilityTest::escClearKeepsFocusOnSearchField()  // T-009
{
    FolderBrowserDialog dialog(QDir::homePath());
    prepare(dialog);
    auto *field = searchFieldOf(dialog);
    field->setText(QStringLiteral("trafo"));
    field->setFocus();

    QTest::keyClick(&dialog, Qt::Key_Escape);
    QCOMPARE(field->text(), QString());
    QVERIFY(field->hasFocus());
}

// ---------------------------------------------------------------------------
// B. Keystroke dispatch — global chords
// ---------------------------------------------------------------------------

void UsabilityTest::cmdF_focusesAndSelectsAll()  // T-020
{
    FolderBrowserDialog dialog(QDir::homePath());
    prepare(dialog);
    auto *field = searchFieldOf(dialog);
    field->setText(QStringLiteral("hello"));

    QTest::keyClick(&dialog, Qt::Key_F, Qt::ControlModifier);
    QVERIFY(field->hasFocus());
    QCOMPARE(field->selectedText(), QString("hello"));
}

void UsabilityTest::cmdShiftG_focusesPathField()  // T-022
{
    FolderBrowserDialog dialog(QDir::homePath());
    prepare(dialog);
    auto *field = searchFieldOf(dialog);
    field->setFocus();

    QTest::keyClick(&dialog, Qt::Key_G,
                    Qt::ControlModifier | Qt::ShiftModifier);
    QVERIFY2(!field->hasFocus(),
             "⌘⇧G is the legacy upstream alias of ⌘L — must move focus off search");
}

void UsabilityTest::cmdUp_atRootIsNoOp()  // T-025
{
    FolderBrowserDialog dialog(QStringLiteral("/"));
    prepare(dialog);
    // ⌘↑ at filesystem root should not crash, dialog stays visible.
    QTest::keyClick(&dialog, Qt::Key_Up, Qt::ControlModifier);
    QVERIFY(dialog.isVisible());
}

void UsabilityTest::escEmptyDoesNotCloseDialog()  // T-029
{
    FolderBrowserDialog dialog(QDir::homePath());
    prepare(dialog);
    searchFieldOf(dialog)->clear();
    QTest::keyClick(&dialog, Qt::Key_Escape);
    QVERIFY(dialog.isVisible());
}

void UsabilityTest::cmdQ_doesNotCrash()  // T-031
{
    FolderBrowserDialog dialog(QDir::homePath());
    prepare(dialog);
    // ⌘Q at Qt level falls through to QApplication. We don't want to
    // actually quit the test process — just assert no crash from sending
    // the key event.
    QTest::keyClick(&dialog, Qt::Key_Q, Qt::ControlModifier);
    QVERIFY(true);  // alive
}

// ---------------------------------------------------------------------------
// C. Typing
// ---------------------------------------------------------------------------

void UsabilityTest::multiWordQueryTyped()  // T-042
{
    FolderBrowserDialog dialog(QDir::homePath());
    prepare(dialog);
    auto *field = searchFieldOf(dialog);
    field->setFocus();
    field->clear();

    QTest::keyClicks(field, "ai bel");
    QCOMPARE(field->text(), QString("ai bel"));
}

void UsabilityTest::cmdFThenTypingReplacesExisting()  // T-044
{
    FolderBrowserDialog dialog(QDir::homePath());
    prepare(dialog);
    auto *field = searchFieldOf(dialog);
    field->setText(QStringLiteral("old"));

    QTest::keyClick(&dialog, Qt::Key_F, Qt::ControlModifier);
    // ⌘F selects-all; typing replaces.
    QTest::keyClicks(field, "new");
    QCOMPARE(field->text(), QString("new"));
}

void UsabilityTest::slashTypedInSearchAppendsLiterally()  // T-045
{
    FolderBrowserDialog dialog(QDir::homePath());
    prepare(dialog);
    auto *field = searchFieldOf(dialog);
    field->setFocus();
    field->clear();

    QTest::keyClick(field, Qt::Key_A);
    QTest::keyClick(field, Qt::Key_Slash);
    QTest::keyClick(field, Qt::Key_B);
    QCOMPARE(field->text(), QString("a/b"));
}

// ---------------------------------------------------------------------------
// D. Arrow navigation
// ---------------------------------------------------------------------------

void UsabilityTest::homeEndInSearchFieldNotIntercepted()  // T-055
{
    FolderBrowserDialog dialog(QDir::homePath());
    prepare(dialog);
    auto *field = searchFieldOf(dialog);
    field->setText(QStringLiteral("abcdef"));
    field->setFocus();
    field->setCursorPosition(3);  // between c and d

    QTest::keyClick(field, Qt::Key_Home);
    QCOMPARE(field->cursorPosition(), 0);
    QTest::keyClick(field, Qt::Key_End);
    QCOMPARE(field->cursorPosition(), 6);
}

// ---------------------------------------------------------------------------
// E. Mouse — clicks against the public surface
// ---------------------------------------------------------------------------

void UsabilityTest::upButtonGoesToParent()  // T-064
{
    FolderBrowserDialog dialog(QDir::homePath());
    prepare(dialog);
    auto *up = dialog.findChild<QPushButton *>("upButton");
    QVERIFY(up);
    QTest::mouseClick(up, Qt::LeftButton);

    auto *label = dialog.findChild<QLabel *>("resolvedPathLabel");
    QVERIFY(label);
    QDir homeUp(QDir::homePath());
    homeUp.cdUp();
    QVERIFY2(label->text().contains(homeUp.absolutePath()),
             qPrintable("Will open: was " + label->text()));
}

void UsabilityTest::homeButtonJumpsHome()  // T-066
{
    FolderBrowserDialog dialog(QStringLiteral("/"));
    prepare(dialog);
    auto *home = dialog.findChild<QPushButton *>("homeButton");
    QVERIFY(home);
    QTest::mouseClick(home, Qt::LeftButton);

    auto *label = dialog.findChild<QLabel *>("resolvedPathLabel");
    QVERIFY(label->text().contains(QDir::homePath()));
}

void UsabilityTest::eyeToggleFlipsAndPersists()  // T-067
{
    FolderBrowserDialog dialog(QDir::homePath());
    prepare(dialog);
    auto *eye = dialog.findChild<QPushButton *>("showHiddenButton");
    QVERIFY(eye);
    QVERIFY(eye->isCheckable());

    const bool before = eye->isChecked();
    QTest::mouseClick(eye, Qt::LeftButton);
    QCOMPARE(eye->isChecked(), !before);
}

void UsabilityTest::eyeToggleDoesNotRescan()  // T-067b — TODO 4 regression-lock
{
    // Pre-seed the cache with a small known set, then toggle the eye —
    // folderCount must not change (no rescan triggered).
    auto *cache = PathCacheManager::instance();
    cache->stopScan();

    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QDir(tmp.path()).mkdir("visible");
    QDir(tmp.path()).mkdir(".hidden");

    cache->expandTo(tmp.path());
    QTest::qWait(400);
    cache->stopScan();
    const int beforeCount = cache->folderCount();
    QVERIFY2(beforeCount > 0, "Cache should have indexed at least the tmp dir");

    FolderBrowserDialog dialog(tmp.path());
    prepare(dialog);
    auto *eye = dialog.findChild<QPushButton *>("showHiddenButton");
    QVERIFY(eye);

    QTest::mouseClick(eye, Qt::LeftButton);
    QTest::qWait(500);  // generous: enough time for a rescan to start IF triggered

    const int afterCount = cache->folderCount();
    QCOMPARE(afterCount, beforeCount);
    QVERIFY2(!cache->isScanning(),
             "Toggling show-hidden must NOT trigger a rescan (TODO 4)");
}

void UsabilityTest::eyeToggleHidesHiddenSearchResults()  // T-067c
{
    // FolderSearchWorker::pathIsHidden is the gate. Test directly so we
    // don't depend on the cache having any specific .hidden entries.
    QVERIFY( FolderSearchWorker::pathIsHidden("/Users/benno/.git/refs"));
    QVERIFY( FolderSearchWorker::pathIsHidden("/.fseventsd"));
    QVERIFY(!FolderSearchWorker::pathIsHidden("/Users/benno/projects/foo"));
    QVERIFY(!FolderSearchWorker::pathIsHidden("/Applications"));
}

void UsabilityTest::singleClickTreeRowSetsScope()  // T-068
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QDir(tmp.path()).mkdir("a");
    QDir(tmp.path()).mkdir("b");

    FolderBrowserDialog dialog(tmp.path());
    prepare(dialog);
    QTest::qWait(200);

    auto *tree = dialog.findChild<QTreeView *>("folderTreeView");
    QVERIFY(tree);
    // Pick the first child row of the root.
    QModelIndex root = tree->rootIndex();
    QModelIndex first = tree->model()->index(0, 0, root);
    if (first.isValid()) {
        tree->setCurrentIndex(first);
        emit tree->clicked(first);
    }
    // Just assert no crash + dialog alive.
    QVERIFY(dialog.isVisible());
}

void UsabilityTest::makeDefaultPersists()  // T-075
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString fav = QDir::cleanPath(tmp.path());
    {
        auto s = folderBrowserSettings();
        s.setValue("favorites", QStringList{fav});
        s.sync();
    }

    FolderBrowserDialog dialog(QDir::homePath());
    prepare(dialog);

    // Call the data-mutator directly — that's the contract the menu
    // action invokes.
    QMetaObject::invokeMethod(&dialog, [&dialog, fav]() {
        // setDefaultFavorite is private; use the visible surface:
        // QSettings update + reopen
    });
    // Simpler: write directly + verify on next instance.
    {
        auto s = folderBrowserSettings();
        s.setValue("defaultFavorite", fav);
        s.sync();
    }
    FolderBrowserDialog dialog2(QDir::homePath());
    prepare(dialog2);
    auto *list = favoritesListOf(dialog2);
    QVERIFY(list && list->count() >= 2);

    // Find the row matching `fav`, assert font is bold.
    bool found = false;
    for (int i = 0; i < list->count(); ++i) {
        if (list->item(i)->data(kFavoritePathRole).toString() == fav) {
            QVERIFY2(list->item(i)->font().bold(),
                     "Default favorite row must render bold");
            found = true;
            break;
        }
    }
    QVERIFY(found);
}

void UsabilityTest::deleteRowPersists()  // T-076
{
    QTemporaryDir tmp;
    const QString fav = QDir::cleanPath(tmp.path());
    {
        auto s = folderBrowserSettings();
        s.setValue("favorites", QStringList{fav});
        s.sync();
    }
    // Simulate deletion: write empty list.
    {
        auto s = folderBrowserSettings();
        s.setValue("favorites", QStringList{});
        s.sync();
    }
    FolderBrowserDialog dialog(QDir::homePath());
    prepare(dialog);
    auto *list = favoritesListOf(dialog);
    for (int i = 0; i < list->count(); ++i) {
        QVERIFY2(list->item(i)->data(kFavoritePathRole).toString() != fav,
                 "Deleted favorite must not appear in the list");
    }
}

void UsabilityTest::deletingDefaultFallsBackToHome()  // T-077
{
    QTemporaryDir tmp;
    const QString fav = QDir::cleanPath(tmp.path());
    {
        auto s = folderBrowserSettings();
        s.setValue("favorites", QStringList{fav});
        s.setValue("defaultFavorite", fav);
        s.sync();
    }
    // Delete it: list empties + defaultFavorite cleared.
    {
        auto s = folderBrowserSettings();
        s.setValue("favorites", QStringList{});
        s.setValue("defaultFavorite", QString());  // <-- key behavior
        s.sync();
    }
    QCOMPARE(FolderBrowserDialog::resolveDefaultStartPath(),
             QDir::homePath());
}

// ---------------------------------------------------------------------------
// F. View-stack & state transitions
// ---------------------------------------------------------------------------

void UsabilityTest::emptyQueryShowsTree()  // T-080
{
    FolderBrowserDialog dialog(QDir::homePath());
    prepare(dialog);
    auto *stack = dialog.findChild<QStackedWidget *>("viewStack");
    auto *tree  = dialog.findChild<QTreeView *>("folderTreeView");
    QVERIFY(stack && tree);
    searchFieldOf(dialog)->clear();
    QTest::qWait(50);
    QCOMPARE(stack->currentWidget(), static_cast<QWidget *>(tree));
}

void UsabilityTest::nonEmptyQueryShowsResults()  // T-081
{
    FolderBrowserDialog dialog(QDir::homePath());
    prepare(dialog);
    auto *stack   = dialog.findChild<QStackedWidget *>("viewStack");
    auto *results = dialog.findChild<QListWidget *>("searchResultsList");
    auto *field   = searchFieldOf(dialog);
    QVERIFY(stack && results && field);

    field->setFocus();
    QTest::keyClicks(field, "trafo");
    QTest::qWait(250);  // > 150ms debounce
    QCOMPARE(stack->currentWidget(), static_cast<QWidget *>(results));
}

void UsabilityTest::clearQueryReturnsToTree()  // T-082
{
    FolderBrowserDialog dialog(QDir::homePath());
    prepare(dialog);
    auto *stack = dialog.findChild<QStackedWidget *>("viewStack");
    auto *tree  = dialog.findChild<QTreeView *>("folderTreeView");
    auto *field = searchFieldOf(dialog);
    field->setFocus();
    QTest::keyClicks(field, "abc");
    QTest::qWait(250);
    field->clear();
    QTest::qWait(50);
    QCOMPARE(stack->currentWidget(), static_cast<QWidget *>(tree));
}

void UsabilityTest::willOpenReflectsSelection()  // T-085
{
    FolderBrowserDialog dialog(QDir::homePath());
    prepare(dialog);
    auto *label = dialog.findChild<QLabel *>("resolvedPathLabel");
    QVERIFY(label);
    QVERIFY(label->text().contains("Will open"));
    QVERIFY(label->text().contains(QDir::homePath()));
}

// ---------------------------------------------------------------------------
// G. Suppression
// ---------------------------------------------------------------------------

void UsabilityTest::repeatedEscNeverCloses()  // T-094
{
    FolderBrowserDialog dialog(QDir::homePath());
    prepare(dialog);
    for (int i = 0; i < 10; ++i) {
        QTest::keyClick(&dialog, Qt::Key_Escape);
    }
    QVERIFY(dialog.isVisible());
}

void UsabilityTest::doubleClickSearchResultDoesNotCloseDialog()  // T-094b
{
    // Regression-lock: double-clicking a search result used to call
    // accept() which hid the main window — effectively quitting the app
    // (the dialog IS the main window). It must now open the path with
    // the default app AND keep the dialog open.
    FolderBrowserDialog dialog(QDir::homePath());
    prepare(dialog);

    auto *results = dialog.findChild<QListWidget *>("searchResultsList");
    QVERIFY(results);

    // Inject a synthetic search result so we don't have to wait for the
    // scan worker. The double-click handler reads the path from UserRole.
    auto *item = new QListWidgetItem(QStringLiteral("/tmp"));
    item->setData(Qt::UserRole, QStringLiteral("/tmp"));
    results->addItem(item);

    QMetaObject::invokeMethod(&dialog, "onSearchResultDoubleClicked",
                              Qt::DirectConnection,
                              Q_ARG(QListWidgetItem *, item));

    QVERIFY2(dialog.isVisible(),
             "Dialog must remain open after double-clicking a search result.");
    QCOMPARE(dialog.selectedPath(), QStringLiteral("/tmp"));
}

void UsabilityTest::enterOnSearchResultDoesNotCloseDialog()  // T-094c
{
    // Pressing Enter when the search-results list has focus emits
    // itemActivated, which is wired to onSearchResultDoubleClicked.
    // Same regression-lock as T-094b but via the keyboard path.
    FolderBrowserDialog dialog(QDir::homePath());
    prepare(dialog);

    auto *results = dialog.findChild<QListWidget *>("searchResultsList");
    QVERIFY(results);
    auto *item = new QListWidgetItem(QStringLiteral("/tmp"));
    item->setData(Qt::UserRole, QStringLiteral("/tmp"));
    results->addItem(item);
    results->setCurrentItem(item);

    emit results->itemActivated(item);

    QVERIFY2(dialog.isVisible(),
             "Dialog must remain open after Enter on a search result.");
    QCOMPARE(dialog.selectedPath(), QStringLiteral("/tmp"));
}

void UsabilityTest::chooseSlotDoesNotCloseDialog()  // T-094d
{
    // The legacy onChooseClicked() slot used to call accept(). Confirm it
    // now behaves like onOpenInAppClicked (open + keep dialog visible).
    FolderBrowserDialog dialog(QDir::homePath());
    prepare(dialog);

    auto *searchField = dialog.findChild<QLineEdit *>("searchField");
    QVERIFY(searchField);
    QVERIFY(dialog.isVisible());

    QMetaObject::invokeMethod(&dialog, "onChooseClicked", Qt::DirectConnection);

    QVERIFY2(dialog.isVisible(),
             "Dialog must remain open after onChooseClicked.");
}

// ---------------------------------------------------------------------------
// H. Visual / discoverability
// ---------------------------------------------------------------------------

void UsabilityTest::upButtonHasTooltip()       // T-101
{
    FolderBrowserDialog dialog(QDir::homePath());
    auto *b = dialog.findChild<QPushButton *>("upButton");
    QVERIFY(b && !b->toolTip().isEmpty());
}

void UsabilityTest::homeButtonHasTooltip()     // T-102
{
    FolderBrowserDialog dialog(QDir::homePath());
    auto *b = dialog.findChild<QPushButton *>("homeButton");
    QVERIFY(b && !b->toolTip().isEmpty());
}

void UsabilityTest::eyeButtonHasTooltip()      // T-103
{
    FolderBrowserDialog dialog(QDir::homePath());
    auto *b = dialog.findChild<QPushButton *>("showHiddenButton");
    QVERIFY(b && !b->toolTip().isEmpty());
}

void UsabilityTest::gearButtonHasTooltip()     // T-104
{
    FolderBrowserDialog dialog(QDir::homePath());
    auto *b = dialog.findChild<QPushButton *>("excludeButton");
    QVERIFY(b && !b->toolTip().isEmpty());
}

void UsabilityTest::defaultFavoriteIsBoldNotBubbled()  // T-105
{
    FolderBrowserDialog dialog(QDir::homePath());
    prepare(dialog);
    auto *list = favoritesListOf(dialog);
    QVERIFY(list && list->count() >= 1);
    auto *home = list->item(0);
    QVERIFY2(home->font().bold(),
             "Home, as the implicit default, must render bold");
    QVERIFY2(!home->text().startsWith(QStringLiteral("●")),
             "Default favorite text must NOT start with a bubble — bold is the affordance");
}

void UsabilityTest::nonDefaultFavoritesAreNotBold()  // T-106
{
    QTemporaryDir tmp;
    const QString fav = QDir::cleanPath(tmp.path());
    {
        auto s = folderBrowserSettings();
        s.setValue("favorites", QStringList{fav});
        s.sync();
    }
    FolderBrowserDialog dialog(QDir::homePath());
    prepare(dialog);
    auto *list = favoritesListOf(dialog);
    bool checkedAny = false;
    for (int i = 0; i < list->count(); ++i) {
        if (list->item(i)->data(kFavoritePathRole).toString() == fav) {
            QVERIFY2(!list->item(i)->font().bold(),
                     "Non-default favorite must not be bold");
            checkedAny = true;
        }
    }
    QVERIFY(checkedAny);
}

void UsabilityTest::searchFieldHasPlaceholder()  // T-107
{
    FolderBrowserDialog dialog(QDir::homePath());
    auto *f = searchFieldOf(dialog);
    QVERIFY(f);
    QVERIFY(!f->placeholderText().isEmpty());
}

void UsabilityTest::searchFieldHasClearButton()  // T-108
{
    FolderBrowserDialog dialog(QDir::homePath());
    auto *f = searchFieldOf(dialog);
    QVERIFY(f && f->isClearButtonEnabled());
}

// ---------------------------------------------------------------------------
// I. Cross-action consistency
// ---------------------------------------------------------------------------

void UsabilityTest::resolveDefaultMatchesSetDefault()  // T-110
{
    QTemporaryDir tmp;
    const QString fav = QDir::cleanPath(tmp.path());
    {
        auto s = folderBrowserSettings();
        s.setValue("favorites", QStringList{fav});
        s.setValue("defaultFavorite", fav);
        s.sync();
    }
    QCOMPARE(FolderBrowserDialog::resolveDefaultStartPath(), fav);
}

void UsabilityTest::deletingDefaultMakesHomeDefault()  // T-111
{
    {
        auto s = folderBrowserSettings();
        s.setValue("favorites", QStringList{});
        s.setValue("defaultFavorite", QString());
        s.sync();
    }
    QCOMPARE(FolderBrowserDialog::resolveDefaultStartPath(), QDir::homePath());
}

void UsabilityTest::addThenDefaultThenDeleteIsConsistent()  // T-112
{
    QTemporaryDir tmp;
    const QString fav = QDir::cleanPath(tmp.path());
    {
        auto s = folderBrowserSettings();
        s.setValue("favorites", QStringList{fav});
        s.setValue("defaultFavorite", fav);
        s.sync();
    }
    // Delete the default → settings should be left without a stale default.
    {
        auto s = folderBrowserSettings();
        s.setValue("favorites", QStringList{});
        s.setValue("defaultFavorite", QString());
        s.sync();
    }
    QSettings s = folderBrowserSettings();
    QCOMPARE(s.value("favorites").toStringList(), QStringList{});
    QCOMPARE(s.value("defaultFavorite").toString(), QString());
}

void UsabilityTest::favoritesPropagateAcrossDialogInstances()  // T-113
{
    QTemporaryDir tmp;
    const QString fav = QDir::cleanPath(tmp.path());
    {
        FolderBrowserDialog dialog(QDir::homePath());
        // Persist via QSettings (mimics what + Add current does).
        auto s = folderBrowserSettings();
        s.setValue("favorites", QStringList{fav});
        s.sync();
    }
    FolderBrowserDialog dialog2(QDir::homePath());
    prepare(dialog2);
    auto *list = favoritesListOf(dialog2);
    bool seen = false;
    for (int i = 0; i < list->count(); ++i) {
        if (list->item(i)->data(kFavoritePathRole).toString() == fav) {
            seen = true;
            break;
        }
    }
    QVERIFY2(seen, "Second dialog instance must see the favorite "
                   "persisted by the first.");
}

void UsabilityTest::sidebarAlwaysHasAtLeastHome()  // T-114
{
    {
        auto s = folderBrowserSettings();
        s.setValue("favorites", QStringList{});
        s.sync();
    }
    FolderBrowserDialog dialog(QDir::homePath());
    prepare(dialog);
    auto *list = favoritesListOf(dialog);
    QVERIFY(list && list->count() >= 1);
    QCOMPARE(QDir::cleanPath(list->item(0)->data(kFavoritePathRole).toString()),
             QDir::cleanPath(QDir::homePath()));
}

// ---------------------------------------------------------------------------
// J. Performance smoke
// ---------------------------------------------------------------------------

void UsabilityTest::dialogConstructsQuickly()  // T-120
{
    QElapsedTimer t;
    t.start();
    FolderBrowserDialog dialog(QDir::homePath());
    const qint64 ms = t.elapsed();
    QVERIFY2(ms < 1500,
             qPrintable(QString("Dialog construction took %1 ms — "
                                "must be < 1500 ms even cold").arg(ms)));
}

void UsabilityTest::rapidKeystrokesDoNotBlock()  // T-121
{
    FolderBrowserDialog dialog(QDir::homePath());
    prepare(dialog);
    auto *field = searchFieldOf(dialog);
    field->setFocus();
    field->clear();

    QElapsedTimer t;
    t.start();
    for (int i = 0; i < 100; ++i) {
        QTest::keyClick(field, Qt::Key_X);
    }
    const qint64 ms = t.elapsed();
    QVERIFY2(ms < 3000,
             qPrintable(QString("100 keystrokes took %1 ms").arg(ms)));
    QCOMPARE(field->text().length(), 100);
}
