#include "SearchResultModel.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QStyle>

namespace {
// Static folder/file icons — NEVER the native QFileIconProvider. That provider
// resolves each path's real icon through macOS LaunchServices/IconServices,
// which (a) touches other apps' data and pops the "would like to access data
// from other apps" TCC prompt, and (b) costs a per-result LaunchServices round
// trip while scrolling. A generic folder/file glyph is all a search list needs.
QIcon folderIcon()
{
    static const QIcon i = QApplication::style()->standardIcon(QStyle::SP_DirIcon);
    return i;
}
QIcon fileIcon()
{
    static const QIcon i = QApplication::style()->standardIcon(QStyle::SP_FileIcon);
    return i;
}
}  // namespace

SearchResultModel::SearchResultModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

void SearchResultModel::setResults(const QList<SearchResult> &results)
{
    beginResetModel();
    m_results = QVector<SearchResult>(results.begin(), results.end());
    endResetModel();
}

void SearchResultModel::clear()
{
    if (m_results.isEmpty()) {
        return;
    }
    beginResetModel();
    m_results.clear();
    endResetModel();
}

int SearchResultModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_results.size();
}

QVariant SearchResultModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_results.size()) {
        return {};
    }
    const SearchResult &r = m_results.at(index.row());

    switch (role) {
    case Qt::DisplayRole: {
        const QFileInfo info(r.path);
        const QString name = info.fileName().isEmpty() ? r.path : info.fileName();
        const QString home = QDir::homePath();
        QString shownPath = r.path;
        if (shownPath.startsWith(home)) {
            shownPath = QStringLiteral("~") + shownPath.mid(home.size());
        }
        return QStringLiteral("%1\n%2").arg(name, shownPath);
    }
    case Qt::ToolTipRole:
        return r.path;
    case Qt::DecorationRole:
        return QFileInfo(r.path).isDir() ? folderIcon() : fileIcon();
    case PathRole:
        return r.path;
    default:
        return {};
    }
}

QString SearchResultModel::pathAt(int row) const
{
    if (row < 0 || row >= m_results.size()) {
        return {};
    }
    return m_results.at(row).path;
}
