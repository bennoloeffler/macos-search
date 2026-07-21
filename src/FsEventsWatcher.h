#ifndef FSEVENTSWATCHER_H
#define FSEVENTSWATCHER_H

#include <QObject>
#include <QString>
#include <QStringList>

// One recursive FSEvents stream over a set of root directories.
//
// WHY FSEVENTS
// ------------
// QFileSystemWatcher on macOS uses one kqueue file descriptor per watched
// directory. With ~200k indexed folders that can't scale (fd limits, memory,
// registration cost). FSEvents is the platform answer: a single stream
// watches an entire subtree recursively, and the kernel/fseventsd coalesce
// changes into one event per changed *directory* — which is exactly the
// granularity PathCacheManager::onDirectoryChanged() diffs at. We therefore
// do NOT pass kFSEventStreamCreateFlagFileEvents.
//
// THREADING
// ---------
// The stream delivers callbacks on a private serial dispatch queue
// (FSEventStreamSetDispatchQueue). The C callback marshals each changed
// directory path to this QObject's owning thread (the main thread) with a
// queued QMetaObject::invokeMethod; consumers only ever see main-thread
// signal emissions. See docs/120_qt_threading.md.
//
// All public methods must be called from the owning (main) thread.
class FsEventsWatcher : public QObject
{
    Q_OBJECT

public:
    // `latencySeconds` is the FSEvents coalescing latency — how long the
    // system may batch changes before delivering a callback. Production
    // uses the 0.5 s default; tests may pass something smaller.
    explicit FsEventsWatcher(double latencySeconds = 0.5,
                             QObject *parent = nullptr);
    ~FsEventsWatcher() override;

    // Replace the watched root set. Nested roots are reduced to their
    // top-level ancestors (one recursive stream covers all descendants).
    // Passing an empty list stops watching entirely.
    void setRoots(const QStringList &roots);

    // Stop and release the stream. setRoots() re-arms.
    void stop();

    // The reduced root set the stream currently covers (empty if stopped).
    QStringList roots() const { return m_roots; }

    // Collapse a root list to top-level entries: cleans each path, drops
    // duplicates and empties, and removes any root that is a descendant of
    // another. Exposed for unit testing.
    static QStringList reduceRoots(const QStringList &roots);

signals:
    // A directory under one of the roots changed (entry added/removed/
    // renamed inside it). Emitted on the owning thread.
    void directoryChanged(const QString &path);

    // FSEvents dropped events (kFSEventStreamEventFlagMustScanSubDirs):
    // everything under `path` must be re-examined, one level is not enough.
    // Emitted in addition to directoryChanged(path).
    void rescanNeeded(const QString &path);

private slots:
    // Landing point for the dispatch-queue callback (queued invocation).
    void forwardEvent(const QString &path, bool mustScanSubDirs);

private:
    friend struct FsEventsWatcherCallback;

    double m_latencySeconds;
    QStringList m_roots;
    void *m_stream = nullptr;  // FSEventStreamRef
    void *m_queue = nullptr;   // dispatch_queue_t (serial)
};

#endif // FSEVENTSWATCHER_H
