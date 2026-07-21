#ifndef FILESEARCHWORKER_H
#define FILESEARCHWORKER_H

#include "FolderSearchWorker.h"  // for SearchResult

#include <QFuture>
#include <QObject>
#include <QString>
#include <QStringList>

class QTimer;

// Mirror of FolderSearchWorker, but reads from FileCacheManager and produces
// file SearchResults. Shares `FolderSearchWorker::fuzzyScore` (path-agnostic
// scoring) so the ranking model is identical across folders and files.
class FileSearchWorker : public QObject
{
    Q_OBJECT

public:
    explicit FileSearchWorker(QObject *parent = nullptr);
    ~FileSearchWorker() override;

    void search(const QString &query, const QString &rootPath = QString());
    void cancel();
    bool isSearching() const;

    void setIncludeHidden(bool include);
    bool includeHidden() const { return m_includeHidden; }

signals:
    void searchStarted();
    void resultsReady(const QList<SearchResult> &results);
    void searchFinished(int totalFound);

private slots:
    void performSearch();

private:
    // The actual O(n) cache scan + scoring. Runs on a background thread,
    // reads no member state (all inputs passed by value), so it's safe to
    // call while the GUI thread mutates the worker.
    static QList<SearchResult> computeResults(const QString &query,
                                              const QString &rootPath,
                                              bool includeHidden);

    QTimer *m_debounceTimer = nullptr;
    QString m_pendingQuery;
    QString m_pendingRootPath;
    bool m_searching = false;
    bool m_includeHidden = false;
    // Bumped on every performSearch()/cancel() (GUI thread only). A finished
    // background search whose generation != this is stale and gets dropped.
    quint64 m_generation = 0;
    QFuture<void> m_future;
};

#endif // FILESEARCHWORKER_H
