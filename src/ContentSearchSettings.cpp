#include "ContentSearchSettings.h"
#include "FileCacheManager.h"

#include <QFileInfo>
#include <QReadLocker>
#include <QSettings>
#include <QWriteLocker>

ContentSearchSettings::ContentSearchSettings(QObject *parent)
    : QObject(parent)
{
    load();
}

int ContentSearchSettings::threshold() const
{
    QReadLocker l(&m_lock);
    return m_threshold;
}

void ContentSearchSettings::setThreshold(int value)
{
    if (value < minThreshold()) value = minThreshold();
    if (value > maxThreshold()) value = maxThreshold();
    bool changed = false;
    {
        QWriteLocker l(&m_lock);
        if (m_threshold != value) {
            m_threshold = value;
            changed = true;
        }
    }
    if (changed) {
        save();
        emit settingsChanged();
    }
}

int ContentSearchSettings::maxFileSizeMB() const
{
    QReadLocker l(&m_lock);
    return m_maxFileSizeMB;
}

void ContentSearchSettings::setMaxFileSizeMB(int value)
{
    if (value < 1) value = 1;
    bool changed = false;
    {
        QWriteLocker l(&m_lock);
        if (m_maxFileSizeMB != value) {
            m_maxFileSizeMB = value;
            changed = true;
        }
    }
    if (changed) {
        save();
        emit settingsChanged();
    }
}

int ContentSearchSettings::defaultFileCacheCap()
{
    return FileCacheManager::kDefaultSoftCap;
}

int ContentSearchSettings::fileCacheCap() const
{
    QReadLocker l(&m_lock);
    return m_fileCacheCap;
}

void ContentSearchSettings::setFileCacheCap(int value)
{
    if (value < 1000) value = 1000;
    bool changed = false;
    {
        QWriteLocker l(&m_lock);
        if (m_fileCacheCap != value) {
            m_fileCacheCap = value;
            changed = true;
        }
    }
    if (changed) {
        save();
        emit settingsChanged();
    }
}

QStringList ContentSearchSettings::extensionBlacklist() const
{
    QReadLocker l(&m_lock);
    QStringList out = QStringList(m_extBlacklist.begin(), m_extBlacklist.end());
    out.sort();
    return out;
}

void ContentSearchSettings::setExtensionBlacklist(const QStringList &exts)
{
    bool changed = false;
    {
        QWriteLocker l(&m_lock);
        QSet<QString> nowSet;
        for (const QString &e : exts) {
            QString trimmed = e.trimmed().toLower();
            if (trimmed.startsWith('.')) trimmed.remove(0, 1);
            if (!trimmed.isEmpty()) nowSet.insert(trimmed);
        }
        if (m_extBlacklist != nowSet) {
            m_extBlacklist = nowSet;
            changed = true;
        }
    }
    if (changed) {
        save();
        emit settingsChanged();
    }
}

QStringList ContentSearchSettings::defaultExtensionBlacklist()
{
    return QStringList{
        "png", "jpg", "jpeg", "gif", "webp", "heic", "heif",
        "mp4", "mov", "m4a", "mp3", "wav", "ogg", "flac",
        "zip", "gz", "bz2", "xz", "7z", "tar", "rar",
        "dmg", "pkg", "ipa", "iso",
        "dylib", "so", "o", "a", "class", "pyc", "pyo", "wasm", "bin", "dat",
        "key", "keychain"
    };
}

bool ContentSearchSettings::isExtensionBlacklisted(const QString &absolutePath) const
{
    QString ext = QFileInfo(absolutePath).suffix().toLower();
    if (ext.isEmpty()) return false;
    QReadLocker l(&m_lock);
    return m_extBlacklist.contains(ext);
}

void ContentSearchSettings::resetToDefaults()
{
    {
        QWriteLocker l(&m_lock);
        m_threshold = defaultThreshold();
        m_maxFileSizeMB = defaultMaxFileSizeMB();
        m_fileCacheCap = defaultFileCacheCap();
        const QStringList defaults = defaultExtensionBlacklist();
        m_extBlacklist = QSet<QString>(defaults.begin(), defaults.end());
    }
    save();
    emit settingsChanged();
}

void ContentSearchSettings::load()
{
    QSettings settings;
    settings.beginGroup("ContentSearchSettings");

    QWriteLocker l(&m_lock);
    m_threshold = settings.value("threshold", defaultThreshold()).toInt();
    if (m_threshold < minThreshold()) m_threshold = minThreshold();
    if (m_threshold > maxThreshold()) m_threshold = maxThreshold();

    m_maxFileSizeMB = settings.value("maxFileSizeMB", defaultMaxFileSizeMB()).toInt();
    if (m_maxFileSizeMB < 1) m_maxFileSizeMB = 1;

    m_fileCacheCap = settings.value("fileCacheCap", defaultFileCacheCap()).toInt();
    if (m_fileCacheCap < 1000) m_fileCacheCap = 1000;
    // Migration: 500000 was the hardcoded default until 2026-07-21 and got
    // persisted by any save(); treat it as "never chosen" and follow the
    // (much higher) current default instead of truncating the file index.
    if (m_fileCacheCap == 500000) m_fileCacheCap = defaultFileCacheCap();

    if (settings.contains("extensionBlacklist")) {
        const QStringList stored = settings.value("extensionBlacklist").toStringList();
        m_extBlacklist.clear();
        for (const QString &e : stored) {
            QString trimmed = e.trimmed().toLower();
            if (trimmed.startsWith('.')) trimmed.remove(0, 1);
            if (!trimmed.isEmpty()) m_extBlacklist.insert(trimmed);
        }
    } else {
        const QStringList defaults = defaultExtensionBlacklist();
        m_extBlacklist = QSet<QString>(defaults.begin(), defaults.end());
    }
    settings.endGroup();
}

void ContentSearchSettings::save()
{
    int threshold, maxSize, cap;
    QStringList exts;
    {
        QReadLocker l(&m_lock);
        threshold = m_threshold;
        maxSize = m_maxFileSizeMB;
        cap = m_fileCacheCap;
        exts = QStringList(m_extBlacklist.begin(), m_extBlacklist.end());
        exts.sort();
    }
    QSettings settings;
    settings.beginGroup("ContentSearchSettings");
    settings.setValue("threshold", threshold);
    settings.setValue("maxFileSizeMB", maxSize);
    settings.setValue("fileCacheCap", cap);
    settings.setValue("extensionBlacklist", exts);
    settings.endGroup();
    settings.sync();
}
