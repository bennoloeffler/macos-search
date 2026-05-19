#include "SearchResultDelegate.h"

#include "SwiftUIStyle.h"

#include <QApplication>
#include <QFileInfo>
#include <QFontMetrics>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPainter>
#include <QStringList>

namespace {

constexpr int kScoreBadgeW = 28;
constexpr int kScoreBadgeH = 18;
constexpr int kGlyphW = 18;
constexpr int kLineNoW = 56;
constexpr int kCountBadgeMinW = 56;
constexpr int kCountBadgeH = 18;
constexpr int kChipPad = 6;

QColor matchBgColor()        { return QColor("#e1bee7"); }
QColor matchFgColor()        { return QColor("#6a1b9a"); }
QColor matchBgSelectedColor(){ return QColor(255, 255, 255, 96); }
QColor matchFgSelectedColor(){ return QColor("#ffffff"); }
QColor selectionColor()      { return QColor(SwiftUIStyle::BrandColor); }
QColor altRowColor()         { return QColor(0, 0, 0, 6); }
QColor scoreBgColor()        { return QColor("#f0d9f5"); }
QColor scoreFgColor()        { return QColor("#6a1b9a"); }
QColor scoreBgSelectedColor(){ return QColor(255, 255, 255, 60); }
QColor scoreFgSelectedColor(){ return QColor("#ffffff"); }
QColor chipBgColor()         { return QColor(SwiftUIStyle::chipBackground()); }
QColor chipFgColor()         { return QColor(SwiftUIStyle::secondaryTextColor()); }
QColor pathFgColor()         { return QColor(SwiftUIStyle::primaryTextColor()); }
QColor pathFgSelectedColor() { return QColor("#ffffff"); }
QColor lineNoFgColor()       { return QColor(SwiftUIStyle::secondaryTextColor()); }
QColor childBgColor()        { return QColor(SwiftUIStyle::secondaryBackground()); }
QColor childBorderColor()    { return QColor(SwiftUIStyle::BrandColor); }

// Find every occurrence (case-insensitive) of each space-separated term
// inside `text`, and return merged sorted ranges of [start, end).
QList<QPair<int, int>> matchRangesIn(const QString &text, const QString &query)
{
    QList<QPair<int, int>> ranges;
    if (query.isEmpty() || text.isEmpty()) return ranges;
    const QString lower = text.toLower();
    const QStringList terms = query.toLower().split(' ', Qt::SkipEmptyParts);
    for (const QString &term : terms) {
        if (term.isEmpty()) continue;
        int pos = 0;
        while ((pos = lower.indexOf(term, pos)) != -1) {
            ranges.append({ pos, pos + static_cast<int>(term.length()) });
            pos += term.length();
        }
    }
    if (ranges.isEmpty()) return ranges;
    std::sort(ranges.begin(), ranges.end());
    QList<QPair<int, int>> merged;
    merged.append(ranges.first());
    for (int i = 1; i < ranges.size(); ++i) {
        auto &last = merged.last();
        if (ranges[i].first <= last.second) {
            last.second = qMax(last.second, ranges[i].second);
        } else {
            merged.append(ranges[i]);
        }
    }
    return merged;
}

// Draw `text` at (x, y baseline) with the given highlight ranges. Returns the
// x offset just past the drawn text.
int drawHighlightedText(QPainter *p, int x, int y, int /*maxW*/,
                        const QString &text,
                        const QList<QPair<int, int>> &ranges,
                        const QColor &fgNormal, const QColor &fgHi,
                        const QColor &bgHi, bool selected)
{
    QFontMetrics fm(p->font());
    int cursor = x;
    int last = 0;
    auto drawSegment = [&](int from, int to, bool hi) {
        if (from >= to) return;
        const QString seg = text.mid(from, to - from);
        const int w = fm.horizontalAdvance(seg);
        if (hi) {
            QRect bgRect(cursor - 1, y - fm.ascent() + 1,
                         w + 2, fm.height() - 2);
            p->fillRect(bgRect, bgHi);
            p->setPen(selected ? matchFgSelectedColor() : fgHi);
            QFont fbold = p->font();
            fbold.setBold(true);
            const QFont prev = p->font();
            p->setFont(fbold);
            p->drawText(cursor, y, seg);
            p->setFont(prev);
        } else {
            p->setPen(fgNormal);
            p->drawText(cursor, y, seg);
        }
        cursor += w;
    };

    for (const auto &r : ranges) {
        drawSegment(last, r.first, /*hi=*/false);
        drawSegment(r.first, r.second, /*hi=*/true);
        last = r.second;
    }
    drawSegment(last, static_cast<int>(text.length()), /*hi=*/false);
    return cursor;
}

void drawRoundedPill(QPainter *p, const QRect &rect, const QColor &bg,
                     const QColor &fg, const QString &text, bool bold = true)
{
    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    p->setPen(Qt::NoPen);
    p->setBrush(bg);
    p->drawRoundedRect(rect, 4, 4);
    QFont f = p->font();
    f.setBold(bold);
    f.setPointSize(qMax(8, f.pointSize() - 2));
    p->setFont(f);
    p->setPen(fg);
    p->drawText(rect, Qt::AlignCenter, text);
    p->restore();
}

}  // anonymous

SearchResultDelegate::SearchResultDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
}

QString SearchResultDelegate::encodeRanges(const QList<QPair<int, int>> &ranges)
{
    QStringList parts;
    parts.reserve(ranges.size());
    for (const auto &r : ranges) {
        parts.append(QString::number(r.first) + ":" + QString::number(r.second));
    }
    return parts.join(',');
}

QList<QPair<int, int>> SearchResultDelegate::decodeRanges(const QString &encoded)
{
    QList<QPair<int, int>> out;
    if (encoded.isEmpty()) return out;
    const QStringList parts = encoded.split(',', Qt::SkipEmptyParts);
    for (const QString &p : parts) {
        const int colon = p.indexOf(':');
        if (colon <= 0) continue;
        bool a, b;
        const int from = p.left(colon).toInt(&a);
        const int to = p.mid(colon + 1).toInt(&b);
        if (a && b) out.append({ from, to });
    }
    return out;
}

QSize SearchResultDelegate::sizeHint(const QStyleOptionViewItem &option,
                                     const QModelIndex &index) const
{
    const Kind kind = static_cast<Kind>(index.data(KindRole).toInt());
    int height = kParentHeight;
    if (kind == Kind::ContentLine || kind == Kind::MoreLine) height = kChildHeight;
    else if (kind == Kind::Empty) height = kEmptyHeight;

    // Width: prefer the natural content width so the list-view contentsSize
    // reflects the actual row width (gives us a horizontal scrollbar when
    // paths exceed the viewport).
    QFontMetrics fm(option.font);
    const QString path = index.data(PathRole).toString();
    const QString snippet = index.data(SnippetRole).toString();
    const QString text = !snippet.isEmpty() ? snippet : path;
    int width = 24 + kScoreBadgeW + 6 + kGlyphW + 6
                + fm.horizontalAdvance(text) + 32;
    if (kind == Kind::ContentLine) {
        width = 64 + kLineNoW + 10 + fm.horizontalAdvance(snippet) + 24;
    }
    return QSize(width, height);
}

int SearchResultDelegate::naturalWidth(QListWidgetItem *item) const
{
    if (!item) return 0;
    QStyleOptionViewItem opt;
    opt.font = QApplication::font();
    return sizeHint(opt, item->listWidget()->indexFromItem(item)).width();
}

void SearchResultDelegate::paint(QPainter *p,
                                 const QStyleOptionViewItem &option,
                                 const QModelIndex &index) const
{
    const Kind kind = static_cast<Kind>(index.data(KindRole).toInt());
    const bool selected = (option.state & QStyle::State_Selected);

    QRect rect = option.rect;
    p->save();
    p->setClipRect(rect);
    p->setRenderHint(QPainter::Antialiasing, true);

    // --- background ----------------------------------------------------------
    if (kind == Kind::ContentLine) {
        // Soft brand-tinted bg + left brand-color border to group child rows
        // under their parent file row.
        p->fillRect(rect, selected ? selectionColor() : childBgColor());
        QRect border = rect; border.setWidth(2);
        p->fillRect(border, childBorderColor());
    } else if (kind == Kind::MoreLine || kind == Kind::Empty) {
        p->fillRect(rect, selected ? selectionColor() : Qt::transparent);
    } else {
        // Parent rows: rounded rect when selected, alt-row stripe otherwise.
        if (selected) {
            QRect r = rect.adjusted(4, 2, -4, -2);
            p->setPen(Qt::NoPen);
            p->setBrush(selectionColor());
            p->drawRoundedRect(r, 6, 6);
        } else if (index.row() % 2 == 1) {
            p->fillRect(rect, altRowColor());
        }
    }

    // --- font baseline -------------------------------------------------------
    p->setFont(option.font);
    QFontMetrics fm(option.font);
    const int baseline = rect.center().y() + (fm.ascent() - fm.descent()) / 2;

    // --- Empty / More content ------------------------------------------------
    if (kind == Kind::Empty) {
        QFont f = option.font;
        f.setItalic(true);
        p->setFont(f);
        p->setPen(QColor(SwiftUIStyle::secondaryTextColor()));
        p->drawText(rect.adjusted(16, 0, -16, 0), Qt::AlignVCenter | Qt::AlignLeft,
                    index.data(PathRole).toString());
        p->restore();
        return;
    }
    if (kind == Kind::MoreLine) {
        QFont f = option.font;
        f.setItalic(true);
        p->setFont(f);
        p->setPen(QColor(SwiftUIStyle::secondaryTextColor()));
        const int n = index.data(MoreCountRole).toInt();
        const QString text = QObject::tr("    + %1 more matches").arg(n);
        p->drawText(rect.adjusted(64, 0, -16, 0),
                    Qt::AlignVCenter | Qt::AlignLeft, text);
        p->restore();
        return;
    }

    // --- Content-match child row --------------------------------------------
    if (kind == Kind::ContentLine) {
        const int line = index.data(LineRole).toInt();
        const QString snippet = index.data(SnippetRole).toString();
        const int hlStart = index.data(SnippetHlStartRole).toInt();
        const int hlEnd = index.data(SnippetHlEndRole).toInt();

        // Line number column (right-aligned, dim, monospace).
        QFont mono = option.font;
        mono.setFamily("SF Mono");
        if (!QFontInfo(mono).fixedPitch()) mono.setFamily("Menlo");
        mono.setPointSize(qMax(9, option.font.pointSize() - 1));
        p->setFont(mono);
        QFontMetrics monoFm(mono);
        p->setPen(lineNoFgColor());
        QRect lineNoRect(rect.left() + 8, rect.top(), kLineNoW, rect.height());
        p->drawText(lineNoRect, Qt::AlignVCenter | Qt::AlignRight,
                    QString::number(line));

        // Snippet with single highlight span.
        QList<QPair<int, int>> ranges;
        if (hlStart >= 0 && hlEnd > hlStart) ranges.append({ hlStart, hlEnd });
        const int snippetX = rect.left() + 8 + kLineNoW + 10;
        const int snippetBaseline = rect.center().y()
                                    + (monoFm.ascent() - monoFm.descent()) / 2;
        drawHighlightedText(p, snippetX, snippetBaseline,
                            rect.right() - snippetX,
                            snippet, ranges,
                            selected ? matchFgSelectedColor() : pathFgColor(),
                            matchFgColor(),
                            selected ? matchBgSelectedColor() : matchBgColor(),
                            selected);
        p->restore();
        return;
    }

    // --- Folder / File row ---------------------------------------------------
    const QString path = index.data(PathRole).toString();
    const int score = index.data(ScoreRole).toInt();
    const QString query = index.data(QueryRole).toString();
    const QString ext = index.data(ExtRole).toString();
    const int matchCount = index.data(MatchCountRole).toInt();

    int cursorX = rect.left() + 12;

    // Score badge — omit when score==0 (rendering "0" was confusing).
    if (score > 0) {
        QRect badgeRect(cursorX, rect.center().y() - kScoreBadgeH / 2,
                        kScoreBadgeW, kScoreBadgeH);
        drawRoundedPill(p, badgeRect,
                        selected ? scoreBgSelectedColor() : scoreBgColor(),
                        selected ? scoreFgSelectedColor() : scoreFgColor(),
                        QString::number(score));
    }
    cursorX += kScoreBadgeW + 6;

    // Glyph (text emoji)
    const QString glyph = (kind == Kind::Folder) ? QStringLiteral("📁")
                                                 : QStringLiteral("📄");
    p->setPen(selected ? pathFgSelectedColor() : pathFgColor());
    p->drawText(QRect(cursorX, rect.top(), kGlyphW, rect.height()),
                Qt::AlignVCenter | Qt::AlignLeft, glyph);
    cursorX += kGlyphW + 6;

    // Path with highlights.
    const QList<QPair<int, int>> ranges = matchRangesIn(path, query);
    cursorX = drawHighlightedText(p, cursorX, baseline,
                                  rect.right() - cursorX,
                                  path, ranges,
                                  selected ? pathFgSelectedColor() : pathFgColor(),
                                  matchFgColor(),
                                  selected ? matchBgSelectedColor() : matchBgColor(),
                                  selected);

    // Extension chip (files only).
    if (kind == Kind::File && !ext.isEmpty()) {
        cursorX += 6;
        QFont chipFont = option.font;
        chipFont.setPointSize(qMax(8, option.font.pointSize() - 3));
        QFontMetrics chipFm(chipFont);
        const QString chipText = "." + ext;
        const int chipW = chipFm.horizontalAdvance(chipText) + 2 * kChipPad;
        QRect chipRect(cursorX, rect.center().y() - kCountBadgeH / 2,
                       chipW, kCountBadgeH);
        drawRoundedPill(p, chipRect, chipBgColor(),
                        selected ? matchFgSelectedColor() : chipFgColor(),
                        chipText, /*bold=*/false);
        cursorX += chipW;
    }

    // Match-count badge (right-aligned).
    if (matchCount > 0) {
        const QString text = QObject::tr("%1 match%2")
                                 .arg(matchCount)
                                 .arg(matchCount == 1 ? "" : "es");
        QFont badgeFont = option.font;
        badgeFont.setPointSize(qMax(8, option.font.pointSize() - 2));
        QFontMetrics badgeFm(badgeFont);
        const int w = qMax(kCountBadgeMinW, badgeFm.horizontalAdvance(text) + 16);
        QRect badgeRect(cursorX + 8, rect.center().y() - kCountBadgeH / 2,
                        w, kCountBadgeH);
        drawRoundedPill(p, badgeRect, selectionColor(),
                        QColor("#ffffff"), text);
    }

    p->restore();
}
