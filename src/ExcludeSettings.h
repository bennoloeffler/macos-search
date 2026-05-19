#ifndef EXCLUDESETTINGS_H
#define EXCLUDESETTINGS_H

#include <QObject>
#include <QReadWriteLock>
#include <QSet>
#include <QString>
#include <QStringList>

// Manages exclude patterns for folder AND file search.
// Patterns are stored persistently using QSettings.
//
// STORAGE (since file-search v1)
// ------------------------------
// Two parallel lists live in the same QSettings group:
//   [ExcludeSettings]
//   folderPatterns = ("node_modules", ".git", ...)
//   enabledFolderPatterns = (...)
//   filePatterns = (".DS_Store", "*.pyc", ...)
//   enabledFilePatterns = (...)
//
// Legacy migration: an older release wrote
//   [ExcludeSettings]
//   patterns        = (...)
//   enabledPatterns = (...)
// On load, if `folderPatterns=` is missing but `patterns=` exists, we copy the
// legacy keys into the new folder slot. We also continue to write the legacy
// `patterns` key for one release so a downgrade reads sensible folder rules.
//
// THREADING
// ---------
// Per Qt's threading rule: a QObject's non-thread-safe state must be
// protected when accessed from a thread other than the QObject's owning
// thread. PathCacheManager::scanWorker() — running on background QThreads
// — calls shouldExclude()/shouldExcludeFile() concurrently with main-thread
// mutations. Without a lock, the QSet iteration crashes with SIGSEGV
// (observed in 2026-05-18 crash report).
//
// We protect the four QString containers with a single QReadWriteLock:
// readers hold a shared lock (cheap, allows concurrent reads from many
// workers); writers hold an exclusive lock (rare, only on user action).
//
// shouldExclude()/shouldExcludeFile() are the hot paths — called once per
// folder/file during scan. A QReadWriteLock acquire is ~10ns when uncontended.
class ExcludeSettings : public QObject
{
    Q_OBJECT

public:
    explicit ExcludeSettings(QObject *parent = nullptr);

    // ----- Folder patterns (existing API kept verbatim) -----

    QStringList allPatterns() const;
    QStringList enabledPatterns() const;
    bool isPatternEnabled(const QString &pattern) const;
    void addPattern(const QString &pattern);
    void removePattern(const QString &pattern);
    void setPatternEnabled(const QString &pattern, bool enabled);
    bool shouldExclude(const QString &folderName) const;
    void resetToDefaults();
    static QStringList defaultPatterns();

    // ----- File patterns (new in file-search v1) -----

    QStringList allFilePatterns() const;
    QStringList enabledFilePatterns() const;
    bool isFilePatternEnabled(const QString &pattern) const;
    void addFilePattern(const QString &pattern);
    void removeFilePattern(const QString &pattern);
    void setFilePatternEnabled(const QString &pattern, bool enabled);
    bool shouldExcludeFile(const QString &fileName) const;
    void resetFilePatternsToDefaults();
    static QStringList defaultFilePatterns();

signals:
    void patternsChanged();
    void filePatternsChanged();

private:
    void load();
    void save();
    static bool matchesGlob(const QString &lowerName, const QString &lowerPattern);

    mutable QReadWriteLock m_lock;
    QStringList m_patterns;
    QSet<QString> m_enabledPatterns;
    QStringList m_filePatterns;
    QSet<QString> m_enabledFilePatterns;
};

#endif // EXCLUDESETTINGS_H
