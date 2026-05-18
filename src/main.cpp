#include "Autostart.h"
#include "ExcludeSettings.h"
#include "FirstRunDialog.h"
#include "FolderBrowserDialog.h"
#include "GlobalHotkey.h"
#include "PathCacheManager.h"
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

/// Resolve the priority-ordered list of paths to pre-scan at startup.
///
///   1. The default favorite (or $HOME if no default is set).
///   2. Each remaining favorite, in sidebar order.
///
/// Non-existent paths are dropped, duplicates collapsed. `/` is fine —
/// the cache's path-level excludes already skip `/System`, `/private`, etc.
QStringList resolveScanQueue()
{
    QSettings settings("Maude", "FolderBrowser");
    const QString defaultFav = FolderBrowserDialog::resolveDefaultStartPath();
    QStringList favorites = settings.value("favorites").toStringList();

    QStringList ordered;
    auto pushIfExists = [&ordered](const QString &raw) {
        const QString cleaned = QDir::cleanPath(raw);
        if (cleaned.isEmpty() || !QDir(cleaned).exists()) return;
        if (ordered.contains(cleaned)) return;
        ordered.append(cleaned);
    };
    pushIfExists(defaultFav);
    pushIfExists(QDir::homePath());
    for (const QString &p : favorites) {
        pushIfExists(p);
    }
    return ordered;
}

/// Drive the scan queue: scan the first path, and on `scanComplete`, expand
/// to the next. Implemented as a QObject so we can keep state across
/// queued-connection invocations without leaking lambdas.
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
    void start()
    {
        if (m_queue.isEmpty()) return;
        m_cache->startScan();  // upstream's startScan walks $HOME by default
        // After upstream startScan finishes, we chain via onScanComplete.
    }
private slots:
    void onScanComplete(int, int)
    {
        if (m_index >= m_queue.size()) return;
        const QString next = m_queue.at(m_index++);
        if (QDir::cleanPath(next) == QDir::cleanPath(QDir::homePath())) {
            // Already covered by startScan's $HOME default; skip ahead.
            onScanComplete(0, 0);
            return;
        }
        m_cache->expandTo(next);
    }
private:
    PathCacheManager *m_cache;
    QStringList m_queue;
    int m_index = 1;  // index 0 is $HOME (covered by startScan); start at 1
};

}  // namespace

#include "main.moc"

int main(int argc, char *argv[])
{
    QCoreApplication::setOrganizationName("v-und-s");
    QCoreApplication::setOrganizationDomain("v-und-s.de");
    QCoreApplication::setApplicationName("macos-search");

    QApplication app(argc, argv);
    QApplication::setStyle("macos");
    QApplication::setWindowIcon(QIcon(":/app/macos-search.png"));

    ThemeManager::instance()->initialize();

    auto *excludeSettings = new ExcludeSettings(&app);
    PathCacheManager::instance()->setExcludeSettings(excludeSettings);

    QObject::connect(&app, &QCoreApplication::aboutToQuit, [] {
        PathCacheManager::instance()->stopScan();
    });

    // Open the dialog at the resolved default start path. If the user has
    // marked a favorite as default that's where we open; otherwise $HOME.
    FolderBrowserDialog dialog(FolderBrowserDialog::resolveDefaultStartPath(),
                               nullptr);
    dialog.setWindowTitle(QObject::tr("Open Project / Folder"));
    dialog.setWindowFlag(Qt::Window, true);
    dialog.show();

    // First-run autostart prompt. Only fires in production builds (i.e. when
    // running from /Applications or ~/Applications). Dev runs from a build*
    // directory never prompt, so we don't accidentally register a transient
    // dev binary as the user's at-login app. See src/Autostart.cpp and
    // docs/todos.md TODO 5.
    if (Autostart::firstRunNeedsPrompt()) {
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
                     hotkey, [hotkey](bool enabled) {
                         if (enabled) hotkey->registerSummonChord();
                         else hotkey->unregisterSummonChord();
                     });

    // Pre-scan the user's favorites in priority order. Each `scanComplete`
    // triggers the next path via `expandTo()`. See docs/todos.md TODO 3.
    auto *scheduler = new ScanScheduler(PathCacheManager::instance(),
                                        resolveScanQueue(),
                                        &app);
    scheduler->start();

    return app.exec();
}
