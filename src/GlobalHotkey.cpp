#include "GlobalHotkey.h"

#include <Carbon/Carbon.h>

#include <QDebug>

namespace {

constexpr UInt32 kHotKeyId = 0xA1B2C3D4;
constexpr FourCharCode kHotKeySignature = 'maus';  // m-a-u-s

bool g_dryRun = false;
bool g_lastRegisterAttempted = false;
bool g_lastUnregisterAttempted = false;

OSStatus carbonHotKeyHandler(EventHandlerCallRef /*nextHandler*/,
                             EventRef event,
                             void *userData)
{
    EventHotKeyID hkID;
    OSStatus status = GetEventParameter(event,
                                        kEventParamDirectObject,
                                        typeEventHotKeyID,
                                        nullptr,
                                        sizeof(hkID),
                                        nullptr,
                                        &hkID);
    if (status != noErr) return status;

    if (hkID.signature == kHotKeySignature && hkID.id == kHotKeyId) {
        auto *self = static_cast<GlobalHotkey *>(userData);
        QMetaObject::invokeMethod(self,
                                  [self]() { emit self->summonRequested(); },
                                  Qt::QueuedConnection);
    }
    return noErr;
}

}  // namespace

GlobalHotkey::GlobalHotkey(QObject *parent) : QObject(parent) {}

GlobalHotkey::~GlobalHotkey()
{
    unregisterSummonChord();
}

bool GlobalHotkey::registerSummonChord()
{
    g_lastRegisterAttempted = true;

    if (m_registered) return true;

    if (g_dryRun) {
        m_registered = true;
        return true;
    }

    EventHotKeyID hkID;
    hkID.signature = kHotKeySignature;
    hkID.id = kHotKeyId;

    EventHotKeyRef ref = nullptr;
    const UInt32 modifiers = controlKey | optionKey | shiftKey;
    const UInt32 keyCode = kVK_ANSI_S;

    OSStatus status = RegisterEventHotKey(keyCode,
                                          modifiers,
                                          hkID,
                                          GetApplicationEventTarget(),
                                          0,
                                          &ref);
    if (status != noErr) {
        qWarning() << "GlobalHotkey: RegisterEventHotKey failed, status =" << status;
        return false;
    }

    m_hotKeyRef = ref;

    EventTypeSpec eventType = { kEventClassKeyboard, kEventHotKeyPressed };
    EventHandlerRef handler = nullptr;
    status = InstallApplicationEventHandler(&carbonHotKeyHandler,
                                            1,
                                            &eventType,
                                            this,
                                            &handler);
    if (status != noErr) {
        qWarning() << "GlobalHotkey: InstallApplicationEventHandler failed, status =" << status;
        UnregisterEventHotKey(ref);
        m_hotKeyRef = nullptr;
        return false;
    }

    m_handlerRef = handler;
    m_registered = true;
    return true;
}

void GlobalHotkey::unregisterSummonChord()
{
    g_lastUnregisterAttempted = true;

    if (!m_registered) return;

    if (!g_dryRun) {
        if (m_handlerRef) {
            RemoveEventHandler(static_cast<EventHandlerRef>(m_handlerRef));
            m_handlerRef = nullptr;
        }
        if (m_hotKeyRef) {
            UnregisterEventHotKey(static_cast<EventHotKeyRef>(m_hotKeyRef));
            m_hotKeyRef = nullptr;
        }
    }
    m_registered = false;
}

void GlobalHotkey::emitSummonForTesting()
{
    emit summonRequested();
}

void GlobalHotkey::Testing::setDryRun(bool dryRun) { g_dryRun = dryRun; }
bool GlobalHotkey::Testing::dryRun() { return g_dryRun; }
bool GlobalHotkey::Testing::lastRegisterAttempted() { return g_lastRegisterAttempted; }
bool GlobalHotkey::Testing::lastUnregisterAttempted() { return g_lastUnregisterAttempted; }
void GlobalHotkey::Testing::resetCallTracking()
{
    g_lastRegisterAttempted = false;
    g_lastUnregisterAttempted = false;
}
