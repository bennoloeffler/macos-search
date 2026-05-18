#ifndef FOLDERSEARCHWORKER_H
#define FOLDERSEARCHWORKER_H

#include <QObject>
#include <QString>
#include <QStringList>

class QTimer;
class PathCacheManager;

// Search result with path and score
struct SearchResult {
    QString path;
    QString displayName;
    int score = 0;
};

// Thin wrapper that searches the PathCacheManager's in-memory cache
// Provides debouncing and result formatting
class FolderSearchWorker : public QObject
{
    Q_OBJECT

public:
    explicit FolderSearchWorker(QObject *parent = nullptr);
    ~FolderSearchWorker() override;

    void search(const QString &query, const QString &rootPath = QString());
    void cancel();
    bool isSearching() const;

    /// The cache always includes hidden folders; this presentation
    /// filter decides whether to surface them in results. Default false.
    void setIncludeHidden(bool include);
    bool includeHidden() const { return m_includeHidden; }

    // Scoring helper (public for testing)
    static int fuzzyScore(const QString &path, const QString &query, const QString &rootPath = QString());
    static bool folderMatchesQuery(const QString &folderName, const QString &query);
    /// Returns true if any path component (after the leading slash) starts
    /// with `.`. Public for testing.
    static bool pathIsHidden(const QString &path);

signals:
    void searchStarted();
    void resultsReady(const QList<SearchResult> &results);
    void searchFinished(int totalFound);

private slots:
    void performSearch();

private:
    QTimer *m_debounceTimer = nullptr;
    QString m_pendingQuery;
    QString m_pendingRootPath;
    bool m_searching = false;
    bool m_includeHidden = false;
};

#endif // FOLDERSEARCHWORKER_H
