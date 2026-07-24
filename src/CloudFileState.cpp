#include "CloudFileState.h"

#include <QFile>
#include <QLocale>

#include <sys/stat.h>

// Present in the macOS SDK since 10.15; guard for safety.
#ifndef SF_DATALESS
#define SF_DATALESS 0x40000000
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
    const bool noLocalBlocks = (st.st_blocks == 0 && st.st_size > 0);
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
