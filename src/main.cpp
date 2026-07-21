#include "Autostart.h"
#include "Bench.h"
#include "ExcludeSettings.h"
#include "FirstRunDialog.h"
#include "FolderBrowserDialog.h"
#include "GlobalHotkey.h"
#include "PathCacheManager.h"
#include "ScanQueue.h"
#include "ThemeManager.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QIcon>
#include <QLineEdit>
#include <QSettings>
#include <QStringList>

namespace {

/// Resolve the priority-ordered list of paths to pre-scan at startup —
/// most-probable search targets first (see ScanQueue::build): the default
/// favorite, Desktop, Downloads, Documents, any real top-level Dropbox
/// folder, then the rest of home, then the remaining favorites.
///
/// Non-existent paths are dropped, duplicates collapsed. `/` is fine —
/// the cache's path-level excludes already skip `/System`, `/private`, etc.
QStringList resolveScanQueue()
{
    QSettings settings("Maude", "FolderBrowser");
    const QString home = QDir::homePath();

    // Real (non-symlink) top-level Dropbox dirs. Symlinked aliases like
    // ~/Dropbox → ~/VundS Dropbox are skipped — the scan never descends
    // into symlinks anyway.
    QStringList dropboxDirs;
    const QFileInfoList topLevel = QDir(home).entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
    for (const QFileInfo &info : topLevel) {
        if (info.fileName().contains(QStringLiteral("dropbox"),
                                     Qt::CaseInsensitive)) {
            dropboxDirs.append(info.absoluteFilePath());
        }
    }

    const QStringList candidates = ScanQueue::build(
        home,
        FolderBrowserDialog::resolveDefaultStartPath(),
        settings.value("favorites").toStringList(),
        dropboxDirs);

    QStringList ordered;
    for (const QString &c : candidates) {
        if (QDir(c).exists() && !ordered.contains(c)) ordered.append(c);
    }
    return ordered;
}

/// Drive the scan queue uniformly via `expandTo()` and chain on `scanComplete`.
///
/// History: an earlier version called `startScan()` first (which walks $HOME)
/// and started the queue at index 1, assuming HOME was covered. That broke
/// when `FolderBrowserDialog`'s constructor triggers a `PathSelector::pathChanged`
/// signal during initial navigateTo() → `setRootPath()` → `expandTo(initialDir)`.
/// If initialDir ≠ $HOME (e.g. the user set a non-HOME defaultFavorite), a scan
/// for initialDir is already running by the time the scheduler starts; the
/// scheduler's `startScan()` then no-ops, HOME is *never* scanned, and `/Users/benno`
/// never lands in `m_completedRoots` — so the Home favorite (and the Search-in
/// indicator pointing at HOME) was stuck showing "Scan now" forever.
///
/// New behaviour: walk the queue from index 0, calling `expandTo()` on each
/// path. If the path is already scanned (covered by an earlier completedRoot)
/// skip it; if `expandTo()` is a no-op (no real scan started, no completion
/// signal will fire), advance immediately to the next; otherwise yield until
/// the corresponding `scanComplete` arrives.
class ScanScheduler : public QObject
{
    Q_OBJECT
public:
    ScanScheduler(PathCacheManager *cache, QStringList queue, QObject *parent)
        : QObject(parent), m_cache(cache), m_queue(std::move(queue))
    {
        connect(cache, &PathCacheManager::scanComplete,
                this, &ScanScheduler::onScanComplete);
    }
    void start() { advance(); }
private slots:
    void onScanComplete(int, int) { advance(); }
private:
    void advance()
    {
        while (m_index < m_queue.size()) {
            const QString next = m_queue.at(m_index++);
            if (m_cache->isPathScanned(next)) continue;  // covered, skip
            m_cache->expandTo(next);
            // If a scan is now in flight, yield and wait for scanComplete.
            // Otherwise expandTo was a no-op (path already known but not in
            // a completedRoot) — keep walking the queue.
            if (m_cache->isScanning()) return;
        }
    }

    PathCacheManager *m_cache;
    QStringList m_queue;
    int m_index = 0;
};

}  // namespace

#include "main.moc"

int main(int argc, char *argv[])
{
    QCoreApplication::setOrganizationName("v-und-s");
    QCoreApplication::setOrganizationDomain("v-und-s.de");
    QCoreApplication::setApplicationName("macos-search");

    // --bench short-circuits the GUI. Runs the benchmark, prints JSON, exits.
    // Set offscreen platform proactively so the benchmark doesn't try to
    // talk to a window server.
    {
        for (int i = 1; i < argc; ++i) {
            if (QString::fromLocal8Bit(argv[i]) == QStringLiteral("--bench")) {
                qputenv("QT_QPA_PLATFORM", "offscreen");
                break;
            }
        }
    }

    QApplication app(argc, argv);
    QApplication::setStyle("macos");
    QApplication::setWindowIcon(QIcon(":/app/macos-search.png"));

    {
        const int rc = Bench::runIfRequested(argc, argv);
        if (rc >= 0) return rc;
    }

    // Belt + braces: keep the app alive even if the main dialog hides
    // momentarily (e.g. an open-with-default-app call that, on some macOS
    // builds, briefly steals focus). The dialog is the long-lived window;
    // exit happens via Cmd-Q.
    QApplication::setQuitOnLastWindowClosed(false);

    ThemeManager::instance()->initialize();

    auto *excludeSettings = new ExcludeSettings(&app);
    PathCacheManager::instance()->setExcludeSettings(excludeSettings);

    QObject::connect(&app, &QCoreApplication::aboutToQuit, [] {
        PathCacheManager::instance()->stopScan();
        // Persist the freshest index for the next warm start (docs/210).
        PathCacheManager::instance()->saveSnapshot();
    });

    // Window title is hotkey-aware: when ⌃⌥⇧S is enabled we surface that
    // in the title bar so the user discovers the chord without opening
    // Preferences. Reactive — flipping the Preferences toggle updates this.
    auto titleForHotkey = [](bool enabled) -> QString {
        return enabled
            ? QObject::tr("Open Project / Folder  ·  ⌃⌥⇧S to show/activate")
            : QObject::tr("Open Project / Folder");
    };
    const bool hotkeyEnabledInitial = QSettings("Maude", "FolderBrowser")
        .value("hotkeyEnabled", true).toBool();

    // Open the dialog at the resolved default start path. If the user has
    // marked a favorite as default that's where we open; otherwise $HOME.
    FolderBrowserDialog dialog(FolderBrowserDialog::resolveDefaultStartPath(),
                               nullptr);
    dialog.setWindowTitle(titleForHotkey(hotkeyEnabledInitial));
    dialog.setWindowFlag(Qt::Window, true);
    dialog.show();

    // First-run autostart prompt. Only fires in production builds (i.e. when
    // running from /Applications or ~/Applications). Dev runs from a build*
    // directory never prompt, so we don't accidentally register a transient
    // dev binary as the user's at-login app. See src/Autostart.cpp and
    // docs/todos.md TODO 5.
    if (Autostart::firstRunNeedsPrompt()) {
        // Mark BEFORE exec: "ask once" must hold even if the app is quit
        // while the prompt is open (exec never returns then, and the
        // prompt used to reappear on every launch). Autostart itself
        // remains changeable in Preferences.
        Autostart::markFirstRunCompleted();
        FirstRunDialog firstRun(&dialog);
        firstRun.exec();
        Autostart::applyFirstRunChoice(firstRun.result() == QDialog::Accepted);
    }

    if (auto *searchField = dialog.findChild<QLineEdit *>("searchField")) {
        searchField->setFocus();
    }

    // Global hotkey ⌃⌥⇧S to summon the dialog from anywhere. Gated on the
    // "hotkeyEnabled" QSettings key (default ON). See docs/todos.md TODO 6.
    auto *hotkey = new GlobalHotkey(&app);
    {
        QSettings hkSettings("Maude", "FolderBrowser");
        const bool hotkeyEnabled =
            hkSettings.value("hotkeyEnabled", true).toBool();
        if (hotkeyEnabled) hotkey->registerSummonChord();
    }
    QObject::connect(hotkey, &GlobalHotkey::summonRequested,
                     &dialog, &FolderBrowserDialog::summon);

    // Let the Preferences dialog (TODO 7) flip the hotkey live.
    dialog.setGlobalHotkey(hotkey);
    QObject::connect(&dialog, &FolderBrowserDialog::hotkeyPreferenceChanged,
                     hotkey, [hotkey, &dialog, titleForHotkey](bool enabled) {
                         if (enabled) hotkey->registerSummonChord();
                         else hotkey->unregisterSummonChord();
                         dialog.setWindowTitle(titleForHotkey(enabled));
                     });

    // Warm start (docs/210): load the last index snapshot before any scan
    // runs. On a fingerprint match the store is instantly searchable; the
    // pre-scan below then reconciles it against the current filesystem. A
    // miss (or first run) is harmless — the scan builds from cold as before.
    PathCacheManager::tryLoadSnapshot();

    // Pre-scan the user's favorites in priority order. Each `scanComplete`
    // triggers the next path via `expandTo()`. See docs/todos.md TODO 3.
    auto *scheduler = new ScanScheduler(PathCacheManager::instance(),
                                        resolveScanQueue(),
                                        &app);
    scheduler->start();

    return app.exec();
}
