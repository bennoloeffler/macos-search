#include "FsEventsWatcher.h"

#include <QDir>
#include <QHash>
#include <QMetaObject>

#include <CoreServices/CoreServices.h>
#include <dispatch/dispatch.h>

// One recursive FSEvents stream over a reduced set of root directories.
// See FsEventsWatcher.h for the design rationale (why FSEvents, why
// directory granularity, how callbacks reach the main thread).

namespace {

// FSEvents callback. Runs on the private serial dispatch queue, NOT the
// owning (main) thread. It must only marshal work back to the watcher via a
// queued QMetaObject::invokeMethod — never touch QObject state directly.
void fsEventsCallback(ConstFSEventStreamRef /*streamRef*/,
                      void *clientCallBackInfo,
                      size_t numEvents,
                      void *eventPaths,
                      const FSEventStreamEventFlags eventFlags[],
                      const FSEventStreamEventId /*eventIds*/[])
{
    auto *watcher = static_cast<FsEventsWatcher *>(clientCallBackInfo);
    const char **paths = static_cast<const char **>(eventPaths);

    // Coalesce duplicate paths within this batch. FSEvents frequently reports
    // the same directory several times in one callback; the diff handler only
    // needs to see each changed directory once. If ANY event for a path had
    // the MustScanSubDirs flag, the coalesced entry inherits it.
    QHash<QString, bool> changed;  // path -> mustScanSubDirs
    changed.reserve(static_cast<int>(numEvents));
    for (size_t i = 0; i < numEvents; ++i) {
        const QString path = QString::fromUtf8(paths[i]);
        if (path.isEmpty()) continue;
        const bool mustScan =
            (eventFlags[i] & kFSEventStreamEventFlagMustScanSubDirs) != 0;
        changed[path] = changed.value(path, false) || mustScan;
    }

    for (auto it = changed.constBegin(); it != changed.constEnd(); ++it) {
        QMetaObject::invokeMethod(watcher, "forwardEvent", Qt::QueuedConnection,
                                  Q_ARG(QString, it.key()),
                                  Q_ARG(bool, it.value()));
    }
}

}  // namespace

FsEventsWatcher::FsEventsWatcher(double latencySeconds, QObject *parent)
    : QObject(parent)
    , m_latencySeconds(latencySeconds)
{
}

FsEventsWatcher::~FsEventsWatcher()
{
    stop();
}

void FsEventsWatcher::setRoots(const QStringList &roots)
{
    const QStringList reduced = reduceRoots(roots);

    // No-op if the reduced set is unchanged and a stream is already running:
    // tearing down and recreating a live stream would drop the in-flight
    // coalescing window and could miss events.
    if (reduced == m_roots && m_stream) {
        return;
    }

    stop();

    m_roots = reduced;
    if (m_roots.isEmpty()) {
        return;
    }

    CFMutableArrayRef cfPaths =
        CFArrayCreateMutable(nullptr, m_roots.size(), &kCFTypeArrayCallBacks);
    for (const QString &r : m_roots) {
        const QByteArray utf8 = r.toUtf8();
        CFStringRef s = CFStringCreateWithCString(nullptr, utf8.constData(),
                                                  kCFStringEncodingUTF8);
        CFArrayAppendValue(cfPaths, s);
        CFRelease(s);
    }

    FSEventStreamContext ctx = {0, this, nullptr, nullptr, nullptr};
    // Directory granularity on purpose: NO kFSEventStreamCreateFlagFileEvents.
    // NoDefer delivers the first event in a burst promptly, then coalesces the
    // rest within the latency window.
    FSEventStreamRef stream = FSEventStreamCreate(
        nullptr, &fsEventsCallback, &ctx, cfPaths, kFSEventStreamEventIdSinceNow,
        m_latencySeconds, kFSEventStreamCreateFlagNoDefer);
    CFRelease(cfPaths);

    if (!stream) {
        m_roots.clear();
        return;
    }

    dispatch_queue_t queue = dispatch_queue_create(
        "de.v-und-s.macos-search.fsevents", DISPATCH_QUEUE_SERIAL);
    FSEventStreamSetDispatchQueue(stream, queue);

    if (!FSEventStreamStart(stream)) {
        FSEventStreamInvalidate(stream);
        FSEventStreamRelease(stream);
        dispatch_release(queue);
        m_roots.clear();
        return;
    }

    m_stream = stream;
    m_queue = queue;
}

void FsEventsWatcher::stop()
{
    if (m_stream) {
        auto stream = static_cast<FSEventStreamRef>(m_stream);
        FSEventStreamStop(stream);
        FSEventStreamInvalidate(stream);
        FSEventStreamRelease(stream);
        m_stream = nullptr;
    }
    if (m_queue) {
        dispatch_release(static_cast<dispatch_queue_t>(m_queue));
        m_queue = nullptr;
    }
    m_roots.clear();
}

QStringList FsEventsWatcher::reduceRoots(const QStringList &roots)
{
    // 1. Clean each path, drop empties, de-duplicate.
    QStringList cleaned;
    for (const QString &r : roots) {
        if (r.isEmpty()) continue;
        const QString c = QDir::cleanPath(r);
        if (c.isEmpty()) continue;
        if (!cleaned.contains(c)) cleaned.append(c);
    }

    // 2. Sort so ancestors precede their descendants, then keep only roots
    //    that are not covered by an already-kept ancestor.
    cleaned.sort();
    QStringList result;
    for (const QString &c : cleaned) {
        bool covered = false;
        for (const QString &kept : result) {
            if (c == kept || c.startsWith(kept + QLatin1Char('/'))) {
                covered = true;
                break;
            }
        }
        if (!covered) result.append(c);
    }
    return result;
}

void FsEventsWatcher::forwardEvent(const QString &path, bool mustScanSubDirs)
{
    if (mustScanSubDirs) emit rescanNeeded(path);
    emit directoryChanged(path);
}
