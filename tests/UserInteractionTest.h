#ifndef USERINTERACTIONTEST_H
#define USERINTERACTIONTEST_H

#include <QObject>

// Hard, user-perspective tests for the FolderBrowserDialog.
//
// These simulate keystrokes and clicks via QTest and assert the resulting
// state from a user's point of view: "did typing 'abc' put 'abc' in the
// field?", "did Esc close the dialog?" (it must NOT), "does ⌘L move focus
// to the path field?" etc.
class UserInteractionTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();          // runs before each test — reset persisted state

    // --- Keyboard: typing into the dialog ---
    void typingAppendsCharactersInsteadOfReplacing();
    void typingFromTreeViewLandsInSearchField();
    void typingDoesNotEatModifierChords();

    // --- Keyboard: navigation ---
    void arrowDownFromSearchFieldNavigatesResults();
    void arrowUpFromSearchFieldNavigatesResults();
    void pageDownFromSearchFieldScrollsTree();
    void arrowOnEmptyResultsListDoesNotRecurseAndCrash();

    // --- Keyboard: dispatch chords ---
    void cmdLFocusesPathField();
    void cmdFFocusesSearchField();
    void cmdHJumpsToHome();
    void cmdUpGoesToParent();
    void escapeClearsSearchButDoesNotCloseDialog();
    void escapeOnEmptySearchDoesNothingAndDoesNotCloseDialog();
    void enterTriggersOpenWithAppAction();
    void cmdEnterTriggersOpenInFinderAction();

    // --- Mouse: favorites sidebar ---
    void clickFavoriteSwitchesRootPath();
    void clickPlusAddCurrentAddsAndPersistsFavorite();
    void clickPlusAddCurrentIgnoresHomePath();
    void clickPlusAddCurrentIgnoresDuplicate();

    // --- Mouse: open buttons ---
    void openInFinderButtonInvokesReveal();
    void openInAppButtonInvokesOpen();

    // --- Visible help line is always present ---
    void shortcutsHintIsVisibleAndMentionsMainChords();

    // --- Window does not close on ⌘W / system close attempts mistakenly ---
    void favoritesListReceivesArrowKeysWhenFocused();
};

#endif // USERINTERACTIONTEST_H
