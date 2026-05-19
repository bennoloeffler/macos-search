#include "RipgrepRunner.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QProcess>
#include <QStandardPaths>

namespace {
QString g_binaryOverride;
}

RipgrepRunner::RipgrepRunner(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<ContentMatch>("ContentMatch");
    qRegisterMetaType<QList<ContentMatch>>("QList<ContentMatch>");
}

RipgrepRunner::~RipgrepRunner()
{
    cancel();
}

void RipgrepRunner::setBinaryOverride(const QString &path)
{
    g_binaryOverride = path;
}

QString RipgrepRunner::findBinary()
{
    if (!g_binaryOverride.isEmpty()) {
        return g_binaryOverride;
    }

    // 1. App-bundle copy. Each frame in `<bundle>/Contents/Resources/rg`.
    const QString appDir = QCoreApplication::applicationDirPath();
    if (!appDir.isEmpty()) {
        const QString bundleRg = appDir + "/../Resources/rg";
        if (QFileInfo(bundleRg).isExecutable()) {
            return QFileInfo(bundleRg).absoluteFilePath();
        }
    }

    // 2. Standard PATH lookup.
    const QString systemRg = QStandardPaths::findExecutable("rg");
    if (!systemRg.isEmpty()) return systemRg;

    // 3. Common Homebrew locations (covers the case where the test/dev shell
    //    has an unusual PATH).
    for (const QString &candidate : { QStringLiteral("/opt/homebrew/bin/rg"),
                                       QStringLiteral("/usr/local/bin/rg") }) {
        if (QFileInfo(candidate).isExecutable()) return candidate;
    }

    return QString();
}

bool RipgrepRunner::start(const QString &query,
                          const QStringList &files,
                          bool useRegex,
                          int maxFileSizeMB,
                          int maxMatchesPerFile)
{
    cancel();
    if (query.isEmpty() || files.isEmpty()) return false;

    const QString binary = findBinary();
    if (binary.isEmpty()) {
        emit errorOccurred(tr("ripgrep not found"));
        return false;
    }
    // Verify the binary actually exists and is executable before we hand off
    // to QProcess (avoids a double error-emission path on missing binaries).
    if (!QFileInfo(binary).isExecutable()) {
        emit errorOccurred(tr("ripgrep binary not executable: %1").arg(binary));
        return false;
    }

    QStringList args;
    args << "--json"
         << "--max-count" << QString::number(maxMatchesPerFile)
         << "--max-filesize" << (QString::number(maxFileSizeMB) + "M")
         << "--no-config"
         << "--no-messages"
         << "-uu";  // -uu: don't apply .gitignore or hidden filters; we control the list
    if (!useRegex) args << "-F";
    args << "-e" << query << "--";
    // Pass file paths as positional args. macOS argv limit is ~256KB which
    // comfortably fits the 1000-file content-search cap.
    for (const QString &f : files) args << f;

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &RipgrepRunner::onReadyReadStandardOutput);
    connect(m_process,
            static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus) { onProcessFinished(code); });
    connect(m_process, &QProcess::errorOccurred,
            this, &RipgrepRunner::onProcessErrorOccurred);

    m_stdoutBuffer.clear();
    m_totalMatches = 0;

    m_process->start(binary, args);
    if (!m_process->waitForStarted(2000)) {
        emit errorOccurred(tr("ripgrep failed to start"));
        m_process->deleteLater();
        m_process = nullptr;
        return false;
    }
    return true;
}

void RipgrepRunner::cancel()
{
    if (m_process) {
        disconnect(m_process, nullptr, this, nullptr);
        if (m_process->state() != QProcess::NotRunning) {
            m_process->kill();
            m_process->waitForFinished(500);
        }
        m_process->deleteLater();
        m_process = nullptr;
    }
    m_stdoutBuffer.clear();
}

bool RipgrepRunner::isRunning() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}

void RipgrepRunner::onReadyReadStandardOutput()
{
    if (!m_process) return;
    m_stdoutBuffer.append(m_process->readAllStandardOutput());

    // Split by newline and parse complete lines, keep trailing partial.
    int newlineIdx;
    QList<ContentMatch> batch;
    while ((newlineIdx = m_stdoutBuffer.indexOf('\n')) != -1) {
        const QByteArray line = m_stdoutBuffer.left(newlineIdx);
        m_stdoutBuffer.remove(0, newlineIdx + 1);
        if (line.trimmed().isEmpty()) continue;
        const QList<ContentMatch> parsed = parseJsonLine(QString::fromUtf8(line));
        for (const auto &m : parsed) batch.append(m);
    }
    if (!batch.isEmpty()) {
        m_totalMatches += batch.size();
        emit matchesReady(batch);
    }
}

void RipgrepRunner::onProcessFinished(int /*exitCode*/)
{
    // Flush any remaining buffered line.
    onReadyReadStandardOutput();
    emit finished(m_totalMatches);
    if (m_process) {
        m_process->deleteLater();
        m_process = nullptr;
    }
}

void RipgrepRunner::onProcessErrorOccurred()
{
    if (!m_process) return;
    emit errorOccurred(m_process->errorString());
}

QList<ContentMatch> RipgrepRunner::parseJsonLine(const QString &line)
{
    QList<ContentMatch> result;
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError) return result;
    if (!doc.isObject()) return result;

    const QJsonObject root = doc.object();
    if (root.value("type").toString() != "match") return result;

    const QJsonObject data = root.value("data").toObject();
    const QJsonObject pathObj = data.value("path").toObject();
    QString filePath = pathObj.value("text").toString();
    if (filePath.isEmpty()) return result;

    const QJsonObject linesObj = data.value("lines").toObject();
    QString lineText = linesObj.value("text").toString();
    if (lineText.endsWith('\n')) lineText.chop(1);

    int lineNumber = data.value("line_number").toInt();

    int firstMatchStart = -1, firstMatchEnd = -1;
    const QJsonArray submatches = data.value("submatches").toArray();
    if (!submatches.isEmpty()) {
        const QJsonObject sm = submatches.first().toObject();
        firstMatchStart = sm.value("start").toInt(-1);
        firstMatchEnd = sm.value("end").toInt(-1);
    }

    // Truncate snippet at 200 chars, preserve match offsets if possible.
    constexpr int kMaxSnippetChars = 200;
    if (lineText.length() > kMaxSnippetChars) {
        // Center on the match if we know where it is.
        int center = (firstMatchStart >= 0 && firstMatchEnd >= 0)
                         ? (firstMatchStart + firstMatchEnd) / 2
                         : kMaxSnippetChars / 2;
        int start = qMax(0, center - kMaxSnippetChars / 2);
        if (start + kMaxSnippetChars > lineText.length()) {
            start = lineText.length() - kMaxSnippetChars;
        }
        lineText = lineText.mid(start, kMaxSnippetChars);
        if (firstMatchStart >= 0) {
            firstMatchStart -= start;
            firstMatchEnd -= start;
            if (firstMatchStart < 0) firstMatchStart = 0;
            if (firstMatchEnd > lineText.length()) firstMatchEnd = lineText.length();
        }
    }

    ContentMatch m;
    m.filePath = filePath;
    m.lineNumber = lineNumber;
    m.snippet = lineText;
    m.matchStart = firstMatchStart;
    m.matchEnd = firstMatchEnd;
    result.append(m);
    return result;
}
