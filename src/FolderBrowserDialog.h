#ifndef FOLDERBROWSERDIALOG_H
#define FOLDERBROWSERDIALOG_H

#include <QDialog>
#include "FolderSearchWorker.h"

class QVBoxLayout;
class QHBoxLayout;
class QLabel;
class QTreeView;
class QPushButton;
class QFileSystemModel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QStackedWidget;
class FolderSearchWorker;
class ExcludeSettings;
class PathSelector;
class GlobalHotkey;

class FolderBrowserDialog : public QDialog
{
    Q_OBJECT

public:
    explicit FolderBrowserDialog(const QString &initialDir, QWidget *parent = nullptr);
    ~FolderBrowserDialog() override;

    QString selectedPath() const;

    static QString getExistingDirectory(QWidget *parent,
                                        const QString &caption,
                                        const QString &dir);

    /// Resolves the persisted default favorite (or Home as fallback) to
    /// a usable startup path. Public so main.cpp and tests can call it
    /// without instantiating a dialog.
    static QString resolveDefaultStartPath();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

public slots:
    /// Standalone-app drift: navigate the picker to a specific path.
    /// Used by tests and by the favorites sidebar; exposed as a slot so
    /// QMetaObject::invokeMethod works.
    void setCurrentRoot(const QString &path);

    /// Summon the dialog: show + raise + activate, then focus the search
    /// field and selectAll so the next keystroke replaces the previous
    /// query. Used by the global ⌃⌥⇧S hotkey (see src/GlobalHotkey.h).
    /// Idempotent — calling while already focused is a no-op aside from
    /// the selectAll().
    void summon();

    /// Hand the GlobalHotkey instance to the dialog so the Preferences
    /// dialog (TODO 7) can flip the chord live. Set from main.cpp after
    /// constructing both. nullptr means "no live hotkey wiring".
    void setGlobalHotkey(GlobalHotkey *hotkey);

signals:
    /// Emitted when the Preferences dialog toggles the hotkey checkbox.
    /// main.cpp listens and forwards to GlobalHotkey::register/unregister.
    void hotkeyPreferenceChanged(bool enabled);

private slots:
    void onFolderClicked(const QModelIndex &index);
    void onFolderDoubleClicked(const QModelIndex &index);
    void onChooseClicked();          // legacy — kept for keyPress dispatch
    void onOpenInFinderClicked();    // standalone-app drift: reveal in Finder
    void onOpenInAppClicked();       // standalone-app drift: open with default app
    void onUpClicked();
    void onHomeClicked();
    void onSearchTextChanged(const QString &text);
    void onSearchResultsReady(const QList<SearchResult> &results);
    void onSearchResultClicked(QListWidgetItem *item);
    void onSearchResultDoubleClicked(QListWidgetItem *item);
    void onExcludeButtonClicked();
    void onShowHiddenToggled(bool checked);
    void onCacheStatusChanged();
    void onPathSelectorChanged(const QString &path);

private:
    void setupUi();
    void navigateTo(const QString &path);
    void updateUpButtonState();
    void updateCacheStatusLabel();
    void updateResolvedPathLabel();
    void setRootPath(const QString &path);
    void triggerSearch();
    QString resolvedPath() const;
    void saveSettings();
    void loadSettings();
    QString highlightMatches(const QString &path, const QString &query);

    QVBoxLayout *m_mainLayout = nullptr;
    QLabel *m_titleLabel = nullptr;
    QWidget *m_navigationToolbar = nullptr;
    QPushButton *m_upButton = nullptr;
    QPushButton *m_homeButton = nullptr;

    // Search UI
    QWidget *m_searchContainer = nullptr;
    QLineEdit *m_searchField = nullptr;
    QPushButton *m_excludeButton = nullptr;
    QPushButton *m_showHiddenButton = nullptr;
    QLabel *m_cacheStatusLabel = nullptr;
    bool m_showHidden = false;

    // Standalone-app drift: Finder-style sidebar of favorite roots.
    //   - Home is always present and is the implicit fallback default.
    //   - User-added favorites persist as QStringList "favorites".
    //   - One favorite (Home or user-added) can be marked the default —
    //     that's the path used at app startup.
    //   - Right-click on a row opens a mini context menu: "Make Default"
    //     and "Delete" (Delete hidden for Home).
    QListWidget *m_favoritesList = nullptr;
    QPushButton *m_addFavoriteButton = nullptr;
    QStringList m_favoritePaths;       // persisted list of user-added paths
    QString m_defaultFavorite;         // empty → Home is default
    void loadFavorites();
    void saveFavorites();
    void rebuildFavoritesList();
    void addCurrentRootAsFavorite();
    void removeFavorite(const QString &path);
    void setDefaultFavorite(const QString &path);
    void onFavoriteRowActivated(QListWidgetItem *item);
    void onFavoritesContextMenu(const QPoint &pos);
    // (resolveDefaultStartPath is declared in the public: block above)

    // Root path UI (PathSelector handles all completion logic)
    QWidget *m_rootContainer = nullptr;
    PathSelector *m_pathSelector = nullptr;
    QString m_rootPath; // Last confirmed valid path (for search scope)

    // View stack (tree view or search results)
    QStackedWidget *m_viewStack = nullptr;
    QTreeView *m_folderTreeView = nullptr;
    QListWidget *m_searchResultsList = nullptr;

    // Resolved path preview
    QLabel *m_resolvedPathLabel = nullptr;

    // Standalone-app drift: single "Choose" → two "Open" buttons.
    QPushButton *m_openInFinderButton = nullptr;
    QPushButton *m_openInAppButton = nullptr;
    QPushButton *m_chooseButton = nullptr; // alias of m_openInAppButton
    QPushButton *m_cancelButton = nullptr;
    QFileSystemModel *m_fileSystemModel = nullptr;

    // Search worker
    FolderSearchWorker *m_searchWorker = nullptr;
    ExcludeSettings *m_excludeSettings = nullptr;
    QString m_lastSearchQuery;

    QString m_currentPath;
    QString m_selectedPath;

    // Optional handle to the global hotkey, set from main.cpp. Used by
    // the Preferences dialog (TODO 7) to flip the chord live.
    GlobalHotkey *m_globalHotkey = nullptr;

    // Reentrancy guard for keyPressEvent's arrow-key forwarding. Qt
    // propagates unaccepted KeyPress events up the parent chain, so a
    // forwarded arrow that the target doesn't accept bubbles back into
    // this dialog and would otherwise recurse until the stack guard
    // page kills the process.
    bool m_inKeyForward = false;
};

#endif // FOLDERBROWSERDIALOG_H
