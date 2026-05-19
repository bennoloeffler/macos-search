#include "Bench.h"

#include "ExcludeSettings.h"
#include "FileCacheManager.h"
#include "FileSearchWorker.h"
#include "FolderSearchWorker.h"
#include "PathCacheManager.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QRandomGenerator>
#include <QString>
#include <QStringList>
#include <QTextStream>
#include <QTimer>
#include <algorithm>
#include <vector>

namespace Bench {

namespace {

constexpr int kDefaultQueryCount = 100;
constexpr int kScanTimeoutSec = 120;

QString takeFlag(QStringList &args, const QString &name)
{
    // Accepts both `--name VALUE` and `--name=VALUE`.
    for (int i = 0; i < args.size(); ++i) {
        if (args[i] == name && i + 1 < args.size()) {
            const QString val = args[i + 1];
            args.removeAt(i + 1);
            args.removeAt(i);
            return val;
        }
        if (args[i].startsWith(name + QLatin1Char('='))) {
            const QString val = args[i].mid(name.size() + 1);
            args.removeAt(i);
            return val;
        }
    }
    return {};
}

QString pickRandomBasenameTerm(const QString &path)
{
    // Pick a 3–6 char substring from the basename. Falls back to "a"
    // if the basename is too short.
    const QString base = QFileInfo(path).fileName();
    if (base.length() < 3) return QStringLiteral("a");
    auto *rng = QRandomGenerator::global();
    const int maxLen = qMin(6, static_cast<int>(base.length()));
    const int len = 3 + rng->bounded(maxLen - 3 + 1);
    const int start = rng->bounded(static_cast<int>(base.length()) - len + 1);
    return base.mid(start, len).toLower();
}

QJsonObject percentiles(std::vector<double> times)
{
    if (times.empty()) return QJsonObject{};
    std::sort(times.begin(), times.end());
    auto pct = [&times](double p) {
        const size_t idx = qBound<size_t>(0,
            static_cast<size_t>(times.size() * p),
            times.size() - 1);
        return times[idx];
    };
    return QJsonObject{
        { "count", static_cast<int>(times.size()) },
        { "min_ms", times.front() },
        { "p50_ms", pct(0.50) },
        { "p95_ms", pct(0.95) },
        { "p99_ms", pct(0.99) },
        { "max_ms", times.back() }
    };
}

}  // anonymous

int runIfRequested(int argc, char *argv[])
{
    QStringList args;
    args.reserve(argc - 1);
    bool wantsBench = false;
    for (int i = 1; i < argc; ++i) {
        const QString a = QString::fromLocal8Bit(argv[i]);
        if (a == "--bench") wantsBench = true;
        else args.append(a);
    }
    if (!wantsBench) return -1;

    // Parse optional sub-flags.
    QString rootArg = takeFlag(args, "--bench-root");
    if (rootArg.isEmpty()) rootArg = QDir::homePath();
    QString queriesArg = takeFlag(args, "--bench-queries");
    const int queryCount = queriesArg.isEmpty()
                               ? kDefaultQueryCount
                               : qMax(10, queriesArg.toInt());

    QTextStream out(stdout);
    QTextStream err(stderr);
    err << "[bench] root=" << rootArg << " queries=" << queryCount << "\n";
    err.flush();

    // The cache stores absolute file/folder paths. The scan walks from the
    // configured root, populating both folder and file caches. We wait for
    // PathCacheManager::scanComplete or until the timeout elapses.
    ExcludeSettings settings;
    PathCacheManager::instance()->setExcludeSettings(&settings);

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, [&]() {
        err << "[bench] scan timed out after " << kScanTimeoutSec << "s\n";
        err.flush();
        loop.quit();
    });
    QObject::connect(PathCacheManager::instance(),
                     &PathCacheManager::scanComplete, &loop,
                     [&loop]() { loop.quit(); });

    QElapsedTimer scanTimer;
    scanTimer.start();
    timeout.start(kScanTimeoutSec * 1000);
    PathCacheManager::instance()->restartScanFrom(rootArg);
    loop.exec();
    const qint64 scanMs = scanTimer.elapsed();

    const int folderCount = PathCacheManager::instance()->folderCount();
    const int fileCount = FileCacheManager::instance()->fileCount();
    const bool fileCapReached = FileCacheManager::instance()->capReached();

    err << "[bench] scan done: " << folderCount << " folders, "
        << fileCount << " files, " << scanMs << " ms\n";
    err.flush();

    // Generate query terms from random cached paths.
    const QStringList folderPaths = PathCacheManager::instance()->cachedPaths();
    const QStringList filePaths = FileCacheManager::instance()->cachedFiles();
    auto *rng = QRandomGenerator::global();

    QStringList queries;
    queries.reserve(queryCount);
    for (int i = 0; i < queryCount; ++i) {
        const auto &pool = (i % 2 == 0 && !filePaths.isEmpty())
                               ? filePaths
                               : folderPaths;
        if (pool.isEmpty()) { queries.append(QStringLiteral("a")); continue; }
        queries.append(pickRandomBasenameTerm(pool.at(rng->bounded(pool.size()))));
    }

    // Run folder-cache queries.
    std::vector<double> folderTimes;
    folderTimes.reserve(queryCount);
    for (const QString &q : queries) {
        QElapsedTimer t; t.start();
        (void)PathCacheManager::instance()->search(q, QString(), 100);
        folderTimes.push_back(t.nsecsElapsed() / 1.0e6);
    }

    // Run file-cache queries.
    std::vector<double> fileTimes;
    fileTimes.reserve(queryCount);
    for (const QString &q : queries) {
        QElapsedTimer t; t.start();
        (void)FileCacheManager::instance()->search(q, QString(), 100);
        fileTimes.push_back(t.nsecsElapsed() / 1.0e6);
    }

    QJsonObject report{
        { "schema", 1 },
        { "timestamp", QDateTime::currentDateTimeUtc().toString(Qt::ISODate) },
        { "root", rootArg },
        { "scan",
          QJsonObject{
              { "wall_ms", scanMs },
              { "folder_count", folderCount },
              { "file_count", fileCount },
              { "file_cap_reached", fileCapReached } } },
        { "query_count", queryCount },
        { "folder_search", percentiles(folderTimes) },
        { "file_search", percentiles(fileTimes) }
    };

    QJsonDocument doc(report);
    out << doc.toJson(QJsonDocument::Indented);
    out.flush();

    // Stop any still-running scan threads cleanly before the QApplication
    // tears down — otherwise the background BFS workers race the singleton
    // destructors and the process crashes on exit.
    PathCacheManager::instance()->stopScan();
    return 0;
}

}  // namespace Bench
