#include "FolderBrowserDialog.h"
#include "ContentSearchSettings.h"
#include "EditorLauncher.h"
#include "ExcludeSettings.h"
#include "ExcludeSettingsDialog.h"
#include "FileCacheManager.h"
#include "FileSearchWorker.h"
#include "FolderSearchWorker.h"
#include "IconRegistry.h"
#include "PathCacheManager.h"
#include "PathSelector/FileSystemAdapter.h"
#include "PathSelector/PathSelector.h"
#include "PreferencesDialog.h"
#include "RipgrepRunner.h"
#include "SearchResultDelegate.h"
#include "SwiftUIStyle.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QToolTip>
#include <QTreeView>
#include <QVBoxLayout>
#include <algorithm>

FolderBrowserDialog::FolderBrowserDialog(const QString &initialDir, QWidget *parent)
    : QDialog(parent)
    , m_currentPath(initialDir)
{
    setObjectName("FolderBrowserDialog");
    setWindowTitle(tr("Select Project Folder"));
    // Standalone-app drift: width raised to 820 to fit the segmented mode
    // control + content-search row without crowding the favorites sidebar.
    setMinimumSize(820, 560);
    resize(980, 700);

    qRegisterMetaType<QList<ContentMatch>>("QList<ContentMatch>");

    m_excludeSettings = new ExcludeSettings(this);
    m_contentSettings = new ContentSearchSettings(this);

    m_searchWorker = new FolderSearchWorker(this);
    m_fileSearchWorker = new FileSearchWorker(this);
    m_ripgrep = new RipgrepRunner(this);

    // Honour the persisted cap before any scanning kicks off.
    FileCacheManager::instance()->setCapLimit(m_contentSettings->fileCacheCap());

    setupUi();
    loadSettings();
    navigateTo(initialDir);
    updateResolvedPathLabel();

    connect(PathCacheManager::instance(), &PathCacheManager::scanStarted,
            this, &FolderBrowserDialog::onCacheStatusChanged);
    connect(PathCacheManager::instance(), &PathCacheManager::scanProgress,
            this, &FolderBrowserDialog::onCacheStatusChanged);
    connect(PathCacheManager::instance(), &PathCacheManager::scanComplete,
            this, &FolderBrowserDialog::onCacheStatusChanged);
    connect(PathCacheManager::instance(), &PathCacheManager::cacheUpdated,
            this, &FolderBrowserDialog::onCacheStatusChanged);
    connect(FileCacheManager::instance(), &FileCacheManager::capReachedSignal,
            this, &FolderBrowserDialog::onFileCapReached);

    updateCacheStatusLabel();
    updateContentFieldState();
}

FolderBrowserDialog::~FolderBrowserDialog()
{
    // Disconnect from PathCacheManager - it's a singleton that outlives us.
    // Its background scan thread emits signals via QueuedConnection that could
    // arrive during our destruction.
    disconnect(PathCacheManager::instance(), nullptr, this, nullptr);

    // Cancel any pending debounced search (timer could fire during destruction)
    if (m_searchWorker) {
        m_searchWorker->cancel();
    }

    // Detach the QFileSystemModel from the tree view before destruction.
    // Qt does NOT guarantee child destruction order, so the model could be
    // freed before the tree view. Detaching prevents use-after-free.
    if (m_folderTreeView) {
        m_folderTreeView->setModel(nullptr);
    }
}

QString FolderBrowserDialog::selectedPath() const
{
    return m_selectedPath;
}

QString FolderBrowserDialog::getExistingDirectory(QWidget *parent,
                                                   const QString &caption,
                                                   const QString &dir)
{
    FolderBrowserDialog dialog(dir, parent);
    if (!caption.isEmpty()) {
        dialog.setWindowTitle(caption);
    }
    dialog.setWindowModality(Qt::WindowModal);

    if (dialog.exec() == QDialog::Accepted) {
        return dialog.selectedPath();
    }
    return QString();
}

bool FolderBrowserDialog::eventFilter(QObject *obj, QEvent *event)
{
    // Steal printable keystrokes from the tree view's built-in
    // keyboardSearch so they end up in the search field instead.
    if (obj == m_folderTreeView && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        const auto mods = ke->modifiers();
        const bool cmd = mods & (Qt::ControlModifier | Qt::MetaModifier);
        if (!cmd && ke->text().length() == 1 && ke->text().at(0).isPrint()) {
            QKeyEvent fwd(QEvent::KeyPress, ke->key(), mods, ke->text());
            QApplication::sendEvent(this, &fwd);
            return true;
        }
    }
    return QDialog::eventFilter(obj, event);
}

void FolderBrowserDialog::keyPressEvent(QKeyEvent *event)
{
    const auto mods = event->modifiers();
    const bool cmd = mods & (Qt::ControlModifier | Qt::MetaModifier);
    const int  key = event->key();

    // ── Global chords (work regardless of focused widget) ───────────────
    if (key == Qt::Key_G && (mods & Qt::ShiftModifier) && cmd) {
        m_pathSelector->focusPathField();
        event->accept(); return;
    }
    if (key == Qt::Key_L && cmd) {
        m_pathSelector->focusPathField();
        event->accept(); return;
    }
    if (key == Qt::Key_F && cmd) {
        m_searchField->setFocus();
        m_searchField->selectAll();
        event->accept(); return;
    }
    if (key == Qt::Key_Up && cmd) {
        onUpClicked();
        event->accept(); return;
    }
    if (key == Qt::Key_H && cmd) {
        onHomeClicked();
        event->accept(); return;
    }
    if ((key == Qt::Key_Return || key == Qt::Key_Enter) && cmd) {
        onOpenInFinderClicked();
        event->accept(); return;
    }
    // ⌥⏎ → open at line in VS Code (best-effort; falls back to `open`).
    if ((key == Qt::Key_Return || key == Qt::Key_Enter)
        && (mods & Qt::AltModifier)) {
        const QString path = resolvedPath();
        if (!path.isEmpty()) {
            EditorLauncher::openAtLine(path, m_selectedLineNumber);
        }
        event->accept(); return;
    }
    if ((key == Qt::Key_Return || key == Qt::Key_Enter) && !cmd) {
        onOpenInAppClicked();
        event->accept(); return;
    }

    // ── Escape — eat always so the dialog never closes via Esc ─────────
    if (key == Qt::Key_Escape) {
        if (m_searchField && !m_searchField->text().isEmpty()) {
            m_searchField->clear();
            m_searchField->setFocus();
        }
        event->accept();
        return;  // Don't delegate to QDialog::keyPressEvent → reject().
    }

    // ── ↑/↓/PgUp/PgDn — drive the visible view even when the search ────
    //    field has focus, so type-then-arrow works without grabbing
    //    the mouse. We forward to whichever view is visible, but never
    //    steal arrows from the favorites list.
    if ((key == Qt::Key_Up || key == Qt::Key_Down ||
         key == Qt::Key_PageUp || key == Qt::Key_PageDown) && !cmd) {
        QWidget *fw = focusWidget();
        const bool inFavorites = fw == m_favoritesList ||
                                 (fw && fw->parent() == m_favoritesList);
        if (!inFavorites && !m_inKeyForward) {
            QWidget *target = (m_viewStack->currentWidget() == m_searchResultsList)
                                   ? static_cast<QWidget *>(m_searchResultsList)
                                   : static_cast<QWidget *>(m_folderTreeView);
            QKeyEvent fwd(QEvent::KeyPress, key, mods, event->text());
            m_inKeyForward = true;
            QApplication::sendEvent(target, &fwd);
            m_inKeyForward = false;
            event->accept();
            return;
        }
        if (m_inKeyForward) {
            // Forwarded event bubbled back up; swallow to avoid recursion.
            event->accept();
            return;
        }
    }

    // ── Printable character → APPEND to the search field. (The old
    //    behavior replaced the whole field — typing "abc" gave "c"!)
    if (!m_searchField->hasFocus() &&
        event->text().length() == 1 && event->text().at(0).isPrint() &&
        !cmd) {
        const QString existing = m_searchField->text();
        m_searchField->setFocus();
        m_searchField->setText(existing + event->text());
        m_searchField->setCursorPosition(m_searchField->text().length());
        event->accept();
        return;
    }
    QDialog::keyPressEvent(event);
}


void FolderBrowserDialog::setupUi()
{
    // Main layout with SwiftUI-like spacing and margins (increased)
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setObjectName("mainLayout");
    m_mainLayout->setSpacing(SwiftUIStyle::SpacingMedium);
    m_mainLayout->setContentsMargins(SwiftUIStyle::SpacingLarge,  // 24px margins
                                      SwiftUIStyle::SpacingLarge,
                                      SwiftUIStyle::SpacingLarge,
                                      SwiftUIStyle::SpacingLarge);

    // Title (20pt, DemiBold)
    m_titleLabel = new QLabel(tr("Select Folder"), this);
    m_titleLabel->setObjectName("titleLabel");
    QFont titleFont = m_titleLabel->font();
    titleFont.setPointSize(20);
    titleFont.setWeight(QFont::DemiBold);
    m_titleLabel->setFont(titleFont);
    m_titleLabel->setAlignment(Qt::AlignCenter);
    m_mainLayout->addWidget(m_titleLabel);

    // Navigation toolbar
    m_navigationToolbar = new QWidget(this);
    m_navigationToolbar->setObjectName("navigationToolbar");
    auto *toolbarLayout = new QHBoxLayout(m_navigationToolbar);
    toolbarLayout->setContentsMargins(0, 0, 0, 0);
    toolbarLayout->setSpacing(SwiftUIStyle::SpacingSmall);

    // Up button with icon
    m_upButton = new QPushButton(this);
    m_upButton->setObjectName("upButton");
    m_upButton->setFlat(true);
    m_upButton->setIcon(IconRegistry::coloredIcon("chevron-up", SwiftUIStyle::primaryTextQColor()));
    m_upButton->setIconSize(QSize(16, 16));
    m_upButton->setToolTip(tr("Go to parent folder"));
    m_upButton->setFixedSize(32, 32);
    m_upButton->setCursor(Qt::PointingHandCursor);
    m_upButton->setStyleSheet(SwiftUIStyle::secondaryButtonStyleSheet());

    // Home button with icon
    m_homeButton = new QPushButton(this);
    m_homeButton->setObjectName("homeButton");
    m_homeButton->setFlat(true);
    m_homeButton->setIcon(IconRegistry::coloredIcon("home", SwiftUIStyle::primaryTextQColor()));
    m_homeButton->setIconSize(QSize(16, 16));
    m_homeButton->setToolTip(tr("Go to home folder"));
    m_homeButton->setFixedSize(32, 32);
    m_homeButton->setCursor(Qt::PointingHandCursor);
    m_homeButton->setStyleSheet(SwiftUIStyle::secondaryButtonStyleSheet());

    toolbarLayout->addWidget(m_upButton);
    toolbarLayout->addWidget(m_homeButton);

    // Cache status label (in toolbar row)
    m_cacheStatusLabel = new QLabel(this);
    m_cacheStatusLabel->setObjectName("cacheStatusLabel");
    m_cacheStatusLabel->setStyleSheet(QString("color: %1; font-size: 11px; padding-left: 12px;").arg(SwiftUIStyle::secondaryTextColor()));
    toolbarLayout->addWidget(m_cacheStatusLabel);

    toolbarLayout->addStretch();

    // === Segmented control: Folders | Files | Both ==========================
    // Persisted in QSettings("searchMode"). Default: "both".
    auto makeSegment = [this](const QString &label, const QString &name) {
        auto *btn = new QPushButton(label, this);
        btn->setObjectName(name);
        btn->setCheckable(true);
        btn->setAutoExclusive(true);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setMinimumWidth(60);
        btn->setStyleSheet(QStringLiteral(
            "QPushButton { padding: 4px 10px; border: 1px solid %1; "
            "background: %2; color: %3; font-size: 11px; }"
            "QPushButton:checked { background: %4; color: white; "
            "border-color: %4; font-weight: 600; }")
                .arg(SwiftUIStyle::subtleBorder(),
                     SwiftUIStyle::secondaryBackground(),
                     SwiftUIStyle::primaryTextColor(),
                     SwiftUIStyle::BrandColor));
        return btn;
    };
    m_modeFolders = makeSegment(tr("Folders"), "modeFolders");
    m_modeFiles   = makeSegment(tr("Files"),   "modeFiles");
    m_modeBoth    = makeSegment(tr("Both"),    "modeBoth");
    m_modeBoth->setChecked(true);  // overridden by loadSettings() below

    toolbarLayout->addWidget(m_modeFolders);
    toolbarLayout->addWidget(m_modeFiles);
    toolbarLayout->addWidget(m_modeBoth);
    toolbarLayout->addSpacing(12);

    connect(m_modeFolders, &QPushButton::toggled, this, [this](bool on) {
        if (on) { m_searchMode = SearchMode::Folders; onSearchModeChanged(); }
    });
    connect(m_modeFiles, &QPushButton::toggled, this, [this](bool on) {
        if (on) { m_searchMode = SearchMode::Files; onSearchModeChanged(); }
    });
    connect(m_modeBoth, &QPushButton::toggled, this, [this](bool on) {
        if (on) { m_searchMode = SearchMode::Both; onSearchModeChanged(); }
    });

    // Show hidden folders toggle (eye icon)
    m_showHiddenButton = new QPushButton(this);
    m_showHiddenButton->setObjectName("showHiddenButton");
    m_showHiddenButton->setFlat(true);
    m_showHiddenButton->setCheckable(true);
    m_showHiddenButton->setToolTip(tr("Show hidden folders"));
    m_showHiddenButton->setFixedSize(32, 32);
    m_showHiddenButton->setCursor(Qt::PointingHandCursor);
    m_showHiddenButton->setIcon(IconRegistry::coloredIcon("eye", SwiftUIStyle::primaryTextQColor()));
    m_showHiddenButton->setIconSize(QSize(16, 16));
    m_showHiddenButton->setStyleSheet(SwiftUIStyle::secondaryButtonStyleSheet());
    toolbarLayout->addWidget(m_showHiddenButton);

    // Exclude settings button (gear icon)
    m_excludeButton = new QPushButton(this);
    m_excludeButton->setObjectName("excludeButton");
    m_excludeButton->setFlat(true);
    m_excludeButton->setToolTip(tr("Exclude Settings"));
    m_excludeButton->setFixedSize(32, 32);
    m_excludeButton->setCursor(Qt::PointingHandCursor);
    m_excludeButton->setIcon(IconRegistry::coloredIcon("gear", SwiftUIStyle::primaryTextQColor()));
    m_excludeButton->setIconSize(QSize(16, 16));
    m_excludeButton->setStyleSheet(SwiftUIStyle::secondaryButtonStyleSheet());
    toolbarLayout->addWidget(m_excludeButton);

    m_mainLayout->addWidget(m_navigationToolbar);

    // === FAVORITES SIDEBAR + BODY ROW (standalone-app drift) ===
    // The remaining content (Search in / Search for / view stack / resolved
    // path / buttons) lives in a right-side column; the sidebar of favorite
    // roots sits to its left, Finder-style.
    auto *bodyRow = new QHBoxLayout();
    bodyRow->setContentsMargins(0, 0, 0, 0);
    bodyRow->setSpacing(SwiftUIStyle::SpacingMedium);

    m_favoritesList = new QListWidget(this);
    m_favoritesList->setObjectName("favoritesList");
    m_favoritesList->setFixedWidth(190);
    m_favoritesList->setFrameShape(QFrame::NoFrame);
    m_favoritesList->setContextMenuPolicy(Qt::CustomContextMenu);
    m_favoritesList->setSpacing(2);   // gap between cards
    m_favoritesList->setStyleSheet(QStringLiteral(R"(
        QListWidget {
            background: transparent;
            border: none;
            outline: 0;
        }
        /* Each favorite renders as a card with a soft background and
           a subtle 1px border, so the list doesn't feel "naked". */
        QListWidget::item {
            padding: 10px 12px;
            border: 1px solid %3;
            border-radius: 8px;
            background: %4;
            color: %1;
            margin: 0px;
        }
        QListWidget::item:hover {
            background: %2;
            border-color: %2;
        }
        QListWidget::item:selected {
            background: %2;
            border-color: %5;
            color: %1;
        }
    )").arg(SwiftUIStyle::primaryTextColor(),
            SwiftUIStyle::chipBackground(),
            SwiftUIStyle::subtleBorder(),
            SwiftUIStyle::secondaryBackground(),
            SwiftUIStyle::BrandColor));

    auto *favSection = new QWidget(this);
    auto *favSectionLayout = new QVBoxLayout(favSection);
    favSectionLayout->setContentsMargins(0, 0, 0, 0);
    favSectionLayout->setSpacing(6);

    QLabel *favHeader = new QLabel(tr("FAVOURITES"), this);
    favHeader->setStyleSheet(QString("color: %1; font-size: 10px; "
                                     "letter-spacing: 1px; padding: 4px 4px 4px 4px; "
                                     "font-weight: 600;")
                                 .arg(SwiftUIStyle::secondaryTextColor()));
    favSectionLayout->addWidget(favHeader);
    favSectionLayout->addWidget(m_favoritesList, 1);

    m_addFavoriteButton = new QPushButton(tr("+ Add current"), this);
    m_addFavoriteButton->setFlat(true);
    m_addFavoriteButton->setCursor(Qt::PointingHandCursor);
    m_addFavoriteButton->setToolTip(tr("Add the current Search-in path as a favorite"));
    m_addFavoriteButton->setStyleSheet(SwiftUIStyle::chipButtonStyleSheet());
    favSectionLayout->addWidget(m_addFavoriteButton);

    bodyRow->addWidget(favSection);

    auto *bodyCol = new QWidget(this);
    auto *body = new QVBoxLayout(bodyCol);
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(SwiftUIStyle::SpacingMedium);
    bodyRow->addWidget(bodyCol, 1);
    m_mainLayout->addLayout(bodyRow, 1);

    loadFavorites();
    rebuildFavoritesList();
    connect(m_favoritesList, &QListWidget::itemClicked,
            this, &FolderBrowserDialog::onFavoriteRowActivated);
    connect(m_favoritesList, &QListWidget::customContextMenuRequested,
            this, &FolderBrowserDialog::onFavoritesContextMenu);
    connect(m_addFavoriteButton, &QPushButton::clicked,
            this, &FolderBrowserDialog::addCurrentRootAsFavorite);

    // From here on, body-area widgets must go into the right column.
    // Re-point m_mainLayout to the right column's layout for the rest of
    // setupUi(). (Hack on purpose — keeps the upstream diff tiny.)
    m_mainLayout = body;

    // === SEARCH IN: Root path filter ===
    m_rootContainer = new QWidget(this);
    m_rootContainer->setObjectName("rootContainer");
    auto *rootLayout = new QHBoxLayout(m_rootContainer);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(SwiftUIStyle::SpacingSmall);

    // Label with fixed width for alignment
    QLabel *rootLabel = new QLabel(tr("Search in:"), this);
    rootLabel->setObjectName("rootLabel");
    rootLabel->setFixedWidth(70);
    rootLabel->setStyleSheet(QString("color: %1; font-size: 12px;").arg(SwiftUIStyle::secondaryTextColor()));
    rootLayout->addWidget(rootLabel);

    // PathSelector widget (handles all path completion logic)
    m_pathSelector = new PathSelector(this);
    m_pathSelector->setObjectName("pathSelector");
    m_pathSelector->setPath(QDir::homePath());
    rootLayout->addWidget(m_pathSelector, 1);

    m_mainLayout->addWidget(m_rootContainer);

    // === SEARCH FOR: Query input ===
    m_searchContainer = new QWidget(this);
    m_searchContainer->setObjectName("searchContainer");
    auto *searchLayout = new QHBoxLayout(m_searchContainer);
    searchLayout->setContentsMargins(0, 0, 0, 0);
    searchLayout->setSpacing(SwiftUIStyle::SpacingSmall);

    // Label with fixed width for alignment
    QLabel *searchLabel = new QLabel(tr("Search for:"), this);
    searchLabel->setObjectName("searchLabel");
    searchLabel->setFixedWidth(70);
    searchLabel->setStyleSheet(QString("color: %1; font-size: 12px;").arg(SwiftUIStyle::secondaryTextColor()));
    searchLayout->addWidget(searchLabel);

    // Search field
    m_searchField = new QLineEdit(this);
    m_searchField->setObjectName("searchField");
    m_searchField->setPlaceholderText(tr("Type to search folders and files..."));
    m_searchField->setClearButtonEnabled(true);
    m_searchField->setStyleSheet(SwiftUIStyle::inputStyleSheet());
    searchLayout->addWidget(m_searchField, 1);

    m_mainLayout->addWidget(m_searchContainer);

    // === INSIDE CONTENTS: ripgrep-backed content search =====================
    // Visible at all times. Enabled only when filename results ≤ threshold.
    m_contentContainer = new QWidget(this);
    m_contentContainer->setObjectName("contentContainer");
    auto *contentLayout = new QHBoxLayout(m_contentContainer);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(SwiftUIStyle::SpacingSmall);

    QLabel *contentLabel = new QLabel(tr("Inside contents:"), this);
    contentLabel->setObjectName("contentLabel");
    contentLabel->setFixedWidth(110);
    contentLabel->setStyleSheet(QString("color: %1; font-size: 12px;")
                                    .arg(SwiftUIStyle::secondaryTextColor()));
    contentLayout->addWidget(contentLabel);

    m_contentField = new QLineEdit(this);
    m_contentField->setObjectName("contentField");
    m_contentField->setPlaceholderText(tr("Narrow filename filter to enable…"));
    m_contentField->setClearButtonEnabled(true);
    m_contentField->setStyleSheet(SwiftUIStyle::inputStyleSheet());
    contentLayout->addWidget(m_contentField, 1);

    m_contentRegex = new QCheckBox(tr("Regex"), this);
    m_contentRegex->setObjectName("contentRegex");
    m_contentRegex->setToolTip(tr("Interpret the content query as a regular expression"));
    contentLayout->addWidget(m_contentRegex);

    m_contentHelpButton = new QPushButton(tr("?"), this);
    m_contentHelpButton->setObjectName("contentHelpButton");
    m_contentHelpButton->setFixedSize(24, 24);
    m_contentHelpButton->setCursor(Qt::PointingHandCursor);
    m_contentHelpButton->setToolTip(tr("Regex cheatsheet"));
    contentLayout->addWidget(m_contentHelpButton);

    m_mainLayout->addWidget(m_contentContainer);

    m_contentHintLabel = new QLabel(this);
    m_contentHintLabel->setObjectName("contentHintLabel");
    m_contentHintLabel->setStyleSheet(
        QString("color: %1; font-size: 11px; padding: 0px 0px 4px 116px;")
            .arg(SwiftUIStyle::secondaryTextColor()));
    m_mainLayout->addWidget(m_contentHintLabel);

    connect(m_contentField, &QLineEdit::textChanged,
            this, &FolderBrowserDialog::onContentTextChanged);
    connect(m_contentRegex, &QCheckBox::toggled,
            this, &FolderBrowserDialog::onContentRegexToggled);
    connect(m_contentHelpButton, &QPushButton::clicked,
            this, &FolderBrowserDialog::onContentHelpClicked);
    connect(m_ripgrep, &RipgrepRunner::matchesReady,
            this, &FolderBrowserDialog::onContentMatches);
    connect(m_ripgrep, &RipgrepRunner::finished,
            this, &FolderBrowserDialog::onContentFinished);

    // Stacked widget for tree view and search results
    m_viewStack = new QStackedWidget(this);
    m_viewStack->setObjectName("viewStack");

    // File system model (directories only)
    m_fileSystemModel = new QFileSystemModel(this);
    // Initial filter is computed once during construction; subsequent
    // changes go through applyTreeViewFilter() so Mode + eye toggle both
    // contribute to the final QDir::Filters bitmask consistently.
    m_fileSystemModel->setFilter(QDir::Dirs | QDir::NoDotAndDotDot);
    m_fileSystemModel->setRootPath(QDir::rootPath());

    // Folder tree view
    m_folderTreeView = new QTreeView(this);
    m_folderTreeView->setObjectName("folderTreeView");
    m_folderTreeView->setModel(m_fileSystemModel);
    m_folderTreeView->setHeaderHidden(true);
    m_folderTreeView->setAnimated(true);
    m_folderTreeView->setIndentation(20);
    m_folderTreeView->setSortingEnabled(true);
    m_folderTreeView->sortByColumn(0, Qt::AscendingOrder);

    // Standalone-app drift: don't steal printable keys for tree's built-in
    // keyboardSearch — those characters should reach the dialog's
    // keyPressEvent and route into the search field instead.
    m_folderTreeView->setFocusPolicy(Qt::ClickFocus);
    m_folderTreeView->installEventFilter(this);

    // Hide all columns except Name
    m_folderTreeView->hideColumn(1); // Size
    m_folderTreeView->hideColumn(2); // Type
    m_folderTreeView->hideColumn(3); // Date Modified

    // Style the tree view
    m_folderTreeView->setStyleSheet(SwiftUIStyle::treeViewStyleSheet());

    // Search results list. Horizontal scrollbar enabled so long paths can be
    // scrolled into view rather than truncated. Per-row sizeHint is computed
    // in rebuildMergedResults() so the list's contentsSize accommodates the
    // widest visible row.
    m_searchResultsList = new QListWidget(this);
    m_searchResultsList->setObjectName("searchResultsList");
    m_searchResultsList->setStyleSheet(SwiftUIStyle::listStyleSheet());
    m_searchResultsList->setSpacing(4); // Per HIG spacing values
    m_searchResultsList->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_searchResultsList->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_searchResultsList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_searchResultsList->setWordWrap(false);
    m_searchResultsList->setTextElideMode(Qt::ElideNone);
    m_searchResultsList->setItemDelegate(new SearchResultDelegate(this));

    m_viewStack->addWidget(m_folderTreeView);
    m_viewStack->addWidget(m_searchResultsList);
    m_viewStack->setCurrentWidget(m_folderTreeView);

    m_mainLayout->addWidget(m_viewStack);

    // Resolved path preview (shows what path will be opened)
    m_resolvedPathLabel = new QLabel(this);
    m_resolvedPathLabel->setObjectName("resolvedPathLabel");
    m_resolvedPathLabel->setStyleSheet(
        QString("color: %1; font-size: 11px; padding: 4px 0px;").arg(SwiftUIStyle::secondaryTextColor())
    );
    m_resolvedPathLabel->setWordWrap(true);
    m_mainLayout->addWidget(m_resolvedPathLabel);

    // Standalone-app drift: persistent keyboard shortcut hint line.
    // Keeps the supported chords visible so the dialog is usable without
    // mouse for power users — and discoverable for everyone else.
    auto *shortcutsHint = new QLabel(this);
    shortcutsHint->setObjectName("shortcutsHint");
    shortcutsHint->setTextFormat(Qt::RichText);
    shortcutsHint->setText(tr(
        "<span style='font-size:11px;'>"
        "<b>⌃⌥⇧S</b> summon &nbsp;·&nbsp; "
        "<b>↑↓</b> nav &nbsp;·&nbsp; "
        "<b>↵</b> open &nbsp;·&nbsp; "
        "<b>⌘↵</b> reveal &nbsp;·&nbsp; "
        "<b>⌥↵</b> editor &nbsp;·&nbsp; "
        "<b>⌘F</b> search &nbsp;·&nbsp; "
        "<b>⌘L</b> path &nbsp;·&nbsp; "
        "<b>⌘↑</b> up &nbsp;·&nbsp; "
        "<b>⌘H</b> home &nbsp;·&nbsp; "
        "<b>Esc</b> clear"
        "</span>"));
    shortcutsHint->setWordWrap(true);
    shortcutsHint->setStyleSheet(
        QString("color: %1; padding: 4px 6px; border-top: 1px solid %2;")
            .arg(SwiftUIStyle::secondaryTextColor(),
                 SwiftUIStyle::subtleBorder()));
    m_mainLayout->addWidget(shortcutsHint);

    // Button layout
    auto *buttonLayout = new QHBoxLayout();
    buttonLayout->setObjectName("buttonLayout");
    buttonLayout->setSpacing(SwiftUIStyle::SpacingSmall);

    // Cancel button (secondary)
    // Standalone-app drift vs. maude-cp-v3: in upstream this is a modal
    // picker (Cancel / Choose). Here it's the main window of a standalone
    // search app, so Cancel doesn't make sense and Choose splits into two
    // explicit open actions. See docs/050_porting_rules.md.
    m_cancelButton = new QPushButton(tr("Cancel"), this);
    m_cancelButton->setObjectName("cancelButton");
    m_cancelButton->setFlat(true);
    m_cancelButton->setCursor(Qt::PointingHandCursor);
    m_cancelButton->setStyleSheet(SwiftUIStyle::closeButtonStyleSheet());
    m_cancelButton->setVisible(false); // keep ptr for layout/keyEvent, hide button

    // Choose button (primary action)
    // Standalone-app drift: two buttons replace the single "Choose".
    m_openInFinderButton = new QPushButton(tr("Open in Finder"), this);
    m_openInFinderButton->setObjectName("openInFinderButton");
    m_openInFinderButton->setFlat(true);
    m_openInFinderButton->setCursor(Qt::PointingHandCursor);
    m_openInFinderButton->setStyleSheet(SwiftUIStyle::secondaryButtonStyleSheet());

    m_openInAppButton = new QPushButton(tr("Open with App"), this);
    m_openInAppButton->setObjectName("openInAppButton");
    m_openInAppButton->setFlat(true);
    m_openInAppButton->setCursor(Qt::PointingHandCursor);
    m_openInAppButton->setStyleSheet(SwiftUIStyle::primaryButtonStyleSheet());

    // Keep m_chooseButton as an alias of the primary "Open with App" button
    // so the keyPressEvent dispatch (Enter triggers it) still works.
    m_chooseButton = m_openInAppButton;

    buttonLayout->addStretch();
    buttonLayout->addWidget(m_cancelButton);
    buttonLayout->addWidget(m_openInFinderButton);
    buttonLayout->addWidget(m_openInAppButton);
    m_mainLayout->addLayout(buttonLayout);

    // Connect signals
    connect(m_folderTreeView, &QTreeView::clicked,
            this, &FolderBrowserDialog::onFolderClicked);
    connect(m_folderTreeView, &QTreeView::doubleClicked,
            this, &FolderBrowserDialog::onFolderDoubleClicked);
    connect(m_folderTreeView->selectionModel(), &QItemSelectionModel::currentChanged,
            this, [this]() { updateResolvedPathLabel(); });
    connect(m_openInFinderButton, &QPushButton::clicked,
            this, &FolderBrowserDialog::onOpenInFinderClicked);
    connect(m_openInAppButton, &QPushButton::clicked,
            this, &FolderBrowserDialog::onOpenInAppClicked);
    connect(m_cancelButton, &QPushButton::clicked,
            this, &QDialog::reject);
    connect(m_upButton, &QPushButton::clicked,
            this, &FolderBrowserDialog::onUpClicked);
    connect(m_homeButton, &QPushButton::clicked,
            this, &FolderBrowserDialog::onHomeClicked);

    // Search signals
    connect(m_searchField, &QLineEdit::textChanged,
            this, &FolderBrowserDialog::onSearchTextChanged);
    connect(m_searchField, &QLineEdit::returnPressed,
            this, &FolderBrowserDialog::onChooseClicked);
    connect(m_searchWorker, &FolderSearchWorker::resultsReady,
            this, &FolderBrowserDialog::onSearchResultsReady);
    connect(m_fileSearchWorker, &FileSearchWorker::resultsReady,
            this, &FolderBrowserDialog::onFileSearchResultsReady);
    connect(m_searchResultsList, &QListWidget::itemClicked,
            this, &FolderBrowserDialog::onSearchResultClicked);
    connect(m_searchResultsList, &QListWidget::itemDoubleClicked,
            this, &FolderBrowserDialog::onSearchResultDoubleClicked);
    connect(m_searchResultsList, &QListWidget::itemActivated,
            this, &FolderBrowserDialog::onSearchResultDoubleClicked);
    connect(m_searchResultsList, &QListWidget::currentRowChanged,
            this, [this](int row) {
                if (row >= 0) {
                    if (auto *item = m_searchResultsList->item(row)) {
                        m_selectedLineNumber = item->data(Qt::UserRole + 1).toInt();
                    }
                }
                updateResolvedPathLabel();
            });
    connect(m_excludeButton, &QPushButton::clicked,
            this, &FolderBrowserDialog::onExcludeButtonClicked);
    connect(m_showHiddenButton, &QPushButton::toggled,
            this, &FolderBrowserDialog::onShowHiddenToggled);

    // PathSelector signals
    connect(m_pathSelector, &PathSelector::pathChanged,
            this, &FolderBrowserDialog::onPathSelectorChanged);
}

void FolderBrowserDialog::setCurrentRoot(const QString &path)
{
    navigateTo(path);
    setRootPath(path);
}

void FolderBrowserDialog::summon()
{
    if (isMinimized()) showNormal();
    show();
    raise();
    activateWindow();

    if (auto *searchField = findChild<QLineEdit *>("searchField")) {
        searchField->setFocus();
        searchField->selectAll();
    }
}

void FolderBrowserDialog::navigateTo(const QString &path)
{
    // Validate path and fallback to home if invalid
    QString validPath = path;
    if (validPath.isEmpty() || !QDir(validPath).exists()) {
        validPath = QDir::homePath();
    }

    m_currentPath = validPath;
    QModelIndex index = m_fileSystemModel->index(validPath);
    m_folderTreeView->setRootIndex(index);
    m_folderTreeView->scrollTo(index);

    // Update PathSelector (it handles its own signal blocking)
    m_pathSelector->setPath(validPath);
    m_rootPath = validPath;

    // Update button states
    updateUpButtonState();
    updateResolvedPathLabel();

    // Switch to tree view
    m_viewStack->setCurrentWidget(m_folderTreeView);
    m_searchField->clear();
}

void FolderBrowserDialog::onFolderClicked(const QModelIndex &index)
{
    QString path = m_fileSystemModel->filePath(index);
    m_currentPath = path;
    updateResolvedPathLabel();
    // Also update root field
    setRootPath(path);
}

void FolderBrowserDialog::onFolderDoubleClicked(const QModelIndex &index)
{
    QString path = m_fileSystemModel->filePath(index);
    // Tree view now also surfaces files when the search mode is Files / Both.
    // Double-clicking a file should open it with the associated app rather
    // than try to descend into it (which navigateTo() would silently bounce
    // back to home).
    QFileInfo info(path);
    if (info.isDir()) {
        navigateTo(path);
    } else if (info.exists()) {
        m_selectedPath = path;
        QProcess::startDetached("/usr/bin/open", { path });
    }
}

void FolderBrowserDialog::onChooseClicked()
{
    // Standalone-app behavior: open the path with the default app and keep
    // the dialog running. Old upstream `accept()` closed the dialog after
    // "Choose" — that quits the app in this standalone context because the
    // dialog IS the main window. Now identical to onOpenInAppClicked so
    // any legacy caller (keyPress dispatch, button aliasing) does the
    // right thing.
    onOpenInAppClicked();
}

void FolderBrowserDialog::onOpenInFinderClicked()
{
    // Standalone-app drift: reveal the path in Finder (parent view, item
    // selected) and keep this dialog open as the running app's main window.
    const QString path = resolvedPath();
    if (path.isEmpty()) {
        return;
    }
    m_selectedPath = path;
    QProcess::startDetached("/usr/bin/open", {QStringLiteral("-R"), path});
}

void FolderBrowserDialog::onOpenInAppClicked()
{
    // Standalone-app drift: open with the default application
    // (folder → Finder current view, file → associated app).
    const QString path = resolvedPath();
    if (path.isEmpty()) {
        return;
    }
    m_selectedPath = path;
    QProcess::startDetached("/usr/bin/open", {path});
}

void FolderBrowserDialog::onUpClicked()
{
    QDir dir(m_currentPath);
    if (dir.cdUp()) {
        navigateTo(dir.absolutePath());
    }
}

void FolderBrowserDialog::onHomeClicked()
{
    setRootPath(QString()); // Clear root filter
    navigateTo(QDir::homePath());
}

void FolderBrowserDialog::updateUpButtonState()
{
    QDir dir(m_currentPath);
    // Disable up button if we're at the root
    m_upButton->setEnabled(dir.cdUp());
}

void FolderBrowserDialog::onSearchTextChanged(const QString &text)
{
    m_lastSearchQuery = text;

    if (text.isEmpty()) {
        // Switch back to tree view, clear cached results.
        m_viewStack->setCurrentWidget(m_folderTreeView);
        m_searchWorker->cancel();
        m_fileSearchWorker->cancel();
        m_ripgrep->cancel();
        m_lastFolderResults.clear();
        m_lastFileResults.clear();
        m_contentMatchesByFile.clear();
        m_contentMatchTotal = 0;
        updateResolvedPathLabel();
        updateContentFieldState();
        return;
    }

    // Switch to search results view
    m_viewStack->setCurrentWidget(m_searchResultsList);

    triggerSearch();
}

void FolderBrowserDialog::triggerSearch()
{
    const QString &q = m_lastSearchQuery;
    const bool wantFolders = (m_searchMode == SearchMode::Folders ||
                              m_searchMode == SearchMode::Both);
    const bool wantFiles   = (m_searchMode == SearchMode::Files ||
                              m_searchMode == SearchMode::Both);

    if (wantFolders) {
        m_searchWorker->search(q, m_rootPath);
    } else {
        m_searchWorker->cancel();
        m_lastFolderResults.clear();
    }
    if (wantFiles) {
        m_fileSearchWorker->search(q, m_rootPath);
    } else {
        m_fileSearchWorker->cancel();
        m_lastFileResults.clear();
    }
    if (!wantFolders && !wantFiles) {
        rebuildMergedResults();
    }
    // Make sure the content-search gating reflects the new mode/query even
    // when no worker emits a follow-up (e.g. mode switched away from Files).
    updateContentFieldState();
}

void FolderBrowserDialog::onSearchResultsReady(const QList<SearchResult> &results)
{
    m_lastFolderResults = results;
    rebuildMergedResults();
}

void FolderBrowserDialog::onFileSearchResultsReady(const QList<SearchResult> &results)
{
    m_lastFileResults = results;
    rebuildMergedResults();
    // Whenever the file-result set changes, the content-search gating may flip.
    updateContentFieldState();
    // If the user has a content query, kick off the ripgrep run on the new
    // file set (or clear results if the threshold dropped us out of range).
    triggerContentSearch();
}

void FolderBrowserDialog::rebuildMergedResults()
{
    m_searchResultsList->clear();
    m_selectedLineNumber = 0;

    using Kind = SearchResultDelegate::Kind;
    using Roles = SearchResultDelegate;

    const bool contentActive =
        m_contentField && !m_contentField->text().isEmpty() && !m_contentBusy
        && m_searchMode != SearchMode::Folders;

    struct Row { SearchResult sr; bool isFile = false; };
    QList<Row> rows;
    rows.reserve(m_lastFolderResults.size() + m_lastFileResults.size());
    if (!contentActive) {
        for (const SearchResult &r : m_lastFolderResults) rows.append({ r, false });
        for (const SearchResult &r : m_lastFileResults)   rows.append({ r, true  });
    } else {
        for (const SearchResult &r : m_lastFileResults) {
            auto it = m_contentMatchesByFile.find(r.path);
            if (it == m_contentMatchesByFile.end() || it.value().isEmpty()) continue;
            rows.append({ r, true });
        }
    }
    std::sort(rows.begin(), rows.end(), [](const Row &a, const Row &b) {
        if (a.sr.score != b.sr.score) return a.sr.score > b.sr.score;
        return a.sr.path.length() < b.sr.path.length();
    });
    if (rows.size() > 200) rows.resize(200);

    auto *delegate = qobject_cast<SearchResultDelegate *>(
        m_searchResultsList->itemDelegate());

    auto sizeRow = [this, delegate](QListWidgetItem *item, int height) {
        int natural = delegate ? delegate->naturalWidth(item) : 0;
        if (natural <= 0) natural = m_searchResultsList->viewport()->width();
        const int width = qMax(natural,
                               m_searchResultsList->viewport()->width());
        item->setSizeHint(QSize(width, height));
    };

    for (const Row &row : rows) {
        auto *item = new QListWidgetItem(m_searchResultsList);
        item->setData(Roles::PathRole, row.sr.path);
        item->setData(Roles::LineRole, 0);
        item->setData(Roles::KindRole,
                      static_cast<int>(row.isFile ? Kind::File : Kind::Folder));
        item->setData(Roles::ScoreRole, row.sr.score);
        item->setData(Roles::QueryRole, m_lastSearchQuery);
        if (row.isFile) {
            item->setData(Roles::ExtRole, QFileInfo(row.sr.path).suffix().toLower());
        }

        const auto matchesIt = m_contentMatchesByFile.find(row.sr.path);
        const int matchCount = (row.isFile && matchesIt != m_contentMatchesByFile.end())
                                   ? matchesIt.value().size()
                                   : 0;
        item->setData(Roles::MatchCountRole, matchCount);

        sizeRow(item, SearchResultDelegate::kParentHeight);

        // Inline content-match child rows under this file (if any).
        if (row.isFile && matchesIt != m_contentMatchesByFile.end()) {
            const QList<ContentMatch> &matches = matchesIt.value();
            const int shownCap = qMin(5, matches.size());
            for (int i = 0; i < shownCap; ++i) {
                const ContentMatch &m = matches.at(i);
                auto *childItem = new QListWidgetItem(m_searchResultsList);
                childItem->setData(Roles::PathRole, row.sr.path);
                childItem->setData(Roles::LineRole, m.lineNumber);
                childItem->setData(Roles::KindRole,
                                   static_cast<int>(Kind::ContentLine));
                childItem->setData(Roles::SnippetRole, m.snippet);
                childItem->setData(Roles::SnippetHlStartRole, m.matchStart);
                childItem->setData(Roles::SnippetHlEndRole, m.matchEnd);
                sizeRow(childItem, SearchResultDelegate::kChildHeight);
            }
            if (matches.size() > shownCap) {
                auto *moreItem = new QListWidgetItem(m_searchResultsList);
                moreItem->setFlags(moreItem->flags() & ~Qt::ItemIsSelectable);
                moreItem->setData(Roles::KindRole,
                                  static_cast<int>(Kind::MoreLine));
                moreItem->setData(Roles::MoreCountRole, matches.size() - shownCap);
                sizeRow(moreItem, SearchResultDelegate::kChildHeight);
            }
        }
    }

    if (rows.isEmpty() && !m_lastSearchQuery.isEmpty()) {
        const QString msg = contentActive
            ? tr("No content matches in any of the %1 file%2.")
                  .arg(m_lastFileResults.size())
                  .arg(m_lastFileResults.size() == 1 ? "" : "s")
            : tr("No results found");
        auto *item = new QListWidgetItem(m_searchResultsList);
        item->setData(Roles::KindRole, static_cast<int>(Kind::Empty));
        item->setData(Roles::PathRole, msg);
        item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
        sizeRow(item, SearchResultDelegate::kEmptyHeight);
    }

    if (!rows.isEmpty()) m_searchResultsList->setCurrentRow(0);
    updateResolvedPathLabel();
}

void FolderBrowserDialog::onSearchResultClicked(QListWidgetItem *item)
{
    QString path = item->data(Qt::UserRole).toString();
    const int lineNo = item->data(Qt::UserRole + 1).toInt();
    if (!path.isEmpty()) {
        m_selectedLineNumber = lineNo;
        m_currentPath = path;
        updateResolvedPathLabel();
        // Only re-scope when a folder is selected. Selecting a file leaves
        // the root alone so the user can scan through multiple file hits.
        if (lineNo == 0 && QFileInfo(path).isDir()) {
            setRootPath(path);
        }
    }
}

void FolderBrowserDialog::onSearchResultDoubleClicked(QListWidgetItem *item)
{
    // Open the search hit with the default app (Finder for folders, the
    // associated app for files) and **keep the dialog open**. Old behavior
    // called accept() here, which closed the dialog — but the dialog IS
    // the running app's main window, so closing it effectively ended the
    // session. Per docs/todos.md: dialog stays open after every open.
    QString path = item->data(Qt::UserRole).toString();
    if (path.isEmpty()) return;
    m_selectedPath = path;
    QProcess::startDetached("/usr/bin/open", {path});
}

void FolderBrowserDialog::onExcludeButtonClicked()
{
    // The gear icon now opens the broader Preferences dialog. Exclude
    // rules are reachable from there via an "Edit exclude rules…" button.
    // See docs/todos.md TODO 7.
    PreferencesDialog dlg(m_excludeSettings, m_globalHotkey, this);

    connect(&dlg, &PreferencesDialog::showHiddenChanged, this,
            [this](bool checked) {
                if (m_showHiddenButton &&
                    m_showHiddenButton->isChecked() != checked) {
                    m_showHiddenButton->setChecked(checked);
                }
            });
    connect(&dlg, &PreferencesDialog::hotkeyEnabledChanged,
            this, &FolderBrowserDialog::hotkeyPreferenceChanged);

    dlg.exec();

    // After changing excludes (via the sub-dialog), trigger a rescan.
    PathCacheManager::instance()->rescan();
}

void FolderBrowserDialog::setGlobalHotkey(GlobalHotkey *hotkey)
{
    m_globalHotkey = hotkey;
}

void FolderBrowserDialog::onShowHiddenToggled(bool checked)
{
    // Standalone-app drift (TODO 4): the eye toggle is now purely
    // presentational. The cache always indexes hidden folders; we
    // just filter them out of the visible views.
    //
    //   - QFileSystemModel filter — tree-view presentation only.
    //   - FolderSearchWorker.setIncludeHidden — search-results filter.
    //   - PathSelector's adapter — completion popup filter.
    //
    //   No rescan is triggered. Flipping the toggle is now instant.
    m_showHidden = checked;

    applyTreeViewFilter();

    m_pathSelector->fileSystemAdapter()->setShowHidden(m_showHidden);
    m_searchWorker->setIncludeHidden(m_showHidden);
    if (m_fileSearchWorker) m_fileSearchWorker->setIncludeHidden(m_showHidden);

    // If a query is currently active, re-run it so visible results
    // reflect the new filter immediately.
    if (!m_lastSearchQuery.isEmpty()) {
        triggerSearch();
    }

    m_showHiddenButton->setToolTip(m_showHidden
        ? tr("Hide hidden folders")
        : tr("Show hidden folders"));

    saveSettings();
}

void FolderBrowserDialog::onCacheStatusChanged()
{
    updateCacheStatusLabel();
}

void FolderBrowserDialog::updateCacheStatusLabel()
{
    PathCacheManager *cache = PathCacheManager::instance();
    // Format the count with thousands separators for legibility, then drop
    // the word "folders" — by this point in the UI the count alone is clear.
    auto fmt = [](int n) {
        return QLocale().toString(n);
    };

    if (cache->isScanning()) {
        m_cacheStatusLabel->setText(tr("Indexing… %1").arg(fmt(cache->folderCount())));
        m_cacheStatusLabel->setStyleSheet("color: #e67e22; font-size: 11px;");
    } else {
        m_cacheStatusLabel->setText(tr("Ready — %1").arg(fmt(cache->folderCount())));
        m_cacheStatusLabel->setStyleSheet("color: #27ae60; font-size: 11px;");
    }
}

void FolderBrowserDialog::setSearchMode(SearchMode m)
{
    if (m == m_searchMode) return;
    m_searchMode = m;
    if (m_modeFolders) m_modeFolders->setChecked(m == SearchMode::Folders);
    if (m_modeFiles)   m_modeFiles->setChecked(m == SearchMode::Files);
    if (m_modeBoth)    m_modeBoth->setChecked(m == SearchMode::Both);
    onSearchModeChanged();
}

QDir::Filters FolderBrowserDialog::treeViewFilters() const
{
    QDir::Filters f = QDir::NoDotAndDotDot | QDir::Dirs;
    // Files / Both mode also surface files in the navigation tree when no
    // search query is active. Folders mode keeps the tree folders-only so
    // it doesn't drown in file noise.
    if (m_searchMode != SearchMode::Folders) {
        f |= QDir::Files;
    }
    if (m_showHidden) {
        f |= QDir::Hidden;
    }
    return f;
}

void FolderBrowserDialog::applyTreeViewFilter()
{
    if (m_fileSystemModel) m_fileSystemModel->setFilter(treeViewFilters());
}

void FolderBrowserDialog::onSearchModeChanged()
{
    saveSettings();
    applyTreeViewFilter();
    if (!m_lastSearchQuery.isEmpty()) triggerSearch();
}

void FolderBrowserDialog::onFileCapReached()
{
    m_cacheStatusLabel->setText(
        tr("File index cap reached (%1) — tighten excludes or raise in Preferences")
            .arg(QLocale().toString(FileCacheManager::instance()->capLimit())));
    m_cacheStatusLabel->setStyleSheet("color: #e67e22; font-size: 11px;");
}

void FolderBrowserDialog::updateContentFieldState()
{
    if (!m_contentField || !m_contentSettings) return;
    const int threshold = m_contentSettings->threshold();
    const int fileResults = m_lastFileResults.size();
    const bool wantsFiles = (m_searchMode != SearchMode::Folders);

    bool enabled = false;
    QString hint;

    if (m_lastSearchQuery.isEmpty()) {
        hint = tr("Type a filename query first to narrow the file set.");
    } else if (!wantsFiles) {
        hint = tr("Switch to Files or Both to enable content search.");
    } else if (fileResults == 0) {
        hint = tr("No files match the filename filter.");
    } else if (fileResults > threshold) {
        hint = tr("Narrow filename filter to ≤ %1 files to enable content search (currently %2).")
                  .arg(threshold).arg(fileResults);
    } else {
        enabled = true;
        if (m_contentField->text().isEmpty()) {
            hint = tr("Ready — content-searching %1 file%2.")
                       .arg(fileResults).arg(fileResults == 1 ? "" : "s");
        } else if (m_contentBusy) {
            hint = tr("Searching contents in %1 file%2…")
                       .arg(fileResults).arg(fileResults == 1 ? "" : "s");
        } else {
            hint = tr("Found %1 match%2 in %3 file%4.")
                       .arg(m_contentMatchTotal)
                       .arg(m_contentMatchTotal == 1 ? "" : "es")
                       .arg(m_contentMatchesByFile.size())
                       .arg(m_contentMatchesByFile.size() == 1 ? "" : "s");
        }
    }

    m_contentField->setEnabled(enabled);
    m_contentHintLabel->setText(hint);
}

void FolderBrowserDialog::onContentTextChanged(const QString & /*text*/)
{
    triggerContentSearch();
}

void FolderBrowserDialog::onContentRegexToggled(bool /*on*/)
{
    triggerContentSearch();
}

void FolderBrowserDialog::onContentHelpClicked()
{
    QMessageBox box(this);
    box.setWindowTitle(tr("Regex cheatsheet"));
    box.setIcon(QMessageBox::NoIcon);
    box.setTextFormat(Qt::RichText);
    box.setText(tr(
        "<h3>Regex cheatsheet</h3>"
        "<table cellspacing='4' cellpadding='2'>"
        "<tr><td><code>(?i)foo</code></td><td>Case-insensitive match</td></tr>"
        "<tr><td><code>\\bfoo\\b</code></td><td>Word boundary</td></tr>"
        "<tr><td><code>foo|bar</code></td><td>Alternation</td></tr>"
        "<tr><td><code>^TODO</code></td><td>Start of line</td></tr>"
        "<tr><td><code>;\\s*$</code></td><td>End of line, ignoring trailing whitespace</td></tr>"
        "<tr><td><code>[A-Z]{3,}</code></td><td>Three or more uppercase letters</td></tr>"
        "<tr><td><code>\\d{3,}</code></td><td>Three or more digits</td></tr>"
        "<tr><td><code>[^/]+</code></td><td>One or more non-slash characters</td></tr>"
        "<tr><td><code>1\\.2\\.3</code></td><td>Escaped literal dots</td></tr>"
        "<tr><td><code>id=(\\d+)</code></td><td>Capture group around digits</td></tr>"
        "</table>"));
    box.exec();
}

void FolderBrowserDialog::triggerContentSearch()
{
    if (!m_contentField || !m_contentSettings) return;

    // Always cancel in-flight rg first.
    m_ripgrep->cancel();
    m_contentMatchesByFile.clear();
    m_contentMatchTotal = 0;
    m_contentBusy = false;

    const QString query = m_contentField->text();
    const int threshold = m_contentSettings->threshold();
    if (query.isEmpty() || m_lastFileResults.isEmpty()
        || m_lastFileResults.size() > threshold
        || m_searchMode == SearchMode::Folders) {
        rebuildMergedResults();
        updateContentFieldState();
        return;
    }

    // Collect candidate paths, excluding blacklisted extensions and files
    // larger than the per-file size cap. ripgrep's --max-filesize is a no-op
    // on explicit positional args, so we enforce the cap ourselves here.
    const qint64 sizeCapBytes =
        qint64(m_contentSettings->maxFileSizeMB()) * 1024 * 1024;
    QStringList paths;
    for (const SearchResult &r : m_lastFileResults) {
        if (m_contentSettings->isExtensionBlacklisted(r.path)) continue;
        const QFileInfo info(r.path);
        if (info.size() > sizeCapBytes) continue;
        paths.append(r.path);
    }
    if (paths.isEmpty()) {
        rebuildMergedResults();
        updateContentFieldState();
        return;
    }

    m_contentBusy = true;
    m_ripgrep->start(query, paths, m_contentRegex && m_contentRegex->isChecked(),
                     m_contentSettings->maxFileSizeMB(), /*maxPerFile*/ 20);
    updateContentFieldState();
}

void FolderBrowserDialog::onContentMatches(const QList<ContentMatch> &matches)
{
    for (const ContentMatch &m : matches) {
        m_contentMatchesByFile[m.filePath].append(m);
    }
    m_contentMatchTotal += matches.size();
    // Don't rebuild on every batch — wait for finished. Update hint count.
    updateContentFieldState();
}

void FolderBrowserDialog::onContentFinished(int /*total*/)
{
    m_contentBusy = false;
    rebuildMergedResults();
    updateContentFieldState();
}

void FolderBrowserDialog::onPathSelectorChanged(const QString &path)
{
    // PathSelector handles validation, so we can use the path directly
    setRootPath(path);
}

void FolderBrowserDialog::setRootPath(const QString &path)
{
    // Default to home if empty
    QString normalizedPath = path.isEmpty() ? QDir::homePath() : QDir::cleanPath(path);

    if (m_rootPath == normalizedPath) {
        return;
    }

    m_rootPath = normalizedPath;
    m_currentPath = normalizedPath;

    // Update PathSelector (it handles its own state)
    m_pathSelector->setPath(m_rootPath);

    // Navigate tree view to the new root path
    QModelIndex index = m_fileSystemModel->index(m_rootPath);
    m_folderTreeView->setRootIndex(index);
    m_folderTreeView->scrollTo(index);
    updateUpButtonState();
    updateResolvedPathLabel();

    // Expand cache to include new root if needed (does NOT stop current scan)
    PathCacheManager::instance()->expandTo(m_rootPath);

    // Update status display
    updateCacheStatusLabel();

    // Re-run search with new root
    triggerSearch();

    // Save settings
    saveSettings();
}

QString FolderBrowserDialog::resolvedPath() const
{
    // If search results are visible, use the selected result. Accept both
    // files and folders here — file rows arrived after the lift.
    if (m_viewStack->currentWidget() == m_searchResultsList) {
        QListWidgetItem *current = m_searchResultsList->currentItem();
        if (current) {
            QString path = current->data(Qt::UserRole).toString();
            if (!path.isEmpty() && QFileInfo(path).exists()) {
                return path;
            }
        }
    }

    // If tree view is visible, use the selected folder
    if (m_viewStack->currentWidget() == m_folderTreeView) {
        QModelIndex index = m_folderTreeView->currentIndex();
        if (index.isValid()) {
            QString path = m_fileSystemModel->filePath(index);
            if (!path.isEmpty() && QDir(path).exists()) {
                return path;
            }
        }
    }

    // If currentPath is valid, use it
    if (!m_currentPath.isEmpty() && QDir(m_currentPath).exists()) {
        return m_currentPath;
    }
    // Fallback to rootPath (the "Search in" path)
    if (!m_rootPath.isEmpty() && QDir(m_rootPath).exists()) {
        return m_rootPath;
    }
    // Last resort
    return QDir::homePath();
}

void FolderBrowserDialog::updateResolvedPathLabel()
{
    QString path = resolvedPath();
    if (m_selectedLineNumber > 0 && !path.isEmpty()) {
        m_resolvedPathLabel->setText(
            tr("Will open: %1:%2").arg(path).arg(m_selectedLineNumber));
    } else {
        m_resolvedPathLabel->setText(tr("Will open: %1").arg(path));
    }
}

void FolderBrowserDialog::saveSettings()
{
    QSettings settings("Maude", "FolderBrowser");
    settings.setValue("rootPath", m_rootPath);
    settings.setValue("showHidden", m_showHidden);

    QString modeStr;
    switch (m_searchMode) {
        case SearchMode::Folders: modeStr = "folders"; break;
        case SearchMode::Files:   modeStr = "files"; break;
        case SearchMode::Both:    modeStr = "both"; break;
    }
    settings.setValue("searchMode", modeStr);
    if (m_contentRegex) settings.setValue("contentRegex", m_contentRegex->isChecked());
}

// ---------------------------------------------------------------------------
// Favorites sidebar (standalone-app drift) — Finder-style left rail.
//
//   Home is implicit and always shown first; not deletable.
//   User-added paths persist as QStringList "favorites".
//   One path may be marked "default" (persisted as "defaultFavorite") —
//   that's the path opened at app launch. When the default is deleted,
//   Home becomes the implicit default again.
//   Right-click on a row opens a mini context menu: "Make Default" and
//   "Delete" (Delete hidden for Home).
// ---------------------------------------------------------------------------

namespace {
constexpr int kFavoritePathRole = Qt::UserRole + 1;
constexpr int kFavoriteIsHomeRole = Qt::UserRole + 2;

QString prettyLabel(const QString &path)
{
    if (path == QStringLiteral("/")) return QStringLiteral("Macintosh HD");
    QString name = QFileInfo(path).fileName();
    return name.isEmpty() ? path : name;
}
}  // namespace

void FolderBrowserDialog::loadFavorites()
{
    QSettings settings("Maude", "FolderBrowser");
    if (settings.contains("favorites")) {
        m_favoritePaths = settings.value("favorites").toStringList();
    } else {
        // First run — seed with the macOS folders most users want one click
        // away. Existence is re-checked at render time, so missing entries
        // disappear automatically (e.g. no ~/Desktop on a fresh install).
        const QString home = QDir::homePath();
        m_favoritePaths = {
            home + "/Documents",
            home + "/Downloads",
            home + "/Desktop",
            QStringLiteral("/"),    // → rendered as "Macintosh HD"
        };
        settings.setValue("favorites", m_favoritePaths);
        settings.sync();
    }
    m_defaultFavorite = settings.value("defaultFavorite").toString();
}

void FolderBrowserDialog::saveFavorites()
{
    QSettings settings("Maude", "FolderBrowser");
    settings.setValue("favorites", m_favoritePaths);
    settings.setValue("defaultFavorite", m_defaultFavorite);
}

QString FolderBrowserDialog::resolveDefaultStartPath()
{
    QSettings settings("Maude", "FolderBrowser");
    const QString defFav = settings.value("defaultFavorite").toString();
    if (!defFav.isEmpty() && QDir(defFav).exists()) {
        return defFav;
    }
    return QDir::homePath();
}

void FolderBrowserDialog::rebuildFavoritesList()
{
    if (!m_favoritesList) return;
    m_favoritesList->blockSignals(true);
    m_favoritesList->clear();

    const QString home = QDir::cleanPath(QDir::homePath());
    const QString effectiveDefault = m_defaultFavorite.isEmpty() ? home : m_defaultFavorite;

    auto addItem = [&](const QString &label, const QString &path, bool isHome) {
        auto *item = new QListWidgetItem(label, m_favoritesList);
        item->setData(kFavoritePathRole, path);
        item->setData(kFavoriteIsHomeRole, isHome);
        item->setToolTip(path);
        // The default favorite is shown bold (no leading bubble — the bold
        // weight is the affordance). Everything else is normal weight.
        if (QDir::cleanPath(path) == QDir::cleanPath(effectiveDefault)) {
            QFont f = item->font();
            f.setBold(true);
            item->setFont(f);
        }
    };

    addItem(tr("Home"), home, /*isHome=*/true);
    for (const QString &path : m_favoritePaths) {
        if (!QDir(path).exists()) continue;
        addItem(prettyLabel(path), QDir::cleanPath(path), /*isHome=*/false);
    }

    m_favoritesList->blockSignals(false);
}

void FolderBrowserDialog::onFavoriteRowActivated(QListWidgetItem *item)
{
    if (!item) return;
    const QString path = item->data(kFavoritePathRole).toString();
    if (path.isEmpty() || !QDir(path).exists()) return;
    navigateTo(path);
    setRootPath(path);
}

void FolderBrowserDialog::onFavoritesContextMenu(const QPoint &pos)
{
    QListWidgetItem *item = m_favoritesList->itemAt(pos);
    if (!item) return;
    const QString path  = item->data(kFavoritePathRole).toString();
    const bool isHome   = item->data(kFavoriteIsHomeRole).toBool();
    const QString home  = QDir::cleanPath(QDir::homePath());
    const QString currentDefault = m_defaultFavorite.isEmpty() ? home : m_defaultFavorite;
    const bool isDefault = QDir::cleanPath(path) == QDir::cleanPath(currentDefault);

    QMenu menu(this);
    QAction *makeDefault = nullptr;
    if (!isDefault) {
        makeDefault = menu.addAction(tr("Make default"));
    }
    QAction *deleteAct = nullptr;
    if (!isHome) {
        deleteAct = menu.addAction(tr("Delete"));
    }
    if (menu.actions().isEmpty()) return;

    QAction *chosen = menu.exec(m_favoritesList->viewport()->mapToGlobal(pos));
    if (chosen && chosen == makeDefault) {
        setDefaultFavorite(path);
    } else if (chosen && chosen == deleteAct) {
        removeFavorite(path);
    }
}

void FolderBrowserDialog::addCurrentRootAsFavorite()
{
    const QString path = QDir::cleanPath(m_rootPath);
    if (path.isEmpty() || !QDir(path).exists()) return;
    if (path == QDir::cleanPath(QDir::homePath())) return;  // Home is implicit.
    if (m_favoritePaths.contains(path)) return;
    m_favoritePaths.append(path);
    saveFavorites();
    rebuildFavoritesList();
}

void FolderBrowserDialog::removeFavorite(const QString &path)
{
    const QString cleaned = QDir::cleanPath(path);
    if (m_favoritePaths.removeAll(cleaned) > 0) {
        // If we removed the default, fall back to Home (empty marker).
        if (QDir::cleanPath(m_defaultFavorite) == cleaned) {
            m_defaultFavorite.clear();
        }
        saveFavorites();
        rebuildFavoritesList();
    }
}

void FolderBrowserDialog::setDefaultFavorite(const QString &path)
{
    const QString cleaned = QDir::cleanPath(path);
    if (cleaned == QDir::cleanPath(QDir::homePath())) {
        // Home as default → store empty string so deleting a different
        // default falls back to Home automatically.
        m_defaultFavorite.clear();
    } else {
        m_defaultFavorite = cleaned;
    }
    saveFavorites();
    rebuildFavoritesList();
}

void FolderBrowserDialog::loadSettings()
{
    QSettings settings("Maude", "FolderBrowser");
    QString savedRoot = settings.value("rootPath").toString();

    QString rootToUse = (!savedRoot.isEmpty() && QDir(savedRoot).exists())
        ? savedRoot : QDir::homePath();

    // During initialization, just set the path without restarting the scan
    // This prevents killing an in-progress scan when the dialog opens
    m_rootPath = QDir::cleanPath(rootToUse);
    m_currentPath = m_rootPath;

    // Restore show hidden setting (presentational only, see TODO 4).
    m_showHidden = settings.value("showHidden", false).toBool();
    m_showHiddenButton->setChecked(m_showHidden);
    if (m_showHidden) {
        m_showHiddenButton->setToolTip(tr("Hide hidden folders"));
    }
    m_pathSelector->fileSystemAdapter()->setShowHidden(m_showHidden);
    m_searchWorker->setIncludeHidden(m_showHidden);
    if (m_fileSearchWorker) m_fileSearchWorker->setIncludeHidden(m_showHidden);
    // Note: deliberately do NOT call PathCacheManager::setShowHidden — the
    // cache always indexes hidden, so the call would be a no-op anyway,
    // but we drop it for clarity.

    // Restore search mode. Default is "both".
    const QString modeStr = settings.value("searchMode", "both").toString();
    if (modeStr == "folders") {
        m_searchMode = SearchMode::Folders;
        if (m_modeFolders) m_modeFolders->setChecked(true);
    } else if (modeStr == "files") {
        m_searchMode = SearchMode::Files;
        if (m_modeFiles) m_modeFiles->setChecked(true);
    } else {
        m_searchMode = SearchMode::Both;
        if (m_modeBoth) m_modeBoth->setChecked(true);
    }

    // Apply the tree filter now that both Mode + showHidden are known.
    applyTreeViewFilter();

    if (m_contentRegex) {
        m_contentRegex->setChecked(settings.value("contentRegex", false).toBool());
    }

    // Update PathSelector
    m_pathSelector->setPath(m_rootPath);

    // Navigate tree view
    QModelIndex index = m_fileSystemModel->index(m_rootPath);
    m_folderTreeView->setRootIndex(index);
    m_folderTreeView->scrollTo(index);
    updateUpButtonState();

    // Only expand cache if needed (doesn't stop current scan)
    PathCacheManager::instance()->expandTo(m_rootPath);
}
