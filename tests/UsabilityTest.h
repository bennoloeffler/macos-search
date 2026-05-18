#ifndef USABILITYTEST_H
#define USABILITYTEST_H

#include <QObject>

// Comprehensive usability tests — see docs/150_usability_tests.md for
// the catalog. Test names track the catalog's T-XXX IDs.
//
// Convention: this class focuses on UX invariants and discoverability,
// not crash-regressions (those live in UserInteractionTest).
class UsabilityTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();

    // A. Focus & traversal
    void initialFocusIsSearchField();                        // T-001
    void cmdFAlwaysLandsOnSearchField();                     // T-007
    void cmdLAlwaysLandsOnPathField();                       // T-008
    void escClearKeepsFocusOnSearchField();                  // T-009

    // B. Keystroke dispatch — global chords
    void cmdF_focusesAndSelectsAll();                        // T-020
    void cmdShiftG_focusesPathField();                       // T-022
    void cmdUp_atRootIsNoOp();                               // T-025
    void escEmptyDoesNotCloseDialog();                       // T-029
    void cmdQ_doesNotCrash();                                // T-031

    // C. Typing
    void multiWordQueryTyped();                              // T-042
    void cmdFThenTypingReplacesExisting();                   // T-044
    void slashTypedInSearchAppendsLiterally();               // T-045

    // D. Arrow navigation
    void homeEndInSearchFieldNotIntercepted();               // T-055

    // E. Mouse
    void upButtonGoesToParent();                             // T-064
    void homeButtonJumpsHome();                              // T-066
    void eyeToggleFlipsAndPersists();                        // T-067
    void eyeToggleDoesNotRescan();                           // T-067b (TODO 4 regression-lock)
    void eyeToggleHidesHiddenSearchResults();                // T-067c
    void singleClickTreeRowSetsScope();                      // T-068
    // (right-click menu structure — covered by makeDefaultPersists below)
    void makeDefaultPersists();                              // T-075
    void deleteRowPersists();                                // T-076
    void deletingDefaultFallsBackToHome();                   // T-077

    // F. View-stack & state transitions
    void emptyQueryShowsTree();                              // T-080
    void nonEmptyQueryShowsResults();                        // T-081
    void clearQueryReturnsToTree();                          // T-082
    void willOpenReflectsSelection();                        // T-085

    // G. Suppression
    void repeatedEscNeverCloses();                           // T-094

    // H. Visual / discoverability
    void upButtonHasTooltip();                               // T-101
    void homeButtonHasTooltip();                             // T-102
    void eyeButtonHasTooltip();                              // T-103
    void gearButtonHasTooltip();                             // T-104
    void defaultFavoriteIsBoldNotBubbled();                  // T-105
    void nonDefaultFavoritesAreNotBold();                    // T-106
    void searchFieldHasPlaceholder();                        // T-107
    void searchFieldHasClearButton();                        // T-108

    // I. Cross-action consistency
    void resolveDefaultMatchesSetDefault();                  // T-110
    void deletingDefaultMakesHomeDefault();                  // T-111
    void addThenDefaultThenDeleteIsConsistent();             // T-112
    void favoritesPropagateAcrossDialogInstances();          // T-113
    void sidebarAlwaysHasAtLeastHome();                      // T-114

    // J. Performance smoke
    void dialogConstructsQuickly();                          // T-120
    void rapidKeystrokesDoNotBlock();                        // T-121
};

#endif // USABILITYTEST_H
