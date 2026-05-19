#ifndef RIPGREPRUNNER_H
#define RIPGREPRUNNER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>

class QProcess;

// Represents one matched line emitted by ripgrep.
//
// Note: ContentMatch is registered with Qt's metatype system at runtime via
// qRegisterMetaType<>() inside RipgrepRunner's constructor. No
// Q_DECLARE_METATYPE here, because moc-generated code may instantiate
// QMetaTypeId<...> before this header is read.
struct ContentMatch {
    QString filePath;
    int lineNumber = 0;
    QString snippet;     // trimmed line text, max 200 chars
    int matchStart = -1; // byte offset of matched span within snippet (after trim)
    int matchEnd = -1;
};

// Drives a `rg --json` subprocess and streams matches back via signals.
//
// Binary resolution order:
//   1. RipgrepRunner::overrideBinary() (test seam)
//   2. App-bundle copy at <bundle>/Contents/Resources/rg
//   3. `which rg` on $PATH
//   4. Common Homebrew locations (/opt/homebrew/bin/rg, /usr/local/bin/rg)
//
// `find()` returns empty string when no binary is available.
class RipgrepRunner : public QObject
{
    Q_OBJECT

public:
    explicit RipgrepRunner(QObject *parent = nullptr);
    ~RipgrepRunner() override;

    // Start a search. Cancels any in-flight search first.
    //   query        — the search pattern (literal text or regex)
    //   files        — absolute paths to search
    //   useRegex     — false means -F (fixed-string mode)
    //   maxFileSizeMB — passed as --max-filesize
    //   maxMatchesPerFile — passed as --max-count
    // Returns true if a process started.
    bool start(const QString &query,
               const QStringList &files,
               bool useRegex,
               int maxFileSizeMB,
               int maxMatchesPerFile = 20);

    // Kill any in-flight process. Idempotent.
    void cancel();

    bool isRunning() const;

    // Static binary resolution. Returns absolute path or empty string.
    static QString findBinary();

    // Test seam: override the binary location. Pass empty string to clear.
    static void setBinaryOverride(const QString &path);

    // Parse a single line of ripgrep --json output into zero-or-more matches.
    // Public for testability.
    static QList<ContentMatch> parseJsonLine(const QString &line);

signals:
    void matchesReady(const QList<ContentMatch> &matches);
    void finished(int totalMatches);
    void errorOccurred(const QString &message);

private slots:
    void onReadyReadStandardOutput();
    void onProcessFinished(int exitCode);
    void onProcessErrorOccurred();

private:
    QProcess *m_process = nullptr;
    QByteArray m_stdoutBuffer;
    int m_totalMatches = 0;
};

#endif // RIPGREPRUNNER_H
