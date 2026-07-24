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

    // Cloud placeholder handling (2026-07-24): size + online-only detection
    // via one lstat, and the "Downloading X — may take some seconds…"
    // announcement when opening a locally-missing file.
    void testCloudFileStateDetection();
    void testFormatFileSize();
    void testOpeningCloudPlaceholderAnnouncesDownload();
    void testHasOpenInFinderAndOpenInAppButtons();
    void testFavoritesRowStartsWithJustHomeAndPlus();
    void testFavoritePersistsAcrossInstances();
    void testRemoveFavoritePersistsRemoval();
    void testHomeFavoriteAlwaysPresent();
    void testNonexistentFavoriteIsHiddenAtRender();
};

#endif // FOLDERBROWSERDIALOGTEST_H
