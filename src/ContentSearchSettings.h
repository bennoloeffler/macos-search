#ifndef CONTENTSEARCHSETTINGS_H
#define CONTENTSEARCHSETTINGS_H

#include <QObject>
#include <QReadWriteLock>
#include <QSet>
#include <QString>
#include <QStringList>

// Settings governing the content-search ("inside files") feature.
//
// Persisted in QSettings under group [ContentSearchSettings].
//
// Threading: same QReadWriteLock pattern as ExcludeSettings.
class ContentSearchSettings : public QObject
{
    Q_OBJECT

public:
    explicit ContentSearchSettings(QObject *parent = nullptr);

    // Threshold below which the content-search field is enabled. Default 1000.
    int threshold() const;
    void setThreshold(int value);
    static int defaultThreshold() { return 1000; }
    static int minThreshold() { return 100; }
    static int maxThreshold() { return 5000; }

    // Skip files larger than this when content-searching (MB). Default 5.
    int maxFileSizeMB() const;
    void setMaxFileSizeMB(int value);
    static int defaultMaxFileSizeMB() { return 5; }

    // File-cache cap (files indexed by name). Default 500_000.
    int fileCacheCap() const;
    void setFileCacheCap(int value);
    static int defaultFileCacheCap() { return 500000; }

    // Lowercased extensions to skip when content-searching ("png", "jpg", ...).
    QStringList extensionBlacklist() const;
    void setExtensionBlacklist(const QStringList &exts);
    static QStringList defaultExtensionBlacklist();

    // Returns true if the file extension is in the blacklist
    // (matched case-insensitively against the dotless extension).
    bool isExtensionBlacklisted(const QString &absolutePath) const;

    void resetToDefaults();

signals:
    void settingsChanged();

private:
    void load();
    void save();

    mutable QReadWriteLock m_lock;
    int m_threshold = defaultThreshold();
    int m_maxFileSizeMB = defaultMaxFileSizeMB();
    int m_fileCacheCap = defaultFileCacheCap();
    QSet<QString> m_extBlacklist;
};

#endif // CONTENTSEARCHSETTINGS_H
