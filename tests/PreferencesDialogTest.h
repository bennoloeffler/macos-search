#pragma once

#include <QObject>

class PreferencesDialogTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void hasAllThreeCheckboxes();
    void hasEditExcludesButton();

    void autostartCheckboxInitiallyReflectsQSettings();
    void hotkeyCheckboxInitiallyReflectsQSettings();
    void showHiddenCheckboxInitiallyReflectsQSettings();

    void togglingAutostartPersists();
    void togglingHotkeyPersistsAndDispatches();
    void togglingShowHiddenPersistsAndEmits();

    void closeButtonAccepts();
    void gearOnMainDialogOpensPreferencesNotExclude();
};
