#pragma once

#include <QObject>

class AutostartTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // Prod detection
    void prodOverrideIsRespected();
    void devModeForBuildDir();

    // First-run prompt gating
    void firstRunPromptHiddenInDevMode();
    void firstRunPromptShownInProdWithFreshSettings();
    void firstRunPromptShowsOnceOnly();

    // Choice application
    void firstRunYesPersistsAutostartAndCompletes();
    void firstRunSkipPersistsCompletedOnly();
    void setEnabledIsNoOpAtOsLayerWhenDev();

    // Dialog
    void dialogDefaultButtonIsEnable();
    void dialogClickEnableAccepts();
    void dialogClickSkipRejects();
    void dialogEscapeRejects();
    void dialogFocusOnEnableButton();
};
