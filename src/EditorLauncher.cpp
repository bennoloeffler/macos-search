#include "EditorLauncher.h"

#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

namespace {
QString g_override;
}

namespace EditorLauncher {

void setOverride(const QString &path)
{
    g_override = path;
}

QString findVsCode()
{
    if (!g_override.isEmpty()) return g_override;

    const QString fromPath = QStandardPaths::findExecutable("code");
    if (!fromPath.isEmpty()) return fromPath;

    const QString bundlePath =
        QStringLiteral("/Applications/Visual Studio Code.app/Contents/Resources/app/bin/code");
    if (QFileInfo(bundlePath).isExecutable()) return bundlePath;

    return QString();
}

bool isAvailable()
{
    return !findVsCode().isEmpty();
}

bool openAtLine(const QString &file, int line)
{
    if (file.isEmpty()) return false;

    const QString editor = findVsCode();
    if (!editor.isEmpty()) {
        const QString target = (line > 0)
            ? (file + ":" + QString::number(line))
            : file;
        return QProcess::startDetached(editor, { "--goto", target });
    }

    // Fallback: plain `open file` — line number is lost, but the user
    // at least gets the file in their default app.
    return QProcess::startDetached("/usr/bin/open", { file });
}

}  // namespace EditorLauncher
