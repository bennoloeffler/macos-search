# Icon System - IMPLEMENTED

## Architecture

All icons are SVG files compiled into the binary via Qt Resource System (`assets/icons.qrc`).
A single `IconRegistry` class provides icon lookup for everything — categories, tools, UI elements, scopes, and keywords.

### Build Requirement: Qt6::Svg

**CRITICAL:** The app **must** link `Qt6::Svg` in CMakeLists.txt:

```cmake
find_package(Qt6 REQUIRED COMPONENTS Core Widgets ... Svg)
target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::Svg ...)
```

Without this:
- SVG icons render fine in dev builds (Qt finds plugins from the system installation)
- SVG icons **silently fail** in deployed/packaged builds (DMG) — all icons show "?" or blank
- Both `QPixmap(":/icons/foo.svg")` and `QIcon(":/icons/foo.svg")` need QtSvg at runtime
- `qt_generate_deploy_app_script` only deploys `QtSvg.framework` + `libqsvg.dylib` if the target links `Qt6::Svg`

### Icon Loading

Icons are loaded via two Qt APIs:

| API | Used By | Needs |
|-----|---------|-------|
| `QIcon(path)` | PluginsDialog, CategoryHeader, MCPServerCard | QtSvg.framework (QSvgIconEngine) |
| `QPixmap(path)` | SidebarMenuItem, TerminalWindow | QtSvg.framework + libqsvg.dylib (imageformat plugin) |

Both resolve `:/icons/...` resource paths compiled into the binary at build time.

### IconRegistry API

```cpp
class IconRegistry {
public:
    static QString iconPath(const QString &key);  // Returns ":/icons/..." or ""
    static bool hasIcon(const QString &key);
};
```

### Lookup Order

1. **Exact match** (lowercase, trimmed, hyphens/spaces normalized)
2. **Keyword match** (e.g., "data-engineering" contains "data" → data icon)
3. **Empty string** (caller decides fallback: initial letter, generic icon, etc.)

---

## Current State: Icon Usage Audit

### A. Category Icons (CategoryHeader.cpp)

| Category | Icon | Source |
|----------|------|--------|
| browser & web | globe.svg | MCP |
| development | terminal.svg | MCP |
| ai & apis | brain.svg | MCP |
| database | cylinder.svg | MCP |
| monitoring | chart-line.svg | MCP |
| productivity | briefcase.svg | MCP |
| testing & monitoring | checkmark-shield.svg | MCP catalog |
| business & payments | creditcard.svg | MCP catalog |
| data & search | magnifyingglass.svg | MCP catalog |
| communication | envelope.svg | MCP catalog |
| global | globe.svg | Scope |
| team | arrow-triangle-branch.svg | Scope |
| project | folder.svg | Scope |
| testing | checkmark-shield.svg | Plugin |
| design | paintbrush.svg | Plugin |
| deployment | arrow-up-to-line.svg | Plugin |
| security | lock-shield.svg | Plugin |
| learning | graduationcap.svg | Plugin |
| other | folder.svg | Plugin |

### B. Tool Icons (BashToolCard.cpp)

| Tool | Icon |
|------|------|
| brew/homebrew | beer.svg |
| python/python3 | chevron-left-forwardslash-chevron-right.svg |
| node/nodejs | shippingbox.svg |
| ripgrep/rg | magnifyingglass.svg |
| fd | folder.svg |
| claude | brain.svg |
| git | arrow-triangle-branch.svg |
| uv | lightning.svg |
| imagemagick | image.svg |
| pandoc | doc-text.svg |
| ffmpeg | film.svg |

### C. Sidebar Icons (Sidebar.cpp)

| Menu Item | Icon |
|-----------|------|
| mcp_servers | icon-server.svg |
| plugins | icon-puzzle.svg |
| bash_tools | icon-terminal.svg |
| apis | icon-key.svg |
| config | icon-gear.svg |
| help | icon-help.svg |

### D. Scope Icons (PluginCatalog.h)

| Scope | Icon |
|-------|------|
| User | globe.svg |
| Project | icon-people.svg |
| Local | icon-person.svg |

### E. UI Element Icons (various files)

| Element | Icon | File |
|---------|------|------|
| Warning banner | exclamationmark-triangle.svg | PrerequisiteBanner.cpp |
| Help brain | brain.svg | ClaudeCodeHelpDialog.cpp |
| Help sparkles | icon-sparkles.svg | ClaudeCodeHelpDialog.cpp |
| Folder nav | icon-folder.svg | ProjectPickerView.cpp |
| Folder item | icon-folder.svg | ProjectListItemWidget.cpp |
| Nav up | icon-chevron-up.svg | FolderBrowserDialog.cpp |
| Nav home | icon-home.svg | FolderBrowserDialog.cpp |
| Settings | icon-gear.svg | FolderBrowserDialog.cpp |
| Marketplace | icon-building.svg | MarketplaceCard.cpp |
| Verified badge | verified-badge.svg | various |

---

## Categories Missing Icons (from screenshots)

These appear in the Plugins Discover tab from marketplace data:

| Category | Proposed Icon | SVG to Use |
|----------|---------------|------------|
| ai-agency | brain.svg | existing |
| ai-ml | brain.svg | existing |
| analytics | chart-line.svg | existing |
| api-development | terminal.svg | existing |
| automation | lightning.svg | existing (tools) |
| business-tools | briefcase.svg | existing |
| code-quality | checkmark-shield.svg | existing |
| community | icon-people.svg | existing |
| crypto | lock-shield.svg | existing |
| data-engineering | cylinder.svg | existing |
| debugging | icon-wrench.svg | existing |
| devops | arrow-up-to-line.svg | existing |
| enterprise | icon-building.svg | existing |
| example | icon-sparkles.svg | existing |
| finance | creditcard.svg | existing |
| fullstack | chevron-left-forwardslash-chevron-right.svg | existing |
| packages | shippingbox.svg | existing |
| performance | lightning.svg | existing (tools) |
| skill-enhancers | graduationcap.svg | existing |

### Keyword Fallback Rules

For categories not explicitly mapped, match by keyword:

| Keyword | Icon |
|---------|------|
| ai, ml, machine, neural, llm, agent | brain.svg |
| data, analytics, search, query | magnifyingglass.svg |
| api, endpoint, rest, graphql | terminal.svg |
| deploy, devops, ci, cd, infra, cloud | arrow-up-to-line.svg |
| test, quality, lint, check | checkmark-shield.svg |
| security, auth, crypto, encrypt, key | lock-shield.svg |
| finance, payment, billing, commerce | creditcard.svg |
| design, ui, ux, style, theme | paintbrush.svg |
| database, db, sql, storage, cache | cylinder.svg |
| monitor, observability, log, metric | chart-line.svg |
| learn, education, tutorial, example | graduationcap.svg |
| community, social, team, collab | icon-people.svg |
| enterprise, business, org, company | icon-building.svg |
| package, npm, pip, cargo, module | shippingbox.svg |
| web, browser, http, html | globe.svg |
| debug, fix, troubleshoot, repair | icon-wrench.svg |
| automate, workflow, pipeline, bot | lightning.svg |
| code, dev, programming, fullstack | chevron-left-forwardslash-chevron-right.svg |
| file, document, text, pdf, doc | doc-text.svg |
| email, mail, message, chat, slack | envelope.svg |
| performance, speed, fast, optimize | lightning.svg |
| git, version, branch, repo | arrow-triangle-branch.svg |

---

---

## Complete Registry Map (60+ entries)

```
// === Categories (MCP, Plugin, Marketplace) ===
"browser & web"        → :/icons/mcp/globe.svg
"development"          → :/icons/mcp/terminal.svg
"ai & apis"            → :/icons/mcp/brain.svg
"database"             → :/icons/mcp/cylinder.svg
"monitoring"           → :/icons/mcp/chart-line.svg
"productivity"         → :/icons/mcp/briefcase.svg
"testing & monitoring" → :/icons/mcp/checkmark-shield.svg
"business & payments"  → :/icons/mcp/creditcard.svg
"data & search"        → :/icons/mcp/magnifyingglass.svg
"communication"        → :/icons/mcp/envelope.svg
"testing"              → :/icons/mcp/checkmark-shield.svg
"design"               → :/icons/mcp/paintbrush.svg
"deployment"           → :/icons/mcp/arrow-up-to-line.svg
"security"             → :/icons/mcp/lock-shield.svg
"learning"             → :/icons/mcp/graduationcap.svg
"other"                → :/icons/mcp/folder.svg
"ai-agency"            → :/icons/mcp/brain.svg
"ai-ml"                → :/icons/mcp/brain.svg
"analytics"            → :/icons/mcp/chart-line.svg
"api-development"      → :/icons/mcp/terminal.svg
"automation"           → :/icons/tools/lightning.svg
"business-tools"       → :/icons/mcp/briefcase.svg
"code-quality"         → :/icons/mcp/checkmark-shield.svg
"community"            → :/icons/icons/icon-people.svg
"crypto"               → :/icons/mcp/lock-shield.svg
"data-engineering"     → :/icons/mcp/cylinder.svg
"debugging"            → :/icons/icons/icon-wrench.svg
"devops"               → :/icons/mcp/arrow-up-to-line.svg
"enterprise"           → :/icons/icons/icon-building.svg
"example"              → :/icons/icons/icon-sparkles.svg
"finance"              → :/icons/mcp/creditcard.svg
"fullstack"            → :/icons/mcp/chevron-left-forwardslash-chevron-right.svg
"packages"             → :/icons/mcp/shippingbox.svg
"performance"          → :/icons/tools/lightning.svg
"skill-enhancers"      → :/icons/mcp/graduationcap.svg

// === Scopes ===
"scope-user"           → :/icons/mcp/globe.svg
"scope-project"        → :/icons/icons/icon-people.svg
"scope-local"          → :/icons/icons/icon-person.svg
"global"               → :/icons/mcp/globe.svg
"team"                 → :/icons/mcp/arrow-triangle-branch.svg
"project"              → :/icons/mcp/folder.svg

// === Tools ===
"brew"                 → :/icons/tools/beer.svg
"homebrew"             → :/icons/tools/beer.svg
"python"               → :/icons/mcp/chevron-left-forwardslash-chevron-right.svg
"python3"              → :/icons/mcp/chevron-left-forwardslash-chevron-right.svg
"node"                 → :/icons/mcp/shippingbox.svg
"nodejs"               → :/icons/mcp/shippingbox.svg
"ripgrep"              → :/icons/mcp/magnifyingglass.svg
"rg"                   → :/icons/mcp/magnifyingglass.svg
"fd"                   → :/icons/mcp/folder.svg
"claude"               → :/icons/mcp/brain.svg
"git"                  → :/icons/mcp/arrow-triangle-branch.svg
"uv"                   → :/icons/tools/lightning.svg
"imagemagick"          → :/icons/tools/image.svg
"pandoc"               → :/icons/mcp/doc-text.svg
"ffmpeg"               → :/icons/tools/film.svg

// === Sidebar / UI Elements ===
"mcp_servers"          → :/icons/icons/icon-server.svg
"plugins"              → :/icons/icons/icon-puzzle.svg
"bash_tools"           → :/icons/icons/icon-terminal.svg
"apis"                 → :/icons/icons/icon-key.svg
"config"               → :/icons/icons/icon-gear.svg
"help"                 → :/icons/icons/icon-help.svg

// === UI Elements ===
"warning"              → :/icons/mcp/exclamationmark-triangle.svg
"info"                 → :/icons/icons/icon-info.svg
"sparkles"             → :/icons/icons/icon-sparkles.svg
"folder"               → :/icons/icons/icon-folder.svg
"folder-open"          → :/icons/icons/icon-folder-open.svg
"trash"                → :/icons/icons/icon-trash.svg
"clipboard"            → :/icons/icons/icon-clipboard.svg
"verified"             → :/icons/icons/verified-badge.svg
"marketplace"          → :/icons/icons/icon-building.svg
"robot"                → :/icons/icons/icon-robot.svg
"hook"                 → :/icons/icons/icon-hook.svg
```
