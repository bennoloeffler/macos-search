#include "IconRegistry.h"
#include <QHash>
#include <QList>
#include <QPainter>
#include <QPair>

// ─── Helpers ────────────────────────────────────────────────────────────────

// Shorthand macros for icon paths (keeps the table readable)
#define MCP(name)   QStringLiteral(":/icons/mcp/" name)
#define ICONS(name) QStringLiteral(":/icons/icons/" name)
#define TOOLS(name) QStringLiteral(":/icons/tools/" name)

// ─── Master icon table ──────────────────────────────────────────────────────

static const QHash<QString, QString> &iconTable()
{
    static const QHash<QString, QString> table = {

        // ── MCP server categories (display name format) ─────────────────
        {QStringLiteral("browser & web"),        MCP("globe.svg")},
        {QStringLiteral("development"),          MCP("terminal.svg")},
        {QStringLiteral("ai & apis"),            MCP("brain.svg")},
        {QStringLiteral("database"),             MCP("database.svg")},
        {QStringLiteral("monitoring"),           MCP("chart-line.svg")},
        {QStringLiteral("productivity"),         MCP("briefcase.svg")},
        {QStringLiteral("testing & monitoring"), MCP("checkmark-shield.svg")},
        {QStringLiteral("business & payments"),  MCP("creditcard.svg")},
        {QStringLiteral("data & search"),        MCP("magnifyingglass.svg")},
        {QStringLiteral("communication"),        MCP("envelope.svg")},

        // ── Scope headers (installed tab grouping) ──────────────────────
        {QStringLiteral("global"),               MCP("globe.svg")},
        {QStringLiteral("team"),                 MCP("arrow-triangle-branch.svg")},
        {QStringLiteral("project"),              MCP("folder.svg")},
        {QStringLiteral("scope-user"),           MCP("globe.svg")},
        {QStringLiteral("scope-project"),        ICONS("icon-people.svg")},
        {QStringLiteral("scope-local"),          ICONS("icon-person.svg")},

        // ── Plugin categories (lowercase key) ───────────────────────────
        {QStringLiteral("testing"),              MCP("checkmark-shield.svg")},
        {QStringLiteral("design"),               MCP("paintbrush.svg")},
        {QStringLiteral("deployment"),           MCP("arrow-up-to-line.svg")},
        {QStringLiteral("security"),             MCP("lock-shield.svg")},
        {QStringLiteral("learning"),             MCP("graduationcap.svg")},
        {QStringLiteral("other"),                MCP("folder.svg")},

        // ── Marketplace categories (from screenshots — previously missing) ──
        {QStringLiteral("ai-agency"),            MCP("robot.svg")},
        {QStringLiteral("ai-ml"),                MCP("brain.svg")},
        {QStringLiteral("analytics"),            MCP("pie-chart.svg")},
        {QStringLiteral("api-development"),      MCP("terminal.svg")},
        {QStringLiteral("automation"),           MCP("zap.svg")},
        {QStringLiteral("business-tools"),       MCP("briefcase.svg")},
        {QStringLiteral("code-quality"),         MCP("checkmark-shield.svg")},
        {QStringLiteral("community"),            MCP("users.svg")},
        {QStringLiteral("crypto"),               MCP("lock-shield.svg")},
        {QStringLiteral("data-engineering"),     MCP("database.svg")},
        {QStringLiteral("debugging"),            MCP("wrench.svg")},
        {QStringLiteral("devops"),               MCP("arrow-up-to-line.svg")},
        {QStringLiteral("enterprise"),           MCP("briefcase.svg")},
        {QStringLiteral("example"),              MCP("star.svg")},
        {QStringLiteral("finance"),              MCP("creditcard.svg")},
        {QStringLiteral("fullstack"),            MCP("layers.svg")},
        {QStringLiteral("packages"),             MCP("shippingbox.svg")},
        {QStringLiteral("performance"),          MCP("zap.svg")},
        {QStringLiteral("skill-enhancers"),      MCP("graduationcap.svg")},

        // ── Extended categories (100 more useful mappings) ──────────────

        // AI / ML / Agents
        {QStringLiteral("ai"),                   MCP("brain.svg")},
        {QStringLiteral("artificial-intelligence"), MCP("brain.svg")},
        {QStringLiteral("machine-learning"),     MCP("brain.svg")},
        {QStringLiteral("deep-learning"),        MCP("brain.svg")},
        {QStringLiteral("neural-network"),       MCP("brain.svg")},
        {QStringLiteral("nlp"),                  MCP("brain.svg")},
        {QStringLiteral("llm"),                  MCP("brain.svg")},
        {QStringLiteral("chatbot"),              MCP("message-circle.svg")},
        {QStringLiteral("agent"),                MCP("robot.svg")},
        {QStringLiteral("agents"),               MCP("robot.svg")},
        {QStringLiteral("bot"),                  MCP("robot.svg")},
        {QStringLiteral("copilot"),              MCP("robot.svg")},

        // Development & Code
        {QStringLiteral("code"),                 MCP("chevron-left-forwardslash-chevron-right.svg")},
        {QStringLiteral("coding"),               MCP("chevron-left-forwardslash-chevron-right.svg")},
        {QStringLiteral("programming"),          MCP("chevron-left-forwardslash-chevron-right.svg")},
        {QStringLiteral("developer"),            MCP("terminal.svg")},
        {QStringLiteral("developer-tools"),      MCP("terminal.svg")},
        {QStringLiteral("dev-tools"),            MCP("terminal.svg")},
        {QStringLiteral("ide"),                  MCP("terminal.svg")},
        {QStringLiteral("editor"),               MCP("terminal.svg")},
        {QStringLiteral("frontend"),             MCP("paintbrush.svg")},
        {QStringLiteral("backend"),              MCP("database.svg")},
        {QStringLiteral("full-stack"),           MCP("layers.svg")},
        {QStringLiteral("api"),                  MCP("cloud.svg")},
        {QStringLiteral("rest"),                 MCP("cloud.svg")},
        {QStringLiteral("graphql"),              MCP("cloud.svg")},
        {QStringLiteral("sdk"),                  MCP("box.svg")},
        {QStringLiteral("library"),              MCP("books-vertical.svg")},
        {QStringLiteral("framework"),            MCP("layers.svg")},
        {QStringLiteral("scaffold"),             MCP("layers.svg")},
        {QStringLiteral("boilerplate"),          MCP("layers.svg")},
        {QStringLiteral("template"),             MCP("doc-text.svg")},
        {QStringLiteral("snippet"),              MCP("scissors.svg")},
        {QStringLiteral("refactoring"),          MCP("wrench.svg")},

        // Testing & Quality
        {QStringLiteral("test"),                 MCP("checkmark-shield.svg")},
        {QStringLiteral("unit-test"),            MCP("checkmark-shield.svg")},
        {QStringLiteral("integration-test"),     MCP("checkmark-shield.svg")},
        {QStringLiteral("e2e"),                  MCP("checkmark-shield.svg")},
        {QStringLiteral("qa"),                   MCP("checkmark-shield.svg")},
        {QStringLiteral("lint"),                 MCP("checkmark-shield.svg")},
        {QStringLiteral("linting"),              MCP("checkmark-shield.svg")},
        {QStringLiteral("formatter"),            MCP("sliders.svg")},
        {QStringLiteral("formatting"),           MCP("sliders.svg")},
        {QStringLiteral("static-analysis"),      MCP("eye.svg")},
        {QStringLiteral("code-review"),          MCP("eye.svg")},

        // DevOps & Infrastructure
        {QStringLiteral("ci"),                   MCP("refresh.svg")},
        {QStringLiteral("cd"),                   MCP("arrow-up-to-line.svg")},
        {QStringLiteral("ci-cd"),                MCP("refresh.svg")},
        {QStringLiteral("infrastructure"),       MCP("cpu.svg")},
        {QStringLiteral("cloud"),                MCP("cloud.svg")},
        {QStringLiteral("aws"),                  MCP("cloud.svg")},
        {QStringLiteral("azure"),                MCP("cloud.svg")},
        {QStringLiteral("gcp"),                  MCP("cloud.svg")},
        {QStringLiteral("docker"),               MCP("box.svg")},
        {QStringLiteral("kubernetes"),           MCP("grid.svg")},
        {QStringLiteral("k8s"),                  MCP("grid.svg")},
        {QStringLiteral("terraform"),            MCP("layers.svg")},
        {QStringLiteral("ansible"),              MCP("settings.svg")},
        {QStringLiteral("server"),               MCP("cpu.svg")},
        {QStringLiteral("hosting"),              MCP("cloud.svg")},
        {QStringLiteral("container"),            MCP("box.svg")},
        {QStringLiteral("orchestration"),        MCP("grid.svg")},
        {QStringLiteral("pipeline"),             MCP("refresh.svg")},

        // Data & Database
        {QStringLiteral("data"),                 MCP("database.svg")},
        {QStringLiteral("data-science"),         MCP("pie-chart.svg")},
        {QStringLiteral("big-data"),             MCP("database.svg")},
        {QStringLiteral("etl"),                  MCP("refresh.svg")},
        {QStringLiteral("sql"),                  MCP("database.svg")},
        {QStringLiteral("nosql"),                MCP("database.svg")},
        {QStringLiteral("redis"),                MCP("zap.svg")},
        {QStringLiteral("elasticsearch"),        MCP("magnifyingglass.svg")},
        {QStringLiteral("search"),               MCP("magnifyingglass.svg")},
        {QStringLiteral("cache"),                MCP("zap.svg")},
        {QStringLiteral("storage"),              MCP("database.svg")},
        {QStringLiteral("backup"),               MCP("archive.svg")},
        {QStringLiteral("migration"),            MCP("refresh.svg")},

        // Security & Auth
        {QStringLiteral("auth"),                 MCP("key.svg")},
        {QStringLiteral("authentication"),       MCP("key.svg")},
        {QStringLiteral("authorization"),        MCP("shield.svg")},
        {QStringLiteral("oauth"),                MCP("key.svg")},
        {QStringLiteral("jwt"),                  MCP("key.svg")},
        {QStringLiteral("encryption"),           MCP("lock-shield.svg")},
        {QStringLiteral("ssl"),                  MCP("lock-shield.svg")},
        {QStringLiteral("tls"),                  MCP("lock-shield.svg")},
        {QStringLiteral("firewall"),             MCP("shield.svg")},
        {QStringLiteral("vulnerability"),        MCP("exclamationmark-triangle.svg")},
        {QStringLiteral("audit"),                MCP("eye.svg")},
        {QStringLiteral("compliance"),           MCP("checkmark-shield.svg")},
        {QStringLiteral("privacy"),              MCP("lock-shield.svg")},
        {QStringLiteral("secrets"),              MCP("key.svg")},

        // Monitoring & Observability
        {QStringLiteral("logging"),              MCP("bar-chart.svg")},
        {QStringLiteral("metrics"),              MCP("chart-line.svg")},
        {QStringLiteral("alerting"),             MCP("bell.svg")},
        {QStringLiteral("observability"),        MCP("activity.svg")},
        {QStringLiteral("apm"),                  MCP("activity.svg")},
        {QStringLiteral("tracing"),              MCP("activity.svg")},
        {QStringLiteral("dashboard"),            MCP("grid.svg")},
        {QStringLiteral("reporting"),            MCP("bar-chart.svg")},
        {QStringLiteral("visualization"),        MCP("pie-chart.svg")},

        // Communication & Collaboration
        {QStringLiteral("email"),                MCP("envelope.svg")},
        {QStringLiteral("mail"),                 MCP("envelope.svg")},
        {QStringLiteral("slack"),                MCP("message-circle.svg")},
        {QStringLiteral("discord"),              MCP("message-circle.svg")},
        {QStringLiteral("chat"),                 MCP("message-circle.svg")},
        {QStringLiteral("messaging"),            MCP("send.svg")},
        {QStringLiteral("notification"),         MCP("bell.svg")},
        {QStringLiteral("notifications"),        MCP("bell.svg")},
        {QStringLiteral("webhook"),              MCP("link.svg")},
        {QStringLiteral("social"),               MCP("share.svg")},
        {QStringLiteral("social-media"),         MCP("share.svg")},
        {QStringLiteral("collaboration"),        MCP("users.svg")},

        // Business & Finance
        {QStringLiteral("business"),             MCP("briefcase.svg")},
        {QStringLiteral("payment"),              MCP("creditcard.svg")},
        {QStringLiteral("payments"),             MCP("creditcard.svg")},
        {QStringLiteral("stripe"),               MCP("creditcard.svg")},
        {QStringLiteral("billing"),              MCP("creditcard.svg")},
        {QStringLiteral("invoice"),              MCP("doc-text.svg")},
        {QStringLiteral("commerce"),             MCP("creditcard.svg")},
        {QStringLiteral("e-commerce"),           MCP("creditcard.svg")},
        {QStringLiteral("ecommerce"),            MCP("creditcard.svg")},
        {QStringLiteral("marketplace"),          MCP("grid.svg")},
        {QStringLiteral("crm"),                  MCP("users.svg")},
        {QStringLiteral("erp"),                  MCP("briefcase.svg")},
        {QStringLiteral("accounting"),           MCP("creditcard.svg")},
        {QStringLiteral("banking"),              MCP("creditcard.svg")},

        // Design & Media
        {QStringLiteral("ui"),                   MCP("paintbrush.svg")},
        {QStringLiteral("ux"),                   MCP("paintbrush.svg")},
        {QStringLiteral("ui-ux"),                MCP("paintbrush.svg")},
        {QStringLiteral("style"),                MCP("paintbrush.svg")},
        {QStringLiteral("theme"),                MCP("paintbrush.svg")},
        {QStringLiteral("css"),                  MCP("paintbrush.svg")},
        {QStringLiteral("animation"),            MCP("play.svg")},
        {QStringLiteral("graphics"),             MCP("camera.svg")},
        {QStringLiteral("image"),                TOOLS("image.svg")},
        {QStringLiteral("photo"),                MCP("camera.svg")},
        {QStringLiteral("video"),                TOOLS("film.svg")},
        {QStringLiteral("audio"),                MCP("music.svg")},
        {QStringLiteral("media"),                TOOLS("film.svg")},
        {QStringLiteral("streaming"),            MCP("play.svg")},

        // Documents & Content
        {QStringLiteral("documentation"),        MCP("books-vertical.svg")},
        {QStringLiteral("docs"),                 MCP("books-vertical.svg")},
        {QStringLiteral("wiki"),                 MCP("books-vertical.svg")},
        {QStringLiteral("markdown"),             MCP("doc-text.svg")},
        {QStringLiteral("pdf"),                  MCP("doc-text.svg")},
        {QStringLiteral("file"),                 MCP("doc-text.svg")},
        {QStringLiteral("files"),                MCP("folder.svg")},
        {QStringLiteral("content"),              MCP("doc-text.svg")},
        {QStringLiteral("cms"),                  MCP("doc-text.svg")},
        {QStringLiteral("translation"),          MCP("globe.svg")},
        {QStringLiteral("i18n"),                 MCP("globe.svg")},
        {QStringLiteral("localization"),         MCP("globe.svg")},

        // Web & Networking
        {QStringLiteral("web"),                  MCP("globe.svg")},
        {QStringLiteral("browser"),              MCP("globe.svg")},
        {QStringLiteral("http"),                 MCP("globe.svg")},
        {QStringLiteral("html"),                 MCP("globe.svg")},
        {QStringLiteral("networking"),           MCP("wifi.svg")},
        {QStringLiteral("network"),              MCP("wifi.svg")},
        {QStringLiteral("dns"),                  MCP("wifi.svg")},
        {QStringLiteral("proxy"),                MCP("shield.svg")},
        {QStringLiteral("vpn"),                  MCP("lock-shield.svg")},
        {QStringLiteral("websocket"),            MCP("zap.svg")},
        {QStringLiteral("mobile"),               MCP("smartphone.svg")},
        {QStringLiteral("ios"),                  MCP("smartphone.svg")},
        {QStringLiteral("android"),              MCP("smartphone.svg")},
        {QStringLiteral("responsive"),           MCP("smartphone.svg")},

        // Version Control
        {QStringLiteral("git"),                  MCP("arrow-triangle-branch.svg")},
        {QStringLiteral("github"),               MCP("arrow-triangle-branch.svg")},
        {QStringLiteral("gitlab"),               MCP("arrow-triangle-branch.svg")},
        {QStringLiteral("bitbucket"),            MCP("arrow-triangle-branch.svg")},
        {QStringLiteral("version-control"),      MCP("arrow-triangle-branch.svg")},
        {QStringLiteral("scm"),                  MCP("arrow-triangle-branch.svg")},

        // Education & Learning
        {QStringLiteral("education"),            MCP("graduationcap.svg")},
        {QStringLiteral("tutorial"),             MCP("graduationcap.svg")},
        {QStringLiteral("course"),               MCP("graduationcap.svg")},
        {QStringLiteral("training"),             MCP("graduationcap.svg")},
        {QStringLiteral("onboarding"),           MCP("flag.svg")},
        {QStringLiteral("guide"),                MCP("compass.svg")},
        {QStringLiteral("reference"),            MCP("bookmark.svg")},
        {QStringLiteral("best-practices"),       MCP("award.svg")},

        // Productivity & Workflow
        {QStringLiteral("workflow"),             MCP("refresh.svg")},
        {QStringLiteral("task"),                 MCP("checkmark-shield.svg")},
        {QStringLiteral("tasks"),                MCP("checkmark-shield.svg")},
        {QStringLiteral("project-management"),   MCP("flag.svg")},
        {QStringLiteral("planning"),             MCP("calendar.svg")},
        {QStringLiteral("scheduling"),           MCP("clock.svg")},
        {QStringLiteral("time-tracking"),        MCP("clock.svg")},
        {QStringLiteral("notes"),                MCP("doc-text.svg")},
        {QStringLiteral("todo"),                 MCP("checkmark-shield.svg")},
        {QStringLiteral("kanban"),               MCP("grid.svg")},

        // Tools & Utilities
        {QStringLiteral("utility"),              MCP("wrench.svg")},
        {QStringLiteral("utilities"),            MCP("wrench.svg")},
        {QStringLiteral("tools"),                MCP("wrench.svg")},
        {QStringLiteral("cli"),                  MCP("terminal.svg")},
        {QStringLiteral("terminal"),             MCP("terminal.svg")},
        {QStringLiteral("shell"),                MCP("terminal.svg")},
        {QStringLiteral("bash"),                 MCP("terminal.svg")},
        {QStringLiteral("script"),               MCP("terminal.svg")},
        {QStringLiteral("scripting"),            MCP("terminal.svg")},
        {QStringLiteral("regex"),                MCP("filter.svg")},
        {QStringLiteral("parser"),               MCP("filter.svg")},
        {QStringLiteral("converter"),            MCP("refresh.svg")},
        {QStringLiteral("generator"),            MCP("wand.svg")},
        {QStringLiteral("scraper"),              MCP("download.svg")},
        {QStringLiteral("crawler"),              MCP("download.svg")},

        // Gaming & Fun
        {QStringLiteral("gaming"),               MCP("play.svg")},
        {QStringLiteral("game"),                 MCP("play.svg")},
        {QStringLiteral("entertainment"),        MCP("theatermasks.svg")},
        {QStringLiteral("fun"),                  MCP("star.svg")},

        // Health & Science
        {QStringLiteral("health"),               MCP("heart.svg")},
        {QStringLiteral("medical"),              MCP("heart.svg")},
        {QStringLiteral("science"),              MCP("compass.svg")},
        {QStringLiteral("research"),             MCP("magnifyingglass.svg")},
        {QStringLiteral("math"),                 MCP("hash.svg")},
        {QStringLiteral("statistics"),           MCP("bar-chart.svg")},

        // IoT & Hardware
        {QStringLiteral("iot"),                  MCP("cpu.svg")},
        {QStringLiteral("hardware"),             MCP("cpu.svg")},
        {QStringLiteral("embedded"),             MCP("cpu.svg")},
        {QStringLiteral("raspberry-pi"),         MCP("cpu.svg")},
        {QStringLiteral("arduino"),              MCP("cpu.svg")},
        {QStringLiteral("sensor"),               MCP("activity.svg")},

        // Geographic & Maps
        {QStringLiteral("geo"),                  MCP("map-pin.svg")},
        {QStringLiteral("maps"),                 MCP("map-pin.svg")},
        {QStringLiteral("location"),             MCP("map-pin.svg")},
        {QStringLiteral("gis"),                  MCP("map-pin.svg")},
        {QStringLiteral("navigation"),           MCP("compass.svg")},

        // Misc
        {QStringLiteral("open-source"),          MCP("heart.svg")},
        {QStringLiteral("oss"),                  MCP("heart.svg")},
        {QStringLiteral("blockchain"),           MCP("link.svg")},
        {QStringLiteral("web3"),                 MCP("link.svg")},
        {QStringLiteral("nft"),                  MCP("tag.svg")},
        {QStringLiteral("config"),               MCP("settings.svg")},
        {QStringLiteral("configuration"),        MCP("settings.svg")},
        {QStringLiteral("settings"),             MCP("settings.svg")},
        {QStringLiteral("accessibility"),        MCP("eye.svg")},
        {QStringLiteral("a11y"),                 MCP("eye.svg")},
        {QStringLiteral("print"),                MCP("printer.svg")},
        {QStringLiteral("seo"),                  MCP("trending-up.svg")},
        {QStringLiteral("marketing"),            MCP("trending-up.svg")},
        {QStringLiteral("growth"),               MCP("trending-up.svg")},
        {QStringLiteral("support"),              MCP("life-buoy.svg")},
        {QStringLiteral("help"),                 MCP("life-buoy.svg")},
        {QStringLiteral("feedback"),             MCP("message-circle.svg")},

        // ── Bash tools (from BashToolCard / bash-tools-catalog) ─────────
        // Core
        {QStringLiteral("brew"),                 TOOLS("beer.svg")},
        {QStringLiteral("homebrew"),             TOOLS("beer.svg")},
        {QStringLiteral("python"),               MCP("chevron-left-forwardslash-chevron-right.svg")},
        {QStringLiteral("python3"),              MCP("chevron-left-forwardslash-chevron-right.svg")},
        {QStringLiteral("node"),                 MCP("shippingbox.svg")},
        {QStringLiteral("nodejs"),               MCP("shippingbox.svg")},
        {QStringLiteral("npm"),                  MCP("shippingbox.svg")},
        {QStringLiteral("yarn"),                 MCP("shippingbox.svg")},
        {QStringLiteral("pnpm"),                 MCP("shippingbox.svg")},
        {QStringLiteral("pip"),                  MCP("shippingbox.svg")},
        {QStringLiteral("cargo"),                MCP("shippingbox.svg")},
        {QStringLiteral("ripgrep"),              MCP("magnifyingglass.svg")},
        {QStringLiteral("rg"),                   MCP("magnifyingglass.svg")},
        {QStringLiteral("fd"),                   MCP("folder.svg")},
        {QStringLiteral("claude"),               MCP("brain.svg")},
        {QStringLiteral("uv"),                   TOOLS("lightning.svg")},
        {QStringLiteral("curl"),                 MCP("download.svg")},
        {QStringLiteral("wget"),                 MCP("download.svg")},
        // Image
        {QStringLiteral("imagemagick"),          TOOLS("image.svg")},
        {QStringLiteral("rembg"),                TOOLS("image.svg")},
        {QStringLiteral("optipng"),              TOOLS("image.svg")},
        {QStringLiteral("pngquant"),             TOOLS("image.svg")},
        {QStringLiteral("svgo"),                 TOOLS("image.svg")},
        // Video & Audio
        {QStringLiteral("ffmpeg"),               TOOLS("film.svg")},
        {QStringLiteral("yt-dlp"),               MCP("download.svg")},
        {QStringLiteral("sox"),                   MCP("music.svg")},
        // Documents
        {QStringLiteral("pandoc"),               MCP("doc-text.svg")},
        {QStringLiteral("poppler"),              MCP("doc-text.svg")},
        {QStringLiteral("tesseract"),            MCP("eye.svg")},
        {QStringLiteral("ghostscript"),          MCP("doc-text.svg")},
        {QStringLiteral("libreoffice"),          MCP("doc-text.svg")},
        {QStringLiteral("glow"),                 MCP("doc-text.svg")},
        // Data Processing
        {QStringLiteral("jq"),                   MCP("filter.svg")},
        {QStringLiteral("yq"),                   MCP("filter.svg")},
        {QStringLiteral("miller"),               MCP("filter.svg")},
        {QStringLiteral("mlr"),                  MCP("filter.svg")},
        // Development
        {QStringLiteral("gh"),                   MCP("arrow-triangle-branch.svg")},
        {QStringLiteral("git-lfs"),              MCP("arrow-triangle-branch.svg")},
        {QStringLiteral("bat"),                  MCP("terminal.svg")},
        {QStringLiteral("delta"),                MCP("arrow-triangle-branch.svg")},
        {QStringLiteral("tree"),                 MCP("folder.svg")},
        {QStringLiteral("figlet"),               MCP("terminal.svg")},
        {QStringLiteral("lolcat"),               MCP("terminal.svg")},
        // AI & ML
        {QStringLiteral("whisper"),              MCP("brain.svg")},

        // ── Sidebar / UI elements ───────────────────────────────────────
        {QStringLiteral("personality"),            ICONS("icon-person.svg")},
        {QStringLiteral("mcp_servers"),          ICONS("icon-server.svg")},
        {QStringLiteral("mcp-servers"),          ICONS("icon-server.svg")},
        {QStringLiteral("plugins"),              ICONS("icon-puzzle.svg")},
        {QStringLiteral("bash_tools"),           ICONS("icon-terminal.svg")},
        {QStringLiteral("bash-tools"),           ICONS("icon-terminal.svg")},
        {QStringLiteral("apis"),                 ICONS("icon-key.svg")},

        // ── UI elements ─────────────────────────────────────────────────
        {QStringLiteral("warning"),              MCP("exclamationmark-triangle.svg")},
        {QStringLiteral("error"),                MCP("exclamationmark-triangle.svg")},
        {QStringLiteral("info"),                 ICONS("icon-info.svg")},
        {QStringLiteral("sparkles"),             ICONS("icon-sparkles.svg")},
        {QStringLiteral("skills"),               ICONS("icon-sparkles.svg")},
        {QStringLiteral("folder"),               ICONS("icon-folder.svg")},
        {QStringLiteral("folder-open"),          ICONS("icon-folder-open.svg")},
        {QStringLiteral("trash"),                ICONS("icon-trash.svg")},
        {QStringLiteral("delete"),               ICONS("icon-trash.svg")},
        {QStringLiteral("clipboard"),            ICONS("icon-clipboard.svg")},
        {QStringLiteral("copy"),                 ICONS("icon-clipboard.svg")},
        {QStringLiteral("verified"),             ICONS("verified-badge.svg")},
        {QStringLiteral("robot"),                MCP("robot.svg")},
        {QStringLiteral("hook"),                 ICONS("icon-hook.svg")},
        {QStringLiteral("home"),                 ICONS("icon-home.svg")},
        {QStringLiteral("chevron-left"),         ICONS("icon-chevron-left.svg")},
        {QStringLiteral("chevron-up"),           ICONS("icon-chevron-up.svg")},
        {QStringLiteral("gear"),                 ICONS("icon-gear.svg")},
        {QStringLiteral("eye"),                  MCP("eye.svg")},
        {QStringLiteral("help-circle"),          ICONS("icon-help.svg")},
        {QStringLiteral("type"),                 ICONS("icon-type.svg")},
        {QStringLiteral("vscode"),               ICONS("icon-code.svg")},
        {QStringLiteral("file-text"),            ICONS("icon-file-text.svg")},
        {QStringLiteral("building"),             ICONS("icon-building.svg")},
        {QStringLiteral("people"),               ICONS("icon-people.svg")},
        {QStringLiteral("key"),                  ICONS("icon-key.svg")},
        {QStringLiteral("wrench"),               ICONS("icon-wrench.svg")},
        {QStringLiteral("ui-robot"),             ICONS("icon-robot.svg")},
        {QStringLiteral("brain"),                MCP("brain.svg")},
        {QStringLiteral("briefcase"),            MCP("briefcase.svg")},
        {QStringLiteral("globe"),                MCP("globe.svg")},
        {QStringLiteral("chart-line"),           MCP("chart-line.svg")},
        {QStringLiteral("cylinder"),             MCP("cylinder.svg")},
        {QStringLiteral("doc-text"),             MCP("doc-text.svg")},
        {QStringLiteral("compass"),              MCP("compass.svg")},
        {QStringLiteral("mcp-folder"),           MCP("folder.svg")},
        {QStringLiteral("shippingbox"),          MCP("shippingbox.svg")},
        {QStringLiteral("theatermasks"),          MCP("theatermasks.svg")},

        // ── Action icons ────────────────────────────────────────────────
        {QStringLiteral("refresh"),              MCP("refresh.svg")},
        {QStringLiteral("update"),               MCP("arrow-up-to-line.svg")},
        {QStringLiteral("share"),                MCP("share.svg")},
        {QStringLiteral("publish"),              MCP("arrow-up-to-line.svg")},
    };
    return table;
}

#undef MCP
#undef ICONS
#undef TOOLS

// ─── Keyword fallback table ─────────────────────────────────────────────────

struct KeywordRule {
    QStringList keywords;
    QString iconPath;
};

static const QList<KeywordRule> &keywordRules()
{
    static const QList<KeywordRule> rules = {
        // Order matters: more specific keywords first
        {{"agent", "bot", "copilot", "robot"},                       QStringLiteral(":/icons/mcp/robot.svg")},
        {{"ai", "ml", "machine", "neural", "llm", "gpt", "model"},  QStringLiteral(":/icons/mcp/brain.svg")},
        {{"test", "quality", "lint", "check", "qa"},                 QStringLiteral(":/icons/mcp/checkmark-shield.svg")},
        {{"deploy", "devops", "ci", "cd", "infra"},                  QStringLiteral(":/icons/mcp/arrow-up-to-line.svg")},
        {{"security", "auth", "crypto", "encrypt", "secret"},        QStringLiteral(":/icons/mcp/lock-shield.svg")},
        {{"data", "analytics", "etl", "pipeline"},                   QStringLiteral(":/icons/mcp/database.svg")},
        {{"search", "find", "query"},                                QStringLiteral(":/icons/mcp/magnifyingglass.svg")},
        {{"finance", "payment", "billing", "commerce", "bank"},      QStringLiteral(":/icons/mcp/creditcard.svg")},
        {{"design", "ui", "ux", "style", "theme", "css"},            QStringLiteral(":/icons/mcp/paintbrush.svg")},
        {{"database", "db", "sql", "storage", "cache"},              QStringLiteral(":/icons/mcp/database.svg")},
        {{"monitor", "observability", "log", "metric", "alert"},     QStringLiteral(":/icons/mcp/chart-line.svg")},
        {{"learn", "education", "tutorial", "course", "train"},      QStringLiteral(":/icons/mcp/graduationcap.svg")},
        {{"community", "social", "collab"},                          QStringLiteral(":/icons/mcp/users.svg")},
        {{"enterprise", "business", "org", "company"},               QStringLiteral(":/icons/mcp/briefcase.svg")},
        {{"package", "npm", "pip", "cargo", "module"},               QStringLiteral(":/icons/mcp/shippingbox.svg")},
        {{"web", "browser", "http", "html"},                         QStringLiteral(":/icons/mcp/globe.svg")},
        {{"debug", "fix", "troubleshoot", "repair"},                 QStringLiteral(":/icons/mcp/wrench.svg")},
        {{"automate", "workflow", "automation"},                      QStringLiteral(":/icons/mcp/zap.svg")},
        {{"code", "dev", "programming", "stack"},                    QStringLiteral(":/icons/mcp/chevron-left-forwardslash-chevron-right.svg")},
        {{"file", "document", "text", "pdf", "doc"},                 QStringLiteral(":/icons/mcp/doc-text.svg")},
        {{"email", "mail", "message", "chat", "slack"},              QStringLiteral(":/icons/mcp/envelope.svg")},
        {{"performance", "speed", "fast", "optimize"},               QStringLiteral(":/icons/mcp/zap.svg")},
        {{"git", "version", "branch", "repo"},                       QStringLiteral(":/icons/mcp/arrow-triangle-branch.svg")},
        {{"cloud", "aws", "azure", "gcp"},                           QStringLiteral(":/icons/mcp/cloud.svg")},
        {{"mobile", "ios", "android", "app"},                        QStringLiteral(":/icons/mcp/smartphone.svg")},
        {{"network", "connect", "wifi", "dns"},                      QStringLiteral(":/icons/mcp/wifi.svg")},
        {{"time", "schedule", "calendar"},                           QStringLiteral(":/icons/mcp/clock.svg")},
        {{"map", "location", "geo"},                                 QStringLiteral(":/icons/mcp/map-pin.svg")},
        {{"media", "video", "film", "stream"},                       QStringLiteral(":/icons/tools/film.svg")},
        {{"image", "photo", "picture", "screenshot"},                QStringLiteral(":/icons/tools/image.svg")},
        {{"config", "setting"},                                      QStringLiteral(":/icons/mcp/settings.svg")},
        {{"tool", "utility", "helper"},                              QStringLiteral(":/icons/mcp/wrench.svg")},
    };
    return rules;
}

// ─── Implementation ─────────────────────────────────────────────────────────

QString IconRegistry::normalize(const QString &key)
{
    QString normalized = key.toLower().trimmed();
    normalized.replace(QLatin1Char('_'), QLatin1Char('-'));
    return normalized;
}

QString IconRegistry::exactMatch(const QString &normalized)
{
    return iconTable().value(normalized);
}

QString IconRegistry::keywordMatch(const QString &normalized)
{
    // Split the key into words for matching (e.g., "data-engineering" → ["data", "engineering"])
    const QStringList words = normalized.split(QLatin1Char('-'), Qt::SkipEmptyParts);

    for (const auto &rule : keywordRules()) {
        for (const auto &keyword : rule.keywords) {
            // Check if any word in the key matches a keyword
            for (const auto &word : words) {
                if (word == keyword) {
                    return rule.iconPath;
                }
            }
            // Also check if the full normalized key contains the keyword
            if (normalized.contains(keyword)) {
                return rule.iconPath;
            }
        }
    }

    return {};
}

QString IconRegistry::iconPath(const QString &key)
{
    if (key.isEmpty())
        return {};

    const QString normalized = normalize(key);

    // 1. Exact match
    QString result = exactMatch(normalized);
    if (!result.isEmpty())
        return result;

    // 2. Keyword fallback
    result = keywordMatch(normalized);
    if (!result.isEmpty())
        return result;

    // 3. No match
    return {};
}

bool IconRegistry::hasIcon(const QString &key)
{
    return !iconPath(key).isEmpty();
}

QIcon IconRegistry::coloredIcon(const QString &key, const QColor &color, const QSize &size)
{
    QString path = iconPath(key);
    if (path.isEmpty())
        return {};

    QPixmap pixmap = QIcon(path).pixmap(size);
    if (pixmap.isNull())
        return {};

    // Replace all visible pixels with the target color, preserving alpha
    QPainter painter(&pixmap);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(pixmap.rect(), color);
    painter.end();

    return QIcon(pixmap);
}
