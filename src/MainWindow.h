#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

class QLabel;
class QListView;
class QModelIndex;

class ExcludeSettings;
class FolderSearchWorker;
class PathCacheManager;
class SearchField;
class SearchResultModel;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    // Kick off the background scan of $HOME. Safe to call once after show().
    void startScan();

private slots:
    void onSearchTriggered(const QString &text);
    void onResultsReady(const QList<struct SearchResult> &results);
    void onScanProgress(int foldersIndexed, int foldersExcluded, const QString &currentFolder);
    void onScanComplete(int totalFolders, int totalExcluded);
    void onCacheUpdated();
    void onResultActivated(const QModelIndex &index);
    void onRevealInFinder(const QModelIndex &index);
    void onCopyPath(const QModelIndex &index);
    void onListContextMenu(const QPoint &pos);

private:
    void setupUi();
    void refreshSearch();
    QString currentPathAt(const QModelIndex &index) const;

    ExcludeSettings *m_excludeSettings = nullptr;
    PathCacheManager *m_cache = nullptr; // singleton, not owned
    FolderSearchWorker *m_worker = nullptr;
    SearchField *m_searchField = nullptr;
    QListView *m_listView = nullptr;
    SearchResultModel *m_model = nullptr;
    QLabel *m_statusLabel = nullptr;
    QString m_currentQuery;
};

#endif // MAINWINDOW_H
