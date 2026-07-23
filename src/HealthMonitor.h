#ifndef HEALTHMONITOR_H
#define HEALTHMONITOR_H

#include <QObject>
#include <QString>
#include <atomic>

class QTimer;
class QThread;

// Always-on self-reflection. The app can freeze (a stuck main thread); when it
// does, nothing on the main thread — including a QTimer log — can run. So this
// is built around TWO independent parts:
//
//   1. A main-thread HEARTBEAT: a QTimer bumps an atomic timestamp every
//      500 ms. If the main thread stalls, the timestamp goes stale.
//   2. A background LOGGER THREAD that never touches the main thread: every
//      ~1 s it appends a health line to ~/.macos-search/health.log — memory,
//      thread count, scan state, counts, and how long the main thread has
//      been stalled. When a stall crosses the threshold it also shells out to
//      `sample` to dump the FROZEN main-thread stack to a health-stall-*.txt
//      file, so a future hang is fully debuggable from disk after the fact.
//
// Reads only lock-free/atomic state (PathCacheManager::isScanning/folderCount,
// FileCacheManager::fileCount) so the logger itself can never block on a lock
// the frozen main thread might hold.
class HealthMonitor : public QObject
{
    Q_OBJECT
public:
    static HealthMonitor *instance();

    // Call once on the main thread after the QApplication exists.
    void start();
    void stop();

private:
    explicit HealthMonitor(QObject *parent = nullptr);
    ~HealthMonitor() override;

    void beat();                       // main-thread heartbeat slot
    void runLogger();                  // background loop
    void writeLine(const QString &line);
    void rotateIfLarge();
    void captureStall(qint64 stallMs); // shells out to `sample` on self

    QTimer *m_beatTimer = nullptr;
    QThread *m_thread = nullptr;
    std::atomic<qint64> m_lastBeatMs{0};
    std::atomic<bool> m_running{false};
    bool m_inStall = false;

    static HealthMonitor *s_instance;
};

#endif // HEALTHMONITOR_H
