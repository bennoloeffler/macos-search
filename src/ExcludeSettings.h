#ifndef EXCLUDESETTINGS_H
#define EXCLUDESETTINGS_H

#include <QObject>
#include <QReadWriteLock>
#include <QSet>
#include <QString>
#include <QStringList>

// Manages exclude patterns for folder search.
// Patterns are stored persistently using QSettings.
//
// THREADING
// ---------
// Per Qt's threading rule: a QObject's non-thread-safe state must be
// protected when accessed from a thread other than the QObject's owning
// thread. PathCacheManager::scanWorker() — running on background QThreads
// — calls shouldExclude() concurrently with main-thread mutations via
// ExcludeSettingsDialog (addPattern, setPatternEnabled, resetToDefaults).
// Without a lock, the QSet iteration in shouldExclude crashes with
// SIGSEGV (observed in 2026-05-18 crash report).
//
// We protect the QStringList + QSet with a QReadWriteLock: readers hold
// a shared lock (cheap, allows concurrent reads from many workers);
// writers hold an exclusive lock (rare, only on user action).
//
// shouldExclude() is the hot path — called once per folder during scan.
// A QReadWriteLock acquire is ~10ns when uncontended.
class ExcludeSettings : public QObject
{
    Q_OBJECT

public:
    explicit ExcludeSettings(QObject *parent = nullptr);

    // Get all patterns (both enabled and disabled)
    QStringList allPatterns() const;

    // Get only enabled (checked) patterns for filtering
    QStringList enabledPatterns() const;

    // Check if a pattern is enabled
    bool isPatternEnabled(const QString &pattern) const;

    // Add a new pattern (enabled by default)
    void addPattern(const QString &pattern);

    // Remove a pattern
    void removePattern(const QString &pattern);

    // Enable/disable a pattern
    void setPatternEnabled(const QString &pattern, bool enabled);

    // Check if a folder name matches any enabled exclude pattern.
    // Thread-safe: safe to call from any thread.
    bool shouldExclude(const QString &folderName) const;

    // Reset to defaults
    void resetToDefaults();

    // Get default patterns
    static QStringList defaultPatterns();

signals:
    void patternsChanged();

private:
    void load();
    void save();

    mutable QReadWriteLock m_lock;
    QStringList m_patterns;
    QSet<QString> m_enabledPatterns;
};

#endif // EXCLUDESETTINGS_H
