#pragma once

#include <QObject>

/// Global system-wide hotkey to summon the macos-search app.
///
/// Default chord: **⌃⌥⇧S** (Control + Option + Shift + S; mnemonic "Search").
///
/// Implementation uses Carbon's `RegisterEventHotKey` — despite the name,
/// Carbon is NOT deprecated and does NOT require macOS Accessibility
/// permission (unlike `NSEvent addGlobalMonitorForEventsMatchingMask:`).
/// Same API used by Alfred, Raycast, Sublime Text, etc.
///
/// Test seams (`GlobalHotkey::Testing`) let unit tests exercise the
/// signal-emission contract without grabbing the user's actual chord.
class GlobalHotkey : public QObject
{
    Q_OBJECT
public:
    explicit GlobalHotkey(QObject *parent = nullptr);
    ~GlobalHotkey() override;

    /// Register the summon chord with the system. Returns true on success.
    /// Returns false if another app has already grabbed the chord
    /// (`eventHotKeyExistsErr`) — caller should NOT block startup on that.
    /// Dry-run mode returns true and emits a "would-have-registered"
    /// signal but does not touch Carbon.
    bool registerSummonChord();

    /// Unregister the previously-registered chord. Safe to call when not
    /// registered (no-op).
    void unregisterSummonChord();

    bool isRegistered() const { return m_registered; }

    /// Test-only — emit the summon signal directly without going through
    /// Carbon. Used by unit tests to verify the receiver side.
    void emitSummonForTesting();

    /// Test seams.
    struct Testing {
        static void setDryRun(bool dryRun);
        static bool dryRun();
        static bool lastRegisterAttempted();
        static bool lastUnregisterAttempted();
        static void resetCallTracking();
    };

signals:
    void summonRequested();

private:
    void *m_hotKeyRef = nullptr;
    void *m_handlerRef = nullptr;
    bool m_registered = false;
};
