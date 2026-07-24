#ifndef CLOUDFILESTATE_H
#define CLOUDFILESTATE_H

#include <QString>

// Local materialization state of a file — detects "online-only" cloud
// placeholders (Dropbox / iCloud Drive / OneDrive / any File Provider).
//
// Detection is ONE lstat() and never touches file content, so it can never
// trigger a download (safe for indexing and for rendering result rows).
// A file counts as locally missing when any of these hold:
//   - SF_DATALESS is set in st_flags (File Provider dataless placeholder;
//     st_size still reports the FULL logical size — show it),
//   - st_blocks == 0 while st_size > 0 (no local data extents — the most
//     robust cross-provider signal),
//   - st_size == 0 (Dropbox-classic placeholders are literally empty files;
//     also covers genuinely empty files, which render identically: "0 bytes
//     in orange" is exactly the user-facing contract).
struct CloudFileState {
    qint64 sizeBytes = -1;        // st_size; -1 = stat failed / not a file
    bool locallyMissing = false;  // true = content not on disk (see above)

    static CloudFileState of(const QString &path);
};

// Locale-aware "0 bytes" / "1,2 KB" / "3,4 MB" / "5,6 GB" (1 decimal).
QString formatFileSize(qint64 bytes);

#endif // CLOUDFILESTATE_H
