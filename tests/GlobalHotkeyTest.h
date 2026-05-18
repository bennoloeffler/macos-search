#pragma once

#include <QObject>

class GlobalHotkeyTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void dryRunRegisterReturnsTrueWithoutCarbonCall();
    void dryRunUnregisterClearsRegisteredFlag();
    void registerIsIdempotent();
    void summonSignalEmittedManually();
    void summonInvokesDialogFocusAndSelectAll();
    void shortcutsHintContainsSummonChord();
};
