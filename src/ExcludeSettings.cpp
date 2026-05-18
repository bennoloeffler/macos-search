#include "ExcludeSettings.h"

#include <QReadLocker>
#include <QSettings>
#include <QWriteLocker>

// Settings backend: Qt's default QSettings constructor uses the per-user
// location keyed by the application org/app name (set in main()).
// Drift vs. maude-cp-v3: upstream routes through MaudeConfig::settingsFilePath();
// here we use QSettings' default location. Same on-disk INI format.
//
// Threading: all member access is guarded by m_lock.
// PathCacheManager::scanWorker() calls shouldExclude() concurrently from
// N worker threads; the user can simultaneously mutate the patterns via
// ExcludeSettingsDialog on the main thread. Without locking, QSet
// iteration in shouldExclude() crashed.
//
// Pattern: writers take a QWriteLocker (exclusive); readers take a
// QReadLocker (shared). Both RAII-release on scope exit.

ExcludeSettings::ExcludeSettings(QObject *parent)
    : QObject(parent)
{
    load();
}

QStringList ExcludeSettings::allPatterns() const
{
    QReadLocker locker(&m_lock);
    return m_patterns;
}

QStringList ExcludeSettings::enabledPatterns() const
{
    QReadLocker locker(&m_lock);
    QStringList enabled;
    for (const QString &pattern : m_patterns) {
        if (m_enabledPatterns.contains(pattern)) {
            enabled.append(pattern);
        }
    }
    return enabled;
}

bool ExcludeSettings::isPatternEnabled(const QString &pattern) const
{
    QReadLocker locker(&m_lock);
    return m_enabledPatterns.contains(pattern);
}

void ExcludeSettings::addPattern(const QString &pattern)
{
    QString trimmed = pattern.trimmed();
    bool changed = false;
    {
        QWriteLocker locker(&m_lock);
        if (trimmed.isEmpty() || m_patterns.contains(trimmed)) {
            return;
        }
        m_patterns.append(trimmed);
        m_enabledPatterns.insert(trimmed);
        changed = true;
    }
    if (changed) {
        save();
        emit patternsChanged();
    }
}

void ExcludeSettings::removePattern(const QString &pattern)
{
    bool changed = false;
    {
        QWriteLocker locker(&m_lock);
        if (m_patterns.removeOne(pattern)) {
            m_enabledPatterns.remove(pattern);
            changed = true;
        }
    }
    if (changed) {
        save();
        emit patternsChanged();
    }
}

void ExcludeSettings::setPatternEnabled(const QString &pattern, bool enabled)
{
    bool changed = false;
    {
        QWriteLocker locker(&m_lock);
        if (!m_patterns.contains(pattern)) {
            return;
        }
        if (enabled && !m_enabledPatterns.contains(pattern)) {
            m_enabledPatterns.insert(pattern);
            changed = true;
        } else if (!enabled && m_enabledPatterns.contains(pattern)) {
            m_enabledPatterns.remove(pattern);
            changed = true;
        }
    }
    if (changed) {
        save();
        emit patternsChanged();
    }
}

bool ExcludeSettings::shouldExclude(const QString &folderName) const
{
    // Snapshot the enabled patterns under the read lock, then iterate
    // outside it. This minimises contention (the inside-lock work is just
    // a copy of QSet, ~10-20 elements; case-insensitive comparison runs
    // outside the lock).
    QSet<QString> snapshot;
    {
        QReadLocker locker(&m_lock);
        snapshot = m_enabledPatterns;
    }

    const QString lowerName = folderName.toLower();

    for (const QString &pattern : snapshot) {
        QString lowerPattern = pattern.toLower();

        if (lowerPattern.contains('*')) {
            if (lowerPattern.startsWith('*') && lowerPattern.endsWith('*')) {
                QString mid = lowerPattern.mid(1, lowerPattern.length() - 2);
                if (lowerName.contains(mid)) return true;
            } else if (lowerPattern.startsWith('*')) {
                QString suffix = lowerPattern.mid(1);
                if (lowerName.endsWith(suffix)) return true;
            } else if (lowerPattern.endsWith('*')) {
                QString prefix = lowerPattern.left(lowerPattern.length() - 1);
                if (lowerName.startsWith(prefix)) return true;
            }
        } else {
            if (lowerName == lowerPattern) return true;
        }
    }
    return false;
}

void ExcludeSettings::resetToDefaults()
{
    {
        QWriteLocker locker(&m_lock);
        m_patterns = defaultPatterns();
        m_enabledPatterns.clear();
        for (const QString &pattern : m_patterns) {
            m_enabledPatterns.insert(pattern);
        }
    }
    save();
    emit patternsChanged();
}

QStringList ExcludeSettings::defaultPatterns()
{
    return QStringList{
        "node_modules",
        ".venv",
        "venv",
        "__pycache__",
        ".git",
        "build",
        "dist",
        "out",
        ".gradle",
        ".idea",
        ".vscode",
        "vendor",
        ".tox",
        "target",
        ".cache",
        ".npm",
        ".yarn",
        "*.egg-info"
    };
}

void ExcludeSettings::load()
{
    QSettings settings;
    settings.beginGroup("ExcludeSettings");

    QWriteLocker locker(&m_lock);
    if (settings.contains("patterns")) {
        m_patterns = settings.value("patterns").toStringList();
        QStringList enabled = settings.value("enabledPatterns").toStringList();
        m_enabledPatterns = QSet<QString>(enabled.begin(), enabled.end());
    } else {
        m_patterns = defaultPatterns();
        for (const QString &pattern : m_patterns) {
            m_enabledPatterns.insert(pattern);
        }
    }
    settings.endGroup();
}

void ExcludeSettings::save()
{
    // Snapshot under read lock, then write to QSettings outside the lock.
    // (QSettings is itself thread-safe.)
    QStringList patternsCopy;
    QStringList enabledCopy;
    {
        QReadLocker locker(&m_lock);
        patternsCopy = m_patterns;
        enabledCopy = QStringList(m_enabledPatterns.begin(), m_enabledPatterns.end());
    }

    QSettings settings;
    settings.beginGroup("ExcludeSettings");
    settings.setValue("patterns", patternsCopy);
    settings.setValue("enabledPatterns", enabledCopy);
    settings.endGroup();
    settings.sync();
}
