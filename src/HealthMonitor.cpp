#include "HealthMonitor.h"

#include "FileCacheManager.h"
#include "MaudeConfig.h"
#include "PathCacheManager.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QThread>
#include <QTimer>

#include <cstdlib>
#include <mach/mach.h>
#include <unistd.h>

HealthMonitor *HealthMonitor::s_instance = nullptr;

namespace {

constexpr int kBeatMs        = 500;    // main-thread heartbeat interval
constexpr int kLogIntervalMs = 1000;   // background log cadence
constexpr qint64 kStallMs    = 3000;   // main-thread stall threshold
constexpr qint64 kMaxLogBytes = 5 * 1024 * 1024;

QString logPath()  { return MaudeConfig::configDir() + QStringLiteral("/health.log"); }

// Current process memory (Activity-Monitor "Memory" = phys_footprint).
struct Mem { double footprintMb = 0; double residentMb = 0; };
Mem currentMem()
{
    Mem m;
    task_vm_info_data_t info;
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO,
                  reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS) {
        m.footprintMb = double(info.phys_footprint) / (1024.0 * 1024.0);
        m.residentMb  = double(info.resident_size) / (1024.0 * 1024.0);
    }
    return m;
}

int threadCount()
{
    thread_act_array_t threads;
    mach_msg_type_number_t n = 0;
    if (task_threads(mach_task_self(), &threads, &n) != KERN_SUCCESS) return -1;
    vm_deallocate(mach_task_self(), vm_address_t(threads), n * sizeof(thread_act_t));
    return int(n);
}

}  // namespace

HealthMonitor *HealthMonitor::instance()
{
    if (!s_instance) s_instance = new HealthMonitor();
    return s_instance;
}

HealthMonitor::HealthMonitor(QObject *parent) : QObject(parent) {}

HealthMonitor::~HealthMonitor() { stop(); }

void HealthMonitor::start()
{
    if (m_running.load()) return;
    QDir().mkpath(MaudeConfig::configDir());
    m_lastBeatMs.store(QDateTime::currentMSecsSinceEpoch());
    m_running.store(true);

    writeLine(QStringLiteral("=== app start · pid %1 · %2 ===")
                  .arg(getpid())
                  .arg(QCoreApplication::applicationFilePath()));

    // Heartbeat on the main thread — goes stale the instant the UI freezes.
    m_beatTimer = new QTimer(this);
    m_beatTimer->setInterval(kBeatMs);
    connect(m_beatTimer, &QTimer::timeout, this, &HealthMonitor::beat);
    m_beatTimer->start();

    // Independent logger thread (survives a frozen main thread).
    m_thread = QThread::create([this]() { runLogger(); });
    m_thread->setObjectName(QStringLiteral("health-monitor"));
    m_thread->start();
}

void HealthMonitor::stop()
{
    if (!m_running.exchange(false)) return;
    if (m_beatTimer) { m_beatTimer->stop(); m_beatTimer->deleteLater(); m_beatTimer = nullptr; }
    if (m_thread) {
        m_thread->wait(3000);
        delete m_thread;
        m_thread = nullptr;
    }
    writeLine(QStringLiteral("=== app stop ==="));
}

void HealthMonitor::beat()
{
    m_lastBeatMs.store(QDateTime::currentMSecsSinceEpoch());
}

void HealthMonitor::runLogger()
{
    // Runs on the background thread. Only reads atomic / lock-free state.
    while (m_running.load()) {
        // Sleep in small slices so stop() is responsive.
        for (int i = 0; i < kLogIntervalMs / 50 && m_running.load(); ++i)
            QThread::msleep(50);
        if (!m_running.load()) break;

        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const qint64 stall = now - m_lastBeatMs.load();
        const Mem mem = currentMem();
        const int threads = threadCount();

        auto *pc = PathCacheManager::instance();
        const bool scanning = pc->isScanning();
        const int folders = pc->folderCount();                     // lock-free
        const int files = FileCacheManager::instance()->fileCount(); // lock-free

        const QString mainState = (stall >= kStallMs)
            ? QStringLiteral("STALLED %1ms").arg(stall)
            : QStringLiteral("ok");

        writeLine(QStringLiteral(
            "mem=%1MB rss=%2MB threads=%3 scan=%4 folders=%5 files=%6 main=%7")
            .arg(mem.footprintMb, 0, 'f', 1)
            .arg(mem.residentMb, 0, 'f', 1)
            .arg(threads)
            .arg(scanning ? QStringLiteral("SCANNING") : QStringLiteral("idle"))
            .arg(folders).arg(files).arg(mainState));

        // Edge-triggered stall handling: on entering a stall, dump the frozen
        // main-thread stack to disk (once), so the hang is debuggable later.
        if (stall >= kStallMs && !m_inStall) {
            m_inStall = true;
            writeLine(QStringLiteral("!!! MAIN THREAD STALLED — capturing sample …"));
            captureStall(stall);
        } else if (stall < kStallMs && m_inStall) {
            m_inStall = false;
            writeLine(QStringLiteral(">>> main thread recovered"));
        }
    }
}

void HealthMonitor::rotateIfLarge()
{
    QFileInfo fi(logPath());
    if (fi.exists() && fi.size() > kMaxLogBytes) {
        QFile::remove(logPath() + QStringLiteral(".old"));
        QFile::rename(logPath(), logPath() + QStringLiteral(".old"));
    }
}

void HealthMonitor::writeLine(const QString &line)
{
    rotateIfLarge();
    QFile f(logPath());
    if (!f.open(QIODevice::Append | QIODevice::Text)) return;
    QTextStream ts(&f);
    ts << QDateTime::currentDateTime().toString(Qt::ISODateWithMs)
       << "  " << line << '\n';
}

void HealthMonitor::captureStall(qint64 stallMs)
{
    // Shell out to `sample` to capture THIS process's stacks while the main
    // thread is frozen. Blocking on the background thread is fine. The output
    // pinpoints exactly where the UI thread is stuck (as it did for the
    // indexNewSubtree recursion hang).
    const QString out = MaudeConfig::configDir()
        + QStringLiteral("/health-stall-%1.txt")
              .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    const QString cmd = QStringLiteral(
        "/usr/bin/sample %1 3 -mayDie -file '%2' >/dev/null 2>&1")
        .arg(getpid()).arg(out);
    std::system(cmd.toUtf8().constData());
    writeLine(QStringLiteral("    stall %1ms → sample written: %2").arg(stallMs).arg(out));
}
