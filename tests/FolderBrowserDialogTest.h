#ifndef FOLDERBROWSERDIALOGTEST_H
#define FOLDERBROWSERDIALOGTEST_H

#include <QObject>

class FolderBrowserDialogTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void testConstructsWithoutCrash();
    void testHasOpenInFinderAndOpenInAppButtons();
    void testFavoritesRowStartsWithJustHomeAndPlus();
    void testFavoritePersistsAcrossInstances();
    void testRemoveFavoritePersistsRemoval();
    void testHomeFavoriteAlwaysPresent();
    void testNonexistentFavoriteIsHiddenAtRender();
};

#endif // FOLDERBROWSERDIALOGTEST_H
