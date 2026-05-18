# Path Selector V2 - Complete Redesign

Technical specification for the modular, testable path selector component.

---

## Overview

Path Selector V2 is a complete redesign of the path completion system in `FolderBrowserDialog`. It replaces the monolithic 1132-line implementation with a clean, state-driven architecture that is fully testable and maintainable.

### Goals

1. **Single source of truth** - State machine manages all path validation
2. **Testable** - 95% coverage target with mockable filesystem
3. **Maintainable** - Each class has one responsibility
4. **Consistent** - Follows existing patterns (SidebarState, SwiftUIStyle)

---

## Problem Analysis

The current `FolderBrowserDialog.cpp` has critical issues:

| Issue | Impact |
|-------|--------|
| 218-line `eventFilter()` with 18+ keyboard branches | Unmaintainable |
| Tab key logic duplicated in 3 places | Bug-prone |
| `m_lastValidPath` updated in 9+ places | Race conditions |
| Popup visibility managed in 4+ places | Inconsistent behavior |
| Tilde expansion in 3 places | Logic drift |
| Signal blocking for recursion prevention | Fragile |
| ~15% test coverage | Unreliable |

---

## Architecture

### Class Diagram

```
PathSelector (Facade - public API)
    │
    ├── PathSelectorState (State machine with Q_PROPERTY)
    │       - 5 states: Complete, Browsing, PartialMultiple, PartialSingle, Invalid
    │       - Single source of truth for path validation
    │       - m_lastValidPath updated in ONE place only
    │
    ├── PathSelectorUI (Visual layer)
    │       - Text field, hint label, popup
    │       - Binds to state via signals
    │       - updateTextStyle(), updateHintText() based on state
    │
    ├── PathSelectorKeyHandler (Single dispatch table)
    │       - One handleKeyPress() method
    │       - Delegates to state machine
    │       - No duplicated logic
    │
    ├── PathSelectorPopup (Animated dropdown)
    │       - showAnimated(), hideAnimated()
    │       - 150ms OutCubic animations
    │
    └── FileSystemAdapter (Mockable filesystem)
            - listSubdirectories(), isValidDirectory()
            - filterCompletions() with contains-match
            - expandTilde()
```

### State Machine

```
              ┌───────────────┐
  ┌──────────>│   Complete    │<───────────┐
  │           │ (Bold Black)  │            │
  │           └───────┬───────┘            │
  │                   │ User types '/'     │
  │                   ▼                    │
  │           ┌───────┴───────┐            │
  │           │   Browsing    │            │
  │           │ (Normal Black)│            │
  │           └───────┬───────┘            │
  │                   │ User types letters │
  │                   ▼                    │
Accept        ┌───────┴───────────┐      Accept
(Return/Tab)  │                   │    (Tab single)
  │           ▼                   ▼        │
  │     ┌─────┴──────┐     ┌──────┴─────┐  │
  └─────│  Partial   │     │  Partial   │──┘
        │  Multiple  │     │   Single   │
        │ (Grey)     │     │ (Bold Grey)│
        └─────┬──────┘     └──────┬─────┘
              │ No matches        │
              ▼                   │
        ┌─────┴──────┐            │
        │  Invalid   │────────────┘
        │ (Red)      │  User deletes
        └────────────┘

ESC from any state → revert to Complete with lastValidPath
```

---

## File Structure

```
src/PathSelector/
    PathSelector.h              # Facade (public API)
    PathSelector.cpp
    PathSelectorState.h         # State machine (Q_PROPERTY pattern)
    PathSelectorState.cpp
    PathSelectorUI.h            # Visual components
    PathSelectorUI.cpp
    PathSelectorKeyHandler.h    # Single keyboard dispatch
    PathSelectorKeyHandler.cpp
    PathSelectorPopup.h         # Animated popup list
    PathSelectorPopup.cpp
    FileSystemAdapter.h         # Mockable filesystem ops
    FileSystemAdapter.cpp

tests/
    FileSystemAdapterTest.cpp   # Filesystem unit tests
    FileSystemAdapterTest.h
    PathSelectorStateTest.cpp   # State machine unit tests
    PathSelectorStateTest.h
    PathSelectorKeyHandlerTest.cpp
    PathSelectorKeyHandlerTest.h
    PathSelectorTest.cpp        # Integration tests
    PathSelectorTest.h
```

---

## Public API

```cpp
class PathSelector : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged)

public:
    explicit PathSelector(QWidget *parent = nullptr);

    // For testing - inject mock filesystem
    explicit PathSelector(FileSystemAdapter *fs, QWidget *parent = nullptr);

    QString path() const;
    void setPath(const QString &path);

    // Focus management
    void focusPathField();

signals:
    void pathChanged(const QString &validPath);
    void pathCancelled();  // ESC pressed, reverted to last valid
    void focusTraversalRequested();  // Tab on complete path
};
```

---

## Component Specifications

### FileSystemAdapter

Mockable interface for filesystem operations.

```cpp
class FileSystemAdapter : public QObject
{
    Q_OBJECT
public:
    explicit FileSystemAdapter(QObject *parent = nullptr);
    virtual ~FileSystemAdapter() = default;

    // Core operations
    virtual bool isValidDirectory(const QString &path) const;
    virtual QStringList listSubdirectories(const QString &path) const;
    virtual QString expandTilde(const QString &path) const;
    virtual QString homePath() const;

    // Completion with contains-match
    virtual QStringList filterCompletions(const QString &basePath,
                                          const QString &prefix) const;
};
```

**Contains-Match Feature**: When typing `/Users/benno/s`, show folders that CONTAIN "s":
- `/Users/benno/Documents`
- `/Users/benno/Downloads`

Implementation:
1. First pass: prefix matches (highest priority)
2. Second pass: contains matches (case-insensitive)
3. Return combined list, deduplicated

### PathSelectorState

State machine following `SidebarState` pattern.

```cpp
class PathSelectorState : public QObject
{
    Q_OBJECT
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString currentText READ currentText WRITE setCurrentText NOTIFY currentTextChanged)
    Q_PROPERTY(QString lastValidPath READ lastValidPath NOTIFY lastValidPathChanged)
    Q_PROPERTY(QStringList completions READ completions NOTIFY completionsChanged)
    Q_PROPERTY(int selectedIndex READ selectedIndex WRITE setSelectedIndex NOTIFY selectedIndexChanged)

public:
    enum class State {
        Complete,        // Valid directory path
        Browsing,        // Valid with trailing slash, browsing subdirs
        PartialMultiple, // Partial match, multiple completions
        PartialSingle,   // Partial match, single completion
        Invalid          // No matches
    };
    Q_ENUM(State)

    explicit PathSelectorState(FileSystemAdapter *fs, QObject *parent = nullptr);

    // State accessors
    State state() const;
    QString currentText() const;
    QString lastValidPath() const;
    QStringList completions() const;
    int selectedIndex() const;

    // Actions
    void setCurrentText(const QString &text);
    void setSelectedIndex(int index);
    void acceptSelection();      // Return pressed
    void revert();               // ESC pressed
    void cycleSelection(int delta);  // Tab/arrows

signals:
    void stateChanged(State state);
    void currentTextChanged(const QString &text);
    void lastValidPathChanged(const QString &path);
    void completionsChanged(const QStringList &completions);
    void selectedIndexChanged(int index);
    void pathAccepted(const QString &path);
    void pathReverted(const QString &path);

private:
    void updateState();  // Single place where state transitions happen

    FileSystemAdapter *m_fs;
    State m_state = State::Complete;
    QString m_currentText;
    QString m_lastValidPath;
    QStringList m_completions;
    int m_selectedIndex = -1;
};
```

### PathSelectorKeyHandler

Single dispatch table for keyboard events.

```cpp
class PathSelectorKeyHandler : public QObject
{
    Q_OBJECT
public:
    explicit PathSelectorKeyHandler(PathSelectorState *state,
                                    QObject *parent = nullptr);

    // Returns true if key was handled
    bool handleKeyPress(QKeyEvent *event, bool popupVisible);

signals:
    void showPopupRequested();
    void hidePopupRequested();
    void focusTraversalRequested();  // Tab should move to next widget

private:
    bool handleTab(bool popupVisible);
    bool handleReturn(bool popupVisible);
    bool handleEscape(bool popupVisible);
    bool handleArrowDown(bool popupVisible);
    bool handleArrowUp(bool popupVisible);
    bool handleSlash();

    PathSelectorState *m_state;
};
```

### PathSelectorPopup

Animated dropdown list.

```cpp
class PathSelectorPopup : public QWidget
{
    Q_OBJECT
public:
    explicit PathSelectorPopup(QWidget *anchor, QWidget *parent = nullptr);

    void setItems(const QStringList &items);
    void setSelectedIndex(int index);
    int selectedIndex() const;

    void showAnimated();
    void hideAnimated();
    bool isAnimating() const;

    // Constants
    static constexpr int AnimationDuration = 150;
    static constexpr int MaxVisibleItems = 8;

signals:
    void itemSelected(int index);
    void itemActivated(int index);  // Double-click or Enter

private:
    QListWidget *m_list;
    QPropertyAnimation *m_animation;
    QWidget *m_anchor;
};
```

### PathSelectorUI

Visual layer binding state to widgets.

```cpp
class PathSelectorUI : public QWidget
{
    Q_OBJECT
public:
    explicit PathSelectorUI(PathSelectorState *state, QWidget *parent = nullptr);

    QLineEdit *lineEdit() const;
    PathSelectorPopup *popup() const;

    void focusLineEdit();

signals:
    void focusTraversalRequested();

private slots:
    void onStateChanged(PathSelectorState::State state);
    void onCompletionsChanged(const QStringList &completions);
    void onSelectedIndexChanged(int index);

private:
    void updateTextStyle();
    void updateHintText();
    void setupConnections();

    PathSelectorState *m_state;
    QLineEdit *m_lineEdit;
    QLabel *m_hintLabel;
    PathSelectorPopup *m_popup;
};
```

---

## Visual Styling

Following `SwiftUIStyle` design tokens:

| State | Text Color | Font Weight | Hint |
|-------|------------|-------------|------|
| Complete | `PrimaryTextColor` (#333) | Bold | "/ or ↓ = show folders list" |
| Browsing | `PrimaryTextColor` (#333) | Normal | "Tab = move in list, Return = choose" |
| PartialMultiple | `SecondaryTextColor` (rgba(0,0,0,0.5)) | Normal | "Tab = move in list, Return = choose" |
| PartialSingle | `SecondaryTextColor` | Bold | "Tab or Return = choose" |
| Invalid | `ErrorColor` (#DC3545) | Normal | "Not a valid path" |

---

## Keyboard Behavior

### When Popup is Closed

| Key | State | Action |
|-----|-------|--------|
| **Tab** | Complete | Return false → focus traversal |
| **Tab** | Invalid | Revert to lastValidPath |
| **/** | Complete | Append `/`, show popup |
| **↓** | Any | Show popup, select first |
| **ESC** | Invalid | Revert to lastValidPath |

### When Popup is Open

| Key | Action |
|-----|--------|
| **↑/↓** | Navigate (don't update text) |
| **Tab** | Cycle to next (multiple) or accept (single) |
| **Return** | Accept selected, close popup |
| **ESC** | Close popup, revert if partial/invalid |
| **Letters** | Filter completions |

---

## Testing Strategy

### Unit Tests

**FileSystemAdapterTest.cpp**:
- `testIsValidDirectory_ValidPath()`
- `testIsValidDirectory_InvalidPath()`
- `testListSubdirectories_ReturnsOnlyDirs()`
- `testExpandTilde_ExpandsHome()`
- `testFilterCompletions_PrefixMatch()`
- `testFilterCompletions_ContainsMatch()`
- `testFilterCompletions_CaseInsensitive()`

**PathSelectorStateTest.cpp**:
- `testInitialState_IsComplete()`
- `testSetText_ValidDir_StaysComplete()`
- `testSetText_WithSlash_TransitionsToBrowsing()`
- `testSetText_PartialMultiple_TransitionsCorrectly()`
- `testSetText_PartialSingle_TransitionsCorrectly()`
- `testSetText_NoMatches_TransitionsToInvalid()`
- `testAcceptSelection_UpdatesLastValidPath()`
- `testRevert_RestoresLastValidPath()`
- `testCycleSelection_WrapsAround()`

**PathSelectorKeyHandlerTest.cpp**:
- `testTab_CompleteState_ReturnsFalse()`
- `testTab_PartialSingle_AcceptsAndReturnsTrue()`
- `testTab_PartialMultiple_CyclesSelection()`
- `testReturn_WithSelection_Accepts()`
- `testEscape_ClosesPopupAndReverts()`
- `testSlash_AppendsAndShowsPopup()`
- `testArrowDown_ShowsPopup()`

### Integration Tests

**PathSelectorTest.cpp**:
- `testFullUserFlow_TypeSlashSelectAccept()`
- `testContainsMatch_FindsNonPrefixMatches()`
- `testEscapeRevert_RestoresPath()`
- `testFocusTraversal_TabOnComplete()`

---

## Implementation Order

1. **FileSystemAdapter** - Foundation, tested first
2. **PathSelectorState** - State machine with full tests
3. **PathSelectorKeyHandler** - Dispatch table with tests
4. **PathSelectorPopup** - Animated dropdown
5. **PathSelectorUI** - Binds state to widgets
6. **PathSelector** - Facade, integration tests
7. **Integration** - Replace in FolderBrowserDialog

---

## Migration Path

The new `PathSelector` widget will be integrated into `FolderBrowserDialog`:

```cpp
// Before (in FolderBrowserDialog):
m_rootField = new QLineEdit(this);
m_rootCompleter = new QCompleter(this);
// ... 200+ lines of event handling ...

// After:
m_pathSelector = new PathSelector(this);
connect(m_pathSelector, &PathSelector::pathChanged,
        this, &FolderBrowserDialog::onRootPathChanged);
connect(m_pathSelector, &PathSelector::focusTraversalRequested,
        this, [this]() { m_searchField->setFocus(); });
```

The old `eventFilter()` code (218 lines) and path state management (~400 lines) will be removed.

---

## Verification

1. **Build**: `cmake -B build -G Ninja && cmake --build build`
2. **Run tests**: `QT_QPA_PLATFORM=offscreen ./build/maude-cp-v3_tests`
3. **Coverage target**: 95% for PathSelector components

---

## Detailed Logical State Machine

### States with Full Definition

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         PATH SELECTOR STATE MACHINE                         │
└─────────────────────────────────────────────────────────────────────────────┘

STATES:
═══════

┌─────────────────────────────────────────────────────────────────────────────┐
│ COMPLETE                                                                     │
├─────────────────────────────────────────────────────────────────────────────┤
│ Definition:  Text is a valid directory path WITHOUT trailing slash          │
│ Text Style:  Bold Black                                                      │
│ Popup:       CLOSED                                                          │
│ Completions: EMPTY                                                           │
│ Selection:   -1 (none)                                                       │
│ lastValid:   = currentText                                                   │
│                                                                              │
│ Example:     "/Users/benno"                                                  │
└─────────────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────────────┐
│ BROWSING                                                                     │
├─────────────────────────────────────────────────────────────────────────────┤
│ Definition:  Text is a valid directory WITH trailing slash                   │
│ Text Style:  Normal Black                                                    │
│ Popup:       OPEN (shows all subdirectories)                                 │
│ Completions: All subdirectories of the path                                  │
│ Selection:   0 (first item highlighted)                                      │
│ lastValid:   = path without trailing slash                                   │
│                                                                              │
│ Example:     "/Users/benno/" → completions: [Applications, Desktop, ...]    │
└─────────────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────────────┐
│ PARTIAL_MULTIPLE                                                             │
├─────────────────────────────────────────────────────────────────────────────┤
│ Definition:  Text has partial name, MULTIPLE matches found                   │
│ Text Style:  Normal Grey                                                     │
│ Popup:       OPEN (shows matching entries)                                   │
│ Completions: 2+ items that contain the partial text                          │
│ Selection:   0 (first match highlighted)                                     │
│ lastValid:   UNCHANGED (stays at previous valid path)                        │
│                                                                              │
│ Example:     "/Users/benno/Do" → completions: [Documents, Downloads]        │
└─────────────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────────────┐
│ PARTIAL_SINGLE                                                               │
├─────────────────────────────────────────────────────────────────────────────┤
│ Definition:  Text has partial name, EXACTLY ONE match found                  │
│ Text Style:  Bold Grey                                                       │
│ Popup:       OPEN (shows single entry)                                       │
│ Completions: Exactly 1 item                                                  │
│ Selection:   0 (the only match highlighted)                                  │
│ lastValid:   UNCHANGED                                                       │
│                                                                              │
│ Example:     "/Users/benno/Doc" → completions: [Documents]                  │
└─────────────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────────────┐
│ INVALID                                                                      │
├─────────────────────────────────────────────────────────────────────────────┤
│ Definition:  Text has partial name, NO matches found                         │
│ Text Style:  Normal Red                                                      │
│ Popup:       CLOSED                                                          │
│ Completions: EMPTY                                                           │
│ Selection:   -1 (none)                                                       │
│ lastValid:   UNCHANGED (can revert to this)                                  │
│                                                                              │
│ Example:     "/Users/benno/Xyz" → no folders match                          │
└─────────────────────────────────────────────────────────────────────────────┘


TRANSITIONS:
════════════

                    ┌─────────────────────────────────────────────┐
                    │              COMPLETE                        │
                    │         (Bold Black, Popup Closed)           │
                    └─────────────────┬───────────────────────────┘
                                      │
                    ┌─────────────────┼─────────────────┐
                    │                 │                 │
              type '/'          type char          ESC/Return
              or ↓ key         (e.g., 'D')        (no change)
                    │                 │                 │
                    ▼                 ▼                 │
    ┌───────────────────────┐  ┌─────────────────────┐ │
    │      BROWSING         │  │  (stays Complete    │ │
    │  (Normal Black)       │  │   if still valid)   │◄┘
    │  Popup: OPEN          │  └─────────────────────┘
    │  Shows all subdirs    │
    └───────────┬───────────┘
                │
          type chars
          (filter)
                │
                ▼
    ┌───────────────────────────────────────────────────────────┐
    │                                                           │
    │  ┌─────────────────┐    ┌─────────────────┐              │
    │  │ PARTIAL_MULTIPLE│    │  PARTIAL_SINGLE │              │
    │  │ (Normal Grey)   │◄──►│  (Bold Grey)    │              │
    │  │ 2+ matches      │    │  1 match        │              │
    │  └────────┬────────┘    └────────┬────────┘              │
    │           │                      │                        │
    │           │    type more chars   │                        │
    │           │    (no matches)      │                        │
    │           ▼                      │                        │
    │  ┌─────────────────┐             │                        │
    │  │    INVALID      │◄────────────┘                        │
    │  │ (Red)           │  (type chars that eliminate match)   │
    │  │ Popup: CLOSED   │                                      │
    │  └─────────────────┘                                      │
    │                                                           │
    └───────────────────────────────────────────────────────────┘

RETURN TO COMPLETE:
───────────────────
- From BROWSING:      Return/Tab → accept selected → COMPLETE
- From PARTIAL_MULTI: Return → accept selected → COMPLETE
- From PARTIAL_SINGLE: Return/Tab → accept the one match → COMPLETE
- From INVALID:       ESC → revert to lastValidPath → COMPLETE
- From ANY:           ESC → revert → COMPLETE (if popup was open)


KEY BEHAVIOR MATRIX:
════════════════════

┌─────────┬──────────────────┬──────────────────┬──────────────────┬──────────────────┬──────────────────┐
│ KEY     │ COMPLETE         │ BROWSING         │ PARTIAL_MULTI    │ PARTIAL_SINGLE   │ INVALID          │
├─────────┼──────────────────┼──────────────────┼──────────────────┼──────────────────┼──────────────────┤
│ /       │ Append /, show   │ (pass through)   │ (pass through)   │ Accept, append / │ (pass through)   │
│         │ popup→BROWSING   │                  │                  │ show popup       │                  │
├─────────┼──────────────────┼──────────────────┼──────────────────┼──────────────────┼──────────────────┤
│ ↓       │ Append /, show   │ Cycle select +1  │ Cycle select +1  │ Cycle select +1  │ (nothing)        │
│         │ popup→BROWSING   │                  │                  │                  │                  │
├─────────┼──────────────────┼──────────────────┼──────────────────┼──────────────────┼──────────────────┤
│ ↑       │ (nothing)        │ Cycle select -1  │ Cycle select -1  │ Cycle select -1  │ (nothing)        │
├─────────┼──────────────────┼──────────────────┼──────────────────┼──────────────────┼──────────────────┤
│ Tab     │ Focus traversal  │ Cycle select +1  │ Cycle select +1  │ ACCEPT→COMPLETE  │ Revert→COMPLETE  │
│         │ (leave field)    │                  │                  │                  │ + focus traverse │
├─────────┼──────────────────┼──────────────────┼──────────────────┼──────────────────┼──────────────────┤
│ Return  │ (confirm path)   │ ACCEPT selected  │ ACCEPT selected  │ ACCEPT→COMPLETE  │ Revert→COMPLETE  │
│         │ →COMPLETE        │ →COMPLETE        │ →COMPLETE        │                  │                  │
├─────────┼──────────────────┼──────────────────┼──────────────────┼──────────────────┼──────────────────┤
│ ESC     │ (nothing)        │ Close popup,     │ Revert,          │ Revert,          │ Revert→COMPLETE  │
│         │                  │ stay BROWSING*   │ →COMPLETE        │ →COMPLETE        │                  │
├─────────┼──────────────────┼──────────────────┼──────────────────┼──────────────────┼──────────────────┤
│ Backsp  │ Remove char,     │ Remove /,        │ Expand matches   │ May expand to    │ May become       │
│         │ may→PARTIAL      │ →COMPLETE        │ or→INVALID       │ PARTIAL_MULTI    │ PARTIAL_*        │
├─────────┼──────────────────┼──────────────────┼──────────────────┼──────────────────┼──────────────────┤
│ chars   │ Append char,     │ Append char,     │ Filter matches,  │ Filter matches,  │ May find match   │
│         │ may→PARTIAL      │ →PARTIAL_*       │ may shrink list  │ may→MULTI/INVLD  │ →PARTIAL_*       │
└─────────┴──────────────────┴──────────────────┴──────────────────┴──────────────────┴──────────────────┘

* Note: ESC in BROWSING currently reverts to lastValidPath. Should it just close popup?
```

---

## Implementation Gap Analysis

### Comparing Spec vs Implementation

| Feature | Spec | Implementation | Gap? |
|---------|------|----------------|------|
| State enum | 5 states | ✅ 5 states in PathSelectorState.h | OK |
| Popup auto-show | On Partial/Browsing | ✅ shouldShowPopup() checks | OK |
| Popup auto-hide | On Complete/Invalid | ⚠️ Only on explicit actions | **GAP** |
| Text field update on ↓ | NO (just highlight) | ✅ Does NOT update text | OK |
| Backspace reopens popup | If matches exist | ❌ State updates but popup not auto-shown | **GAP** |
| Filter in popup | Filter list items | ✅ SearchField in popup | OK |
| Filter vs State text | Popup filter != state text | ⚠️ Separate systems | **CONFUSION** |

### Critical Issue: Two Filtering Systems

The current implementation has **TWO** places where filtering happens:

1. **PathSelectorState::updateState()** - filters based on `m_currentText` (main line edit)
2. **PathSelectorPopup::filterItems()** - filters based on popup's SearchField

**Problem**: When user types in main field, state filters completions. When user types in popup SearchField, only popup UI filters (state completions unchanged).

**Expected behavior**: The main line edit text should drive filtering. The popup SearchField should NOT exist, or it should sync with main field.

---

## Greybox Test Scenarios

### Test Descriptions (Verbal)

These tests describe real user workflows that verify the PathSelector behaves correctly. Each test is written as a story that a tester (human or automated) can follow.

---

#### Test 1: "Filter and Navigate to Downloads"

**Story**: A user wants to navigate to their Downloads folder. They start at their home directory, type "/" to see all folders, then start typing "Do" to filter. They see Documents and Downloads. They type "c" to narrow to Documents only, then realize they wanted Downloads, so they backspace to "Do" again. They use the down arrow to move to Downloads and press Enter to accept.

**What we're testing**:
- Typing "/" opens the popup with all subdirectories
- Typing letters filters the list (contains-match, not just prefix)
- Backspace expands the filter (shows more matches again)
- Arrow keys navigate the list WITHOUT changing the text field
- Enter accepts the highlighted item

**Why this matters**: This is the most common use case - quick keyboard navigation to a known folder.

---

#### Test 2: "Escape Revert Flow"

**Story**: A user accidentally types a path that doesn't exist. The text turns red indicating invalid. They press ESC to cancel their mistake and return to the last valid path.

**What we're testing**:
- Invalid paths are detected and shown in red
- ESC reverts to the last known good path
- After ESC, state returns to Complete

**Why this matters**: Users make typos. They need a safe way to undo without losing their previous valid selection.

---

#### Test 3: "Tab Accepts Single Match"

**Story**: A user types "/Doc" and only Documents matches. They press Tab expecting it to auto-complete. It does, and they stay in the field to continue typing (not tabbing away).

**What we're testing**:
- When only one match exists, Tab accepts it
- Focus stays in the path field after Tab-accept
- User can continue typing "/" to browse deeper

**Why this matters**: Tab-completion is expected behavior. Unlike Tab on a complete path (which moves focus), Tab on a partial should complete and stay.

---

#### Test 4: "Tab Focus Traversal on Complete"

**Story**: A user has selected a valid path. They press Tab to move to the next form field.

**What we're testing**:
- On a complete path, Tab moves focus to next widget
- The path is NOT modified
- Signal focusTraversalRequested is emitted

**Why this matters**: Standard form behavior - Tab should navigate between fields when the current field is "done".

---

#### Test 5: "Arrow Down Opens Popup from Complete"

**Story**: A user is on a valid path "/Users/benno" and wants to browse its contents without typing. They press the down arrow. The popup opens showing all subdirectories with the first one highlighted.

**What we're testing**:
- Down arrow on Complete state opens popup
- A "/" is appended to the path
- State becomes Browsing
- First item is pre-selected

**Why this matters**: Users expect arrow keys to work like native file pickers. Down arrow = "show me what's inside".

---

## Run-Tests Menu

### Test Groups

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           GREYBOX TEST MENU                                 │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  GROUP: path-selector                                                       │
│  ═══════════════════                                                        │
│                                                                             │
│  [ ] Test 1: Filter and Navigate to Downloads                               │
│      - Typing / opens popup                                                 │
│      - Typing "Do" filters to Documents, Downloads                          │
│      - Typing "c" narrows to Documents only                                 │
│      - Backspace expands back to two matches                                │
│      - Down arrow moves selection (not text!)                               │
│      - Enter accepts Downloads                                              │
│      - Backspace reopens popup                                              │
│      - Multiple backspaces expand matches                                   │
│                                                                             │
│  [ ] Test 2: Escape Revert Flow                                             │
│      - Type invalid path "/Xyz"                                             │
│      - Text turns red, state = Invalid                                      │
│      - Press ESC                                                            │
│      - Reverts to last valid path                                           │
│                                                                             │
│  [ ] Test 3: Tab Accepts Single Match                                       │
│      - Type partial "/Doc"                                                  │
│      - State = PartialSingle                                                │
│      - Press Tab                                                            │
│      - Path completes to /Documents                                         │
│      - Focus stays in field                                                 │
│                                                                             │
│  [ ] Test 4: Tab Focus Traversal on Complete                                │
│      - Start with complete path                                             │
│      - Press Tab                                                            │
│      - focusTraversalRequested emitted                                      │
│      - Focus leaves path field                                              │
│                                                                             │
│  [ ] Test 5: Arrow Down Opens Popup                                         │
│      - Start with complete path, popup closed                               │
│      - Press Down arrow                                                     │
│      - "/" appended, popup opens                                            │
│      - State = Browsing, first item selected                                │
│                                                                             │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  FUTURE GROUPS:                                                             │
│  ──────────────                                                             │
│  [ ] folder-browser-dialog                                                  │
│  [ ] project-picker                                                         │
│  [ ] recent-projects                                                        │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Running Tests

#### In-App Test Runner (Greybox Tests)

The production app includes a built-in test runner with logging and screenshot capabilities.
This is designed for both manual testing and automated testing via Claude Code.

**Enable Debug Mode (required for test menu):**
```bash
# Set environment variable
export MAUDE_DEBUG=true

# Or use the rb (run build) script which sets it automatically
./scripts/rb
```

**Run tests from CLI:**
```bash
# List available tests
MAUDE_DEBUG=true ./build/maude-cp-v3.app/Contents/MacOS/maude-cp-v3 --list-tests

# Run specific test group
MAUDE_DEBUG=true ./build/maude-cp-v3.app/Contents/MacOS/maude-cp-v3 --run-test path-selector

# Run specific test
MAUDE_DEBUG=true ./build/maude-cp-v3.app/Contents/MacOS/maude-cp-v3 --run-test FilterAndNavigateToDownloads

# Run all tests
MAUDE_DEBUG=true ./build/maude-cp-v3.app/Contents/MacOS/maude-cp-v3 --run-test all
```

**Run tests from Menu (GUI):**
1. Start with `MAUDE_DEBUG=true`
2. Menu: Help > Run Tests > path-selector

**Test Output:**
- Console: Real-time step-by-step logging
- Screenshots: Saved to `/tmp/maude-tests/` after each step
- Log file: Can be configured via `--test-log <path>`

**For Claude Code Automation:**
```bash
# Claude Code can run tests and analyze screenshots
MAUDE_DEBUG=true ./build/maude-cp-v3.app/Contents/MacOS/maude-cp-v3 --run-test path-selector 2>&1 | tee test-output.log

# Screenshots are saved with timestamps:
# /tmp/maude-tests/FilterAndNavigateToDownloads_step1_20240101_120000.png
```

#### Unit Tests (Qt Test Framework)

**Run all unit tests (headless):**
```bash
QT_QPA_PLATFORM=offscreen ./build/maude-cp-v3_tests
```

**Run specific test class:**
```bash
QT_QPA_PLATFORM=offscreen ./build/maude-cp-v3_tests PathSelectorStateTest
```

---

### Test Concept: How to Implement

Greybox tests combine:
- **Black-box**: Testing through the public interface (key events, visible state)
- **White-box**: Asserting internal state (PathSelectorState properties)

**Test Framework**:
```cpp
class PathSelectorGreyboxTest : public QObject
{
    Q_OBJECT

private:
    // Test fixture with mock filesystem
    MockFileSystemAdapter *m_mockFs;
    PathSelector *m_selector;

    // Helper to simulate key press
    void pressKey(Qt::Key key);
    void typeText(const QString &text);
    void pressBackspace(int count = 1);

    // Assertions
    void assertState(PathSelectorState::State expected);
    void assertTextField(const QString &expected);
    void assertPopupVisible(bool visible);
    void assertPopupItems(const QStringList &expected);
    void assertSelectedIndex(int index);
};
```

**Mock Filesystem Setup**:
```cpp
void setupTestFilesystem()
{
    // Simulate /Users/benno/ with subdirectories
    m_mockFs->setDirectories({
        "/Users/benno",
        "/Users/benno/Applications",
        "/Users/benno/Desktop",
        "/Users/benno/Documents",
        "/Users/benno/Downloads",
        "/Users/benno/Library",
        "/Users/benno/Movies",
        "/Users/benno/Music",
        "/Users/benno/Pictures",
        "/Users/benno/Public"
    });
}
```

---

### Greybox Test 1: Filter and Navigate to Downloads

**Scenario**: User filters list by typing, navigates with arrow, accepts with Return.

```
GIVEN:  PathSelector initialized with "/Users/benno"
        State: COMPLETE, TextField: "/Users/benno", Popup: CLOSED

STEP 1: Type "/"
EXPECT: State: BROWSING
        TextField: "/Users/benno/"
        Popup: OPEN with all subdirs [Applications, Desktop, Documents, Downloads, ...]
        Selection: index 0 (Applications highlighted)

STEP 2: Type "Do"
EXPECT: State: PARTIAL_MULTIPLE
        TextField: "/Users/benno/Do"
        Popup: OPEN, filtered to [Documents, Downloads]
        Selection: index 0 (Documents highlighted)

STEP 3: Type "c" (now "Doc")
EXPECT: State: PARTIAL_SINGLE
        TextField: "/Users/benno/Doc"
        Popup: OPEN, filtered to [Documents]
        Selection: index 0 (Documents highlighted)

STEP 4: Press BACKSPACE (back to "Do")
EXPECT: State: PARTIAL_MULTIPLE
        TextField: "/Users/benno/Do"
        Popup: OPEN, expanded to [Documents, Downloads]
        Selection: index 0 (Documents highlighted)

STEP 5: Press DOWN ARROW
EXPECT: State: PARTIAL_MULTIPLE (unchanged)
        TextField: "/Users/benno/Do" (unchanged!)
        Popup: OPEN
        Selection: index 1 (Downloads highlighted)

STEP 6: Press RETURN
EXPECT: State: COMPLETE
        TextField: "/Users/benno/Downloads"
        Popup: CLOSED
        lastValidPath: "/Users/benno/Downloads"

STEP 7: Press BACKSPACE (remove 's')
EXPECT: State: PARTIAL_SINGLE (only "Downloads" matches "Download")
        TextField: "/Users/benno/Download"
        Popup: OPEN, showing [Downloads]
        Selection: index 0

STEP 8: Press BACKSPACE 6x (remove "wnload", leaving "Do")
EXPECT: State: PARTIAL_MULTIPLE
        TextField: "/Users/benno/Do"
        Popup: OPEN, showing [Documents, Downloads]
        Selection: index 0
```

---

### Greybox Test 2: Escape Revert Flow

**Scenario**: User types invalid path, ESC reverts to last valid.

```
GIVEN:  PathSelector with "/Users/benno/Documents"
        State: COMPLETE

STEP 1: Type "/Xyz"
EXPECT: State: INVALID
        TextField: "/Users/benno/Documents/Xyz"
        Popup: CLOSED
        lastValidPath: "/Users/benno/Documents" (unchanged)
        Text color: RED

STEP 2: Press ESC
EXPECT: State: COMPLETE
        TextField: "/Users/benno/Documents"
        Popup: CLOSED
        Text color: BLACK BOLD
```

---

### Greybox Test 3: Tab Accepts Single Match

**Scenario**: Tab on single match accepts and allows continued typing.

```
GIVEN:  PathSelector with "/Users/benno"
        State: COMPLETE

STEP 1: Type "/Doc"
EXPECT: State: PARTIAL_SINGLE
        TextField: "/Users/benno/Doc"
        Popup: OPEN with [Documents]
        Selection: 0

STEP 2: Press TAB
EXPECT: State: COMPLETE
        TextField: "/Users/benno/Documents"
        Popup: CLOSED
        lastValidPath: "/Users/benno/Documents"
        Focus: STILL in path field (not traversed!)

STEP 3: Type "/"
EXPECT: State: BROWSING
        TextField: "/Users/benno/Documents/"
        Popup: OPEN with Documents' subdirectories
```

---

### Greybox Test 4: Tab Focus Traversal on Complete

**Scenario**: Tab on complete path moves focus to next widget.

```
GIVEN:  PathSelector with "/Users/benno"
        State: COMPLETE
        Popup: CLOSED

STEP 1: Press TAB
EXPECT: State: COMPLETE (unchanged)
        TextField: "/Users/benno" (unchanged)
        Popup: CLOSED
        Signal: focusTraversalRequested emitted
        Result: Focus moves to next widget in tab order
```

---

### Greybox Test 5: Arrow Down Opens Popup from Complete

**Scenario**: Arrow down on complete path opens popup with subdirs.

```
GIVEN:  PathSelector with "/Users/benno"
        State: COMPLETE
        Popup: CLOSED

STEP 1: Press DOWN ARROW
EXPECT: State: BROWSING
        TextField: "/Users/benno/"
        Popup: OPEN with all subdirs
        Selection: index 0 (first item highlighted)

STEP 2: Press DOWN ARROW
EXPECT: State: BROWSING
        TextField: "/Users/benno/" (unchanged!)
        Popup: OPEN
        Selection: index 1 (second item highlighted)

STEP 3: Press RETURN
EXPECT: State: COMPLETE
        TextField: second item's full path
        Popup: CLOSED
```

---

## Test Implementation Files

Create these test files:

```
tests/
    PathSelectorGreyboxTest.cpp    # All greybox scenarios
    PathSelectorGreyboxTest.h
    MockFileSystemAdapter.cpp      # Mock filesystem for tests
    MockFileSystemAdapter.h
```

### MockFileSystemAdapter

```cpp
class MockFileSystemAdapter : public FileSystemAdapter
{
public:
    void setDirectories(const QStringList &dirs);
    void setDirectoryContents(const QString &dir, const QStringList &subdirs);

    // Override FileSystemAdapter methods
    bool isValidDirectory(const QString &path) const override;
    QStringList listSubdirectories(const QString &path) const override;
    QStringList filterCompletions(const QString &basePath,
                                  const QString &filter) const override;

private:
    QSet<QString> m_validDirs;
    QHash<QString, QStringList> m_contents;
};
```

---

## Known Implementation Issues

Based on the state machine analysis:

1. **Popup SearchField confusion**: Remove or make it sync with main field text
2. **Backspace doesn't reopen popup**: State changes but popup stays closed
3. **ESC in BROWSING**: Should it revert or just close popup?
4. **Text field styling**: Verify colors match spec
5. **cycleSelection doesn't update text**: This is CORRECT per spec

**Priority fixes**:
1. Remove popup SearchField (simplify)
2. Add popup auto-show when state transitions to Partial/Browsing
3. Verify all key behaviors match the matrix above
