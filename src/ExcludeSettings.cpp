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
// PathCacheManager::scanWorker() calls shouldExclude()/shouldExcludeFile()
// concurrently from N worker threads; the user can simultaneously mutate the
// patterns via ExcludeSettingsDialog on the main thread. Without locking,
// QSet iteration crashed (2026-05-18 incident).
//
// Pattern: writers take a QWriteLocker (exclusive); readers take a
// QReadLocker (shared). Both RAII-release on scope exit.

ExcludeSettings::ExcludeSettings(QObject *parent)
    : QObject(parent)
{
    load();
}

// ============================================================================
// Folder patterns
// ============================================================================

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

bool ExcludeSettings::matchesGlob(const QString &lowerName, const QString &lowerPattern)
{
    if (lowerPattern.contains('*')) {
        if (lowerPattern.startsWith('*') && lowerPattern.endsWith('*')) {
            QString mid = lowerPattern.mid(1, lowerPattern.length() - 2);
            return lowerName.contains(mid);
        } else if (lowerPattern.startsWith('*')) {
            return lowerName.endsWith(lowerPattern.mid(1));
        } else if (lowerPattern.endsWith('*')) {
            return lowerName.startsWith(lowerPattern.left(lowerPattern.length() - 1));
        }
        return false;
    }
    return lowerName == lowerPattern;
}

bool ExcludeSettings::shouldExclude(const QString &folderName) const
{
    QSet<QString> snapshot;
    {
        QReadLocker locker(&m_lock);
        snapshot = m_enabledPatterns;
    }

    const QString lowerName = folderName.toLower();
    for (const QString &pattern : snapshot) {
        if (matchesGlob(lowerName, pattern.toLower())) return true;
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

// ============================================================================
// File patterns
// ============================================================================

QStringList ExcludeSettings::allFilePatterns() const
{
    QReadLocker locker(&m_lock);
    return m_filePatterns;
}

QStringList ExcludeSettings::enabledFilePatterns() const
{
    QReadLocker locker(&m_lock);
    QStringList enabled;
    for (const QString &pattern : m_filePatterns) {
        if (m_enabledFilePatterns.contains(pattern)) {
            enabled.append(pattern);
        }
    }
    return enabled;
}

bool ExcludeSettings::isFilePatternEnabled(const QString &pattern) const
{
    QReadLocker locker(&m_lock);
    return m_enabledFilePatterns.contains(pattern);
}

void ExcludeSettings::addFilePattern(const QString &pattern)
{
    QString trimmed = pattern.trimmed();
    bool changed = false;
    {
        QWriteLocker locker(&m_lock);
        if (trimmed.isEmpty() || m_filePatterns.contains(trimmed)) {
            return;
        }
        m_filePatterns.append(trimmed);
        m_enabledFilePatterns.insert(trimmed);
        changed = true;
    }
    if (changed) {
        save();
        emit filePatternsChanged();
    }
}

void ExcludeSettings::removeFilePattern(const QString &pattern)
{
    bool changed = false;
    {
        QWriteLocker locker(&m_lock);
        if (m_filePatterns.removeOne(pattern)) {
            m_enabledFilePatterns.remove(pattern);
            changed = true;
        }
    }
    if (changed) {
        save();
        emit filePatternsChanged();
    }
}

void ExcludeSettings::setFilePatternEnabled(const QString &pattern, bool enabled)
{
    bool changed = false;
    {
        QWriteLocker locker(&m_lock);
        if (!m_filePatterns.contains(pattern)) {
            return;
        }
        if (enabled && !m_enabledFilePatterns.contains(pattern)) {
            m_enabledFilePatterns.insert(pattern);
            changed = true;
        } else if (!enabled && m_enabledFilePatterns.contains(pattern)) {
            m_enabledFilePatterns.remove(pattern);
            changed = true;
        }
    }
    if (changed) {
        save();
        emit filePatternsChanged();
    }
}

bool ExcludeSettings::shouldExcludeFile(const QString &fileName) const
{
    QSet<QString> snapshot;
    {
        QReadLocker locker(&m_lock);
        snapshot = m_enabledFilePatterns;
    }

    const QString lowerName = fileName.toLower();
    for (const QString &pattern : snapshot) {
        if (matchesGlob(lowerName, pattern.toLower())) return true;
    }
    return false;
}

void ExcludeSettings::resetFilePatternsToDefaults()
{
    {
        QWriteLocker locker(&m_lock);
        m_filePatterns = defaultFilePatterns();
        m_enabledFilePatterns.clear();
        for (const QString &pattern : m_filePatterns) {
            m_enabledFilePatterns.insert(pattern);
        }
    }
    save();
    emit filePatternsChanged();
}

QStringList ExcludeSettings::defaultFilePatterns()
{
    return QStringList{
        ".DS_Store",
        ".localized",
        "Thumbs.db",
        "desktop.ini",
        "*~",
        "*.swp",
        "*.swo",
        "*.pyc",
        "*.pyo",
        "*.class",
        "*.o",
        "*.a"
    };
}

// ============================================================================
// Persistence
// ============================================================================

void ExcludeSettings::load()
{
    QSettings settings;
    settings.beginGroup("ExcludeSettings");

    QWriteLocker locker(&m_lock);

    // Folder patterns. Prefer the new key; fall back to legacy `patterns` for
    // one-release migration. Default if neither key exists.
    if (settings.contains("folderPatterns")) {
        m_patterns = settings.value("folderPatterns").toStringList();
        QStringList enabled = settings.value("enabledFolderPatterns").toStringList();
        m_enabledPatterns = QSet<QString>(enabled.begin(), enabled.end());
    } else if (settings.contains("patterns")) {
        // Legacy migration path.
        m_patterns = settings.value("patterns").toStringList();
        QStringList enabled = settings.value("enabledPatterns").toStringList();
        m_enabledPatterns = QSet<QString>(enabled.begin(), enabled.end());
    } else {
        m_patterns = defaultPatterns();
        for (const QString &pattern : m_patterns) {
            m_enabledPatterns.insert(pattern);
        }
    }

    // File patterns. New in file-search v1; default if missing.
    if (settings.contains("filePatterns")) {
        m_filePatterns = settings.value("filePatterns").toStringList();
        QStringList enabled = settings.value("enabledFilePatterns").toStringList();
        m_enabledFilePatterns = QSet<QString>(enabled.begin(), enabled.end());
    } else {
        m_filePatterns = defaultFilePatterns();
        for (const QString &pattern : m_filePatterns) {
            m_enabledFilePatterns.insert(pattern);
        }
    }

    settings.endGroup();
}

void ExcludeSettings::save()
{
    // Snapshot under read lock, then write to QSettings outside the lock.
    QStringList folderPatternsCopy;
    QStringList enabledFolderCopy;
    QStringList filePatternsCopy;
    QStringList enabledFileCopy;
    {
        QReadLocker locker(&m_lock);
        folderPatternsCopy = m_patterns;
        enabledFolderCopy = QStringList(m_enabledPatterns.begin(), m_enabledPatterns.end());
        filePatternsCopy = m_filePatterns;
        enabledFileCopy = QStringList(m_enabledFilePatterns.begin(), m_enabledFilePatterns.end());
    }

    QSettings settings;
    settings.beginGroup("ExcludeSettings");
    settings.setValue("folderPatterns", folderPatternsCopy);
    settings.setValue("enabledFolderPatterns", enabledFolderCopy);
    settings.setValue("filePatterns", filePatternsCopy);
    settings.setValue("enabledFilePatterns", enabledFileCopy);

    // Legacy aliases — keep for one release so a downgrade still finds folder rules.
    settings.setValue("patterns", folderPatternsCopy);
    settings.setValue("enabledPatterns", enabledFolderCopy);

    settings.endGroup();
    settings.sync();
}
