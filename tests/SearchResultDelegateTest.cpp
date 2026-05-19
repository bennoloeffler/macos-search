#include "SearchResultDelegateTest.h"
#include "SearchResultDelegate.h"

#include <QListWidget>
#include <QStyleOptionViewItem>
#include <QtTest/QtTest>

namespace {
using D = SearchResultDelegate;
}

void SearchResultDelegateTest::testEncodeDecodeRangesRoundTrip()
{
    QList<QPair<int, int>> in{ {1, 4}, {7, 10}, {12, 15} };
    const QString encoded = D::encodeRanges(in);
    const auto back = D::decodeRanges(encoded);
    QCOMPARE(back, in);
}

void SearchResultDelegateTest::testDecodeEmpty()
{
    QVERIFY(D::decodeRanges(QString()).isEmpty());
}

void SearchResultDelegateTest::testDecodeGarbageIsTolerated()
{
    // Malformed segments are silently dropped.
    const auto out = D::decodeRanges("oops,5:10,bad:also,3:7");
    QCOMPARE(out.size(), 2);
    QCOMPARE(out.at(0), qMakePair(5, 10));
    QCOMPARE(out.at(1), qMakePair(3, 7));
}

void SearchResultDelegateTest::testSizeHintParentRowFixedHeight()
{
    QListWidget list;
    auto *delegate = new D(&list);
    list.setItemDelegate(delegate);
    auto *item = new QListWidgetItem(&list);
    item->setData(D::PathRole, "/Users/me/projects/foo");
    item->setData(D::KindRole, static_cast<int>(D::Kind::File));
    item->setData(D::ScoreRole, 50);

    QStyleOptionViewItem opt;
    opt.font = list.font();
    QSize hint = delegate->sizeHint(opt, list.indexFromItem(item));
    QCOMPARE(hint.height(), D::kParentHeight);
    QVERIFY(hint.width() > 0);
}

void SearchResultDelegateTest::testSizeHintChildRowFixedHeight()
{
    QListWidget list;
    auto *delegate = new D(&list);
    list.setItemDelegate(delegate);
    auto *item = new QListWidgetItem(&list);
    item->setData(D::KindRole, static_cast<int>(D::Kind::ContentLine));
    item->setData(D::SnippetRole, "matched line text");
    item->setData(D::LineRole, 42);

    QStyleOptionViewItem opt;
    opt.font = list.font();
    QSize hint = delegate->sizeHint(opt, list.indexFromItem(item));
    QCOMPARE(hint.height(), D::kChildHeight);
}

void SearchResultDelegateTest::testSizeHintEmptyRowFixedHeight()
{
    QListWidget list;
    auto *delegate = new D(&list);
    list.setItemDelegate(delegate);
    auto *item = new QListWidgetItem(&list);
    item->setData(D::KindRole, static_cast<int>(D::Kind::Empty));
    item->setData(D::PathRole, "No results found");

    QStyleOptionViewItem opt;
    opt.font = list.font();
    QSize hint = delegate->sizeHint(opt, list.indexFromItem(item));
    QCOMPARE(hint.height(), D::kEmptyHeight);
}

void SearchResultDelegateTest::testNaturalWidthMonotonicInPathLength()
{
    QListWidget list;
    auto *delegate = new D(&list);
    list.setItemDelegate(delegate);

    auto *shortItem = new QListWidgetItem(&list);
    shortItem->setData(D::PathRole, "/a/b");
    shortItem->setData(D::KindRole, static_cast<int>(D::Kind::Folder));

    auto *longItem = new QListWidgetItem(&list);
    longItem->setData(D::PathRole,
        "/Users/benno/VundS Dropbox/VundS/A_Leistungsteams/much/longer/path/than/the/short/one");
    longItem->setData(D::KindRole, static_cast<int>(D::Kind::Folder));

    const int wShort = delegate->naturalWidth(shortItem);
    const int wLong = delegate->naturalWidth(longItem);
    QVERIFY(wLong > wShort);
}

void SearchResultDelegateTest::testDelegateInstallsOnList()
{
    QListWidget list;
    auto *d = new D(&list);
    list.setItemDelegate(d);
    QCOMPARE(qobject_cast<D *>(list.itemDelegate()), d);
}

void SearchResultDelegateTest::testRowsRenderViaDelegateNotItemWidget()
{
    // Sanity check: after the delegate refactor, the list must not be
    // using setItemWidget on rows (which was the slow path). For each row
    // we add, itemWidget(item) should be null — paint goes through the
    // delegate instead.
    QListWidget list;
    list.setItemDelegate(new D(&list));
    auto *item = new QListWidgetItem(&list);
    item->setData(D::PathRole, "/x");
    item->setData(D::KindRole, static_cast<int>(D::Kind::Folder));
    item->setSizeHint(QSize(200, D::kParentHeight));
    QCOMPARE(list.itemWidget(item), nullptr);
}
