#ifndef FOLDERBROWSERDIALOG_H
#define FOLDERBROWSERDIALOG_H

#include <QDialog>
#include <QDir>
#include "FolderSearchWorker.h"

class QVBoxLayout;
class QHBoxLayout;
class QLabel;
class QTreeView;
class QPushButton;
class QFileSystemModel;
class QTimer;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QStackedWidget;
class QCheckBox;
class FolderSearchWorker;
class FileSearchWorker;
class ExcludeSettings;
class ContentSearchSettings;
class RipgrepRunner;
class PathSelector;
class GlobalHotkey;
class ScanStateIndicator;
struct ContentMatch;

class FolderBrowserDialog : public QDialog
{
    Q_OBJECT

public:
    enum class SearchMode { Folders, Files, Both };

    explicit FolderBrowserDialog(const QString &initialDir, QWidget *parent = nullptr);
    ~FolderBrowserDialog() override;

    QString selectedPath() const;

    static QString getExistingDirectory(QWidget *parent,
                                        const QString &caption,
                                        const QString &dir);

    static QString resolveDefaultStartPath();

    // Public for tests.
    SearchMode searchMode() const { return m_searchMode; }
    void setSearchMode(SearchMode m);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

public slots:
    void setCurrentRoot(const QString &path);
    void summon();
    void setGlobalHotkey(GlobalHotkey *hotkey);

signals:
    void hotkeyPreferenceChanged(bool enabled);

private slots:
    void onFolderClicked(const QModelIndex &index);
    void onFolderDoubleClicked(const QModelIndex &index);
    void onChooseClicked();
    void onOpenInFinderClicked();
    void onOpenInAppClicked();
    void onUpClicked();
    void onHomeClicked();
    void onSearchTextChanged(const QString &text);
    void onSearchResultsReady(const QList<SearchResult> &results);
    void onFileSearchResultsReady(const QList<SearchResult> &results);
    void onSearchResultClicked(QListWidgetItem *item);
    void onSearchResultDoubleClicked(QListWidgetItem *item);
    void onExcludeButtonClicked();
    void onShowHiddenToggled(bool checked);
    void onCacheStatusChanged();
    void onFileCapReached();
    void onPathSelectorChanged(const QString &path);
    void onScanRequested(const QString &path);
    void refreshScanStateIndicators();
    void onSearchModeChanged();
    void onContentTextChanged(const QString &text);
    void onContentRegexToggled(bool on);
    void onContentHelpClicked();
    void onContentMatches(const QList<ContentMatch> &matches);
    void onContentFinished(int total);

public:
    // Test seam: suppress the `/usr/bin/open` shell-out so the aggregate
    // test runner never spawns Finder/app windows when exercising the
    // open/reveal slots. The app itself never sets this. Set once in
    // tests/test_main.cpp.
    static void setShellOpenSuppressedForTests(bool suppress);

private:
    // All Finder/app launches funnel through here so the test seam above
    // gates every one. `args` is the argv passed to /usr/bin/open.
    void launchOpen(const QStringList &args);
    static bool s_shellOpenSuppressed;

    // Open `path` with its default app. Every FILE-open goes through here:
    // if the file is an online-only cloud placeholder (Dropbox/iCloud/
    // OneDrive — see CloudFileState), first announce
    // "⬇ Downloading X MB — may take some seconds…" in the resolved-path
    // label, then let /usr/bin/open materialize it (LaunchServices reads the
    // bytes, which triggers the provider download). A light poll flips the
    // label to "✓ Downloaded" when the content lands.
    void openPathWithApp(const QString &path);
    void announceCloudDownload(const QString &path);
    void onDownloadPollTick();
    // After a cloud download materializes `path`, push the fresh size into
    // any visible result rows for it — the orange "☁ 0 bytes" flips to the
    // real size without waiting for the next search rebuild.
    void refreshResultRowsForPath(const QString &path);
    QTimer *m_downloadPollTimer = nullptr;
    QString m_downloadPollPath;
    int m_downloadPollTicks = 0;

    void setupUi();
    void navigateTo(const QString &path);
    void updateUpButtonState();
    void updateCacheStatusLabel();
    void updateResolvedPathLabel();
    void setRootPath(const QString &path);
    void triggerSearch();
    void triggerContentSearch();
    void rebuildMergedResults();
    void updateContentFieldState();
    QString resolvedPath() const;
    void saveSettings();
    void loadSettings();
    // Compute the tree-view filter based on the current search mode and the
    // eye toggle. Files mode / Both mode include QDir::Files; Folders mode
    // stays folders-only.
    QDir::Filters treeViewFilters() const;
    void applyTreeViewFilter();

    QVBoxLayout *m_mainLayout = nullptr;
    QWidget *m_navigationToolbar = nullptr;
    QPushButton *m_upButton = nullptr;
    QPushButton *m_homeButton = nullptr;

    // Search UI
    QWidget *m_searchContainer = nullptr;
    QLineEdit *m_searchField = nullptr;
    QPushButton *m_excludeButton = nullptr;
    QPushButton *m_showHiddenButton = nullptr;
    ScanStateIndicator *m_searchInIndicator = nullptr;
    QLabel *m_cacheStatusLabel = nullptr;
    bool m_showHidden = false;

    // Mode toggle (Folders / Files / Both)
    QPushButton *m_modeFolders = nullptr;
    QPushButton *m_modeFiles = nullptr;
    QPushButton *m_modeBoth = nullptr;
    SearchMode m_searchMode = SearchMode::Both;

    // Content-search row (visible all the time, gated by threshold).
    QWidget *m_contentContainer = nullptr;
    QLineEdit *m_contentField = nullptr;
    QCheckBox *m_contentRegex = nullptr;
    QPushButton *m_contentHelpButton = nullptr;
    QLabel *m_contentHintLabel = nullptr;

    // Favorites sidebar
    QListWidget *m_favoritesList = nullptr;
    QPushButton *m_addFavoriteButton = nullptr;
    QStringList m_favoritePaths;
    QString m_defaultFavorite;
    void loadFavorites();
    void saveFavorites();
    void rebuildFavoritesList();
    void addCurrentRootAsFavorite();
    void removeFavorite(const QString &path);
    void setDefaultFavorite(const QString &path);
    void onFavoriteRowActivated(QListWidgetItem *item);
    void onFavoritesContextMenu(const QPoint &pos);

    // Root path
    QWidget *m_rootContainer = nullptr;
    PathSelector *m_pathSelector = nullptr;
    QString m_rootPath;

    // View stack
    QStackedWidget *m_viewStack = nullptr;
    QTreeView *m_folderTreeView = nullptr;
    QListWidget *m_searchResultsList = nullptr;

    // Resolved path preview
    QLabel *m_resolvedPathLabel = nullptr;

    QPushButton *m_openInFinderButton = nullptr;
    QPushButton *m_openInAppButton = nullptr;
    QPushButton *m_chooseButton = nullptr;
    QPushButton *m_cancelButton = nullptr;
    QFileSystemModel *m_fileSystemModel = nullptr;

    // Search workers
    FolderSearchWorker *m_searchWorker = nullptr;
    FileSearchWorker *m_fileSearchWorker = nullptr;
    ExcludeSettings *m_excludeSettings = nullptr;

    // Content search
    ContentSearchSettings *m_contentSettings = nullptr;
    RipgrepRunner *m_ripgrep = nullptr;

    QString m_lastSearchQuery;
    QList<SearchResult> m_lastFolderResults;
    QList<SearchResult> m_lastFileResults;
    // Map from file path → list of (line, snippet, matchStart, matchEnd).
    QMap<QString, QList<ContentMatch>> m_contentMatchesByFile;
    int m_contentMatchTotal = 0;
    bool m_contentBusy = false;
    // The line number selected via a content-match child row (or 0 if none).
    int m_selectedLineNumber = 0;

    QString m_currentPath;
    QString m_selectedPath;

    GlobalHotkey *m_globalHotkey = nullptr;

    bool m_inKeyForward = false;
};

#endif // FOLDERBROWSERDIALOG_H
