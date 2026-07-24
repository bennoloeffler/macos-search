#ifndef SEARCHRESULTDELEGATE_H
#define SEARCHRESULTDELEGATE_H

#include <QStyledItemDelegate>

class QListWidget;
class QListWidgetItem;

// Custom-paints rows in the search-results QListWidget.
//
// The previous implementation used QListWidget::setItemWidget(row, QWidget)
// which allocated ~5-10 widgets (HBoxLayout + score QLabel + glyph QLabel +
// path QLabel + optional chip / badge) for every visible result, every
// keystroke. Qt's docs explicitly warn that setItemWidget is "only suitable
// for small numbers of items" — for 200-row lists rebuilt per keystroke it
// was the dominant per-keystroke cost (200ms+ in Debug).
//
// This delegate replaces all of that with QPainter draws on each row's rect.
// Per-row state is stored as item data via QStyledItemDelegate's role
// system — see ResultRoles below for the layout.
class SearchResultDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    enum class Kind : int {
        Folder = 0,
        File = 1,
        ContentLine = 2,
        MoreLine = 3,    // "+ N more matches"
        Empty = 4,       // "No results found"
    };

    enum ResultRoles {
        // PathRole + LineRole reuse Qt::UserRole / Qt::UserRole+1 — kept
        // compatible with existing code that reads them.
        PathRole = Qt::UserRole,
        LineRole = Qt::UserRole + 1,
        KindRole = Qt::UserRole + 2,
        ScoreRole = Qt::UserRole + 3,
        QueryRole = Qt::UserRole + 4,
        ExtRole = Qt::UserRole + 5,
        MatchCountRole = Qt::UserRole + 6,
        SnippetRole = Qt::UserRole + 7,
        SnippetHlStartRole = Qt::UserRole + 8,
        SnippetHlEndRole = Qt::UserRole + 9,
        MoreCountRole = Qt::UserRole + 10,
        // File size (qint64, st_size) + cloud state (bool). Populated once per
        // result row from CloudFileState::of() when the list is (re)built —
        // paint() must NEVER stat (it runs on every hover/scroll repaint).
        SizeRole = Qt::UserRole + 11,
        CloudMissingRole = Qt::UserRole + 12,
    };

    explicit SearchResultDelegate(QObject *parent = nullptr);

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override;

    // Public helper: compute the natural width an item needs to display its
    // content without truncation. Called by FolderBrowserDialog when sizing
    // items so the QListWidget's horizontal scrollbar reflects real content
    // width rather than just viewport width.
    int naturalWidth(QListWidgetItem *item) const;

    // Public helper: parse comma-separated "start:end,start:end" match ranges
    // (used to highlight matches inside path/snippet text). The format is a
    // string so it survives QListWidgetItem::setData (which only stores
    // QVariant-compatible values).
    static QString encodeRanges(const QList<QPair<int, int>> &ranges);
    static QList<QPair<int, int>> decodeRanges(const QString &encoded);

    static constexpr int kParentHeight = 40;
    static constexpr int kChildHeight = 26;
    static constexpr int kEmptyHeight = 36;
};

#endif // SEARCHRESULTDELEGATE_H
