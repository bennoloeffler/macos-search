#include "FileSystemAdapter.h"
#include <QDir>
#include <QFileInfo>

FileSystemAdapter::FileSystemAdapter(QObject *parent)
    : QObject(parent)
{
}

bool FileSystemAdapter::isValidDirectory(const QString &path) const
{
    if (path.isEmpty()) {
        return false;
    }

    QString expandedPath = expandTilde(path);
    QFileInfo info(expandedPath);
    return info.exists() && info.isDir();
}

QStringList FileSystemAdapter::listSubdirectories(const QString &path) const
{
    QString expandedPath = expandTilde(path);
    QDir dir(expandedPath);

    if (!dir.exists()) {
        return {};
    }

    QStringList result;
    QDir::Filters filters = QDir::Dirs | QDir::NoDotAndDotDot;
    if (m_showHidden) {
        filters |= QDir::Hidden;
    }
    const auto entries = dir.entryInfoList(filters, QDir::Name | QDir::IgnoreCase);

    for (const QFileInfo &entry : entries) {
        result.append(entry.fileName());
    }

    return result;
}

QString FileSystemAdapter::expandTilde(const QString &path) const
{
    if (path.startsWith(QLatin1Char('~'))) {
        return homePath() + path.mid(1);
    }
    return path;
}

QString FileSystemAdapter::homePath() const
{
    return QDir::homePath();
}

QStringList FileSystemAdapter::filterCompletions(const QString &basePath,
                                                  const QString &prefix) const
{
    QString expandedBase = expandTilde(basePath);

    if (!isValidDirectory(expandedBase)) {
        return {};
    }

    QStringList subdirs = listSubdirectories(expandedBase);
    QStringList prefixMatches;
    QStringList containsMatches;

    QString lowerPrefix = prefix.toLower();

    for (const QString &subdir : subdirs) {
        QString lowerSubdir = subdir.toLower();

        if (lowerSubdir.startsWith(lowerPrefix)) {
            // Prefix match - highest priority
            QString fullPath = expandedBase;
            if (!fullPath.endsWith(QLatin1Char('/'))) {
                fullPath += QLatin1Char('/');
            }
            fullPath += subdir;
            prefixMatches.append(fullPath);
        } else if (lowerSubdir.contains(lowerPrefix)) {
            // Contains match - secondary priority
            QString fullPath = expandedBase;
            if (!fullPath.endsWith(QLatin1Char('/'))) {
                fullPath += QLatin1Char('/');
            }
            fullPath += subdir;
            containsMatches.append(fullPath);
        }
    }

    // Combine: prefix matches first, then contains matches
    QStringList result = prefixMatches + containsMatches;
    return result;
}

bool FileSystemAdapter::createDirectory(const QString &path) const
{
    if (path.isEmpty()) {
        return false;
    }

    QString expandedPath = expandTilde(path);
    return QDir().mkpath(expandedPath);
}

void FileSystemAdapter::parsePath(const QString &fullPath,
                                   QString &basePath,
                                   QString &partialName) const
{
    QString path = expandTilde(fullPath);

    if (path.isEmpty()) {
        basePath.clear();
        partialName.clear();
        return;
    }

    // Handle trailing slash - base is the path, partial is empty
    if (path.endsWith(QLatin1Char('/'))) {
        basePath = path.chopped(1);  // Remove trailing slash
        partialName.clear();
        return;
    }

    // Find last slash
    qsizetype lastSlash = path.lastIndexOf(QLatin1Char('/'));
    if (lastSlash < 0) {
        // No slash found - entire thing is partial name
        basePath.clear();
        partialName = path;
        return;
    }

    // Split at last slash
    if (lastSlash == 0) {
        // Root directory case: "/foo" -> base="/", partial="foo"
        basePath = QStringLiteral("/");
    } else {
        basePath = path.left(lastSlash);
    }
    partialName = path.mid(lastSlash + 1);
}
