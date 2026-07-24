#include "CloudFileState.h"

#include <QFile>
#include <QLocale>

#include <sys/stat.h>

// Present in the macOS SDK since 10.15; guard for safety.
#ifndef SF_DATALESS
#define SF_DATALESS 0x40000000
#endif
#ifndef UF_COMPRESSED
#define UF_COMPRESSED 0x00000020
#endif

CloudFileState CloudFileState::of(const QString &path)
{
    CloudFileState s;
    struct stat st;
    if (::lstat(QFile::encodeName(path).constData(), &st) != 0) {
        return s;                       // vanished / unreadable: stat failed
    }
    if (!S_ISREG(st.st_mode)) {
        return s;                       // dirs, symlinks: no size shown
    }
    s.sizeBytes = qint64(st.st_size);
    const bool dataless = (st.st_flags & SF_DATALESS) != 0;
    // decmpfs (HFS+/APFS transparent compression) stores the data in an xattr:
    // st_blocks is 0 but the bytes ARE local. Without this exclusion every
    // compressed-but-local file would false-positive as "online-only".
    // (Genuinely evicted iCloud files carry SF_DATALESS — often together with
    // UF_COMPRESSED — and are still caught by the dataless branch.)
    const bool compressed = (st.st_flags & UF_COMPRESSED) != 0;
    const bool noLocalBlocks =
        (st.st_blocks == 0 && st.st_size > 0 && !compressed);
    const bool empty = (st.st_size == 0);
    s.locallyMissing = dataless || noLocalBlocks || empty;
    return s;
}

QString formatFileSize(qint64 bytes)
{
    if (bytes < 0) return QString();
    // SI units (1000-based, "kB/MB/GB") — matches what Finder shows for the
    // same file, so the numbers agree across apps. (Qt's default is IEC KiB.)
    return QLocale::system().formattedDataSize(bytes, 1,
                                               QLocale::DataSizeSIFormat);
}
