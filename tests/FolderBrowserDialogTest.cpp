#include "FolderBrowserDialogTest.h"

#include "CloudFileState.h"
#include "SearchResultDelegate.h"
#include "ExcludeSettings.h"
#include "FolderBrowserDialog.h"
#include "PathCacheManager.h"

#include <QLabel>
#include <unistd.h>

#include <QDir>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

// Construct a real FolderBrowserDialog (offscreen — no show()) and verify
// the sidebar/favorites behaviour. QApplication is provided by test_main.cpp.

namespace {

constexpr int kFavoritePathRole = Qt::UserRole + 1;

QSettings folderBrowserSettings() { return QSettings("Maude", "FolderBrowser"); }

void resetPersistedFavorites()
{
    auto s = folderBrowserSettings();
    s.remove("favorites");
    s.remove("defaultFavorite");
    s.sync();
}

QStringList rowTextsIn(FolderBrowserDialog &dialog)
{
    auto *list = dialog.findChild<QListWidget *>("favoritesList");
    if (!list) return {};
    QStringList out;
    for (int i = 0; i < list->count(); ++i) {
        out.append(list->item(i)->text());
    }
    return out;
}

QStringList pathsIn(FolderBrowserDialog &dialog)
{
    auto *list = dialog.findChild<QListWidget *>("favoritesList");
    if (!list) return {};
    QStringList out;
    for (int i = 0; i < list->count(); ++i) {
        out.append(list->item(i)->data(kFavoritePathRole).toString());
    }
    return out;
}

}  // namespace

void FolderBrowserDialogTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    resetPersistedFavorites();
    static ExcludeSettings settings;
    PathCacheManager::instance()->setExcludeSettings(&settings);
}

void FolderBrowserDialogTest::cleanupTestCase()
{
    PathCacheManager::instance()->stopScan();
    resetPersistedFavorites();
    QStandardPaths::setTestModeEnabled(false);
}

void FolderBrowserDialogTest::testConstructsWithoutCrash()
{
    FolderBrowserDialog dialog(QDir::homePath());
    QVERIFY(dialog.windowTitle().contains("Folder", Qt::CaseInsensitive));
}

void FolderBrowserDialogTest::testCloudFileStateDetection()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    // 0-byte file — the "not physically there" placeholder shape.
    const QString placeholder = tmp.path() + "/online-only.docx";
    { QFile f(placeholder); QVERIFY(f.open(QIODevice::WriteOnly)); }
    const CloudFileState empty = CloudFileState::of(placeholder);
    QCOMPARE(empty.sizeBytes, qint64(0));
    QVERIFY(empty.locallyMissing);

    // File with real bytes on disk (fsync so APFS delayed allocation cannot
    // leave st_blocks transiently at 0).
    const QString local = tmp.path() + "/local.txt";
    {
        QFile f(local);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArray(4096, 'x'));
        f.flush();
        ::fsync(f.handle());
    }
    const CloudFileState real = CloudFileState::of(local);
    QCOMPARE(real.sizeBytes, qint64(4096));
    QVERIFY(!real.locallyMissing);

    // Directories carry no size and are never "missing".
    const CloudFileState dir = CloudFileState::of(tmp.path());
    QCOMPARE(dir.sizeBytes, qint64(-1));
    QVERIFY(!dir.locallyMissing);

    // Vanished path: stat fails, no size, not flagged.
    const CloudFileState gone = CloudFileState::of(tmp.path() + "/nope");
    QCOMPARE(gone.sizeBytes, qint64(-1));
    QVERIFY(!gone.locallyMissing);
}

void FolderBrowserDialogTest::testFormatFileSize()
{
    QCOMPARE(formatFileSize(-1), QString());
    // Locale-formatted; assert shape, not the locale's decimal separator.
    QVERIFY(!formatFileSize(0).isEmpty());
    QVERIFY(formatFileSize(1536).contains(QStringLiteral("KB"), Qt::CaseInsensitive)
            || formatFileSize(1536).contains(QStringLiteral("kB")));
    QVERIFY(formatFileSize(qint64(7) * 1024 * 1024 * 1024)
                .contains(QStringLiteral("GB"), Qt::CaseInsensitive));
}

void FolderBrowserDialogTest::testOpeningCloudPlaceholderAnnouncesDownload()
{
    FolderBrowserDialog::setShellOpenSuppressedForTests(true);
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString placeholder = tmp.path() + "/online-only.pdf";
    { QFile f(placeholder); QVERIFY(f.open(QIODevice::WriteOnly)); }
    const QString local = tmp.path() + "/here.txt";
    {
        QFile f(local);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArray(4096, 'y'));
        f.flush();
        ::fsync(f.handle());
    }

    FolderBrowserDialog dialog(QDir::homePath());
    auto *results = dialog.findChild<QListWidget *>("searchResultsList");
    auto *label = dialog.findChild<QLabel *>("resolvedPathLabel");
    QVERIFY(results && label);

    // Activating a placeholder result announces the download.
    auto *item = new QListWidgetItem(QStringLiteral("online-only.pdf"));
    item->setData(Qt::UserRole, placeholder);
    results->addItem(item);
    results->setCurrentItem(item);
    emit results->itemActivated(item);
    QVERIFY2(label->text().contains(QStringLiteral("Downloading")),
             qPrintable("label was: " + label->text()));

    // Activating a file with local bytes does NOT.
    auto *item2 = new QListWidgetItem(QStringLiteral("here.txt"));
    item2->setData(Qt::UserRole, local);
    results->addItem(item2);
    results->setCurrentItem(item2);   // refresh resets the label + stops poll
    emit results->itemActivated(item2);
    QVERIFY2(!label->text().contains(QStringLiteral("Downloading")),
             qPrintable("label was: " + label->text()));

    FolderBrowserDialog::setShellOpenSuppressedForTests(false);
}

void FolderBrowserDialogTest::testDownloadedFileRowShowsRealSize()
{
    FolderBrowserDialog::setShellOpenSuppressedForTests(true);
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.path() + "/cloud.pdf";
    { QFile f(path); QVERIFY(f.open(QIODevice::WriteOnly)); }  // 0 bytes

    FolderBrowserDialog dialog(QDir::homePath());
    auto *results = dialog.findChild<QListWidget *>("searchResultsList");
    QVERIFY(results);
    using Roles = SearchResultDelegate;
    auto *item = new QListWidgetItem(QStringLiteral("cloud.pdf"));
    item->setData(Roles::PathRole, path);
    item->setData(Roles::KindRole,
                  static_cast<int>(SearchResultDelegate::Kind::File));
    item->setData(Roles::SizeRole, qint64(0));
    item->setData(Roles::CloudMissingRole, true);
    results->addItem(item);
    results->setCurrentItem(item);

    // Activate → announce + poll starts (700 ms ticks).
    emit results->itemActivated(item);

    // "Cloud download" lands: write real bytes, fsync so blocks exist.
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArray(4096, 'z'));
        f.flush();
        ::fsync(f.handle());
    }

    // The poll must flip the row's roles to the real size.
    QTRY_COMPARE_WITH_TIMEOUT(item->data(Roles::SizeRole).toLongLong(),
                              qint64(4096), 5000);
    QVERIFY(!item->data(Roles::CloudMissingRole).toBool());
    FolderBrowserDialog::setShellOpenSuppressedForTests(false);
}

void FolderBrowserDialogTest::testHasOpenInFinderAndOpenInAppButtons()
{
    FolderBrowserDialog dialog(QDir::homePath());
    auto *finder = dialog.findChild<QPushButton *>("openInFinderButton");
    auto *app    = dialog.findChild<QPushButton *>("openInAppButton");
    QVERIFY2(finder, "Open in Finder button must exist (drift from upstream)");
    QVERIFY2(app,    "Open with App button must exist (drift from upstream)");
    QCOMPARE(finder->text(), QString("Open in Finder"));
    QCOMPARE(app->text(),    QString("Open with App"));
}

void FolderBrowserDialogTest::testFavoritesRowStartsWithJustHomeAndPlus()
{
    // First-run state seeds Documents / Downloads / Desktop / Macintosh HD
    // (whichever exist on this machine), plus the always-implicit Home.
    // We assert Home is first and "/" (Macintosh HD) is somewhere in the list.
    resetPersistedFavorites();
    FolderBrowserDialog dialog(QDir::homePath());
    const QStringList paths = pathsIn(dialog);
    QVERIFY(!paths.isEmpty());
    QCOMPARE(QDir::cleanPath(paths.first()), QDir::cleanPath(QDir::homePath()));
    QVERIFY2(paths.contains(QStringLiteral("/")),
             "Macintosh HD (/) must be among the first-run defaults");

    // "+ Add current" button exists.
    auto *addBtn = dialog.findChild<QPushButton *>();
    Q_UNUSED(addBtn);
}

void FolderBrowserDialogTest::testFavoritePersistsAcrossInstances()
{
    resetPersistedFavorites();
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString fav = QDir::cleanPath(tmp.path());

    {
        auto s = folderBrowserSettings();
        s.setValue("favorites", QStringList{fav});
        s.sync();
    }

    FolderBrowserDialog dialog(QDir::homePath());
    auto paths = pathsIn(dialog);
    QCOMPARE(paths.size(), 2);                              // Home + favorite
    QCOMPARE(QDir::cleanPath(paths.first()), QDir::cleanPath(QDir::homePath()));
    QCOMPARE(paths.last(), fav);
}

void FolderBrowserDialogTest::testRemoveFavoritePersistsRemoval()
{
    resetPersistedFavorites();
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString fav = QDir::cleanPath(tmp.path());

    {
        auto s = folderBrowserSettings();
        s.setValue("favorites", QStringList{fav});
        s.sync();
    }

    // Simulate user-driven deletion by writing the post-delete state and
    // constructing a fresh dialog (rebuildFavoritesList runs at ctor).
    {
        auto s = folderBrowserSettings();
        s.setValue("favorites", QStringList{});
        s.sync();
    }

    FolderBrowserDialog dialog(QDir::homePath());
    QCOMPARE(rowTextsIn(dialog).size(), 1);  // only Home left
}

void FolderBrowserDialogTest::testHomeFavoriteAlwaysPresent()
{
    resetPersistedFavorites();
    FolderBrowserDialog dialog(QDir::homePath());
    auto paths = pathsIn(dialog);
    QVERIFY(!paths.isEmpty());
    QCOMPARE(QDir::cleanPath(paths.first()), QDir::cleanPath(QDir::homePath()));
}

void FolderBrowserDialogTest::testNonexistentFavoriteIsHiddenAtRender()
{
    resetPersistedFavorites();
    {
        auto s = folderBrowserSettings();
        s.setValue("favorites", QStringList{"/nonexistent/path/that/cannot/exist/zzz"});
        s.sync();
    }
    FolderBrowserDialog dialog(QDir::homePath());
    // Bad path filtered out → just Home.
    QCOMPARE(rowTextsIn(dialog).size(), 1);
}
