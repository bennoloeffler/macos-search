#include "MainWindow.h"

#include "ExcludeSettings.h"
#include "FolderSearchWorker.h"
#include "PathCacheManager.h"
#include "SearchField.h"
#include "SearchResultModel.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QFileInfo>
#include <QKeySequence>
#include <QLabel>
#include <QListView>
#include <QMenu>
#include <QProcess>
#include <QShortcut>
#include <QStatusBar>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setupUi();

    m_excludeSettings = new ExcludeSettings(this);

    m_cache = PathCacheManager::instance();
    m_cache->setExcludeSettings(m_excludeSettings);

    m_worker = new FolderSearchWorker(this);

    connect(m_searchField, &SearchField::searchTriggered,
            this, &MainWindow::onSearchTriggered);
    connect(m_worker, &FolderSearchWorker::resultsReady,
            this, &MainWindow::onResultsReady);
    connect(m_cache, &PathCacheManager::scanProgress,
            this, &MainWindow::onScanProgress);
    connect(m_cache, &PathCacheManager::scanComplete,
            this, &MainWindow::onScanComplete);
    connect(m_cache, &PathCacheManager::cacheUpdated,
            this, &MainWindow::onCacheUpdated);

    connect(m_listView, &QListView::activated,
            this, &MainWindow::onResultActivated);
    connect(m_listView, &QListView::doubleClicked,
            this, &MainWindow::onResultActivated);
    connect(m_listView, &QListView::customContextMenuRequested,
            this, &MainWindow::onListContextMenu);

    // ⌘⏎ on the focused row reveals in Finder.
    auto *revealShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), this);
    connect(revealShortcut, &QShortcut::activated, this, [this]() {
        onRevealInFinder(m_listView->currentIndex());
    });

    // ⌘C copies path of focused row.
    auto *copyShortcut = new QShortcut(QKeySequence::Copy, this);
    connect(copyShortcut, &QShortcut::activated, this, [this]() {
        onCopyPath(m_listView->currentIndex());
    });

    // ⌘L focuses search field.
    auto *focusShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_L), this);
    connect(focusShortcut, &QShortcut::activated, this, [this]() {
        m_searchField->setFocus();
    });
}

MainWindow::~MainWindow() = default;

void MainWindow::setupUi()
{
    setWindowTitle(tr("macos-search"));
    resize(720, 520);

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    m_searchField = new SearchField(central);
    m_searchField->setPlaceholderText(
        tr("Search files and folders — multiple words = AND"));
    layout->addWidget(m_searchField);

    m_listView = new QListView(central);
    m_listView->setUniformItemSizes(true);
    m_listView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_listView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_listView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_listView->setAlternatingRowColors(true);
    layout->addWidget(m_listView, /*stretch*/ 1);

    m_model = new SearchResultModel(this);
    m_listView->setModel(m_model);

    setCentralWidget(central);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setText(tr("Starting…"));
    statusBar()->addPermanentWidget(m_statusLabel);

    m_searchField->setFocus();
}

void MainWindow::startScan()
{
    if (m_cache) {
        m_cache->startScan();
    }
}

void MainWindow::onSearchTriggered(const QString &text)
{
    m_currentQuery = text.trimmed();
    refreshSearch();
}

void MainWindow::refreshSearch()
{
    if (m_currentQuery.isEmpty()) {
        m_model->clear();
        return;
    }
    m_worker->search(m_currentQuery);
}

void MainWindow::onResultsReady(const QList<SearchResult> &results)
{
    m_model->setResults(results);
    if (results.isEmpty() && !m_currentQuery.isEmpty()) {
        m_statusLabel->setText(tr("No results for \"%1\"").arg(m_currentQuery));
    } else {
        m_statusLabel->setText(tr("%n result(s)", nullptr, results.size()));
    }
}

void MainWindow::onScanProgress(int foldersIndexed, int foldersExcluded, const QString &currentFolder)
{
    Q_UNUSED(currentFolder);
    m_statusLabel->setText(
        tr("Indexing… %1 folders (%2 excluded)").arg(foldersIndexed).arg(foldersExcluded));
}

void MainWindow::onScanComplete(int totalFolders, int totalExcluded)
{
    m_statusLabel->setText(
        tr("Ready — %1 paths indexed (%2 excluded)").arg(totalFolders).arg(totalExcluded));
    // Re-run any active query now that the cache is fully built.
    if (!m_currentQuery.isEmpty()) {
        refreshSearch();
    }
}

void MainWindow::onCacheUpdated()
{
    // Live cache mutation (FSEvents-driven) — refresh active query if any.
    if (!m_currentQuery.isEmpty()) {
        refreshSearch();
    }
}

QString MainWindow::currentPathAt(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return {};
    }
    return m_model->pathAt(index.row());
}

void MainWindow::onResultActivated(const QModelIndex &index)
{
    const QString path = currentPathAt(index);
    if (path.isEmpty()) {
        return;
    }
    const QFileInfo info(path);
    if (info.isDir()) {
        // For folders: open in Finder. Files get opened in their default app.
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    } else {
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    }
}

void MainWindow::onRevealInFinder(const QModelIndex &index)
{
    const QString path = currentPathAt(index);
    if (path.isEmpty()) {
        return;
    }
    // `open -R` reveals the item in Finder (selects it inside its parent).
    QProcess::startDetached("/usr/bin/open", {"-R", path});
}

void MainWindow::onCopyPath(const QModelIndex &index)
{
    const QString path = currentPathAt(index);
    if (path.isEmpty()) {
        return;
    }
    QApplication::clipboard()->setText(path);
}

void MainWindow::onListContextMenu(const QPoint &pos)
{
    const QModelIndex index = m_listView->indexAt(pos);
    if (!index.isValid()) {
        return;
    }

    QMenu menu(this);
    QAction *openAct = menu.addAction(tr("Open"));
    QAction *revealAct = menu.addAction(tr("Reveal in Finder"));
    menu.addSeparator();
    QAction *copyAct = menu.addAction(tr("Copy Path"));

    QAction *chosen = menu.exec(m_listView->viewport()->mapToGlobal(pos));
    if (chosen == openAct) {
        onResultActivated(index);
    } else if (chosen == revealAct) {
        onRevealInFinder(index);
    } else if (chosen == copyAct) {
        onCopyPath(index);
    }
}
