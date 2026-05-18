#include "ExcludeSettings.h"
#include "FolderBrowserDialog.h"
#include "PathCacheManager.h"
#include "ThemeManager.h"

#include <QApplication>
#include <QCoreApplication>
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

    if (auto *searchField = dialog.findChild<QLineEdit *>("searchField")) {
        searchField->setFocus();
    }

    // Pre-scan the user's favorites in priority order. Each `scanComplete`
    // triggers the next path via `expandTo()`. See docs/todos.md TODO 3.
    auto *scheduler = new ScanScheduler(PathCacheManager::instance(),
                                        resolveScanQueue(),
                                        &app);
    scheduler->start();

    return app.exec();
}
