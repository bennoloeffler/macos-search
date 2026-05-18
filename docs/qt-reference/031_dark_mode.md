# Dark Mode / Light Mode Support

Maude automatically adapts to the system appearance (light or dark) on macOS.
Qt detects palette changes via `QEvent::ApplicationPaletteChange` and the app
refreshes all themed surfaces in response.

## Architecture

Three-layer approach:

1. **ThemeManager** singleton (`src/ThemeManager.h/.cpp`) - detects dark/light,
   emits `themeChanged()` signal, re-polishes top-level widgets.
2. **SwiftUIStyle** dynamic colors (`src/SwiftUIStyle.h/.cpp`) - all color
   constants are now functions that check `ThemeManager::isDark()` at call time.
   All pre-built stylesheet methods use these functions internally.
3. **Lightweight refresh** - only persistent UI (Sidebar, SidebarHeader,
   SidebarMenuItem, ProjectPickerView, ProjectListItemWidget) connects to
   `themeChanged()` for runtime switching. Dialogs are created on-demand and
   get the correct theme at construction.

## ThemeManager

```
src/ThemeManager.h
src/ThemeManager.cpp
```

- Singleton `QObject` accessed via `ThemeManager::instance()`
- `initialize()` must be called once after `QApplication` is created
  (done in `main.cpp`)
- `isDark()` checks `QPalette::Window` lightness (`< 0.5` = dark)
- Installs an application event filter for `QEvent::ApplicationPaletteChange`
- On theme change: walks all top-level widgets, calls
  `style()->unpolish(w) / polish(w)`, then emits `themeChanged()`

## SwiftUIStyle Color System

All colors fall into four categories:

### 1. Static colors (same in light and dark)

| Constant           | Value     | Usage           |
|--------------------|-----------|-----------------|
| `BrandColor`       | `#9E38BE` | Primary brand   |
| `BrandColorHover`  | `#8A2FA8` | Brand hover     |
| `BrandColorPressed`| `#762890` | Brand pressed   |
| `ErrorColor`       | `#DC3545` | Error states    |
| `SuccessColor`     | `#27AE60` | Success states  |
| `WarningColor`     | `#E67E22` | Warning states  |

### 2. Theme-aware text colors (CSS strings for stylesheets)

| Function                | Light                    | Dark                       |
|-------------------------|--------------------------|----------------------------|
| `primaryTextColor()`    | `#333333`                | `#E0E0E0`                  |
| `secondaryTextColor()`  | `rgba(0,0,0,0.5)`       | `rgba(255,255,255,0.55)`   |
| `tertiaryTextColor()`   | `rgba(0,0,0,0.4)`       | `rgba(255,255,255,0.4)`    |

### 3. Theme-aware text colors (QColor for QPainter / icon tinting)

| Function                 | Light                      | Dark                        |
|--------------------------|----------------------------|-----------------------------|
| `primaryTextQColor()`    | `QColor(0x33,0x33,0x33)`  | `QColor(0xE0,0xE0,0xE0)`   |
| `secondaryTextQColor()`  | `QColor(0,0,0,128)`       | `QColor(255,255,255,140)`   |
| `tertiaryTextQColor()`   | `QColor(0,0,0,102)`       | `QColor(255,255,255,102)`   |

> **CRITICAL:** The CSS-string functions (`secondaryTextColor()` etc.) return
> strings like `"rgba(255, 255, 255, 0.55)"`. Passing these to `QColor()`
> creates an **INVALID** QColor (black!). QColor only parses hex (`#RRGGBB`),
> not CSS `rgba()` format. Always use the `*QColor()` variants for QPainter,
> icon tinting, and any context that needs a `QColor` object.
> See [Pitfall: CSS strings vs QColor](#pitfall-css-strings-vs-qcolor) below.

### 4. Theme-aware surface colors (functions)

| Function                      | Light                    | Dark                        | Usage                  |
|-------------------------------|--------------------------|-----------------------------|------------------------|
| `cardBackground()`            | `rgba(0,0,0,0.03)`      | `rgba(255,255,255,0.06)`    | Card / input bg        |
| `secondaryBackground()`       | `rgba(0,0,0,0.04)`      | `rgba(255,255,255,0.07)`    | Secondary button bg    |
| `subtleBorder()`              | `rgba(0,0,0,0.1)`       | `rgba(255,255,255,0.12)`    | 1px borders            |
| `hoverBackground()`           | `rgba(0,0,0,0.08)`      | `rgba(255,255,255,0.10)`    | Hover state            |
| `pressedBackground()`         | `rgba(0,0,0,0.12)`      | `rgba(255,255,255,0.15)`    | Pressed state          |
| `strongPressedBackground()`   | `rgba(0,0,0,0.14)`      | `rgba(255,255,255,0.18)`    | Strong pressed         |
| `deepPressedBackground()`     | `rgba(0,0,0,0.18)`      | `rgba(255,255,255,0.22)`    | Deep pressed           |
| `closeButtonBackground()`     | `rgba(0,0,0,0.06)`      | `rgba(255,255,255,0.08)`    | Close / dismiss button |
| `chipBackground()`            | `rgba(0,0,0,0.05)`      | `rgba(255,255,255,0.08)`    | Chip default bg        |

### 5. Pre-built stylesheets (all theme-aware)

- `primaryButtonStyleSheet()` - brand-colored button
- `secondaryButtonStyleSheet()` - subtle background button
- `closeButtonStyleSheet()` - dismiss / cancel button
- `destructiveButtonStyleSheet()` - red danger button
- `cardStyleSheet()` - rounded card frame
- `inputStyleSheet()` - text input field
- `listStyleSheet()` - QListWidget
- `treeViewStyleSheet()` - QTreeView
- `contentAreaGradientStyleSheet()` - subtle brand gradient
- `chipButtonStyleSheet()` - filter chip
- `chipActiveStyleSheet()` - active filter chip
- `chipInactiveStyleSheet()` - inactive filter chip

---

## Pitfall: CSS Strings vs QColor

This is the most important thing to understand about the color system.

SwiftUIStyle has **two sets** of text color functions:

| Purpose | Function | Return Type | Example Return Value |
|---------|----------|-------------|---------------------|
| Stylesheets (`.arg()`) | `secondaryTextColor()` | `QString` | `"rgba(255, 255, 255, 0.55)"` |
| QPainter / icon tinting | `secondaryTextQColor()` | `QColor` | `QColor(255, 255, 255, 140)` |

**Why two sets?** Qt stylesheets accept CSS `rgba()` format. But `QColor`'s
constructor does NOT parse `rgba()` strings. It only accepts:
- Hex: `#RRGGBB`, `#AARRGGBB`
- Named colors: `"red"`, `"blue"`
- Component constructor: `QColor(r, g, b, a)`

Passing `"rgba(255, 255, 255, 0.55)"` to `QColor()` creates an **invalid**
QColor that defaults to **black** (`QColor::isValid() == false`).

### When to use which

```cpp
// STYLESHEET context -> use CSS string functions
label->setStyleSheet(QString("color: %1;").arg(SwiftUIStyle::secondaryTextColor()));
//                                                              ^^^^^^^^^^^^^^^^^^
//                                                              Returns CSS string

// QPAINTER / ICON context -> use QColor functions
QColor tint = SwiftUIStyle::secondaryTextQColor();
//                                     ^^^^^^^^
//                                     Returns QColor
painter.fillRect(rect(), tint);

// IconRegistry -> use QColor functions
button->setIcon(IconRegistry::coloredIcon("refresh", SwiftUIStyle::primaryTextQColor()));
//                                                                 ^^^^^^^^^^^^^^^^^^
//                                                                 coloredIcon() takes QColor
```

### The bug this fixes

Before the `*QColor()` functions were added, all icon tinting code used:
```cpp
// BUG: QColor("rgba(255, 255, 255, 0.55)") -> INVALID -> renders as BLACK
QColor tint(SwiftUIStyle::secondaryTextColor());
```

This made ALL dark mode icons render as solid black, invisible on dark
backgrounds. The fix was adding proper `QColor`-returning functions and
replacing all 21+ broken usages across 15 files.

---

## How to Add a New Widget

### Dialogs (created on-demand)

Dialogs get the correct theme at construction. No `themeChanged` connection
needed. Use SwiftUIStyle helpers in `setStyleSheet()`:

```cpp
// Stylesheets: use CSS string functions
label->setStyleSheet(QString("color: %1;").arg(SwiftUIStyle::secondaryTextColor()));
card->setStyleSheet(SwiftUIStyle::cardStyleSheet());
button->setStyleSheet(SwiftUIStyle::primaryButtonStyleSheet());

// Icons: use QColor functions
button->setIcon(IconRegistry::coloredIcon("settings", SwiftUIStyle::primaryTextQColor()));
iconLabel->setPixmap(IconRegistry::coloredIcon("folder", SwiftUIStyle::secondaryTextQColor())
                     .pixmap(20, 20));
```

### Persistent widgets (always visible)

Persistent widgets like Sidebar, SidebarMenuItem, or ProjectPickerView need
to connect to `themeChanged()` and re-apply their stylesheets AND re-tint
icons:

```cpp
// In constructor
connect(ThemeManager::instance(), &ThemeManager::themeChanged,
        this, &MyWidget::refreshTheme);

// Slot
void MyWidget::refreshTheme()
{
    // Re-apply stylesheets (CSS string functions)
    setStyleSheet(QString("color: %1;").arg(SwiftUIStyle::primaryTextColor()));

    // Re-tint icons (QColor functions)
    m_button->setIcon(IconRegistry::coloredIcon("settings", SwiftUIStyle::primaryTextQColor()));
}
```

### Custom-painted widgets (explicit background)

Widgets that paint their own background (like ProjectPickerView) should use
`QApplication::palette().color(QPalette::Window)` in their `paintEvent`
rather than relying on `setAutoFillBackground(true)`, which may not update
reliably on theme changes for non-top-level widgets:

```cpp
void MyWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);

    // Always reads the CURRENT system palette (updates on theme change)
    painter.fillRect(rect(), QApplication::palette().color(QPalette::Window));

    // Optional gradient overlay (theme-aware)
    if (ThemeManager::instance()->isDark()) {
        // dark overlay...
    } else {
        // light overlay...
    }
}
```

Also ensure any QScrollArea viewports are transparent:
```cpp
m_scrollArea->setStyleSheet("background: transparent; border: none;");
m_scrollArea->viewport()->setAutoFillBackground(false);
```

### Syntax highlighters

Highlighters connect to `themeChanged()`, clear their rules, rebuild with
the new theme colors, and call `rehighlight()`:

```cpp
connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, [this]() {
    m_highlightingRules.clear();
    setupHighlightingRules();
    rehighlight();
});
```

---

## Rules for Hardcoded Colors

**Never** use inline `rgba(0,0,0,...)` or `#333333` in setStyleSheet() calls.
Always use SwiftUIStyle helper functions.

| Instead of                           | Use                                       |
|--------------------------------------|-------------------------------------------|
| `color: #333333`                     | `SwiftUIStyle::primaryTextColor()`        |
| `color: rgba(0,0,0,0.5)`            | `SwiftUIStyle::secondaryTextColor()`      |
| `color: rgba(0,0,0,0.4)`            | `SwiftUIStyle::tertiaryTextColor()`       |
| `background: rgba(0,0,0,0.03)`      | `SwiftUIStyle::cardBackground()`          |
| `border: 1px solid rgba(0,0,0,0.1)` | `SwiftUIStyle::subtleBorder()`            |
| `background: white`                  | `background: palette(window)` or `transparent` |
| `QColor(SwiftUIStyle::primaryTextColor())` | `SwiftUIStyle::primaryTextQColor()` |

**Exceptions** (OK to hardcode):
- Brand color `#9E38BE` and variants (same in both themes)
- Status colors `#DC3545`, `#27AE60`, `#E67E22` (same in both themes)
- Intentionally dark overlays like crash overlay (`rgba(0,0,0,0.85)`)
- Selection highlights using system blue (`rgba(0,122,255,...)`)
- Colors inside `isDark()` ternaries (the light-mode branch is fine)

---

## Icon Tinting for Dark Mode (Phase 7 - COMPLETE)

### The Problem

All SVG icons in `assets/icons/` use `stroke="currentColor"`. Qt's SVG renderer
treats `currentColor` as **black** (not the CSS/palette text color), so icons are
invisible on dark backgrounds.

### The Solution: `IconRegistry::coloredIcon()`

```cpp
// src/IconRegistry.h
static QIcon coloredIcon(const QString &key, const QColor &color,
                         const QSize &size = QSize(16, 16));
```

This method:
1. Loads the SVG via `QIcon::pixmap()`
2. Uses `QPainter::CompositionMode_SourceIn` to replace all visible pixels
   with the target color (preserving alpha)
3. Returns a `QIcon` wrapping the tinted `QPixmap`

### How to Use

**For QPushButton icons (via `setIcon`):**
```cpp
button->setIcon(IconRegistry::coloredIcon("refresh", SwiftUIStyle::primaryTextQColor()));
button->setIconSize(QSize(14, 14));
```

**For QLabel pixmaps (via `setPixmap`):**
```cpp
iconLabel->setPixmap(
    IconRegistry::coloredIcon("folder", SwiftUIStyle::secondaryTextQColor())
    .pixmap(20, 20));
```

**Manual QPainter tinting (when icon is already a QPixmap):**
```cpp
QPixmap icon = QIcon(path).pixmap(QSize(20, 20));
QColor tint = SwiftUIStyle::secondaryTextQColor();
QPainter iconPainter(&icon);
iconPainter.setCompositionMode(QPainter::CompositionMode_SourceIn);
iconPainter.fillRect(icon.rect(), tint);
iconPainter.end();
label->setPixmap(icon);
```

### Which Color to Use

| Context | Color Function |
|---------|---------------|
| Primary UI icons (toolbar buttons, nav) | `SwiftUIStyle::primaryTextQColor()` |
| Secondary icons (list items, cards) | `SwiftUIStyle::secondaryTextQColor()` |
| Tertiary / decorative icons | `SwiftUIStyle::tertiaryTextQColor()` |
| Warning icons | `SwiftUIStyle::WarningColor` (static, same in both themes) |
| Error icons | `SwiftUIStyle::ErrorColor` (static) |
| Brand-colored icons | `QColor(158, 56, 190)` / `SwiftUIStyle::BrandColor` (static) |

### Icon Tinting Coverage (all COMPLETE)

| Widget | Method | Theme Refresh |
|--------|--------|---------------|
| TerminalWindow | `coloredIcon()` | `refreshOnThemeChange()` slot |
| SidebarMenuItem | Manual QPainter | `updateStyle()` slot |
| ProjectListItemWidget | Manual QPainter | `themeChanged` lambda |
| BashToolsDialog | `coloredIcon()` | N/A (dialog) |
| BashToolCard | `coloredIcon()` + QPainter | N/A (dialog) |
| MCPServerCard | `coloredIcon()` + QPainter | N/A (dialog) |
| MCPServerDetailPage | QPainter | N/A (dialog) |
| PluginsDialog | QPainter | N/A (dialog) |
| PluginCard | QPainter | N/A (dialog) |
| PluginDetailPage | `coloredIcon()` | N/A (dialog) |
| MarketplaceCard | `coloredIcon()` | N/A (dialog) |
| FolderBrowserDialog | `coloredIcon()` | N/A (dialog) |
| APIWrappersDialog | `coloredIcon()` | N/A (dialog) |
| HelpDialog | QPainter (in `createIconRow`) | N/A (dialog) |
| ClaudeCodeHelpDialog | QPainter (in `createIconRow`) | N/A (dialog) |
| ConfigPathLink | `coloredIcon()` | N/A (dialog) |
| ScopeSelector | QPainter | N/A (dialog) |
| CategoryHeader | QPainter | N/A (dialog) |
| PrerequisiteBanner | `coloredIcon()` | N/A (persistent, but icon is WarningColor) |
| TerminalCrashOverlay | `coloredIcon()` | N/A (overlay, WarningColor) |

### Why `currentColor` Doesn't Work in Qt

Qt's SVG renderer (`QSvgRenderer`) does **not** support the CSS `currentColor`
keyword. It renders `stroke="currentColor"` as black regardless of palette.
This is a known Qt limitation. Three workarounds exist:

1. **CompositionMode_SourceIn** (current approach via `coloredIcon()`) -
   Render to pixmap, then tint. Simple, fast, monochrome only.
2. **SVG string replacement** - Read SVG bytes, replace `"currentColor"` with
   a hex color, load into QSvgRenderer. Produces vector-quality output.
3. **Custom QIconEngine** - Subclass `QIconEngine` to read `QPalette::WindowText`
   at paint time, so icons auto-adapt to theme changes without per-widget code.

Approach 1 is what we use now. Approach 3 is the ideal long-term solution
(see PaletteIcon: https://github.com/Kolcha/paletteicon) but requires more
infrastructure. For now, `coloredIcon()` is sufficient for all dialog and
card icons.

---

## Theme Switching for Custom-Painted Views (Phase 8 - COMPLETE)

### The Problem

When switching from dark mode to light mode, the ProjectPickerView background
stayed dark while the Sidebar correctly switched to light. The project list
area appeared as a dark rectangle in an otherwise light UI.

### Root Cause

`ProjectPickerView` originally used `setAutoFillBackground(true)`, which fills
with `QPalette::Window` color. However, for non-top-level widgets, Qt may not
reliably trigger a repaint when the application palette changes.
`ThemeManager::refreshTopLevelWidgets()` only re-polishes top-level widgets -
child widgets like ProjectPickerView (inside a QStackedWidget) were missed.

Additionally, `QScrollArea` creates an internal viewport widget with
`autoFillBackground(true)`. This viewport cached the dark palette color and
didn't update, creating a dark rectangle over the project list.

### The Fix

1. **Removed `setAutoFillBackground(true)`** from ProjectPickerView
2. **Explicit palette painting** in `paintEvent`:
   ```cpp
   painter.fillRect(rect(), QApplication::palette().color(QPalette::Window));
   ```
   This reads the CURRENT system palette every paint, so it always matches.
3. **Theme-aware gradient overlay** with different opacity for dark/light:
   ```cpp
   if (ThemeManager::instance()->isDark()) {
       gradient.setColorAt(0.0, QColor(158, 56, 190, 15));  // ~6%
       gradient.setColorAt(1.0, QColor(158, 56, 190, 5));   // ~2%
   } else {
       gradient.setColorAt(0.0, QColor(158, 56, 190, 10));  // ~4%
       gradient.setColorAt(1.0, QColor(158, 56, 190, 3));   // ~1%
   }
   ```
4. **Transparent scroll area viewport**:
   ```cpp
   m_projectsScrollArea->setStyleSheet("background: transparent; border: none;");
   m_projectsScrollArea->viewport()->setAutoFillBackground(false);
   ```
5. **Viewport repaint on theme change**:
   ```cpp
   connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, [this]() {
       // ... re-apply stylesheets ...
       if (m_projectsScrollArea && m_projectsScrollArea->viewport()) {
           m_projectsScrollArea->viewport()->update();
       }
       update();  // repaint self
   });
   ```

### Why Sidebar Works But ProjectPickerView Didn't

The Sidebar uses stylesheets for its background (`refreshTheme()` re-applies
its stylesheet on every theme change). Qt's stylesheet system handles palette
changes for styled widgets. ProjectPickerView relied on the native palette
fill, which doesn't auto-refresh for non-top-level widgets.

**Lesson:** For custom-painted persistent widgets, always paint backgrounds
explicitly in `paintEvent` using `QApplication::palette()`, not
`setAutoFillBackground(true)`.

---

## Verification

```bash
# Build
cmake -B build -G Ninja && cmake --build build

# Unit tests
QT_QPA_PLATFORM=offscreen ./build/maude-cp-v3_tests

# Check for broken QColor(CSS-string) pattern (should return zero)
grep -rn "QColor(SwiftUIStyle::" src/ --include="*.cpp" | grep -v QColor

# Check for hardcoded colors (should return zero outside SwiftUIStyle.cpp)
grep -rn "rgba(0, 0, 0" src/ --include="*.cpp" | grep -v SwiftUIStyle.cpp | grep -v isDark

# Check for white backgrounds
grep -rn "background.*white" src/ --include="*.cpp"

# Visual test: run in macOS dark mode, verify all screens are readable
# Visual test: switch macOS appearance while app is running, verify UI updates
```

---

## Files Modified (All Phases)

### Infrastructure (Phase 1)
- `src/ThemeManager.h` + `src/ThemeManager.cpp` - created
- `src/main.cpp` - initialize ThemeManager
- `CMakeLists.txt` - added ThemeManager sources

### SwiftUIStyle overhaul (Phase 2)
- `src/SwiftUIStyle.h` + `src/SwiftUIStyle.cpp` - converted static color
  constants to theme-aware functions, updated all pre-built stylesheets

### Migrated callers (Phase 3-4)
All files that previously used `SwiftUIStyle::PrimaryTextColor` (static const)
or inline `rgba(0,0,0,...)` patterns:

- `Sidebar.cpp`, `SidebarHeader.cpp`, `SidebarMenuItem.cpp`
- `ProjectPickerView.cpp`, `ProjectListItemWidget.cpp`
- `PluginsDialog.cpp`, `MCPServersDialog.cpp`, `ConfigExplorerDialog.cpp`
- `FolderBrowserDialog.cpp`, `BashToolsDialog.cpp`, `APIWrappersDialog.cpp`
- `HelpDialog.cpp`, `ClaudeCodeHelpDialog.cpp`
- `MCPServerCard.cpp`, `PluginCard.cpp`, `MarketplaceCard.cpp`
- `BashToolCard.cpp`, `ToolCard.cpp`
- `MCPServerDetailPage.cpp`, `PluginDetailPage.cpp`, `PluginDetailDialog.cpp`
- `ClaudeInstallDialog.cpp`, `HomebrewInstallDialog.cpp`
- `NodeInstallDialog.cpp`, `ToolInstallDialog.cpp`, `MCPServerInstallDialog.cpp`
- `ConfigExplorerRootPage.cpp`, `ConfigFileViewer.cpp`, `ConfigFileRow.cpp`
- `ConfigPathLink.cpp`, `ScopeSelector.cpp`, `ScopeGroupRow.cpp`
- `ExcludeSettingsDialog.cpp`, `PrerequisiteBanner.cpp`
- `AddMarketplaceDialog.cpp`, `AddAPIDialog.cpp`, `CategoryHeader.cpp`
- `TerminalWindow.cpp`, `TerminalCrashOverlay.cpp`
- `FileViewerPage.cpp`, `PathSelectorPopup.cpp`, `PathSelectorUI.cpp`

### Runtime theme switching (Phase 5)
- `Sidebar.cpp` - `refreshTheme()` slot connected to `themeChanged()`
- `SidebarHeader.cpp` - `refreshTheme()` re-applies hamburger button styles
- `SidebarMenuItem.cpp` - `updateStyle()` on theme change
- `ProjectPickerView.cpp` - re-applies styles, updates viewport, repaints
- `ProjectListItemWidget.cpp` - re-tints icons, refreshes label colors

### Syntax highlighters (Phase 6)
- `MarkdownSyntaxHighlighter.cpp` - theme-aware code block colors, rehighlight
- `JsonSyntaxHighlighter.cpp` - theme-aware syntax colors, rehighlight

### Icon tinting (Phase 7)
Added `*QColor()` functions to SwiftUIStyle and applied `coloredIcon()` or
manual QPainter tinting across all icon-using widgets:

- `SwiftUIStyle.h/.cpp` - added `primaryTextQColor()`, `secondaryTextQColor()`, `tertiaryTextQColor()`
- `BashToolsDialog.cpp` - title icon, refresh button, chip icons
- `BashToolCard.cpp` - tool category icon, update/refresh buttons
- `MCPServerCard.cpp` - server icon, verified badge, key badge
- `MCPServerDetailPage.cpp` - server detail icon, tool section icon
- `PluginsDialog.cpp` - category filter icons
- `PluginCard.cpp` - plugin category icon, verified badge
- `PluginDetailPage.cpp` - package icon, section icons
- `MarketplaceCard.cpp` - card icon, action buttons, type icons
- `FolderBrowserDialog.cpp` - navigation buttons (up, home, gear)
- `APIWrappersDialog.cpp` - settings button icon
- `HelpDialog.cpp` - all `createIconRow()` icons
- `ClaudeCodeHelpDialog.cpp` - all `createIconRow()` icons
- `ConfigPathLink.cpp` - clipboard copy icon
- `ScopeSelector.cpp` - scope icon
- `CategoryHeader.cpp` - section icons (`setIconPath()` + `updateForCategory()`)
- `PrerequisiteBanner.cpp` - warning icon (WarningColor)
- `TerminalCrashOverlay.cpp` - warning icon (WarningColor)

### Theme-switching fix (Phase 8)
- `ProjectPickerView.cpp` - explicit `paintEvent` with palette-based background,
  transparent scroll area viewport, theme-aware gradient overlay

### SVG assets
All 114+ SVG files in `assets/icons/` use `stroke="currentColor"`.
No SVG modifications were needed - the fix is entirely in C++ code.
