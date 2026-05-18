# Proposed Features — Autostart & Global Hotkey

Two features that aren't built yet but are tracked here so the design
constraints don't get lost. Either is a few hours' work; both should ask
the user before being silently enabled.

---

## 1) Autostart so the cache is warm

### Why

The cache takes seconds to seconds-of-tens to build, depending on
`$HOME` size. If the app isn't running, the user's first search is
slower than every subsequent one — exactly the moment we'd want it
fastest. If the app is launched at login, the cache is built while
the user is doing other things, and the first interactive search is
instantaneous.

### What to ship

A **one-time prompt** on first launch:

> "Start macos-search automatically when you log in?
>  This keeps the file index ready so searches are instant."
>
>  [No] [Yes]

Default: **No**. Persist the answer in `QSettings`. Add a Preferences
toggle to flip it later.

### How to implement (two flavors)

Pick one based on minimum macOS support — see `070_build_and_ship.md`.

**(a) LaunchAgent** (works on every macOS we'd care about):

Write a `.plist` to `~/Library/LaunchAgents/de.v-und-s.macos-search.plist`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" ...>
<plist version="1.0">
<dict>
    <key>Label</key>             <string>de.v-und-s.macos-search</string>
    <key>ProgramArguments</key>  <array>
        <string>/Applications/macos-search.app/Contents/MacOS/macos-search</string>
        <string>--background</string>
    </array>
    <key>RunAtLoad</key>         <true/>
    <key>KeepAlive</key>         <false/>
    <key>ProcessType</key>       <string>Interactive</string>
</dict>
</plist>
```

Then `launchctl load -w <plist>` once. To uninstall, `launchctl unload -w` and
delete the file.

The app needs a `--background` flag that hides the window on launch
and registers the menu-bar / Dock-icon-only flow. Without it, the
user sees a window pop up at login, which is annoying.

**(b) `SMAppService`** (macOS 13 Ventura+):

The modern Apple-blessed API. From C++/Qt:

```cpp
#import <ServiceManagement/ServiceManagement.h>
// SMAppService *svc = [SMAppService mainAppService];
// [svc registerAndReturnError:&err];
```

Trade-off: less plist-juggling, but breaks on macOS 12. Decision is gated on
`090_open_questions.md` item #2 (minimum macOS version).

### Risks / things to get right

- **Don't enable silently.** macOS users are allergic to "this app
  added itself to login items without asking" behavior.
- **Provide a clear "disable" path** — both in our Preferences and via
  System Settings → General → Login Items (where macOS lists it).
- The background-launched instance should not steal focus when the
  user later runs the foreground app. Use `--background` to mean
  "scan only, don't show the window unless explicitly summoned"; the
  hotkey (below) becomes the way to summon it.

### Open question for Benno

> Do you want the app to ask about autostart on first launch?
> If yes, default to *No* (opt-in) or *Yes* (opt-out)?

---

## 2) Global hotkey ⌃⌥⇧S

### Why

Right now the only way to bring the window forward is to click the
Dock icon or `Cmd-Tab` to it. A global hotkey makes summon-to-search
as fast as Spotlight (⌘Space) or Raycast (⌥Space).

### The chord

**⌃⌥⇧S** (Control + Option + Shift + S).

- Three modifiers + a letter ⇒ very unlikely to collide with other apps.
- `S` mnemonic = "Search".
- Spotlight (⌘Space) and many launchers use two-modifier chords.
  Ours is three to stay out of their way.

### Can an app register itself for that?

**Yes.** Three APIs to choose from, ranked by recommended-ness for
this app:

#### (a) Carbon `RegisterEventHotKey`  — **recommended**

Despite being "Carbon," this API is **not deprecated** and still works
on macOS 14 / 15 / 16. It's what Sublime Text, MacVim, Alfred,
HotKey-the-library, and most third-party apps use.

Permission model:

- **Does NOT require Accessibility permission**, because the OS
  delivers the hotkey event to the registered app directly rather
  than the app monitoring all key events globally.
- This is the key distinction from `NSEvent
  addGlobalMonitorForEventsMatchingMask:`, which DOES require
  Accessibility and intercepts every key event.

Sketch:

```cpp
#include <Carbon/Carbon.h>

static EventHotKeyRef gHotKey = nullptr;

static OSStatus hotKeyHandler(EventHandlerCallRef, EventRef, void *userData) {
    QMetaObject::invokeMethod(static_cast<MainWindow *>(userData),
                              "summon", Qt::QueuedConnection);
    return noErr;
}

void MainWindow::registerGlobalHotkey() {
    EventTypeSpec eventType = { kEventClassKeyboard, kEventHotKeyPressed };
    InstallApplicationEventHandler(&hotKeyHandler, 1, &eventType, this, nullptr);

    EventHotKeyID id = { 'mcsr', 1 };       // OSType + numeric id; any unique value
    UInt32 modifiers = controlKey | optionKey | shiftKey;
    UInt32 keyCode   = kVK_ANSI_S;          // virtual key code for S (Search)
    RegisterEventHotKey(keyCode, modifiers, id,
                        GetApplicationEventTarget(), 0, &gHotKey);
}

void MainWindow::summon() {
    show();
    raise();
    activateWindow();
    m_searchField->setFocus();
    m_searchField->selectAll();
}
```

The Carbon header `Carbon/Carbon.h` is included in macOS SDKs; no
extra link flags beyond `-framework Carbon`.

#### (b) `MASShortcut` / `HotKey` (Swift) / DDHotKey — third-party wrappers

These wrap (a) with niceties (user-customizable bindings, persistence,
UI for picking a chord). Not needed for v1 since we hard-code one chord;
worth considering when we add a "Preferences → Hotkey" dialog.

#### (c) `NSEvent addGlobalMonitorForEventsMatchingMask:` — **avoid**

Monitors every key event globally. Requires the user to grant
Accessibility permission. Both are bad trade-offs when (a) gives us
exactly what we want.

### What pressing the hotkey should do

- If the window is hidden / minimized: show, raise, activate, focus
  search field, select-all so the next keystroke replaces the previous
  query.
- If the window is already visible and focused: do nothing (don't
  steal-and-restore focus from itself).
- If the window is visible but unfocused: focus and select-all (no
  re-show).

Encoded in the `summon()` method sketch above.

### Conflict / discoverability

- ⌃⌥⇧S is unbound by default in macOS as of macOS 14.
- If another app has already claimed it, our `RegisterEventHotKey`
  returns `eventHotKeyExistsErr`. Surface that in a one-time toast
  ("Could not register ⌃⌥⇧S — another app is using it. Change the
  hotkey in Preferences.") and continue running.

### Open question for Benno

> Confirm ⌃⌥⇧S is the right chord, and that the hotkey should be
> on by default (not opt-in)?

---

## How both interact

Best UX: autostart launches the app in `--background` mode (cache
warms in the background, no window). The user hits ⌃⌥⇧S to summon.
Together, "open file by name" becomes a Spotlight-class operation.

Without either, the app is still useful but every cold launch pays the
scan cost.
