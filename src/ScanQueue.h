#ifndef SCANQUEUE_H
#define SCANQUEUE_H

#include <QDir>
#include <QString>
#include <QStringList>

// Priority order of the startup scan roots — most-probable search targets
// first, so Desktop/Downloads/Documents/Dropbox are searchable within the
// first seconds while the rest of home is still being walked. Pure
// function: existence filtering and Dropbox-dir discovery are the
// caller's job (see resolveScanQueue() in main.cpp).
namespace ScanQueue {

inline QStringList build(const QString &home,
                         const QString &defaultStart,
                         const QStringList &favorites,
                         const QStringList &dropboxDirs)
{
    QStringList ordered;
    auto push = [&ordered](const QString &raw) {
        const QString cleaned = QDir::cleanPath(raw);
        if (cleaned.isEmpty() || ordered.contains(cleaned)) return;
        ordered.append(cleaned);
    };

    push(defaultStart);                      // what the UI opens on
    push(home + QStringLiteral("/Desktop"));
    push(home + QStringLiteral("/Downloads"));
    push(home + QStringLiteral("/Documents"));
    for (const QString &d : dropboxDirs) push(d);
    push(home);                              // the rest of home
    for (const QString &f : favorites) push(f);
    return ordered;
}

}  // namespace ScanQueue

#endif // SCANQUEUE_H
