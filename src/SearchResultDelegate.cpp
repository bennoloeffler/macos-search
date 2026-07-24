#include "SearchResultDelegate.h"

#include "CloudFileState.h"
#include "ThemeManager.h"

#include <QApplication>
#include <QFileInfo>
#include <QFontMetrics>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPainter>
#include <QPainterPath>
#include <QStringList>

// ============================================================================
// macOS Big Sur+ visual treatment for the search results list.
//
// Apple HIG principles applied here:
//   - Selection is a subtle accent tint, not a saturated bar. Primary text
//     stays its default color on the selected row (no white-on-purple).
//   - Match highlights are a light accent wash + medium weight, not bold +
//     vivid background. They mark the span without dominating.
//   - Child rows (content matches) are a light surface with a thin 2 px
//     accent border, not a dark band. Snippet text is monospace in the
//     primary color, not white on black.
//   - Badges are small, low-saturation chips. Score "0" is omitted, score
//     "1-100" is a tiny accent pill.
//   - Path text uses Finder's directory/basename hierarchy: parent dirs in
//     secondary gray, the leaf in primary. Adds visual rhythm at zero cost.
//   - Subtle 1 px separators between top-level rows replace zebra striping.
//
// Colors are direct QColor values — we don't route through SwiftUIStyle's
// CSS-rgba helpers here because QColor cannot parse the "rgba(...)" form
// and silently fell back to opaque black (the original "ugly snippet
// band" regression).
// ============================================================================

namespace {

bool isDark() { return ThemeManager::instance()->isDark(); }

// ----- Palette --------------------------------------------------------------

QColor primaryText()    { return isDark() ? QColor(0xE0,0xE0,0xE0) : QColor(0x1d,0x1d,0x1f); }
QColor secondaryText()  { return isDark() ? QColor(255,255,255,140) : QColor(0,0,0,128); }
QColor tertiaryText()   { return isDark() ? QColor(255,255,255,90)  : QColor(0,0,0,90); }
QColor separator()      { return isDark() ? QColor(255,255,255,18)  : QColor(0,0,0,18); }

QColor brand()          { return QColor(0x9E, 0x38, 0xBE); }
QColor brandSoft()      { return QColor(0x9E, 0x38, 0xBE, isDark() ? 60 : 38);  }  // ~15% / 24%
QColor brandSofter()    { return QColor(0x9E, 0x38, 0xBE, isDark() ? 40 : 24);  }  // selection bg
QColor brandWash()      { return QColor(0x9E, 0x38, 0xBE, isDark() ? 28 : 18);  }  // match highlight
QColor brandBorder()    { return QColor(0x9E, 0x38, 0xBE, 180); }                  // 2px accent stripe

QColor rowBgHover()     { return isDark() ? QColor(255,255,255,10) : QColor(0,0,0,8); }
QColor childRowBg()     { return isDark() ? QColor(255,255,255,8)  : QColor(0,0,0,6); }
QColor chipBg()         { return isDark() ? QColor(255,255,255,18) : QColor(0,0,0,12); }

// Online-only / zero-byte size text — Apple systemOrange, darkened for
// legibility on the light theme.
QColor warnOrange()     { return isDark() ? QColor(0xFF,0x9F,0x0A) : QColor(0xE0,0x6A,0x00); }

// ----- Geometry constants ---------------------------------------------------

constexpr int kRowPadLeft  = 14;
constexpr int kRowPadRight = 14;
constexpr int kScoreBadgeW = 24;
constexpr int kScoreBadgeH = 16;
constexpr int kGlyphW      = 16;
// Fixed-width type-indicator column (folder glyph OR a ".ext" chip). Fixed so
// the paths left-align across rows regardless of the chip's own width.
// (User 24.07.: front columns tightened — 52/84/10 → 42/72/6; ~34 px more
// room for the path, ".html" chip and "☁ 999,9 kB" still fit.)
constexpr int kTypeColW    = 42;
// Fixed-width size column, part of the FRONT row (rank / type / size / path).
// Right-aligned inside the column; "☁ 999,9 kB" fits. Never drawn over the
// path — an earlier right-edge floating pill covered long paths (user veto).
constexpr int kSizeColW    = 72;
constexpr int kColumnGap   = 6;
constexpr int kLineNoW     = 60;
constexpr int kChildIndent = 56;
constexpr int kChildBorderInset = 8;
constexpr int kSnippetSidePad = 8;
constexpr int kMatchPad    = 3;
constexpr qreal kCornerR   = 6.0;

// ----- Match-range helper ---------------------------------------------------

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

// ----- Apple-style highlighted text drawing ---------------------------------

struct DrawTextOpts {
    QColor primaryColor;
    QColor secondaryColor;       // used to dim the directory prefix of a path
    QColor matchBg;
    QColor matchFg;
    int dimUntil = 0;            // chars [0, dimUntil) drawn in secondary color
};

// Draw `text` at baseline (x, y). Returns the x just past the last drawn
// character. Highlighted ranges get a subtle background pill, medium font
// weight, and the brand color.
int drawHighlightedText(QPainter *p, int x, int y, const QString &text,
                        const QList<QPair<int, int>> &ranges,
                        const DrawTextOpts &opts)
{
    QFontMetrics fm(p->font());
    int cursor = x;
    int last = 0;

    auto draw = [&](int from, int to, bool hi) {
        if (from >= to) return;
        const QString seg = text.mid(from, to - from);
        const int w = fm.horizontalAdvance(seg);
        if (hi) {
            // Apple-style: rounded subtle accent background, medium weight,
            // brand color foreground. No saturated bar, no bold.
            QRectF bgRect(cursor - kMatchPad, y - fm.ascent() + 1,
                          w + 2 * kMatchPad, fm.height() - 1);
            QPainterPath pp;
            pp.addRoundedRect(bgRect, 4, 4);
            p->fillPath(pp, opts.matchBg);

            QFont weighted = p->font();
            weighted.setWeight(QFont::Medium);
            const QFont prev = p->font();
            p->setFont(weighted);
            p->setPen(opts.matchFg);
            p->drawText(cursor, y, seg);
            p->setFont(prev);
        } else {
            // Dim the prefix portion of paths (everything up to the basename)
            // to give Finder-style visual hierarchy: secondary path,
            // primary basename.
            const bool inDimZone = (from < opts.dimUntil);
            p->setPen(inDimZone ? opts.secondaryColor : opts.primaryColor);
            p->drawText(cursor, y, seg);
        }
        cursor += w;
    };

    for (const auto &r : ranges) {
        draw(last, r.first, false);
        draw(r.first, r.second, true);
        last = r.second;
    }
    draw(last, static_cast<int>(text.length()), false);
    return cursor;
}

// ----- Small chip helpers ---------------------------------------------------

void drawChip(QPainter *p, const QRect &rect, const QColor &bg,
              const QColor &fg, const QString &text,
              QFont::Weight weight = QFont::Medium)
{
    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    p->setPen(Qt::NoPen);
    p->setBrush(bg);
    p->drawRoundedRect(rect, 4, 4);
    QFont f = p->font();
    f.setWeight(weight);
    f.setPointSize(qMax(8, f.pointSize() - 2));
    p->setFont(f);
    p->setPen(fg);
    p->drawText(rect, Qt::AlignCenter, text);
    p->restore();
}

// Split path into (dirPrefix, basename). dirPrefix includes the trailing '/'.
QPair<QString, QString> splitPath(const QString &path)
{
    const int slash = path.lastIndexOf('/');
    if (slash < 0) return { QString(), path };
    return { path.left(slash + 1), path.mid(slash + 1) };
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
    for (const QString &part : parts) {
        const int colon = part.indexOf(':');
        if (colon <= 0) continue;
        bool a, b;
        const int from = part.left(colon).toInt(&a);
        const int to = part.mid(colon + 1).toInt(&b);
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

    QFontMetrics fm(option.font);
    const QString path = index.data(PathRole).toString();
    const QString snippet = index.data(SnippetRole).toString();
    const QString text = !snippet.isEmpty() ? snippet : path;
    int width = kRowPadLeft + kScoreBadgeW + kColumnGap + kTypeColW + kColumnGap
                + kSizeColW + kColumnGap
                + fm.horizontalAdvance(text) + 100 + kRowPadRight;
    if (kind == Kind::ContentLine) {
        width = kChildIndent + kLineNoW + kSnippetSidePad
                + fm.horizontalAdvance(snippet) + kRowPadRight;
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
    const bool hovered  = (option.state & QStyle::State_MouseOver);

    QRect rect = option.rect;
    p->save();
    p->setClipRect(rect);
    p->setRenderHint(QPainter::Antialiasing, true);
    p->setRenderHint(QPainter::TextAntialiasing, true);

    // -------- background ----------------------------------------------------
    if (kind == Kind::ContentLine) {
        // Child snippet rows: subtle surface tint + thin accent border on
        // the left. NOT a dark band — that was the regression that made
        // the earlier design feel like a terminal embedded in a Mac app.
        QRect bgRect = rect.adjusted(kChildBorderInset, 1,
                                     -kChildBorderInset, -1);
        QPainterPath bg;
        bg.addRoundedRect(bgRect, 4, 4);
        p->fillPath(bg, selected ? brandSofter() : childRowBg());
        QRect border = bgRect;
        border.setWidth(2);
        p->fillRect(border, brandBorder());
    } else if (kind == Kind::MoreLine || kind == Kind::Empty) {
        if (selected) {
            QRect r = rect.adjusted(8, 2, -8, -2);
            QPainterPath pp; pp.addRoundedRect(r, kCornerR, kCornerR);
            p->fillPath(pp, brandSofter());
        }
    } else {
        // Parent rows. Rounded subtle wash on selection / hover, hairline
        // separator on the bottom edge (Apple Finder pattern).
        if (selected) {
            QRect r = rect.adjusted(8, 2, -8, -2);
            QPainterPath pp; pp.addRoundedRect(r, kCornerR, kCornerR);
            p->fillPath(pp, brandSofter());
        } else if (hovered) {
            QRect r = rect.adjusted(8, 2, -8, -2);
            QPainterPath pp; pp.addRoundedRect(r, kCornerR, kCornerR);
            p->fillPath(pp, rowBgHover());
        }
        // Bottom hairline separator — invisible behind the highlighted row,
        // visible between rows. Real macOS lists do this.
        if (!selected) {
            p->setPen(separator());
            const int x1 = rect.left() + kRowPadLeft;
            const int x2 = rect.right() - kRowPadRight;
            const int y  = rect.bottom();
            p->drawLine(x1, y, x2, y);
        }
    }

    p->setFont(option.font);
    QFontMetrics fm(option.font);
    const int baseline = rect.center().y() + (fm.ascent() - fm.descent()) / 2;

    // -------- Empty -----------------------------------------------------
    if (kind == Kind::Empty) {
        QFont f = option.font;
        f.setItalic(true);
        p->setFont(f);
        p->setPen(secondaryText());
        p->drawText(rect.adjusted(kRowPadLeft + 4, 0, -kRowPadRight, 0),
                    Qt::AlignVCenter | Qt::AlignLeft,
                    index.data(PathRole).toString());
        p->restore();
        return;
    }

    // -------- More ------------------------------------------------------
    if (kind == Kind::MoreLine) {
        QFont f = option.font;
        f.setItalic(true);
        p->setFont(f);
        p->setPen(tertiaryText());
        const int n = index.data(MoreCountRole).toInt();
        const QString text = QObject::tr("+ %1 more matches").arg(n);
        p->drawText(rect.adjusted(kChildIndent + kLineNoW + kSnippetSidePad,
                                  0, -kRowPadRight, 0),
                    Qt::AlignVCenter | Qt::AlignLeft, text);
        p->restore();
        return;
    }

    // -------- Content-match child row -----------------------------------
    if (kind == Kind::ContentLine) {
        const int line = index.data(LineRole).toInt();
        const QString snippet = index.data(SnippetRole).toString();
        const int hlStart = index.data(SnippetHlStartRole).toInt();
        const int hlEnd = index.data(SnippetHlEndRole).toInt();

        // Line number — right-aligned dim monospace.
        QFont mono = option.font;
        mono.setFamily(QStringLiteral("SF Mono"));
        if (!QFontInfo(mono).fixedPitch()) mono.setFamily(QStringLiteral("Menlo"));
        mono.setPointSize(qMax(9, option.font.pointSize() - 1));
        p->setFont(mono);
        QFontMetrics monoFm(mono);
        p->setPen(tertiaryText());
        QRect lineNoRect(rect.left() + kChildIndent - kLineNoW + 8, rect.top(),
                         kLineNoW, rect.height());
        p->drawText(lineNoRect, Qt::AlignVCenter | Qt::AlignRight,
                    QString::number(line));

        // Snippet with subtle accent highlight on the matched span.
        QList<QPair<int, int>> ranges;
        if (hlStart >= 0 && hlEnd > hlStart) ranges.append({ hlStart, hlEnd });
        const int snippetX = rect.left() + kChildIndent + kSnippetSidePad;
        const int snippetBaseline = rect.center().y()
                                    + (monoFm.ascent() - monoFm.descent()) / 2;
        DrawTextOpts opts;
        opts.primaryColor = primaryText();
        opts.secondaryColor = primaryText();
        opts.matchBg = brandWash();
        opts.matchFg = brand();
        drawHighlightedText(p, snippetX, snippetBaseline, snippet, ranges, opts);
        p->restore();
        return;
    }

    // -------- Folder / File parent row ----------------------------------
    const QString path = index.data(PathRole).toString();
    const int score = index.data(ScoreRole).toInt();
    const QString query = index.data(QueryRole).toString();
    const QString ext = index.data(ExtRole).toString();
    const int matchCount = index.data(MatchCountRole).toInt();

    int cursorX = rect.left() + kRowPadLeft;

    // Score badge — subtle, only when score > 0.
    if (score > 0) {
        QRect badgeRect(cursorX, rect.center().y() - kScoreBadgeH / 2,
                        kScoreBadgeW, kScoreBadgeH);
        drawChip(p, badgeRect, brandSoft(), brand(), QString::number(score),
                 QFont::DemiBold);
    }
    cursorX += kScoreBadgeW + kColumnGap;

    // Front-row column block after the rank badge: [type] [size] — then the
    // path. All fixed-width so paths left-align across rows.
    //
    // Type indicator — folder glyph for folders, a ".ext" chip for files (or
    // the generic file glyph when a file has no extension). This is the
    // at-a-glance "what kind of thing is this" cue.
    {
        const int typeColX = cursorX;
        if (kind == Kind::File && !ext.isEmpty()) {
            // Extension chip, left-aligned in the column.
            QFont chipFont = option.font;
            chipFont.setPointSize(qMax(8, option.font.pointSize() - 3));
            QFontMetrics chipFm(chipFont);
            const QString chipText = "." + ext;
            const int chipW = qMin(kTypeColW,
                                   chipFm.horizontalAdvance(chipText) + 12);
            QRect chipRect(typeColX, rect.center().y() - 8, chipW, 16);
            const QFont prev = p->font();
            p->setFont(chipFont);
            drawChip(p, chipRect, chipBg(), tertiaryText(), chipText,
                     QFont::Medium);
            p->setFont(prev);
        } else {
            // Folder glyph (or generic file glyph for extensionless files).
            QFont gf = option.font;
            p->setFont(gf);
            p->setPen(secondaryText());
            const QString glyph = (kind == Kind::Folder) ? QStringLiteral("􀈕")
                                                         : QStringLiteral("􀈷");
            // SF Symbols private-use codepoints fall back gracefully to text
            // emoji on systems without the SF Pro Symbols font:
            const QString fallback = (kind == Kind::Folder) ? QStringLiteral("📁")
                                                            : QStringLiteral("📄");
            const bool hasSymbols = QFontMetrics(gf).inFont(glyph.at(0));
            p->drawText(QRect(typeColX, rect.top(), kGlyphW, rect.height()),
                        Qt::AlignVCenter | Qt::AlignLeft,
                        hasSymbols ? glyph : fallback);
            p->setFont(option.font);
        }
    }
    cursorX += kTypeColW + kColumnGap;

    // Size column (files only; folders leave it blank). Right-aligned small
    // text. Online-only / zero-byte files ("not physically there") show in
    // ORANGE with a cloud glyph — the "opening this will download it first"
    // cue. The value comes from SizeRole (st_size is the full logical size
    // even for dataless placeholders); paint never stats.
    if (kind == Kind::File) {
        const QVariant sizeVar = index.data(SizeRole);
        const qint64 sizeBytes = sizeVar.isValid() ? sizeVar.toLongLong() : -1;
        if (sizeBytes >= 0) {
            const bool cloudMissing = index.data(CloudMissingRole).toBool();
            QString text = formatFileSize(sizeBytes);
            if (cloudMissing) text.prepend(QStringLiteral("☁ "));
            QFont sizeFont = option.font;
            sizeFont.setPointSize(qMax(8, option.font.pointSize() - 2));
            const QFont prev = p->font();
            p->setFont(sizeFont);
            p->setPen(cloudMissing ? warnOrange() : tertiaryText());
            p->drawText(QRect(cursorX, rect.top(), kSizeColW, rect.height()),
                        Qt::AlignVCenter | Qt::AlignRight, text);
            p->setFont(prev);
        }
    }
    cursorX += kSizeColW + kColumnGap;

    // Path with directory/basename hierarchy and match highlighting.
    const QList<QPair<int, int>> ranges = matchRangesIn(path, query);
    const auto split = splitPath(path);
    const int dimUntil = static_cast<int>(split.first.length());
    DrawTextOpts opts;
    opts.primaryColor = primaryText();
    opts.secondaryColor = secondaryText();
    opts.matchBg = brandWash();
    opts.matchFg = brand();
    opts.dimUntil = dimUntil;
    cursorX = drawHighlightedText(p, cursorX, baseline, path, ranges, opts);

    // (The extension chip now lives up front as the type indicator.)

    // Match-count badge — right-aligned at the item edge, secondary chip.
    // (The size lives in the front column block — never over the path.)
    if (matchCount > 0) {
        const QString text = (matchCount == 1)
                                ? QObject::tr("1 match")
                                : QObject::tr("%1 matches").arg(matchCount);
        QFont badgeFont = option.font;
        badgeFont.setPointSize(qMax(8, option.font.pointSize() - 2));
        const int w = QFontMetrics(badgeFont).horizontalAdvance(text) + 16;
        QRect badgeRect(rect.right() - kRowPadRight - w,
                        rect.center().y() - 9, w, 18);
        drawChip(p, badgeRect, brandSoft(), brand(), text, QFont::Medium);
    }

    p->restore();
}
