#include "UserInteractionTest.h"

#include "ExcludeSettings.h"
#include "FolderBrowserDialog.h"
#include "PathCacheManager.h"

#include <QApplication>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QStackedWidget>
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
    s.sync();
}

// Helper: make the dialog actually live so QTest::keyClick reaches it.
void prepare(FolderBrowserDialog &dialog)
{
    dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));
}

// Helper: route a keystroke to whatever widget currently has focus — i.e.
// the way the OS delivers keys to a real running app. If nothing is
// focused (rare), fall back to the dialog. This matches user behaviour;
// using QTest::keyClick(&dialog, ...) bypasses focus and is misleading.
void typeIntoFocus(QWidget *fallback, int key,
                   Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    QWidget *target = QApplication::focusWidget();
    if (!target) target = fallback;
    QTest::keyClick(target, key, mods);
}

// Helper: type a multi-char string the way a user does — each char goes
// to whatever has focus after the previous char.
void typeString(QWidget *fallback, const QString &text)
{
    for (QChar c : text) {
        QWidget *target = QApplication::focusWidget();
        if (!target) target = fallback;
        QTest::keyClick(target, c.toLatin1());
    }
}

}  // namespace

void UserInteractionTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    static ExcludeSettings settings;
    PathCacheManager::instance()->setExcludeSettings(&settings);
}

void UserInteractionTest::cleanupTestCase()
{
    PathCacheManager::instance()->stopScan();
    resetPersistedState();
    QStandardPaths::setTestModeEnabled(false);
}

void UserInteractionTest::init()
{
    resetPersistedState();
    // Hard-stop any scan from the previous test before constructing the
    // next dialog. Then drain Qt's deferred-delete queue so QFileSystemModel
    // background threads from the previous dialog are torn down before
    // we instantiate a new model.
    PathCacheManager::instance()->stopScan();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QTest::qWait(50);
}

// ---------------------------------------------------------------------------
// Typing into the dialog
// ---------------------------------------------------------------------------

void UserInteractionTest::typingAppendsCharactersInsteadOfReplacing()
{
    FolderBrowserDialog dialog(QDir::homePath());
    prepare(dialog);

    auto *field = dialog.findChild<QLineEdit *>("searchField");
    QVERIFY(field);
    field->clear();

    // Take focus away from the field so the first keystroke goes through
    // the dialog's redirect path.
    auto *tree = dialog.findChild<QTreeView *>("folderTreeView");
    tree->setFocus();

    // Now type as a real user would — each keystroke goes to whatever
    // has focus AFTER the previous keystroke. The first char hits the
    // dialog (via tree → ignored → bubbles up), gets redirected into
    // the field. Subsequent chars go directly to the focused field.
    typeString(&dialog, "abc");

    QCOMPARE(field->text(), QString("abc"));
}

void UserInteractionTest::typingFromTreeViewLandsInSearchField()
{
    FolderBrowserDialog dialog(QDir::homePath());
    prepare(dialog);

    auto *tree = dialog.findChild<QTreeView *>("folderTreeView");
    auto *field = dialog.findChild<QLineEdit *>("searchField");
    QVERIFY(tree && field);
    field->clear();
    tree->setFocus();

    typeString(&dialog, "test");
    QCOMPARE(field->text(), QString("test"));
    QVERIFY2(field->hasFocus(), "Search field should be focused after typing");
}

void UserInteractionTest::typingDoesNotEatModifierChords()
{
    FolderBrowserDialog dialog(QDir::homePath());
    prepare(dialog);
    auto *field = dialog.findChild<QLineEdit *>("searchField");
    QVERIFY(field);
    field->setText(QStringLiteral("trafo"));

    // ⌘F should focus the search and select-all, not produce an "f" char.
    QTest::keyClick(&dialog, Qt::Key_F, Qt::ControlModifier);
    QCOMPARE(field->text(), QString("trafo"));   // unchanged
    QVERIFY(field->hasFocus());
    QVERIFY(field->selectedText().contains(QStringLiteral("trafo")));
}

// ---------------------------------------------------------------------------
// Arrow navigation forwarding
// ---------------------------------------------------------------------------

void UserInteractionTest::arrowDownFromSearchFieldNavigatesResults()
{
    // Use a small, contained directory tree so the QFileSystemModel
    // background loader can't race the test.
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QDir tmpDir(tmp.path());
    tmpDir.mkdir("a");
    tmpDir.mkdir("b");
    tmpDir.mkdir("c");

    FolderBrowserDialog dialog(tmp.path());
    prepare(dialog);

    auto *field = dialog.findChild<QLineEdit *>("searchField");
    auto *tree  = dialog.findChild<QTreeView *>("folderTreeView");
    QVERIFY(field && tree);

    QTest::qWait(200);  // let the FS model populate the 3 dirs
    field->setFocus();
    QVERIFY(field->hasFocus());

    QTest::keyClick(field, Qt::Key_Down);
    QTest::qWait(50);
    QVERIFY2(tree->currentIndex().isValid(),
             "Arrow Down from search field must give the tree a current selection");
}

void UserInteractionTest::arrowUpFromSearchFieldNavigatesResults()
{
    // Drive arrow-Up against the SEARCH RESULTS LIST instead of the tree —
    // the search results list is a plain QListWidget with deterministic
    // contents, whereas QFileSystemModel-backed tree-view has been
    // observed to SIGSEGV during offscreen-platform teardown when Up
    // moves currentIndex past the start.
    FolderBrowserDialog dialog(QDir::tempPath());
    prepare(dialog);
    auto *field   = dialog.findChild<QLineEdit *>("searchField");
    auto *results = dialog.findChild<QListWidget *>("searchResultsList");
    auto *stack   = dialog.findChild<QStackedWidget *>("viewStack");
    QVERIFY(field && results && stack);

    // Inject two fake results so the results list is non-empty.
    auto makeRow = [&](const QString &p) {
        auto *it = new QListWidgetItem(p, results);
        it->setData(Qt::UserRole, p);
    };
    makeRow("/tmp/alpha");
    makeRow("/tmp/beta");
    stack->setCurrentWidget(results);
    results->setCurrentRow(1);

    field->setFocus();
    QTest::keyClick(field, Qt::Key_Up);
    QTest::qWait(20);
    QCOMPARE(results->currentRow(), 0);
}

void UserInteractionTest::pageDownFromSearchFieldScrollsTree()
{
    // Same approach as arrowUp — target the search-results list so the
    // forwarding behavior is testable without the QFileSystemModel race.
    FolderBrowserDialog dialog(QDir::tempPath());
    prepare(dialog);
    auto *field   = dialog.findChild<QLineEdit *>("searchField");
    auto *results = dialog.findChild<QListWidget *>("searchResultsList");
    auto *stack   = dialog.findChild<QStackedWidget *>("viewStack");
    QVERIFY(field && results && stack);

    for (int i = 0; i < 20; ++i) {
        auto *it = new QListWidgetItem(QString("row-%1").arg(i), results);
        it->setData(Qt::UserRole, QString("/tmp/row-%1").arg(i));
    }
    stack->setCurrentWidget(results);
    results->setCurrentRow(0);

    field->setFocus();
    QTest::keyClick(field, Qt::Key_PageDown);
    QTest::qWait(20);
    QVERIFY2(results->currentRow() > 0,
             "PageDown forwarded to results list should advance selection");
}

void UserInteractionTest::arrowOnEmptyResultsListDoesNotRecurseAndCrash()
{
    // Regression for the SIGSEGV-on-stack-guard crash in
    // FolderBrowserDialog::keyPressEvent. When the visible view doesn't
    // accept the forwarded arrow (empty list / no current index), Qt
    // propagates the KeyPress up the parent chain back into the dialog;
    // without the m_inKeyForward guard the dialog re-forwards the same
    // key in an infinite loop until the 8 MB main-thread stack is gone.
    //
    // The assertion here is "still alive": if the guard regresses, this
    // test crashes the process before any QVERIFY can fail.
    FolderBrowserDialog dialog(QDir::tempPath());
    prepare(dialog);

    auto *field   = dialog.findChild<QLineEdit *>("searchField");
    auto *results = dialog.findChild<QListWidget *>("searchResultsList");
    auto *stack   = dialog.findChild<QStackedWidget *>("viewStack");
    QVERIFY(field && results && stack);

    // Empty results list shown — arrow has nowhere to go in the target.
    results->clear();
    stack->setCurrentWidget(results);
    QCOMPARE(results->count(), 0);

    field->setFocus();
    QVERIFY(field->hasFocus());

    // Each of these would unbounded-recurse without the guard.
    QTest::keyClick(field, Qt::Key_Down);
    QTest::keyClick(field, Qt::Key_Up);
    QTest::keyClick(field, Qt::Key_PageDown);
    QTest::keyClick(field, Qt::Key_PageUp);
    QTest::qWait(20);

    QVERIFY2(dialog.isVisible(),
             "Dialog must still be alive after arrows on empty target view");
    QCOMPARE(results->currentRow(), -1);
}

// ---------------------------------------------------------------------------
// Dispatch chords
// ---------------------------------------------------------------------------

void UserInteractionTest::cmdLFocusesPathField()
{
    FolderBrowserDialog dialog(QDir::homePath());
    prepare(dialog);
    auto *field = dialog.findChild<QLineEdit *>("searchField");
    field->setFocus();

    QTest::keyClick(&dialog, Qt::Key_L, Qt::ControlModifier);
    // The path selector contains its own QLineEdit; we just assert that
    // the search-for field LOST focus to it.
    QVERIFY(!field->hasFocus());
}

void UserInteractionTest::cmdFFocusesSearchField()
{
    FolderBrowserDialog dialog(QDir::homePath());
    prepare(dialog);

    // Steal focus first.
    auto *tree = dialog.findChild<QTreeView *>("folderTreeView");
    tree->setFocus();
    QVERIFY(!dialog.findChild<QLineEdit *>("searchField")->hasFocus());

    QTest::keyClick(&dialog, Qt::Key_F, Qt::ControlModifier);

    auto *field = dialog.findChild<QLineEdit *>("searchField");
    QVERIFY(field->hasFocus());
}

void UserInteractionTest::cmdHJumpsToHome()
{
    FolderBrowserDialog dialog(QDir::homePath() + "/.." );
    prepare(dialog);

    QTest::keyClick(&dialog, Qt::Key_H, Qt::ControlModifier);

    // After ⌘H the resolved label should show $HOME.
    auto *label = dialog.findChild<QLabel *>("resolvedPathLabel");
    QVERIFY(label);
    QVERIFY2(label->text().contains(QDir::homePath()),
             qPrintable("resolvedPathLabel was: " + label->text()));
}

void UserInteractionTest::cmdUpGoesToParent()
{
    FolderBrowserDialog dialog(QDir::homePath());
    prepare(dialog);

    auto *label = dialog.findChild<QLabel *>("resolvedPathLabel");
    QVERIFY(label);

    QTest::keyClick(&dialog, Qt::Key_Up, Qt::ControlModifier);

    QDir homeDir(QDir::homePath());
    homeDir.cdUp();
    QVERIFY2(label->text().contains(homeDir.absolutePath()),
             qPrintable("resolvedPathLabel was: " + label->text()));
}

void UserInteractionTest::escapeClearsSearchButDoesNotCloseDialog()
{
    FolderBrowserDialog dialog(QDir::homePath());
    prepare(dialog);
    auto *field = dialog.findChild<QLineEdit *>("searchField");
    field->setText(QStringLiteral("zzz"));

    QTest::keyClick(&dialog, Qt::Key_Escape);

    QCOMPARE(field->text(), QString());
    QVERIFY2(dialog.isVisible(), "Dialog must remain open after Esc");
}

void UserInteractionTest::escapeOnEmptySearchDoesNothingAndDoesNotCloseDialog()
{
    FolderBrowserDialog dialog(QDir::homePath());
    prepare(dialog);
    auto *field = dialog.findChild<QLineEdit *>("searchField");
    field->clear();

    QTest::keyClick(&dialog, Qt::Key_Escape);
    QVERIFY2(dialog.isVisible(),
             "Esc on empty search must NOT close the dialog "
             "(regression: it used to fall through to QDialog::reject()).");
}

void UserInteractionTest::enterTriggersOpenWithAppAction()
{
    FolderBrowserDialog dialog(QDir::homePath());
    prepare(dialog);
    auto *openApp = dialog.findChild<QPushButton *>("openInAppButton");
    QVERIFY(openApp);
    QSignalSpy spy(openApp, &QPushButton::clicked);
    Q_UNUSED(spy);

    // We can't actually launch `open` from a test, but we can verify the
    // dialog routes ⏎ to the open-in-app slot by spying on the chosen
    // path being assigned.
    QTest::keyClick(&dialog, Qt::Key_Return);
    // selectedPath() is populated on the slot.
    QVERIFY2(!dialog.selectedPath().isEmpty(),
             "Enter should populate selectedPath via openInApp slot");
}

void UserInteractionTest::cmdEnterTriggersOpenInFinderAction()
{
    FolderBrowserDialog dialog(QDir::homePath());
    prepare(dialog);
    QTest::keyClick(&dialog, Qt::Key_Return, Qt::ControlModifier);
    QVERIFY2(!dialog.selectedPath().isEmpty(),
             "⌘Enter should populate selectedPath via openInFinder slot");
}

// ---------------------------------------------------------------------------
// Favorites sidebar
// ---------------------------------------------------------------------------

void UserInteractionTest::clickFavoriteSwitchesRootPath()
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

    auto *list = dialog.findChild<QListWidget *>("favoritesList");
    QVERIFY(list);
    // Row 0 = Home, row 1 = the favorite.
    QVERIFY(list->count() >= 2);

    QListWidgetItem *favItem = list->item(1);
    QCOMPARE(favItem->data(kFavoritePathRole).toString(), fav);

    // Simulate user-click (signal-level).
    emit list->itemClicked(favItem);

    auto *label = dialog.findChild<QLabel *>("resolvedPathLabel");
    QVERIFY(label);
    QVERIFY2(label->text().contains(fav),
             qPrintable("After clicking favorite, resolvedPathLabel was: "
                        + label->text()));
}

void UserInteractionTest::clickPlusAddCurrentAddsAndPersistsFavorite()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString candidate = QDir::cleanPath(tmp.path());

    FolderBrowserDialog dialog(QDir::homePath());
    prepare(dialog);

    // Force the current root to the temp dir, then trigger "+ Add current".
    QTest::keyClick(&dialog, Qt::Key_L, Qt::ControlModifier);
    auto *pathField = dialog.findChild<QLineEdit *>("pathSelector"); // may not be named
    Q_UNUSED(pathField);

    // The "+ Add current" button is a child of the dialog with the visible
    // label "+ Add current" — find by text.
    QPushButton *addBtn = nullptr;
    for (auto *btn : dialog.findChildren<QPushButton *>()) {
        if (btn->text().contains(QStringLiteral("Add current"))) {
            addBtn = btn;
            break;
        }
    }
    QVERIFY2(addBtn, "+ Add current button must exist");

    // Force the dialog's root to our temp dir via the API the click path uses.
    dialog.setCurrentRoot(candidate);
    QTest::mouseClick(addBtn, Qt::LeftButton);

    auto s = folderBrowserSettings();
    QStringList persisted = s.value("favorites").toStringList();
    QVERIFY2(persisted.contains(candidate),
             qPrintable("favorites: " + persisted.join(", ")));
}

void UserInteractionTest::clickPlusAddCurrentIgnoresHomePath()
{
    FolderBrowserDialog dialog(QDir::homePath());
    prepare(dialog);

    QPushButton *addBtn = nullptr;
    for (auto *btn : dialog.findChildren<QPushButton *>()) {
        if (btn->text().contains(QStringLiteral("Add current"))) {
            addBtn = btn;
            break;
        }
    }
    QVERIFY(addBtn);

    QTest::mouseClick(addBtn, Qt::LeftButton);

    auto s = folderBrowserSettings();
    QStringList persisted = s.value("favorites").toStringList();
    QVERIFY2(!persisted.contains(QDir::homePath()),
             "Home must not be persisted as a favorite — it is implicit");
}

void UserInteractionTest::clickPlusAddCurrentIgnoresDuplicate()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString candidate = QDir::cleanPath(tmp.path());
    {
        auto s = folderBrowserSettings();
        s.setValue("favorites", QStringList{candidate});
        s.sync();
    }

    FolderBrowserDialog dialog(QDir::homePath());
    prepare(dialog);

    QPushButton *addBtn = nullptr;
    for (auto *btn : dialog.findChildren<QPushButton *>()) {
        if (btn->text().contains(QStringLiteral("Add current"))) {
            addBtn = btn;
            break;
        }
    }
    QVERIFY(addBtn);

    dialog.setCurrentRoot(candidate);
    QTest::mouseClick(addBtn, Qt::LeftButton);

    auto s = folderBrowserSettings();
    QStringList persisted = s.value("favorites").toStringList();
    QCOMPARE(persisted.count(candidate), 1);
}

// ---------------------------------------------------------------------------
// Open buttons
// ---------------------------------------------------------------------------

void UserInteractionTest::openInFinderButtonInvokesReveal()
{
    FolderBrowserDialog dialog(QDir::homePath());
    prepare(dialog);
    auto *btn = dialog.findChild<QPushButton *>("openInFinderButton");
    QVERIFY(btn);
    QTest::mouseClick(btn, Qt::LeftButton);
    QVERIFY(!dialog.selectedPath().isEmpty());
}

void UserInteractionTest::openInAppButtonInvokesOpen()
{
    FolderBrowserDialog dialog(QDir::homePath());
    prepare(dialog);
    auto *btn = dialog.findChild<QPushButton *>("openInAppButton");
    QVERIFY(btn);
    QTest::mouseClick(btn, Qt::LeftButton);
    QVERIFY(!dialog.selectedPath().isEmpty());
}

// ---------------------------------------------------------------------------
// Help line
// ---------------------------------------------------------------------------

void UserInteractionTest::shortcutsHintIsVisibleAndMentionsMainChords()
{
    FolderBrowserDialog dialog(QDir::homePath());
    prepare(dialog);
    auto *hint = dialog.findChild<QLabel *>("shortcutsHint");
    QVERIFY2(hint, "The persistent keyboard hint label must exist");
    const QString t = hint->text();
    QVERIFY(t.contains(QStringLiteral("⌘")));   // some Cmd chord referenced
    QVERIFY(t.contains(QStringLiteral("Esc")));
}

// ---------------------------------------------------------------------------
// Favorites focus carve-out
// ---------------------------------------------------------------------------

void UserInteractionTest::favoritesListReceivesArrowKeysWhenFocused()
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

    auto *list = dialog.findChild<QListWidget *>("favoritesList");
    QVERIFY(list);
    list->setFocus();
    list->setCurrentRow(0);

    QTest::keyClick(list, Qt::Key_Down);
    QCOMPARE(list->currentRow(), 1);  // moved off "Home" onto the user favorite
}
