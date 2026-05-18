#ifndef SEARCHRESULTMODEL_H
#define SEARCHRESULTMODEL_H

#include <QAbstractListModel>
#include <QFileIconProvider>
#include <QString>
#include <QVector>

#include "FolderSearchWorker.h" // SearchResult

class SearchResultModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles {
        PathRole = Qt::UserRole + 1,
    };

    explicit SearchResultModel(QObject *parent = nullptr);

    void setResults(const QList<SearchResult> &results);
    void clear();

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    QString pathAt(int row) const;

private:
    QVector<SearchResult> m_results;
    mutable QFileIconProvider m_iconProvider;
};

#endif // SEARCHRESULTMODEL_H
