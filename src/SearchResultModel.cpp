#include "SearchResultModel.h"

#include <QDir>
#include <QFileInfo>

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
        return m_iconProvider.icon(QFileInfo(r.path));
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
